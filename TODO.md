# Direction / TODO

Strategic discussion notes (2026-07-28) — direction, not yet a plan.
**Decision (2026-07-28): the next chapter is (1) Siril/PixInsight
interoperability**, alongside using the tool in real operations.

Local tool survey: Siril 1.4.0-beta1 (`siril-cli` in the app bundle —
script files AND the named-pipe live command interface), GraXpert.app
(CLI via subcommands), PixInsight (no external driving; file-level interop,
already verified both directions).

## The three candidate directions

1. **Interoperability with Siril / PixInsight** — e.g. driving their
   processing from NebulaScope or slotting NebulaScope into their pipelines.
2. **State-of-the-art open-source denoising / deblurring.**
3. **Deepening the differentiators** — the capabilities where PI/Siril are
   weak (star recomposition is already arguably best-in-class; colour
   transport, blink + stretch memory, linked split views, annotations → ML
   training sets, scripting).

## Assessment (agreed)

- **(3) is the backbone.** An open-source inspector wins by being
  unmistakably best at what the suites neglect, not by matching their
  strengths. Everything that improves *looking, comparing, annotating,
  judging* is core. The annotation → ML-training-set story serves an audience
  (researchers) that PI/Siril do not serve at all.
- **(1) is the force multiplier.** Nobody switches suites for an inspector;
  they add one. Siril has a real CLI → genuine integration is tractable.
  PixInsight cannot be cleanly driven externally → PI interop stays at the
  (already verified) file level. **SAMP** (VO messaging: DS9, Aladin, Topcat)
  would suit the academic audience and the existing WCS/annotation features.
  Inbound interop already exists: the `.nsc` scripting lets anyone drive
  NebulaScope.
- **(2) enter sideways, not head-on.** The space is crowded and fast-moving
  (BlurX/NoiseX commercial SOTA, GraXpert free and good). Native ML denoising
  = heavy deps + research risk, off-identity ("raw data never modified").
  The fitting version: **integrate, don't implement.**

## The unifying design candidate

An **external-tools framework**: configure a CLI tool (Siril, GraXpert,
StarNet-likes) — input file → run → ingest output as a new list entry — with
NebulaScope contributing what it is best at: before/after inspection (split
views, blink, transport-style comparison). This single design covers most of
(1) and the sane version of (2). Defer native denoising until this proves
demand.

## Near-term candidates (roughly ordered)

- [x] Debayering — RCD/bilinear/superpixel, BAYERPAT auto-detect, per-image
      override, validated against Siril's RCD (from real-operations need)
- [x] Watch & auto-reload — images overwritten by external tools re-decode
      live in every view (View ▸ Auto-Reload Changed Files; shipped post-v0.89)
- [ ] External-tools framework (GraXpert CLI as the first integration)
- [ ] SPCC/DisplayFunction test hardening (2026-08-06): synthetic XISF
      fixtures (linked/unlinked, far/in-range white, mono), `assert stretch`
      script command, io-level DisplayFunction parse tests. PI's behaviour is
      fiddly to reproduce — fixtures beat live PI sessions.
- [ ] Siril SPCC interop (2026-08-06): same faithfulness work against Siril,
      with the advantage that Siril's SPCC and autostretch are open source —
      semantics verifiable at the source level (siril-cli available locally;
      compare our Shift+U to Siril's linked autostretch, check what Siril
      persists in FITS after SPCC).
- [ ] Siril CLI round-trip (send current image to a Siril script, ingest result)
- [ ] SAMP client (broadcast/receive images + coordinates with DS9/Aladin/Topcat)
- [ ] Differentiator polish as usage reveals friction (star combine, transport,
      annotation/dataset export)

## Colour transport: the full 3-D invertible operator (design notes, 2026-07-31)

Status: **discussion recorded, not yet scheduled.** Field-test order agreed:
first the two-stage stretch fit (v. current dialog), then — if the separable
family still can't reach the needed hues — the operator below. GHS fitting
remains only as a cheap tweak to the existing simple mode; the operator
subsumes it (tonal flexibility comes free in the marginals).

### The problem statement

We want an operator $A : [0,1]^3 \to [0,1]^3$ taking the full 3-D RGB colour
distribution of the source to that of the reference, which is **nonlinear,
strictly monotone, invertible — hence lossless**. The current empirical OT
procedure is not invertible (tie collapse ⇒ many-to-one), and the STF
approximation (even with the stage-2 colour-adjust fit) is not expressive
enough: three separable monotone curves plus four global adjustment
parameters cannot represent an arbitrary smooth hue field.

### Precision: what "lossless" buys, and what it doesn't

Invertibility gives exactly two guarantees: **no colour collapse**
(injectivity ⇒ posterization is impossible, since no two colours can merge)
and **exact reversibility**. It does *not* by itself bound noise
amplification — an invertible map can still have an arbitrarily large local
Jacobian. The correct specification is therefore:

> $A$ strictly monotone, invertible, smooth, with **bounded Jacobian
> singular values** $\sigma_{\max}(J_A) \le S$.

Then noise amplification is $\le S$ everywhere, and $S$ is a natural
user-facing knob: "maximum local contrast gain". (Compare the 1-D noise law
already derived in TRANSPORT.md §2.2, $T'(v) = f_s(v)/f_r(T(v))$: the
empirical map obeys no such bound.)

### The monotone-operator idea is canonical OT theory (Brenier)

The right notion of monotone in 3-D is the vector one:
$\langle A(x) - A(y),\, x - y \rangle > 0$ for $x \ne y$. **Brenier's
theorem**: the $L^2$-optimal transport map between absolutely continuous
measures is the gradient of a convex potential, $T = \nabla\varphi$ — and
gradients of strictly convex functions are exactly the (cyclically)
monotone maps. So "learn a strictly monotone operator approximating $A$" is
not a heuristic: it is the mathematically correct *parametrization of OT
itself*. The empirical sliced procedure is a noisy estimate of that
monotone map; the proposal amounts to projecting it back onto the monotone
class, recovering the structure that finite sampling destroyed.

### Three candidate families (ranked for this codebase)

1. **Smoothed Knothe–Rosenblatt (triangular) map — recommended.**
   $$A(r,g,b) = \big(f_1(r),\; f_2(g;\, r),\; f_3(b;\, r, g)\big),$$
   each $f_i$ a strictly increasing, slope-capped 1-D spline; the
   conditionals estimated from binned conditional quantiles (r-bins for
   $f_2$, (r,g)-bins for $f_3$), smoothed across bins. Properties:
   - a classical transport map in its own right (the KR rearrangement — the
     limit of OT under anisotropic cost scaling, Carlier–Galichon–
     Santambrogio);
   - matches the target distribution *exactly* in the limit;
   - **bijective by construction**, inverse in closed form (invert three
     1-D splines in sequence);
   - **no learning machinery**: conditional histogram specification +
     smoothing, built on the quantile-map code already in `Transport`;
   - noise control = slope caps on each spline.
   One wart: channel-ordering asymmetry (R→G→B privileged). Mitigate by
   averaging the six orderings, or a few slope-capped random rotations —
   at which point it converges toward the sliced procedure *with
   monotonicity and smoothness enforced*.

2. **Monotonicity-constrained 3-D lattice LUT.** A $17^3$–$33^3$ trilinear
   lattice fitted to the OT samples $(u_i, T(u_i))$: minimise
   $\sum_i \omega_i \lVert L(u_i) - T(u_i)\rVert^2 + \lambda\,\text{(lattice
   Laplacian)}$ subject to Jacobian-positivity constraints. Industry-standard
   (this is how colour grading works), fast to apply, and exports directly
   as a standard `.cube` LUT usable in any grading tool — an interop bonus.
   Invertibility is numerical rather than closed-form.

3. **ICNN / Brenier potentials** (Makkuva et al. 2020): learn a convex
   $\varphi$ as an input-convex neural network, use $A = \nabla\varphi$;
   inverse is $\nabla\varphi^*$ (the conjugate). Theoretically canonical,
   but drags a neural training loop into a deterministic, dependency-light,
   testable codebase. Hold in reserve unless 1–2 visibly fall short.

### Architectural home

Make $A$ a **display-pipeline stage** — a fitted colour-operator slot,
applied at render time via 3-D LUT interpolation, stored in the stretch
state and the sidecar like everything else. Then "lossless" holds twice
over: the raw data is never touched (inspector ethos), *and* the operator
itself is invertible if ever baked. Strength blending composes cleanly:
$(1-s)\,\mathrm{Id} + sA$ is monotone whenever $A$ is (convex combinations
of monotone operators are monotone).

### Testable guarantees (unit-test targets when built)

- bijectivity: $A^{-1}(A(x)) = x$ to tolerance on a grid;
- slope caps respected: $\sigma_{\max}(J_A) \le S$ sampled;
- distribution match: pushed-forward histogram vs reference (per-channel
  and joint, e.g. sliced-Wasserstein distance below threshold);
- monotonicity: $\langle A(x)-A(y), x-y\rangle > 0$ on random pairs;
- strength blend stays monotone for all $s \in [0,1]$.

*References to add to the bibliography when this ships: Brenier 1991;
Carlier, Galichon & Santambrogio 2010 (Knothe–Rosenblatt as OT limit);
Makkuva et al. 2020 (OT via input convex neural networks).*
