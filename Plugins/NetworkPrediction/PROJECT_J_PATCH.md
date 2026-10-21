# Project J: UE 5.8.3 NetworkPrediction patch

Source: installed Epic UE 5.8.3, build `58210709`, `Engine/Plugins/Runtime/NetworkPrediction`. Epic copyright headers are retained. Source/config/descriptor only; engine binaries and caches are not copied.

The project plugin overrides the installed plugin by the same name. `Plugins/Mover` is the unchanged matching engine source dependency, rebuilt in the same project plugin scope because engine modules cannot depend on project modules. Do not enable another independent presentation clock or modify the installed engine.

Only production change: `Private/NetworkPredictionWorldManager.cpp` calls `Private/IndependentInterpolationClock.h` for Independent interpolation. Fixed ticking, prediction/rollback, serialization, class layouts and public API are unchanged.

- Bootstrap a first received timestamp into its server epoch with the existing configured target buffer. A late join is not accumulated history from time zero.
- Adjust presentation clock rate within 0.9–1.1 to replenish the same target after jitter.
- Bound forward advancement to received history. A long frame cannot extrapolate beyond the latest received state; a stalled stream holds its last valid interval and resumes without rewinding.
- Keep existing warnings for a genuine invalid timestamp or excessive accumulated backlog. No log category is disabled and the 100/250ms project settings are unchanged.

Regression prefix: `ProjectJ.NetworkPrediction`. `ProjectJ.CaptureMoverBuffers` writes real receive/simulation/presentation CSVs for live verification; check held time and frame stalls as well as warning counts. This does not eliminate the underlying render/game-thread hitch or guarantee smoothness under arbitrary packet loss.

For an engine upgrade, compare pristine engine sources, reapply/revalidate the small patch, and rebuild both plugins. Remove both overrides together when the upstream implementation provides equivalent behavior. Never restore old binaries to avoid the rebuild.
