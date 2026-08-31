#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace trowel {

// A persistent, per-window breakpoint store. Keyed by absolute path + 1-based
// line, carrying `enabled` and `condition`. Lives outside any view so
// breakpoints survive across debug sessions (one program per session,
// constraint 3 — "restart" means respawning `tur dap`).
//
// The model is the source of truth; the editor gutter markers and the
// breakpoints panel are views over it. `DebugSession` maps a `changed(path)`
// signal to a `setBreakpoints` request for that one source (a full
// replacement per source — exactly what `tur dap` expects).
class BreakpointModel : public QObject {
    Q_OBJECT
public:
    struct Key {
        QString path;
        int line = 0;  // 1-based
        bool operator==(const Key& o) const { return path == o.path && line == o.line; }
    };

    struct Breakpoint {
        QString path;       // absolute
        int line = 0;       // 1-based
        bool enabled = true;
        QString condition;  // empty = unconditional
    };

    explicit BreakpointModel(QObject* parent = nullptr);

    // All breakpoints, in insertion order. The view iterates this to render
    // the gutter markers and the breakpoints panel.
    const QVector<Breakpoint>& breakpoints() const { return breakpoints_; }

    // Breakpoints for one source, as a full replacement set. This is the
    // shape `setBreakpoints` expects (constraint: dap.c clears the file's
    // set before adding the new one).
    QVector<Breakpoint> forFile(const QString& path) const;

    // Toggle a breakpoint at `path`:`line`. Adds one if absent, removes it if
    // present. Returns true if the set changed.
    bool toggle(const QString& path, int line);

    // Set / clear / edit a specific breakpoint.
    void set(const QString& path, int line, bool enabled, const QString& condition = {});
    void remove(const QString& path, int line);
    void clearFile(const QString& path);
    void clear();

    // Enable/disable without removing.
    void setEnabled(const QString& path, int line, bool enabled);
    void setCondition(const QString& path, int line, const QString& condition);

    // True if two open sources share a basename — they collide inside the
    // interpreter, which matches breakpoints by basename (constraint 5). The
    // caller passes the set of open paths; this reports whether `path`'s
    // basename appears more than once.
    static bool HasBasenameCollision(const QString& path, const QStringList& openPaths);

signals:
    // The set of breakpoints for `path` changed (added, removed, enabled, or
    // condition edited). `DebugSession` maps this to a `setBreakpoints`
    // request; `EditorView` maps it to gutter markers. Empty `path` means
    // "everything changed" (clear).
    void changed(const QString& path);

private:
    int indexOf(const QString& path, int line) const;
    QVector<Breakpoint> breakpoints_;
};

}
