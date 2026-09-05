# Project_J Baseline Capture Guide

**Date:** 2026-09-03  
**Purpose:** Stage 0~1의 성능 기준선을 같은 조건으로 수집하는 실행 가이드. 이 capture는 최적화를 자동 적용하지 않는다.

## 1. 이번 빌드의 CPU trace labels

Unreal Insights CPU timeline에서 아래 Project_J scope를 찾는다.

| Scope | 의미 |
|---|---|
| `Project_J_PlayerCharacterTick` | player actor의 전체 tick |
| `Project_J_PlayerCharacterTick_MovementPolicy` | walk speed 및 combat rotation policy |
| `Project_J_PlayerCharacterTick_Trajectory` | trajectory state update |
| `Project_J_PlayerCharacterTick_LocomotionState` | semantic locomotion state update |
| `Project_J_AnimNativeUpdate` | AnimInstance Game Thread native update 전체 |
| `Project_J_AnimNativeThreadSafeUpdate` | UE thread-safe update phase callback. 아래 두 child scope로 실제 parallel/foreground 실행을 구분한다. |
| `...NativeThreadSafeUpdate_ParallelEvaluation` | `IsRunningParallelEvaluation()=true`인 실제 parallel evaluation 상태 |
| `...NativeThreadSafeUpdate_Foreground` | thread-safe callback이 foreground task 또는 즉시 경로에서 실행된 상태 |
| `Project_J_AnimNativePostEvaluate` | graph evaluation 완료 후 contact-curve 등 completion-side 처리 |
| `Project_J_AnimShouldSkipNativeUpdate` | tier/URO에 따른 native update skip 판단 |
| `Project_J_AnimBuildThreadSafeData` | GT에서 proxy용 snapshot 작성 |
| `Project_J_AnimStateControllerChooser` | event-driven one-shot State Controller Chooser 평가 |
| `Project_J_AnimPublishChooserProperties` | Chooser property publish와 tier policy 적용 |
| `Project_J_AnimBuildOptimizationPolicy` | near/mid/far/hidden animation policy 산출 |
| `Project_J_AnimPoseSearchDatabaseChooser` | current PoseSearch database(PSD) 선택 |
| `Project_J_AnimPublishProxy` | snapshot 및 MM update decision을 proxy로 전달 |
| `Project_J_ServerSideRewindTick` | server-side rewind history update |

이 scope는 Game Thread-side Project_J orchestration을 분리하기 위한 것이다. PoseSearch node search와 AnimGraph evaluate가 이 scope 밖의 engine task에 나타날 수 있으므로, worker/parallel lanes도 함께 검사해야 한다.

## 2. Capture 준비

1. **Development Editor** 또는 packaged **Development** build를 사용한다. Shipping에서는 debug dump가 비활성화된다.
2. 테스트마다 같은 map, graphics scalability, camera path, character placement를 사용한다.
3. capture 중에는 MM transition/network debug log를 계속 켜지 않는다. log formatting 자체가 비용을 오염시킬 수 있다. one-shot/MM 문제 재현 때만 별도 짧은 capture에서 켠다.
4. 가능한 한 standalone 또는 packaged Development를 우선한다. PIE는 편리하지만 Editor 비용이 섞일 수 있으므로 결과에 PIE 여부를 기록한다.

## 3. Unreal Insights capture

### 권장: 실행 인수로 시작

Editor 또는 Development executable을 아래 인수로 시작한다.

```text
-trace=cpu,frame,bookmark,log -tracefile="<Project_J>\Saved\Profiling\S0_Local.utrace"
```

`<Project_J>`는 저장소 루트다. `Saved\Profiling` 폴더가 없다면 먼저 만든다. 개발 환경에서 trace channel 명칭이 다르거나 capture가 시작되지 않으면 Unreal Insights의 Trace Store/Session Browser에서 CPU와 Frame 채널을 선택해 시작한다.

### 대안: runtime console

실행 인수를 쓰지 못하면 게임 콘솔에서 다음 순서로 수행한다. UE 5.8에서는 `Trace.Start` 대신 `Trace.File`을 사용한다.

```text
Trace.File C:/Users/I/Documents/GitHub/Project_J/Saved/Profiling/S0_Local.utrace cpu,frame,bookmark,log
```

시나리오 종료 직후:

```text
Trace.Stop
```

생성된 `.utrace` 위치는 Output Log의 Trace 메시지 또는 Unreal Insights Trace Store에서 확인한다. 파일 위치/이름을 바꾸기보다 원본 `.utrace`를 보관한다.

## 4. Scenario 실행 순서

각 scenario는 20~30초의 안정 구간과, 10초의 locomotion/combat 구간을 포함한다. 시작 장비/전투 모드/카메라 거리를 동일하게 유지한다.

| Scenario | 인구 | 수행 |
|---|---:|---|
| S0 | local 1 | idle → walk/run → sprint → start/stop → 180도 회전 → jump/land → combat enter/exit |
| S1 | local + remote 10 | S0 path를 수행하고 remote players가 화면에 유지되도록 한다 |
| S2 | local + remote 30 | S1과 같은 path 및 거리 tier 분포 기록 |
| S3 | local + remote 50 | S2와 동일. Game/worker/replication p95/p99 확인 |
| S4 | local + low-significance 100 | representation/test harness가 준비된 뒤에만 수행 |

각 시나리오 종료 시 다음 Exec 명령을 한 번 실행하고 Output Log를 보관한다.

```text
DumpMMOProfilingSnapshot 16
DumpAnimBudget
DumpReplicationPolicy
DumpLocomotionKinematics
```

one-shot/MM 선택을 따로 확인해야 하는 짧은 재현에서만 다음을 사용한다.

```text
p.ProjectJ.MMTransitionDebug 1
p.ProjectJ.MMNetDebug 1
; start/stop, pivot, land, combat enter/exit를 한 번씩 재현
DumpMotionMatchingTrace
DumpMotionMatchingTransitionTrace
p.ProjectJ.MMTransitionDebug 0
p.ProjectJ.MMNetDebug 0
```

`DumpMotionMatchingTransitionTrace`는 native transition/pivot trace를 Output Log에 기록한다. MM debug 화면에 one-shot asset이 보이지 않는다는 사실만으로 오류로 판단하지 않는다.

### Development visual crowd (10 / 30 / 50)

에디터 배치나 수동 조작 없이, 현재 possessed player class를 로컬 clone으로 자동 생성할 수 있다.

```text
StartProfilingVisualCrowd 50
DumpProfilingVisualCrowd
```

clone은 spacing을 두고 왕복 이동하며, replication과 movement replication을 모두 끈다. 따라서 이것은 **visual/animation/CPU scaling** 용도이며, 50 simulated proxy 또는 dedicated-server network test가 아니다. capture 종료 뒤에는 반드시 정리한다.

```text
StopProfilingVisualCrowd
```

10/30/50/70/100을 각각 별도 trace로 비교한다. 실제 remote-player networking과 bandwidth/AOI 검증은 dedicated-server bot scenario에서 별도로 수행한다.

### 100명 local visual: CPU / worker task / GPU 분리 capture

100은 현 하네스의 상한이며, local visual/animation scaling의 stress point다. 시작·종료 처리 비용을 결과에 섞지 않도록 clone이 움직이는 것을 확인한 뒤 trace를 시작한다.

```text
StartProfilingVisualCrowd 100
; 3~5초 대기
DumpProfilingVisualCrowd
; Spawned=100, Moving=100 확인
Trace.File C:/Users/I/Documents/GitHub/Project_J/Saved/Profiling/S100_LocalVisual_CPU_GPU_Tasks.utrace cpu,frame,bookmark,log,tasks,animation
; 20~30초 안정 구간 유지. player/camera는 S70과 같은 위치 유지.
Trace.Stop
DumpProfilingVisualCrowd
StopProfilingVisualCrowd
```

`tasks`는 UE 5.8 Task Graph trace channel이고 `animation`은 AnimGraph runtime trace channel이다. Unreal Insights에서 선택 구간의 GameThread, `TaskGraphThread*`, RenderThread/RHIThread 및 Tasks view를 함께 본다. `Project_J_AnimNativeUpdate`가 Game Thread snapshot path인 것과 actual AnimGraph evaluate/PoseSearch engine task의 worker 실행은 구분한다. `gpu`는 이 command에 넣지 않는다. GPU는 아래의 `ProfileGPU` 단일-frame capture로 별도 측정한다.

GPU pass 상세가 필요하면 CPU trace와 별개로 같은 화면·camera에서 다음을 한 번 실행한다. GPU capture는 profiling overhead가 있으므로 20~30초 CPU trace 도중에는 실행하지 않는다.

```text
ProfileGPU
```

GPU Visualizer에서 skeletal mesh, shadow/VSM, Lumen, translucency/Niagara, base pass의 상위 항목 screenshot을 보관한다.

### Root motion / worker-path 확인 capture

현재 프로젝트는 `RootMotionMode=RootMotionFromEverything`이며, 100명 workload에서 `NativeThreadSafeUpdateAnimation` callback count가 0이었다. 아래 trace는 설정을 바꾸지 않고 이 사실을 재확인하고, evaluation/completion의 실제 thread lane을 분리해 보는 용도다.

```text
StartProfilingVisualCrowd 100
; 3~5초 대기 후 Moving이 98~100 범위인지 확인
DumpAnimationExecutionPolicy
Trace.File C:/Users/I/Documents/GitHub/Project_J/Saved/Profiling/S100_AnimationPath.utrace cpu,frame,bookmark,log,tasks,animation
; 20초 안정 이동 유지
Trace.Stop
StopProfilingVisualCrowd
```

Insights에서 `Project_J_AnimNativeThreadSafeUpdate_ParallelEvaluation`, `Project_J_AnimNativeThreadSafeUpdate_Foreground`, `Project_J_AnimNativePostEvaluate`, `USkeletalMeshComponent_CompleteParallelAnimationEvaluation`을 각각 Timers 검색으로 확인한다. `FParallelAnimationEvaluationTask`는 UE의 cycle-stat 이름이라 CPU timer 검색에 나오지 않을 수 있다. **Tasks 탭의 검색 결과가 없다는 것만으로 worker 부재라고 결론 내리지 않는다.** CPU Timing View의 `TaskGraphThread*` lane과 timer count를 함께 확인한다. `a.ForceParallelAnimUpdate=1` 또는 Root Motion Mode 변경은 이 capture에서 사용하지 않는다.

## 5. 함께 보낼 자료

각 scenario마다 아래만 보내면 된다.

1. 원본 `.utrace` 파일 또는 Unreal Insights CPU Timing/Threads 화면의 screenshot
2. `Project_J.log`에서 해당 capture 시각의 `MMOProfileSummary`와 `MMOProfile Actor=` 줄
3. 실행 방식(PIE/standalone/packaged), hardware, resolution/scalability, player/NPC 수
4. 이상 현상이 있으면 재현 절차와 10~20초짜리 MM transition log

trace가 너무 크면 `.utrace` 대신 Insights screenshot을 우선 보내도 된다. screenshot은 **CPU Timing**, **Timing View threads**, 그리고 Project_J scope가 펼쳐진 구간을 포함한다.

## 6. 이번 기준선에서 아직 결론 내리지 않는 것

- Iris custom filter/prioritizer의 runtime integration
- 100명 NPC/Mass가 아직 없는 상태에서의 MMO-scale 성능
- PoseSearch PCA/KDTree/channel/pruning tuning
- worker task 도입, object pooling, Animation Sharing/Leader Pose 적용
- Renderer/device-profile 정책

이들은 S0~S3 trace와 actual content/representation evidence 뒤에 선택한다.
