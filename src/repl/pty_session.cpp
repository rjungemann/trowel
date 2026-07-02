#include "repl/pty_session.h"

#include <QDir>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#endif

namespace trowel {

PtySession::PtySession(QObject* parent)
    : QObject(parent) {}

PtySession::~PtySession() {
    terminate();
}

bool PtySession::start(const QString& program, const QStringList& args, const QString& workingDir) {
    if (isRunning()) return false;

    struct termios term{};
    term.c_iflag = ICRNL | IXON | IUTF8;
    term.c_oflag = OPOST | ONLCR;
    term.c_cflag = CREAD | CS8 | HUPCL;
    term.c_lflag = ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;
    cfsetispeed(&term, B38400);
    cfsetospeed(&term, B38400);

    struct winsize win = {24, 80, 0, 0};

    int master = -1;
    pid_t pid = forkpty(&master, nullptr, &term, &win);
    if (pid < 0) {
        emit startFailed(QString("forkpty failed: %1").arg(strerror(errno)));
        return false;
    }

    if (pid == 0) {
        if (!workingDir.isEmpty()) {
            if (chdir(workingDir.toUtf8().constData()) != 0) {
                // best effort; ignore failure
            }
        }
        setenv("TERM", "xterm-256color", 1);

        std::vector<QByteArray> argStorage;
        argStorage.reserve(args.size() + 1);
        argStorage.push_back(program.toUtf8());
        for (const auto& a : args) argStorage.push_back(a.toUtf8());

        std::vector<char*> argv;
        argv.reserve(argStorage.size() + 1);
        for (auto& s : argStorage) argv.push_back(s.data());
        argv.push_back(nullptr);

        execvp(argStorage[0].constData(), argv.data());
        // If we reach here, exec failed.
        std::_Exit(127);
    }

    // Parent
    master_ = master;
    pid_ = pid;

    int flags = fcntl(master_, F_GETFL, 0);
    fcntl(master_, F_SETFL, flags | O_NONBLOCK);

    notifier_ = new QSocketNotifier(master_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &PtySession::onMasterReadable);

    // Poll for child exit — cheap enough for a REPL.
    auto* reaper = new QTimer(this);
    reaper->setInterval(200);
    connect(reaper, &QTimer::timeout, this, [this, reaper]{
        if (!isRunning()) { reaper->stop(); return; }
        int status = 0;
        pid_t r = waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
        if (r == static_cast<pid_t>(pid_)) {
            const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            reap();
            emit finished(code);
            reaper->stop();
        }
    });
    reaper->start();

    return true;
}

void PtySession::onMasterReadable() {
    if (master_ < 0) return;
    QByteArray chunk;
    chunk.resize(4096);
    for (;;) {
        ssize_t n = ::read(master_, chunk.data(), chunk.size());
        if (n > 0) {
            emit dataReceived(chunk.left(static_cast<int>(n)));
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            // EIO on macOS means child exited.
            break;
        } else {
            break;
        }
    }
}

void PtySession::write(const QByteArray& bytes) {
    if (master_ < 0 || bytes.isEmpty()) return;
    ssize_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = ::write(master_, bytes.constData() + off, bytes.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            break;
        }
        off += n;
    }
}

void PtySession::resize(int rows, int cols) {
    if (master_ < 0) return;
    struct winsize win {
        static_cast<unsigned short>(rows),
        static_cast<unsigned short>(cols),
        0, 0
    };
    ioctl(master_, TIOCSWINSZ, &win);
}

void PtySession::terminate() {
    if (pid_ > 0) {
        ::kill(static_cast<pid_t>(pid_), SIGTERM);
        // Give it 200ms to exit gracefully.
        for (int i = 0; i < 20; ++i) {
            int status = 0;
            pid_t r = waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
            if (r == static_cast<pid_t>(pid_)) { pid_ = -1; break; }
            usleep(10 * 1000);
        }
        if (pid_ > 0) {
            ::kill(static_cast<pid_t>(pid_), SIGKILL);
            int status = 0;
            waitpid(static_cast<pid_t>(pid_), &status, 0);
        }
    }
    reap();
}

void PtySession::reap() {
    if (notifier_) {
        notifier_->setEnabled(false);
        notifier_->deleteLater();
        notifier_ = nullptr;
    }
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
    pid_ = -1;
}

}
