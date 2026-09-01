#pragma once

#include "debug/debug_session.h"

#include <QColor>
#include <QVector>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace trowel {

struct Theme;
class DepthRibbon;

// The time-travel scrubber (T4) and the call-depth ribbon under it (T6).
//
// A recording is an axis, which is the thing DAP has no vocabulary for and the
// `replayInfo` / `replaySeek` / `replaySites` extension exists to supply. This
// is the widget that makes the axis touchable: first / back / forward / last,
// a slider over `[0, steps)`, the cursor's position and site, and a ribbon
// showing where the recursion is.
//
// Only meaningful in a replay session against a `tur` that advertises the
// extension. `MainWindow` hides it otherwise rather than disabling it — a
// scrubber for a recording that does not exist is not a disabled control, it
// is a lie about what this session can do.
class TimelineStrip : public QWidget {
    Q_OBJECT
public:
    explicit TimelineStrip(QWidget* parent = nullptr);

    void applyTheme(const Theme& theme);

    // Point the slider at a recording. `steps` of 0 leaves the strip inert.
    void setTimeline(const DebugSession::Timeline& t);
    // The depth profile, one entry per bucket, from `replaySites`.
    void setSites(const QVector<DebugSession::Site>& sites);
    // What the cursor is currently sitting on, for the readout beside it.
    void setSite(const QString& function, const QString& fileName, int line);

signals:
    // The user asked for a step index. Coalescing lives in `DebugSession`,
    // deliberately: it is a property of the transport, not of the widget, and
    // the keyboard and the buttons want it as much as the drag does.
    void seekRequested(int index);

private:
    void emitDelta(int delta);

    QToolButton* first_;
    QToolButton* back_;
    QToolButton* forward_;
    QToolButton* last_;
    DepthRibbon* ribbon_;
    QSlider* slider_;
    QLabel* position_;
    QLabel* site_;
    int steps_ = 0;
    int index_ = 0;
    QColor accent_;
    QColor dim_;
};

}
