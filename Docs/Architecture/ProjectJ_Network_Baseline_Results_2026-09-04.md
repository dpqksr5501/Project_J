# Project_J Network Baseline Results

**Date:** 2026-09-04  
**Build:** Development Editor PIE, 2 clients, `Play As Client`  
**Purpose:** 실제 multi-world NetDriver와 Iris runtime 상태를 첫 실행 증거로 보관한다.

## N2-PIE: 2-client dedicated-server topology

`ProjectJ.DumpNetworkRuntime` 출력:

```text
DedicatedServer: Driver=IpNetDriver_0 DriverClass=IpNetDriver IrisActive=0
ReplicationModel=Legacy ReplicationDriver=None ClientConnections=2
HasServerConnection=0 Actors=86 ReplicatedActors=13

Client A: Driver=IpNetDriver_1 DriverClass=IpNetDriver IrisActive=0
ReplicationModel=Legacy ReplicationDriver=None ClientConnections=0
HasServerConnection=1 Actors=79 ReplicatedActors=10

Client B: Driver=IpNetDriver_2 DriverClass=IpNetDriver IrisActive=0
ReplicationModel=Legacy ReplicationDriver=None ClientConnections=0
HasServerConnection=1 Actors=79 ReplicatedActors=10
```

## N2-PIE Iris activation verification

**Date:** 2026-09-05  
**Build:** Development Editor PIE, 2 clients, `Play As Client`

```text
DedicatedServer: Driver=IpNetDriver_0 IrisActive=1 IrisUseReplicationCVar=1
ReplicationModel=Iris ReplicationDriver=None ClientConnections=2

Client A: Driver=IpNetDriver_1 IrisActive=1 IrisUseReplicationCVar=1
ReplicationModel=Iris ReplicationDriver=None HasServerConnection=1

Client B: Driver=IpNetDriver_2 IrisActive=1 IrisUseReplicationCVar=1
ReplicationModel=Iris ReplicationDriver=None HasServerConnection=1
```

**판정: 통과.** PIE dedicated-server world와 두 client world 모두 Iris replication을 사용한다. `ReplicationDriver=None`은 Replication Graph를 별도로 연결하지 않았다는 뜻이며 Iris 실패가 아니다. FastArray registration 오류가 재발하지 않은 이 실행을 Iris N2 기능·비용 검증의 시작점으로 사용한다.

## 확정된 사실

1. PIE의 `Play As Client`, player 수 2 설정은 실제 dedicated server world 1개와 client world 2개를 구성했다.
2. Server가 `ClientConnections=2`, 각 client가 `HasServerConnection=1`이므로 기본 연결 topology는 정상이다.
3. Server/client 모두 `IpNetDriver`이며 `IrisActive=0`, `ReplicationModel=Legacy`, `ReplicationDriver=None`이다. **현재 live replication은 Legacy replication이다.**
4. 기존에 조회한 `net.Iris.bEnableIris`, `net.Iris.bUseIrisForReplication`은 UE 5.8에서 등록되지 않은 이름이라 모두 `-1`이었다. 실제 UE 5.8 runtime switch는 `net.Iris.UseIrisReplication`이며, 이 baseline의 Legacy 결과는 잘못된 CVAR 이름 때문에 Iris가 활성화되지 않았음을 확인한다.
5. actor 수는 server 86/replicated 13, client 각각 79/replicated 10이었다. 이 숫자는 시작 map/PIE bootstrap을 포함한 N2 snapshot이며 MMO 인구 예산은 아니다.

## 현재 의사결정

- Legacy `IpNetDriver` 결과를 Iris 전환 전 기준선으로 보관한다.
- Iris plugin은 이미 enabled였고, `GameNetDriver`의 Iris 허용 설정도 존재했다. 2026-09-04에 실제 UE 5.8 CVAR인 `net.Iris.UseIrisReplication=1`로 전환했다.
- Iris 첫 PIE 기동에서 엔진이 요구한 `net.SubObjects.DefaultUseSubObjectReplicationList=1`도 함께 설정했다. 이는 Iris가 replicated actor의 subobject replication을 registered list 경유로 관리하도록 하는 전제조건이다.
- 첫 Iris PIE 실행에서 `Project_JInventoryComponent::InventoryArray`와 `Project_JEquipmentManagerComponent::EquipmentArray`가 FastArray fragment로 등록되지 않았다는 엔진 오류를 확인했다. 두 FastArray를 소유한 `Project_JCharacter` 모듈에 `SetupIrisSupport(Target)`를 적용해 UHT의 Iris FastArray fragment 생성을 활성화했다. Iris 성능 측정은 이 오류가 사라진 뒤에만 수행한다.
- 다음 PIE N2 실행은 server/client 모두 `IrisActive=1`, `ReplicationModel=Iris`를 먼저 확인한 뒤 기능과 비용을 별도 결과로 기록한다.
- Custom `UProject_JNetObjectFilter_Distance` / `UProject_JNetObjectPrioritizer_Combat`는 live Iris adapter가 아니며, 이 N2 결과에서 연결됐다고 볼 수 없다.

## N2 기능 관찰

- User 관찰: 두 client 사이의 일반 이동 presentation은 매우 잘 보이며, basic movement replication은 이번 N2 PIE에서 기능상 정상으로 보였다.
- 알려진 별도 결함: **180도 제자리 회전(TIP)은 remote client에서 보이지 않는다.** 이 항목은 기존 remote one-shot/locomotion presentation 버그로 관리하며, 이번 N2 movement baseline의 통과/실패 기준과 Iris runtime 판정에는 포함하지 않는다.
- 다음 N2 trace는 일반 이동과 이미 정상으로 확인된 replicated path의 CPU/packet 기준선을 수집하는 목적이다. remote TIP 수리는 별도 재현·수정·회귀 테스트로 다룬다.

## N2-PIE Iris packet capture (raw capture complete)

**Date:** 2026-09-05  
**Trace:** `Saved/Profiling/N2_Iris_Net.utrace`  
**Duration:** 약 32.5초 (`Trace.File` 10:52:29.460 → `Trace.Stop` 10:53:01.982)  
**Trace mode:** `ProjectJ.SetNetTraceVerbosity 1` 후 `cpu,frame,bookmark,log,net`

### Runtime facts

- server 및 두 client가 모두 `IrisActive=1`, `ReplicationModel=Iris`로 기록됐다.
- server의 replicated actor는 13개, replicated movement actor는 3개였다.
- 두 player character, 두 player state, wyvern은 현재 `NetUpdateHz=100`, `MinNetUpdateHz=2`, cull distance 15,000 uu로 기록됐다. 아직 packet/bytes 증거 전이므로 rate를 변경하지 않는다.
- FastArray snapshot은 server/client마다 동일 `PlayerId`의 equipment `StateHash`가 일치했다. inventory는 OwnerOnly 정책이며 모두 empty payload였다. 따라서 Iris FastArray **등록 및 초기 state 전달은 확인**됐지만, add/remove/equip/unequip mutation의 delta replication은 별도 functional smoke test가 필요하다.

### Capture notes

- 이전처럼 명령이 합쳐지지 않았고 `Trace.File`/`Trace.Stop`가 각각 실행되어 실제 새 trace 파일이 생성됐다.
- PIE teardown 시점의 `ClientSetViewTarget` root-object warning은 client world destruction과 같은 시각에 발생했다. 이번 steady-state capture의 runtime RPC failure로 해석하지 않는다.
- Iris가 active replication system 상태에서 loaded-module update 경고를 한 번 출력했다. polymorphic serializer를 이 시나리오에서 추가하지 않았으므로 이 capture의 packet baseline은 보관하되, standalone/source-server capture 단계에서는 cold process로 재검증한다.

### Network Insights detailed extraction

**Trace:** `Saved/Profiling/N2_Iris_NetVerbose.utrace` (199,331,383 bytes)  
**Trace mode:** `ProjectJ.SetNetTraceVerbosity 2` + `cpu,frame,bookmark,log,net`  
**Selection:** server, connection 0, outgoing, steady-state packet range.

Net Stats의 `Incl`/`Excl`은 **bits**다. 아래 값은 선택 구간의 합계이며, actor 하나의 고정 비용이나 초당 bandwidth로 해석하면 안 된다.

| Top event / object | Count | Incl (bits) | Incl (bytes, approx.) | 관찰 |
|---|---:|---:|---:|---|
| `PacketHeaderAndInfo` | 1,739 | 159,988 | 20.0 KB | 선택 구간 packet header/base cost |
| `DataStream` | 360 | 77,751 | 9.7 KB | Iris data stream aggregate |
| `ReplicationData` | 360 | 65,151 | 8.1 KB | replicated payload aggregate |
| `Batch` | 380 | 52,911 | 6.6 KB | Iris batch aggregate |
| `BP_Greatsword_C` | 184 | 24,793 | 3.1 KB | 현재 가장 큰 project actor payload |
| `RPCs` | 191 | 21,823 | 2.7 KB | RPC aggregate; action/ability 구간의 별도 비교 필요 |
| `Project_JGameState` | 186 | 20,460 | 2.6 KB | GameState payload |
| `ClientMoveResponsePacked` | 173 | 15,283 | 1.9 KB | CharacterMovement response traffic |
| `ReplicatedWorldTimeSecondsDouble` | 186 | 12,090 | 1.5 KB | world-time replication |
| `Project_JReplicatedAnimEventComponent` | 13 | 3,133 | 392 B | replicated animation event payload |
| `Project_JAbilitySystemComponent` | 7 | 2,176 | 272 B | ASC payload |

### Interpretation / decision

- **Iris detailed object tracing is working.** Verbosity 1 trace의 counters-only view와 달리, verbosity 2 capture에서 actor, RPC, replicated property/event까지 실제 bit cost가 확인됐다.
- 이 N2 범위에서는 `FailedToWriteSmallObjectCount=0`, `RemainingObjectsPendingWriteCount=0`이며 write backlog 신호는 없다.
- 가장 먼저 자세히 볼 대상은 `BP_Greatsword_C`, `RPCs`, `Project_JGameState`다. 다만 현재 2-client 단일 연결의 짧은 baseline만으로 100 Hz, cull distance, AOI 또는 prioritizer를 변경하지 않는다.
- 다음 비교는 **동일 조건의 idle vs movement vs combat action**을 각각 15~20초로 나누어 capture하고, 초당 bit/packet을 비교하는 방식으로 한다. 그 뒤 N10/N30 server pressure에서 replication frequency/AOI 결정을 내린다.

## N2-PIE CPU baseline

`N2_PIE_AllWorlds.utrace`의 Insights 선택 구간에서 확인된 값이다. PIE single process의 dedicated-server + client 2개 world가 합산된 수치이므로, 이를 server-only 또는 client-only 비용으로 해석하지 않는다.

| Scope | Count | Incl | Excl | 1회 Incl 평균 |
|---|---:|---:|---:|---:|
| `Project_J_PlayerCharacterTick` | 20,532 | 262.14 ms | 7.72 ms | 12.77 µs |
| `...PlayerCharacterTick_MovementPolicy` | 20,532 | 176.31 ms | 1.79 ms | 8.59 µs |
| `...PlayerCharacterTick_ApplyCombatRotationMode` | 20,532 | 166.60 ms | 7.78 ms | 8.11 µs |
| `Project_J_AnimNativeUpdate` | 13,639 | 107.76 ms | 10.68 ms | 7.90 µs |
| `...PlayerCharacterState` | 6,496 | 89.05 ms | 19.37 ms | 13.71 µs |
| `Project_J_AnimBuildThreadSafeData` | 13,639 | 71.29 ms | 45.00 ms | 5.23 µs |
| `...PlayerCharacterTick_LocomotionState` | 20,532 | 41.35 ms | 41.31 ms | 2.01 µs |
| `...PlayerCharacterTick_Trajectory` | 20,532 | 36.86 ms | 36.86 ms | 1.80 µs |
| `UCharacterMovementComponent_TickComponent` | 30,798 | 813.36 ms | 574.83 ms | 26.41 µs |
| `ServerMovePacked` (CMC child) | 3,847 | 47.07 ms | 26.24 ms | 12.24 µs |

`ServerReplicateActors`는 CPU Timers 검색에 나타나지 않았다. Legacy `IpNetDriver`의 replication path가 이 trace에서 별도 CPU event로 노출되지 않은 것이며, trace 실패를 뜻하지 않는다. NetDriver-level bytes/packet/object 비용은 Network Insights의 Net Stats/Packet Content로 별도 측정한다.

이번 N2에서는 Project_J native update의 thread-safe callback이 parallel evaluation 9,859회, foreground 2,204회로 기록됐다. local S100 workload보다 foreground 비율이 높지만, server/client PIE worlds가 합산된 결과이며 actor/role별 분리가 되기 전에는 root-motion 또는 특정 role의 병목으로 단정하지 않는다.

## 다음 N2 검증

1. Client A의 movement, jump/land, combat enter/exit, attack/dodge(구현된 경우)를 Client B에서 관찰한다. known remote 180° TIP 결함은 별도 항목으로 제외한다.
2. 거리 이탈/재진입 후 remote pawn의 movement 및 animation presentation을 확인한다.
3. 기능이 정상일 때에만 server/client CPU+net trace를 별도로 20초 수집한다.
4. 이 N2 correctness/비용 baseline 뒤에만 N10으로 확대한다.
