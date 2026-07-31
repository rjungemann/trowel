#pragma once

#include <QObject>
#include <QString>

#include "repl/run_buffer.h"

class QProcess;

namespace trowel {

class TerminalView;

// Builds a Turmeric *project* — the thing a `build.tur` describes — as opposed
// to evaluating a buffer in the REPL.
//
// This deliberately does not go through ReplSession: the REPL's pty is occupied
// by an interactive `tur repl`, and a manifest is not something you can `(load
// …)`. Instead `tur build <projectDir>` runs as a plain child process and its
// merged stdout/stderr is echoed into the same TerminalView, so the output
// lands where the user already looks for it.
class ProjectRunner : public QObject {
    Q_OBJECT
public:
    explicit ProjectRunner(TerminalView* terminal, QObject* parent = nullptr);

    bool isRunning() const;

    // Start `tur build <projectDir>` rooted at that directory. Returns
    // immediately; progress arrives in the terminal. `ok` is false (with a
    // message) when the toolchain is missing, a build is already in flight, or
    // the process could not be started.
    RunResult run(const QString& projectDir);

signals:
    void finished(int exitCode);

private:
    void onReadyRead();
    void onFinished(int exitCode);

    TerminalView* terminal_ = nullptr;
    QProcess* proc_ = nullptr;
};

}
