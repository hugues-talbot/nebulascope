# Colour Transport — Theory and the Stretch Fit

NebulaScope's colour transport transfers the colour *distribution* of a
reference image onto the displayed image. It has two application modes with
very different data-integrity properties:

1. **Exact transport** — pixels are rewritten through an empirical optimal
   transport map (a new list entry; the source is never modified).
2. **Stretch fit** — no pixel is written at all: NebulaScope *fits the
   display stretch* so that the source **looks** like the transported
   result. Data loss is zero by construction.

This chapter derives both, and explains precisely why mode 1 can posterize
and amplify noise, and why mode 2 cannot.

## 1. The distribution-transfer problem

Work in display space: each pixel of the source is a colour vector
$u \in [0,1]^3$ drawn from a distribution $\mu_s$; the reference's pixels
follow $\mu_r$. We seek a map $T : [0,1]^3 \to [0,1]^3$ such that when $u$
is distributed as $\mu_s$, $T(u)$ is distributed as $\mu_r$ — written
$T_\# \mu_s = \mu_r$ ("$T$ pushes $\mu_s$ forward onto $\mu_r$"). Among all
such maps, optimal transport selects the one moving colours the least, i.e.
minimising $\mathbb{E}\,\lVert T(u) - u \rVert^2$.

Alignment is irrelevant: only the two colour distributions matter, which is
why the images need not overlap, match in size, or even show the same field.

### 1.1 One dimension is solved exactly

For scalar distributions with cumulative distribution functions $F_s$ and
$F_r$, the optimal map is the classical *monotone rearrangement*

$$T = F_r^{-1} \circ F_s ,$$

i.e. the value at source quantile $q$ maps to the reference value at the
same quantile. This is exactly a histogram specification, and it is the
unique monotone solution.

### 1.2 Sliced transport in three dimensions

In $\mathbb{R}^3$ no closed form exists, but the 1-D solution can be
*sliced* (Pitié et al.'s iterative distribution transfer, the same family
as the sliced Wasserstein methods): repeat, for $k = 1 \dots K$,

1. draw a random rotation $R_k \in SO(3)$;
2. project both point clouds onto the three rotated axes;
3. apply the 1-D monotone rearrangement independently along each axis;
4. rotate back.

Each iteration reduces the discrepancy between the current and reference
distributions; after $K \approx 15$ sweeps the marginals agree along enough
directions that the full 3-D distributions essentially match.

**Implementation notes.** NebulaScope estimates the maps from at most
$2 \times 10^5$ samples per image, restricted to each view's visible region
(off-screen features must not steer the match) and excluding near-saturated
pixels (their colours are unmatchable). Each 1-D map is tabulated at 1024
quantile knots with linear interpolation between them, and the rotation
sequence is deterministically seeded, so results are reproducible. A
*strength* $s \in [0,1]$ blends the result with the original:
$u' = u + s\,(T(u) - u)$. Mono image pairs skip the slicing and use the 1-D
rearrangement directly.

## 2. Why exact transport degrades data

Two artefacts are *inherent* to pushing pixels through an empirical map —
no implementation detail removes them entirely.

### 2.1 Posterization from tied samples

The empirical map is built from finite, quantized data. Where many samples
share one value $v$ (ties are guaranteed when the source was ever integer
data), the empirical CDF jumps: the map sends the whole tie-run to a single
output, and the *next* distinct input value lands a finite step away. The
transported histogram develops gaps and spikes — visible as banding in
smooth gradients. Iterating the slicing compounds the effect.

### 2.2 Noise amplification from map slope

For smooth densities $f_s, f_r$, differentiating
$F_r(T(v)) = F_s(v)$ gives the local slope of the 1-D map:

$$T'(v) \;=\; \frac{f_s(v)}{f_r\!\big(T(v)\big)} .$$

Small perturbations (noise) scale by this slope:
$\sigma_\text{out} \approx T'(v)\,\sigma_\text{in}$. Wherever the reference
distribution is locally *thinner* than the source — i.e. the reference has
more contrast in that range, exactly what you asked for — $T' > 1$ and the
noise is stretched with the signal. Matching a punchy reference therefore
*necessarily* amplifies background noise under any exact histogram match.

## 3. The stretch fit: match the look, not the pixels

NebulaScope's display applies, per channel $c$, a smooth parametric
transfer to the raw value $v$:

$$D_c(v) \;=\; \mathrm{MTF}\!\big(W_c(v);\, m_c\big),$$

where the *window* normalises and clips,

$$W_c(v) \;=\; \operatorname{clip}_{[0,1]}
\left( \frac{\dfrac{v - \ell_c}{h_c - \ell_c} - b_c}{w_c - b_c} \right),$$

with data range $[\ell_c, h_c]$ and black/white points $b_c, w_c$, and the
midtone transfer function is the rational curve

$$\mathrm{MTF}(x; m) \;=\; \frac{(m-1)\,x}{(2m-1)\,x - m},
\qquad m_c = \frac{\mathrm{mid}_c - b_c}{w_c - b_c},$$

which fixes $\mathrm{MTF}(0)=0$, $\mathrm{MTF}(1)=1$ and
$\mathrm{MTF}(m)=\tfrac12$. Every such curve is smooth and strictly
monotone on the window.

**The fit.** Let $t_i = T_c(u_i)$ be the transported display values of a
pixel sample and $v_i$ the corresponding *raw* values. The stretch fit
solves, independently per channel,

$$\min_{\,b_c,\ \mathrm{mid}_c,\ w_c}\;
\sum_i \Big( D_c(v_i;\, b_c, \mathrm{mid}_c, w_c) - t_i \Big)^{2},$$

a bounded three-parameter least-squares problem. NebulaScope solves it by
coordinate descent — four sweeps of golden-section line searches over
$b_c \in [0, w_c)$, $w_c \in (b_c, 1]$, $\mathrm{mid}_c \in (b_c, w_c)$ —
on up to $6 \times 10^4$ sample pairs. The root-mean-square residual per
channel is reported in the status bar.

The result is **only a stretch state**: it lives in the same place as any
Auto STF, composes with copy/paste and *Apply to All*, persists in the
sidecar, and the underlying data is untouched.

### 3.1 Guarantees

- **Zero data loss** — nothing is written; the "result" is a view.
- **No posterization** — the fitted curve is smooth and strictly monotone;
  there are no empirical steps for ties to collapse onto.
- **Controlled noise behaviour** — the curve's slope is the smooth
  parametric $D_c'(v)$, free of the locally extreme slopes an empirical
  density ratio can produce.

### 3.2 The approximation gap

The stretch family is *separable*: three independent monotone curves. The
optimal transport map is generally *not* — its slicing rotations mix
channels, which is how it can rotate hues. The projection of $T$ onto the
separable family therefore reproduces global colour balance and tonality
closely, but cannot express hue rotation. The reported per-channel RMSE
quantifies this gap for your pair of images; when it matters, the exact
mode remains one checkbox away.

## 4. Practice

- Dialog: **Tools ▸ Transport Colors from Reference…**, tick *Apply as
  stretch fit*. Scripts: `transport <row> [strength%] stretch`.
- Strength applies *before* the fit — the fit targets the blended result.
- The fit absorbs the current display (including adjustments) into fresh
  Linear black/mid/white values and resets the adjustment sliders, so
  what you see immediately after is the fitted match itself.
- Judging RMSE: values are in display units on $[0,1]$; experience suggests
  a fit below $\sim 0.02$ is visually convincing, while larger residuals
  usually indicate the reference demanded a hue rotation.

*Background reading: Pitié, Kokaram & Dahyot 2007 (the iterative
distribution transfer this implements); Rabin et al. 2012 for the sliced
Wasserstein viewpoint — full citations in the References.*
