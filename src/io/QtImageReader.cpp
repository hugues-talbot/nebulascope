#include "io/QtImageReader.h"
#include "core/Convert.h"

#include <QImage>
#include <QImageReader>
#include <QFileInfo>
#include <cstring>

namespace astro::io {

static bool isSixteen(QImage::Format f) {
    return f == QImage::Format_Grayscale16 ||
           f == QImage::Format_RGBX64 ||
           f == QImage::Format_RGBA64 ||
           f == QImage::Format_RGBA64_Premultiplied;
}

bool QtImageReader::canRead(const QString& path) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "tif" || ext == "tiff";
}

LoadResult QtImageReader::load(const QString& path, const LoadOptions& opts) const {
    LoadResult r;

    QImageReader reader(path);
    reader.setAutoTransform(true);                 // honour EXIF orientation
    QImage src = reader.read();
    if (src.isNull()) {
        r.error = QStringLiteral("Qt image read failed: %1").arg(reader.errorString());
        return r;
    }

    const bool gray = src.isGrayscale();
    const bool deep = isSixteen(src.format());
    const int  w = src.width(), h = src.height();
    const int  ch = gray ? 1 : 3;
    const SampleFormat nativeFmt = deep ? SampleFormat::UInt16 : SampleFormat::UInt8;

    ImageData img(w, h, ch, nativeFmt, gray ? ColorSpace::Gray : ColorSpace::RGB);

    // De-interleave Qt's packed pixels into our planar buffer.
    if (gray && !deep) {
        const QImage c = src.convertToFormat(QImage::Format_Grayscale8);
        auto* o = img.plane<std::uint8_t>(0);
        for (int y = 0; y < h; ++y) std::memcpy(o + std::size_t(y) * w, c.constScanLine(y), w);
    } else if (gray && deep) {
        const QImage c = src.convertToFormat(QImage::Format_Grayscale16);
        auto* o = img.plane<std::uint16_t>(0);
        for (int y = 0; y < h; ++y)
            std::memcpy(o + std::size_t(y) * w, c.constScanLine(y), std::size_t(w) * 2);
    } else if (!gray && !deep) {
        const QImage c = src.convertToFormat(QImage::Format_RGB888);
        auto *rp = img.plane<std::uint8_t>(0), *gp = img.plane<std::uint8_t>(1), *bp = img.plane<std::uint8_t>(2);
        for (int y = 0; y < h; ++y) {
            const uchar* line = c.constScanLine(y);
            const std::size_t off = std::size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                rp[off + x] = line[x * 3 + 0];
                gp[off + x] = line[x * 3 + 1];
                bp[off + x] = line[x * 3 + 2];
            }
        }
    } else {  // colour, 16-bit  (RGBX64 = 4×quint16 per pixel: r,g,b,x)
        const QImage c = src.convertToFormat(QImage::Format_RGBX64);
        auto *rp = img.plane<std::uint16_t>(0), *gp = img.plane<std::uint16_t>(1), *bp = img.plane<std::uint16_t>(2);
        for (int y = 0; y < h; ++y) {
            const auto* line = reinterpret_cast<const quint16*>(c.constScanLine(y));
            const std::size_t off = std::size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                rp[off + x] = line[x * 4 + 0];
                gp[off + x] = line[x * 4 + 1];
                bp[off + x] = line[x * 4 + 2];
            }
        }
    }

    if (opts.promoteToFloat)
        img = toFloat32(img, /*normalizeIntegers=*/true);

    const QString fmtName = QFileInfo(path).suffix().toUpper();
    r.header.container  = fmtName;
    r.header.nativeType = QStringLiteral("%1-bit %2").arg(deep ? 16 : 8).arg(gray ? "grayscale" : "RGB");
    r.header.structure << QStringLiteral("%1: %2×%3, %4")
                              .arg(fmtName).arg(w).arg(h).arg(r.header.nativeType);

    r.image = std::move(img);
    r.ok = true;
    return r;
}

} // namespace astro::io
