// Colormap tests: LUT shape, the Gray ramp, and the invert/split modifier
// algebra that must hold for every base map.
#include "nstest.h"
#include "core/Colormap.h"
#include <cstring>

using namespace astro;

static const Colormap kAll[] = { Colormap::Gray, Colormap::Heat, Colormap::Viridis,
                                 Colormap::Magma, Colormap::Inferno, Colormap::Cividis,
                                 Colormap::Ds9A, Colormap::Ds9B, Colormap::Ds9BB,
                                 Colormap::Ds9HE, Colormap::Ds9Cool, Colormap::Ds9Rainbow,
                                 Colormap::Ds9Standard, Colormap::Ds9I8, Colormap::Ds9AIPS0,
                                 Colormap::Ds9SLS };

NS_TEST(colormap_lut_shape_and_names) {
    NS_CHECK(kColormapCount == 16);
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

// DS9 palettes must match DS9's reference control points exactly.
NS_TEST(colormap_ds9_reference_values) {
    // a: piecewise-linear — black at 0; at u=0.5 R=1,G=0,B=1 (magenta);
    // yellow at 1 (R=1,G=1,B=0).
    auto a = buildColormapLut(Colormap::Ds9A, {}, 256);
    NS_CHECK(a[0] == 0 && a[1] == 0 && a[2] == 0);
    { const int i = 128; // u ~ 0.502: R=1, G~0, B~1
      NS_CHECK(a[i*3] == 255 && a[i*3+1] <= 3 && a[i*3+2] >= 250); }
    NS_CHECK(a[255*3] == 255 && a[255*3+1] == 255 && a[255*3+2] == 0);

    // i8: stepped 8-colour table — first eighth black, second green,
    // last white; NO interpolation between steps.
    auto i8 = buildColormapLut(Colormap::Ds9I8, {}, 256);
    NS_CHECK(i8[10*3] == 0 && i8[10*3+1] == 0 && i8[10*3+2] == 0);          // black
    NS_CHECK(i8[40*3] == 0 && i8[40*3+1] == 255 && i8[40*3+2] == 0);        // green
    NS_CHECK(i8[250*3] == 255 && i8[250*3+1] == 255 && i8[250*3+2] == 255); // white
    // Step boundary is sharp: samples inside one band are identical.
    NS_CHECK(i8[33*3+1] == i8[62*3+1]);

    // aips0: first band is the AIPS grey (.196), last is red.
    auto ap = buildColormapLut(Colormap::Ds9AIPS0, {}, 256);
    NS_CHECK(ap[5*3] == 50 && ap[5*3+1] == 50 && ap[5*3+2] == 50);          // .196*255
    NS_CHECK(ap[255*3] == 255 && ap[255*3+1] == 0 && ap[255*3+2] == 0);

    // bb: black-body run black -> red -> yellow -> white.
    auto bb = buildColormapLut(Colormap::Ds9BB, {}, 256);
    NS_CHECK(bb[0] == 0 && bb[255*3] == 255 && bb[255*3+1] == 255 && bb[255*3+2] == 255);
    { const int i = 128; NS_CHECK(bb[i*3] > 200 && bb[i*3+2] < 30); }       // hot red zone

    // sls: 200-entry table, ends black -> white.
    auto sls = buildColormapLut(Colormap::Ds9SLS, {}, 256);
    NS_CHECK(sls[0] == 0 && sls[255*3] == 255 && sls[255*3+1] == 255 && sls[255*3+2] == 255);
}
