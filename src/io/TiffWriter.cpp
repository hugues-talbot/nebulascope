#include "io/TiffWriter.h"
#include "core/Convert.h"
#include "core/ImageStats.h"

#include <QImage>
#include <QImageWriter>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>

namespace astro::io {

bool TiffWriter::canWrite(const QString& path) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "tif" || ext == "tiff";
}

SaveResult TiffWriter::save(const QString& path, const ImageData& image,
                            const ImageHeader& /*header*/, const SaveOptions& /*opts*/) const {
    SaveResult r;
    if (!image.isValid()) { r.error = QStringLiteral("Empty image"); return r; }

    // Work on a Float32 copy so we have one code path regardless of source type.
    const ImageData f = (image.format() == SampleFormat::Float32)
                            ? image : toFloat32(image, /*normalizeIntegers=*/true);
    const int w = f.width(), h = f.height(), ch = f.channels();

    // Global finite min/max across channels → consistent 16-bit scaling.
    double mn = std::numeric_limits<double>::infinity();
    double mx = -std::numeric_limits<double>::infinity();
    for (int c = 0; c < ch; ++c) {
        const float* p = f.plane<float>(c);
        const std::size_t n = f.samplesPerChannel();
        for (std::size_t i = 0; i < n; ++i) {
            const float v = p[i];
            if (!std::isfinite(v)) continue;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
    }
    if (!std::isfinite(mn) || !std::isfinite(mx)) { mn = 0.0; mx = 1.0; }
    const double span = std::max(1e-12, mx - mn);

    auto to16 = [&](float v) -> quint16 {
        if (!std::isfinite(v)) return 0;
        double x = (double(v) - mn) / span;
        x = x < 0 ? 0 : (x > 1 ? 1 : x);
        return quint16(x * 65535.0 + 0.5);
    };

    QImage out;
    if (ch >= 3) {
        out = QImage(w, h, QImage::Format_RGBX64);
        const float* rp = f.plane<float>(0);
        const float* gp = f.plane<float>(1);
        const float* bp = f.plane<float>(2);
        for (int y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<quint16*>(out.scanLine(y));
            const std::size_t off = std::size_t(y) * w;
            for (int x = 0; x < w; ++x) {
                line[x * 4 + 0] = to16(rp[off + x]);
                line[x * 4 + 1] = to16(gp[off + x]);
                line[x * 4 + 2] = to16(bp[off + x]);
                line[x * 4 + 3] = 0xFFFF;                 // opaque X/alpha
            }
        }
    } else {
        out = QImage(w, h, QImage::Format_Grayscale16);
        const float* p = f.plane<float>(0);
        for (int y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<quint16*>(out.scanLine(y));
            const std::size_t off = std::size_t(y) * w;
            for (int x = 0; x < w; ++x) line[x] = to16(p[off + x]);
        }
    }

    QImageWriter writer(path, "tiff");
    if (!writer.write(out)) {
        r.error = QStringLiteral("TIFF write failed: %1").arg(writer.errorString());
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace astro::io
