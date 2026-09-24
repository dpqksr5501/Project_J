# Project_J Profiling Baseline Results

**Date:** 2026-09-03  
**Build:** `Project_JEditor Win64 Development`  
**Tool:** Unreal Insights / CPU trace scopes  
**Purpose:** 최적화 전후를 같은 workload에서 비교할 수 있도록, 실제 로컬 CPU·animation 기준선을 보관한다. 수치는 평균 FPS가 아니라 Insights에서 선택한 구간의 timer aggregate다.

## 1. 결과를 읽는 규칙

- `Incl`은 해당 scope와 자식 scope를 합친 선택 구간 전체 시간이며, 여러 thread에서 겹쳐 실행될 수 있다. 프레임 wall time과 동일시하지 않는다.
- `Excl`은 해당 scope 자체 시간이다.
- `1회 평균`은 `Incl / Count`이며, 개별 호출의 p95/p99가 아니다.
- S0과 S70은 인구·시나리오·선택 구간이 다르다. 이 문서의 S0 값은 참고용 anchor이며, 비용 기울기 비교에는 같은 harness/graphics/camera 조건으로 새로 수집한 S0/S10/S30/S50/S70만 사용한다.
- 이 문서의 local visual crowd는 **replication을 끈 로컬 actor**다. dedicated server, simulated proxy, Iris, bandwidth, relevancy의 성능 결과가 아니다.

## 2. Trace inventory

| ID | 원본 trace | workload | 상태 |
|---|---|---|---|
| S0-MovementPolicy | `Saved/Profiling/S0_MovementPolicy.utrace` | local player 1명, locomotion/camera 이동 | 기존 초기 기준선 |
| S50-first | `Saved/Profiling/Crowd50.utrace` | 50 local clone, 초기 하네스 | clone이 이동하지 않아 animation scaling 기준선으로 **사용하지 않음** |
| S70-MovingCrowd | `Saved/Profiling/S50_MovingCrowd.utrace` | local player + 이동 local clone 70명 | 현재 유효 visual/animation CPU 기준선 |
| S100-CPU-GPU-Tasks | `Saved/Profiling/S100_LocalVisual_CPU_GPU_Tasks.utrace` | local player + 이동 local clone 100명 | CPU/Tasks capture 및 별도 `ProfileGPU` 1-frame sample |
| S100-WorkerProof | `Saved/Profiling/S100_AnimWorkerProof_v2.utrace` 및 후속 18.01초 capture | local player + 이동 local clone 100명 | parallel evaluation dispatch 및 native thread-safe update 경로 확인 |

> 파일명 `S50_MovingCrowd.utrace`는 명령 입력 시 정한 이름이지만, 로그와 trace의 실제 workload는 70명이다. 이후 비교 문서에서는 **S70-MovingCrowd**로 표기한다. 원본 파일은 재현성을 위해 이름을 바꾸지 않는다.

## 3. S0: 기존 단일 local player 이동 정책 anchor

이 값은 `S0_MovementPolicy.utrace`의 기존 Insights 선택 결과다. 원본 trace와 당시 선택 구간이 최종 근거이며, 이 표는 빠른 전후 대조용이다.

| Scope | Count | Incl | Excl | 1회 Incl 평균 |
|---|---:|---:|---:|---:|
| `Project_J_PlayerCharacterTick_MovementPolicy` | 7,500 | 300.09 ms | 0.92 ms | 40.01 µs |
| `...ApplyCombatRotationMode` | 7,500 | 294.05 ms | 6.68 ms | 39.21 µs |
| `...LocomotionState` | 7,500 | 14.94 ms | 14.93 ms | 1.99 µs |
| `...Trajectory` | 7,500 | 12.64 ms | 12.64 ms | 1.69 µs |
| `...UpdateMaxWalkSpeed` | 7,500 | 5.12 ms | 5.12 ms | 0.68 µs |

**S0 판단:** stationary combat/root-motion 보정이 포함된 `ApplyCombatRotationMode`가 MovementPolicy의 대부분을 차지했지만, 호출당 약 39 µs다. 움직이는 입력과 TIP root motion에 필요한 경로이므로, 이 수치만으로 tick 제거·Task Graph 이동을 수행하지 않는다.

## 4. S70-MovingCrowd: 유효한 visual/animation CPU 기준선

### 4.1 Workload 및 증거

| 항목 | 값 |
|---|---|
| 실행 방식 | Development Editor PIE |
| 생성 명령 | `StartProfilingVisualCrowd 70` |
| clone 상태 | `Spawned=70`, `Moving=70`, `Replicated=false` |
| 추가 local actor | 현재 possessed local player 1명 |
| clone controller | 로컬 비틱 AIController (CMC movement 구동 전용) |
| 충돌 정책 | clone끼리 Pawn collision ignore, world/floor collision 유지 |
| 입력 경로 | 연속 이동 후 반주기마다 방향 반전. artificial target-chasing jitter 제거 |
| trace 명령 | `Trace.File .../Saved/Profiling/S50_MovingCrowd.utrace cpu,frame,bookmark,log` |
| trace 기록 시간 | 약 44.5초 (`Trace.File`~`Trace.Stop`) |
| Insights 선택 구간 | 39.65초 (capture start/stop 주변 안정화 구간 제외) |

70 clones가 모두 이동했다는 로그:

```text
ProfilingVisualCrowd started Requested=70 Spawned=70 Replicated=false Mode=VisualCpuOnly
ProfilingVisualCrowd movement health Moving=70 Spawned=70
ProfilingVisualCrowd Spawned=70 Moving=70 Replicated=false Purpose=VisualAnimationCpuOnly
```

### 4.2 Project_J CPU timers (39.65초 선택 구간)

| Scope | Count | Incl | Excl | 1회 Incl 평균 |
|---|---:|---:|---:|---:|
| `Project_J_PlayerCharacterTick` | 292,175 | 596.78 ms | 50.72 ms | 2.04 µs |
| `Project_J_AnimNativeUpdate` | 98,930 | 429.67 ms | 65.94 ms | 4.34 µs |
| `...PlayerCharacterTick_Trajectory` | 292,175 | 290.48 ms | 290.48 ms | 0.99 µs |
| `...PlayerCharacterTick_LocomotionState` | 292,175 | 183.30 ms | 183.30 ms | 0.63 µs |
| `...AnimBuildThreadSafeData` | 98,930 | 175.72 ms | 175.72 ms | 1.78 µs |
| `...AnimStateControllerChooser` | 98,930 | 81.44 ms | 22.49 ms | 0.82 µs |
| `...AnimPublishProxy` | 98,930 | 79.79 ms | 34.79 ms | 0.81 µs |
| `...PlayerCharacterTick_MovementPolicy` | 292,175 | 72.35 ms | 19.49 ms | 0.25 µs |
| `...PlayerCharacterTick_UpdateMaxWalkSpeed` | 292,175 | 42.65 ms | 42.65 ms | 0.15 µs |
| `...AnimShouldSkipNativeUpdate` | 98,930 | 26.77 ms | 17.89 ms | 0.27 µs |
| `...AnimPoseSearchDatabaseChooser` | 98,930 | 20.79 ms | 20.79 ms | 0.21 µs |
| `...AnimBuildOptimizationPolicy` | 296,728 | 20.44 ms | 20.44 ms | 0.07 µs |
| `...AnimPublishChooserProperties` | 100,130 | 18.64 ms | 12.91 ms | 0.19 µs |
| `Project_J_ProfilingVisualCrowdTick` | 4,058 | 12.56 ms | 12.56 ms | 3.10 µs |
| `...PlayerCharacterTick_ApplyCombatRotationMode` | 292,175 | 10.23 ms | 10.23 ms | 0.04 µs |

`PlayerCharacterTick`은 약 7,369회/초, `AnimNativeUpdate`는 약 2,495회/초였다. 71개 캐릭터(70 clones + local player)의 actor tick은 유지되지만, animation native update가 동일 빈도로 실행되지는 않는다. 이 기록은 현재 URO/Animation Budget/tier 정책이 update cadence에 영향을 주고 있음을 보여준다.

### 4.3 `AnimNativeUpdate` 호출 관계

Insights Callers 및 source 대조 결과:

```text
USkeletalMeshComponent::TickAnimation
  -> ABP_Humanoid_Master_C
    -> UProject_JCharacterAnimInstance::NativeUpdateAnimation
      -> BuildThreadSafeData
      -> StateControllerChooser
      -> PublishProxy
```

- `BuildThreadSafeData`는 `NativeUpdateAnimation()`에서 직접 호출된다.
- `ThreadSafe`는 worker가 읽을 immutable proxy snapshot이라는 의미이며, 이 scope 자체가 worker task라는 뜻은 아니다.
- `AnimNativeUpdate` 내 비중: BuildThreadSafeData 40.9%, StateControllerChooser 19.0%, PublishProxy 18.6%, ShouldSkipNativeUpdate 6.2%.
- Engine의 실제 parallel AnimGraph evaluation/PoseSearch node task 비용은 이 Project_J orchestration scope 밖일 수 있으므로, worker lane과 engine animation task를 별도 capture에서 확인해야 한다.

## 5. S100: local visual 100명 CPU/GPU/Tasks 기준선

### 5.1 Workload

| 항목 | 값 |
|---|---|
| 실행 방식 | Development Editor PIE |
| 생성 명령 | `StartProfilingVisualCrowd 100` |
| clone 상태 | 로그상 `Spawned=100`, 안정화 샘플에서 `Moving=98~99` |
| trace | `S100_LocalVisual_CPU_GPU_Tasks.utrace`, 약 36초 안정 구간 선택 |
| 범위 | local visual CPU/animation용. replication, remote proxy, server 부하는 포함하지 않음 |

`Moving`이 100보다 1~2 작게 잡힌 샘플은 이동 하네스가 반주기에 방향을 반전하며 감속/재가속하는 순간의 상태다. 정지 또는 생성 실패로 해석하지 않는다.

### 5.2 Project_J CPU timers (약 35.95초 선택 구간)

| Scope | Count | Incl | Excl | 1회 Incl 평균 |
|---|---:|---:|---:|---:|
| `Project_J_PlayerCharacterTick` | 288,965 | 601.00 ms | 48.78 ms | 2.08 µs |
| `Project_J_AnimNativeUpdate` | 95,270 | 423.17 ms | 65.48 ms | 4.44 µs |
| `...PlayerCharacterTick_Trajectory` | 288,965 | 293.77 ms | 290.48 ms | 1.02 µs |
| `...PlayerCharacterTick_LocomotionState` | 288,965 | 187.79 ms | 187.79 ms | 0.65 µs |
| `...AnimBuildThreadSafeData` | 95,269 | 175.49 ms | 175.49 ms | 1.84 µs |
| `...AnimStateControllerChooser` | 95,269 | 80.61 ms | 21.45 ms | 0.85 µs |
| `...AnimPublishProxy` | 95,269 | 76.61 ms | 33.67 ms | 0.80 µs |
| `...PlayerCharacterTick_MovementPolicy` | 288,965 | 80.86 ms | 18.61 ms | 0.28 µs |
| `...PlayerCharacterTick_UpdateMaxWalkSpeed` | 288,965 | 42.56 ms | 42.56 ms | 0.15 µs |
| `...AnimShouldSkipNativeUpdate` | 95,269 | 25.32 ms | 17.16 ms | 0.27 µs |
| `...AnimPoseSearchDatabaseChooser` | 95,270 | 20.04 ms | 20.04 ms | 0.21 µs |

100명에서도 scope당 호출 평균은 S70과 같은 수준이다. 이번 구간만으로 animation/chooser 비용의 비정상적인 인구 증가나 Game Thread 포화를 주장할 근거는 없다. 다만 Editor PIE와 local visual harness의 값이므로 shipping/dedicated-server 예산으로 직접 전환하지 않는다.

### 5.3 GPU sample (`ProfileGPU`)

`ProfileGPU`의 한 프레임 sample은 `Frame 3.99 ms`, `SceneRender - ViewFamilies 3.70 ms`였다. 주요 항목은 PostProcessing 약 0.70 ms, RenderDeferredLighting 약 0.36 ms, ShadowDepths 약 0.25 ms, BasePass 약 0.15 ms, RayTracingGeometry 약 0.10 ms였다. `GPUSkinCache_UpdateSkinningBatches`는 약 0.01 ms였다.

이는 이 카메라/에디터 프레임에서 GPU가 100명 skeletal clone의 즉시 병목이라는 증거가 없다는 뜻이다. single-frame 결과이므로 GPU p95/p99 또는 worst camera 결론으로 사용하지 않는다.

### 5.4 Tasks 및 병렬 animation 정책 확인

Tasks 보기에서 보인 큰 항목은 `MassProcessingQueue Main-Thread Runner Task`와 `UMassEntityEditorSubsystem::Tick` 계열이었다. Editor/Mass processor 비용이므로 Project_J 캐릭터 animation worker 비용으로 귀속하지 않는다.

`DumpAnimationExecutionPolicy` 출력:

```text
ParallelEval=1 ParallelUpdate=1 ForceParallelUpdate=0 ParallelInterpolation=1
EngineAllowMT=1 AnimAllowMT=1 CanRunParallel=1 RootMotionMode=3
Mesh=CharacterMesh0 AnimInstance=ABP_Humanoid_Master_C_0
```

- 병렬 evaluation/update를 허용하는 engine·project·AnimInstance 플래그는 모두 켜져 있고 `CanRunParallel=1`이다.
- `RootMotionMode=3`은 `RootMotionFromEverything`이다.
- 초기 worker-proof trace의 약 11.7초 선택 구간에서 callback count가 0이었으나, 이는 그 capture/선택 구간만의 결과였다. 후속 18.01초 capture에서는 `Project_J_AnimNativeThreadSafeUpdate`가 **43,826회**(Incl 4.22 ms) 실행됐다.
- 같은 후속 capture에서 `Project_J_AnimNativePostEvaluate`는 **50,956회**(Incl 16.47 ms), `USkeletalMeshComponent_CompleteParallelAnimationEvaluation`은 **130,174회**(Incl 2.55 s)였다. Engine source상 후자는 `FParallelAnimationEvaluationTask` 완료 뒤 Game Thread에서 수행되는 completion task다. 따라서 2.55 s를 worker evaluation 비용으로 해석하지 않는다.
- `FParallelAnimationEvaluationTask`는 UE의 cycle-stat 이름이어서 CPU Timers 검색에 반드시 나타나는 trace timer가 아니다. `CompleteParallelAnimationEvaluation`의 존재는 parallel evaluation task가 dispatch/complete됐다는 강한 증거다.
- `NativeThreadSafeUpdate`의 caller가 `ExecuteTask -> ExecuteForegroundTask`인 것만으로 callback이 worker thread에서 실행됐다고 확정할 수 없다. Task Graph가 foreground execution을 선택할 수 있기 때문이다. 다음 capture부터는 `_ParallelEvaluation`/`_Foreground` child scope가 `IsRunningParallelEvaluation()` 결과를 기록해 이를 직접 판정한다.

### 5.5 S100 animation-path v2: parallel/foreground 직접 판정 (18.6초)

새 child scope를 포함한 후속 100명 capture 결과:

| Scope | Count | Incl | 1회 Incl 평균 | 해석 |
|---|---:|---:|---:|---|
| `Project_J_AnimNativeThreadSafeUpdate_ParallelEvaluation` | 47,274 | 4.63 ms | 0.098 µs | `IsRunningParallelEvaluation()=true` |
| `Project_J_AnimNativeThreadSafeUpdate_Foreground` | 4 | 0.0004 ms | 0.100 µs | 전환/초기화 수준의 극소수 호출 |

thread-safe update callback의 표본 중 약 **99.99%**가 parallel-evaluation 상태였다. 따라서 현재 S100 local visual workload에서 Project_J의 AnimGraph parallel-update/evaluation 경로는 실질적으로 worker execution을 사용한다. 이 callback 자체의 비용은 매우 작으므로, 이를 더 쪼개거나 별도 Task Graph로 옮기는 최적화는 수행하지 않는다.

이 결론은 100명 local visual workload의 animation path에 한정한다. root-motion montage/traversal의 최악 구간, remote proxy, dedicated server 및 GPU p95/p99은 별도 scenario에서 계속 측정한다.

성능만을 위해 Root Motion Mode를 바꾸거나 `a.ForceParallelAnimUpdate=1`을 강제하지 않는다. 몽타주/one-shot/traversal의 root-motion 정확성과 네트워크 예측 검증이 선행되어야 한다.

## 6. 현재 의사결정

### 확정

1. 현재 70명 visual/animation CPU workload에서 MM/Chooser/trajectory snapshot은 P0 병목 증거가 없다.
2. `BuildThreadSafeData`를 별도 Task Graph 작업으로 강제 분리하지 않는다. 현재 호출당 1.78 µs인 경로에 task dispatch·synchronization·복사 비용을 추가할 이유가 없다.
3. local clone harness는 S10/S30/S50/S70 visual CPU 추세 측정에 사용한다. 동일 map/camera/scalability에서만 비교한다.
4. S100에서 parallel evaluation dispatch와 callback의 worker/foreground 비율이 확인됐다. root-motion 정책은 기능/네트워크 검증이 필요한 architecture 결정으로 남기며, 단순 Task Graph 강제 분리 대상이 아니다.

### 아직 확정하지 않음

1. 전체 Game Thread / Render Thread / GPU frame p95/p99 및 hitch 원인.
2. actual AnimGraph evaluation의 worker-thread utilization 및 PoseSearch search cost.
3. 50 remote simulated proxy 또는 dedicated-server 50 client의 replication/CMC/server CPU.
4. PSD PCA/KDTree/channel/pruning tuning, Animation Sharing, object pooling, AI/Mass, Iris/AOI. 이들은 현재 trace만으로 적용하지 않는다.

## 7. 다음 비교 캡처 규격

1. local visual: S0, S10, S30, S50, S70, S100을 같은 map/scalability/camera/20초 안정 구간으로 수집한다.
2. 각 trace에서 selected duration, clone count, `Spawned`, `Moving`, resolution/scalability를 이 문서 표 형식으로 추가한다.
3. actual networking은 별도 dedicated server + 2/10/30/50 client 시나리오에서 Network Insights, replication bytes, server frame p95/p99로 기록한다. local clone 결과를 network 결론으로 사용하지 않는다.
4. 최적화 변경은 S0와 동일 인구, S70과 동일 인구 각각에서 재측정하고, 평균뿐 아니라 p95/p99/hitch 및 presentation correctness를 함께 비교한다.
