// IO round-trip tests: write with each backend, read back, compare pixels.
// These catch format-interop regressions (sample scaling, layout, headers) —
// e.g. the XISF float-normalization requirement is asserted here.
#include "nstest.h"
#include "core/ImageData.h"
#include "io/ImageReader.h"
#include "io/ImageWriter.h"
#include <QTemporaryDir>
#include <QString>
#include <cmath>

using namespace astro;

static ImageData makePattern(int w, int h, int ch, float lo, float hi) {
    ImageData img(w, h, ch, SampleFormat::Float32,
                  ch == 3 ? ColorSpace::RGB : ColorSpace::Gray);
    const std::size_t n = img.samplesPerChannel();
    for (int c = 0; c < ch; ++c) {
        float* p = img.plane<float>(c);
        for (std::size_t i = 0; i < n; ++i)
            p[i] = lo + (hi - lo) * float((i * (c + 1)) % n) / float(n - 1);
    }
    return img;
}

// Round-trip through `ext`; returns max |in-out| after undoing a linear
// rescale fitted from the data (some writers normalize the stored range).
static double roundTripError(const QString& dir, const char* ext,
                             const ImageData& src, bool allowRescale) {
    const QString path = dir + "/rt." + ext;
    io::SaveResult sr = io::saveImage(path, src, {});
    if (!sr.ok) return 1e9;
    io::LoadResult lr = io::loadImage(path);
    if (!lr.ok || !lr.image.isValid()) return 1e9;
    if (lr.image.width() != src.width() || lr.image.height() != src.height() ||
        lr.image.channels() != src.channels()) return 1e9;

    double worst = 0.0;
    const std::size_t n = src.samplesPerChannel();
    for (int c = 0; c < src.channels(); ++c) {
        const float* a = src.plane<float>(c);
        const float* b = lr.image.plane<float>(c);
        double s = 1.0, o = 0.0;
        if (allowRescale) {
            // Fit out = s*in + o from the data extremes.
            float amin = a[0], amax = a[0], bmin = b[0], bmax = b[0];
            for (std::size_t i = 0; i < n; ++i) {
                amin = std::min(amin, a[i]); amax = std::max(amax, a[i]);
                bmin = std::min(bmin, b[i]); bmax = std::max(bmax, b[i]);
            }
            if (amax > amin) { s = double(bmax - bmin) / double(amax - amin); o = bmin - s * amin; }
        }
        for (std::size_t i = 0; i < n; ++i)
            worst = std::max(worst, std::fabs(double(b[i]) - (s * double(a[i]) + o)));
    }
    return worst;
}

NS_TEST(fits_roundtrip_float_exact) {
    QTemporaryDir tmp;
    NS_CHECK(tmp.isValid());
    ImageData img = makePattern(64, 48, 1, -3.5f, 1200.0f);
    NS_CHECK(roundTripError(tmp.path(), "fits", img, false) < 1e-4);
}

NS_TEST(fits_roundtrip_rgb) {
    QTemporaryDir tmp;
    ImageData img = makePattern(32, 32, 3, 0.0f, 1.0f);
    NS_CHECK(roundTripError(tmp.path(), "fits", img, false) < 1e-5);
}

NS_TEST(xisf_roundtrip_unit_range_exact) {
    // Data already in [0,1] must round-trip without rescaling.
    QTemporaryDir tmp;
    ImageData img = makePattern(64, 48, 3, 0.0f, 1.0f);
    NS_CHECK(roundTripError(tmp.path(), "xisf", img, false) < 1e-5);
}

NS_TEST(xisf_writes_normalized_floats) {
    // PixInsight interop: float XISF output must land in [0,1] even when the
    // source range is wild (the writer records NSSCALE/NSZERO instead).
    QTemporaryDir tmp;
    ImageData img = makePattern(48, 32, 1, 100.0f, 60000.0f);
    const QString path = tmp.path() + "/norm.xisf";
    NS_CHECK(io::saveImage(path, img, {}).ok);
    io::LoadResult lr = io::loadImage(path);
    NS_CHECK(lr.ok);
    const float* p = lr.image.plane<float>(0);
    float mn = p[0], mx = p[0];
    for (std::size_t i = 0; i < lr.image.samplesPerChannel(); ++i) {
        mn = std::min(mn, p[i]); mx = std::max(mx, p[i]);
    }
    NS_CHECK(mn >= -1e-4f && mx <= 1.0f + 1e-4f);
    // And the shape survives the linear rescale.
    NS_CHECK(roundTripError(tmp.path(), "xisf", img, true) < 1e-3 * 60000.0);
}

NS_TEST(tiff_roundtrip_16bit) {
    // 16-bit TIFF: quantization error bounded by 1/65535 of the range.
    QTemporaryDir tmp;
    ImageData img = makePattern(64, 48, 1, 0.0f, 1.0f);
    NS_CHECK(roundTripError(tmp.path(), "tiff", img, true) < 2.0 / 65535.0);
}

int main() { return nstest::runAll(); }
