# ST Comp — guide for agents

ST Comp is the general-purpose compressor of the ST family: thirteen styles (modern and vintage, including the
two-stage LA-76) on one set of controls. Built on the shared module in `../st-shell`
(https://github.com/davidstokarski/st-shell). Read the shell docs first:

- `../st-shell/CLAUDE.md`: non-negotiables (design, real-time safety, native menus, verification)
- `../st-shell/docs/DESIGN.md`, `../st-shell/docs/ARCHITECTURE.md`, `../st-shell/docs/EXAMPLES.md`
- `../st-sat` is the reference plugin; `../st-dc` is the drum compressor this one grew out of.

Plugin-specific notes:

- North star: the Pro-series compressor interaction model (full-window level display with GR trace and threshold
  line, knee display, style + character split, side-chain filter on its own spectrum), without FabFilter names,
  presets or artwork. Look: graphite with a deep green cast, emerald accent, red gain reduction, blue upward gain
  (tokens in `Source/ui/Style.cpp::applyTheme`).
- Parameters: `Source/Params.*` (IDs are permanent, never rename them). Styles: `Source/Styles.*` (the `Style` order is
  permanent: it is the parameter value). **A style is data**: `StyleInfo` holds up to two `StageSpec`s (detector,
  feed-forward / feedback, knee, curve shape, ratio mapping, timing limits, release type, gain smoothing) plus the
  harmonics it adds. Add or tune a style there, not with `if (style == ...)` in the DSP.
- DSP: `Source/dsp/Compressor.*` runs any style: per stage detector -> `StageCurve::target` -> ballistics (attack,
  hold, release type) -> sum of the stages -> range. Feedback stages solve their output-detecting curve so their
  steady state equals `Compressor::staticGainDb` (what the knee display draws); tests check it for every style. Keep
  that true. `Source/dsp/Character.*` is the colour stage (style harmonics + Character / Drive),
  `Source/dsp/SidechainEq.*` the detector filter, `Source/dsp/Engine.*` the whole path (input -> M/S -> side chain ->
  lookahead -> gain -> oversampled colour -> make-up -> latency-aligned mix -> output -> listen -> bypass) plus the
  `st::LevelHistory` the displays read. Oversampling changes re-prepare on the message thread
  (`StCompProcessor::applyPendingChanges`); lookahead changes are allocation-free, the host latency is updated
  asynchronously.
- The level history classes (`st::LevelHistory`, `st::HistoryReader`, `st::SmoothScroll`, `st::HistoryPaths`,
  `st::LiveLevels`) live in the shell. Gain change is signed: negative = reduction, positive = upward.
- UI: `Source/ui/Display.*` (level display, knee, meters, side-chain display), `Source/ui/Panels.*` (control panel,
  footer, side-chain popover), `Source/ui/Strip.*` (the multiview module), `Source/PluginEditor.*`.
- Verify: `StCompPluginTests` (curves and timing per style, lookahead, hold, nulls and latency per oversampling,
  channel modes, side chain, upward, stress, state, presets, multiview with the installed ST EQ AU),
  `StCompPluginTests --snapshot <dir>` (look at the PNGs), `auval -v aufx Stcm StAu`.
- Release: `scripts/release.sh`.
- Anything reusable belongs in st-shell, not here.
