#pragma once

#include <QColor>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

namespace trowel {

class TabBar : public QWidget {
    Q_OBJECT
public:
    explicit TabBar(QWidget* parent = nullptr);

    void setTabs(const QStringList& displayNames, int activeIndex);
    void setActive(int index);
    void setModified(int index, bool modified);
    void setTooltip(int index, const QString& tip);

    void setColors(const QColor& bg, const QColor& fg, const QColor& divider);

signals:
    void activateRequested(int index);
    void closeRequested(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent* e) override;

private:
    struct TabGeom {
        QRect rect;
        QRect closeRect;
        QString label;      // possibly elided, with modified marker appended
        QString tooltip;
        bool modified = false;
    };

    void relayout();
    void updateFixedHeight();
    int tabAt(const QPoint& p) const;
    bool closeHit(int index, const QPoint& p) const;

    std::vector<TabGeom> geoms_;
    std::vector<bool> modified_;
    QStringList names_;
    QStringList tooltips_;
    int active_ = -1;
    int hovered_ = -1;
    bool hoverClose_ = false;

    QColor bg_;
    QColor fg_;
    QColor divider_;
};

}
