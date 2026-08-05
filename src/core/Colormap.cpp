#include "core/Colormap.h"
#include <array>
#include <algorithm>

namespace astro {

// Each map = 9 anchor colours at t = 0, 1/8, ... , 1. The perceptual maps
// (Viridis/Magma/Inferno/Cividis) use sampled values from the originals; good
// enough for display without embedding full 256-entry tables.
struct Anchors { std::array<std::array<int,3>,9> c; };

static const Anchors& anchorsFor(Colormap m) {
    static const Anchors gray = {{{ {{0,0,0}},{{32,32,32}},{{64,64,64}},{{96,96,96}},
        {{128,128,128}},{{160,160,160}},{{192,192,192}},{{224,224,224}},{{255,255,255}} }}};
    static const Anchors heat = {{{ {{0,0,0}},{{60,0,0}},{{120,0,0}},{{200,30,0}},
        {{255,90,0}},{{255,150,0}},{{255,210,40}},{{255,245,140}},{{255,255,255}} }}};
    static const Anchors viridis = {{{ {{68,1,84}},{{70,50,127}},{{54,92,141}},{{39,127,142}},
        {{31,161,135}},{{74,194,109}},{{159,218,58}},{{220,227,42}},{{253,231,37}} }}};
    static const Anchors magma = {{{ {{0,0,4}},{{28,16,68}},{{79,18,123}},{{129,37,129}},
        {{181,54,122}},{{229,80,100}},{{251,135,97}},{{254,194,135}},{{252,253,191}} }}};
    static const Anchors inferno = {{{ {{0,0,4}},{{31,12,72}},{{85,15,109}},{{136,34,106}},
        {{186,54,85}},{{227,89,51}},{{249,140,10}},{{249,201,50}},{{252,255,164}} }}};
    static const Anchors cividis = {{{ {{0,32,76}},{{0,42,102}},{{47,68,105}},{{86,91,105}},
        {{124,114,106}},{{165,139,99}},{{208,166,84}},{{247,196,64}},{{255,233,69}} }}};
    switch (m) {
        case Colormap::Heat:    return heat;
        case Colormap::Viridis: return viridis;
        case Colormap::Magma:   return magma;
        case Colormap::Inferno: return inferno;
        case Colormap::Cividis: return cividis;
        case Colormap::Gray:
        default:                return gray;
    }
}

// ---- DS9 colormaps ---------------------------------------------------------
// Control points reproduced as data from SAOImage DS9 (Smithsonian
// Astrophysical Observatory), tksao/colorbar/default.C — the reference
// implementation, so renditions match DS9 exactly.
struct PwlPoint { float x, y; };
struct PwlMap { const PwlPoint* r; int nr; const PwlPoint* g; int ng; const PwlPoint* b; int nb; };
static const PwlPoint kDs9_a_R[] = { {0.0f,0.0f}, {0.25f,0.0f}, {0.5f,1.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_a_G[] = { {0.0f,0.0f}, {0.25f,1.0f}, {0.5f,0.0f}, {0.77f,0.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_a_B[] = { {0.0f,0.0f}, {0.125f,0.0f}, {0.5f,1.0f}, {0.64f,0.5f}, {0.77f,0.0f}, {1.0f,0.0f} };
static const PwlMap kDs9_a = { kDs9_a_R, 4, kDs9_a_G, 5, kDs9_a_B, 6 };
static const PwlPoint kDs9_b_R[] = { {0.0f,0.0f}, {0.25f,0.0f}, {0.5f,1.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_b_G[] = { {0.0f,0.0f}, {0.5f,0.0f}, {0.75f,1.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_b_B[] = { {0.0f,0.0f}, {0.25f,1.0f}, {0.5f,0.0f}, {0.75f,0.0f}, {1.0f,1.0f} };
static const PwlMap kDs9_b = { kDs9_b_R, 4, kDs9_b_G, 4, kDs9_b_B, 5 };
static const PwlPoint kDs9_bb_R[] = { {0.0f,0.0f}, {0.5f,1.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_bb_G[] = { {0.0f,0.0f}, {0.25f,0.0f}, {0.75f,1.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_bb_B[] = { {0.0f,0.0f}, {0.5f,0.0f}, {1.0f,1.0f} };
static const PwlMap kDs9_bb = { kDs9_bb_R, 3, kDs9_bb_G, 4, kDs9_bb_B, 3 };
static const PwlPoint kDs9_he_R[] = { {0.0f,0.0f}, {0.015f,0.5f}, {0.25f,0.5f}, {0.5f,0.75f}, {1.0f,1.0f} };
static const PwlPoint kDs9_he_G[] = { {0.0f,0.0f}, {0.065f,0.0f}, {0.125f,0.5f}, {0.25f,0.75f}, {0.5f,0.81f}, {1.0f,1.0f} };
static const PwlPoint kDs9_he_B[] = { {0.0f,0.0f}, {0.015f,0.125f}, {0.03f,0.375f}, {0.065f,0.625f}, {0.25f,0.25f}, {1.0f,1.0f} };
static const PwlMap kDs9_he = { kDs9_he_R, 5, kDs9_he_G, 6, kDs9_he_B, 6 };
static const PwlPoint kDs9_cool_R[] = { {0.0f,0.0f}, {0.29f,0.0f}, {0.76f,0.1f}, {1.0f,1.0f} };
static const PwlPoint kDs9_cool_G[] = { {0.0f,0.0f}, {0.22f,0.0f}, {0.96f,1.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_cool_B[] = { {0.0f,0.0f}, {0.53f,1.0f}, {1.0f,1.0f} };
static const PwlMap kDs9_cool = { kDs9_cool_R, 4, kDs9_cool_G, 4, kDs9_cool_B, 3 };
static const PwlPoint kDs9_rainbow_R[] = { {0.0f,1.0f}, {0.2f,0.0f}, {0.6f,0.0f}, {0.8f,1.0f}, {1.0f,1.0f} };
static const PwlPoint kDs9_rainbow_G[] = { {0.0f,0.0f}, {0.2f,0.0f}, {0.4f,1.0f}, {0.8f,1.0f}, {1.0f,0.0f} };
static const PwlPoint kDs9_rainbow_B[] = { {0.0f,1.0f}, {0.4f,1.0f}, {0.6f,0.0f}, {1.0f,0.0f} };
static const PwlMap kDs9_rainbow = { kDs9_rainbow_R, 5, kDs9_rainbow_G, 5, kDs9_rainbow_B, 4 };
static const PwlPoint kDs9_standard_R[] = { {0.0f,0.0f}, {0.333f,0.3f}, {0.333f,0.0f}, {0.666f,0.3f}, {0.666f,0.3f}, {1.0f,1.0f} };
static const PwlPoint kDs9_standard_G[] = { {0.0f,0.0f}, {0.333f,0.3f}, {0.333f,0.3f}, {0.666f,1.0f}, {0.666f,0.0f}, {1.0f,0.3f} };
static const PwlPoint kDs9_standard_B[] = { {0.0f,0.0f}, {0.333f,1.0f}, {0.333f,0.0f}, {0.666f,0.3f}, {0.666f,0.0f}, {1.0f,0.3f} };
static const PwlMap kDs9_standard = { kDs9_standard_R, 6, kDs9_standard_G, 6, kDs9_standard_B, 6 };

struct LutColor { float r, g, b; };
static const LutColor kDs9_i8[] = {
    {0.0f,0.0f,0.0f}, {0.0f,1.0f,0.0f}, {0.0f,0.0f,1.0f}, {0.0f,1.0f,1.0f},
    {1.0f,0.0f,0.0f}, {1.0f,1.0f,0.0f}, {1.0f,0.0f,1.0f}, {1.0f,1.0f,1.0f}
};
static const int kDs9_i8_n = 8;
static const LutColor kDs9_aips0[] = {
    {0.196f,0.196f,0.196f}, {0.475f,0.0f,0.608f}, {0.0f,0.0f,0.785f}, {0.373f,0.655f,0.925f},
    {0.0f,0.596f,0.0f}, {0.0f,0.965f,0.0f}, {1.0f,1.0f,0.0f}, {1.0f,0.694f,0.0f},
    {1.0f,0.0f,0.0f}
};
static const int kDs9_aips0_n = 9;
static const LutColor kDs9_sls[] = {
    {0.0f,0.0f,0.0f}, {0.043442f,0.0f,0.052883f}, {0.086883f,0.0f,0.105767f}, {0.130325f,0.0f,0.15865f},
    {0.173767f,0.0f,0.211533f}, {0.217208f,0.0f,0.264417f}, {0.26065f,0.0f,0.3173f}, {0.304092f,0.0f,0.370183f},
    {0.347533f,0.0f,0.423067f}, {0.390975f,0.0f,0.47595f}, {0.434417f,0.0f,0.528833f}, {0.477858f,0.0f,0.581717f},
    {0.5213f,0.0f,0.6346f}, {0.506742f,0.0f,0.640217f}, {0.492183f,0.0f,0.645833f}, {0.477625f,0.0f,0.65145f},
    {0.463067f,0.0f,0.657067f}, {0.448508f,0.0f,0.662683f}, {0.43395f,0.0f,0.6683f}, {0.419392f,0.0f,0.673917f},
    {0.404833f,0.0f,0.679533f}, {0.390275f,0.0f,0.68515f}, {0.375717f,0.0f,0.690767f}, {0.361158f,0.0f,0.696383f},
    {0.3466f,0.0f,0.702f}, {0.317717f,0.0f,0.712192f}, {0.288833f,0.0f,0.722383f}, {0.25995f,0.0f,0.732575f},
    {0.231067f,0.0f,0.742767f}, {0.202183f,0.0f,0.752958f}, {0.1733f,0.0f,0.76315f}, {0.144417f,0.0f,0.773342f},
    {0.115533f,0.0f,0.783533f}, {0.08665f,0.0f,0.793725f}, {0.057767f,0.0f,0.803917f}, {0.028883f,0.0f,0.814108f},
    {0.0f,0.0f,0.8243f}, {0.0f,0.019817f,0.838942f}, {0.0f,0.039633f,0.853583f}, {0.0f,0.05945f,0.868225f},
    {0.0f,0.079267f,0.882867f}, {0.0f,0.099083f,0.897508f}, {0.0f,0.1189f,0.91215f}, {0.0f,0.138717f,0.926792f},
    {0.0f,0.158533f,0.941433f}, {0.0f,0.17835f,0.956075f}, {0.0f,0.198167f,0.970717f}, {0.0f,0.217983f,0.985358f},
    {0.0f,0.2378f,1.0f}, {0.0f,0.268533f,1.0f}, {0.0f,0.299267f,1.0f}, {0.0f,0.33f,1.0f},
    {0.0f,0.360733f,1.0f}, {0.0f,0.391467f,1.0f}, {0.0f,0.4222f,1.0f}, {0.0f,0.452933f,1.0f},
    {0.0f,0.483667f,1.0f}, {0.0f,0.5144f,1.0f}, {0.0f,0.545133f,1.0f}, {0.0f,0.575867f,1.0f},
    {0.0f,0.6066f,1.0f}, {0.0f,0.631733f,0.9753f}, {0.0f,0.656867f,0.9506f}, {0.0f,0.682f,0.9259f},
    {0.0f,0.707133f,0.9012f}, {0.0f,0.732267f,0.8765f}, {0.0f,0.7574f,0.8518f}, {0.0f,0.782533f,0.8271f},
    {0.0f,0.807667f,0.8024f}, {0.0f,0.8328f,0.7777f}, {0.0f,0.857933f,0.753f}, {0.0f,0.883067f,0.7283f},
    {0.0f,0.9082f,0.7036f}, {0.0f,0.901908f,0.676675f}, {0.0f,0.895617f,0.64975f}, {0.0f,0.889325f,0.622825f},
    {0.0f,0.883033f,0.5959f}, {0.0f,0.876742f,0.568975f}, {0.0f,0.87045f,0.54205f}, {0.0f,0.864158f,0.515125f},
    {0.0f,0.857867f,0.4882f}, {0.0f,0.851575f,0.461275f}, {0.0f,0.845283f,0.43435f}, {0.0f,0.838992f,0.407425f},
    {0.0f,0.8327f,0.3805f}, {0.0f,0.832308f,0.354858f}, {0.0f,0.831917f,0.329217f}, {0.0f,0.831525f,0.303575f},
    {0.0f,0.831133f,0.277933f}, {0.0f,0.830742f,0.252292f}, {0.0f,0.83035f,0.22665f}, {0.0f,0.829958f,0.201008f},
    {0.0f,0.829567f,0.175367f}, {0.0f,0.829175f,0.149725f}, {0.0f,0.828783f,0.124083f}, {0.0f,0.828392f,0.098442f},
    {0.0f,0.828f,0.0728f}, {0.033167f,0.834167f,0.066733f}, {0.066333f,0.840333f,0.060667f}, {0.0995f,0.8465f,0.0546f},
    {0.132667f,0.852667f,0.048533f}, {0.165833f,0.858833f,0.042467f}, {0.199f,0.865f,0.0364f}, {0.232167f,0.871167f,0.030333f},
    {0.265333f,0.877333f,0.024267f}, {0.2985f,0.8835f,0.0182f}, {0.331667f,0.889667f,0.012133f}, {0.364833f,0.895833f,0.006067f},
    {0.398f,0.902f,0.0f}, {0.43095f,0.902f,0.0f}, {0.4639f,0.902f,0.0f}, {0.49685f,0.902f,0.0f},
    {0.5298f,0.902f,0.0f}, {0.56275f,0.902f,0.0f}, {0.5957f,0.902f,0.0f}, {0.62865f,0.902f,0.0f},
    {0.6616f,0.902f,0.0f}, {0.69455f,0.902f,0.0f}, {0.7275f,0.902f,0.0f}, {0.76045f,0.902f,0.0f},
    {0.7934f,0.902f,0.0f}, {0.810617f,0.897133f,0.003983f}, {0.827833f,0.892267f,0.007967f}, {0.84505f,0.8874f,0.01195f},
    {0.862267f,0.882533f,0.015933f}, {0.879483f,0.877667f,0.019917f}, {0.8967f,0.8728f,0.0239f}, {0.913917f,0.867933f,0.027883f},
    {0.931133f,0.863067f,0.031867f}, {0.94835f,0.8582f,0.03585f}, {0.965567f,0.853333f,0.039833f}, {0.982783f,0.848467f,0.043817f},
    {1.0f,0.8436f,0.0478f}, {0.995725f,0.824892f,0.0516f}, {0.99145f,0.806183f,0.0554f}, {0.987175f,0.787475f,0.0592f},
    {0.9829f,0.768767f,0.063f}, {0.978625f,0.750058f,0.0668f}, {0.97435f,0.73135f,0.0706f}, {0.970075f,0.712642f,0.0744f},
    {0.9658f,0.693933f,0.0782f}, {0.961525f,0.675225f,0.082f}, {0.95725f,0.656517f,0.0858f}, {0.952975f,0.637808f,0.0896f},
    {0.9487f,0.6191f,0.0934f}, {0.952975f,0.600408f,0.085617f}, {0.95725f,0.581717f,0.077833f}, {0.961525f,0.563025f,0.07005f},
    {0.9658f,0.544333f,0.062267f}, {0.970075f,0.525642f,0.054483f}, {0.97435f,0.50695f,0.0467f}, {0.978625f,0.488258f,0.038917f},
    {0.9829f,0.469567f,0.031133f}, {0.987175f,0.450875f,0.02335f}, {0.99145f,0.432183f,0.015567f}, {0.995725f,0.413492f,0.007783f},
    {1.0f,0.3948f,0.0f}, {0.998342f,0.3619f,0.0f}, {0.996683f,0.329f,0.0f}, {0.995025f,0.2961f,0.0f},
    {0.993367f,0.2632f,0.0f}, {0.991708f,0.2303f,0.0f}, {0.99005f,0.1974f,0.0f}, {0.988392f,0.1645f,0.0f},
    {0.986733f,0.1316f,0.0f}, {0.985075f,0.0987f,0.0f}, {0.983417f,0.0658f,0.0f}, {0.981758f,0.0329f,0.0f},
    {0.9801f,0.0f,0.0f}, {0.955925f,0.0f,0.0f}, {0.93175f,0.0f,0.0f}, {0.907575f,0.0f,0.0f},
    {0.8834f,0.0f,0.0f}, {0.859225f,0.0f,0.0f}, {0.83505f,0.0f,0.0f}, {0.810875f,0.0f,0.0f},
    {0.7867f,0.0f,0.0f}, {0.762525f,0.0f,0.0f}, {0.73835f,0.0f,0.0f}, {0.714175f,0.0f,0.0f},
    {0.69f,0.0f,0.0f}, {0.715833f,0.083333f,0.083333f}, {0.741667f,0.166667f,0.166667f}, {0.7675f,0.25f,0.25f},
    {0.793333f,0.333333f,0.333333f}, {0.819167f,0.416667f,0.416667f}, {0.845f,0.5f,0.5f}, {0.870833f,0.583333f,0.583333f},
    {0.896667f,0.666667f,0.666667f}, {0.9225f,0.75f,0.75f}, {0.948333f,0.833333f,0.833333f}, {0.974167f,0.916667f,0.916667f},
    {1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f},
    {1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f}
};
static const int kDs9_sls_n = 200;

// Piecewise-linear evaluation of one DS9 channel at u in [0,1].
static inline float pwlEval(const PwlPoint* pts, int n, double u) {
    if (u <= pts[0].x) return pts[0].y;
    for (int i = 1; i < n; ++i)
        if (u <= pts[i].x) {
            const double dx = pts[i].x - pts[i - 1].x;
            const double f = dx > 0 ? (u - pts[i - 1].x) / dx : 1.0;
            return float(pts[i - 1].y + (pts[i].y - pts[i - 1].y) * f);
        }
    return pts[n - 1].y;
}

static const PwlMap* ds9PwlFor(Colormap c) {
    switch (c) {
        case Colormap::Ds9A:        return &kDs9_a;
        case Colormap::Ds9B:        return &kDs9_b;
        case Colormap::Ds9BB:       return &kDs9_bb;
        case Colormap::Ds9HE:       return &kDs9_he;
        case Colormap::Ds9Cool:     return &kDs9_cool;
        case Colormap::Ds9Rainbow:  return &kDs9_rainbow;
        case Colormap::Ds9Standard: return &kDs9_standard;
        default:                    return nullptr;
    }
}

static const LutColor* ds9LutFor(Colormap c, int* n) {
    switch (c) {
        case Colormap::Ds9I8:    *n = kDs9_i8_n;    return kDs9_i8;
        case Colormap::Ds9AIPS0: *n = kDs9_aips0_n; return kDs9_aips0;
        case Colormap::Ds9SLS:   *n = kDs9_sls_n;   return kDs9_sls;
        default:                 *n = 0;            return nullptr;
    }
}

static inline std::uint8_t to8(float v) {
    const int i = int(v * 255.0f + 0.5f);
    return std::uint8_t(i < 0 ? 0 : (i > 255 ? 255 : i));
}

const char* colormapName(Colormap c) {
    switch (c) {
        case Colormap::Gray:     return "Gray";
        case Colormap::Heat:     return "Heat";
        case Colormap::Viridis:  return "Viridis";
        case Colormap::Magma:    return "Magma";
        case Colormap::Inferno:  return "Inferno";
        case Colormap::Cividis:  return "Cividis";
        case Colormap::Ds9A:        return "a (DS9)";
        case Colormap::Ds9B:        return "b (DS9)";
        case Colormap::Ds9BB:       return "bb (DS9)";
        case Colormap::Ds9HE:       return "he (DS9)";
        case Colormap::Ds9Cool:     return "cool (DS9)";
        case Colormap::Ds9Rainbow:  return "rainbow (DS9)";
        case Colormap::Ds9Standard: return "standard (DS9)";
        case Colormap::Ds9I8:       return "i8 (DS9)";
        case Colormap::Ds9AIPS0:    return "aips0 (DS9)";
        case Colormap::Ds9SLS:      return "sls (DS9)";
    }
    return "Gray";
}

// Fold the input at the split threshold: at t==T -> 0 (dark base-map end); both
// t==0 and t==1 -> 1 (bright end). Below T the ramp is inverted, above it normal.
static inline double splitFold(double t, double T) {
    T = T < 0 ? 0 : (T > 1 ? 1 : T);
    if (t >= T) return (T >= 1.0) ? 0.0 : (t - T) / (1.0 - T);
    return (T <= 0.0) ? 0.0 : (T - t) / T;
}

// Interpolate the 9 anchors of a base map at u in [0,1].
static inline void sampleAnchors(const Anchors& a, double u, std::uint8_t out[3]) {
    u = u < 0 ? 0 : (u > 1 ? 1 : u);
    const double seg = u * 8.0;
    int s = int(seg); if (s > 7) s = 7;
    const double f = seg - s;
    for (int k = 0; k < 3; ++k) {
        const double v = a.c[s][k] + (a.c[s + 1][k] - a.c[s][k]) * f;
        out[k] = std::uint8_t(std::clamp(int(v + 0.5), 0, 255));
    }
}

std::vector<std::uint8_t> buildColormapLut(Colormap c, const ColormapMods& mods, int n) {
    std::vector<std::uint8_t> lut(std::size_t(n) * 3);
    const PwlMap* pwl = ds9PwlFor(c);
    int lutN = 0;
    const LutColor* stepped = ds9LutFor(c, &lutN);
    const Anchors& a = anchorsFor(c);
    for (int i = 0; i < n; ++i) {
        const double t = (n == 1) ? 0.0 : double(i) / (n - 1);   // [0,1]
        double u = mods.split ? splitFold(t, mods.splitT) : t;   // fold, then
        if (mods.invert) u = 1.0 - u;                            // reverse, then
        std::uint8_t* out = &lut[std::size_t(i) * 3];
        if (pwl) {                                   // DS9 piecewise-linear
            out[0] = to8(pwlEval(pwl->r, pwl->nr, u));
            out[1] = to8(pwlEval(pwl->g, pwl->ng, u));
            out[2] = to8(pwlEval(pwl->b, pwl->nb, u));
        } else if (stepped) {                        // DS9 stepped colour table
            // Epsilon-nudged floor: u arrives via different arithmetic under
            // invert/split (1-u vs u), and a 1-ULP difference at an exact band
            // boundary must not select adjacent bands.
            int s = int(u * lutN - 1e-6); if (s >= lutN) s = lutN - 1; if (s < 0) s = 0;
            out[0] = to8(stepped[s].r);
            out[1] = to8(stepped[s].g);
            out[2] = to8(stepped[s].b);
        } else {
            sampleAnchors(a, u, out);                // native 9-anchor maps
        }
    }
    return lut;
}

} // namespace astro
