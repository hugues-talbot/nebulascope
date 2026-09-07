#include "core/StarRemove.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace astro {

namespace {

constexpr int kBox     = 31;    // detection background box
constexpr int kPeakWin = 3;     // local-maximum half-window (7x7)

// Separable running-mean box filter (edge-clamped), NaN treated as 0.
void boxFilter(const float* in, int w, int h, int box, std::vector<float>& out) {
    const int half = box / 2;
    std::vector<float> tmp(std::size_t(w) * h);
    out.assign(std::size_t(w) * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        const float* row = in + std::size_t(y) * w;
        auto at = [&](int x) {
            const float v = row[std::min(std::max(x, 0), w - 1)];
            return std::isfinite(v) ? double(v) : 0.0;
        };
        double acc = 0.0;
        for (int x = -half; x <= half; ++x) acc += at(x);
        float* trow = tmp.data() + std::size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            trow[x] = float(acc / box);
            acc += at(x + half + 1) - at(x - half);
        }
    }
    for (int x = 0; x < w; ++x) {
        auto at = [&](int y) { return double(tmp[std::size_t(std::min(std::max(y, 0), h - 1)) * w + x]); };
        double acc = 0.0;
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

// The Moffat shape in pixels: Moffat alphas along the axes, orientation, beta.
struct Shape { double sx, sy, ct, st, beta, fwhm; };

Shape shapeFrom(const PsfChannelReport& rep) {
    double fmaj = rep.fwhmMaj, fmin = rep.fwhmMin, pa = rep.paDeg, beta = rep.beta;
    if (rep.nFitted < 5 || !(fmaj > 0.3) || !(fmin > 0.3) || !(beta > 1.0)) {
        fmaj = fmin = 3.0; pa = 0.0; beta = 2.5;          // circular default
    }
    beta = std::min(8.0, std::max(1.2, beta));
    const double conv = 2.0 * std::sqrt(std::pow(2.0, 1.0 / beta) - 1.0);
    const double th = pa * M_PI / 180.0;
    return { fmaj / conv, fmin / conv, std::cos(th), std::sin(th), beta,
             std::sqrt(fmaj * fmin) };
}

inline double profileAt(const Shape& s, double dx, double dy, double scale, double beta) {
    const double a = (dx * s.ct + dy * s.st) / (s.sx * scale);
    const double b = (-dx * s.st + dy * s.ct) / (s.sy * scale);
    return std::pow(1.0 + a * a + b * b, -beta);
}
inline double profileAt(const Shape& s, double dx, double dy, double scale) {
    return profileAt(s, dx, dy, scale, s.beta);
}

// Radius along the wider axis where the profile falls to `frac` of its peak.
double radiusAtFraction(const Shape& s, double scale, double frac) {
    if (frac >= 1.0) return 0.0;
    if (frac <= 0.0) return 1e9;
    const double u = std::pow(frac, -1.0 / s.beta) - 1.0;
    return std::sqrt(std::max(0.0, u)) * std::max(s.sx, s.sy) * scale;
}

// Solve the n x n system M d = b by Gaussian elimination with pivoting.
bool solveSmall(int n, double* M, double* b, double* d) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r)
            if (std::fabs(M[r * n + col]) > std::fabs(M[piv * n + col])) piv = r;
        if (std::fabs(M[piv * n + col]) < 1e-300) return false;
        if (piv != col) {
            for (int c = 0; c < n; ++c) std::swap(M[col * n + c], M[piv * n + c]);
            std::swap(b[col], b[piv]);
        }
        for (int r = col + 1; r < n; ++r) {
            const double f = M[r * n + col] / M[col * n + col];
            for (int c = col; c < n; ++c) M[r * n + c] -= f * M[col * n + c];
            b[r] -= f * b[col];
        }
    }
    for (int a = n - 1; a >= 0; --a) {
        double s = b[a];
        for (int c = a + 1; c < n; ++c) s -= M[a * n + c] * d[c];
        d[a] = s / M[a * n + a];
    }
    return true;
}

struct StarFit {
    double x0 = 0, y0 = 0, A = 0, bg = 0, gx = 0, gy = 0, scale = 1, beta = 2.5;
    double rmsCore = 0;      // fit residual rms within 1.5 FWHM
    bool clipped = false;
    double clipRadius = 0;   // farthest clipped pixel from the centre
    bool ok = false;
};

// Fit position, amplitude, local background with its gradient, and
// (optionally) one width scale of the fixed shape on the unclipped finite
// pixels of a disc. Levenberg–Marquardt with a forward-difference Jacobian —
// a handful of parameters, a few hundred residuals.
struct Neighbour { double x, y, r; };   // an unsubtracted star to keep out of a fit

StarFit fitStar(const std::vector<float>& img, const std::vector<unsigned char>& excl,
                const std::vector<Neighbour>& nb,
                int w, int h, const Shape& sh,
                double cx, double cy, double A0, double bg0, double sigma,
                double R, double satLevel, bool fitScale, bool fitBeta) {
    StarFit f;
    f.beta = sh.beta;
    std::vector<double> xs, ys, vals;
    const int x0 = std::max(0, int(std::floor(cx - R))), x1 = std::min(w - 1, int(std::ceil(cx + R)));
    const int y0 = std::max(0, int(std::floor(cy - R))), y1 = std::min(h - 1, int(std::ceil(cy + R)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > R * R) continue;
            const float v = img[std::size_t(y) * w + x];
            if (!std::isfinite(v) || excl[std::size_t(y) * w + x]) continue;
            bool near = false;
            for (const Neighbour& q : nb)
                if ((x - q.x) * (x - q.x) + (y - q.y) * (y - q.y) < q.r * q.r) { near = true; break; }
            if (near) continue;
            if (v >= satLevel) {
                f.clipped = true;
                f.clipRadius = std::max(f.clipRadius, std::hypot(dx, dy));
                continue;
            }
            xs.push_back(x - cx); ys.push_back(y - cy); vals.push_back(v);
        }
    const int n = int(vals.size());
    if (n < 10) return f;
    // Parameters: x, y, A, bg, gx, gy, [scale], [beta], [qxx, qyy, qxy]. The
    // width scale only with an unclipped core (degenerate with A on
    // power-law wings), the exponent only when the wings are bright enough
    // to set it, the quadratic background whenever the disc is large enough
    // for the nebula's curvature to matter (a quadratic cannot mimic a core).
    constexpr int NP = 11;
    int act[NP] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    int np = 6;
    if (fitScale) act[np++] = 6;
    if (fitBeta) act[np++] = 7;
    const bool fitQuad = n > 60;
    if (fitQuad) { act[np++] = 8; act[np++] = 9; act[np++] = 10; }
    double p[NP] = { 0.0, 0.0, std::max(A0, 1e-12), bg0, 0.0, 0.0, 1.0, sh.beta, 0.0, 0.0, 0.0 };
    // The candidate IS the peak: the fit refines it to sub-pixel, it must
    // not wander to a neighbour (a close pair would then lose both stars
    // and get a fill between them). A clipped plateau's candidate can sit
    // anywhere on the plateau, hence its wider slack.
    const double slack = std::max(1.5, f.clipRadius + 1.0);
    const double bs = 5.0 * std::fabs(bg0) + 20.0 * sigma + 1e-12;
    auto clamp = [&](double* q) {
        q[0] = std::min(slack, std::max(-slack, q[0]));
        q[1] = std::min(slack, std::max(-slack, q[1]));
        q[2] = std::min(1e5 * A0, std::max(0.02 * A0, q[2]));
        q[3] = std::min(bg0 + bs, std::max(bg0 - bs, q[3]));
        q[4] = std::min(bs / R, std::max(-bs / R, q[4]));
        q[5] = std::min(bs / R, std::max(-bs / R, q[5]));
        q[6] = std::min(2.0, std::max(0.5, q[6]));
        q[7] = std::min(6.0, std::max(1.5, q[7]));
        const double qs = bs / (R * R);
        for (int i = 8; i < 11; ++i) q[i] = std::min(qs, std::max(-qs, q[i]));
    };
    std::vector<double> r0(n), r1(n), J(std::size_t(n) * np);
    auto residuals = [&](const double* q, std::vector<double>& r) {
        for (int k = 0; k < n; ++k)
            r[k] = q[3] + q[4] * xs[k] + q[5] * ys[k]
                 + q[8] * xs[k] * xs[k] + q[9] * ys[k] * ys[k] + q[10] * xs[k] * ys[k]
                 + q[2] * profileAt(sh, xs[k] - q[0], ys[k] - q[1], q[6], q[7]) - vals[k];
    };
    auto sse = [&](const std::vector<double>& r) { double s = 0; for (double v : r) s += v * v; return s; };
    residuals(p, r0);
    double e0 = sse(r0), lambda = 1e-3;
    const double gstep = std::max(1e-15, 1e-4 * (std::fabs(bg0) + sigma) / std::max(1.0, R));
    const double steps[NP] = { 1e-3, 1e-3, std::max(1e-12, 1e-4 * A0),
                               std::max(1e-12, 1e-4 * std::fabs(bg0) + 1e-3 * sigma),
                               gstep, gstep, 1e-3, 1e-3,
                               gstep / std::max(1.0, R), gstep / std::max(1.0, R), gstep / std::max(1.0, R) };
    for (int iter = 0; iter < 30; ++iter) {
        for (int f2 = 0; f2 < np; ++f2) {
            double q[NP]; std::memcpy(q, p, sizeof q);
            q[act[f2]] += steps[act[f2]];
            residuals(q, r1);
            for (int k = 0; k < n; ++k) J[std::size_t(k) * np + f2] = (r1[k] - r0[k]) / steps[act[f2]];
        }
        double JtJ[NP * NP] = { 0 }, Jtr[NP] = { 0 };
        for (int k = 0; k < n; ++k) {
            const double* Jk = &J[std::size_t(k) * np];
            for (int a = 0; a < np; ++a) {
                Jtr[a] += Jk[a] * r0[k];
                for (int b = a; b < np; ++b) JtJ[a * np + b] += Jk[a] * Jk[b];
            }
        }
        for (int a = 0; a < np; ++a)
            for (int b = 0; b < a; ++b) JtJ[a * np + b] = JtJ[b * np + a];
        double dmax = 0.0;
        for (int a = 0; a < np; ++a) dmax = std::max(dmax, JtJ[a * np + a]);
        for (int a = 0; a < np; ++a)
            JtJ[a * np + a] = JtJ[a * np + a] * (1.0 + lambda) + 1e-9 * dmax + 1e-300;
        double M[NP * NP], b[NP], d[NP];
        std::memcpy(M, JtJ, sizeof M);
        for (int a = 0; a < np; ++a) b[a] = -Jtr[a];
        if (!solveSmall(np, M, b, d)) { lambda *= 10.0; if (lambda > 1e8) break; continue; }
        double q[NP]; std::memcpy(q, p, sizeof q);
        for (int a = 0; a < np; ++a) q[act[a]] += d[a];
        clamp(q);
        residuals(q, r1);
        const double e1 = sse(r1);
        if (e1 < e0) {
            std::memcpy(p, q, sizeof p);
            r0.swap(r1);
            const double rel = (e0 - e1) / std::max(1e-300, e0);
            e0 = e1;
            lambda = std::max(1e-7, lambda * 0.5);
            if (rel < 1e-7) break;
        } else {
            lambda *= 4.0;
            if (lambda > 1e6) break;
        }
    }
    f.x0 = cx + p[0]; f.y0 = cy + p[1]; f.A = p[2]; f.bg = p[3];
    f.gx = p[4]; f.gy = p[5]; f.scale = p[6]; f.beta = p[7];
    // Residual within 1.5 FWHM of the centre — the shape test.
    double s = 0; int cnt = 0;
    const double rc = 1.5 * sh.fwhm * f.scale;
    for (int k = 0; k < n; ++k) {
        const double dx = xs[k] - p[0], dy = ys[k] - p[1];
        if (dx * dx + dy * dy <= rc * rc) { s += r0[k] * r0[k]; ++cnt; }
    }
    f.rmsCore = cnt > 0 ? std::sqrt(s / cnt) : 0.0;
    f.ok = true;
    return f;
}

// Harmonic fill of the disc r < Rc around (cx, cy) from the ring just
// outside it: the Poisson integral for the disc, exact, with the boundary
// sampled in angular sectors of the ring (sector means — a little
// denoising of the boundary comes free). Pixels missing from the ring
// (frame edge, NaN) borrow their angular neighbours.
void harmonicFill(std::vector<float>& img, std::vector<unsigned char>& excl,
                  int w, int h, double cx, double cy,
                  double Rc, double sigma, bool noise, std::mt19937& rng) {
    const double Rb = Rc + 1.5;                // sampling radius (ring [Rc, Rc+3])
    const int K = std::max(16, int(std::ceil(2.0 * M_PI * Rb)));
    std::vector<double> f(K, 0.0), cnt(K, 0.0);
    const int x0 = std::max(0, int(std::floor(cx - Rc - 3))), x1 = std::min(w - 1, int(std::ceil(cx + Rc + 3)));
    const int y0 = std::max(0, int(std::floor(cy - Rc - 3))), y1 = std::min(h - 1, int(std::ceil(cy + Rc + 3)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x - cx, dy = y - cy, r = std::hypot(dx, dy);
            if (r < Rc || r > Rc + 3.0) continue;
            const float v = img[std::size_t(y) * w + x];
            if (!std::isfinite(v) || excl[std::size_t(y) * w + x]) continue;   // another star's unfilled core
            double th = std::atan2(dy, dx);
            if (th < 0) th += 2.0 * M_PI;
            const int k = std::min(K - 1, int(th / (2.0 * M_PI) * K));
            f[k] += v; cnt[k] += 1.0;
        }
    bool any = false;
    for (int k = 0; k < K; ++k) if (cnt[k] > 0) { f[k] /= cnt[k]; any = true; }
    if (!any) return;
    for (int k = 0; k < K; ++k) {                // fill empty sectors from neighbours
        if (cnt[k] > 0) continue;
        int a = k, b = k;
        while (cnt[a] <= 0) a = (a + K - 1) % K;
        while (cnt[b] <= 0) b = (b + 1) % K;
        f[k] = 0.5 * (f[a] + f[b]);
    }
    std::vector<double> ck(K), sk(K);
    for (int k = 0; k < K; ++k) {
        const double th = (k + 0.5) * 2.0 * M_PI / K;
        ck[k] = std::cos(th); sk[k] = std::sin(th);
    }
    std::normal_distribution<double> gauss(0.0, sigma);
    const double R2 = Rb * Rb;
    const double feather = std::min(2.0, 0.5 * Rc);   // soft seam: blend over the last pixels
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x - cx, dy = y - cy, r2 = dx * dx + dy * dy;
            if (r2 >= Rc * Rc) continue;
            float& v = img[std::size_t(y) * w + x];
            if (!std::isfinite(v)) continue;
            const double r = std::sqrt(r2);
            const double cphi = r > 0 ? dx / r : 1.0, sphi = r > 0 ? dy / r : 0.0;
            double num = 0.0, den = 0.0;
            for (int k = 0; k < K; ++k) {
                const double cosd = ck[k] * cphi + sk[k] * sphi;
                const double P = (R2 - r2) / (R2 - 2.0 * Rb * r * cosd + r2);
                num += f[k] * P; den += P;
            }
            double u = num / den;
            if (noise) u += gauss(rng);
            const double t = r > Rc - feather ? 0.5 + 0.5 * std::cos(M_PI * (Rc - r) / feather) : 0.0;
            v = float((1.0 - t) * u + t * double(v));  // t: 0 inside … 1 at the rim
            excl[std::size_t(y) * w + x] = 0;
        }
}

} // namespace

StarRemoveResult removeStars(const float* plane, int w, int h,
                             const PsfChannelReport& shape,
                             const StarRemoveOptions& opt,
                             std::atomic<int>* done, std::atomic<int>* total) {
    StarRemoveResult res;
    const std::size_t n = std::size_t(w) * h;
    res.starless.assign(plane, plane + n);
    std::vector<float>& img = res.starless;
    if (n == 0) return res;
    const Shape sh = shapeFrom(shape);

    // Detection on the high-passed frame; robust noise from its MAD.
    std::vector<float> bgf;
    boxFilter(plane, w, h, kBox, bgf);
    std::vector<float> hp(n);
    for (std::size_t i = 0; i < n; ++i)
        hp[i] = std::isfinite(plane[i]) ? plane[i] - bgf[i] : 0.0f;
    // Noise from the discrete Laplacian (MAD / sqrt 20): blind to the sky
    // pedestal and to the nebula's gradients, and unaffected by the box
    // background's halo around every star, which inflates a plain MAD of
    // the high-passed frame by an order of magnitude.
    double sigma;
    {
        const std::size_t stride = std::max<std::size_t>(1, n / 2000000);
        std::vector<double> s;
        s.reserve(n / stride + 1);
        for (std::size_t i = std::size_t(w) + 1; i + std::size_t(w) + 1 < n; i += stride) {
            const float c0 = plane[i], l = plane[i - 1], r = plane[i + 1],
                        u = plane[i - std::size_t(w)], d = plane[i + std::size_t(w)];
            if (std::isfinite(c0) && std::isfinite(l) && std::isfinite(r) &&
                std::isfinite(u) && std::isfinite(d))
                s.push_back(std::fabs(4.0 * double(c0) - l - r - u - d));
        }
        sigma = 1.4826 * medianOf(s) / std::sqrt(20.0);
    }
    if (!(sigma > 0)) sigma = 1e-12;
    res.noiseSigma = sigma;
    // Clipped pixels: within 2% of the frame's maximum finite value.
    double vmax = -1e30;
    for (std::size_t i = 0; i < n; ++i) if (std::isfinite(plane[i])) vmax = std::max(vmax, double(plane[i]));
    const double satLevel = vmax > 0 ? 0.98 * vmax : 1e30;

    struct Cand { int x, y; float amp; };
    std::vector<Cand> cands;
    const float thr = float(opt.detectSigma * sigma);
    for (int y = kPeakWin; y < h - kPeakWin; ++y) {
        const float* row = hp.data() + std::size_t(y) * w;
        for (int x = kPeakWin; x < w - kPeakWin; ++x) {
            const float v = row[x];
            if (v <= thr) continue;
            bool isMax = true;                  // >= : a clipped plateau is all maxima
            for (int dy = -kPeakWin; dy <= kPeakWin && isMax; ++dy) {
                const float* r2 = hp.data() + std::size_t(y + dy) * w;
                for (int dx = -kPeakWin; dx <= kPeakWin; ++dx) {
                    if (!dx && !dy) continue;
                    if (r2[x + dx] > v) { isMax = false; break; }
                }
            }
            if (!isMax) continue;
            // Local contrast on the frame itself: the peak must stand above
            // the median of a ring 3-5 px out by the threshold. On smooth
            // nebulosity the box high-pass rides high and its noise peaks
            // would otherwise be fitted and subtracted as faint stars.
            double ring[64]; int nr = 0;
            for (int dy = -5; dy <= 5; ++dy)
                for (int dx = -5; dx <= 5; ++dx) {
                    const int r2 = dx * dx + dy * dy;
                    if (r2 < 9 || r2 > 25 || nr >= 64) continue;
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                    const float pv = plane[std::size_t(yy) * w + xx];
                    if (std::isfinite(pv)) ring[nr++] = pv;
                }
            if (nr < 8) continue;
            std::nth_element(ring, ring + nr / 2, ring + nr);
            const float pc = plane[std::size_t(y) * w + x];
            if (!std::isfinite(pc) || pc - float(ring[nr / 2]) <= thr) continue;
            cands.push_back({ x, y, v });
        }
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.amp > b.amp; });
    res.nCandidates = int(cands.size());
    if (total) total->fetch_add(int(cands.size()));

    std::vector<unsigned char> claimed(n, 0);
    // Cores awaiting their fill are excluded from every later fit and from
    // every ring: a subtracted clipped core is a hole of large negative
    // values until pass 2 fills it.
    std::vector<unsigned char> excluded(n, 0);
    auto claim = [&](double cx, double cy, double r) {
        const int x0 = std::max(0, int(cx - r)), x1 = std::min(w - 1, int(cx + r) + 1);
        const int y0 = std::max(0, int(cy - r)), y1 = std::min(h - 1, int(cy + r) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                    claimed[std::size_t(y) * w + x] = 1;
    };
    std::mt19937 rng(opt.seed);
    const double minR = 3.0 * sh.fwhm;

    // Pass 1 — fit and subtract, brightest first on the running image. A
    // fit keeps out the pixels of every candidate not yet subtracted (a
    // close pair would otherwise fail the shape gate on each other's
    // core, and lose both stars).
    struct Pending { StarFit f; double Rc; };
    std::vector<Pending> pend;
    std::vector<unsigned char> subtracted(cands.size(), 0);
    const double nbRadius = 1.5 * sh.fwhm;
    for (std::size_t ci = 0; ci < cands.size(); ++ci) {
        const Cand& c = cands[ci];
        if (claimed[std::size_t(c.y) * w + c.x]) { if (done) done->fetch_add(1); continue; }
        const double A0 = std::max(double(c.amp), 3.0 * sigma);
        const double bg0 = bgf[std::size_t(c.y) * w + c.x];
        double R = std::min(80.0, std::max(minR, 1.2 * radiusAtFraction(sh, 1.0, sigma / A0)));
        std::vector<Neighbour> nb;
        auto gatherNb = [&](double Rf) {
            nb.clear();
            for (std::size_t cj = 0; cj < cands.size(); ++cj) {
                if (cj == ci || subtracted[cj] || claimed[std::size_t(cands[cj].y) * w + cands[cj].x]) continue;
                const double dx = cands[cj].x - c.x, dy = cands[cj].y - c.y;
                if (dx * dx + dy * dy < (Rf + nbRadius) * (Rf + nbRadius) && std::hypot(dx, dy) > 2.0)
                    nb.push_back({ double(cands[cj].x), double(cands[cj].y), nbRadius });
            }
        };
        gatherNb(R);
        // Width scale only for stars with an unclipped core: on power-law
        // wings amplitude and scale are degenerate (A s^2beta), and the
        // region that breaks the degeneracy is the clipped one.
        const bool clippedCand = plane[std::size_t(c.y) * w + c.x] >= satLevel;
        // The exponent stays at the field's median for every star: measured
        // on thousands of stars it is more robust than any single fit, and
        // a per-star exponent that slides low on a bright star's non-Moffat
        // wings subtracts 100x too much tens of pixels out (seen on the M16
        // crop as dark lobes). The width scale is fitted only with an
        // unclipped core — on power-law wings amplitude and scale are
        // degenerate, so a clipped star keeps the field's shape and gives up
        // only its flux, from the wing level.
        StarFit f = fitStar(img, excluded, nb, w, h, sh, c.x, c.y, A0, bg0, sigma, R, satLevel,
                            A0 > 20.0 * sigma && !clippedCand, false);
        if (f.ok && f.A > 3.0 * A0) {           // wings said brighter (clipped core): widen and refit
            R = std::min(120.0, std::max(R, 1.2 * radiusAtFraction(sh, f.scale, sigma / f.A)));
            gatherNb(R);
            f = fitStar(img, excluded, nb, w, h, sh, f.x0, f.y0, f.A, f.bg, sigma, R, satLevel,
                        !f.clipped, false);
        }
        if (done) done->fetch_add(1);
        if (!f.ok || f.A < 3.0 * sigma) continue;
        // An unclipped candidate whose fitted amplitude far exceeds its
        // observed peak is a fit absorbing background, not a star.
        if (!f.clipped && f.A > 3.0 * A0) continue;
        // Shape gate: a Moffat of the field's shape must explain the core —
        // nebular knots and galaxies fail it (or pin the width scale).
        if (f.rmsCore > std::max(0.15 * f.A, 2.5 * sigma) || f.scale > 1.95) continue;
        Shape ss = sh;                          // this star's shape: its own exponent
        ss.beta = f.beta;

        // Subtract the model out to where it drops under a fifth of the noise.
        const double Rsub = std::min(200.0, std::max(R, radiusAtFraction(ss, f.scale, 0.2 * sigma / f.A)));
        {
            const int x0 = std::max(0, int(std::floor(f.x0 - Rsub))), x1 = std::min(w - 1, int(std::ceil(f.x0 + Rsub)));
            const int y0 = std::max(0, int(std::floor(f.y0 - Rsub))), y1 = std::min(h - 1, int(std::ceil(f.y0 + Rsub)));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const double dx = x - f.x0, dy = y - f.y0;
                    if (dx * dx + dy * dy > Rsub * Rsub) continue;
                    float& v = img[std::size_t(y) * w + x];
                    if (std::isfinite(v)) v = float(v - f.A * profileAt(ss, dx, dy, f.scale));
                }
        }
        // Core: where the model exceeded coreSigma sigmas (a few-percent fit
        // residual would show), and at least the clipped disc plus a margin.
        double Rc = f.A > opt.coreSigma * sigma
                  ? radiusAtFraction(ss, f.scale, opt.coreSigma * sigma / f.A) : 0.0;
        if (f.clipped) Rc = std::max(Rc, f.clipRadius + 2.0);
        Rc = std::min(Rc, 60.0);
        subtracted[ci] = 1;
        // Claim only what can be a duplicate maximum of THIS star (a clipped
        // plateau, the peak's own pixels): a neighbour inside the core disc
        // is still a star to fit and subtract before the disc is filled.
        claim(f.x0, f.y0, std::max(3.0, std::max(f.clipRadius + 1.0, 0.75 * sh.fwhm * f.scale)));
        if (Rc > 0.0) {
            const int x0 = std::max(0, int(f.x0 - Rc)), x1 = std::min(w - 1, int(f.x0 + Rc) + 1);
            const int y0 = std::max(0, int(f.y0 - Rc)), y1 = std::min(h - 1, int(f.y0 + Rc) + 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if ((x - f.x0) * (x - f.x0) + (y - f.y0) * (y - f.y0) < Rc * Rc)
                        excluded[std::size_t(y) * w + x] = 1;
        }
        pend.push_back({ f, Rc });
    }

    // Pass 2 — with every star subtracted: the residual left around each
    // core, measured as the rms against a plane fitted to the ring just
    // outside it (so the nebula's own gradient is not charged to the star),
    // in noise sigmas. Where it exceeds the noise the disc GROWS until the
    // ring is quiet or the cap is reached — the wings of a bright star are
    // rounder than its core, and a Moffat of the field's shape over-
    // subtracts along the major axis out to a few tens of pixels; the fill
    // then covers exactly what the model could not explain, and the report
    // says so. Then the fill.
    // Ring statistics around a core: a plane is fitted to a CONTROL ring
    // further out (beyond the star's reach) and the inner ring is judged
    // against it — its coherent MEAN offset (a star's over- or under-
    // subtraction is coherent around the ring; nebular texture averages
    // out) and its scatter relative to the control ring's scatter (texture
    // sits in both, the star's residual only in the inner one).
    struct RingStat { double rmsRel = 0, meanExcess = 0; bool ok = false; };
    auto ringStat = [&](const StarFit& f, double Ri, double Rm, double Ro) {
        RingStat st;
        std::vector<double> ix, iy, iv, ox, oy, ov;
        const int x0 = std::max(0, int(f.x0 - Ro)), x1 = std::min(w - 1, int(f.x0 + Ro) + 1);
        const int y0 = std::max(0, int(f.y0 - Ro)), y1 = std::min(h - 1, int(f.y0 + Ro) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const double dx = x - f.x0, dy = y - f.y0, r = std::hypot(dx, dy);
                if (r < Ri || r > Ro) continue;
                const float v = img[std::size_t(y) * w + x];
                if (!std::isfinite(v) || excluded[std::size_t(y) * w + x]) continue;
                if (r < Rm) { ix.push_back(dx); iy.push_back(dy); iv.push_back(v); }
                else        { ox.push_back(dx); oy.push_back(dy); ov.push_back(v); }
            }
        if (iv.size() < 12 || ov.size() < 12) return st;
        // Baseline: a quadratic surface fitted on the control ring — the
        // nebula's gradient AND curvature, extrapolated one ring inward.
        // Coordinates scaled by Ro so the normal equations stay tame.
        auto basis = [&](double x, double y, double* row) {
            const double u = x / Ro, v2 = y / Ro;
            row[0] = 1.0; row[1] = u; row[2] = v2; row[3] = u * u; row[4] = v2 * v2; row[5] = u * v2;
        };
        double M[36] = { 0 }, b[6] = { 0 }, d[6] = { 0 };
        for (std::size_t k = 0; k < ov.size(); ++k) {
            double row[6]; basis(ox[k], oy[k], row);
            for (int a2 = 0; a2 < 6; ++a2) {
                b[a2] += row[a2] * ov[k];
                for (int c2 = 0; c2 < 6; ++c2) M[a2 * 6 + c2] += row[a2] * row[c2];
            }
        }
        if (!solveSmall(6, M, b, d)) return st;
        auto base = [&](double x, double y) {
            double row[6]; basis(x, y, row);
            double v2 = 0.0;
            for (int a2 = 0; a2 < 6; ++a2) v2 += d[a2] * row[a2];
            return v2;
        };
        double so = 0.0;
        for (std::size_t k = 0; k < ov.size(); ++k) {
            const double e = ov[k] - base(ox[k], oy[k]);
            so += e * e;
        }
        const double rmsOut = std::sqrt(so / double(ov.size())) / sigma;
        double mi = 0.0;
        for (std::size_t k = 0; k < iv.size(); ++k) mi += iv[k] - base(ix[k], iy[k]);
        mi /= double(iv.size());
        double si = 0.0;
        for (std::size_t k = 0; k < iv.size(); ++k) {
            const double e = iv[k] - base(ix[k], iy[k]) - mi;
            si += e * e;
        }
        const double rmsIn = std::sqrt(si / double(iv.size())) / sigma;
        st.rmsRel = rmsIn / std::max(1.0, rmsOut);
        st.meanExcess = std::fabs(mi) / sigma;
        st.ok = true;
        return st;
    };
    constexpr double kRmsQuiet = 1.5;   // inner scatter within this factor of the control ring's
    constexpr double kMeanQuiet = 1.0;  // coherent offset of the inner ring, in sigmas
    constexpr double kRcMax = 60.0;
    auto residualAt = [&](const StarFit& f, double Rc) {
        const double wRing = std::max(3.0, 2.0 * sh.fwhm * f.scale);
        return ringStat(f, Rc, Rc + wRing, Rc + 2.0 * wRing);
    };
    auto quiet = [&](const RingStat& r) {
        return !r.ok || (r.rmsRel <= kRmsQuiet && r.meanExcess <= kMeanQuiet);
    };
    for (const Pending& pf : pend) {
        const StarFit& f = pf.f;
        double Rc = pf.Rc;
        RingStat rr = residualAt(f, Rc);
        // Growth is bounded by the star's own model level as well: past the
        // radius where the model is under 5 sigmas, whatever is left cannot
        // be the star's (a 100% error would be 5 sigmas).
        Shape ss = sh; ss.beta = f.beta;
        const double Rgrow = std::min(kRcMax, std::max(Rc, radiusAtFraction(ss, f.scale, 5.0 * sigma / f.A)));
        while (!quiet(rr) && Rc < Rgrow) {
            Rc = std::min(Rgrow, Rc + 2.0);
            rr = residualAt(f, Rc);
        }
        // Reported as the larger of the two excesses: 1 = nothing left.
        const double rms = rr.ok ? std::max(rr.rmsRel, rr.meanExcess) : 0.0;
        if (Rc > 0.0) {
            harmonicFill(img, excluded, w, h, f.x0, f.y0, Rc, sigma, opt.fillNoise, rng);
            ++res.nInpainted;
        }
        RemovedStar st;
        st.x = f.x0; st.y = f.y0; st.amp = f.A; st.scale = f.scale; st.beta = f.beta;
        st.coreRadius = Rc; st.residualSigma = rms; st.clipped = f.clipped;
        res.stars.push_back(st);
        ++res.nRemoved;
        if (f.clipped) ++res.nClipped;
        if (rms > kStarRemoveFlagSigma) ++res.nFlagged;
        res.maxResidualSigma = std::max(res.maxResidualSigma, rms);
    }
    std::sort(res.stars.begin(), res.stars.end(),
              [](const RemovedStar& a, const RemovedStar& b) { return a.amp > b.amp; });
    return res;
}

ImageData removeStarsImage(const ImageData& img,
                           const std::vector<PsfChannelReport>& shapes,
                           const StarRemoveOptions& opt,
                           std::vector<StarRemoveResult>* results,
                           std::atomic<int>* done, std::atomic<int>* total) {
    ImageData out = img;
    if (!img.isValid() || img.format() != SampleFormat::Float32) return out;
    const PsfChannelReport none;
    for (int c = 0; c < img.channels(); ++c) {
        const PsfChannelReport& sh = shapes.empty() ? none
            : shapes[std::size_t(std::min(c, int(shapes.size()) - 1))];
        StarRemoveOptions o = opt;
        o.seed = opt.seed + unsigned(c) * 7919u;
        StarRemoveResult r = removeStars(img.plane<float>(c), img.width(), img.height(),
                                         sh, o, done, total);
        std::copy(r.starless.begin(), r.starless.end(), out.plane<float>(c));
        if (results) {
            r.starless.clear(); r.starless.shrink_to_fit();   // the plane lives in `out`
            results->push_back(std::move(r));
        }
    }
    return out;
}

} // namespace astro
