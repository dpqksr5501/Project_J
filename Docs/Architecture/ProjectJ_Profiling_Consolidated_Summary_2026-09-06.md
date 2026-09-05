# Project_J Profiling Consolidated Summary

**Updated:** 2026-09-06

**Purpose:** 2026-09-03부터 2026-09-06까지 이 프로젝트에서 수집한 local
animation/CPU, GPU, PIE networking/Iris, N50 replicated-movement 결과를 한
곳에서 비교·추적한다. 이 문서는 새 측정을 요구하지 않는다. 원본 trace와 당시
Insights의 선택 구간이 최종 근거이며, 아래 표는 의사결정과 변경 전후 비교를 위한
요약이다.

## 1. 먼저 읽을 규칙

| 구분 | 의미 |
|---|---|
| `Incl` / `Excl` | Unreal Insights timer aggregate다. 각각 자식 포함 시간 / scope 자체 시간이며 frame wall time이나 p95/p99가 아니다. |
| 1회 평균 | `Incl / Count`다. 호출당 평균이며 worst frame 또는 percentile이 아니다. |
| local visual crowd | `StartProfilingVisualCrowd`가 생성한 **비복제** local clone이다. CPU/animation/visual 전용이며 server, remote proxy, Iris, bandwidth 결론에 사용하지 않는다. |
| N2/N50 PIE | Development Editor의 한 process에 dedicated-server world와 client world가 함께 있을 수 있다. Network Insights에서 server outgoing으로 고른 selection만 network 비용으로 해석한다. |
| 비교 제한 | S0/S70/S100은 map·camera·선택 구간·인구가 완전히 같지 않다. 정확한 성능 기울기에는 같은 workload의 재측정만 사용한다. N2와 N50도 actor 수·입력·connection 조건이 달라 단순 비율 비교를 하지 않는다. |

## 2. 수집본 목록과 현재 판정

| ID | 원본 trace / 자료 | Workload | 현재 용도 | 판정 |
|---|---|---|---|---|
| S0 | `Saved/Profiling/S0_MovementPolicy.utrace` | local player 1명, locomotion/camera | 단일 캐릭터 movement-policy anchor | 참고 기준선 |
| S70 | `Saved/Profiling/S50_MovingCrowd.utrace` | local player + 이동 clone 70명 | 유효한 local visual/animation CPU 기준선 | 통과 |
| S100 CPU/GPU | `Saved/Profiling/S100_LocalVisual_CPU_GPU_Tasks.utrace` + `ProfileGPU` | local player + 이동 clone 100명 | local visual CPU, tasks, 단일 GPU sample | 통과 |
| S100 worker proof | `Saved/Profiling/S100_AnimWorkerProof_v2.utrace` + 후속 18.6초 capture | local player + 이동 clone 100명 | parallel animation path 직접 판정 | 통과 |
| N2 Iris | `Saved/Profiling/N2_Iris_NetVerbose.utrace` | PIE dedicated server + client 2 | Iris runtime 및 저인구 packet/object 기준선 | 통과 |
| N50 movement | `Saved/Profiling/N50_MovementOnly.utrace` | server mover 50명 -> client 2, 30 Hz | 이동 복제 outbound 비용 기준선 | 통과 |

> 파일명 `S50_MovingCrowd.utrace`는 실제로 70 clone workload를 기록한다. 이 문서에서는 혼동을 피하기 위해 S70으로 표기한다.

## 3. Local CPU와 animation orchestration

### S0: local player 1명 movement-policy anchor

| Scope | Count | Incl | Excl | 1회 Incl 평균 |
|---|---:|---:|---:|---:|
| `Project_J_PlayerCharacterTick_MovementPolicy` | 7,500 | 300.09 ms | 0.92 ms | 40.01 µs |
| `...ApplyCombatRotationMode` | 7,500 | 294.05 ms | 6.68 ms | 39.21 µs |
| `...LocomotionState` | 7,500 | 14.94 ms | 14.93 ms | 1.99 µs |
| `...Trajectory` | 7,500 | 12.64 ms | 12.64 ms | 1.69 µs |
| `...UpdateMaxWalkSpeed` | 7,500 | 5.12 ms | 5.12 ms | 0.68 µs |

`ApplyCombatRotationMode`가 당시 stationary combat/root-motion 보정이 포함된
MovementPolicy의 대부분을 차지했지만, 평균 약 39 µs다. 기능에 필요한 경로이므로
이 수치만으로 tick 제거 또는 Task Graph 이전을 수행하지 않았다.

### S70: local moving visual crowd 70명

**조건:** Development Editor PIE, `StartProfilingVisualCrowd 70`, local player 1명
추가, `Spawned=70`, `Moving=70`, `Replicated=false`. clone은 pawn 간 collision을
무시하고, 연속 이동 후 반주기마다 방향을 반전한다. trace 기록은 약 44.5초이며
Insights 안정 selection은 39.65초다.

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
| `...AnimPoseSearchDatabaseChooser` | 98,930 | 20.79 ms | 20.79 ms | 0.21 µs |

71개 character의 actor tick은 초당 약 7,369회였지만 animation native update는
초당 약 2,495회였다. URO/Animation Budget/tier가 animation update cadence에
영향을 주고 있으며, actor tick 수와 animation update 수를 같은 값으로 간주하면 안 된다.

`NativeUpdateAnimation()` 내부의 Project_J orchestration 비중은
`BuildThreadSafeData` 40.9%, State Controller Chooser 19.0%, proxy publish 18.6%,
skip 판단 6.2%다. `BuildThreadSafeData`의 `ThreadSafe` 명칭은 worker가 읽을
immutable snapshot을 뜻할 뿐, 이 scope가 곧 worker task라는 뜻은 아니다.

### S100: local moving visual crowd 100명

**조건:** Development Editor PIE, `StartProfilingVisualCrowd 100`, 안정화 로그상
`Moving=98~99`, 약 35.95초 안정 selection. 방향 반전 중 1~2명이 감속/재가속해
`Moving`에서 빠지는 순간은 생성 실패나 영구 정지가 아니다.

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
| `...AnimPoseSearchDatabaseChooser` | 95,270 | 20.04 ms | 20.04 ms | 0.21 µs |

S70과 S100의 scope당 평균은 비슷하다. 이 결과만으로 animation/Chooser 비용의
비정상적 인구 증가 또는 Game Thread 포화를 주장할 근거는 없다. 반대로 shipping,
dedicated server, remote simulated proxy 성능으로 일반화할 수도 없다.

## 4. Animation worker / parallel execution

### Runtime policy

```text
ParallelEval=1 ParallelUpdate=1 ForceParallelUpdate=0 ParallelInterpolation=1
EngineAllowMT=1 AnimAllowMT=1 CanRunParallel=1 RootMotionMode=3
Mesh=CharacterMesh0 AnimInstance=ABP_Humanoid_Master_C_0
```

Engine, project, AnimInstance 모두 병렬 animation을 허용한다. `RootMotionMode=3`
은 `RootMotionFromEverything`이며, 성능만을 이유로 root motion 또는
`a.ForceParallelAnimUpdate`를 바꾸지 않았다.

### Direct proof (S100, 18.6초)

| Scope | Count | Incl | 1회 Incl 평균 | 해석 |
|---|---:|---:|---:|---|
| `Project_J_AnimNativeThreadSafeUpdate_ParallelEvaluation` | 47,274 | 4.63 ms | 0.098 µs | `IsRunningParallelEvaluation()=true` |
| `Project_J_AnimNativeThreadSafeUpdate_Foreground` | 4 | 0.0004 ms | 0.100 µs | 초기화/전환 수준 |

동일 callback 표본의 약 **99.99%**가 parallel-evaluation 상태였다. 따라서 현 S100
local visual workload에서 AnimGraph parallel path는 실제로 사용된다. callback의
자체 비용도 매우 작으므로, `BuildThreadSafeData`를 별도 task로 강제 분리하는 것은
dispatch/synchronization/copy 비용만 추가할 가능성이 높아 수행하지 않는다.

별도 후속 sample에는 `Project_J_AnimNativePostEvaluate` 50,956회 / 16.47 ms,
`USkeletalMeshComponent_CompleteParallelAnimationEvaluation` 130,174회 / 2.55 s가
기록됐다. 후자는 `FParallelAnimationEvaluationTask` 완료 뒤 Game Thread
completion 경로이므로 2.55초 전체를 worker evaluate 비용으로 해석하지 않는다.
또한 `FParallelAnimationEvaluationTask`는 cycle-stat 이름이라 CPU Timers 검색에
항상 나타나지 않는다.

## 5. GPU single-frame sample

S100과 같은 visual workload/camera에서 `ProfileGPU` 한 프레임은 다음과 같다.

| GPU scope | 시간 |
|---|---:|
| `Frame` | 3.99 ms |
| `SceneRender - ViewFamilies` | 3.70 ms |
| PostProcessing | 약 0.70 ms |
| RenderDeferredLighting | 약 0.36 ms |
| ShadowDepths | 약 0.25 ms |
| BasePass | 약 0.15 ms |
| RayTracingGeometry | 약 0.10 ms |
| `GPUSkinCache_UpdateSkinningBatches` | 약 0.01 ms |

이 단일 camera/frame에서는 100 skeletal clone이 즉시 GPU 병목이라는 증거가 없다.
단일 frame이므로 GPU p95/p99, worst camera, 실전 VFX/전투 장면의 결론으로 쓰지 않는다.

## 6. Networking / Iris

### N2 PIE topology와 Iris runtime

Development Editor PIE에서 `Play As Client`, player 2로 실행했다.

```text
DedicatedServer: IrisActive=1 ReplicationModel=Iris ClientConnections=2
Client A: IrisActive=1 ReplicationModel=Iris HasServerConnection=1
Client B: IrisActive=1 ReplicationModel=Iris HasServerConnection=1
```

`ReplicationDriver=None`은 Replication Graph가 별도로 연결되지 않았다는 뜻이며,
Iris 실패가 아니다. Iris 활성화에는 `net.Iris.UseIrisReplication=1` 및
`net.SubObjects.DefaultUseSubObjectReplicationList=1`을 사용했다. inventory와
equipment FastArray에는 Iris fragment 지원을 적용해 초기 state 전달까지 확인했다.
실제 add/remove/equip/unequip delta는 해당 기능이 본격화될 때 별도로 검증한다.

### N2 Iris detailed packet baseline

**조건:** server, connection 0, outgoing, steady-state selection 약 32.5초,
`ProjectJ.SetNetTraceVerbosity 2`, `cpu,frame,bookmark,log,net`.

| Event / object | Count | Incl (bits) | Incl (bytes, approx.) |
|---|---:|---:|---:|
| `PacketHeaderAndInfo` | 1,739 | 159,988 | 20.0 KB |
| `DataStream` | 360 | 77,751 | 9.7 KB |
| `ReplicationData` | 360 | 65,151 | 8.1 KB |
| `Batch` | 380 | 52,911 | 6.6 KB |
| `BP_Greatsword_C` | 184 | 24,793 | 3.1 KB |
| `RPCs` | 191 | 21,823 | 2.7 KB |
| `Project_JGameState` | 186 | 20,460 | 2.6 KB |
| `ClientMoveResponsePacked` | 173 | 15,283 | 1.9 KB |
| `ReplicatedWorldTimeSecondsDouble` | 186 | 12,090 | 1.5 KB |

이 selection에서는 `FailedToWriteSmallObjectCount=0`,
`RemainingObjectsPendingWriteCount=0`이었다. N2는 low-population functional/
packet baseline이며, high rate/cull 값이나 custom Iris filter/prioritizer 변경의
근거가 아니다.

### N2 PIE CPU 참고값

N2 CPU trace는 dedicated-server + 두 client world가 섞인 PIE process-wide 값이다.
server-only/client-only 비용으로 해석하지 않는다.

| Scope | Count | Incl | Excl | 1회 Incl 평균 |
|---|---:|---:|---:|---:|
| `Project_J_PlayerCharacterTick` | 20,532 | 262.14 ms | 7.72 ms | 12.77 µs |
| `...MovementPolicy` | 20,532 | 176.31 ms | 1.79 ms | 8.59 µs |
| `...ApplyCombatRotationMode` | 20,532 | 166.60 ms | 7.78 ms | 8.11 µs |
| `Project_J_AnimNativeUpdate` | 13,639 | 107.76 ms | 10.68 ms | 7.90 µs |
| `...AnimBuildThreadSafeData` | 13,639 | 71.29 ms | 45.00 ms | 5.23 µs |
| `UCharacterMovementComponent_TickComponent` | 30,798 | 813.36 ms | 574.83 ms | 26.41 µs |
| `ServerMovePacked` | 3,847 | 47.07 ms | 26.24 ms | 12.24 µs |

### N50 replicated-movement baseline

**조건:** `StartProfilingReplicatedMovementCrowd 50`, dedicated-server world,
connected client 2, mover 50, 30 Hz, steady-state 19.362초 / 1,520 packets.
로그는 `Spawned=50 Moving=50 Replicated=true`를 확인했다. mover는 owner 없는
simulated proxy로 전송되며, user player input, combat, TIP, inventory mutation은
capture에서 제외됐다. 모든 mover를 always-relevant로 둔 의도적 baseline이므로,
이 값은 AOI 결과가 아니다.

| Event / object | Count | Incl (bits) | 관찰 |
|---|---:|---:|---|
| `DataStream` | 1,435 | 9,649,722 | aggregate 약 62.3 KB/s |
| `ReplicationData` | 1,435 | 9,599,497 | Iris replicated payload |
| `Batch` | 55,038 | 9,550,907 | Iris batch aggregate |
| `BP_Greatsword_C` | 54,844 | 9,368,417 | profiler mover character-class payload |
| `Location` | 108,319 | 4,365,512 | movement position |
| `ReplicatedMovement` | 54,669 | 3,227,738 | actor movement state |
| `Rotation` | 54,669 | 551,933 | movement rotation |
| `LinearVelocity` | 54,669 | 447,984 | movement velocity |
| `RPCs` | 175 | 17,500 | background RPC; action/TIP 없음 |

### N50 해석

- packet rate는 aggregate 약 **78.5 packets/s**다.
- `BP_Greatsword_C` update count는 mover 1명당 약 **56.7 updates/s**다. 50 mover,
  두 recipient, 30 Hz의 aggregate 이론치 60 updates/s에 가깝다. 따라서 두
  client에 이동 update가 실제로 전달됐다는 것을 확인한다.
- `DataStream`은 aggregate 약 **62.3 KB/s**다. 두 connection 합산 selection이라는
  전제에서 균등 분배하면 connection당 약 **31.2 KB/s**, mover 1개·connection
  1개당 약 **4.84 kbit/s**다.
- 이번 결과는 **50 server-authoritative mover -> 2 actual client** 비용이다.
  50 real client connections의 authentication, input RPC, CMC client prediction,
  packet loss 동작을 측정한 결과가 아니다.

## 7. 기능·구조 관련 확인 사항

| 항목 | 현재 확인 | 성능 수치와의 관계 |
|---|---|---|
| 일반 movement replication | 2-client PIE에서 remote presentation 정상 관찰 | N2/N50 network baseline의 전제 |
| remote TIP | server-authoritative event / simulated-proxy presentation 경로 구현 및 기본 2-client 동작 확인 | 이번 N50은 TIP을 제외해 movement bandwidth와 분리 |
| 빠른 연속 회전 TIP | yaw unwrap 기반의 방향 연속성 수정 후 build 성공 | 기능 회귀 확인이 남아 있으며, performance profile 대상은 아님 |
| visual crowd | local CMC/animation workload 자동 생성 | networking 수치와 절대 혼용 금지 |
| N50 replicated crowd | server-only spawned, 30 Hz, always relevant mover 50개 | AOI 적용 전 outbound movement 기준선 |

## 8. 현재 결정: 할 것과 하지 않을 것

### 확정된 결정

1. 현 local S70/S100 workload에서 MM/Chooser/trajectory snapshot은 P0 병목 증거가 없다.
2. `BuildThreadSafeData`를 별도 Task Graph job으로 강제 분리하지 않는다.
3. 현재 animation parallel path는 실사용 중이다. worker thread를 더 만들기 위한
   구조 변경은 하지 않는다.
4. N50 movement baseline은 기록 완료다. 현 시점에 N10/N30/N50을 같은 방식으로
   반복 capture하지 않는다.
5. N50 값만으로 NetUpdateFrequency, cull distance, dormancy, Iris filter/prioritizer를
   변경하지 않는다.

### 이후에만 측정할 항목

| Trigger | 필요한 다음 측정 |
|---|---|
| 50 실제 동시 접속이 제품 요구가 됨 | 다중 process/client-bot으로 client input·CMC·connection pressure 측정 |
| inventory/equipment mutation 구현 | Iris FastArray add/remove/equip/unequip delta correctness와 bytes |
| AOI/relevance 정책 구현 | 거리/priority/dormancy별 connection outbound bytes와 relevancy churn |
| NPC/Mass population 구현 | server CPU, memory/GC, representation promotion/demotion cost |
| 실전 전투/VFX/target hardware 확정 | CPU/GPU p95/p99, worst camera, hitch, RHI/RenderThread |
| standalone/server-capable 환경 확보 | cold process dedicated-server capture로 PIE 합산 비용 재검증 |

## 9. 관련 원본 문서

- [Local CPU/animation 세부 원본](ProjectJ_Profiling_Baseline_Results_2026-09-03.md)
- [Network/Iris 세부 원본](ProjectJ_Network_Baseline_Results_2026-09-04.md)
- [Iris/AOI scale gate](ProjectJ_Iris_AOI_Scale_Gates_2026-09-05.md)
- [전체 실행 로드맵](ProjectJ_Mmorpg_Execution_Roadmap_2026-09-03.md)
- [재현 명령 기준](ProjectJ_Baseline_Capture_Guide_2026-09-03.md)
