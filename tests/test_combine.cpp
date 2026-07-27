// ChannelCombine + ColorTransport tests: weight routing, pre-normalization,
// error paths, and the distribution-transfer invariants (identity, collapse
// to a constant reference). Transport is seeded, so results are reproducible.
#include "nstest.h"
#include "core/ChannelCombine.h"
#include "core/ColorTransport.h"
#include <cmath>
#include <vector>

using namespace astro;

NS_TEST(combine_routes_weights) {
    // Two planes with pure R / pure G weights: outputs are the inputs.
    const int W = 4, H = 2;
    std::vector<float> a{ 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
    std::vector<float> b{ 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f };
    std::vector<CombinePlane> planes(2);
    planes[0].data = a.data(); planes[0].wR = 1.0;
    planes[1].data = b.data(); planes[1].wG = 1.0;
    CombineResult r = combineChannels(W, H, planes, PreNorm::None,
                                      nullptr, LumMode::None, 0.0);
    NS_CHECK(r.ok);
    NS_CHECK(r.image.channels() == 3 && r.image.width() == W && r.image.height() == H);
    for (std::size_t i = 0; i < a.size(); ++i) {
        NS_CHECK_NEAR(r.image.plane<float>(0)[i], a[i], 1e-6);
        NS_CHECK_NEAR(r.image.plane<float>(1)[i], b[i], 1e-6);
        NS_CHECK_NEAR(r.image.plane<float>(2)[i], 0.0, 1e-6);
    }
}

NS_TEST(combine_weighted_sum) {
    const int W = 2, H = 1;
    std::vector<float> a{ 0.2f, 0.4f };
    std::vector<CombinePlane> planes(1);
    planes[0].data = a.data();
    planes[0].wR = 1.0; planes[0].wG = 0.5; planes[0].wB = 0.25;
    CombineResult r = combineChannels(W, H, planes, PreNorm::None,
                                      nullptr, LumMode::None, 0.0);
    NS_CHECK(r.ok);
    NS_CHECK_NEAR(r.image.plane<float>(1)[1], 0.2, 1e-6);   // 0.4 * 0.5
    NS_CHECK_NEAR(r.image.plane<float>(2)[0], 0.05, 1e-6);  // 0.2 * 0.25
}

NS_TEST(combine_minmax_prenorm) {
    // MinMax rescales each plane to [0,1] before weighting: a 10..20 ramp
    // contributes exactly like a 0..1 ramp.
    const int W = 3, H = 1;
    std::vector<float> a{ 10.0f, 15.0f, 20.0f };
    std::vector<CombinePlane> planes(1);
    planes[0].data = a.data(); planes[0].wR = 1.0;
    CombineResult r = combineChannels(W, H, planes, PreNorm::MinMax,
                                      nullptr, LumMode::None, 0.0);
    NS_CHECK(r.ok);
    NS_CHECK_NEAR(r.image.plane<float>(0)[0], 0.0, 1e-6);
    NS_CHECK_NEAR(r.image.plane<float>(0)[1], 0.5, 1e-6);
    NS_CHECK_NEAR(r.image.plane<float>(0)[2], 1.0, 1e-6);
}

NS_TEST(combine_rejects_empty) {
    CombineResult r = combineChannels(4, 4, {}, PreNorm::None,
                                      nullptr, LumMode::None, 0.0);
    NS_CHECK(!r.ok);
    NS_CHECK(!r.error.empty());
}

static ImageData monoImage(int w, int h, float lo, float hi) {
    ImageData img(w, h, 1, SampleFormat::Float32, ColorSpace::Gray);
    float* p = img.plane<float>(0);
    const std::size_t n = img.samplesPerChannel();
    for (std::size_t i = 0; i < n; ++i)
        p[i] = lo + (hi - lo) * float(i) / float(n - 1);
    return img;
}

NS_TEST(transport_identity_is_noop) {
    // Transporting an image onto itself must (approximately) return it.
    // saturationCut >= 1 disables the bright-end exclusion, which otherwise
    // (by design) caps a full [0,1] ramp at the cut value.
    ImageData src = monoImage(64, 64, 0.0f, 1.0f);
    ColorTransportResult r = transportColors(src, src, 15, 200000, {}, {}, 1.0f);
    NS_CHECK(r.ok);
    const float* a = src.plane<float>(0);
    const float* b = r.image.plane<float>(0);
    double worst = 0.0;
    for (std::size_t i = 0; i < src.samplesPerChannel(); ++i)
        worst = std::max(worst, std::fabs(double(a[i]) - double(b[i])));
    NS_CHECK(worst < 0.02);
}

NS_TEST(transport_collapses_to_constant_ref) {
    // A constant reference has a single-point distribution: every source
    // pixel must land (near) that value.
    ImageData src = monoImage(64, 64, 0.0f, 1.0f);
    ImageData ref(64, 64, 1, SampleFormat::Float32, ColorSpace::Gray);
    float* rp = ref.plane<float>(0);
    for (std::size_t i = 0; i < ref.samplesPerChannel(); ++i) rp[i] = 0.3f;
    ColorTransportResult r = transportColors(src, ref);
    NS_CHECK(r.ok);
    const float* b = r.image.plane<float>(0);
    for (std::size_t i = 0; i < src.samplesPerChannel(); i += 97)
        NS_CHECK(std::fabs(b[i] - 0.3f) < 0.02f);
}

NS_TEST(transport_rejects_channel_mismatch) {
    ImageData rgb(16, 16, 3, SampleFormat::Float32, ColorSpace::RGB);
    ImageData mono = monoImage(16, 16, 0.0f, 1.0f);
    ColorTransportResult r = transportColors(rgb, mono);
    NS_CHECK(!r.ok);
}

int main() { return nstest::runAll(); }
