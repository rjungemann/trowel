#include "app/icon_font.h"

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QPainter>
#include <QPixmap>

namespace trowel {

namespace {

struct IconCacheKey {
    char32_t codepoint;
    int pixelSize;
    QRgb color;

    bool operator==(const IconCacheKey& o) const noexcept {
        return codepoint == o.codepoint && pixelSize == o.pixelSize && color == o.color;
    }
};

size_t qHash(const IconCacheKey& k, size_t seed = 0) noexcept {
    return ::qHash(static_cast<uint>(k.codepoint), seed)
         ^ ::qHash(k.pixelSize, seed)
         ^ ::qHash(k.color, seed);
}

QString& familyName() {
    static QString name;
    return name;
}

QHash<IconCacheKey, QIcon>& iconCache() {
    static QHash<IconCacheKey, QIcon> cache;
    return cache;
}

} // namespace

QString RegisterNerdFont() {
    QString& family = familyName();
    if (!family.isEmpty()) return family;

    QFile f(":/fonts/SymbolsNerdFont-Regular.ttf");
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = f.readAll();

    const int id = QFontDatabase::addApplicationFontFromData(bytes);
    if (id < 0) return {};

    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) return {};
    family = families.first();
    return family;
}

QIcon NerdIcon(char32_t codepoint, int pixelSize, const QColor& color) {
    const QString family = RegisterNerdFont();
    if (family.isEmpty() || pixelSize <= 0) return {};

    const IconCacheKey key{codepoint, pixelSize, color.rgba()};
    auto& cache = iconCache();
    auto it = cache.find(key);
    if (it != cache.end()) return it.value();

    // Render at 2× the target size for crispness on hi-DPI (Qt scales down
    // via QIcon's devicePixelRatio handling on 1× displays and uses the raw
    // bitmap on 2× displays).
    const qreal dpr = 2.0;
    const int px = static_cast<int>(pixelSize * dpr);
    QPixmap pm(px, px);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QFont font(family);
    font.setPixelSize(pixelSize);

    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(font);
    p.setPen(color);
    // QString from a single Unicode codepoint (may be outside the BMP).
    QString glyph;
    if (codepoint > 0xFFFF) {
        const uint c = codepoint - 0x10000;
        glyph.append(QChar(0xD800 | (c >> 10)));
        glyph.append(QChar(0xDC00 | (c & 0x3FF)));
    } else {
        glyph.append(QChar(static_cast<ushort>(codepoint)));
    }
    p.drawText(QRect(0, 0, pixelSize, pixelSize), Qt::AlignCenter, glyph);
    p.end();

    QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
}

}
