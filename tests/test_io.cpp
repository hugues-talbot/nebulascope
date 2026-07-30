// IO round-trip tests: write with each backend, read back, compare pixels.
// These catch format-interop regressions (sample scaling, layout, headers) —
// e.g. the XISF float-normalization requirement is asserted here.
#include "nstest.h"
#include "core/ImageData.h"
#include "io/ImageReader.h"
#include "io/ImageWriter.h"
#include <QFile>
#include <QTemporaryDir>
#include <QString>
#include <cmath>
#include <cstring>

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

NS_TEST(xisf_compressed_roundtrip_all_codecs) {
    // Every codec (with byte shuffling) must engage in the file and round-trip
    // exactly. The XML header is checked so a silent fallback to uncompressed
    // blocks can't masquerade as a pass.
    QTemporaryDir tmp;
    using C = io::SaveOptions::Compression;
    const struct { C codec; const char* attr; } cases[] = {
        { C::None,  nullptr },
        { C::Zlib,  "compression=\"zlib+sh:" },
        { C::LZ4,   "compression=\"lz4+sh:" },
        { C::LZ4HC, "compression=\"lz4hc+sh:" },
        { C::Zstd,  "compression=\"" },   // zstd, or zlib where compiled out
    };
    ImageData img = makePattern(64, 48, 3, 0.0f, 1.0f);
    int idx = 0;
    for (const auto& tc : cases) {
        const QString path = tmp.path() + QString("/codec%1.xisf").arg(idx++);
        io::SaveOptions opts;
        opts.xisfCompression = tc.codec;
        NS_CHECK(io::saveImage(path, img, {}, opts).ok);

        QFile f(path);
        NS_CHECK(f.open(QIODevice::ReadOnly));
        const QByteArray head = f.read(4096);
        if (tc.attr) NS_CHECK(head.contains(tc.attr));
        else         NS_CHECK(!head.contains("compression=\""));

        io::LoadResult lr = io::loadImage(path);
        NS_CHECK(lr.ok && lr.image.isValid());
        double worst = 0.0;
        for (int c = 0; c < img.channels(); ++c) {
            const float* a = img.plane<float>(c);
            const float* b = lr.image.plane<float>(c);
            for (std::size_t i = 0; i < img.samplesPerChannel(); ++i)
                worst = std::max(worst, std::fabs(double(b[i]) - double(a[i])));
        }
        NS_CHECK(worst < 1e-5);
    }
}

NS_TEST(pixinsight_written_xisf_interop) {
    // Fixtures written by PixInsight itself (64x64 RGB crop of an HII
    // region, saved four ways). Every variant must decode to the same
    // pixels — this tests the read direction our own round-trips can't:
    // a symmetric reader/writer bug passes round-trips but fails here.
    const QString dir = QStringLiteral(NS_TESTDATA_DIR);
    io::LoadResult ref = io::loadImage(dir + "/pi_f32_plain.xisf");
    NS_CHECK(ref.ok && ref.image.isValid());
    NS_CHECK(ref.image.width() == 64 && ref.image.height() == 64 &&
             ref.image.channels() == 3);
    // PixInsight float convention: samples in [0,1], with real signal.
    float mn = 1e9f, mx = -1e9f;
    for (int c = 0; c < 3; ++c) {
        const float* p = ref.image.plane<float>(c);
        for (std::size_t i = 0; i < ref.image.samplesPerChannel(); ++i) {
            mn = std::min(mn, p[i]); mx = std::max(mx, p[i]);
        }
    }
    NS_CHECK(mn >= 0.0f && mx <= 1.0f && mx > mn);

    // Compressed variants (zstd+shuffle, zlib+shuffle): bit-identical.
    for (const char* name : { "/pi_f32_zstd.xisf", "/pi_f32_zlib.xisf" }) {
        io::LoadResult r = io::loadImage(dir + name);
        NS_CHECK(r.ok && r.image.isValid());
        bool same = r.image.width() == 64 && r.image.height() == 64 &&
                    r.image.channels() == 3;
        for (int c = 0; same && c < 3; ++c)
            same = std::memcmp(r.image.plane<float>(c), ref.image.plane<float>(c),
                               ref.image.samplesPerChannel() * sizeof(float)) == 0;
        NS_CHECK(same);
    }

    // UInt16 variant: reader promotes + normalizes to [0,1] — must agree
    // with the float original to 16-bit quantization.
    io::LoadResult u16 = io::loadImage(dir + "/pi_u16_plain.xisf");
    NS_CHECK(u16.ok && u16.image.isValid());
    double worst = 0.0;
    for (int c = 0; c < 3; ++c) {
        const float* a = ref.image.plane<float>(c);
        const float* b = u16.image.plane<float>(c);
        for (std::size_t i = 0; i < ref.image.samplesPerChannel(); ++i)
            worst = std::max(worst, std::fabs(double(a[i]) - double(b[i])));
    }
    NS_CHECK(worst <= 1.0 / 65535.0 + 1e-7);
}

NS_TEST(fits_writes_typed_cards) {
    // Numbers must land as NUMERIC cards, not quoted strings — and quoted
    // strings must come out single-quoted, not doubled.
    QTemporaryDir tmp;
    ImageData img = makePattern(8, 8, 1, 0.0f, 1.0f);
    ImageHeader h;
    h.cards.push_back({ "FOCALLEN", "1672.4416", "focal length" });
    h.cards.push_back({ "XBINNING", "2", "binning" });
    h.cards.push_back({ "DATE-OBS", "'2026-04-25T12:00:00.000'", "date" });
    const QString path = tmp.path() + "/typed.fits";
    NS_CHECK(io::saveImage(path, img, h).ok);
    QFile f(path);
    NS_CHECK(f.open(QIODevice::ReadOnly));
    const QByteArray raw = f.read(8 * 2880);
    bool focOk = false, binOk = false, dateOk = false;
    for (int i = 0; i + 80 <= raw.size(); i += 80) {
        const QByteArray c = raw.mid(i, 80);
        if (c.startsWith("FOCALLEN")) focOk  = !c.contains('\'') && c.contains("1672.44");
        if (c.startsWith("XBINNING")) binOk  = !c.contains('\'') && c.contains("2");
        if (c.startsWith("DATE-OBS")) dateOk = c.count('\'') == 2 && c.contains("2026-04-25T12");
        if (c.startsWith("END")) break;
    }
    NS_CHECK(focOk);
    NS_CHECK(binOk);
    NS_CHECK(dateOk);
}

NS_TEST(fits_synthesizes_cards_from_xisf_properties) {
    // PixInsight-native XISF: metadata as properties, no FITS keywords. The
    // FITS writer must synthesize the standard cards (with unit fixes:
    // FocalLength metres → FOCALLEN mm, timestamps lose the trailing Z).
    QTemporaryDir tmp;
    ImageData img = makePattern(8, 8, 1, 0.0f, 1.0f);
    ImageHeader h;                                    // no cards at all
    h.properties["Observation:Time:Start"] = "2025-12-27T00:45:46.943Z";
    h.properties["Instrument:Telescope:FocalLength"] = "2.040398";
    h.properties["Instrument:Sensor:XPixelSize"] = "7.52";
    h.properties["Instrument:ExposureTime"] = "300";
    h.properties["Instrument:Camera:Name"] = "ZWO ASI1600MM Pro";
    const QString path = tmp.path() + "/props.fits";
    NS_CHECK(io::saveImage(path, img, h).ok);
    io::LoadResult lr = io::loadImage(path);
    NS_CHECK(lr.ok);
    auto value = [&lr](const char* key) {
        for (const auto& c : lr.header.cards)
            if (c.key.compare(QLatin1String(key), Qt::CaseInsensitive) == 0)
                return c.value;
        return QString();
    };
    NS_CHECK(value("DATE-OBS").contains("2025-12-27T00:45:46.943"));
    NS_CHECK(!value("DATE-OBS").contains("Z"));
    NS_CHECK(std::fabs(value("FOCALLEN").remove('\'').toDouble() - 2040.398) < 0.01);
    NS_CHECK(std::fabs(value("XPIXSZ").remove('\'').toDouble() - 7.52) < 1e-6);
    NS_CHECK(std::fabs(value("EXPTIME").remove('\'').toDouble() - 300.0) < 1e-6);
    NS_CHECK(value("INSTRUME").contains("ASI1600MM"));
}

NS_TEST(tiff_large_image_reads_back) {
    // Regression: Qt 6's QImageReader allocation limit (128 MB default)
    // rejected big 16-bit TIFFs on READ that we ourselves had written —
    // 16-bit RGB decodes to 8 bytes/pixel, so ~16 Mpx tripped it while
    // Preview opened the same file happily. The reader must lift the limit.
    QTemporaryDir tmp;
    const int W = 6200, H = 6200;                  // 6200² × 8 B ≈ 307 MB decoded
    ImageData img(W, H, 3, SampleFormat::Float32, ColorSpace::RGB);
    for (int c = 0; c < 3; ++c) {
        float* p = img.plane<float>(c);
        for (std::size_t i = 0; i < img.samplesPerChannel(); ++i)
            p[i] = float((i * (c + 2)) % 1000) / 999.0f;
    }
    const QString path = tmp.path() + "/big.tiff";
    NS_CHECK(io::saveImage(path, img, {}).ok);
    io::LoadResult lr = io::loadImage(path);
    NS_CHECK(lr.ok);
    NS_CHECK(lr.image.width() == W && lr.image.height() == H &&
             lr.image.channels() == 3);
}

NS_TEST(tiff_roundtrip_16bit) {
    // 16-bit TIFF: quantization error bounded by 1/65535 of the range.
    QTemporaryDir tmp;
    ImageData img = makePattern(64, 48, 1, 0.0f, 1.0f);
    NS_CHECK(roundTripError(tmp.path(), "tiff", img, true) < 2.0 / 65535.0);
}

int main() { return nstest::runAll(); }
