# Project_J Network Baseline Capture Guide

**Date:** 2026-09-04  
**Purpose:** local visual crowd 결과와 분리하여, 실제 client/server NetDriver·Iris·replication의 첫 기준선을 수집한다. 이 단계는 Iris filter, ReplicationGraph, rate/cull 값을 변경하지 않는다.

## 1. 이번 단계에서 확인할 것

| 확인 항목 | 성공 기준 |
|---|---|
| Dedicated Server 실행 | server world가 `NetMode=DedicatedServer`로 기록된다. |
| Client 연결 | 각 client world가 `NetMode=Client`, `HasServerConnection=1`로 기록된다. |
| Iris runtime | `IrisActive=1` 및 `ReplicationModel`이 legacy/default 여부가 아닌 실제 driver 상태로 기록된다. cvar만으로 판정하지 않는다. |
| 기본 replication | 다른 client가 이동, 점프/착지, 전투 상태와 one-shot presentation을 정상적으로 관찰한다. |
| 기본 비용 | 2-client server/client trace에서 Game Thread, `ServerReplicateActors`, CharacterMovement 및 bytes/packet stats를 보관한다. |

`ProjectJ.DumpNetworkRuntime`은 Development/Debug용 전역 console command다. PlayerController Exec이 아니므로 dedicated server의 `-ExecCmds`와 client console 모두에서 쓸 수 있다.

> **현재 개발 환경 제한:** 2026-09-04에 설치형 Epic UE 5.8에서 `Project_JServer Win64 Development`를 빌드했으나 `Server targets are not currently supported from this engine distribution.`으로 중단됐다. 새 `Project_JServer.Target.cs`는 source-built/server-enabled engine에서 사용할 준비가 됐지만, 이 환경의 첫 기준선은 PIE의 **Play As Client**로 자동 생성되는 dedicated-server world로 수집한다. standalone packaged dedicated server 기준선은 UE source build 또는 server target을 지원하는 엔진 전환 뒤에 수집한다.

## 2. 가장 먼저 할 2-client PIE 검증

에디터의 Play 설정에서 다음처럼 실행한다.

1. **Number of Players**: `2`
2. **Net Mode**: `Play As Client`
3. UE 5.8의 이 메뉴에서는 별도 `Run Dedicated Server` 체크박스가 없다. `Play As Client`가 PIE dedicated-server world와 client world를 자동 구성한다.
4. map과 graphics 설정은 동일하게 유지한다.

두 client가 접속한 뒤, 한 client의 게임 콘솔에서 다음을 한 번 실행한다.

```text
ProjectJ.DumpNetworkRuntime
```

PIE는 같은 editor process 안에 server/client world가 공존할 수 있으므로, Output Log에 `NetworkRuntime` 줄이 여러 개 나올 수 있다. 아래 형태의 server 및 client 줄을 모두 보관한다.

```text
NetworkRuntime World=... NetMode=DedicatedServer ... IrisActive=... ClientConnections=2 ...
NetworkRuntime World=... NetMode=Client ... HasServerConnection=1 ...
```

이 명령은 읽기 전용 진단이며 actor replication이나 trace를 시작하지 않는다.

## 3. Functional replication path

두 client를 서로 보이는 거리로 배치하고, A client에서 아래를 한 번씩 수행한다. B client에서 결과를 관찰한다.

1. walk/run/sprint start-stop 및 180도 회전
2. jump → land
3. combat enter/exit 및 attack/dodge가 이미 구현돼 있다면 각 1회
4. 거리 이탈 후 재진입

이 단계의 목적은 animation presentation의 완전한 시각 품질이 아니라 authority, CMC movement, replicated semantic state, one-shot event가 remote proxy에서 정상 전달되는지를 확인하는 것이다. 증상이 있으면 A/B 어느 쪽에서 보였는지와 재현 순서를 기록한다.

## 4. 2-client trace

functional path가 정상임을 확인한 뒤, server와 client를 별도 trace로 수집한다. PIE의 동일 process trace는 world가 섞일 수 있으므로, 비용 수치는 standalone/packaged Development dedicated server에서 재측정하는 것을 최종 기준으로 한다.

PIE는 dedicated server world와 client world가 **하나의 UnrealEditor process** 안에 공존할 수 있다. 따라서 PIE에서 console의 `Trace.File`을 한 번 실행하면 process-wide trace가 생성되며, server/client별 별도 파일이 아니다. N2 PIE의 첫 비용 기준선에는 아래 한 파일을 사용한다.

```text
Trace.File C:/Users/I/Documents/GitHub/Project_J/Saved/Profiling/N2_PIE_AllWorlds.utrace cpu,frame,bookmark,log,net
; 20초 동안 두 client가 위 functional path 수행
Trace.Stop
ProjectJ.DumpNetworkRuntime
```

Timing View에서는 server/client world가 공유 process에 섞일 수 있음을 전제로 actor/NetDriver caller와 `NetworkRuntime` bookmark 시각을 대조한다. standalone/packaged Development dedicated server가 준비된 뒤에는 server와 각 client process에서 별도 `N2_Server.utrace`, `N2_ClientA.utrace`를 수집해 이것을 최종 비용 기준으로 삼는다.

`net` channel을 사용할 수 없는 환경이면 `cpu,frame,bookmark,log`만으로 trace를 남기고, `stat net` / `stat netpkt` screenshot을 따로 보관한다. 이때 channel 오류 메시지도 함께 보관한다.

### Packet/bytes breakdown용 Network Insights capture

위 `Trace.File ... net`은 CPU trace에 Net channel을 추가한다. UE 5.8의 packet content/Net Stats는 NetTrace verbosity가 필요하다. Project_J에는 이를 PIE 실행 중에 켜는 Development command도 추가했다.

```text
ProjectJ.SetNetTraceVerbosity 1
Trace.File C:/Users/I/Documents/GitHub/Project_J/Saved/Profiling/N2_Iris_Net.utrace cpu,frame,bookmark,log,net
; 20~30초 동안 두 client가 functional path 수행
Trace.Stop
ProjectJ.SetNetTraceVerbosity 0
```

`1`은 packet trace, `2`/`3`은 더 상세한 content 분석용이며 trace 파일과 runtime 비용이 커질 수 있다. Network Insights에서 packet/object detail이 필요할 때만 `2` 이상을 사용한다. 별도 standalone 또는 source/server-enabled 환경에서는 기존처럼 `-trace=net -NetTrace=1 -tracehost=localhost` process launch도 사용할 수 있다.

## 5. 보내줄 자료

1. `NetworkRuntime` server 1줄 + client 1줄
2. A client 행동을 B client가 보는 짧은 화면 녹화 또는 screenshot
3. server trace와 client trace, 또는 Timing/Network Insights screenshot
4. 실행 방식(PIE/standalone/packaged), player 수, map, resolution과 net emulation 사용 여부
5. known remote 180° TIP 결함은 별도 이슈로 표시하고 일반 movement baseline과 혼합하지 않는다.
6. `ProjectJ.DumpServerReplicationPolicy 32` 출력. 이 명령은 live server의 actor별 `NetUpdateHz`, minimum rate, cull distance, movement replication과 class count를 읽기 전용으로 기록한다.

## 6. 아직 하지 않는 것

- custom Iris filter/prioritizer 등록
- ReplicationGraph 도입
- net update frequency/cull distance 일괄 변경
- 10/30/50 client 확장
- packet loss/latency emulation 기반의 CMC prediction tuning

N2 baseline과 functional correctness가 확보된 뒤에만 다음 인구 단계로 확장한다.
