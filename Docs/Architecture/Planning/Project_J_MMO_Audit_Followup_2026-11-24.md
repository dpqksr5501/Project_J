# 독립 기술 감사 후속 현황과 200인 필드 보스 검증

2026-11-24 기준. 첨부 `점검.md`를 바탕으로 코드와 에셋 연결을 감사하고 외부 자료를 조사했다. 이후 서버 권위·영속 저장·실행 수명의 결함을 수정하고, 저장된 테스트 맵과 실제 소켓에서 경로를 검증했다. 전투 인원을 올리는 과정에서 클라이언트 의상 비용, 생성기의 메모리 사용량, 서버 복제·틱 비용, 타격 시각 검증을 각각 조사하게 됐다. [초기 발견과 단계별 수정 기록](Project_J_MMO_Independent_Audit_2026-11-23.md)

## 다시 검증하기 전에

다음 표의 기준은 완료한 로컬 실행이다. 새 검증은 변경한 호출·에셋·역할의 경로에 맞춰 고른다.

| 확인할 계약 | 현재 능력과 기존 근거 | 다시 볼 관측값 |
| --- | --- | --- |
| 공통 전투와 전용 서버 애니메이션 | 확정 피해, 사망·진영·투사체·이베이드, Mover 조준, 베이크와 라이브 소켓의 독립 회귀 | 공격 활성화·스윕·이벤트·피해·체력 복제·종료. 베이크 A/B는 실제 데이터 참조와 `AuthoredMeleePath` 호출, 본 평가·타격 일치·정책 복원을 확인 |
| 서버 처리·채널 용량 | 설치형 Iris의 유효 슬롯 127, Generic 합성 200접속, 이전 Iris 필드 40인과 던전 24인 완주. 야간 필드 재검증은 32인까지 완주, 40인 타격 후 RAM 보호 중단 | 복제 방식·공격 데이터·맵·입력·표적을 고정. 처리 시간과 틱 시작 간격, 송신량, 역할별 메모리와 준비·종료를 따로 수집 |
| 던전·온라인 저장 | 별도 D3D12 PIE에서 실제 입력 처치. 로컬 Nakama·PostgreSQL과 두 서버에서는 테스트 처치 신호 뒤 클리어·우편·복귀. Migration 010은 PGlite에서 실패·재시도 경로 검증 | 입장 티켓과 참가 기록, 커밋과 보상 영수증, 복귀 앵커, 재시작 후 nonce, 가방 revision. Standalone 보상은 별도 미해결 |
| 그래픽·VFX·의상 | HUD 설정의 실제 CVar 적용, 저장 보스 경고 생성·해제, 소유자 원본과 원격 경량 망토의 기본 주기 | 화면 인원·해상도·프리셋·프레임 p95·Cloth 대기, 소유 전환과 관통. 경고는 프로젝트의 장식 FX 제한에 대한 보호와 실제 화면 가독성을 함께 확인 |
| 탈것·월드 | Starter의 실제 비행, 급강하·부스트 스태미나, 미로드 지형 상공의 세부 그리드 회귀, MainWorld 단일 경로 | 탑승·하차와 입력 복원, 셀·지면·캡슐 준비, 경로별 HLOD·PSO·식생 전환. 전역 무팝인은 아직 미검증 |

원자료는 아래 실행 링크와 기존 [중앙 보관 규칙](../../Maintenance/Local_Archive_Policy.md)으로 찾는다. 새 실행은 실제 빌드·에셋 판본과 실행 조건을 기록한다. 부하 확장은 직전 단계 통과와 메모리 보호선 확인 후 진행한다. 이번 후속 검증에서 제외한 유료 호스트, 실제 콘텐츠 제작, Cook·패키징, 24시간 소크는 이전 Development 패키지 이력과 구분한다.

## 점검에서 이어진 작업

| 영역 | 반영과 확인 범위 | 남은 검증 |
| --- | --- | --- |
| 온라인 권위와 영속 저장 | 일반 로그인, 우편 수령과 가방, 장착·해제, 제작·채집과 퀘스트 진척을 해당 PostgreSQL 명령에 연결했다. 로컬 Nakama·PostgreSQL에서 실제 클라이언트 수령과 중복 요청, 가방 ID·수량을 확인했다. [온라인 저장](Project_J_MMO_Independent_Audit_2026-11-23.md#온라인-로그인가방과-채널의-postgresql-권위) · [장비·제작](Project_J_MMO_Independent_Audit_2026-11-23.md#신규-테스트-에셋-없이-적용한-온라인-던전장비제작) | 실서비스의 제공자·운영 부하와 감사 시나리오에 포함된 경제·소셜 경합 전반은 각 경로의 증거가 필요하다. |
| 던전과 채널 | 실제 Unreal 서버 두 개로 목적지 입장·유예 내 재접속·채널 복귀를 확인했다. 로컬 백엔드 재시작 뒤 복귀 nonce 재사용을 거절했다. 저장된 맵에서 서버가 테스트 보스 체력을 0으로 만든 뒤 클리어 우편·목표·저장 및 원래 위치 복귀까지 확인했다. [재시작·채널](Project_J_MMO_Independent_Audit_2026-11-23.md#로컬-백엔드-재시작과-다른-채널-복귀) · [저장 맵 검증](Project_J_MMO_Independent_Audit_2026-11-23.md#저장된-mmo-샌드박스와-던전-점검) | 실제 플레이어가 내는 연속 피해로 보스를 처치하는 단계부터 보상·복귀까지 이어지는 대규모 통합 실행은 남아 있다. |
| 전투·병렬 애니메이션·수명 | 사망·부활 보호, 어그로와 탑승 정리, 손 IK 워커의 프록시 스냅샷, 오디오 슬롯 회수를 보완했다. GAS 종료의 서버 세션 재검사는 배치와 비배치 양쪽에서 시행한다. [전투·애니메이션 후속](Project_J_MMO_Independent_Audit_2026-11-23.md#전투애니메이션탈것월드-후속-변경) · [GAS 종료 코드](../../../Source/Project_JGAS/Private/Project_JAbilitySystemComponent.cpp#L504) | 일반 전투의 관계 정책, 서버·클라이언트 수명 전환, 장시간 잔류 상태는 각 시나리오로 판단한다. 24시간 소크는 이번 검증 범위에서 제외했다. |
| 탈것·고속 스트리밍 | Mover의 이륙·착륙 입력과 탑승 수명, 스트리밍 소스의 상대 고도·급선회 대응을 보완했다. 저장된 샌드박스에서 실제 탑승과 비행을 검증했다. [비행과 저장 맵 검증](Project_J_MMO_Independent_Audit_2026-11-23.md#저장된-mmo-샌드박스와-던전-점검) | MainWorld의 고속 비행 전 구간에서 셀·HLOD·PSO 준비, 팝인과 접근 중 성능은 별도 실측이 필요하다. |
| 그래픽·VFX·보조 움직임 | 그래픽 UI의 런타임 프리셋 전환과 수면·실내 차이를 확인했다. 원격 코스메틱 VFX 배율 경로를 연결했다. 스펠블레이드는 머리카락·망토의 충돌 에셋을 다듬고, 원격 망토를 기본 갱신 주기의 경량 메시로 연결했다. [그래픽 프리셋](../../Performance/README.md#그래픽-품질-프리셋-실측--2026-11-24) · [충돌 최적화](../../Performance/Reports/ProjectJ_Spellblade_Secondary_Motion_2026-11-24.md) · [경량 망토](../../Performance/README.md#스펠블레이드-원격-경량-망토-적용--2026-11-24) | 200 실접속 렌더링, 전투 VFX 가시성과 경고 전달, 다른 캐릭터 의상의 시각 품질은 해당 장면에서 검증한다. |

이 범위의 적용은 기존 8모듈 의존성, 서버 권위와 클라이언트 예측, NPC 비모션매칭 정책을 따른다. 필드 보스·던전은 공통 전투·복제·표현 경로를 재현하는 장면이다. 후속 부하 검증은 로컬에서 수행하며, 유료 호스트, 실제 콘텐츠 제작, Cook·패키징과 24시간 소크는 현재 검증 범위에서 제외한다.

## 200인에서 확인한 사실

- **접속과 전투는 별도 검증이다.** 설치형 UE 5.8의 현행 Iris 드라이버는 유효 슬롯 127을 보고했다. 채널 요청 정원과 드라이버가 허용하는 정원은 따로 기록한다. Generic에서 200 소켓과 단순 피해 부하는 통과했다. 앞선 단순 피해 부하는 플레이어 Pawn 없이 GAS 대상에 즉시 피해를 준 시험이다. [채널 정원과 Iris](Project_J_MMO_Independent_Audit_2026-11-23.md#채널별-운영-정원과-로컬-제작-보스-기준선) · [200접속 단순 부하](../../../Saved/Validation/Continuation_20261014/CombatCohort200Final_20261123/verification.json)
- **제작 보스를 사용한 이전 200인 시험은 실패했고, 최신 장비·조준 경로의 200인 재실행은 남아 있다.** 이전 실행에서 참가자 200명이 접속하고 공격 활성화 200건을 기록했으며 확정 피해는 0건이었다. 최장 월드 틱은 3.34초였다. 생성기는 서버가 결과를 기록한 뒤 종료 처리 중 Windows 커밋 한도에 도달했다. 참가자 간 복제를 제한한 후속 진단에서는 메모리 오류 없이 종료했지만 여전히 피해 0건이었다. 공격 단계 월드 틱 p95는 148.86ms였고, 기록된 거절 사유에는 공격 시작 시각 검증 47건과 미래 시각 24건이 포함됐다. [최초 실패](../../../Saved/Validation/Continuation_20261014/AuthoredBossGeneric200Final_20261123/verification.json) · [메모리 로그](../../../Saved/Validation/Continuation_20261014/AuthoredBossGeneric200Final_20261123/Clients.log#L103299) · [후속 진단](Project_J_MMO_Independent_Audit_2026-11-23.md#채널별-운영-정원과-로컬-제작-보스-기준선)
- **이후 러너 수정은 그보다 작은 실제 전투에서 검증했다.** 구형 테스트 장비의 스윕이 보스에 닿지 않았고, 실제 장비로 바꾼 뒤에는 조준 입력이 빠진 문제가 드러났다. 러너를 실제 기본 장비와 소유 클라이언트의 조준 입력에 연결한 뒤 필드 40명 전원 타격을 확인했다. 그 실행의 공격 구간 월드 틱 p95는 36.44ms로 30Hz 예산 33.33ms를 넘었다. 별도 36인 추적의 느린 틱 하나에서 액터 복제 25.31ms와 틱 그룹 대기 31.52ms가 관측됐다. 대기 내부 원인과 최신 경로의 200인 결과는 미확인이다. [장비·조준 수정과 40인 비교](../../Performance/README.md#스윕이벤트-분리와-테스트-장비-수정) · [36인 서버 추적](../../Performance/README.md#군중-클라이언트와-전투-서버-cpu-추적--2026-11-24)
- **표현 최적화의 성과는 클라이언트에서 확인했다.** 스펠블레이드 소유자는 원본 망토를 사용한다. 원격 망토는 렌더 형상과 몸체 충돌을 유지하며 물리 정점을 2,534→678개로 줄였다. 16·20·24인 실접속 필드·던전에서 원격 망토, 타격 및 정상 종료를 확인했다. 24인 던전은 기능 검증 통과, 서버 종합 틱 기준 미달이며 가용 물리 메모리 최저는 5,065MiB였다. 별도 200체 화면 렌더 시험의 실제 소켓은 0개다. [실접속 망토 결과](../../Performance/README.md#스펠블레이드-원격-경량-망토-적용--2026-11-24) · [200체 화면 렌더 시험](../../Performance/README.md#스펠블레이드-망토-계산-주기와-물리-메시-비교--2026-11-24)

## 다음 측정에서 분리할 원인

아래 표는 앞선 Generic 제작 보스 실패를 진단하기 위해 세운 비교다. 이후 기본 복제인 Iris로 야간 재검증을 수행했으며, 현재 비교 순서와 인원은 [수용량 추정과 분리 호스트 검증](#수용량-추정과-분리-호스트-검증)을 따른다. 시작·실행·종료에서 역할별 Private Bytes, 시스템 가용 물리 메모리, 커밋 여유, 서버 틱과 각 클라이언트의 전투 결과를 함께 기록한다. [현재 러너](../../../Scripts/Validation/Run-LoopbackCohort.ps1) · [Windows 커밋·물리 메모리 지표](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-performance_information)

| 비교 | 판정에 사용할 증거 |
| --- | --- |
| 같은 PC에서 서버와 생성기의 스케줄링 관측 | 서버 스레드의 실행 시간, 실행 가능 상태에서 CPU를 기다린 시간, 엔진 작업 의존성에서 대기한 시간을 대조한다. 틱마다 월드 처리 시간과 시작 간격을 따로 보고, `ServerReplicateActors`와 느린 틱의 원인을 연결한다. 패킷 추적은 대표 구간의 연결당 RPC·프로퍼티와 송신량을 확인하는 데 사용한다. [Unreal Context Switches](https://dev.epicgames.com/documentation/unreal-engine/context-switches-in-unreal-engine-5) · [WPA CPU 분석](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis) · [Networking Insights](https://dev.epicgames.com/documentation/unreal-engine/networking-insights-in-unreal-engine) |
| 같은 PC에서 생성기 CPU 배정 변경 | 서버 CPU 배정을 고정하고 생성기의 중첩 배정과 분리 배정을 비교한다. 같은 입력 수와 공격 발행 시각 분포를 확인한다. RAM·캐시·메모리 대역폭은 계속 공유하므로 CPU 경합 증거와 메모리 여유를 함께 읽는다. [Windows CPU Sets](https://learn.microsoft.com/en-us/windows/win32/procthread/cpu-sets) |
| 여분 PC에서 생성기만 사설 LAN으로 분리 | 서버 PC를 고정하고 동일 부하를 반복해 하드웨어 경합 제거 후에도 느린 틱과 타격 거절이 남는지 확인한다. 현재 테스트의 접속 경계는 루프백이며, 이 비교에는 명시적으로 허용한 테스트 전용 LAN 경로가 필요하다. 유료 서버나 클라우드 없이 가능한 선택지다. [루프백 가드](../../../Source/Project_J/Testing/Project_JLoopbackCohort.cpp#L136) · [Epic 멀티플레이 시험](https://dev.epicgames.com/documentation/unreal-engine/testing-and-debugging-networked-games-in-unreal-engine) |

유사한 서버 틱에서 시간 거절이 반복되면 클라이언트가 보고한 공격 시작·타격 시각, 서버의 공격 시작·수신·환산 시각, 거절 사유를 같은 예측 키와 공격 방문 번호로 대조한다. 계측은 기존 서버 권위·시간 허용 범위·타격 기하 검증을 유지한다. 생성기를 더 가볍게 만들 경우 먼저 기존 클라이언트와 이동·조준·어빌리티 예측·서버 확정 피해·체력 복제의 결과가 일치하는지 소규모에서 확인한다. [타격 시간 검증](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp#L771) · [Supercell 전용 부하 클라이언트 사례](https://d1.awsstatic.com/onedam/marketing-channels/website/aws/en_US/events/approved/reinvent-2025/reinvent/2024/slides/gam/GAM309_How-Supercells-newest-game-launched-with-tens-of-millions-of-players.pdf)

로컬 200인 기능 통과는 같은 보스의 실제 접속·조준·공격·서버 확정 피해·체력 복제·종료로 판정한다. 성능 판정에는 30Hz 월드 처리 예산과 실제 틱 간격 p95·p99, 메모리 여유를 별도로 남긴다. 단발 로컬 부하와 지속 공격·라이브 채널 수용량은 실행 조건을 함께 기록한다.

## Phase 1~3 후속 회귀와 Iris 실측

선행 Phase 1~3은 헤드리스 서버 기본 애니메이션, 소유자 복제, 전투·진영·PvE·부활·투사체, 장비와 온라인 저장, 경고 VFX·Cloth·오디오·UI, 탈것·스트리밍·에셋 검증기를 수정했다. 기존 흩어진 사용자 변경도 같은 작업 트리에 있다. 선행 40인 Iris 필드와 24인 Iris 던전의 `worldTickBudgetMet`은 모두 통과했다. 40인 공격 p99는 61.93ms, 33.33ms 초과는 6틱이었다. 24인 던전의 대기 p95는 4.16ms, 공격 중 33.33ms 초과는 0틱이었다. [선행 99건 회귀](../../../Saved/Validation/Continuation_20261014/AuditFixFinal_20261124/index.json) · [40인 필드](../../../Saved/Validation/Continuation_20261014/AuditFixField40_Iris_20261124/verification.json) · [24인 던전](../../../Saved/Validation/Continuation_20261014/AuditFixDungeon24_Iris_20261124/verification.json)

- **신규 계약 보호.** PvE의 플레이어·같은 인카운터 NPC 판정과 무소속 NPC 예외, 사망 공격자·GAS 태그·Mover 입력, 보스 이베이드 체력 복구, 아군 뒤 벽 및 적 판정, 원격 Mover 기수와 미확정 소유권의 Cloth 경계, 급강하 부스트 비용, 높은 미로드 지형 위의 디테일 그리드를 검증했다. 실제 D3D12에서는 활성 경고의 원격 스폰 예산·불투명도 면제를 확인했다. [집중 회귀 102/102](../../../Saved/Validation/Continuation_20261014/AuditFollowupContractsFinal_20261124/index.json) · [D3D12 경고 회귀 1/1](../../../Saved/Validation/Continuation_20261014/AuditFollowupWarningRHI_20261124/index.json)
- **재현에서 확인한 수정.** 투사체가 아군 뒤의 같은 스윕 구간을 다시 검사해 벽이나 적을 놓치지 않게 했다. 겹친 아군이 많을 때는 루프를 제한하고 다음 틱에서 나머지 구간을 처리한다. 경고의 예산 면제는 즉시 스폰과 비동기 에셋 로드 완료가 같은 판정 함수를 사용한다. [투사체 경로](../../../Source/Project_JCharacter/Private/Combat/Project_JProjectileManagerSubsystem.cpp) · [경고 스폰과 불투명도](../../../Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp)

### 필드 실접속과 Burst 스파이크

각 실행은 30Hz Iris, `-ThinClient`, 서버 마스크 255와 생성기 마스크 65280, 물리·커밋 4,096MiB 보호선을 사용했다. 러너는 시작 후 적용한 affinity 값을 읽어 검증 결과에 포함한다.

| 실행 | 접속·타격 | 월드 처리 p95 / p99 | 33.33ms 초과 | 합산 서버 기준 | 최소 가용 물리 |
| --- | ---: | ---: | ---: | --- | ---: |
| 40인, 생성기 둘, 트레이스 | 34에서 보호 중단 | — | — | 미측정 | 3,799MiB |
| 40인, 생성기 하나, 트레이스 | 40/40 | 27.25 / 45.15ms | 4틱 | 통과 | 4,966MiB |
| 24인, D3D12 관측 클라이언트 하나 | 24/24 | 23.37 / 33.65ms | 2틱 | 통과 | 4,814MiB |

24인 렌더 실행은 원격 경량 망토 23개와 소유 캐릭터 원본을 확인했다. 공격 중앙 FPS 75.82, 프레임 p95 24.20ms, 화면 인원 중앙 24명이었다. 기존 Generic 필드 24인 렌더의 중앙 95.86FPS와 비교해 낮았으며, 복제 방식·실행 구성·빌드가 달라 프레임 차이의 원인은 분리되지 않았다. [두 생성기 중단](../../../Saved/Validation/Continuation_20261014/AuditFollowupIrisField40Trace_20261124/verification.json) · [40인 결과](../../../Saved/Validation/Continuation_20261014/AuditFollowupIrisField40SingleTrace_20261124/verification.json) · [24인 렌더 결과](../../../Saved/Validation/Continuation_20261014/AuditFollowupIrisField24Rendered_20261124/verification.json)

40인에서 느린 틱 4개는 `ProjectJ_CohortMeasuredTickStart/End`를 사용해 각 CSV 행과 대조했다. 네 틱의 `ReleaseTickGroup Block`은 28.03~40.71ms, `NetDriver TickFlush`는 4.22~5.77ms였다. 최장 49.29ms 틱에서는 대기 구간 안의 `STAT_USkeletalMeshComponent_RefreshBoneTransforms_GamethreadEval` 40회가 합계 37.50ms였다. 같은 구간의 Iris 업데이트는 2.65ms이며 NetDriver 범위에 포함된다. 네 틱에서 새로 확정된 타격은 0건이었다. 이전 p99와는 생성기 수와 계측 조건이 달라 개선 폭을 판정할 수 없다. [느린 틱 이벤트 요약](../../../Saved/Validation/Continuation_20261014/AuditFollowupIrisField40SingleTrace_20261124/BurstSlowTicks.json) · [Insights CSV 내보내기](../../../Scripts/Validation/Export-DTrace.ps1)

서버 유휴 정책과 별도로 공격 시작은 `GameplayPose`를 요구하고, 현행 근접 검증은 본 갱신 완료에서 소켓·몽타주 위치·루트 샘플을 수집한다. 이 경로를 유지했다. 다음 최적화에서 서버 공격 포즈를 줄이려면 실제 공격 정의별로 소켓 기반과 베이크된 궤적의 소비자를 확인하고, 같은 명중·노티파이·루트 모션을 검증할 수 있는 경로부터 비교해야 한다. 동기 본 평가의 엔진 분기를 일으킨 조건도 추가 분리 대상이다. [공격 포즈 요청과 샘플 소비](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp#L240)

80·100·120인은 시작을 보류했다. 동일 두 생성기는 34접속에서 보호선에 닿았고, 40인 단일 생성기도 최저 여유가 4,966MiB였다. 로컬 단발 전투에서 관측한 서버 종합 기준 통과는 40인까지다. 80인 이상과 운영 정원은 별도 생성기 호스트 또는 검증된 경량 생성기와 반복 부하에서 확인한다.

### Migration 010과 저장 테스트 맵

- **DB.** PGlite 0.3.10의 새 저장소에 001~010을 적용했다. 최초 적용을 실패시킨 기존 함수 인자명과 우편 스키마 불일치를 수정했다. 기존 우편 수령의 서버 가방 용량, 세션, 응답과 원장 계정을 유지하고 던전 골드에 시스템 발행 출처를 추가했다. 보상 정의 버전 저장·재실행·키 충돌, 우편 수령 재시도·오래된 세션·잔액 보존, 미완료 퀘스트 거부와 완료 수령 재실행, 만료 데이터 정리와 재실행을 실제 SQL로 통과했다. 이 실행은 단일 로컬 PGlite의 범위다. [마이그레이션](../../../Infra/migrations/010_online_audit_fixes.sql) · [회귀 스크립트](../../../Infra/scripts/check-mmo-commands.mjs)
- **보스와 검증기.** 저장본에서 두 테스트 인카운터가 같은 `BP_FieldBoss_Test`의 머리·몸 MultiPart 두 개를 사용하는 것을 확인했다. NPC BP·AnimBP와 두 탈것 후보, 보스·샌드박스·던전 등 선택 13개는 모두 Valid였고 Data Validation 오류·경고는 각각 0건이었다. 두 배치 인카운터의 `CombatDefinition`과 보스의 AnimClass가 비어 있었다. 이 상태는 피해를 받는 테스트 보스 용도로 유지했다. 실제 보스 공격의 경고 VFX까지 연결하려면 해당 장면용 전투 정의, 호환 AnimBP와 경고 타이밍이 있는 공격 몽타주가 필요하다. 경고의 수신부는 별도 D3D12 회귀에서 확인했다. [저장본 에셋 검사](../../../Saved/Validation/Continuation_20261014/AuditFollowupAssets_20261124/verification.json)

설치 엔진 직접 Editor 증분 빌드와 8모듈 DAG 검사를 통과했다. 작업 원자료는 기존 중앙 보관소에서 당시 실행 이름으로 찾을 수 있다.

## 전용 서버 병렬 평가·MontageTiming 및 24인 Iris/Generic A/B (2026-11-24 후속)

1. **원인 분리와 코드 반영.**
   - UE 5.8 전용 서버(`-server`)는 `-useperfthreads`가 없으면 `FApp::ShouldUseThreadingForPerformance()`가 `false`를 반환해 `RefreshBoneTransforms`가 게임 스레드 직렬(`GamethreadEval`)로 떨어진다. [Run-LoopbackCohort.ps1](../../../Scripts/Validation/Run-LoopbackCohort.ps1)에 `-UsePerfThreads`와 `-RenderedAffinityMask`를 추가했다.
   - 40인 러너의 기본 공격이 라이브 소켓 스윕을 사용함을 당시 트레이스(`ProjectJ_Combat_AuthoredMeleePath=0`, `ProjectJ_Combat_MeleeNotify=223`)에서 확인했다. 후속 에셋 검사에서 실제 장비 참조는 `DA_Spellblade_Equip → DA_CombatStyle_SpellbladeKit → DA_Combo_SpellbladeKit`로 확인했다. 레거시 `DA_Attack_Spellblade_LMB1`과 실제 키트의 `Light01`을 별도로 평가했다.
   - 향후 베이크된 공격(`MeleeTrajectory.bEnabled=true`) 및 전용 서버 일반 몽타주를 위해 `EProject_JAnimationUpdateRequirement::MontageTiming`과 `UProject_JBudgetedSkeletalMeshComponent::OnPostTickPose`, `UProject_JCombatHitValidationComponent::CanUseDedicatedServerMontageOnlyPose()`를 추가해 전용 서버에서 본 평가 없이 Mover 루트 앵커·몽타주 시각만으로 `TickAuthoredPath` 검증이 가능하도록 분리했다. [Project_JBudgetedSkeletalMeshComponent.cpp](../../../Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp) · [Project_JCombatHitValidationComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp)
   - 전용 서버에서 `PostProcessAnimBlueprint`, `CopyPose` 모듈러 메시 틱, 그리고 `VisualBodyMesh` 리타깃 팔로워(`bEnableFollowerRetarget`·Hand/Foot/Retarget IK)를 비활성화했다. [Project_JPlayerCharacter.cpp](../../../Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp) · [Project_JNPCCharacter.cpp](../../../Source/Project_JCharacter/Private/Project_JNPCCharacter.cpp) · [Project_JModularMeshComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JModularMeshComponent.cpp) · [Project_JCharacterAnimInstance.Budget.cpp](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Budget.cpp)

2. **40인 Iris 필드 보스 Burst 스파이크 제거 검증.**
   - `-UsePerfThreads` 적용 40인 실행([DSZeroBoneEvalField40_Iris_20261124](../../../Saved/Validation/Continuation_20261014/DSZeroBoneEvalField40_Iris_20261124/verification.json), [AllTimers.csv](../../../Saved/Validation/Continuation_20261014/DSZeroBoneEvalField40_Iris_20261124/TraceExport/AllTimers.csv)): 40/40 확정 타격, `worldTickBudgetMet: true`, **측정 구간 `GamethreadEval=0`**, `SetupParallel=960`(총 0.94ms), Burst `worldTickMs` p95 **12.59ms** / p99 **18.50ms** / p100 **18.95ms** (`over33ms: 0`), `strikeTickMs` p100 **10.57ms**.
   - `-UsePerfThreads` 미적용 30.0Hz 대조군([DSZeroBoneEvalV2_Field40_Iris30Hz_20261124](../../../Saved/Validation/Continuation_20261014/DSZeroBoneEvalV2_Field40_Iris30Hz_20261124/verification.json)): Idle p50 **6.65ms** / p95 **7.62ms**, `strikeTickMs` p100 **14.78ms**, 비베이크 공격의 직렬 본 평가로 Burst `over33ms` 3틱(p100 47.52ms).

3. **24인 렌더 클라이언트 Iris vs Generic 동일 조건 A/B.**
   - 3-way 코어 격리(`Server=15`, `Clients=240`, `RenderedClient=65280`, `-ThinClient -UsePerfThreads -VerifyRemoteCloak`) 비교:
     - **Iris** ([AB_Field24_Iris_20261124](../../../Saved/Validation/Continuation_20261014/AB_Field24_Iris_20261124/verification.json)): 렌더 Before **84.07 FPS** / Strike **77.44 FPS** (`gpuMs` 2.48/2.54ms), 서버 Burst p95 **10.33ms** (`over33ms: 0`), 송신 **1,651 KiB/s**.
     - **Generic** ([AB_Field24_Generic_20261124](../../../Saved/Validation/Continuation_20261014/AB_Field24_Generic_20261124/verification.json)): 렌더 Before **78.47 FPS** / Strike **80.93 FPS** (`gpuMs` 2.46/2.48ms), 서버 Burst p95 **15.98ms** (`over33ms: 0`), 송신 **2,308 KiB/s**.
   - 이 실행에서 Iris의 Burst 월드 처리 p95는 약 35%, 송신량은 약 29% 낮았다. Burst 틱 시작 간격 p95는 Iris 48.03ms, Generic 34.08ms였다. 관측 클라이언트 FPS는 공격 전과 공격 중의 순위가 달랐다. 월드 처리 시간은 프로세스 CPU 사용률과 다른 지표이며, 각 구간과 반복 수를 함께 읽는다.
   - 최종 검증: `Validate-MMOArchitecture.py` `passed: true`, 직접 UBT `Project_JEditor` 빌드 오류·경고 0건, 최종 집중 회귀 99/99 통과([DSZeroBoneEvalFinalReg_20261124](../../../Saved/Validation/Continuation_20261014/DSZeroBoneEvalFinalReg_20261124/index.json)).

4. **당시 후속 작업 후보.**
   - 기본 공격과 호환 공격을 사전 검사하고, 같은 인원·입력의 라이브/베이크 대조에서 본 평가와 타격 일치를 확인하는 후보였다. 적용한 세 공격과 40인 기본 공격의 결과는 아래에 기록한다.
   - `Test_MMO_Sandbox` / `Test_MMO_Dungeon` 보스 인카운터에 `CombatDefinition`, 비모션매칭 `AnimBP`, 공격 경고 몽타주 연결 및 실전 루프 검증.
   - `MontageTiming` / `OnPostTickPose` 계약 단위 테스트 추가 및 변경 파일 Git 커밋 정리.
   - (추후 마일스톤) 소스 UE 5.8 전환 → `ReplicationConnections.h` 256슬롯 확장 + `Project_JServer.exe` 전용 빌드 + 200인 단일 존 실측.

## 실전 근접 궤적과 저장된 보스 경고 후속 검증

### 변경과 유지 근거

- `ProjectJ.Combat.DedicatedMeleeMontageTiming`은 전용 서버 모드에서 실제 NPC 몽타주를 월드 틱으로 재생한다. `OnPostTickPose`가 캡처한 Mover 앵커를 사용한 권위 스윕 패킷과 본 변환 리비전 불변을 확인한다. 공격 요청 해제 후에도 몽타주 요청이 독립적으로 남으며, 몽타주 종료 후 기존 메시 정책으로 복원된다. 비베이크 공격은 본 갱신으로 승격되고 자체 요청을 해제할 때 기존 시인성·URO·노티파이 정책으로 복원된다.
- 전용 서버 retarget AnimInstance의 초기화 및 갱신에서 `bEnableFollowerRetarget`을 명시적으로 해제했다. `ProjectJ.PlayerLocomotion.CompatibleBodyServerSource`에서 팔로워 틱과 retarget 플래그, 플레이어 소스 메시의 Post Process AnimBP 비활성화를 검증한다.
- `DA_Spellblade_Equip`의 현재 참조는 `DA_CombatStyle_SpellbladeKit` → `DA_Combo_SpellbladeKit`이다. 해당 조합과 레거시 공격을 함께 사전 검사했다. 레거시 `DA_Attack_Spellblade_LMB1`의 34개 점은 모두 동일했다. 현재 키트의 `Light01` 계열은 두 세그먼트 몽타주로, 기존 베이커의 단일 세그먼트 계약을 벗어난다. 두 경로의 라이브 소켓 경로와 `_Canonical` 테스트 데이터는 유지했다. `AirHeavy`도 약 17.7m의 누적 이동이 관찰되어 공중 경로의 런타임 일치 근거가 확보될 때까지 유지했다.
- 베이커가 허용한 비공중 공격 `DA_Attack_Longblade_Flurry_3`(30키, 506.1cm), `DA_Attack_Longblade_Kick`(14키, 194.2cm), `DA_Attack_SpellbladeKit_ChordLunge`(21키, 63.4cm)의 궤적을 저장했다. 나머지 후보들은 사전 검사에서 다중 구간, 네이티브 타격 창 부재, 소스 스켈레톤 등의 조건으로 제외됐다. [사전 검사](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/preflight.json), [저장 및 검증](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/Baked.json)
- `BP_FieldBoss_Test`에 기존 NPC 비모션매칭 `ABP_NPC_Mover_Unarmed`를 연결했다. 두 저장 맵의 인카운터는 테스트 전용 공격 몽타주와 NPC 활성화 정의를 사용한다. 경고는 몽타주 0.293초에 시작해 0.623초에 해제되고, 원래 0.643초 타격 시작은 유지된다. `ActiveWarningCues`의 공유 네이티브 경고 마커를 사용한다. 루프백 벤치마크는 여전히 배치 인카운터를 제거하고 같은 클래스의 독립 표적을 기존 위치와 25,000 HP 계약으로 생성한다. [에셋 연결 도구](../../../Scripts/Editor/Bind-MMOTestBossCombat.py), [Data Validation](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/BossAssets.json)

### 검증

- 설치형 UE 5.8 직접 UBT `Project_JEditor Win64 Development` 증분 빌드 성공. 컴파일 오류·경고 0건. 8모듈 DAG 검사 통과. 신규 전용 서버 계약과 기존 Canonical/경고 회귀를 포함한 집중 테스트 **104/104 통과**. 보스/맵 6개와 베이크 공격 3개의 Data Validation 오류·경고 0건. [빌드](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/UBTFinal.log), [집중 회귀](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/RegFinal/index.json)
- 로컬 40인 Iris, ThinClient, `-UsePerfThreads -ServerTrace`, 서버/생성기 8스레드씩 격리. 사전 가용 물리 메모리 18,520MiB, 가용 가상 메모리 23,687MiB, 실행 중 물리 메모리 보호 기준 4,096MiB. 40/40 연결에서 40/40 확정 타격과 피해 합계 400을 확인했다. 서버 30Hz 예산을 충족했다. [40인 재실측](../../../Saved/Validation/Continuation_20261014/SpellbladeBossField40_Iris_20261124/verification.json)

| 40인 Iris 필드 보스 | 이전 병렬 기준선 | 이번 재실측 |
| --- | ---: | ---: |
| Burst 월드 틱 p95 / p99 / 최대 | 12.59 / 18.50 / 18.95 ms | 10.66 / 17.12 / 17.18 ms |
| Burst 타격 틱 최대 | 10.57 ms | 7.70 ms |
| Burst 33.3ms 초과 | 0 | 0 |
| Burst `GamethreadEval` | 0 | 0 |
| 전체 트레이스 `GamethreadEval` | 442 | 441 |
| Burst `SetupParallel` | 960 | 1,335 |
| 전체 트레이스 `FParallelAnimationEvaluationTask` | 960 | 1,335 |
| `ProjectJ_Combat_AuthoredMeleePath` | 0 | 0 |

측정 틱의 시작·종료 마커를 CSV와 대조해 전용 서버 직렬 본 평가 0회가 두 실행의 측정 구간에 해당함을 확인했다. 40인 입력이 사용하는 기본 공격은 양쪽에서 라이브 소켓 경로였으므로 틱 차이의 원인을 이번 궤적 베이크에 귀속할 수 없다. 병렬 본 평가 감소와 40인 베이크 효과는 미검증이다. [구간별 타이머 집계](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/TraceSummary.json), [Unreal Insights 타이머](../../../Saved/Validation/Continuation_20261014/SpellbladeBossField40_Iris_20261124/TraceExport/AllTimers.csv)

로컬 24인 Iris 던전 루프백에서 배치된 클래스와 기존 위치를 사용해 24/24 연결·확정 타격, 30Hz 예산 충족을 재검증했다. Burst p95 / p99 / 최대는 7.49 / 10.65 / 13.06ms이며 33.3ms 초과는 0회다. [24인 재실측](../../../Saved/Validation/Continuation_20261014/SpellbladeBossDungeon24_Iris_20261124/verification.json)

두 최신 실행의 실제 Burst 틱 시작 간격 p95는 필드 34.08ms, 던전 34.04ms였다. `worldTickBudgetMet`은 러너의 월드 처리 기준 통과를 뜻한다. 틱 시작 간격과 운영 정원은 별도로 판단한다.

저장된 두 맵을 로컬 D3D12 PIE에서 실행해 배치 보스의 공격과 `ActiveWarningCues` 기반 네이티브 마커 생성·해제를 확인했다. 두 보스 모두 실제 스펠블레이드 전투 모드와 LMB 입력으로 100 HP에서 처치했다. 필드 보스 처치 후 게임 브리지의 우편 조회가 0건에서 1건으로 바뀌었다. 던전은 필드에서 기존 메뉴 티켓을 발행해 이동했다. 플레이어의 시작 위치를 맞추는 데 테스트 텔레포트를 사용했다. [저장 맵 플레이 검증](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/BossPIE.json)

로컬 PostgreSQL·Nakama와 별도 서버 2대에서 `-VerifyAuthoredSandbox`를 통과했다. 저장된 보스의 처치 신호가 온라인 던전 완료, 우편을 포함하는 보상 영수증 정확히 1건, 출발 앵커 복귀로 이어졌다. 온라인 픽스처는 보스 HP를 0으로 설정한다. 플레이어 LMB 반격은 위 PIE 실행에서 검증했다. [로컬 온라인 검증](../../../Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/OnlineDungeon.json)

대표 서버 트레이스, 검증 요약과 변경 전 에셋 백업을 기존 중앙 보관소에 등록했다. 원래 경로와 중앙 경로의 SHA-256을 대조했으며 복원 매핑은 `ArchiveStore/INDEX.csv`와 `Deduplicated.jsonl`에 기록했다.

### 남은 사항

- Standalone PIE에서 던전 지도를 이동하고 보스를 처치했을 때 클리어 우편은 0건이었다. 일반 Standalone `InitNewPlayer`는 던전 입장 티켓을 보존하지 않고, `PostLogin`은 새 프로토타입 캐릭터 ID를 배정한다. 이동 후 참가 기록이 없어 `ResolveFinalBossDefeat`가 완료를 거부했다. Standalone 참가자 식별·티켓 소비는 별도 작업으로 남긴다. 온라인 서버 경로의 클리어와 복귀는 위 별도 실행에서 통과했다.
- 40인 기본 공격의 베이크 적용과 실제 군중에서의 본 평가 감소는 추가 검증이 필요하다. 레거시 고정 궤적과 현재 키트 다중 구간 몽타주에 대해 라이브 판정과 원점·구간을 대조한 후 적용해야 한다.

## 문서 기준 로컬 재검증 · 2026-11-24 야간

기존 Editor 바이너리와 저장된 보스 클래스·아레나를 사용했다. 필드는 같은 PC의 Iris·NullRHI 서버와 ThinClient 생성기 하나를 서버/생성기 8스레드씩 분리했다. 설정은 `-MeasureCombat -BossCombat -FieldArena -ThinClient -UsePerfThreads`, 런타임 물리·커밋 메모리 보호선은 4,096MiB였다. 각 소유 클라이언트가 같은 보스를 한 번 공격했다.

| 실행 | 서버 확정 타격 / 체력 확인 / 로그아웃 | Burst 월드 처리 p95 / p99 | 틱 시작 간격 p95 | 최저 물리 / 커밋 여유 | 결과 |
| --- | --- | --- | --- | --- | --- |
| [필드 16](../../../Saved/Validation/Continuation_20261014/DocGate_Field16_Iris_20261124_214933/verification.json) | 16 / 16 / 16 | 5.19 / 6.92ms | 34.02ms | 8,424 / 12,568MiB | 통과 |
| [필드 24](../../../Saved/Validation/Continuation_20261014/DocGate_Field24_Iris_20261124_215104/verification.json) | 24 / 24 / 24 | 7.49 / 10.47ms | 34.01ms | 7,402 / 11,502MiB | 통과 |
| [필드 32](../../../Saved/Validation/Continuation_20261014/DocGate_Field32_Iris_20261124_215359/verification.json) | 32 / 32 / 32 | 9.50 / 13.31ms | 34.02ms | 5,968 / 10,024MiB | 통과 |
| [필드 40](../../../Saved/Validation/Continuation_20261014/DocGate_Field40_Iris_20261124_215229/verification.json) | 40 / 0 / 0 | 14.01 / 17.84ms, 중단 전 표본 | 34.07ms | 4,063 / 8,026MiB | 물리 메모리 보호 중단 |

16·24·32인의 Burst는 모두 처리 시간 33.3ms 초과 0틱이었고, 러너 종합 처리 예산을 통과했다. 40인은 체력 25,000→24,600의 서버 피해를 확정한 뒤 `COHORT_MEMORY_STOP`이 발동해 체력 수신과 종료 단계에 진입하지 못했다. 서버·생성기 프로세스는 정상 종료했다. 생성기 Private Bytes 최고값은 24인 6,327MiB, 32인 7,930MiB, 40인 10,141MiB로 증가했다. 실제 실패 조건은 물리 메모리 부족이었다. 앞서 성공한 40인 실행은 더 큰 사전 여유와 8개씩 종료하는 구성이므로 별도 판본으로 유지한다. 이 호스트의 인원 확대는 32인 검증까지이며, 32인은 운영 정원 추정치가 아니다.

별도 [던전 16인 D3D12 관측](../../../Saved/Validation/Continuation_20261014/DocGate_Dungeon16_Iris_RHI_20261124_215530/verification.json)은 서버·생성기·관측을 4/4/8스레드로 분리했다. 타격·체력 수신·로그아웃은 각각 16건이었다. 공격 구간에서 화면 인원 중앙 16, 원격 경량 망토 검증 통과, 중앙 103.90 FPS와 프레임 p95 13.94ms(255표본)를 기록했다. 서버 Burst 월드 처리 p95 7.35ms, 틱 시작 간격 p95 34.20ms, 33.3ms 초과 0틱이었다. 최저 물리/커밋 여유는 4,693/4,719MiB였다. 이 실행은 던전 전투와 화면 표현의 검증이며 온라인 입장·보상과 배치 보스 경고는 위 별도 실행에 근거한다.

- [집중 회귀](../../../Saved/Validation/Continuation_20261014/DocGate_Focused_20261124_215708/index.json) 7/7 통과. 전용 서버 `MontageTiming`과 라이브 복원, 서버 리타깃 비활성, 공통 피해 권위·투사체, 원격 탑승자 Cloth, D3D12 경고 수명, 비행 스태미나를 확인했다.
- [PGlite 재실행](../../../Saved/Validation/Continuation_20261014/DocGate_SQL_20261124_215757/Run.log)은 새 테스트 DB에서 001~010 적용과 저장·보상·우편·던전·채널·제작·재시작 회귀를 통과했다. 이번 실행에서는 실제 Nakama·PostgreSQL 서버 통합을 다시 열지 않았다.
- 대표 원자료 16개를 중앙 `ArchiveStore`에 하드링크로 등록하고 크기·SHA-256 및 원본과 동일한 파일인지 확인했다. 원래 실행 경로, 중앙 위치와 복원 정보는 기존 `INDEX.csv`와 `Deduplicated.jsonl`에 있다. 새 C++ 빌드·Cook/패키징은 수행하지 않았다.

## 수용량 추정과 분리 호스트 검증

야간 실측에서 **32인이 전체 시나리오를 완주**했다. **40인은 전원 피해 확정 뒤 물리 RAM 보호 중단**이다. 이전 40인 완주, 설치형 Iris의 유효 슬롯 127개, Generic 합성 200접속은 각자 다른 관측이다. 아래 계산은 같은 PC의 Iris 단발 필드 보스에서 다음 검증 구간을 고르기 위한 외삽이다.

### 메모리 보호선

야간 필드의 최저 가용 물리 메모리는 16/24/32/40인에서 각각 8,424/7,402/5,968/4,063MiB였다. 8명 증가마다 1,022/1,434/1,905MiB가 줄었다. 32→40인 기울기 `(4,063 − 5,968) / 8 = −238.125MiB/인`을 적용하면 36인에서 약 5,016MiB, 보호선 4,096MiB와의 교차점은 약 39.9인이다. 36인은 이번 조건에서 미실측이다.

최근 두 구간의 가용 RAM 감소량을 180~240MiB/인으로 단순 연장하면, 40인 실행의 최저 여유를 기준으로 동일 PC에서 보호선에 닿기까지 더 필요한 가용 RAM의 예시는 다음과 같다.

| 목표 인원 | 추가 가용 물리 메모리의 산식 결과 |
| --- | ---: |
| 80 | 7.1~9.4GiB |
| 100 | 10.6~14.1GiB |
| 120 | 14.1~18.8GiB |

산식은 `((목표 인원 − 40) × (180~240) + (4,096 − 4,063)) / 1,024`다. 보호선에 도달하는 값이므로 시작 전 여유·운영 여유를 포함하지 않는다. OS와 다른 프로세스 사용량, 생성기 구성, 에셋과 종료 단계에 따라 달라진다. 생성기의 Private Bytes도 인원과 함께 늘었으며, 이 가용 RAM 감소량으로 서버의 연결당 메모리를 추정할 수 없다.

### Burst 처리 시간의 30Hz 예산 교차점

인원 `N`과 Burst 월드 처리 시간 `T(N)`의 관계를 `T(N) = a × N + b`로 놓고, `T(N) = 1,000 / 30ms`인 지점을 계산했다. 완주 표본 16·24·32인의 최소제곱 직선과, 32인부터 40인 중단 전 표본까지의 기울기를 별도로 사용했다.

| 지표 | 완주 3개 표본의 기울기 / 교차점 | 32→40인 구간의 기울기 / 교차점 |
| --- | ---: | ---: |
| Burst 월드 처리 p95 | +0.26925ms/인 / 약 120인 | +0.563125ms/인 / 약 74인 |
| Burst 월드 처리 p99 | +0.3991875ms/인 / 약 82인 | +0.566375ms/인 / 약 67인 |

**p95 약 74~120인, p99 약 67~82인**은 두 단순 모델의 교차점 범위다. 통계적 신뢰구간, 호스트를 분리한 서버의 예측치, 검증된 정원 또는 운영 설정값으로 쓰지 않는다. p99의 33.33ms 교차점은 Burst 꼬리 지연을 살펴보는 추가 기준이다. 러너의 실제 `worldTickBudgetMet`에는 유휴·타격·Drain 조건도 있다. 32인 완주에서도 실제 틱 시작 간격 p95는 34.02ms였다. 반복 교전과 비동기 복제·종료 비용이 커지면 선형 관계가 달라질 수 있다.

### 호스트 분리에서 확인할 것

- RAM 여유를 늘리면 메모리 보호 중단 가능성을 줄일 수 있다. 이번 40인에서 페이징이 Burst에 미친 영향은 계측하지 않았다. 호스트 분리는 RAM과 CPU 스케줄링 경쟁을 줄일 수 있으나 서버의 복제·GAS·충돌·포즈 처리 비용은 각 경로에 남는다.
- 같은 PC의 **별도 Generic 합성 200접속**에서 생성기 1→2개로 바꾸자 지속 구간의 월드 처리 p95가 17.00→11.74ms, 요청 처리 p99가 1,093.66→94.99ms였다. 생성기 구성에 측정값이 민감하다는 근거다. Iris 제작 보스나 별도 호스트의 개선율은 그 시험에서 측정하지 않았다. [합성 200접속과 종료 비교](Project_J_MMO_Independent_Audit_2026-11-23.md#로컬-200접속-전투-성능-기준선)
- 동일 서버 PC·서버 빌드·맵·장비·조준·공격 밀도·복제 설정을 유지하고 32인과 40인에서 생성기 배치만 바꿔 비교한다. 사설 LAN 분리는 현재 루프백 러너의 범위를 벗어나므로 구현·검증된 테스트 경로가 마련되는 시점에 진행한다. LAN 지연과 패킷 손실, 서버 게임 스레드의 실행·스케줄 대기, 복제·송신량, 메모리·페이지 폴트, 체력 확인·종료를 함께 기록한다.
- 여유와 재현성을 확인한 뒤 48→64→80인과 반복 교전을 시험할 수 있다. 100·120인 시험과 운영 정원은 후속 실측으로 판단한다. 설치형 Iris의 관측 입장 한도 127은 별도 경계다. 실제 200인 같은 보스에는 해당 규모를 지원하는 NetDriver와 전투 검증이 모두 필요하다.

## 스펠블레이드 실전 공격 전체 베이크

- 실제 장착 경로가 참조하는 29개 정의를 검사했다. 근접 공격 26개의 28개 타격 창을 기존 `/Game/DataAssetSets/AttackDefinition/Spellblade/CombatKit`의 공격 DA에 저장했다. 새로 저장한 23개와 기존 지문·키가 같아 재저장을 건너뛴 3개를 포함한다. 타격 창이 두 개인 Flurry 1·2타는 두 경로를 분리한다. Focus·Mend·Haste는 기존 효과 스킬이고, 이전 `_Canonical` 회귀용 정의는 유지한다. [제작 절차와 재베이크 기준](../../Animation/Authoring/Spellblade_CombatKit.md#근접-타격-궤적-베이크)
- 타격 창을 완전히 포함하는 세그먼트를 고르고, 120Hz와 최대 64키를 적용했다. 긴 Finisher 창에는 60Hz를 적용했다. 모든 창에서 압축 소스의 키·중간점과 비교했다. 최대 차이는 AirLight의 13.08cm였고 해당 스윕 반지름은 50cm였다. 이 값의 비교 기준은 메시 공간 원본이고, 런타임 블렌딩·루트 이동·이동 표적에서의 판정 일치는 별도 검증 항목이다. [사전 검사](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Preflight.json) · [저장 기록](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Applied.json) · [원본 패키지 복원 명세](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Backups.json)

동일한 로컬 Iris 필드 보스 설정에서 `Light01`을 A/B로 측정했다. 두 실행 모두 16개 접속이 피해 160을 합산해 보스 체력을 25,000→24,840으로 바꿨고, 16개 클라이언트가 결과를 받은 후 정상 종료했다. 서버/생성기 코어 마스크 255/65280, ThinClient, `-UsePerfThreads`, 물리 메모리 보호선 4,096MiB를 고정했다. 기존 `MontageTiming`과 새 베이크를 함께 사용한 작업 트리에서의 결과다.

| 측정 | 라이브 소켓 | 베이크 |
| --- | ---: | ---: |
| Burst 월드 처리 p95 / p99 / 최대 | 5.477 / 6.971 / 12.546ms | 3.901 / 4.696 / 4.844ms |
| Burst 틱 시작 간격 p95 | 34.025ms | 34.035ms |
| Burst 월드 처리 33.3ms 초과 | 0틱 | 0틱 |
| Burst `ProjectJ_Combat_AuthoredMeleePath` | 0회 | 112회 |
| Burst 본 갱신 병렬 작업 등록 | 532회 | 0회 |
| 전체 트레이스 병렬 애니메이션 평가 | 532회, 누적 146.394ms | 0회 |

베이크 적용 후 기본 공격은 서버에서 몽타주 시간과 Mover 루트로 타격 스윕을 수행했다. 두 실행은 모두 `worldTickBudgetMet=true`였고 틱 시작 간격 p95는 비슷했다. 월드 처리 시간 차이는 한 쌍의 단발 A/B에서 관측한 값이며, 워커 누적 시간은 스레드별 실행 시간의 합이다. [라이브 결과](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_Field16_Live_20261124/verification.json) · [베이크 결과](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_Field16_Baked_20261124/verification.json) · [Insights 타이머 비교](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/ServerTraceComparison.json)

- **수행 검증.** Direct UBT `Project_JEditor Win64 Development` 빌드 성공, 새 Editor 프로세스에서 공격 DA 26/26 재로드 및 Data Validation 통과(오류·경고 0건), 집중 회귀 11/11 통과. [재로드 결과](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Verified.json) · [집중 회귀](../../../Saved/Validation/Continuation_20261014/SpellbladeFullBake_Regression_20261124/index.json)
- **미검증.** 16인 실소켓 A/B의 입력은 기본 공격이었다. 전체 콤보·공중기·이중 타격 창의 라이브 이동 표적 시나리오와 베이크 적용 후 40인 이상은 재측정하지 않았다. 위의 이전 40인 실측과 수용량 외삽은 당시 에셋 판본의 값이다.
