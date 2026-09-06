// Windows pseudo-console backend for PtySession.
//
// The POSIX implementation (pty_session.cpp) is built on forkpty, which has no
// Windows counterpart: there is no fork, and until Windows 10 1809 there was no
// pseudo-terminal either. ConPTY is that counterpart. It gives the child a real
// console attached to a pair of pipes, so a program that asks whether it is on a
// terminal gets "yes" and emits the escape sequences the terminal view already
// knows how to render -- which is the whole reason the REPL uses a pty rather
// than a plain pipe.
//
// Shape differences from the POSIX file, all forced by the platform:
//
//   * Two pipes, not one fd. A pty master is bidirectional; ConPTY takes an
//     input pipe it reads from and an output pipe it writes to.
//   * A reader thread, not a QSocketNotifier. QSocketNotifier watches sockets
//     and fds, and a Win32 pipe HANDLE is neither. Blocking ReadFile on a
//     dedicated thread is the standard answer, and the bytes are marshalled
//     back to the object's thread with a queued invocation.
//   * ResizePseudoConsole, not TIOCSWINSZ.
//   * TerminateProcess, not SIGTERM then SIGKILL. Windows has no graceful
//     signal to send a console child that is not itself a console app.
//
// Requires Windows 10 1809 (build 17763). NTDDI_VERSION is set below rather
// than project-wide so nothing else silently acquires a newer floor.

#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006  // NTDDI_WIN10_RS5
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "repl/pty_session.h"

#include <QDir>
#include <QTimer>

#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>

namespace trowel {
namespace {

// Quote one argument for the CommandLineToArgvW rules CreateProcess parses
// with. Backslashes are literal EXCEPT immediately before a quote, where they
// must be doubled -- the same rule tur_shell_quote applies on the Turmeric
// side, and the reason a trailing `C:\dir\` cannot simply be wrapped in quotes.
QString quoteArg(const QString& arg) {
    if (!arg.isEmpty() && !arg.contains(QLatin1Char(' ')) &&
        !arg.contains(QLatin1Char('\t')) && !arg.contains(QLatin1Char('"'))) {
        return arg;
    }
    QString out = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar c : arg) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"')) {
            out.append(QString(backslashes * 2 + 1, QLatin1Char('\\')));
            out.append(c);
        } else {
            out.append(QString(backslashes, QLatin1Char('\\')));
            out.append(c);
        }
        backslashes = 0;
    }
    out.append(QString(backslashes * 2, QLatin1Char('\\')));
    out.append(QLatin1Char('"'));
    return out;
}

// A block for CreateProcessW: "K=V\0K=V\0\0", built from the current
// environment with the caller's overrides applied on top.
std::vector<wchar_t> buildEnvBlock(const QStringList& extraEnv) {
    QMap<QString, QString> env;
    if (LPWCH block = GetEnvironmentStringsW()) {
        for (LPWCH p = block; *p; ) {
            const QString entry = QString::fromWCharArray(p);
            const int eq = entry.indexOf(QLatin1Char('='), 1);
            if (eq > 0) env.insert(entry.left(eq).toUpper(), entry.mid(eq + 1));
            p += wcslen(p) + 1;
        }
        FreeEnvironmentStringsW(block);
    }
    for (const QString& kv : extraEnv) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        env.insert(kv.left(eq).toUpper(), kv.mid(eq + 1));
    }
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));

    // utf16(), not toWCharArray(): the latter makes gcc -O3 infer an absurd
    // memcpy bound inside qstring.h and trip -Werror=stringop-overflow.  On
    // Windows wchar_t IS UTF-16, so this is a straight copy of a buffer Qt has
    // already NUL-terminated.
    std::vector<wchar_t> out;
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        const QString entry = it.key() + QLatin1Char('=') + it.value();
        const auto* w = reinterpret_cast<const wchar_t*>(entry.utf16());
        out.insert(out.end(), w, w + entry.size());
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

}  // namespace

// Handles live in the header as void* so windows.h stays out of it.
#define HPC_(s)      (reinterpret_cast<HPCON>((s)->hpc_))
#define HIN_(s)      (reinterpret_cast<HANDLE>((s)->in_write_))
#define HOUT_(s)     (reinterpret_cast<HANDLE>((s)->out_read_))
#define HPROC_(s)    (reinterpret_cast<HANDLE>((s)->proc_))

PtySession::PtySession(QObject* parent)
    : QObject(parent) {}

PtySession::~PtySession() {
    terminate();
}

bool PtySession::start(const QString& program, const QStringList& args,
                       const QString& workingDir, const QStringList& extraEnv) {
    if (isRunning()) return false;

    HANDLE inRead = nullptr, inWrite = nullptr;
    HANDLE outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, nullptr, 0) ||
        !CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        emit startFailed(QStringLiteral("CreatePipe failed (%1)").arg(GetLastError()));
        return false;
    }

    HPCON hpc = nullptr;
    const COORD size{80, 24};
    HRESULT hr = CreatePseudoConsole(size, inRead, outWrite, 0, &hpc);
    // ConPTY duplicates the ends it needs; ours are dead weight either way.
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (FAILED(hr)) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        emit startFailed(QStringLiteral("CreatePseudoConsole failed (0x%1)")
                             .arg(static_cast<quint32>(hr), 0, 16));
        return false;
    }

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) ||
        !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hpc, sizeof(hpc), nullptr, nullptr)) {
        ClosePseudoConsole(hpc);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        emit startFailed(QStringLiteral("UpdateProcThreadAttribute failed (%1)")
                             .arg(GetLastError()));
        return false;
    }

    QString cmdline = quoteArg(program);
    for (const QString& a : args) cmdline += QLatin1Char(' ') + quoteArg(a);
    const auto* cmdw = reinterpret_cast<const wchar_t*>(cmdline.utf16());
    std::vector<wchar_t> cmd(cmdw, cmdw + cmdline.size() + 1);  // includes the NUL

    std::vector<wchar_t> envBlock = buildEnvBlock(extraEnv);
    const QString cwd = workingDir.isEmpty() ? QString() : QDir::toNativeSeparators(workingDir);
    std::vector<wchar_t> cwdBuf;
    if (!cwd.isEmpty()) {
        const auto* w = reinterpret_cast<const wchar_t*>(cwd.utf16());
        cwdBuf.assign(w, w + cwd.size() + 1);
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};

    const BOOL ok = CreateProcessW(
        nullptr, cmd.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        envBlock.data(), cwdBuf.empty() ? nullptr : cwdBuf.data(),
        &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(attrs);

    if (!ok) {
        const DWORD err = GetLastError();
        ClosePseudoConsole(hpc);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        emit startFailed(QStringLiteral("CreateProcess failed for '%1' (%2)")
                             .arg(program).arg(err));
        return false;
    }
    CloseHandle(pi.hThread);

    hpc_       = hpc;
    in_write_  = inWrite;
    out_read_  = outRead;
    proc_      = pi.hProcess;
    pid_       = static_cast<long>(pi.dwProcessId);

    // Blocking reads on a dedicated thread; a pipe HANDLE is not something
    // QSocketNotifier can watch. `alive` outlives the thread so a read that
    // returns after teardown cannot touch a destroyed object.
    auto alive = std::make_shared<std::atomic<bool>>(true);
    reader_alive_ = new std::shared_ptr<std::atomic<bool>>(alive);
    HANDLE readFrom = outRead;
    std::thread([this, readFrom, alive]{
        char buf[4096];
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(readFrom, buf, sizeof buf, &n, nullptr) || n == 0) break;
            if (!alive->load()) break;
            QByteArray chunk(buf, static_cast<int>(n));
            QMetaObject::invokeMethod(this, [this, chunk, alive]{
                if (alive->load()) emit dataReceived(chunk);
            }, Qt::QueuedConnection);
        }
    }).detach();

    // Poll for exit, mirroring the POSIX reaper rather than adding a second
    // thread whose only job is to wait.
    auto* reaper = new QTimer(this);
    reaper->setInterval(200);
    connect(reaper, &QTimer::timeout, this, [this, reaper]{
        if (!isRunning()) { reaper->stop(); return; }
        DWORD code = STILL_ACTIVE;
        if (GetExitCodeProcess(HPROC_(this), &code) && code != STILL_ACTIVE) {
            reap();
            emit finished(static_cast<int>(code));
            reaper->stop();
        }
    });
    reaper->start();

    return true;
}

void PtySession::write(const QByteArray& bytes) {
    if (!in_write_ || bytes.isEmpty()) return;
    DWORD off = 0;
    while (off < static_cast<DWORD>(bytes.size())) {
        DWORD n = 0;
        if (!WriteFile(HIN_(this), bytes.constData() + off,
                       static_cast<DWORD>(bytes.size()) - off, &n, nullptr) || n == 0)
            break;
        off += n;
    }
}

void PtySession::resize(int rows, int cols) {
    if (!hpc_) return;
    const COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
    ResizePseudoConsole(HPC_(this), size);
}

void PtySession::terminate() {
    if (proc_) {
        DWORD code = STILL_ACTIVE;
        if (GetExitCodeProcess(HPROC_(this), &code) && code == STILL_ACTIVE)
            TerminateProcess(HPROC_(this), 1);
        WaitForSingleObject(HPROC_(this), 2000);
    }
    reap();
}

void PtySession::reap() {
    if (reader_alive_) {
        auto* p = static_cast<std::shared_ptr<std::atomic<bool>>*>(reader_alive_);
        (*p)->store(false);
        delete p;
        reader_alive_ = nullptr;
    }
    // Closing the pseudo-console first releases the child's ends, which is what
    // lets the reader thread's blocking ReadFile return instead of hanging.
    if (hpc_)      { ClosePseudoConsole(HPC_(this)); hpc_ = nullptr; }
    if (in_write_) { CloseHandle(HIN_(this));        in_write_ = nullptr; }
    if (out_read_) { CloseHandle(HOUT_(this));       out_read_ = nullptr; }
    if (proc_)     { CloseHandle(HPROC_(this));      proc_ = nullptr; }
    pid_ = -1;
}

void PtySession::onMasterReadable() {}  // POSIX-only slot; unused here.

}  // namespace trowel
