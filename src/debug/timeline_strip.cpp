#include "debug/timeline_strip.h"

#include "app/icon_font.h"
#include "editor/theme_loader.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace trowel {

namespace {
constexpr int kIconSize = 13;
// Tall enough to read a shape from, short enough that it stays a margin note
// under the slider rather than a chart.
constexpr int kRibbonHeight = 18;
}  // namespace

// The call-depth profile, one bar per bucket.
//
// Try Turmeric never built this — its own plan lists it under "Left", and the
// `trace-site-at` plumbing there has no caller. The reason it is cheap at all
// is that depth is an index read (`turi_trace_replay_depth_at`), so a bar per
// pixel costs a scan and not a seek per sample; asking by seeking is the trap
// `trace.h` documents, and turns an 80k recording from milliseconds into a
// hang.
//
// Each bar is a bucket's *maximum* depth, chosen server-side. A ribbon is read
// for recursion shape, so a deep call falling between two samples is exactly
// what the reader is looking for — averaging would erase it.
//
// No Q_OBJECT: it has no signals or slots.
class DepthRibbon : public QWidget {
public:
    explicit DepthRibbon(QWidget* parent) : QWidget(parent) {
        setFixedHeight(kRibbonHeight);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setSites(const QVector<DebugSession::Site>& sites) {
        sites_ = sites;
        peak_ = 0;
        for (const auto& s : sites_) peak_ = qMax(peak_, s.depth);
        update();
    }
    void setCursor(int index, int steps) { index_ = index; steps_ = steps; update(); }
    void setColors(const QColor& bar, const QColor& cursor) {
        bar_ = bar; cursor_ = cursor; update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (sites_.isEmpty() || peak_ <= 0) return;
        QPainter p(this);
        const int w = width();
        const int h = height();
        const int n = sites_.size();
        // Bars are laid out by bucket, not by index: the buckets already
        // partition the recording evenly, so this is the same axis the slider
        // uses without having to re-derive it from the indices.
        const double bw = double(w) / n;
        p.setPen(Qt::NoPen);
        p.setBrush(bar_);
        for (int i = 0; i < n; ++i) {
            const double frac = double(sites_[i].depth) / peak_;
            const int bh = qMax(1, int(frac * (h - 2)));
            p.drawRect(QRectF(i * bw, h - bh, qMax(1.0, bw - 0.5), bh));
        }
        // Where the cursor is, so the ribbon and the slider agree at a glance.
        if (steps_ > 1) {
            const int x = int(double(index_) / (steps_ - 1) * (w - 1));
            p.setPen(cursor_);
            p.drawLine(x, 0, x, h);
        }
    }

private:
    QVector<DebugSession::Site> sites_;
    int peak_ = 0;
    int index_ = 0;
    int steps_ = 0;
    QColor bar_ = QColor("#454545");
    QColor cursor_ = QColor("#EFA030");
};

TimelineStrip::TimelineStrip(QWidget* parent)
    : QWidget(parent)
    , first_(new QToolButton(this))
    , back_(new QToolButton(this))
    , forward_(new QToolButton(this))
    , last_(new QToolButton(this))
    , ribbon_(new DepthRibbon(this))
    , slider_(new QSlider(Qt::Horizontal, this))
    , position_(new QLabel(this))
    , site_(new QLabel(this))
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(6);

    struct { QToolButton* b; const char* tip; } buttons[] = {
        {first_,   "First step"},
        {back_,    "Step back (Alt+Left)"},
        {forward_, "Step forward (Alt+Right)"},
        {last_,    "Last step"},
    };
    for (auto& e : buttons) {
        e.b->setAutoRaise(true);
        e.b->setToolTip(QString::fromUtf8(e.tip));
        e.b->setIconSize(QSize(kIconSize, kIconSize));
        row->addWidget(e.b);
    }

    connect(first_, &QToolButton::clicked, this, [this] { emit seekRequested(0); });
    connect(last_, &QToolButton::clicked, this,
            [this] { if (steps_ > 0) emit seekRequested(steps_ - 1); });
    connect(back_, &QToolButton::clicked, this, [this] { emitDelta(-1); });
    connect(forward_, &QToolButton::clicked, this, [this] { emitDelta(1); });

    // Ribbon directly above the slider and in the same column, so a spike and
    // the position it stands for line up vertically.
    auto* axis = new QVBoxLayout;
    axis->setContentsMargins(0, 0, 0, 0);
    axis->setSpacing(0);
    axis->addWidget(ribbon_);
    axis->addWidget(slider_);
    row->addLayout(axis, 1);

    slider_->setMinimum(0);
    slider_->setMaximum(0);
    slider_->setPageStep(1);
    // `sliderMoved` rather than `valueChanged`: the latter also fires when the
    // *adapter's* answer moves the slider, which would echo a seek back at the
    // session for every seek it completed.
    connect(slider_, &QSlider::sliderMoved, this, [this](int v) {
        emit seekRequested(v);
    });

    // Fixed room for the readouts. The slider takes all the stretch, so
    // without a floor these were squeezed until "203 / 405" and the site ran
    // off the right edge of the pane.
    position_->setToolTip(QStringLiteral("Cursor position in the recording"));
    position_->setMinimumWidth(74);
    position_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    site_->setToolTip(QStringLiteral("Where the cursor is in the source"));
    site_->setMinimumWidth(96);
    site_->setTextInteractionFlags(Qt::NoTextInteraction);
    row->addWidget(position_);
    row->addWidget(site_);

    applyTheme(LoadBuiltinDarkTheme());
    setTimeline({});
}

void TimelineStrip::emitDelta(int delta) {
    if (steps_ <= 0) return;
    emit seekRequested(qBound(0, index_ + delta, steps_ - 1));
}

void TimelineStrip::applyTheme(const Theme& theme) {
    accent_ = theme.matchedBraceFg;
    dim_ = theme.lineNumberFg;
    // A transport row reads left-to-right as one direction of travel, so the
    // glyphs are the media set rather than the debugger's step icons: `Rewind`
    // on "last" pointed backwards in a control that goes forwards.
    first_->setIcon(NerdIcon(NF::SkipPrevious, kIconSize, accent_));
    back_->setIcon(NerdIcon(NF::StepBackward, kIconSize, accent_));
    forward_->setIcon(NerdIcon(NF::StepForward, kIconSize, accent_));
    last_->setIcon(NerdIcon(NF::SkipNext, kIconSize, accent_));
    ribbon_->setColors(dim_, accent_);

    const QString bg = theme.editorBg.name();
    setStyleSheet(QStringLiteral(R"(
QWidget { background: %1; }
QToolButton { border: none; border-radius: 3px; padding: 3px; }
QToolButton:hover:enabled { background: %4; }
QLabel { color: %2; }
QSlider::groove:horizontal { background: %3; height: 3px; border-radius: 2px; }
QSlider::handle:horizontal { background: %5; width: 9px; margin: -4px 0;
    border-radius: 4px; }
QSlider::sub-page:horizontal { background: %5; height: 3px; border-radius: 2px; }
)").arg(bg, dim_.name(), theme.editorBg.lighter(160).name(),
        theme.editorBg.lighter(140).name(), accent_.name()));
}

void TimelineStrip::setTimeline(const DebugSession::Timeline& t) {
    steps_ = t.steps;
    index_ = t.index;
    const bool live = steps_ > 0;
    const QVector<QWidget*> controls{first_, back_, forward_, last_, slider_};
    for (QWidget* w : controls) w->setEnabled(live);
    slider_->setMaximum(qMax(0, steps_ - 1));
    // Blocked: setValue would otherwise be indistinguishable from a drag on
    // widgets that emit valueChanged, and echo the adapter's own answer back
    // as a fresh seek.
    {
        const QSignalBlocker blocker(slider_);
        slider_->setValue(index_);
    }
    position_->setText(live ? QStringLiteral("%1 / %2").arg(index_ + 1).arg(steps_)
                            : QString());
    ribbon_->setCursor(index_, steps_);
}

void TimelineStrip::setSites(const QVector<DebugSession::Site>& sites) {
    ribbon_->setSites(sites);
}

void TimelineStrip::setSite(const QString& function, const QString& fileName, int line) {
    if (function.isEmpty() && fileName.isEmpty()) { site_->setText(QString()); return; }
    site_->setText(fileName.isEmpty()
        ? function
        : QStringLiteral("%1  %2:%3").arg(function, fileName).arg(line));
}

}
