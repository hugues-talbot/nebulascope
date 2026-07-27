// Colormap tests: LUT shape, the Gray ramp, and the invert/split modifier
// algebra that must hold for every base map.
#include "nstest.h"
#include "core/Colormap.h"
#include <cstring>

using namespace astro;

static const Colormap kAll[] = { Colormap::Gray, Colormap::Heat, Colormap::Viridis,
                                 Colormap::Magma, Colormap::Inferno, Colormap::Cividis };

NS_TEST(colormap_lut_shape_and_names) {
    NS_CHECK(kColormapCount == 6);
    for (Colormap c : kAll) {
        NS_CHECK(colormapName(c) != nullptr && std::strlen(colormapName(c)) > 0);
        auto lut = buildColormapLut(c, {}, 256);
        NS_CHECK(lut.size() == 256 * 3);
    }
}

NS_TEST(colormap_gray_is_identity_ramp) {
    auto lut = buildColormapLut(Colormap::Gray, {}, 256);
    for (int i = 0; i < 256; ++i) {
        NS_CHECK(lut[i * 3] == lut[i * 3 + 1] && lut[i * 3] == lut[i * 3 + 2]);
        if (i) NS_CHECK(lut[i * 3] >= lut[(i - 1) * 3]);   // monotone
    }
    NS_CHECK(lut[0] == 0 && lut[255 * 3] == 255);
}

NS_TEST(colormap_invert_reverses_every_map) {
    // Sample points are symmetric, so inversion is an exact reversal.
    for (Colormap c : kAll) {
        auto plain = buildColormapLut(c, {}, 256);
        ColormapMods m; m.invert = true;
        auto inv = buildColormapLut(c, m, 256);
        bool ok = true;
        for (int i = 0; i < 256 && ok; ++i)
            for (int k = 0; k < 3; ++k)
                if (inv[i * 3 + k] != plain[(255 - i) * 3 + k]) { ok = false; break; }
        NS_CHECK(ok);
    }
}

NS_TEST(colormap_split_folds_gray) {
    // Gray + split: reversed-contrast ramp below the threshold (descending),
    // normal ramp above (ascending) — the fold the display uses for faint
    // background inspection.
    ColormapMods m; m.split = true; m.splitT = 0.5;
    auto lut = buildColormapLut(Colormap::Gray, m, 256);
    for (int i = 1; i < 124; ++i)                        // safely below the fold
        NS_CHECK(lut[i * 3] <= lut[(i - 1) * 3]);
    for (int i = 132; i < 256; ++i)                      // safely above it
        NS_CHECK(lut[i * 3] >= lut[(i - 1) * 3]);
}

NS_TEST(colormap_active_logic) {
    NS_CHECK(!colormapActive(Colormap::Gray, {}));
    NS_CHECK(colormapActive(Colormap::Viridis, {}));
    ColormapMods inv; inv.invert = true;
    NS_CHECK(colormapActive(Colormap::Gray, inv));
    ColormapMods sp; sp.split = true;
    NS_CHECK(colormapActive(Colormap::Gray, sp));
}

int main() { return nstest::runAll(); }
