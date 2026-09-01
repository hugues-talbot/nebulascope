#include "core/PsfMeasure.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace astro {

namespace {

constexpr int    kBox      = 31;    // background box (separable running mean)
constexpr int    kPeakWin  = 5;     // local-maximum half-window (11x11)
constexpr double kNSigma   = 8.0;   // detection threshold over MAD noise
constexpr int    kMargin   = 20;    // frame border to skip
constexpr int    kMaxCand  = 3000;  // brightest candidates kept
constexpr double kSepPx    = 20.0;  // isolation radius
constexpr int    kCutHalf  = 13;    // cutout half-size (27x27)
constexpr double kResidGate = 0.05; // rms(model-data)/amplitude quality gate

// Separable running-mean box filter (edge-clamped), NaN treated as 0.
void boxFilter(const float* in, int w, int h, int box, std::vector<float>& out) {
    const int half = box / 2;
    std::vector<float> tmp(std::size_t(w) * h);
    out.assign(std::size_t(w) * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        const float* row = in + std::size_t(y) * w;
        double acc = 0.0;
        auto at = [&](int x) {
            const float v = row[std::min(std::max(x, 0), w - 1)];
            return std::isfinite(v) ? double(v) : 0.0;
        };
        for (int x = -half; x <= half; ++x) acc += at(x);
        float* trow = tmp.data() + std::size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            trow[x] = float(acc / box);
            acc += at(x + half + 1) - at(x - half);
        }
    }
    for (int x = 0; x < w; ++x) {
        double acc = 0.0;
        auto at = [&](int y) { return double(tmp[std::size_t(std::min(std::max(y, 0), h - 1)) * w + x]); };
        for (int y = -half; y <= half; ++y) acc += at(y);
        for (int y = 0; y < h; ++y) {
            out[std::size_t(y) * w + x] = float(acc / box);
            acc += at(y + half + 1) - at(y - half);
        }
    }
}

double medianOf(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const std::size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + m, v.end());
    return v[m];
}

// Axial circular median of position angles (period 180°): median of the
// doubled-angle components, halved back.
double axialMedianDeg(const std::vector<PsfStar>& stars,
                      std::size_t lo, std::size_t hi,
                      const std::vector<std::size_t>& idx) {
    std::vector<double> ss, cc;
    ss.reserve(hi - lo); cc.reserve(hi - lo);
    for (std::size_t i = lo; i < hi; ++i) {
        const double a2 = 2.0 * stars[idx[i]].paDeg * M_PI / 180.0;
        ss.push_back(std::sin(a2));
        cc.push_back(std::cos(a2));
    }
    return 0.5 * std::atan2(medianOf(ss), medianOf(cc)) * 180.0 / M_PI;
}

struct MoffatParams { double x0, y0, A, bg, sx, sy, th, beta; };

double moffatAt(const MoffatParams& p, double x, double y) {
    const double ct = std::cos(p.th), st = std::sin(p.th);
    const double dx = x - p.x0, dy = y - p.y0;
    const double u = std::pow((dx * ct + dy * st) / p.sx, 2.0)
                   + std::pow((-dx * st + dy * ct) / p.sy, 2.0);
    return p.bg + p.A * std::pow(1.0 + u, -p.beta);
}

void clampParams(MoffatParams& p, int half, double A0, double bg0) {
    const double c = half;
    p.x0 = std::min(c + 3.0, std::max(c - 3.0, p.x0));
    p.y0 = std::min(c + 3.0, std::max(c - 3.0, p.y0));
    p.A  = std::min(3.0 * A0, std::max(0.1 * A0, p.A));
    const double bslack = 5.0 * std::fabs(bg0) + 1e-3;
    p.bg = std::min(bg0 + bslack, std::max(bg0 - bslack, p.bg));
    p.sx = std::min(12.0, std::max(0.4, p.sx));
    p.sy = std::min(12.0, std::max(0.4, p.sy));
    p.beta = std::min(8.0, std::max(1.2, p.beta));
}

} // namespace

bool fitMoffatCutout(const float* cut, int side, double satLevel, PsfStar& out) {
    const int half = side / 2;
    const int n = side * side;
    double bg0;
    {
        std::vector<double> v(cut, cut + n);
        bg0 = medianOf(v);
    }
    double peak = -1e30;
    for (int i = 0; i < n; ++i) peak = std::max(peak, double(cut[i]));
    const double A0 = peak - bg0;
    if (A0 <= 0 || peak >= satLevel) return false;

    // Asymmetric initial widths: a circular guess zeroes the rotation
    // gradient (rotating a circle changes nothing) and the theta column of
    // the Jacobian with it.
    MoffatParams p { double(half), double(half), A0, bg0, 2.3, 1.8, 0.0, 2.5 };
    // Levenberg–Marquardt with a forward-difference Jacobian: 8 parameters,
    // side^2 residuals — small enough that simplicity beats cleverness.
    double lambda = 1e-3;
    auto residuals = [&](const MoffatParams& q, std::vector<double>& r) {
        r.resize(n);
        int k = 0;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x, ++k)
                r[k] = moffatAt(q, x, y) - double(cut[k]);
    };
    std::vector<double> r0, r1;
    residuals(p, r0);
    auto sse = [](const std::vector<double>& r) {
        double s = 0; for (double v : r) s += v * v; return s;
    };
    double e0 = sse(r0);
    double* fields[8] = { &p.x0, &p.y0, &p.A, &p.bg, &p.sx, &p.sy, &p.th, &p.beta };
    const double steps[8] = { 1e-3, 1e-3, std::max(1e-6, 1e-4 * A0),
                              std::max(1e-8, 1e-4 * std::fabs(bg0) + 1e-8),
                              1e-3, 1e-3, 1e-3, 1e-3 };
    std::vector<double> J(std::size_t(n) * 8);
    for (int iter = 0; iter < 40; ++iter) {
        for (int f = 0; f < 8; ++f) {
            MoffatParams q = p;
            double* qf[8] = { &q.x0, &q.y0, &q.A, &q.bg, &q.sx, &q.sy, &q.th, &q.beta };
            *qf[f] += steps[f];
            residuals(q, r1);
            for (int k = 0; k < n; ++k)
                J[std::size_t(k) * 8 + f] = (r1[k] - r0[k]) / steps[f];
        }
        // Normal equations JtJ d = -Jt r with LM damping on the diagonal.
        double JtJ[64] = { 0 }, Jtr[8] = { 0 };
        for (int k = 0; k < n; ++k) {
            const double* Jk = &J[std::size_t(k) * 8];
            for (int a = 0; a < 8; ++a) {
                Jtr[a] += Jk[a] * r0[k];
                for (int b = a; b < 8; ++b) JtJ[a * 8 + b] += Jk[a] * Jk[b];
            }
        }
        for (int a = 0; a < 8; ++a)
            for (int b = 0; b < a; ++b) JtJ[a * 8 + b] = JtJ[b * 8 + a];
        double dmax = 0.0;
        for (int a = 0; a < 8; ++a) dmax = std::max(dmax, JtJ[a * 8 + a]);
        // Levenberg floor: a degenerate direction (e.g. theta when the fit
        // passes through circularity) must damp, not sink the whole solve.
        for (int a = 0; a < 8; ++a)
            JtJ[a * 8 + a] = JtJ[a * 8 + a] * (1.0 + lambda) + 1e-9 * dmax + 1e-30;
        // Solve by Gaussian elimination with partial pivoting.
        double M[64]; std::memcpy(M, JtJ, sizeof M);
        double d[8]; for (int a = 0; a < 8; ++a) d[a] = -Jtr[a];
        bool ok = true;
        for (int col = 0; col < 8 && ok; ++col) {
            int piv = col;
            for (int rw = col + 1; rw < 8; ++rw)
                if (std::fabs(M[rw * 8 + col]) > std::fabs(M[piv * 8 + col])) piv = rw;
            if (std::fabs(M[piv * 8 + col]) < 1e-30) { ok = false; break; }
            if (piv != col) {
                for (int cc = 0; cc < 8; ++cc) std::swap(M[col * 8 + cc], M[piv * 8 + cc]);
                std::swap(d[col], d[piv]);
            }
            for (int rw = col + 1; rw < 8; ++rw) {
                const double f = M[rw * 8 + col] / M[col * 8 + col];
                for (int cc = col; cc < 8; ++cc) M[rw * 8 + cc] -= f * M[col * 8 + cc];
                d[rw] -= f * d[col];
            }
        }
        if (!ok) { lambda *= 10.0; if (lambda > 1e8) break; continue; }
        for (int a = 7; a >= 0; --a) {
            for (int b = a + 1; b < 8; ++b) d[a] -= M[a * 8 + b] * d[b];
            d[a] /= M[a * 8 + a];
        }
        MoffatParams q = p;
        double* qf[8] = { &q.x0, &q.y0, &q.A, &q.bg, &q.sx, &q.sy, &q.th, &q.beta };
        for (int a = 0; a < 8; ++a) *qf[a] += d[a];
        clampParams(q, half, A0, bg0);
        residuals(q, r1);
        const double e1 = sse(r1);
        if (e1 < e0) {
            p = q; r0.swap(r1);
            const double rel = (e0 - e1) / std::max(1e-30, e0);
            e0 = e1;
            lambda = std::max(1e-7, lambda * 0.5);
            if (rel < 1e-8) break;
        } else {
            lambda *= 4.0;
            if (lambda > 1e6) break;
        }
    }
    const double rms = std::sqrt(e0 / n);
    if (rms > kResidGate * p.A) return false;

    const double kfac = 2.0 * std::sqrt(std::pow(2.0, 1.0 / p.beta) - 1.0);
    double f1 = kfac * p.sx, f2 = kfac * p.sy;
    double pa = p.th;
    if (f2 > f1) { std::swap(f1, f2); pa += M_PI / 2.0; }
    pa = std::fmod(pa * 180.0 / M_PI + 90.0, 180.0);
    if (pa < 0) pa += 180.0;
    pa -= 90.0;                                       // (-90, 90]
    out.fwhmMaj = f1;
    out.fwhmMin = f2;
    out.paDeg = pa;
    out.ecc = std::sqrt(std::max(0.0, 1.0 - (f2 / f1) * (f2 / f1)));
    out.beta = p.beta;
    out.amp = p.A;
    out.x = p.x0 - half;                              // caller re-bases to image
    out.y = p.y0 - half;
    return true;
}

PsfChannelReport measurePsf(const ImageData& img, int channel) {
    PsfChannelReport rep;
    if (!img.isValid() || channel < 0 || channel >= img.channels()) return rep;
    const int w = img.width(), h = img.height();
    const float* p = img.plane<float>(channel);
    const std::size_t npx = std::size_t(w) * h;

    // High-pass for detection: data minus a broad box background.
    std::vector<float> bgf;
    boxFilter(p, w, h, kBox, bgf);
    std::vector<float> hp(npx);
    for (std::size_t i = 0; i < npx; ++i) {
        const float v = std::isfinite(p[i]) ? p[i] : 0.0f;
        hp[i] = v - bgf[i];
    }
    // Robust noise: MAD over a subsample.
    double sd;
    {
        const std::size_t stride = std::max<std::size_t>(1, npx / 2000000);
        std::vector<double> s;
        s.reserve(npx / stride + 1);
        for (std::size_t i = 0; i < npx; i += stride) s.push_back(hp[i]);
        const double med = medianOf(s);
        for (double& v : s) v = std::fabs(v - med);
        sd = 1.4826 * medianOf(s);
    }
    const float thr = float(kNSigma * std::max(1e-12, sd));

    // Saturation guard: near the channel's brightest values the profile is
    // nonlinear — those stars are rejected in the fit.
    double satLevel;
    {
        const std::size_t stride = std::max<std::size_t>(1, npx / 2000000);
        std::vector<double> s;
        for (std::size_t i = 0; i < npx; i += stride)
            if (std::isfinite(p[i])) s.push_back(p[i]);
        std::sort(s.begin(), s.end());
        satLevel = s.empty() ? 1e30 : s[std::size_t(s.size() * 0.99999)];
    }

    // Local maxima above threshold, away from borders.
    struct Cand { int x, y; float amp; };
    std::vector<Cand> cands;
    for (int y = kMargin; y < h - kMargin; ++y) {
        const float* row = hp.data() + std::size_t(y) * w;
        for (int x = kMargin; x < w - kMargin; ++x) {
            const float v = row[x];
            if (v <= thr) continue;
            bool isMax = true;
            for (int dy = -kPeakWin; dy <= kPeakWin && isMax; ++dy) {
                const float* r2 = hp.data() + std::size_t(y + dy) * w;
                for (int dx = -kPeakWin; dx <= kPeakWin; ++dx) {
                    if (!dx && !dy) continue;
                    if (r2[x + dx] > v) { isMax = false; break; }
                }
            }
            if (isMax) cands.push_back({ x, y, v });
        }
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.amp > b.amp; });
    if (int(cands.size()) > kMaxCand) cands.resize(kMaxCand);

    // Isolation: reject any peak with a comparable neighbour within kSepPx.
    std::vector<char> keep(cands.size(), 1);
    for (std::size_t i = 0; i < cands.size(); ++i) {
        for (std::size_t j = 0; j < cands.size(); ++j) {
            if (i == j || cands[j].amp < 0.2f * cands[i].amp) continue;
            const double dx = cands[i].x - cands[j].x, dy = cands[i].y - cands[j].y;
            if (dx * dx + dy * dy < kSepPx * kSepPx) { keep[i] = 0; break; }
        }
    }
    rep.nDetected = 0;
    const int side = 2 * kCutHalf + 1;
    std::vector<float> cut(std::size_t(side) * side);
    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (!keep[i]) continue;
        const int cx = cands[i].x, cy = cands[i].y;
        if (cx - kCutHalf < 0 || cy - kCutHalf < 0 ||
            cx + kCutHalf >= w || cy + kCutHalf >= h) continue;
        bool finite = true;
        for (int yy = 0; yy < side && finite; ++yy) {
            const float* src = p + std::size_t(cy - kCutHalf + yy) * w + (cx - kCutHalf);
            for (int xx = 0; xx < side; ++xx) {
                const float v = src[xx];
                if (!std::isfinite(v)) { finite = false; break; }
                cut[std::size_t(yy) * side + xx] = v;
            }
        }
        if (!finite) continue;
        ++rep.nDetected;
        PsfStar st;
        if (!fitMoffatCutout(cut.data(), side, satLevel, st)) continue;
        st.x += cx;
        st.y += cy;
        rep.stars.push_back(st);
    }
    rep.nFitted = int(rep.stars.size());
    if (rep.nFitted < 5) return rep;
    std::sort(rep.stars.begin(), rep.stars.end(),
              [](const PsfStar& a, const PsfStar& b) { return a.amp > b.amp; });

    auto medField = [&](auto get, std::size_t lo, std::size_t hi,
                        const std::vector<std::size_t>& idx) {
        std::vector<double> v;
        v.reserve(hi - lo);
        for (std::size_t i = lo; i < hi; ++i) v.push_back(get(rep.stars[idx[i]]));
        return medianOf(v);
    };
    std::vector<std::size_t> all(rep.stars.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
    rep.fwhmMaj = medField([](const PsfStar& s) { return s.fwhmMaj; }, 0, all.size(), all);
    rep.fwhmMin = medField([](const PsfStar& s) { return s.fwhmMin; }, 0, all.size(), all);
    rep.fwhmGeo = medField([](const PsfStar& s) { return std::sqrt(s.fwhmMaj * s.fwhmMin); }, 0, all.size(), all);
    rep.ecc     = medField([](const PsfStar& s) { return s.ecc; }, 0, all.size(), all);
    rep.beta    = medField([](const PsfStar& s) { return s.beta; }, 0, all.size(), all);
    rep.paDeg   = axialMedianDeg(rep.stars, 0, all.size(), all);

    for (int zr = 0; zr < 3; ++zr)
        for (int zc = 0; zc < 4; ++zc) {
            std::vector<std::size_t> idx;
            for (std::size_t i = 0; i < rep.stars.size(); ++i) {
                const PsfStar& s = rep.stars[i];
                if (int(3.0 * s.y / h) == zr && int(4.0 * s.x / w) == zc)
                    idx.push_back(i);
            }
            PsfZone& z = rep.zone[zr][zc];
            z.nStars = int(idx.size());
            if (z.nStars >= 5) {
                z.fwhmGeo = medField([](const PsfStar& s) { return std::sqrt(s.fwhmMaj * s.fwhmMin); }, 0, idx.size(), idx);
                z.ecc     = medField([](const PsfStar& s) { return s.ecc; }, 0, idx.size(), idx);
                z.paDeg   = axialMedianDeg(rep.stars, 0, idx.size(), idx);
            }
        }
    return rep;
}

} // namespace astro
