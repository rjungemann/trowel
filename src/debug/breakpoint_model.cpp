#include "debug/breakpoint_model.h"

#include <QFileInfo>
#include <QHash>

#include <algorithm>

namespace trowel {

BreakpointModel::BreakpointModel(QObject* parent)
    : QObject(parent)
{}

int BreakpointModel::indexOf(const QString& path, int line) const {
    for (int i = 0; i < breakpoints_.size(); ++i) {
        if (breakpoints_[i].path == path && breakpoints_[i].line == line) return i;
    }
    return -1;
}

QVector<BreakpointModel::Breakpoint> BreakpointModel::forFile(const QString& path) const {
    QVector<Breakpoint> out;
    for (const Breakpoint& b : breakpoints_) {
        if (b.path == path) out.append(b);
    }
    return out;
}

bool BreakpointModel::toggle(const QString& path, int line) {
    const int i = indexOf(path, line);
    if (i >= 0) {
        breakpoints_.removeAt(i);
        emit changed(path);
        return true;
    }
    Breakpoint b;
    b.path = path;
    b.line = line;
    b.enabled = true;
    breakpoints_.append(b);
    emit changed(path);
    return true;
}

void BreakpointModel::set(const QString& path, int line, bool enabled,
                          const QString& condition) {
    const int i = indexOf(path, line);
    if (i >= 0) {
        if (breakpoints_[i].enabled == enabled && breakpoints_[i].condition == condition)
            return;
        breakpoints_[i].enabled = enabled;
        breakpoints_[i].condition = condition;
    } else {
        Breakpoint b;
        b.path = path;
        b.line = line;
        b.enabled = enabled;
        b.condition = condition;
        breakpoints_.append(b);
    }
    emit changed(path);
}

void BreakpointModel::remove(const QString& path, int line) {
    const int i = indexOf(path, line);
    if (i < 0) return;
    breakpoints_.removeAt(i);
    emit changed(path);
}

void BreakpointModel::clearFile(const QString& path) {
    bool didChange = false;
    for (int i = breakpoints_.size() - 1; i >= 0; --i) {
        if (breakpoints_[i].path == path) {
            breakpoints_.removeAt(i);
            didChange = true;
        }
    }
    if (didChange) emit changed(path);
}

void BreakpointModel::clear() {
    if (breakpoints_.isEmpty()) return;
    breakpoints_.clear();
    emit changed({});  // empty path = everything changed
}

void BreakpointModel::setEnabled(const QString& path, int line, bool enabled) {
    const int i = indexOf(path, line);
    if (i < 0 || breakpoints_[i].enabled == enabled) return;
    breakpoints_[i].enabled = enabled;
    emit changed(path);
}

void BreakpointModel::setCondition(const QString& path, int line,
                                   const QString& condition) {
    const int i = indexOf(path, line);
    if (i < 0 || breakpoints_[i].condition == condition) return;
    breakpoints_[i].condition = condition;
    emit changed(path);
}

void BreakpointModel::applyLineMoves(const QString& path,
                                     const QVector<QPair<int, int>>& moves) {
    if (moves.isEmpty()) return;
    // Applied as one batch against a snapshot, not one at a time. Moves
    // routinely overlap — inserting a line above two adjacent breakpoints
    // produces {5→6, 6→7}, and applying 5→6 first would land on top of the
    // breakpoint still sitting at 6 and lose one of them.
    QHash<int, int> byOldLine;
    for (const auto& m : moves) byOldLine.insert(m.first, m.second);

    QVector<Breakpoint> updated;
    updated.reserve(breakpoints_.size());
    bool didChange = false;
    for (const Breakpoint& b : breakpoints_) {
        if (b.path != path || !byOldLine.contains(b.line)) {
            updated.append(b);
            continue;
        }
        const int to = byOldLine.value(b.line);
        didChange = true;
        // 0 means the line holding it was deleted; the breakpoint goes with
        // it rather than sliding onto whatever is now at that number.
        if (to < 1) continue;
        Breakpoint moved = b;
        moved.line = to;
        updated.append(moved);
    }
    if (!didChange) return;

    // A move can land on a line that already has one. Keep the first and drop
    // the rest: two breakpoints on one line is a state the model has no way
    // to render and the adapter no way to honour.
    QVector<Breakpoint> deduped;
    for (const Breakpoint& b : updated) {
        const bool dup = std::any_of(
            deduped.cbegin(), deduped.cend(), [&b](const Breakpoint& o) {
                return o.path == b.path && o.line == b.line;
            });
        if (!dup) deduped.append(b);
    }
    breakpoints_ = deduped;
    emit changed(path);
}

bool BreakpointModel::HasBasenameCollision(const QString& path,
                                           const QStringList& openPaths) {
    const QString name = QFileInfo(path).fileName();
    if (name.isEmpty()) return false;
    int count = 0;
    for (const QString& p : openPaths) {
        if (QFileInfo(p).fileName() == name) ++count;
    }
    return count > 1;
}

}
