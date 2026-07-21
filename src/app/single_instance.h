#pragma once

#include <QString>
#include <QStringList>

// Single-instance support for platforms without an OS-level "open document in
// the running app" mechanism (i.e. everything except macOS, where
// LaunchServices delivers QEvent::FileOpen). A second `trowel foo.tur` connects
// to the first instance's control socket, forwards the files as new tabs, and
// exits — giving the same "open in the existing window" behavior the macOS
// build gets from the `trowel` CLI shim's `open -b`.
namespace trowel {
namespace single_instance {

// Stable, per-user (and per-GUI-session) path for the single-instance control
// socket. Distinct X11/Wayland displays get distinct sockets so two logins by
// the same user don't cross-talk.
QString WellKnownSocketPath();

// Try to hand `absPaths` (already absolute; may be empty, meaning "just bring
// the window to the front") to an already-running Trowel via its
// single-instance socket.
//
// Returns true if an instance was reached and the request forwarded — the
// caller should exit. Returns false if no instance is listening — the caller
// should become the primary instance and start listening itself.
bool ForwardToRunningInstance(const QStringList& absPaths);

}  // namespace single_instance
}  // namespace trowel
