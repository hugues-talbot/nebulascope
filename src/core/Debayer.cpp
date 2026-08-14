#include "core/Debayer.h"
#include "core/ImageHeader.h"
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace astro {

// ---- pattern plumbing -------------------------------------------------------

namespace {
// colour of CFA site (x,y): 0=R 1=G 2=B, from the pattern's 2×2 matrix.
struct PatternMap { unsigned char m[2][2]; };   // [y&1][x&1]
PatternMap mapOf(BayerPattern p) {
    switch (p) {
        case BayerPattern::RGGB: return {{{0,1},{1,2}}};
        case BayerPattern::BGGR: return {{{2,1},{1,0}}};
        case BayerPattern::GRBG: return {{{1,0},{2,1}}};
        case BayerPattern::GBRG: return {{{1,2},{0,1}}};
        default:                 return {{{1,1},{1,1}}};
    }
}
BayerPattern fromMap(const PatternMap& pm) {
    for (BayerPattern p : { BayerPattern::RGGB, BayerPattern::BGGR,
                            BayerPattern::GRBG, BayerPattern::GBRG })
        if (std::memcmp(mapOf(p).m, pm.m, 4) == 0) return p;
    return BayerPattern::None;
}
} // namespace

const char* bayerPatternName(BayerPattern p) {
    switch (p) {
        case BayerPattern::RGGB: return "RGGB";
        case BayerPattern::BGGR: return "BGGR";
        case BayerPattern::GRBG: return "GRBG";
        case BayerPattern::GBRG: return "GBRG";
        default:                 return "none";
    }
}

BayerPattern bayerPatternFromString(const char* s) {
    QString t = QString::fromLatin1(s).trimmed().toUpper();
    t.remove(QLatin1Char('\''));
    t = t.trimmed();
    if (t == QLatin1String("RGGB")) return BayerPattern::RGGB;
    if (t == QLatin1String("BGGR")) return BayerPattern::BGGR;
    if (t == QLatin1String("GRBG")) return BayerPattern::GRBG;
    if (t == QLatin1String("GBRG")) return BayerPattern::GBRG;
    return BayerPattern::None;
}

BayerPattern bayerPatternFromHeader(const ImageHeader& h) {
    const QString pat = h.valueOf(QStringLiteral("BAYERPAT"));
    if (pat.isEmpty()) return BayerPattern::None;
    BayerPattern p = bayerPatternFromString(pat.toLatin1().constData());
    if (p == BayerPattern::None) return p;
    // Odd sensor-origin offsets shift which colour lands at (0,0).
    const int xo = h.valueOf(QStringLiteral("XBAYROFF")).toInt() & 1;
    const int yo = h.valueOf(QStringLiteral("YBAYROFF")).toInt() & 1;
    if (xo || yo) {
        PatternMap base = mapOf(p), shifted;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                shifted.m[y][x] = base.m[(y + yo) & 1][(x + xo) & 1];
        p = fromMap(shifted);
    }
    return p;
}

// ---- algorithms -------------------------------------------------------------

namespace {

// Mirror out-of-range coordinates back into the frame (border handling).
inline int mirror(int v, int n) {
    if (v < 0) return -v;
    if (v >= n) return 2 * n - 2 - v;
    return v;
}

ImageData superpixel(const ImageData& cfa, const PatternMap& pm) {
    const int W = cfa.width(), H = cfa.height();
    const int w = W / 2, h = H / 2;
    ImageData out(w, h, 3, SampleFormat::Float32, ColorSpace::RGB);
    const float* s = cfa.plane<float>(0);
    float* R = out.plane<float>(0);
    float* G = out.plane<float>(1);
    float* B = out.plane<float>(2);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum[3] = {0, 0, 0};
            int   cnt[3] = {0, 0, 0};
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int c = pm.m[dy][dx];
                    sum[c] += s[std::size_t(2 * y + dy) * W + (2 * x + dx)];
                    ++cnt[c];
                }
            const std::size_t o = std::size_t(y) * w + x;
            R[o] = cnt[0] ? sum[0] / cnt[0] : 0.0f;
            G[o] = cnt[1] ? sum[1] / cnt[1] : 0.0f;
            B[o] = cnt[2] ? sum[2] / cnt[2] : 0.0f;
        }
    }
    return out;
}

ImageData bilinear(const ImageData& cfa, const PatternMap& pm) {
    const int W = cfa.width(), H = cfa.height();
    ImageData out(W, H, 3, SampleFormat::Float32, ColorSpace::RGB);
    const float* s = cfa.plane<float>(0);
    float* dst[3] = { out.plane<float>(0), out.plane<float>(1), out.plane<float>(2) };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float sum[3] = {0, 0, 0};
            int   cnt[3] = {0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int yy = mirror(y + dy, H), xx = mirror(x + dx, W);
                    const int c = pm.m[yy & 1][xx & 1];
                    sum[c] += s[std::size_t(yy) * W + xx];
                    ++cnt[c];
                }
            const std::size_t o = std::size_t(y) * W + x;
            const int self = pm.m[y & 1][x & 1];
            for (int c = 0; c < 3; ++c)
                dst[c][o] = (c == self) ? s[o] : (cnt[c] ? sum[c] / cnt[c] : 0.0f);
        }
    }
    return out;
}

// RCD-family directional demosaic:
//   1. green at R/B sites by gradient-corrected vertical/horizontal candidates
//      (Hamilton–Adams), blended by an inverse-variance direction weight;
//   2. chroma (R−G, B−G) interpolated across the lattice and added back to G.
// Difference (not ratio) chroma correction: astronomical backgrounds sit near
// zero, where ratios amplify noise. Validated against Siril's RCD output.
ImageData rcd(const ImageData& cfa, const PatternMap& pm) {
    const int W = cfa.width(), H = cfa.height();
    const float* s = cfa.plane<float>(0);
    ImageData out(W, H, 3, SampleFormat::Float32, ColorSpace::RGB);
    float* R = out.plane<float>(0);
    float* G = out.plane<float>(1);
    float* B = out.plane<float>(2);
    const float eps = 1e-5f;
    auto S = [&](int x, int y) { return s[std::size_t(mirror(y, H)) * W + mirror(x, W)]; };

    // Pass 1: full green plane.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t o = std::size_t(y) * W + x;
            if (pm.m[y & 1][x & 1] == 1) { G[o] = s[o]; continue; }
            const float c = s[o];
            const float gN = S(x, y - 1), gS = S(x, y + 1),
                        gW = S(x - 1, y), gE = S(x + 1, y);
            const float cN2 = S(x, y - 2), cS2 = S(x, y + 2),
                        cW2 = S(x - 2, y), cE2 = S(x + 2, y);
            const float gV = 0.5f * (gN + gS) + 0.25f * (2.0f * c - cN2 - cS2);
            const float gH = 0.5f * (gW + gE) + 0.25f * (2.0f * c - cW2 - cE2);
            const float dV = std::fabs(gN - gS) + std::fabs(2.0f * c - cN2 - cS2);
            const float dH = std::fabs(gW - gE) + std::fabs(2.0f * c - cW2 - cE2);
            const float wV = 1.0f / (eps + dV * dV), wH = 1.0f / (eps + dH * dH);
            G[o] = (wV * gV + wH * gH) / (wV + wH);
        }

    // Pass 2: chroma via colour-difference interpolation on the green plane.
    auto Gm = [&](int x, int y) { return G[std::size_t(mirror(y, H)) * W + mirror(x, W)]; };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t o = std::size_t(y) * W + x;
            const int self = pm.m[y & 1][x & 1];
            for (int c = 0; c <= 2; c += 2) {          // 0=R, 2=B
                float* dst = (c == 0) ? R : B;
                if (self == c) { dst[o] = s[o]; continue; }
                float sum = 0; int cnt = 0;
                if (self == 1) {
                    // G site: the missing colour lives on one axis of the
                    // pattern; average the (colour − green) difference there.
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            if ((dx == 0) == (dy == 0)) continue;   // 4-neighbours
                            const int yy = mirror(y + dy, H), xx = mirror(x + dx, W);
                            if (pm.m[yy & 1][xx & 1] != c) continue;
                            sum += S(xx, yy) - Gm(xx, yy);
                            ++cnt;
                        }
                } else {
                    // Opposite colour site: the four diagonals carry it.
                    for (int dy = -1; dy <= 1; dy += 2)
                        for (int dx = -1; dx <= 1; dx += 2) {
                            const int yy = mirror(y + dy, H), xx = mirror(x + dx, W);
                            if (pm.m[yy & 1][xx & 1] != c) continue;
                            sum += S(xx, yy) - Gm(xx, yy);
                            ++cnt;
                        }
                }
                dst[o] = G[o] + (cnt ? sum / cnt : 0.0f);
            }
        }
    return out;
}

} // namespace

ImageData debayer(const ImageData& cfa, BayerPattern p, DebayerMethod m) {
    if (!cfa.isValid() || cfa.channels() != 1 ||
        cfa.format() != SampleFormat::Float32 || p == BayerPattern::None)
        return ImageData();
    const PatternMap pm = mapOf(p);
    switch (m) {
        case DebayerMethod::Superpixel: return superpixel(cfa, pm);
        case DebayerMethod::Bilinear:   return bilinear(cfa, pm);
        case DebayerMethod::RCD:        return rcd(cfa, pm);
    }
    return ImageData();
}

CfaSniff sniffCfaMosaic(const ImageData& img) {
    CfaSniff s;
    if (!img.isValid() || img.channels() != 1 ||
        img.format() != SampleFormat::Float32) return s;
    const int w = img.width(), h = img.height();
    if (w < 16 || h < 16) return s;
    const float* p = img.plane<float>(0);

    // Cross-colour vs same-colour neighbour differences. On a mosaic the
    // 1-pixel step jumps between filters (large), the 2-pixel step stays on
    // one filter (small ≈ scene gradient); on a real mono image the 2-pixel
    // step is the LARGER of the two. Row-subsample big frames: statistics,
    // not coverage.
    const int ystep = std::max(1, h / 512);
    double d1h = 0, d2h = 0, d1v = 0, d2v = 0;
    std::int64_t n = 0;
    for (int y = 1; y + 2 < h; y += ystep) {
        const float* r0 = p + std::size_t(y) * w;
        const float* r1 = r0 + w;
        const float* r2 = r1 + w;
        for (int x = 0; x + 2 < w; ++x) {
            d1h += std::abs(double(r0[x]) - r0[x + 1]);
            d2h += std::abs(double(r0[x]) - r0[x + 2]);
            d1v += std::abs(double(r0[x]) - r1[x]);
            d2v += std::abs(double(r0[x]) - r2[x]);
            ++n;
        }
    }
    if (n == 0) return s;
    // 1.6: white noise gives ratio 1, smooth scenes < 1; a mosaic with any
    // colour separation lands well above (the eclipse frame scores ≈ 3).
    // Strict > keeps a perfectly flat colour scene (d2 = 0, d1 > 0) likely
    // while rejecting a perfectly flat mono one (d1 = d2 = 0).
    if (!(d1h > 1.6 * d2h) || !(d1v > 1.6 * d2v)) return s;

    // Which diagonal holds the greens? Same-filter sites agree in their
    // bright-pixel means; the R/B pair differs by the scene's colour.
    double m00 = 0, m01 = 0, m10 = 0, m11 = 0, mean = 0;
    std::int64_t c00 = 0, c01 = 0, c10 = 0, c11 = 0;
    for (int y = 0; y < h; y += ystep) {
        const float* r = p + std::size_t(y) * w;
        for (int x = 0; x < w; ++x) mean += r[x];
    }
    mean /= double(std::max<std::int64_t>(1, (std::int64_t(h / ystep) + 1) * w));
    for (int y = 0; y + 1 < h; y += 2 * ystep) {
        const float* r0 = p + std::size_t(y) * w;
        const float* r1 = r0 + w;
        for (int x = 0; x + 1 < w; x += 2) {
            // Use only above-mean 2×2 cells: background noise dilutes the
            // per-filter separation.
            if (r0[x] < mean && r0[x+1] < mean && r1[x] < mean && r1[x+1] < mean)
                continue;
            m00 += r0[x];     ++c00;
            m01 += r0[x + 1]; ++c01;
            m10 += r1[x];     ++c10;
            m11 += r1[x + 1]; ++c11;
        }
    }
    if (!c00 || !c01 || !c10 || !c11) return s;
    m00 /= double(c00); m01 /= double(c01); m10 /= double(c10); m11 /= double(c11);
    s.likely = true;
    if (std::abs(m01 - m10) <= std::abs(m00 - m11)) {
        s.candidateA = BayerPattern::RGGB;   // greens on the anti-diagonal
        s.candidateB = BayerPattern::BGGR;
    } else {
        s.candidateA = BayerPattern::GRBG;   // greens on the main diagonal
        s.candidateB = BayerPattern::GBRG;
    }
    return s;
}

} // namespace astro
