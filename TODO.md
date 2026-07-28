# Direction / TODO

Strategic discussion notes (2026-07-28) — direction, not yet a plan. Current
phase: **use NebulaScope in real operations** and observe where its impact is
greatest before committing to any branch below.

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

- [ ] External-tools framework (GraXpert CLI as the first integration)
- [ ] Siril CLI round-trip (send current image to a Siril script, ingest result)
- [ ] SAMP client (broadcast/receive images + coordinates with DS9/Aladin/Topcat)
- [ ] Differentiator polish as usage reveals friction (star combine, transport,
      annotation/dataset export)
