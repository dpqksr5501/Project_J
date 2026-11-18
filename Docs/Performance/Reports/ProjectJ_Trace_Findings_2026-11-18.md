# Project J 성능 트레이스 검증 결과

2026-11-18 기준으로 중앙 보관소에 남은 트레이스 76개가 연결된 55개 실행을 살폈다. 이 문서는 원본을 퇴역시킨 뒤에도 검증 조건, 판정과 당시 확인한 원인을 읽기 위한 기록이다. 수치 재집계에는 아래의 CSV와 실행 조건을 함께 사용한다. 당시 타이머를 새로 검색하거나, 내보내지 않은 스레드·프레임·호출 스택을 다시 분석하려면 해당 원본 `.utrace`가 필요하다.

## 다음 변경에서 다시 비교할 기준선

| 실행 조건 | 측정 결과 | 해석과 재집계 자료 |
| --- | --- | --- |
| Spellblade 100·200개, 1280×720 D3D12 offscreen, 소켓 0, 로컬 소유자 1명 | 공격 프레임 P95 **55.26 / 102.93ms**. 이동 P95 **41.40 / 71.56ms**. 프로세스 Private peak **18.36 / 21.46 GiB**. | 같은 렌더 바이너리에서 표시 밀도를 바꿨다. 공격 표본은 각각 66·34프레임이다. 단계별 `frames.csv`, 실행 manifest, viewport 확인 결과, 프로세스 메모리로 재집계한다. [기준선 수집 절차](../README.md#2026-11-18-기반-작업-기준선) |
| 전용 Editor 서버, 실제 Spellblade 2·4접속, NullRHI, 로컬 소켓, 복제 프로브 100개 | 서버 틱 평균 **30.01 / 29.82Hz**, 프레임 시작 간격 P95 **34.63 / 34.63ms**, 전체 송신율 **37.54 / 133.06 KiB/s**, 서버 Private peak **2.83 / 2.85 GiB**. | Stage1–5의 서버 프레임 CSV와 `Server/network-phases.csv`, 연결 검증, 메모리 기록을 사용한다. 송신율에는 시험 제어 트래픽도 포함된다. [실제 캐릭터 접속 결과](../README.md#실제-캐릭터-24접속-후속) |
| 초기 실제 캐릭터 4접속 | 네 소켓 입장 뒤 Stage0 시간 초과. 임시 Pawn 네 슬롯이 같은 좌표였고, 실제 캐릭터 캡슐의 자리와 바닥 지지 검사가 없었다. | 시험 배치를 슬롯별로 분리한 뒤 기존 착지 검사와 Stage1–5를 통과했다. 초기 실패 로그와 최종 4접속 결과를 함께 읽는다. [수정과 재실행 기록](../README.md#실제-캐릭터-24접속-후속) |

두 축의 원자료를 다시 읽어 `NPCFoundationTable_20261118_Baseline.json`과 대조했다. **60개 지표가 모두 일치했다.** 이 수치는 로컬 Development Editor의 조건부 기준선이다. 표시 Avatar 수, 실제 접속 수, 복제 프로브 수는 각각 따로 기록한다. [재집계 명령](../README.md#저장된-실행-재집계와-전후-비교)

### 리팩터링 전후 비교 절차

[기준선 JSON](Data/ProjectJ_Foundation_Baseline_2026-11-18.json)에 전체 정밀도의 60개 지표, 실행 조건, 소스·바이너리 지문, 입력 파일별 SHA-256을 저장했다. `run`은 저장소 기준 상대 경로다. 비교 도구는 이전 기준선의 지표를 JSON에서 읽으며, 새 측정과 조건·부하 단계가 일치하는지 확인한다. 리팩터링 전후 실행 바이너리 지문은 서로 달라질 수 있다. 한 번의 100·200 표시 비교나 2·4접속 비교 안에서는 동일 바이너리를 사용한다.

| 변경 영역 | 다시 실행할 비교 | 함께 확인할 값 |
| --- | --- | --- |
| 렌더링·LOD·Cloth·애니메이션 | 같은 D3D12 장면에서 1280×720, 로컬 소유자 1명, Spellblade 100·200개, 소켓 0. 이동·공격·복귀를 같은 입력으로 실행한다. [렌더 실행 명령](../README.md#2026-11-18-기반-작업-기준선) | 단계별 P50·P95·P99, 공격 표본 수, Private/Working Set, 실제 viewport와 등장 수. 동시 공격은 전체 Avatar에서 몽타주가 활성인 구간을 사용한다. |
| 서버·복제·AOI | NullRHI 전용 서버, 실제 2·4접속, 복제 프로브 100개, 30Hz 제한, 지연·손실 0, 동일 Stage1–5. [접속 실행 명령](../README.md#실제-캐릭터-24접속-후속) | 단계별 실제 연결 수와 종료 코드, 서버 틱 간격 P95·P99, 전체 송신 바이트와 초당 송신율, Private/Working Set. 손실이나 지연을 주입한 비교는 해당 설정으로 새 기준을 만든다. |
| NPC 스케줄링·전투·FX | 기존 렌더 실행기로 플레이어 200개, 2560×1440, 이동 입력, 로컬 플레이어 1명, `-NPCCombatCount 32`를 맞춘다. `Canonical143_1`의 궤적·FX 조합을 재현할 때는 `-NPCAuthoredPath -MixedFXResources`를 명시하고 해당 manifest의 나머지 입력과 대조한다. | `npc-combat.csv`·`.json`에서 실제 피격·효과·자연 종료·예산 복귀·정리를 먼저 비교한다. FX의 활성·제한·정리와 전체 판정도 함께 확인한다. WQHD 혼합 실행과 720p 표시 밀도는 각각 자기 기준선에서 비교한다. |

변경하려는 조건이 해상도, 맵, 패킷 손실, NPC 수 또는 FX 예산 자체라면 전후 비교에서 그 조건만 바꿔 별도 결과로 남긴다. 기존 수치는 참고값으로 사용한다. 결과가 흔들리면 동일 조건을 교대로 반복해 편차를 확인한다. 한 실행의 P95 차이만으로 통과 한도나 전 환경의 개선율을 정하지 않는다.

**트레이스가 없는 과거 실행의 확인.** 서버 2·4접속의 `manifest.json`, `verification.json`, `Server-effective-arguments.json`, `server-baseline.json`, `Server/network-phases.csv`, Stage1–5 `ServerFrames` CSV만 임시 폴더에 복사했다. `Server.utrace`가 없는 상태에서 전 단계 수치를 재집계해 60개 지표가 모두 일치했다. CSV 한 개를 바꾸면 도구가 재집계를 거절한다. 새 실행은 실제 트레이스를 읽어 SHA-256을 남긴다.

## 트레이스에서 확인한 성능과 실패 원인

| 실행·비교 | 실제 관측과 판정 | 원본 없이 다시 볼 근거 |
| --- | --- | --- |
| 10월 14일 RA 교대 CPU | NullRHI·실제 authored Avatar 200개·소켓 0·240틱 준비/600틱 측정. 공격 중 ABA on 평균은 A/B **21.2348 → 23.2443ms**. 일부 조건의 증가 원인은 미확정이다. | `Saved/Validation/Reaudit_20261014/CPUComparisonRows.csv`, `CPUComparisonSummary.json`, `CPUAttackWindowSummary.json`. [RA 교대 비교](../../Review/Audits/Project_J_Reaudit_2026-10-14.md#200명-ra-교대-cpu-비교--2026-10-14) |
| 이후 CA CPU 교대·트레이스 | RA27 대 Final2의 장기 교대 시험에서 공격 평균은 ABA off/on **−3.25% / −2.73%**였다. 별도 짧은 추적에서는 준비 단계가 포함된 WorldTick 3,840개의 inclusive 시간이 **60.483 → 61.753초**였다. 두 시험의 표본과 계측 조건은 다르다. | [CA 비교 조건과 표](../../Review/Audits/Project_J_Continuation_Audit_2026-10-14.md#cpu렌더-조사), `R15_CA16_CPU_Trace1_A_trace`·`_B_trace`의 실행 manifest. |
| 1명·50명 CPU 군중 비교 | `Baseline1Trace`는 1명 기준이다. `Baseline50Trace`와 `After50Trace`는 50명 전후 비교다. 50명 공격 구간의 자산별 타이머는 worker의 중첩 시간을 포함한다. | [군중 최적화의 수치·조건](../Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md), 각 실행의 manifest와 로그. |
| FrameTail105, 대기와 결과 상한 | 결과 상한 32/16 비교에서 공격창 프레임 P95는 **23.499 / 21.198ms**, 결과 스냅샷 나이 P95는 **45.895 / 84.450ms**였다. 상한 32 측정의 Cloth 하위 GT 대기 P95는 10.525ms였다. 프레임과 결과 신선도의 상충이 관측됐다. | `Saved/ArchitectureBatch105FrameTail/MatchedTraceComparison.json`, `GTAnalysis.json`, `Phase5`·`ControlPhase5` 내보내기. [105차 분석](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#105차-실제-cpu-대기와-결과-상한-채택-판정) |
| Combat114, 전투 scope 도달 | WQHD·Avatar 200개·로컬 플레이어에서 725개 프레임 행과 관측 이벤트가 대응했다. 장비 표현의 Spawn·Stop은 각각 200회였다. 새 근접 판정·권위 피해 scope 10개는 이 실행에서 관측되지 않았다. 한 장비 표현 경로에서 Niagara 컴파일 요청의 inclusive 10.87ms를 관측했다. | `Combat114_Trace1`의 `frames.csv`, `manifest.json`, `verification.json`, [호출 트리와 미도달 판정](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#114차-공유-전투-노티파이와-장비-표현의-반복-작업-제거). |
| Mixed134, 입력 준비 | `_1`은 공격 보조 레이어가 400→600 소비자로 늘며 준비 검사를 실패했다. 보조 관측과 고정해야 할 입력을 분리한 `_2`는 실제 렌더 200·판단 NPC 64·대상 128에서 통과했다. 6개 DB 준비 후 frame 103에서 승인했고, 동시 공격 32프레임의 P95는 **86.561ms**였다. | `Saved/ArchitectureBatch134MixedReadiness/Verification134.json`, `AttackGTAnalysis134.json`, `Phase5_2` 및 두 실행의 원 CSV. [134차 기록](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#134차-플레이어-검색-입력-준비와-혼합-실행의-실제-비용) |
| Mixed135, Flying 계산 | worker에서 14개 스레드에 실제 6,000회가 관측됐다. 공격 P95는 worker 실행 **102.115ms**, 당시 같은 바이너리의 동기 대조 **95.596ms**였다. Flying worker는 기본값으로 채택하지 않았다. 대조 스위치는 다른 구간의 Ground 경로에도 영향을 준다. | `Saved/ArchitectureBatch135FlightForecast/ControlComparison135.json`, `TraceAnalysis135.json`, CPU 내보내기. [135차 판정](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#135차-native-flying-값-계산과-성능-채택-경계) |
| Bounds137, 공유 캐시 | 이동·공격·점프에서 원본과 재사용 bounds 2,250쌍의 최대 차는 0이었다. 공격 P95는 **106.297 → 102.861ms**였지만 다른 단계는 혼재했고, 대상 9파트 외부 구간은 **7.34 → 7.86ms/frame**이었다. 기본값은 꺼진 상태로 유지했다. | `Saved/ArchitectureBatch135FlightForecast/Bounds137*`의 대조군·후보군 export와 `Saved/ArchitectureBatch137SharedPhysicsBounds`의 proof. [137차 판정](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#137차-원본-물리-bounds-재사용과-채택-조건) |
| MixedFX142·Canonical143 | `_1`은 준비 완료 전 대기가 첫 Niagara Activate를 막았고, `_2`는 실제 메시 Z 오프셋으로 시야 검사를 실패했다. `_3`은 소유자 132개 중 활성 128·수량 제한 4, 모든 lease 정리를 확인했지만 NPC 피해 **25/32**로 통합 실패였다. 다른 명시 궤적을 사용한 `Canonical143_1`은 NPC 피해 **32/32**, FX 활성 128·제한 4, 소유자 정리를 통과했다. | 각 실행의 `result.txt`, `frames.csv`, `npc-combat.csv`; `Saved/ArchitectureBatch142MixedFX/Verification142.json`, `Saved/ArchitectureBatch143CanonicalMelee/Verification143.json`. [142·143차 기록](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#142차-혼합-이펙트-수명-근거와-전체-통합-gate-미완료) |
| 추적 종료 진단 | CPU 전용과 `StableSettingsMovement1`은 장면 성공 후 exit **777003**, GPU 전용은 exit **0**. `TraceExitDebug2`의 정확한 중단점은 `FPageAllocator` 소멸 중 `GetNumUsed() == 0` 검사였다. 남아 있던 페이지의 할당 소유자는 미확정이며 CPU 프로파일러 TLS는 후보였다. | `Saved/Validation/MMOFoundation_20261026/TraceExitDebug2`의 `AllocatorDiagnosis.json`, `ProfilerShutdownBoundary.json`, `DebugEvents.json`, `Exception00.dmp`; CPU/GPU 대조의 `Process.json`, `Arguments.json`, `Run.log`. |
| Mover 버퍼와 네트워크 | Clock/Poll 진단은 원거리 수신이 약 333ms 간격으로 느려진 구간을 확인했다. `BufferPriorityFinal`은 실제 소켓 2개·50ms/1% 손실에서 원격 수신 **14.49 / 15.16Hz**, 최종 평균 버퍼 **66.78 / 75.60ms**였다. `IndependentParity`, `NativeMover`, `NativeRetirement`는 프로세스 종료가 정상이어도 해당 기능 판정은 실패했다. | `Saved/Validation/GroupD_20260910/GASP58_*_20261020`의 `verification.json`·파일별 CSV. 최종 실행의 두 `mover-buffer-validation.json`과 [Mover 원인·수정 기록](../../Review/Audits/Project_J_Mover_Animation_Source_Reaudit_2026-10-20.md). |
| Audience·Companion·원격 입력 | `Audience1` 실패, `Audience3` 통과. `CompanionAudience1`과 `Phase9Input1`도 통과했다. 관측한 보간 소비 프레임의 전진 간격은 그 자체로 패킷 도착 간격이 아니다. | 서버·클라이언트 로그와 각 `verification.json`, `manifest.json`. [원격 검증 작업 기록](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md) |
| 장비·RHI 노티파이 | `EquipmentSoak100`은 독립 캐릭터 100개를 3회 반복해 300회 장착·해제 수명 검증을 마쳤고 실패 0이었다. `Batch69NotifyRHI1`은 실제 렌더러 환경에서 실행됐다. 장비 시험은 NullRHI였다. | `Saved/Profiling/EquipmentSoak100`의 `Phases.csv`·`Summary.txt`, `Batch69NotifyRHI1` 실행 로그와 [69차 RHI 검증](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#69차-headless-표현-소비와-실제-rhi-검증-연결). |

## NPC139 실패에서 최종 통과까지

200개 플레이어 표시, 실제 non-MM NPC 32개, 별도 피격 대상 32개를 둔 혼합 실행이다. 각 실행의 피해 적용 NPC 수, 자연 종료 수, 체력 감소 합계는 `npc-combat.json`의 개별 NPC 행을 다시 집계했다.

| 실행 | 피해 적용 NPC | 자연 종료 NPC | 체력 감소 합계 | 판정과 확인한 경계 |
| --- | ---: | ---: | ---: | --- |
| `_1` | 0 | 0 | 0 | 실패. 첫 실행에서 적용 효과의 공격·ASC 소유 경계가 맞지 않았다. |
| `_2`, `_3` | 0 | 0 | 0 | 실패. 원시 접촉 수는 각각 0회, 7회였다. |
| `_4` | 2 | 0 | 20 | 실패. 일부 피해가 적용됐지만 자연 종료까지 이어지지 않았다. |
| `_5`, `_6`, `_7` | 10 / 5 / 9 | 0 | 100 / 50 / 90 | 실패. 중간 진단 실행이다. 원본 트레이스 퇴역 후에도 로그·CSV·결과가 남아 있다. |
| `_8` | 10 | 0 | 100 | 실패. 현재 token·context와 일치한 판단이 **515.7356ms**로 **500ms** 기한을 넘었고, 판단 무효화에서 실제 GAS 공격 취소로 이어졌다. |
| `_9` | 2 | 32 | 20 | 실패. 판단 만료 수정 이후 자연 종료와 예산 복귀는 했지만 피해가 부족했다. |
| `_10` | 12 | 32 | 120 | 실패. frame 473은 **394.149ms**였고, PNG 압축 **266.150ms**와 렌더 flush **42.271ms**가 관측됐다. 다음 델타가 약 266ms 명중 창을 건너뛰었다. |
| `_11` | 18 | 32 | 180 | 실패. 비동기 PNG 저장 5회가 끝났지만 피해 조건 미달은 남았다. |
| `_12` | 32 | 32 | 320 | 통과. 원시 접촉 55, 실제 효과 32, 메시·액터 64개 정리와 PNG 5회 저장을 확인했다. |

실행별 `npc-combat.json`, `npc-combat.csv`, `frames.csv`, `result.txt` 및 manifest는 `Saved/Validation/WholeProjectAudit_20261013/MMOFoundation_20261103_NPC139_<번호>`에 있다. 판단 만료 호출 경로는 `Saved/ArchitectureBatch139NPCPopulation/Progress139.json`, 중간 피해 미달 판정은 `Verification139Interim.json`, 최종 판정은 `Verification139.json`에 남아 있다. [139차 변경과 한계](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#139차-실제-비모션매칭-npc-혼합-실행과-미완료-판정-경계)

## 원본 퇴역 이후 확인할 수 있는 범위

- **수치 비교와 합격·실패 판정:** 표와 연결된 CSV·JSON·로그로 가능하다. 현재 기준선, 실패 증거, 실행 조건 및 원래 소스·바이너리 지문은 트레이스와 함께 삭제하지 않는다.
- **기존 분석 재검토:** 105·134·135·137차처럼 분석에 사용한 프레임과 타이머를 이미 CSV로 내보낸 경우에는 그 구간을 다시 계산할 수 있다. 겹치는 inclusive 시간과 대기 시간을 실제 CPU 실행 시간에 더하지 않는다. 측정 조건이 다른 실행 간 수치도 그대로 합치지 않는다.
- **새 질문으로 과거 원본 재분석:** 선택되지 않은 프레임·새 타이머·전체 호출 트리·원래 패킷 순서는 표와 기존 export에서 복원할 수 없다. 특히 트레이스 종료 시 미해결 할당 소유자나 과거 CPU 회귀를 이어서 분석할 때는 새 캡처가 필요할 수 있다.
- 보관 경로 기준으로 삭제 대상 원본, SHA-256, 원래 경로와 복원 여부는 기존 중앙 명세에서 확인한다. 초기 분류에서 46개의 보관 SHA-256을 대조했고 `NPC139_8`은 gzip 해제 스트림도 원본 SHA-256과 일치했다. 이 문서 작성에서는 새 엔진 실행이나 모든 트레이스의 Insights 재분석을 수행하지 않았다. [중앙 보관 명세](../../Maintenance/Local_Archive_Policy.md)

## 원본 퇴역과 검증 캡처 정리 결과

2026-11-18에 이 문서의 범위인 트레이스 76개와 동일 파일을 가리키는 링크를 퇴역했다. 실행 로그·CSV·결과 및 비교 자료 619개는 전후 SHA-256이 일치했고, 삭제 뒤 실제로 남은 CSV와 명세만으로 기준선 60개 지표를 다시 검증했다. C: 여유 공간은 **20.59 GiB** 늘었다. 뒤이어 오래된 로그 1,563개는 원문 SHA-256을 검증한 gzip으로 보존하고 동일 DLL 150개와 폰트 12개는 기존 경로를 유지하며 통합했다. 로그·DLL·폰트 표본을 실제로 복원했고, 별도 정리에서 **약 716 MiB**가 늘었다. 실패 로그로 확인한 363개는 원래 경로에 남겼다.

| 중간 캡처 실행 | 정리한 PNG | 남긴 대표 자료 |
| --- | ---: | --- |
| `StrafeReverseTurnPreview` | 144 | 네 클립마다 원본 5장, 프레임 manifest, 전체 41프레임 GIF 4개와 프레임 모아보기. 최종 `StrafeReverseTurn_20261011/Rendered` 캡처 전체. |
| `StrafeBackwardBalancedPreview` | 144 | 네 클립마다 원본 5장, 프레임 manifest, 전체 41프레임 GIF 4개와 프레임 모아보기. |
| `UnifiedLocomotion_20261012/RenderedBaseline2` | 32 | 4개 시나리오별 초기 화면과 두 복귀 구간의 시작·중간·안정 화면 7장씩. 재생·방향 기록과 최종 `SelectiveRendered` 및 실패한 `RenderedBaseline`의 캡처 전체를 남겼다. |
| `Continuation_20261014/R20_JumpLandRendered16` | 12 | 착지 복귀 frame 175와 앞뒤·안정 구간을 포함한 10장, `Playback.txt`, 성공 판정. 같은 case 16을 통과한 `R20_FinalCase16`의 22장 전체. |

중간 PNG 332장을 제거해 C: 여유 공간이 **약 217 MiB** 늘었다. 대표·비교 근거 99개는 SHA-256이 일치했고, 정리 범위 밖 PNG와 트레이스 2,733개는 크기와 수정 시각이 일치했다. 착지 시험에서 실패가 확인된 `AnimationRefactor_20261013/Rendered`와 최종 착지 캡처들은 유지했다. 초기 후진 Reface On/Off 쌍도 유지했다. Off 캡처는 `.png` 확장자와 실제 파일 형식이 달랐고 두 쪽의 프레임을 함께 비교할 수 있도록 둘 다 남겼다. 퇴역한 미리보기 프레임의 파일명과 SHA-256은 `Saved/StorageCleanup_20261104/IntermediateValidationPNGRetirement_20261118.jsonl`에 있다. 해당 프레임의 원본 화질이 필요하면 다시 캡처한다.
