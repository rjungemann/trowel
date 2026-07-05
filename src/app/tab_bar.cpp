#include "app/tab_bar.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>

namespace trowel {

namespace {
constexpr int kVPad = 4;
constexpr int kHPad = 12;
constexpr int kCloseSlot = 20;
constexpr int kMaxTabWidth = 240;
constexpr int kCloseGlyphPadRight = 8;
constexpr int kDividerMarginY = 4;
}

TabBar::TabBar(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    bg_ = QColor("#1e1e1e");
    fg_ = QColor("#d0d0d0");
    divider_ = QColor("#3a3a3a");

    QFont f = font();
    const int base = f.pointSize();
    if (base > 0) f.setPointSize(std::max(1, base - 1));
    else if (f.pixelSize() > 0) f.setPixelSize(std::max(1, f.pixelSize() - 1));
    setFont(f);

    updateFixedHeight();
}

void TabBar::setColors(const QColor& bg, const QColor& fg, const QColor& divider) {
    bg_ = bg;
    fg_ = fg;
    divider_ = divider;
    update();
}

void TabBar::setTabs(const QStringList& displayNames, int activeIndex) {
    names_ = displayNames;
    tooltips_.clear();
    for (int i = 0; i < names_.size(); ++i) tooltips_.append(QString());
    modified_.assign(names_.size(), false);
    active_ = activeIndex;
    if (hovered_ >= names_.size()) hovered_ = -1;
    relayout();
    clampScrollOffset();
    update();
}

void TabBar::setActive(int index) {
    if (active_ == index) return;
    active_ = index;
    relayout();
    update();
}

void TabBar::setModified(int index, bool modified) {
    if (index < 0 || index >= static_cast<int>(modified_.size())) return;
    if (modified_[index] == modified) return;
    modified_[index] = modified;
    relayout();
    update();
}

void TabBar::setTooltip(int index, const QString& tip) {
    if (index < 0 || index >= tooltips_.size()) return;
    tooltips_[index] = tip;
    if (index < static_cast<int>(geoms_.size())) geoms_[index].tooltip = tip;
}

void TabBar::updateFixedHeight() {
    const int h = fontMetrics().height() + 2 * kVPad;
    setFixedHeight(h);
}

void TabBar::relayout() {
    geoms_.clear();
    geoms_.reserve(names_.size());
    const QFontMetrics fm(font());
    int x = 0;
    const int h = height();
    for (int i = 0; i < names_.size(); ++i) {
        QString label = names_[i];
        if (i < static_cast<int>(modified_.size()) && modified_[i]) {
            label += QStringLiteral(" •");
        }
        int textW = fm.horizontalAdvance(label);
        int w = textW + kHPad * 2 + kCloseSlot;
        bool elided = false;
        if (w > kMaxTabWidth) {
            w = kMaxTabWidth;
            elided = true;
        }
        TabGeom g;
        g.rect = QRect(x, 0, w, h);
        g.closeRect = QRect(x + w - kCloseSlot, 0, kCloseSlot, h);
        if (elided) {
            const int textArea = w - kHPad * 2 - kCloseSlot;
            g.label = fm.elidedText(label, Qt::ElideMiddle, textArea);
        } else {
            g.label = label;
        }
        g.tooltip = (i < tooltips_.size()) ? tooltips_[i] : QString();
        g.modified = (i < static_cast<int>(modified_.size())) ? modified_[i] : false;
        geoms_.push_back(std::move(g));
        x += w;
    }
}

int TabBar::tabAt(const QPoint& p) const {
    const QPoint q(p.x() + scrollOffset_, p.y());
    for (int i = 0; i < static_cast<int>(geoms_.size()); ++i) {
        if (geoms_[i].rect.contains(q)) return i;
    }
    return -1;
}

bool TabBar::closeHit(int index, const QPoint& p) const {
    if (index < 0 || index >= static_cast<int>(geoms_.size())) return false;
    const QPoint q(p.x() + scrollOffset_, p.y());
    const QRect& cr = geoms_[index].closeRect;
    // Approximate the glyph hit box: an 16x16 square centered vertically,
    // right-aligned within the close slot with kCloseGlyphPadRight padding.
    const int size = 16;
    const int cx = cr.right() - (kCloseGlyphPadRight - 2) - size / 2;
    const int cy = cr.center().y() - 1;
    const QRect glyphRect(cx - size / 2, cy - size / 2, size, size);
    return glyphRect.contains(q);
}

int TabBar::contentWidth() const {
    if (geoms_.empty()) return 0;
    return geoms_.back().rect.right() + 1;
}

int TabBar::maxScrollOffset() const {
    return std::max(0, contentWidth() - width());
}

void TabBar::clampScrollOffset() {
    scrollOffset_ = std::clamp(scrollOffset_, 0, maxScrollOffset());
}

void TabBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), bg_);

    // Bottom 1px border.
    p.setPen(divider_);
    p.drawLine(0, height() - 1, width(), height() - 1);

    p.translate(-scrollOffset_, 0);

    for (int i = 0; i < static_cast<int>(geoms_.size()); ++i) {
        const TabGeom& g = geoms_[i];

        // Divider on the right edge of each tab (except last).
        if (i + 1 < static_cast<int>(geoms_.size())) {
            p.setPen(divider_);
            p.drawLine(g.rect.right(), kDividerMarginY,
                       g.rect.right(), height() - kDividerMarginY - 1);
        }

        // Label: centered in the text area (excluding close slot).
        QRect textRect = g.rect.adjusted(kHPad, 0, -kCloseSlot, -2);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(fg_);
        p.drawText(textRect, Qt::AlignCenter, g.label);

        // Close glyph on hover.
        if (i == hovered_) {
            const int size = 18;
            const int cx = g.closeRect.right() - (kCloseGlyphPadRight - 2) - size / 2;
            const int cy = g.closeRect.center().y();
            const QRect glyphRect(cx - size / 2, cy - size / 2, size, size);
            QFont cf = font();
            cf.setBold(false);
            p.setFont(cf);
            p.setPen(hoverClose_ ? fg_ : divider_);
            p.drawText(glyphRect, Qt::AlignCenter, QString(QChar(0x00D7)));
        }
    }

    // Restore font.
    p.setFont(font());
}

void TabBar::mouseMoveEvent(QMouseEvent* e) {
    const int idx = tabAt(e->pos());
    const bool onClose = (idx >= 0) && closeHit(idx, e->pos());
    if (idx != hovered_ || onClose != hoverClose_) {
        hovered_ = idx;
        hoverClose_ = onClose;
        update();
    }
    if (idx >= 0) {
        const QString& tip = geoms_[idx].tooltip;
        if (!tip.isEmpty()) {
            QToolTip::showText(e->globalPosition().toPoint(), tip, this);
        } else {
            QToolTip::hideText();
        }
    }
}

void TabBar::mousePressEvent(QMouseEvent* e) {
    const int idx = tabAt(e->pos());
    if (idx < 0) return;
    if (e->button() == Qt::MiddleButton) {
        emit closeRequested(idx);
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (closeHit(idx, e->pos())) {
            emit closeRequested(idx);
        } else {
            emit activateRequested(idx);
        }
    }
}

void TabBar::leaveEvent(QEvent*) {
    if (hovered_ != -1 || hoverClose_) {
        hovered_ = -1;
        hoverClose_ = false;
        update();
    }
}

void TabBar::resizeEvent(QResizeEvent*) {
    relayout();
    clampScrollOffset();
}

void TabBar::wheelEvent(QWheelEvent* e) {
    // Prefer pixel-precise deltas from trackpads; fall back to angle deltas.
    const QPoint pd = e->pixelDelta();
    const QPoint ad = e->angleDelta();
    int dx = 0;
    if (!pd.isNull()) {
        // Horizontal scroll or, when swiping vertically, use y as x.
        dx = pd.x() != 0 ? pd.x() : pd.y();
    } else if (!ad.isNull()) {
        const int a = ad.x() != 0 ? ad.x() : ad.y();
        // Angle delta is in 1/8 degrees; typical notch = 120 units.
        // Scroll roughly one tab (~120 px) per notch.
        dx = a * 120 / 120;
    }
    if (dx == 0) { e->ignore(); return; }
    scrollOffset_ -= dx;
    clampScrollOffset();
    update();
    e->accept();
}

bool TabBar::event(QEvent* e) {
    if (e->type() == QEvent::FontChange) {
        updateFixedHeight();
        relayout();
        update();
    }
    return QWidget::event(e);
}

}
