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

- **접속과 전투는 별도 검증이다.** 설치형 UE 5.8의 현행 Iris 드라이버는 유효 슬롯 127을 보고했다. 채널 요청 정원과 드라이버가 허용하는 정원은 따로 기록한다. Generic에서 200 소켓과 단순 피해 부하는 통과했다. 앞선 단순 피해 부하는 플레이어 Pawn 없이 GAS 대상에 즉시 피해를 준 시험이다. [채널 정원과 Iris](Project_J_MMO_Independent_Audit_2026-11-23.md#채널별-운영-정원과-로컬-제작-보스-기준선) · 200접속 단순 부하 (로컬 `Saved/Validation/Continuation_20261014/CombatCohort200Final_20261123/verification.json`)
- **제작 보스를 사용한 이전 200인 시험은 실패했고, 최신 장비·조준 경로의 200인 재실행은 남아 있다.** 이전 실행에서 참가자 200명이 접속하고 공격 활성화 200건을 기록했으며 확정 피해는 0건이었다. 최장 월드 틱은 3.34초였다. 생성기는 서버가 결과를 기록한 뒤 종료 처리 중 Windows 커밋 한도에 도달했다. 참가자 간 복제를 제한한 후속 진단에서는 메모리 오류 없이 종료했지만 여전히 피해 0건이었다. 공격 단계 월드 틱 p95는 148.86ms였고, 기록된 거절 사유에는 공격 시작 시각 검증 47건과 미래 시각 24건이 포함됐다. 최초 실패 (로컬 `Saved/Validation/Continuation_20261014/AuthoredBossGeneric200Final_20261123/verification.json`) · 메모리 로그 (로컬 `Saved/Validation/Continuation_20261014/AuthoredBossGeneric200Final_20261123/Clients.log`) · [후속 진단](Project_J_MMO_Independent_Audit_2026-11-23.md#채널별-운영-정원과-로컬-제작-보스-기준선)
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

선행 Phase 1~3은 헤드리스 서버 기본 애니메이션, 소유자 복제, 전투·진영·PvE·부활·투사체, 장비와 온라인 저장, 경고 VFX·Cloth·오디오·UI, 탈것·스트리밍·에셋 검증기를 수정했다. 기존 흩어진 사용자 변경도 같은 작업 트리에 있다. 선행 40인 Iris 필드와 24인 Iris 던전의 `worldTickBudgetMet`은 모두 통과했다. 40인 공격 p99는 61.93ms, 33.33ms 초과는 6틱이었다. 24인 던전의 대기 p95는 4.16ms, 공격 중 33.33ms 초과는 0틱이었다. 선행 99건 회귀 (로컬 `Saved/Validation/Continuation_20261014/AuditFixFinal_20261124/index.json`) · 40인 필드 (로컬 `Saved/Validation/Continuation_20261014/AuditFixField40_Iris_20261124/verification.json`) · 24인 던전 (로컬 `Saved/Validation/Continuation_20261014/AuditFixDungeon24_Iris_20261124/verification.json`)

- **신규 계약 보호.** PvE의 플레이어·같은 인카운터 NPC 판정과 무소속 NPC 예외, 사망 공격자·GAS 태그·Mover 입력, 보스 이베이드 체력 복구, 아군 뒤 벽 및 적 판정, 원격 Mover 기수와 미확정 소유권의 Cloth 경계, 급강하 부스트 비용, 높은 미로드 지형 위의 디테일 그리드를 검증했다. 실제 D3D12에서는 활성 경고의 원격 스폰 예산·불투명도 면제를 확인했다. 집중 회귀 102/102 (로컬 `Saved/Validation/Continuation_20261014/AuditFollowupContractsFinal_20261124/index.json`) · D3D12 경고 회귀 1/1 (로컬 `Saved/Validation/Continuation_20261014/AuditFollowupWarningRHI_20261124/index.json`)
- **재현에서 확인한 수정.** 투사체가 아군 뒤의 같은 스윕 구간을 다시 검사해 벽이나 적을 놓치지 않게 했다. 겹친 아군이 많을 때는 루프를 제한하고 다음 틱에서 나머지 구간을 처리한다. 경고의 예산 면제는 즉시 스폰과 비동기 에셋 로드 완료가 같은 판정 함수를 사용한다. [투사체 경로](../../../Source/Project_JCharacter/Private/Combat/Project_JProjectileManagerSubsystem.cpp) · [경고 스폰과 불투명도](../../../Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp)

### 필드 실접속과 Burst 스파이크

각 실행은 30Hz Iris, `-ThinClient`, 서버 마스크 255와 생성기 마스크 65280, 물리·커밋 4,096MiB 보호선을 사용했다. 러너는 시작 후 적용한 affinity 값을 읽어 검증 결과에 포함한다.

| 실행 | 접속·타격 | 월드 처리 p95 / p99 | 33.33ms 초과 | 합산 서버 기준 | 최소 가용 물리 |
| --- | ---: | ---: | ---: | --- | ---: |
| 40인, 생성기 둘, 트레이스 | 34에서 보호 중단 | — | — | 미측정 | 3,799MiB |
| 40인, 생성기 하나, 트레이스 | 40/40 | 27.25 / 45.15ms | 4틱 | 통과 | 4,966MiB |
| 24인, D3D12 관측 클라이언트 하나 | 24/24 | 23.37 / 33.65ms | 2틱 | 통과 | 4,814MiB |

24인 렌더 실행은 원격 경량 망토 23개와 소유 캐릭터 원본을 확인했다. 공격 중앙 FPS 75.82, 프레임 p95 24.20ms, 화면 인원 중앙 24명이었다. 기존 Generic 필드 24인 렌더의 중앙 95.86FPS와 비교해 낮았으며, 복제 방식·실행 구성·빌드가 달라 프레임 차이의 원인은 분리되지 않았다. 두 생성기 중단 (로컬 `Saved/Validation/Continuation_20261014/AuditFollowupIrisField40Trace_20261124/verification.json`) · 40인 결과 (로컬 `Saved/Validation/Continuation_20261014/AuditFollowupIrisField40SingleTrace_20261124/verification.json`) · 24인 렌더 결과 (로컬 `Saved/Validation/Continuation_20261014/AuditFollowupIrisField24Rendered_20261124/verification.json`)

40인에서 느린 틱 4개는 `ProjectJ_CohortMeasuredTickStart/End`를 사용해 각 CSV 행과 대조했다. 네 틱의 `ReleaseTickGroup Block`은 28.03~40.71ms, `NetDriver TickFlush`는 4.22~5.77ms였다. 최장 49.29ms 틱에서는 대기 구간 안의 `STAT_USkeletalMeshComponent_RefreshBoneTransforms_GamethreadEval` 40회가 합계 37.50ms였다. 같은 구간의 Iris 업데이트는 2.65ms이며 NetDriver 범위에 포함된다. 네 틱에서 새로 확정된 타격은 0건이었다. 이전 p99와는 생성기 수와 계측 조건이 달라 개선 폭을 판정할 수 없다. 느린 틱 이벤트 요약 (로컬 `Saved/Validation/Continuation_20261014/AuditFollowupIrisField40SingleTrace_20261124/BurstSlowTicks.json`) · [Insights CSV 내보내기](../../../Scripts/Validation/Export-DTrace.ps1)

서버 유휴 정책과 별도로 공격 시작은 `GameplayPose`를 요구하고, 현행 근접 검증은 본 갱신 완료에서 소켓·몽타주 위치·루트 샘플을 수집한다. 이 경로를 유지했다. 다음 최적화에서 서버 공격 포즈를 줄이려면 실제 공격 정의별로 소켓 기반과 베이크된 궤적의 소비자를 확인하고, 같은 명중·노티파이·루트 모션을 검증할 수 있는 경로부터 비교해야 한다. 동기 본 평가의 엔진 분기를 일으킨 조건도 추가 분리 대상이다. [공격 포즈 요청과 샘플 소비](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp#L240)

80·100·120인은 시작을 보류했다. 동일 두 생성기는 34접속에서 보호선에 닿았고, 40인 단일 생성기도 최저 여유가 4,966MiB였다. 로컬 단발 전투에서 관측한 서버 종합 기준 통과는 40인까지다. 80인 이상과 운영 정원은 별도 생성기 호스트 또는 검증된 경량 생성기와 반복 부하에서 확인한다.

### Migration 010과 저장 테스트 맵

- **DB.** PGlite 0.3.10의 새 저장소에 001~010을 적용했다. 최초 적용을 실패시킨 기존 함수 인자명과 우편 스키마 불일치를 수정했다. 기존 우편 수령의 서버 가방 용량, 세션, 응답과 원장 계정을 유지하고 던전 골드에 시스템 발행 출처를 추가했다. 보상 정의 버전 저장·재실행·키 충돌, 우편 수령 재시도·오래된 세션·잔액 보존, 미완료 퀘스트 거부와 완료 수령 재실행, 만료 데이터 정리와 재실행을 실제 SQL로 통과했다. 이 실행은 단일 로컬 PGlite의 범위다. [마이그레이션](../../../Infra/migrations/010_online_audit_fixes.sql) · [회귀 스크립트](../../../Infra/scripts/check-mmo-commands.mjs)
- **보스와 검증기.** 저장본에서 두 테스트 인카운터가 같은 `BP_FieldBoss_Test`의 머리·몸 MultiPart 두 개를 사용하는 것을 확인했다. NPC BP·AnimBP와 두 탈것 후보, 보스·샌드박스·던전 등 선택 13개는 모두 Valid였고 Data Validation 오류·경고는 각각 0건이었다. 두 배치 인카운터의 `CombatDefinition`과 보스의 AnimClass가 비어 있었다. 이 상태는 피해를 받는 테스트 보스 용도로 유지했다. 실제 보스 공격의 경고 VFX까지 연결하려면 해당 장면용 전투 정의, 호환 AnimBP와 경고 타이밍이 있는 공격 몽타주가 필요하다. 경고의 수신부는 별도 D3D12 회귀에서 확인했다. 저장본 에셋 검사 (로컬 `Saved/Validation/Continuation_20261014/AuditFollowupAssets_20261124/verification.json`)

설치 엔진 직접 Editor 증분 빌드와 8모듈 DAG 검사를 통과했다. 작업 원자료는 기존 중앙 보관소에서 당시 실행 이름으로 찾을 수 있다.

## 전용 서버 병렬 평가·MontageTiming 및 24인 Iris/Generic A/B (2026-11-24 후속)

1. **원인 분리와 코드 반영.**
   - UE 5.8 전용 서버(`-server`)는 `-useperfthreads`가 없으면 `FApp::ShouldUseThreadingForPerformance()`가 `false`를 반환해 `RefreshBoneTransforms`가 게임 스레드 직렬(`GamethreadEval`)로 떨어진다. [Run-LoopbackCohort.ps1](../../../Scripts/Validation/Run-LoopbackCohort.ps1)에 `-UsePerfThreads`와 `-RenderedAffinityMask`를 추가했다.
   - 40인 러너의 기본 공격이 라이브 소켓 스윕을 사용함을 당시 트레이스(`ProjectJ_Combat_AuthoredMeleePath=0`, `ProjectJ_Combat_MeleeNotify=223`)에서 확인했다. 후속 에셋 검사에서 실제 장비 참조는 `DA_Spellblade_Equip → DA_CombatStyle_SpellbladeKit → DA_Combo_SpellbladeKit`로 확인했다. 레거시 `DA_Attack_Spellblade_LMB1`과 실제 키트의 `Light01`을 별도로 평가했다.
   - 향후 베이크된 공격(`MeleeTrajectory.bEnabled=true`) 및 전용 서버 일반 몽타주를 위해 `EProject_JAnimationUpdateRequirement::MontageTiming`과 `UProject_JBudgetedSkeletalMeshComponent::OnPostTickPose`, `UProject_JCombatHitValidationComponent::CanUseDedicatedServerMontageOnlyPose()`를 추가해 전용 서버에서 본 평가 없이 Mover 루트 앵커·몽타주 시각만으로 `TickAuthoredPath` 검증이 가능하도록 분리했다. [Project_JBudgetedSkeletalMeshComponent.cpp](../../../Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp) · [Project_JCombatHitValidationComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp)
   - 전용 서버에서 `PostProcessAnimBlueprint`, `CopyPose` 모듈러 메시 틱, 그리고 `VisualBodyMesh` 리타깃 팔로워(`bEnableFollowerRetarget`·Hand/Foot/Retarget IK)를 비활성화했다. [Project_JPlayerCharacter.cpp](../../../Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp) · [Project_JNPCCharacter.cpp](../../../Source/Project_JCharacter/Private/Project_JNPCCharacter.cpp) · [Project_JModularMeshComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JModularMeshComponent.cpp) · [Project_JCharacterAnimInstance.Budget.cpp](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Budget.cpp)

2. **40인 Iris 필드 보스 Burst 스파이크 제거 검증.**
   - `-UsePerfThreads` 적용 40인 실행(DSZeroBoneEvalField40_Iris_20261124 (로컬 `Saved/Validation/Continuation_20261014/DSZeroBoneEvalField40_Iris_20261124/verification.json`), AllTimers.csv (로컬 `Saved/Validation/Continuation_20261014/DSZeroBoneEvalField40_Iris_20261124/TraceExport/AllTimers.csv`)): 40/40 확정 타격, `worldTickBudgetMet: true`, **측정 구간 `GamethreadEval=0`**, `SetupParallel=960`(총 0.94ms), Burst `worldTickMs` p95 **12.59ms** / p99 **18.50ms** / p100 **18.95ms** (`over33ms: 0`), `strikeTickMs` p100 **10.57ms**.
   - `-UsePerfThreads` 미적용 30.0Hz 대조군(DSZeroBoneEvalV2_Field40_Iris30Hz_20261124 (로컬 `Saved/Validation/Continuation_20261014/DSZeroBoneEvalV2_Field40_Iris30Hz_20261124/verification.json`)): Idle p50 **6.65ms** / p95 **7.62ms**, `strikeTickMs` p100 **14.78ms**, 비베이크 공격의 직렬 본 평가로 Burst `over33ms` 3틱(p100 47.52ms).

3. **24인 렌더 클라이언트 Iris vs Generic 동일 조건 A/B.**
   - 3-way 코어 격리(`Server=15`, `Clients=240`, `RenderedClient=65280`, `-ThinClient -UsePerfThreads -VerifyRemoteCloak`) 비교:
     - **Iris** (AB_Field24_Iris_20261124 (로컬 `Saved/Validation/Continuation_20261014/AB_Field24_Iris_20261124/verification.json`)): 렌더 Before **84.07 FPS** / Strike **77.44 FPS** (`gpuMs` 2.48/2.54ms), 서버 Burst p95 **10.33ms** (`over33ms: 0`), 송신 **1,651 KiB/s**.
     - **Generic** (AB_Field24_Generic_20261124 (로컬 `Saved/Validation/Continuation_20261014/AB_Field24_Generic_20261124/verification.json`)): 렌더 Before **78.47 FPS** / Strike **80.93 FPS** (`gpuMs` 2.46/2.48ms), 서버 Burst p95 **15.98ms** (`over33ms: 0`), 송신 **2,308 KiB/s**.
   - 이 실행에서 Iris의 Burst 월드 처리 p95는 약 35%, 송신량은 약 29% 낮았다. Burst 틱 시작 간격 p95는 Iris 48.03ms, Generic 34.08ms였다. 관측 클라이언트 FPS는 공격 전과 공격 중의 순위가 달랐다. 월드 처리 시간은 프로세스 CPU 사용률과 다른 지표이며, 각 구간과 반복 수를 함께 읽는다.
   - 최종 검증: `Validate-MMOArchitecture.py` `passed: true`, 직접 UBT `Project_JEditor` 빌드 오류·경고 0건, 최종 집중 회귀 99/99 통과(DSZeroBoneEvalFinalReg_20261124 (로컬 `Saved/Validation/Continuation_20261014/DSZeroBoneEvalFinalReg_20261124/index.json`)).

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
- 베이커가 허용한 비공중 공격 `DA_Attack_Longblade_Flurry_3`(30키, 506.1cm), `DA_Attack_Longblade_Kick`(14키, 194.2cm), `DA_Attack_SpellbladeKit_ChordLunge`(21키, 63.4cm)의 궤적을 저장했다. 나머지 후보들은 사전 검사에서 다중 구간, 네이티브 타격 창 부재, 소스 스켈레톤 등의 조건으로 제외됐다. 사전 검사 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/preflight.json`), 저장 및 검증 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/Baked.json`)
- `BP_FieldBoss_Test`에 기존 NPC 비모션매칭 `ABP_NPC_Mover_Unarmed`를 연결했다. 두 저장 맵의 인카운터는 테스트 전용 공격 몽타주와 NPC 활성화 정의를 사용한다. 경고는 몽타주 0.293초에 시작해 0.623초에 해제되고, 원래 0.643초 타격 시작은 유지된다. `ActiveWarningCues`의 공유 네이티브 경고 마커를 사용한다. 루프백 벤치마크는 여전히 배치 인카운터를 제거하고 같은 클래스의 독립 표적을 기존 위치와 25,000 HP 계약으로 생성한다. [에셋 연결 도구](../../../Scripts/Editor/Bind-MMOTestBossCombat.py), Data Validation (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/BossAssets.json`)

### 검증

- 설치형 UE 5.8 직접 UBT `Project_JEditor Win64 Development` 증분 빌드 성공. 컴파일 오류·경고 0건. 8모듈 DAG 검사 통과. 신규 전용 서버 계약과 기존 Canonical/경고 회귀를 포함한 집중 테스트 **104/104 통과**. 보스/맵 6개와 베이크 공격 3개의 Data Validation 오류·경고 0건. 빌드 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/UBTFinal.log`), 집중 회귀 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/RegFinal/index.json`)
- 로컬 40인 Iris, ThinClient, `-UsePerfThreads -ServerTrace`, 서버/생성기 8스레드씩 격리. 사전 가용 물리 메모리 18,520MiB, 가용 가상 메모리 23,687MiB, 실행 중 물리 메모리 보호 기준 4,096MiB. 40/40 연결에서 40/40 확정 타격과 피해 합계 400을 확인했다. 서버 30Hz 예산을 충족했다. 40인 재실측 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossField40_Iris_20261124/verification.json`)

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

측정 틱의 시작·종료 마커를 CSV와 대조해 전용 서버 직렬 본 평가 0회가 두 실행의 측정 구간에 해당함을 확인했다. 40인 입력이 사용하는 기본 공격은 양쪽에서 라이브 소켓 경로였으므로 틱 차이의 원인을 이번 궤적 베이크에 귀속할 수 없다. 병렬 본 평가 감소와 40인 베이크 효과는 미검증이다. 구간별 타이머 집계 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/TraceSummary.json`), Unreal Insights 타이머 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossField40_Iris_20261124/TraceExport/AllTimers.csv`)

로컬 24인 Iris 던전 루프백에서 배치된 클래스와 기존 위치를 사용해 24/24 연결·확정 타격, 30Hz 예산 충족을 재검증했다. Burst p95 / p99 / 최대는 7.49 / 10.65 / 13.06ms이며 33.3ms 초과는 0회다. 24인 재실측 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossDungeon24_Iris_20261124/verification.json`)

두 최신 실행의 실제 Burst 틱 시작 간격 p95는 필드 34.08ms, 던전 34.04ms였다. `worldTickBudgetMet`은 러너의 월드 처리 기준 통과를 뜻한다. 틱 시작 간격과 운영 정원은 별도로 판단한다.

저장된 두 맵을 로컬 D3D12 PIE에서 실행해 배치 보스의 공격과 `ActiveWarningCues` 기반 네이티브 마커 생성·해제를 확인했다. 두 보스 모두 실제 스펠블레이드 전투 모드와 LMB 입력으로 100 HP에서 처치했다. 필드 보스 처치 후 게임 브리지의 우편 조회가 0건에서 1건으로 바뀌었다. 던전은 필드에서 기존 메뉴 티켓을 발행해 이동했다. 플레이어의 시작 위치를 맞추는 데 테스트 텔레포트를 사용했다. 저장 맵 플레이 검증 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/BossPIE.json`)

로컬 PostgreSQL·Nakama와 별도 서버 2대에서 `-VerifyAuthoredSandbox`를 통과했다. 저장된 보스의 처치 신호가 온라인 던전 완료, 우편을 포함하는 보상 영수증 정확히 1건, 출발 앵커 복귀로 이어졌다. 온라인 픽스처는 보스 HP를 0으로 설정한다. 플레이어 LMB 반격은 위 PIE 실행에서 검증했다. 로컬 온라인 검증 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeBossFollowup_20261124/OnlineDungeon.json`)

대표 서버 트레이스, 검증 요약과 변경 전 에셋 백업을 기존 중앙 보관소에 등록했다. 원래 경로와 중앙 경로의 SHA-256을 대조했으며 복원 매핑은 `ArchiveStore/INDEX.csv`와 `Deduplicated.jsonl`에 기록했다.

### 남은 사항

- Standalone PIE에서 던전 지도를 이동하고 보스를 처치했을 때 클리어 우편은 0건이었다. 일반 Standalone `InitNewPlayer`는 던전 입장 티켓을 보존하지 않고, `PostLogin`은 새 프로토타입 캐릭터 ID를 배정한다. 이동 후 참가 기록이 없어 `ResolveFinalBossDefeat`가 완료를 거부했다. Standalone 참가자 식별·티켓 소비는 별도 작업으로 남긴다. 온라인 서버 경로의 클리어와 복귀는 위 별도 실행에서 통과했다.
- 40인 기본 공격의 베이크 적용과 실제 군중에서의 본 평가 감소는 추가 검증이 필요하다. 레거시 고정 궤적과 현재 키트 다중 구간 몽타주에 대해 라이브 판정과 원점·구간을 대조한 후 적용해야 한다.

## 문서 기준 로컬 재검증 · 2026-11-24 야간

기존 Editor 바이너리와 저장된 보스 클래스·아레나를 사용했다. 필드는 같은 PC의 Iris·NullRHI 서버와 ThinClient 생성기 하나를 서버/생성기 8스레드씩 분리했다. 설정은 `-MeasureCombat -BossCombat -FieldArena -ThinClient -UsePerfThreads`, 런타임 물리·커밋 메모리 보호선은 4,096MiB였다. 각 소유 클라이언트가 같은 보스를 한 번 공격했다.

| 실행 | 서버 확정 타격 / 체력 확인 / 로그아웃 | Burst 월드 처리 p95 / p99 | 틱 시작 간격 p95 | 최저 물리 / 커밋 여유 | 결과 |
| --- | --- | --- | --- | --- | --- |
| 필드 16 (로컬 `Saved/Validation/Continuation_20261014/DocGate_Field16_Iris_20261124_214933/verification.json`) | 16 / 16 / 16 | 5.19 / 6.92ms | 34.02ms | 8,424 / 12,568MiB | 통과 |
| 필드 24 (로컬 `Saved/Validation/Continuation_20261014/DocGate_Field24_Iris_20261124_215104/verification.json`) | 24 / 24 / 24 | 7.49 / 10.47ms | 34.01ms | 7,402 / 11,502MiB | 통과 |
| 필드 32 (로컬 `Saved/Validation/Continuation_20261014/DocGate_Field32_Iris_20261124_215359/verification.json`) | 32 / 32 / 32 | 9.50 / 13.31ms | 34.02ms | 5,968 / 10,024MiB | 통과 |
| 필드 40 (로컬 `Saved/Validation/Continuation_20261014/DocGate_Field40_Iris_20261124_215229/verification.json`) | 40 / 0 / 0 | 14.01 / 17.84ms, 중단 전 표본 | 34.07ms | 4,063 / 8,026MiB | 물리 메모리 보호 중단 |

16·24·32인의 Burst는 모두 처리 시간 33.3ms 초과 0틱이었고, 러너 종합 처리 예산을 통과했다. 40인은 체력 25,000→24,600의 서버 피해를 확정한 뒤 `COHORT_MEMORY_STOP`이 발동해 체력 수신과 종료 단계에 진입하지 못했다. 서버·생성기 프로세스는 정상 종료했다. 생성기 Private Bytes 최고값은 24인 6,327MiB, 32인 7,930MiB, 40인 10,141MiB로 증가했다. 실제 실패 조건은 물리 메모리 부족이었다. 앞서 성공한 40인 실행은 더 큰 사전 여유와 8개씩 종료하는 구성이므로 별도 판본으로 유지한다. 이 호스트의 인원 확대는 32인 검증까지이며, 32인은 운영 정원 추정치가 아니다.

별도 던전 16인 D3D12 관측 (로컬 `Saved/Validation/Continuation_20261014/DocGate_Dungeon16_Iris_RHI_20261124_215530/verification.json`)은 서버·생성기·관측을 4/4/8스레드로 분리했다. 타격·체력 수신·로그아웃은 각각 16건이었다. 공격 구간에서 화면 인원 중앙 16, 원격 경량 망토 검증 통과, 중앙 103.90 FPS와 프레임 p95 13.94ms(255표본)를 기록했다. 서버 Burst 월드 처리 p95 7.35ms, 틱 시작 간격 p95 34.20ms, 33.3ms 초과 0틱이었다. 최저 물리/커밋 여유는 4,693/4,719MiB였다. 이 실행은 던전 전투와 화면 표현의 검증이며 온라인 입장·보상과 배치 보스 경고는 위 별도 실행에 근거한다.

- 집중 회귀 (로컬 `Saved/Validation/Continuation_20261014/DocGate_Focused_20261124_215708/index.json`) 7/7 통과. 전용 서버 `MontageTiming`과 라이브 복원, 서버 리타깃 비활성, 공통 피해 권위·투사체, 원격 탑승자 Cloth, D3D12 경고 수명, 비행 스태미나를 확인했다.
- PGlite 재실행 (로컬 `Saved/Validation/Continuation_20261014/DocGate_SQL_20261124_215757/Run.log`)은 새 테스트 DB에서 001~010 적용과 저장·보상·우편·던전·채널·제작·재시작 회귀를 통과했다. 이번 실행에서는 실제 Nakama·PostgreSQL 서버 통합을 다시 열지 않았다.
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
- 타격 창을 완전히 포함하는 세그먼트를 고르고, 120Hz와 최대 64키를 적용했다. 긴 Finisher 창에는 60Hz를 적용했다. 모든 창에서 압축 소스의 키·중간점과 비교했다. 최대 차이는 AirLight의 13.08cm였고 해당 스윕 반지름은 50cm였다. 이 값의 비교 기준은 메시 공간 원본이고, 런타임 블렌딩·루트 이동·이동 표적에서의 판정 일치는 별도 검증 항목이다. 사전 검사 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Preflight.json`) · 저장 기록 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Applied.json`) · 원본 패키지 복원 명세 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Backups.json`)

동일한 로컬 Iris 필드 보스 설정에서 `Light01`을 A/B로 측정했다. 두 실행 모두 16개 접속이 피해 160을 합산해 보스 체력을 25,000→24,840으로 바꿨고, 16개 클라이언트가 결과를 받은 후 정상 종료했다. 서버/생성기 코어 마스크 255/65280, ThinClient, `-UsePerfThreads`, 물리 메모리 보호선 4,096MiB를 고정했다. 기존 `MontageTiming`과 새 베이크를 함께 사용한 작업 트리에서의 결과다.

| 측정 | 라이브 소켓 | 베이크 |
| --- | ---: | ---: |
| Burst 월드 처리 p95 / p99 / 최대 | 5.477 / 6.971 / 12.546ms | 3.901 / 4.696 / 4.844ms |
| Burst 틱 시작 간격 p95 | 34.025ms | 34.035ms |
| Burst 월드 처리 33.3ms 초과 | 0틱 | 0틱 |
| Burst `ProjectJ_Combat_AuthoredMeleePath` | 0회 | 112회 |
| Burst 본 갱신 병렬 작업 등록 | 532회 | 0회 |
| 전체 트레이스 병렬 애니메이션 평가 | 532회, 누적 146.394ms | 0회 |

베이크 적용 후 기본 공격은 서버에서 몽타주 시간과 Mover 루트로 타격 스윕을 수행했다. 두 실행은 모두 `worldTickBudgetMet=true`였고 틱 시작 간격 p95는 비슷했다. 월드 처리 시간 차이는 한 쌍의 단발 A/B에서 관측한 값이며, 워커 누적 시간은 스레드별 실행 시간의 합이다. 라이브 결과 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_Field16_Live_20261124/verification.json`) · 베이크 결과 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_Field16_Baked_20261124/verification.json`) · Insights 타이머 비교 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/ServerTraceComparison.json`)

- **수행 검증.** Direct UBT `Project_JEditor Win64 Development` 빌드 성공, 새 Editor 프로세스에서 공격 DA 26/26 재로드 및 Data Validation 통과(오류·경고 0건), 집중 회귀 11/11 통과. 재로드 결과 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_20261124/Verified.json`) · 집중 회귀 (로컬 `Saved/Validation/Continuation_20261014/SpellbladeFullBake_Regression_20261124/index.json`)
- **미검증.** 16인 실소켓 A/B의 입력은 기본 공격이었다. 전체 콤보·공중기·이중 타격 창의 라이브 이동 표적 시나리오와 베이크 적용 후 40인 이상은 재측정하지 않았다. 위의 이전 40인 실측과 수용량 외삽은 당시 에셋 판본의 값이다.

## 대규모 인원 대응 멀티스레딩·Tick 집계 1차 구현 · 2026-11-24

아래는 1차 구현과 당시 실측이다. 재등록, 타격 기록 순서, 비동기 투사체와 쿨다운 경계 및 성능 귀인은 [2차 독립 검증](#멀티스레딩과-tick-집계-2차-독립-검증)에서 보완했다.

### 적용 내용

1. **중앙 데이터 지향 `TG_PostPhysics` Tick 집계 (`UProject_JCombatSpatialHistorySubsystem`)**
   - `Source/Project_JCharacter/Public/Combat/Project_JCombatSpatialHistorySubsystem.h`, `Private/Combat/Project_JCombatSpatialHistorySubsystem.cpp` 신설.
   - `UProject_JServerSideRewindComponent`와 `UProject_JMultiPartHitComponent`의 액터별 분산 `TG_PostPhysics` 컴포넌트 Tick을 단일 `FProjectJCombatSpatialHistoryTickFunction` 연속 배열 패스로 통합(액터 파괴/EndPlay 시 자동 해제, 빈 상태 시 Tick 자동 비활성, 히치 보간 왜곡 금지 및 `TickGroup`/`GetComponentTickInterval()` 계약 보존).
2. **전용 서버 클라이언트 전용 Tick 제거 및 Tick Batching Opt-in**
   - `UProject_JWeaponPresentationComponent`, `UProject_JPlayerInputBindingComponent`: `NM_DedicatedServer`에서 `BeginPlay` 시 Tick 비활성화.
   - `AProject_JPlayerCharacter`: `NM_DedicatedServer`에서 `LocomotionAnimStateComponent->UpdateState` 및 `RecordMouseTurnFrame` 생략, `PrimaryActorTick.bAllowTickBatching = true` 활성화.
   - `UProject_JModularMeshComponent`, `UProject_JServerSideRewindComponent`, `UProject_JMultiPartHitComponent`: `PrimaryComponentTick.bAllowTickBatching = true` 설정.
   - `AProject_JGameMode::BeginPlay()`: 전용 서버가 `-useperfthreads` 없이 기동되어 `FApp::ShouldUseThreadingForPerformance() == false`일 때 운영자 경고 로그 출력.
3. **Mass 군중 이동·투사체 핫패스 무할당 및 병렬화**
   - `UProject_JMassMovementProcessor`: `ReusableSamples`/`ReusableCells`/`ReusableGrid`로 스크래치를 재사용한다. 회귀는 시리얼/병렬 위치 일치를 확인했다. 프레임당 전체 힙 할당 건수는 측정하지 않았다.
   - `UProject_JProjectileManagerSubsystem`: `ParallelFor` 탄도 적분, 쿼리 파라미터 재사용, 선택형 `AsyncSweepByChannel`을 추가했다. 당시 정리는 파괴된 대상만 처리했다.
4. **UE 5.8 엔진 스레딩·비동기 스트리밍 명시 설정**
   - `Config/DefaultServerEngine.ini`: `tick.AllowBatchedTicks=1`, `tick.AllowAsyncComponentTicks=1`, `tick.AllowAsyncTickDispatch=1`, `tick.AllowAsyncTickCleanup=1`.
   - `Config/DefaultEngine.ini`: `[/Script/Engine.StreamingSettings]`(`s.AsyncLoadingThreadEnabled=True`, `s.EventDrivenLoaderEnabled=True`, `s.UseBackgroundLevelStreaming=True`) 및 `tick.AllowBatchedTicks=1` 명시.

### 검증 결과

- **아키텍처·빌드**: `Validate-MMOArchitecture.py` 통과(8모듈 DAG 유지, 205 기능 / 20 도메인), Direct UBT `Project_JEditor Win64 Development` 성공(**0 errors, 0 warnings**).
- **집중 회귀 테스트** (결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleParallelReg_20261124_v2/index.json`)): **106/106 통과, 0 failures, 0 warnings** (19.42s).
  - 신규 3종(`ProjectJ.Combat.SpatialHistoryAggregation`, `ProjectJ.Combat.ProjectileParallelAndPrune`, `ProjectJ.Mass.ZeroAllocSteadyStateCrowd`) 및 기존 `ProjectJ.Internal.Updates.RewindScheduling`, `ProjectJ.Crowd.MassSpacing` 모두 통과.
  - 이 실행의 필터는 `RewindScheduling`만 선택했으므로 `UrgentAnimationWindow`는 제외됐다. 2차 검증에 현재 Mover 캐릭터를 사용하는 픽스처로 포함했다.
- **실측 32인 Iris Field Boss 비교** (`-MeasureCombat -BossCombat -FieldArena -ThinClient -UsePerfThreads`, 서버/생성기 코어 마스크 255/65280, 물리 메모리 보호선 4,096MiB; 직전 32인 기준선 vs 스펠블레이드 베이크+대규모 병렬화 통합 작업 트리):

| 지표 (32인 Iris Field Boss) | 변경 전 (DocGate_Field32 (로컬 `Saved/Validation/Continuation_20261014/DocGate_Field32_Iris_20261124_215359/verification.json`)) | 변경 후 (MassiveScaleField32 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleField32_Iris_20261124/verification.json`)) | 변화 |
| --- | ---: | ---: | ---: |
| 타격 확정 / 체력 확인 / 정상 종료 | 32 / 32 / 32 | **32 / 32 / 32** (`25,000→24,680`) | 동일(완주) |
| Idle `worldTickMs` p95 / p99 | 7.457 / 8.049ms | **6.687 / 6.862ms** | **-10.3% / -14.7%** |
| Burst `worldTickMs` p95 | 9.501ms | **8.087ms** | **-14.9%** |
| Burst `worldTickMs` p99 | 13.305ms | **8.827ms** | **-33.7%** |
| Burst `worldTickMs` p100 (최대) | 15.041ms | **9.124ms** | **-39.3%** |
| Burst `peakStrikesPerTick` | 14 strikes/tick | 19 strikes/tick | 공격이 묶이는 틱 조건도 달랐다 |
| Burst `over33ms` / 예산 충족 | 0 / `true` | **0 / `true`** | 통과 |

- **참고 및 보류**: 동일 호스트(32GB RAM)에서 40인 재시도(MassiveScaleField40 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleField40_Iris_20261124/verification.json`))는 37접속 시점에 4,096MiB 물리 메모리 보호선으로 안전 중단됨(호스트 RAM 제약). 100~200인 이상 실측 및 소스 빌드 UE 5.8 Iris 256슬롯 확장 + 독립 `Project_JServer.exe` 패키징 + 분리 호스트 부하 테스트는 다음 인프라 단계 과제로 남긴다.

## 멀티스레딩과 Tick 집계 2차 독립 검증

### 확인된 결함과 조치

| 심각도 | 원인과 영향 | 조치 |
| --- | --- | --- |
| P1 | 캡처가 `BeginPlay`에만 등록됐다. UE `UActorComponent::UnregisterComponent`는 Tick을 해제하되 `EndPlay`를 호출하지 않으므로, 컴포넌트를 재등록한 뒤 중앙 슬롯을 잃으면 SSR 기록이 끊길 수 있었다. 외부 Tick 호출로 기존 개별 writer도 다시 켤 수 있었다. | `OnRegister`/`OnUnregister`에 수명을 연결하고 일시 정지·재개를 중앙 writer에 전달한다. 권위 상실 시 과거 기록을 지우고 기존 등록의 권위 회복 시 새로 채운다. [SSR](../../../Source/Project_JCharacter/Private/Combat/Project_JServerSideRewindComponent.cpp) · [MultiPart](../../../Source/Project_JCharacter/Private/Combat/Project_JMultiPartHitComponent.cpp) |
| P1 | `TG_PostPhysics` 안의 다른 작업 순서는 그룹만으로 보장되지 않는다. UE 스켈레탈 메시의 끝 그룹도 PostPhysics가 될 수 있어 MultiPart가 먼저 기록할 수 있었다. 텔레포트 직후 빈 기록도 이전 주기를 기다렸다. | 소켓을 사용하는 MultiPart의 메시 Tick에 선행 관계를 추가했다. Reset 다음 유효 틱에서 최초 샘플을 기록하고, 정상 주기는 예정 시각을 이어가되 히치 보간용 샘플을 만들지 않는다. [중앙 캡처](../../../Source/Project_JCharacter/Private/Combat/Project_JCombatSpatialHistorySubsystem.cpp) |
| P1 | Async 투사체는 요청과 회수 틱을 교대로 쓰면서 회수 틱의 이동량을 버렸고, 만료나 조회 실패에서 미결 구간을 버리거나 교체할 수 있었다. UE `WorldCollisionAsync.cpp::QueryTraceData`는 완료된 직전 프레임만 반환한다. | 이전 구간을 먼저 확정하고 이번 구간을 제출한다. 조회할 수 없으면 그 구간을 동기로 검사한다. 새 구간을 열기 전에 이전 구간의 벽·동료 통과·관통 상태와 만료를 확정한다. 동기 경로가 기본값이다. [투사체 처리](../../../Source/Project_JCharacter/Private/Combat/Project_JProjectileManagerSubsystem.cpp) |
| P1 | 생존 대상의 만료 쿨다운이 남고, 투사체가 0개가 되면 정리도 중단됐다. 고유 채널을 오래 발급하는 공격에 연결하면 맵이 세션 동안 커질 수 있다. 저장소 C++의 일반 플레이 호출자는 이번 추적에서 확인되지 않았다. | 월드 시간 만료를 보관하고 미정리 쿨다운이 있을 동안 주기적으로 만료·파괴 대상을 제거한다. 살아 있는 대상에 200개 채널을 만든 후 정리까지 검증한다. [쿨다운 처리](../../../Source/Project_JCharacter/Private/Combat/Project_JProjectileManagerSubsystem.cpp) |
| P2 | 기존 `UrgentAnimationWindow`의 `ACharacter` 픽스처는 Mover Pawn만 받는 실제 coordinator의 타입 검사에서 반환됐다. | 현행 Spellblade Pawn·coordinator·예산 메시 픽스처로 교체했다. URO 해제, 짧은 재요청, 복원, 파괴를 확인한다. [URO 회귀](../../../Source/Project_JCharacter/Private/Tests/Project_JUpdateOwnershipTests.cpp) |
| P2 | 로컬 `UnrealEditor-Cmd -server`의 실제 값은 배치 1, 비동기 Dispatch/Cleanup 0/0이었다. 배치는 SystemSettings에서 왔고 뒤의 두 값은 엔진 기본값이었다. UE `ConfigContext.cpp`의 dedicated 플랫폼 경로는 `DedicatedServerEngine.ini`를 사용한다. 현재 `DefaultServerEngine.ini`의 모든 값을 로컬 실행에 적용된 값으로 볼 수 없다. | 24인 검증에서 세 옵션을 서버 프로세스에만 명시 적용했다. 이번 비교에서 성능 이득이 관측되지 않아 다른 서버 정책까지 함께 활성화하는 설정 파일 이관은 보류했다. 기본값 관찰 로그 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependent16_Observe_20261124/Server.log`) |

### 확인된 엔진 계약과 남은 경계

- UE 5.8 `TickTaskManager.cpp`에서 배치는 동일 시작·끝 그룹, 일반 우선순위, GT Tick만 묶는다. 순서 보존 모드에서는 선행 작업이 같은 배치의 앞에 있거나 외부 이벤트로 연결된다. 동시 큐 등록(`tick.AllowConcurrentTickQueue`)과 배치는 엔진이 상호 배제한다. 비동기 Dispatch는 큐 제출을 넘기며 Cleanup은 완료 뒤 그룹당 완료 이벤트 50개 이상에서 분기한다. 두 설정은 게임플레이 Tick을 워커에 보내지 않는다. Cloth는 자체 PrePhysics→PostPhysics/LastDemotable 체인과 완료 핸들 조건을 유지한다. 별도 클라이언트 RHI 장시간 부하는 여기에서 검증하지 않았다.
- CharacterMovement의 물리 작업과 Mover의 Network Prediction 월드 사전 Tick이 PostPhysics보다 앞선다. 소켓 메시 선행 관계는 본 갱신을 실제로 요청하는 에셋의 완료를 기다린다. 동적으로 소켓/시각 메시를 교체하는 보스, 미권위로 생성됐다가 처음 권위를 얻는 액터, PIE 다중 월드·SeamlessTravel·리플레이 실플레이는 별도 시나리오다. `RecordRateHz`는 제작 기본값으로 초기화·재등록 시 적용된다. 실행 중 주기·링 크기 재설정 API는 없다.
- 전용 서버의 PlayerCharacter Tick은 Super와 전투 회전 정책을 계속 실행한다. 입력 바인딩 Tick은 로컬 Enhanced Input 정지 경계, 무기 표현 Tick은 생성된 시각 무기와 그립 상태를 처리한다. 서버 판정은 별도의 권위 공격 데이터, Mover 루트 및 필요 시 권위 스켈레탈 소켓을 읽는다. `Run-ContinuationTests.ps1`은 native 종료 코드와 자동화 결과를 보고 기동 경고 문자열로 실패시키지 않는다.
- `FCollisionQueryParams::ClearIgnoredSourceObjects`는 `AddIgnoredActor`와 같은 배열을 비운다. 고정 필터·복잡도는 초기화 후 투사체별로 바꾸지 않는다. UE는 비동기 요청의 파라미터를 trace datum에 복사한다. `ParallelFor`는 투사체별 탄도 숫자와 분리된 스텝 배열만 수정하며 반환한 뒤 배열 제거·약한 참조·Chaos·GAS를 GT에서 처리한다. 인접 메모리 경쟁에 따른 성능 저하는 고밀도 투사체 프로파일에서 측정할 항목이다.
- Mass는 매 실행 그리드 키를 지우고 활성 셀만 인덱싱한다. 워커는 준비된 const 스냅샷을 읽고 독립 청크를 갱신한 뒤 join한다. WorldSubsystem마다 Processor 하나이며 중첩 구동을 거부하고 등록은 2,048개로 제한한다. 스크래치는 최대 사용량을 월드 수명 동안 보관한다. 2,000개 스폰·축소 반복 메모리와 할당 계수는 실측하지 않았다.
- StreamingSettings 섹션은 하나다. `AsyncPackageLoader.cpp`는 cooked와 uncooked에서 각각 `StreamingSettings`와 `EditorStreamingSettings`를 읽는다. `s.EventDrivenLoaderEnabled`는 설치 엔진의 BaseEngine과 중복이고 UE 5.8 `IsEventDrivenLoaderEnabled()`는 cooked 여부를 사용한다. 이번 Editor 러너로 패키지 로더나 Cook 변화를 판정하지 않았다.

### 성능 귀인

기존 32인 두 실행은 각각 32/32 피해·체력·종료를 확인했다. Burst p95는 9.501→8.087ms였으나, 적용 에셋과 코드가 다르고 실제 시작 간격 p95는 34.020→48.019ms, 대기 초과 p95는 0.682→14.671ms였다. 초당 송신량과 공격이 묶인 틱도 다르다. 당시 실행 경로와 중앙 명세에서 두 비교의 `Server.utrace`가 확인되지 않았다. 시작 간격이 길어진 OS 정책까지 특정할 자료도 없다. 해당 차이는 통합 변경의 관측으로 보존한다.

베이크 자체는 위의 동일 작업 트리 16인 A/B에서 확인했다. 새 24인 A/B는 동일 베이크와 동일 Editor 바이너리에서 Tick 스케줄러 세 옵션만 0/1로 변경했다. 중앙 SSR/MultiPart 집계와 나머지 코드가 두 실행에 모두 포함됐다. `AllowAsyncComponentTicks=1`, `AllowConcurrentTickQueue=0`, `AllowBatchedTicksUnordered=0`을 양쪽 로그에서 확인했다. Safe memory reserve는 4,096MiB, 서버/생성기 코어 마스크 255/65280이었다.

| 24인 Iris 단발 필드 교전 | 엔진 스케줄러 기본값 | 배치·비동기 Dispatch/Cleanup 켬 |
| --- | ---: | ---: |
| 확정 피해 / 체력 확인 / 정상 종료 | 24 / 24 / 24 | 24 / 24 / 24 |
| Burst 월드 처리 p95 / p99 | 5.856 / 6.525ms | 6.015 / 6.971ms |
| Burst 시작 간격 p95 | 34.058ms | 34.058ms |
| Burst 처리 33.3ms 초과 | 0틱 | 0틱 |
| 최저 물리 / 커밋 여유 | 6,451 / 10,171MiB | 6,537 / 10,248MiB |

이번 A/B에서 세 옵션을 켜는 성능 이득은 관측되지 않았다. 단일 비교이므로 0.159ms 차이를 재현 가능한 비용으로 확정할 수 없다. 중앙 Tick 집계 단독 기여율을 구하려면 동일 장비와 압력에서 집계만 바꾼 통제 비교가 필요하다. 24인 기준 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependent24_EngineDefaults_20261124/verification.json`) · 24인 옵션 사용 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependent24_Optimized_20261124/verification.json`)

Insights에서 Burst 틱당 GT p95는 중앙 이력 캡처 **0.0055→0.0053ms**, `PlayerCharacterTick` 합산 **0.0146→0.0161ms**, `WaitForTasks` **0.1059→0.0968ms**, `ReleaseTickGroup Block` **0.9471→1.0266ms**였다. 전체 트레이스의 `TG_PostPhysics` 평균은 각각 약 **0.027ms**였다. Burst의 GT 본 평가는 양쪽 모두 0회였고 베이크 근접 스윕은 양쪽 168회였다. 전체 실행의 `GamethreadEval` 265회씩은 Burst 선택 범위 밖이었다. 실제 시작 간격이 같은 이 조건에서 비용이 집계 함수에 집중됐다는 증거는 없다. Insights 집계 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependent24_Optimized_20261124/TickSchedulerComparison.json`)

### 최종 회귀

- 8모듈 DAG 통과. Direct UBT `Project_JEditor Win64 Development` 증분 빌드 성공, 컴파일러 오류·경고 각각 0건. 이전 UBA 세션 디렉터리 청소 권한 메시지는 빌드 로그에 보존했다. 최종 빌드 로그 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependentGroundedTestBuild_20261124/UBT.log`)
- 1차와 같은 필터에 기존 누락 테스트와 새 Async Sweep을 더해 **110/110 통과**, 실패·경고·미실행 0건. 실제 월드 틱에서 동기·비동기 벽, 아군 통과, 관통 후 중복 타격, swap 제거, 발사자 파괴, 수명 끝 구간을 확인했다. 도중 바닥이 없던 움직이는 NPC 픽스처의 실패도 남겼다. 최종 결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependentGroundedReg_20261124/index.json`) · 중간 결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependentFinalReg_20261124/index.json`)
- 최종 빌드의 기본 설정 관찰에서는 16/16 피해·체력·종료, Burst 월드 처리 p95 4.209ms, 틱 시작 간격 p95 34.059ms, 33.3ms 초과 0틱이었다. 24인 스케줄러 A/B 이후 재등록과 개별 Tick 중복 방지를 추가한 판본이다. 최종 실접속 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleIndependent16_Observe_20261124/verification.json`)
- 실측은 로컬 16·24인 단발 Iris 필드 보스다. 반복 교전, 밀집 투사체 A/B, 접속 100~200인, 24시간 지속시간, Shipping 패키지와 분리 호스트는 이 작업의 검증 범위 밖이다.

### 100~200인 확장 검증 우선순위

| 순위 | 관측할 지점 | 영향·난이도 | 검증 방법 |
| --- | --- | --- | --- |
| 1 | Iris 연결 수와 실제 전송 정책 | 매우 높음 · 높음 | `GetIncomingCapacity`의 유효 슬롯과 `PreSendUpdate`·연결별 직렬화·송신량·업데이트 지연을 분리한다. `UProject_JNetObjectPrioritizer_Combat` 호출은 개발용 정책 덤프와 테스트에 있다. 실제 설정은 `ProjectJPlayerMover`용 SpherePrioritizer와 별도 PlayerState 관객 필터를 쓴다. 공유 보스·아군·군중간 가시성을 고정한 상태로 분리 호스트에서 증가시킨다. [NetDriver](../../../Source/Project_J/Network/Project_JIpNetDriver.cpp) · [관객 필터](../../../Source/Project_J/Network/Project_JPlayerStateAudienceFilter.cpp) |
| 2 | Mover·Chaos 쿼리·SSR | 매우 높음 · 중~높음 | 정지와 동시 이동·밀집·정정 재생을 분리한다. Network Prediction, Chaos query, 타격 원점 보관과 판정 실패 사유를 함께 기록한다. 데스크톱 RAM과 CPU 경쟁을 줄인 뒤 p99를 비교한다. |
| 3 | GAS 및 AI 동시 이벤트 | 높음 · 중~높음 | 타격 수와 타깃 수를 동시에 올리지 말고 단발·반복·AoE·투사체를 나눈다. `ApplyGameplayEffectSpecToTarget`, Cue 복제, Threat의 참가자 순회와 TargetScoring의 snapshot·후보 수·워커 대기를 계측한다. 한 보스의 선형 순회와 많은 NPC가 같은 군중을 평가하는 복합 비용을 구분한다. [피해 확정](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp) · [위협](../../../Source/Project_JCharacter/Private/AI/Project_JThreatComponent.cpp) |
| 4 | 클라이언트 GT·RT·GPU와 표현 밀도 | 매우 높음 · 중간 | 동일 카메라·해상도에서 두 팀의 동시 이동과 교전을 렌더한다. 애니메이션·Cloth, Niagara 경고/일반 FX, 오디오 음성, 네임플레이트·미니맵·HUD의 항목 수와 p95를 분리하고 경고 가독성을 관측한다. [기존 클라이언트 성능 자료](../../Performance/README.md) |
| 5 | GC·스트리밍·저장·접속/종료의 꼬리 비용 | 높음 · 중간 | 스폰/퇴장, 셀 전환, 보상·채널 변경을 반복해 GC reachability/Destroy·로드·GT JSON 완료 콜백과 Nakama/PostgreSQL 요청 지연을 분리한다. SQLite는 로컬 프로필 경로다. 이번 테스트의 전투 동시 처리량을 온라인 저장 부하로 환산할 수 없다. |

## 2차 감사 후속: 실전 Iris 전투 우선순위기·전용 서버 INI·200인 서버/클라이언트 핫패스 고도화 · 2026-11-25

아래는 Wave 2의 1차 구현과 당시 검증이다. 실제 Iris 적용, 설정의 소비 경로, 누락 회귀는 [Wave 2 독립 감사](#wave-2-독립-감사와-런타임-검증)에 이어서 기록했다.

### 적용 내용

1. **실전 Iris 5계층 전투 우선순위기 연결 (`USphereWithOwnerBoostNetObjectPrioritizer`)**
   - `Source/Project_JCharacter/Public/Movement/Project_JPlayerMoverPrioritizerConfig.h/.cpp`, `Config/DefaultEngine.ini`: 기존 `ProjectJPlayerMover`를 `USphereWithOwnerBoostNetObjectPrioritizer`로 승격하고 `ProjectJCombatActive`(3.0x), `ProjectJCombatMode`(1.5x), `ProjectJCombatDead`(0.25x), `ProjectJCombatBoss`(외곽 150m까지 보스 보호) 5계층 등록.
   - `Source/Project_JCharacter/Public/Network/Project_JNetObjectPrioritizer_Combat.h/.cpp`, `Source/Project_JCharacter/Public/Project_JBaseCharacter.h/.cpp`: GAS 상태 태그(`State_Dead`, `State_Attacking`, `State_Dodging`, `State_HitReacting`, `State_CombatMode`) 이벤트 기반으로 `UReplicationSystem::SetPrioritizer`를 O(1) 전환(프레임당 폴링·접속별 태그 조회 0회, `ProjectJ.Net.CombatIrisPrioritizer`로 A/B 토글 가능, 초기 `NetRefHandle` 미등록 시 최대 4틱 안전 재시도 및 `EndPlay`/`UnPossessed` 해제).
2. **UE 5.8 전용 서버 설정 경로 정합화 (`Config/DedicatedServerEngine.ini`)**
   - UE 5.8 `ConfigContext.cpp`가 실제로 읽는 `Config/DedicatedServerEngine.ini` 신설(`TotalNetBandwidth=50000000`, `net.MaxRPCPerNetUpdate=32`, `a.Budget.Enabled=0`, `r.PSOPrecaching=0`, `DDCvar.FootPlacementMode=0`, `tick.AllowBatchedTicks=1`, 24인 A/B 결과에 따라 `tick.AllowAsyncTickDispatch=0` / `Cleanup=0` 유지). `Config/DefaultServerEngine.ini`에 안내 주석 추가.
3. **200인 서버 전투·생명주기·백엔드 핫패스 최적화**
   - `Project_JAnimNotifyState_MeleeHit.cpp`: 권위 `TickAuthoredPath`에서 `ECC_Visibility` LOS 레이캐스트·GAS `TargetData` 할당·이벤트 전송 **이전**에 `CanAffectCombatTarget`(아군/사망/회피) 사전 필터링 및 `TInlineAllocator<8>` 적용.
   - `Project_JThreatComponent.cpp`: 보스 페이즈 기본 임계값 배열을 `static const`로 전환해 피격당 힙 할당 제거.
   - `Project_JMultiPartHitComponent.cpp`: 링버퍼 리와인드 탐색을 선형 스캔에서 $O(\log H)$ 이진 탐색으로 교체.
   - `Project_JCombatHitValidationComponent.cpp`: `ServerExecuteHitQuery`의 $O(N \times K)$ `OutHitActors.Contains`를 `TInlineSetAllocator<16>` `TSet`으로 교체.
   - `Project_JDeathReviveSubsystem.h/.cpp`: `NotifyCharacterDied`의 만료 오퍼 정리를 프레임당 1회로 병합하고 `ScheduledReleaseDeadline` 비교로 보스 전멸 시 200명 동시 사망을 $O(N^2)$에서 $O(N)$으로 축소.
   - `Project_JGatewaySubsystem.h/.cpp`: `MaxInFlightRequests` 기본값을 `128 → 512`로 상향(200인 보스 처치 시 400건 보상/퀘스트 HTTP 폭주 유실 방지) 및 Nakama Basic Auth Base64 캐싱.
   - `Project_JGameMode.h/.cpp`: `MaxTrustedSessions` 기본값 `128 → 256`(설치형 Iris 127 상한은 `GetIncomingCapacity()`가 자동 클램프) 및 `Logout()`/`EndPlay()`에서 `PendingOnlineQuestProgress` 누수 정리.
   - `Project_JLootSubsystem.h/.cpp`: 해결·만료된 `PartyRolls` 즉시/주기 제거 및 `BossRewardReceipts` 16,384건 FIFO 상한 적용.
4. **200인 클라이언트 군중 표현(ABA·VFX·Audio·HUD) 최적화**
   - `Project_JBudgetedSkeletalMeshComponent.h/.cpp`, `Project_JCharacterAnimationBudgetSubsystem.cpp`: `UProject_JAnimationUpdateCoordinatorComponent` 약참조 캐싱으로 매 프레임 200인 `FindComponentByClass` 스캔 제거.
   - `Project_JCombatPresentationComponent.h/.cpp`, `Config/DefaultScalability.ini`: 원격 플레이어 선택적 단발 VFX 거리 컬링(`ProjectJ.Combat.Presentation.OtherPlayerMaxCueDistanceCm=3500`, 보스/경고/루프/로컬 제외), `WeaponPresentationComponent` 캐싱, `[EffectsQuality@0..Cine]` 군중 VFX 투명도/선택 이펙트 연동.
   - `Project_JAudioBudgetSubsystem.h/.cpp`: `GFrameCounter` 단위 로컬 오디오 리스너 위치 캐싱, 25m 초과 원격 SFX 즉시 기각, 프레임당 1회/상한 도달 시 `PrunePlayback()`.
   - `Project_JPlayerHUDWidget.cpp`: `UpdateCharacter()`에서 리소스 행 수가 변경될 때만 `ApplyPreferences()`를 호출해 피격·회복 틱마다 발생하던 전체 HUD Slate 레이아웃 무효화 제거.

### 검증 결과

- **아키텍처·빌드**: `Validate-MMOArchitecture.py` 통과(8모듈 DAG 유지, 205 features / 20 domains), Direct UBT `Project_JEditor Win64 Development` 성공(**0 errors, 0 warnings**).
- **집중 회귀 테스트** (index.json (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2Reg_20261125/index.json`)): **110/110 통과, 0 failures, 0 warnings** (13.61s). 신규 `ProjectJ.Net.IrisCombatPrioritizerPolicy`, `ProjectJ.Combat.MultiPartBinarySearchRewind` 포함.
- **실접속 24인 Iris Field Boss 검증** (verification.json (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2_Field24_Iris_20261125/verification.json`) · Server.log (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2_Field24_Iris_20261125/Server.log`)):
  - `DedicatedServerEngine.ini` 실제 로딩 확인 (`net.MaxRPCPerNetUpdate:32`, `a.Budget.Enabled:0`), 5계층 Iris 전투 우선순위기 오류·경고 0건.
  - 확정 타격/체력 확인/정상 종료 **24 / 24 / 24** (`25,000 → 24,760`), 틱 시작 간격 p95 **34.062ms**, Idle `worldTickMs` p95 **5.106ms**, Burst `worldTickMs` p50 **4.381ms** / p95 **5.737ms** / p99 **6.991ms** / p100 **7.123ms**(`peakStrikesPerTick=15`, `over33ms=0`, `worldTickBudgetMet=true`).
- **미검증/후속 항목**: `DedicatedServerEngine.ini`의 DDoS 섹션명(`[DDoSDetection]` vs `[/Script/PacketHandler.DDoSDetection]`) 런타임 활성화 확인, 100~200인 동시 이동+반복 교전+클라이언트 RHI 군중 프레임 실측, UE 5.8 소스 빌드 256슬롯 및 호스트 분리 부하 테스트.

## Wave 2 독립 감사와 런타임 검증

### 확인된 결함과 변경

| 심각도 | 관찰과 영향 | 수정 범위 |
| --- | --- | --- |
| P1 | 초기 Iris 전환이 최대 네 번의 다음 틱 재시도에 의존했고 CVar를 꺼도 이미 적용된 전투 우선순위가 남았다. 이름 캐시는 등록 종료와 새 핸들을 함께 추적하지 않았다. | 엔진 `OnReplicationStartedForIris`/`OnStopReplicationForIris`를 사용한다. 등록 시 적용하고 종료 시 캐시를 비운다. CVar 변경 시 현재 권위 캐릭터를 한 번 갱신한다. 빙의 해제·새 ASC 바인딩, MultiPart 등록 변경, NPC의 `EnableThreatObservation`에서 현재 상태를 반영한다. [Iris 적용](../../../Source/Project_JCharacter/Private/Network/Project_JNetObjectPrioritizer_Combat.cpp) · [액터 수명](../../../Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp) |
| P1 | UE 5.8 `FDDoSDetection::Init`는 `[DDoSDetection]`을 읽는다. 기존 클래스 형식 섹션은 소비되지 않아 전용 서버에서 탐지가 꺼져 있었다. | 전용 서버 섹션 이름을 수정했다. 루프백 부하에서 실제 활성값과 정상 접속 결과를 확인하는 진단 옵션을 추가했다. 한 IP에서 접속한 사실만으로 공격으로 분류되는 경로는 확인되지 않았다. 집계 패킷·수신 시간 한도는 40인 이상과 반복 교전에서 관찰해야 한다. [전용 서버 INI](../../../Config/DedicatedServerEngine.ini) · [러너](../../../Scripts/Validation/Run-LoopbackCohort.ps1) |
| P1 | 코디네이터가 없는 NPC의 약참조는 유효하지 않기 때문에 기존 조건이 매 프레임 다시 검색했다. 무기 표현이 없는 액터도 부착 큐마다 같은 검색을 반복했다. | 한 번 조회한 빈 결과를 보관하고 관련 컴포넌트 등록·해제에서 캐시를 무효화한다. 파괴된 약참조는 다음 조회에서 다시 확인한다. [메시 조회](../../../Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp) · [무기 큐](../../../Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp) |
| P1 | 던전에서 시체 자동 해제가 멈춘 동안 파괴되거나 스트리밍 아웃한 시체의 추적 등록이 남았다. 여러 시체를 한 번에 해제할 때 각 부활이 타이머 후보를 다시 스캔할 수 있었다. | EndPlay에서 추적을 해제한다. 가장 이른 후보가 바뀌는 경우에 재계산하고 일괄 해제 중에는 처리 뒤 한 번 재계산한다. [사망 추적](../../../Source/Project_J/Gameplay/Project_JDeathReviveSubsystem.cpp) |
| P1 | PlayerState가 사라졌을 때 포인터 키를 사용하면 이전 큐를 찾지 못하고, 같은 PlayerState에 재승인된 세션을 이전 HTTP 응답이 지울 수 있었다. 16건을 넘는 진척 입력은 일부를 버리고 성공을 반환했다. | 저장된 약참조 키로 이전 세션을 해제하고 활성 lease를 확인한 뒤 응답을 소비한다. 진척 묶음은 전량 접수하거나 과부하를 반환한다. Gateway 종료에서는 인증 캐시 두 값을 비운다. [GameMode](../../../Source/Project_J/Game/Project_JGameMode.cpp) · [Gateway](../../../Source/Project_J/Backend/Project_JGatewaySubsystem.cpp) |
| P2 | 베이크 스윕의 사전 필터가 제외 대상을 이미 처리한 것으로 표시했다. 이후 같은 틱의 다른 구간에서 상태가 바뀌면 다시 판정할 수 없었다. | 허용된 접촉만 중복 집합에 기록하고, 필터 호출 뒤 실행 토큰을 재확인한다. ASC 없는 일반 기하·테스트 액터는 기존 공통 규칙대로 검사한다. 라이브 소켓은 기존 원시 접촉 이벤트 계약을 유지한다. [근접 판정](../../../Source/Project_JCharacter/Private/Animation/Project_JAnimNotifyState_MeleeHit.cpp) |
| P2 | 24인 로그에서 `TotalNetBandwidth=32000`을 확인했다. 이 속성은 `AGameNetworkManager`의 Game 설정에 속하고 `UpdateNetSpeeds()`는 전용 서버에서 조기 반환한다. | 전용 서버 INI에서 동작하지 않는 50 MB/s 선언을 제거했다. 실제 연결 속도는 UE 5.8 `UWorld::NotifyControlMessage`의 `NMT_Netspeed` 경로에서 `NetDriver->MaxClientRate`로 제한된다. [전용 서버 INI](../../../Config/DedicatedServerEngine.ini) |
| P2 | 넓힌 회귀에서 구형 리타깃 구성 전제와 스트리밍 재진입 fixture 오류를 발견했다. `RouteEndPlay(RemovedFromWorld)`는 컴포넌트를 해제하며, 이어서 `DispatchBeginPlay`만 호출하면 Mover backend가 초기화되지 않는다. | 테스트에서 `ULevel::RouteActorInitialize`의 컴포넌트 초기화 순서를 재현했다. 실제 Spellblade의 직접 재생과 합성 follower 교체의 두 계약을 각각 검증한다. [Reaudit 테스트](../../../Source/Project_JCharacter/Private/Tests/Project_JReauditContractsTests.cpp) |

### 감사에서 유지한 계약

- UE 5.8은 엔진의 첫 유효 우선순위기 정의를 공간 기본 핸들 0에 배치한다. 프로젝트는 `+`로 5개를 추가한다. Sphere 우선순위와 소유자 부스트는 스케줄 점수다. 신규 테스트에서 실제 Iris의 `DefaultSpatial=0`, 5개 유효·서로 다른 핸들, CVar 복귀·재활성화, 복제 해제 후 8틱 뒤 재등록, 새 MultiPart 등록/해제, 사망 전환을 확인했다. 해당 실험은 일반 게임 NetDriver를 사용한다. [Iris 런타임 테스트](../../../Source/Project_JCharacter/Private/Tests/Project_JMassiveScaleThreadingTests.cpp)
- MultiPart는 시각이 오름차순인 논리 링에서 첫 `time >= query`를 찾는다. 단일·2샘플, 오래된/최신 경계와 중복 시각이 있는 링을 포함한 **197,478건의 오프라인 대조**에서 이전 선형 탐색과 같은 보간 구간을 골랐다. 런타임 회귀의 링 래핑 검증도 유지했다.
- 파티 주사위는 해결 후 같은 ID의 재해결을 거절했다. 기존 C++에서 해결 상태를 다시 읽는 호출자는 확인되지 않았다. 만료 청소는 다음 주사위 생성에 의존한다. 16,384건 로컬 보상 영수증은 FIFO로 제한되며 운영 보상의 지속적인 멱등성은 PostgreSQL 명령이 담당한다.
- 원격 거리 컬링은 선택적 플레이어 단발에 적용한다. 그 공격 중 거리 밖에서 소진한 단발은 다시 가까워져도 소급 재생하지 않는다. 보스 경고·루프는 별도 수명 경로다. 저장된 군중 효과 설정은 `SetByGameSetting`이 `SetByScalability`보다 높아 그래픽 프리셋의 군중 항목 값을 덮어쓴다. 초기화가 저장값 유무와 관계없이 `Apply()`를 호출하므로 새 프로필의 기본값도 해당 두 프리셋 항목을 덮어쓴다. P2 후속 항목으로 프리셋을 따르는 기본값과 사용자가 명시한 개별 설정을 구분한다. [그래픽 설정](../../../Source/Project_J/UI/Project_JGraphicsSettings.cpp)
- 오디오 위치는 World와 프레임으로 구분한 스냅샷이다. 같은 프레임의 카메라 이동은 다음 프레임에 반영된다. 정상 재생 종료는 콜백에서 즉시 음성 집계에서 제거된다. 시작 실패처럼 완료 콜백이 오지 않는 음성은 다음 입장 정리에서 회수한다. HUD는 초기 지오메트리가 0이어도 Canvas 크기가 바뀌는 Tick에서 레이아웃·스케일을 재적용한다.

### 24인 Iris 전용 서버 실측

같은 호스트에서 전용 서버와 ThinClient 생성기를 코어 집합별로 분리했다. 시작 시 확인한 물리·커밋 여유의 최솟값은 14,727 MiB였고, 실행 중 최소 물리 여유는 4,728 MiB, 최소 커밋 여유는 7,618 MiB였다. 테스트 중 메모리 예약 기준 4,096 MiB를 지켰다. 검증 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2Independent_FieldIris_20261125/verification.json`) · 서버 로그 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2Independent_FieldIris_20261125/Server.log`)

| 결과 | 수치 |
| --- | --- |
| 연결 / 확정 타격 / 체력 반영 확인 / 정상 종료 | 24 / 24 / 24 / 24 |
| 서버 권위 보스 체력 | 25,000 → 24,760 |
| 설정 틱률 / Burst 시작 간격 p95 | 30 Hz / 34.053 ms |
| Idle 월드 작업 p95 | 5.461 ms |
| Burst 월드 작업 p95 / p99 / 최대 | 6.372 / 7.380 / 8.277 ms |
| Burst 33.3 ms 초과 / 한 틱 최대 타격 | 0 / 15 |
| Burst 입력 요청부터 응답 p95 / 500 ms 초과 | 630.062 ms / 24건. 이전 24인 동일 시나리오는 632.904 ms / 24건 |
| Burst 서버 수신 / 송신 | 130.45 / 2,280.02 KiB/s |
| `SetPrioritizer()` 성공 | 플레이어 24명이 각각 Mover(2)·CombatMode(4)·CombatActive(3)에 도달. 실제 필드 보스 1개가 Boss(6)에 도달. Dead는 별도 Iris 런타임 테스트에서 관측 |

서버 런타임은 DDoS 탐지 1과 analytics 0, `NetServerMaxTickRate=30`, `MaxClientRate=100000`, `net.MaxRPCPerNetUpdate=32`, PSO 사전 컴파일·ABA·FootPlacement·느린 스트리밍 블로킹 0, 배치 틱·비동기 컴포넌트 틱 1 및 비동기 디스패치·정리 0을 보고했다. 부팅 시 Iris의 활성 ReplicationSystem 중 모듈 등록 경고가 한 차례 있었고 이번 실행에서 이후 복제·타격 실패는 없었다. 단일 IP 정상 코호트에서 탐지 오탐으로 인한 접속 차단은 관찰되지 않았다. 검증은 이 코호트 크기와 같은 트래픽에 한정된다.

월드 작업 시간과 틱 시작 간격은 서로 다른 지표다. 입력 요청부터 응답까지는 공격 몽타주에서 서버 피해가 확정될 때까지의 시간도 포함한다. 송신량과 응답 지연은 확대 실험에서 몽타주·피해 처리·복제 큐의 시간을 나눠 측정한다.

### 완료 검증과 남은 범위

- **빌드와 경계.** direct UBT `Project_JEditor Win64 Development` 성공. 첫 대상 빌드와 fixture 수정 후의 마지막 증분 빌드가 모두 종료 코드 0이었다. 아키텍처 검사에서 8모듈 DAG, 205 기능, 20 도메인을 확인했다. 마지막 빌드 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2IndependentFinalBuild_20261125/UBT.log`)
- **확장 자동화.** 최초 필터에서 발견된 202건과 별도 표현 수명 회귀 3건이 통과했다. 초기 RHI 실행은 128건을 완료한 뒤 재진입 fixture에서 종료됐다. 해당 fixture 및 오래된 애니메이션 경고 기대를 바로잡고 부족한 항목을 다시 실행했다. 같은 런타임 코드에서 원본 로그와 이후 두 개 결과를 합산한 **고유 테스트 205/205**다. Gateway 입장·종료, 요청 한도, 실제 루프백 HTTP, 실제 Iris, Niagara 풀/경고, 사망·재진입, UMG/HUD, 전용 서버 MontageTiming, AuthorityHitOrigin, MultiPart 회귀를 포함한다. 테스트별 근거 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2IndependentFinal_20261125/Wave2RegressionSummary.json`)
- **영속 저장.** SQLite에서 실제 인벤토리 명령 12,289건과 재로드를 통과했다. 별도 스트림에서 16,384건을 처리하고 2개 레코드와 65개 영수증으로 제한되는 것을 확인했다. 새 로컬 PGlite DB에서 마이그레이션과 보상·우편·리스/멱등·재시작 회귀를 통과했다. Nakama 서비스 포트 계약과 로컬 Gateway HTTP도 통과했으며 별도 native Nakama와 PostgreSQL 실행 파일을 쓰는 `NakamaRealMailClaim` 종단 테스트는 이번 환경에서 실행하지 않았다. SQL 결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2Independent_SQL_20261125/Run.log`)
- **그래픽/UI.** uncooked D3D12 standalone에서 그래픽 UI 적용, 저장, 값 재로드와 관련 CVar를 확인했다. 전체 `ProjectJUIRuntimeSmoke`는 `RequiresCookedData()`가 성공 전제라 이번에는 검증 범위에서 빠졌다. 다인원 카메라의 실제 FPS는 미측정이다. 그래픽 스모크 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave2Independent_GraphicsSmoke_20261125/Run.log`)
- **원자료.** 검증 결과·로그·CSV 중 17개를 기존 중앙 보관소에 등록했다. 기존 경로와 중앙 파일이 동일한 NTFS 파일을 가리키며 크기와 SHA-256이 일치하는 것을 확인했다. 원래 경로가 없을 때의 복원은 [중앙 보관 규칙](../../Maintenance/Local_Archive_Policy.md)을 따른다.
- **판정.** 로컬 Wave 2 조건부 통과. 동일 NetDriver의 복제 해제·재등록까지 관찰했다. 실제 seamless travel, 드라이버 교체, 100~200인 이상의 반복 교전, 장시간 엔진 상주, 패키지 전용 서버, DDoS 오탐 한계, 두 보스 동시 보상은 미검증이다. Iris 부팅 경고와 활성 복제 중 serializer 등록 순서는 고인원 검증에서 재확인한다.

### 다음 고인원 실험의 우선순위

1. **온라인 보상의 배치와 재처리.** 한 보스 처치의 보상·퀘스트 제출은 최대 수백 건이고, 두 보스와 저장 요청이 겹치면 512 요청 한도를 넘을 수 있다. 현재 보상 실패는 경고로 남는다. 기존 멱등 키를 쓰는 배치·제출 속도 조절·실패 기록의 재처리가 최우선이다. 2개 인카운터와 인벤토리 저장을 겹쳐 DB 수신·중복·누락을 비교한다. [인카운터 제출 경로](../../../Source/Project_J/World/Project_JWorldEncounter.cpp)
2. **전투 표현 RPC 및 Iris 대상별 비용.** `ForceNetUpdate`는 복구 상태 게시에서 발생하며 모든 단발 큐가 이를 호출하는 것은 아니다. 반면 단발·종료·경고에는 별도 멀티캐스트가 있다. 경고 전달 계약을 고정하고 각 경로의 호출수, 연결별 전송 바이트·큐 지연, 의미 없는 상태 전송을 분리한다. [표현 복제](../../../Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp)
3. **동시 몽타주로 인한 원격 ABA 이탈.** 클라이언트 몽타주 보호는 사라지면 안 되는 노티파이·루트 모션을 지킨다. 정지 군중 대비 동시 공격에서 보호 메시 수, 본 평가, Cloth, GPU p95를 동일 카메라로 측정한 뒤 펄스 형태를 판단한다. [애니메이션 정책](../../../Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp)
4. **다중 표적 근접·Mover·Chaos와 AI.** 방금 검증한 기본 공격 한 종류보다 광역 다중 표적, 동시 이동, 복수 보스의 위협/사거리 갱신이 다르다. 스윕/LOS 수와 GAS 처리·게임 스레드 p99를 같은 조건에서 비교한다.
5. **대규모 접속 수명과 GC·역직렬화.** PlayerState의 여러 서브오브젝트가 누구에게 실제 복제되는지 먼저 수신 측에서 계측하고, 연결 생성/종료의 할당·GC와 GT JSON 완료 콜백을 분리한다. 분리 호스트의 반복 입퇴장 결과로 우선순위를 정한다.

## Wave 3: 200인+ 동시 보스 보상 멱등 Outbox·그래픽 프리셋 우선순위·원격 군중 VFX/ABA 최적화 · 2026-11-25

Wave 2 감사의 후속으로 페이싱·재시도를 갖춘 온라인 보스 보상 큐, 그래픽 프리셋 상속과 클라이언트 군중 표현 경로를 구현했다. 아래는 1차 구현·실측 기록이며, 이어지는 Wave 3 독립 감사에서 수명과 검증 범위를 다시 확인했다.

### 적용 내용

1. **서버 멱등 Durable Outbox (`UProject_JOnlineWorldSubsystem` + `AProject_JWorldEncounter`)**
   - `Source/Project_J/Backend/Project_JOnlineWorldSubsystem.h/.cpp`: 최대 4,096건 메모리 큐와 전송 동시성 64건, UTF-8 JSON 사전 직렬화, 동일 `StableIdempotencyKey`로 최대 5회 재시도(`0.25→0.5→1→2→4s`), `COMMITTED`/`REPLAYED` 확정 및 `INVALID`/`KEY_REUSED`/`STALE`/`CONFLICT` 종료. 보상 큐 자체의 상한은 Gateway의 나머지 소비자에 대한 별도 예약을 제공하지 않는다.
   - `Source/Project_J/World/Project_JWorldEncounter.cpp`: 온라인 필드 보스 보상을 단발 `Submit()`에서 `EnqueueDurableMutation()`으로 전환했다. 400건의 접수·재시도·확정은 합성 시험으로 검증했다.
2. **그래픽 프리셋 vs 커스텀 군중 VFX 오버라이드 분리 (`UProject_JGraphicsSettings` + `Widget`)**
   - `Source/Project_J/UI/Project_JGraphicsSettings.h/.cpp`, `Project_JGraphicsSettingsWidget.h/.cpp`: `bUseCustomOtherPlayerEffects`(기본 `false`) 추가. 신규 프로필·프리셋 전환 시 UE 5.8 `IConsoleVariable::Unset(ECVF_SetByGameSetting)`으로 `DefaultScalability.ini` `[EffectsQuality@0..Cine]` 값이 덮어씌워지지 않게 보존하고, 사용자가 직접 군중 설정을 변경하거나 기존 저장 프로필인 경우에만 `ECVF_SetByGameSetting` 적용.
3. **원격 군중 전투 표현 캐시 통합 및 오프스크린 몽타주 본 평가 절감**
   - `Source/Project_JCharacter/Public/Components/Project_JCombatPresentationComponent.h/.cpp`: `GetCachedWeaponPresentation()` 헬퍼를 추출해 `BeginAttackPresentation`, `EndAttackPresentation`, `ApplyReplicatedState`, `MulticastPlayPresentationCue`, `PlayCueLocal` 5곳 모두의 `FindComponentByClass` 스캔 제거.
   - `Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp`: `Project_J.Anim.RemoteProxyMontageTimingOnly`(기본 `1`)를 추가했다. 클라이언트 원격 플레이어가 `MontageTiming`을 요청할 수 있으며 모델별 절감 여부는 포즈 소스와 기존 정책에 따라 달라진다.

### 검증 결과

- **아키텍처·빌드**: `Validate-MMOArchitecture.py` 통과(8모듈 DAG 유지, 205 features / 20 domains), Direct UBT `Project_JEditor Win64 Development` 성공(**0 errors, 0 warnings**).
- **확장 회귀 테스트** (index.json (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3Reg_20261125_v2/index.json`)): **120/120 통과, 0 failures, 0 warnings** (21.58s).
  - 신규 `ProjectJ.Online.DurableOutboxBurstAndRetry`(동시 2보스 400건 보상 64 페이싱 + `Overloaded`/타임아웃 멱등 재시도 400/400 확정 + `INVALID` 1회 즉시 실패), `ProjectJ.UI.GraphicsSettingsPresetVsCustomOverride` 포함.
- **실접속 24인 Iris Field Boss 검증** (verification.json (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3_Field24_Iris_20261125/verification.json`)): 확정 타격/체력 확인/정상 종료 **24 / 24 / 24**(`25,000→24,760`), 틱 시작 간격 p95 **34.049ms**, Idle p95 **5.331ms**, Burst `worldTickMs` p50 **4.524ms** / p95 **5.817ms** / p99 **7.141ms**(`strikeTickMs` p95 **5.522ms**, `over33ms=0`, `worldTickBudgetMet=true`).
- **D3D12 그래픽 설정 UI 스모크** (Run.log (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3_GraphicsSmoke_20261125/Run.log`)): **`EXIT=0`, `PROJECT_J_GRAPHICS_SETTINGS_SMOKE success=1`**.
- **미검증**: 실제 PostgreSQL/Nakama 2보스 동시 처치 종단 부하, 200인 클라이언트 RHI 오프스크린 몽타주 FPS 비교, UE 5.8 소스 빌드 256슬롯 및 호스트 분리 실측.

### Wave 3 독립 감사

| 우선순위 | 관찰과 조치 |
| --- | --- |
| P1 | Gateway는 미설정·포화 시 콜백을 동기 반환할 수 있다. 완료마다 큐 처리가 재귀적으로 호출되던 경로를 제거하고 게임 스레드 단일 펌프, 시도별 일회 완료, 펌프당 최대 64건을 적용했다. 전송 시각과 접수 순서로 정렬하는 힙의 push/pop은 O(log n)이며 전체 배열을 앞에서 매번 이동하지 않는다. [보상 큐](../../../Source/Project_J/Backend/Project_JOnlineWorldSubsystem.cpp) |
| P1 | 입장 시 확인한 월드의 파괴가 접수된 미전송·재시도 보상을 지울 수 있었다. 접수된 작업의 소유 범위를 GameInstance로 맞췄다. 큐 거절은 호출 결과로 전달하고, GameInstance 종료 시 남은 미확정 수를 기록한다. [보스 처치 호출부](../../../Source/Project_J/World/Project_JWorldEncounter.cpp) |
| P1 | 실제 ServicePort에서 Gateway의 전송 전 거절 일부가 `Unavailable`로 합쳐졌다. Gateway 미설정·유효성 실패·발송 실패 등을 `PRE_DISPATCH_REJECTED`로 전달하고, HTTP 전송 뒤 응답이 불명확한 경우의 재시도를 구분했다. [ServicePort](../../../Source/Project_J/Backend/Project_JNakamaServicePort.cpp) |
| P1 | 실제 D3D12 설정 화면에서 UE가 군중 VFX 프리셋 값의 적용을 거부했다. 두 CVar 등록에 `ECVF_Scalability`가 없어 정적 설정과 직접 Set 테스트로는 재현되지 않았다. 두 변수를 Scalability 용도로 등록하고 INI 허용 여부와 실제 UI 적용을 함께 검증했다. [CVar 등록](../../../Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp) |
| P1 | UE `USkeletalMeshComponent`는 망토나 리타깃 결과가 화면에 있어도 숨은 소스 메시의 최근 렌더 상태로 몽타주 전용 여부를 판단한다. 보이는 소스와 가시성 기반 포즈 정책에 한해 원격 `MontageTiming`을 적용한다. Full pose 요청이 우선하며 설정 변경과 가시성 변화도 재평가한다. 저장된 Spellblade의 소스는 숨겨져 있고 기본 정책은 full pose여서 현재는 보호 경로를 유지한다. [메시 정책](../../../Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp) |
| P2 | 프리셋 모드 초기화도 동일한 적용 함수를 거쳐 이전 프로필/PIE가 남긴 `SetByGameSetting` 값을 해제한다. UE 5.8 `TConsoleVariable::Unset`은 우선순위 기록의 남은 최고 값을 즉시 설치한다. Effects 행만 수정한 경우 명시적 커스텀 값은 유지하고, 전체 프리셋 전환에서 군중 값을 따로 편집하지 않으면 프리셋을 따른다. [그래픽 정책](../../../Source/Project_J/UI/Project_JGraphicsSettings.cpp) |

`FProjectJOutboxGatewayHTTPTest`는 Unreal의 실제 Nakama ServicePort와 Gateway를 127.0.0.1 TCP 응답기에 연결했다. 미설정 URL의 동기 실패, Gateway 한도 포화, HTTP 503 뒤 `REPLAYED`를 확인했다. 두 실제 요청은 서로 다른 전송 ID, 같은 멱등 키, 같은 UTF-8 인텐트를 보냈다. 별도 SQL 회귀에서는 실제 Nakama 보상 RPC 함수의 SQL과 바인딩을 PGlite에서 실행했다. `(defeat_id, character_id)` 영수증으로 첫 2통의 우편을 확정했고, DB 재시작 후 같은 우편 ID를 반환했으며 변경 페이로드는 `KEY_REUSED`였다. 기존 보스·던전 보상·우편·리스 회귀도 통과했다. [HTTP 테스트](../../../Source/Project_J/Private/Tests/Project_JGatewayHTTPTests.cpp) · PGlite 로그 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3Independent_SQLWire_20261125/Run.log`)

최대 4,096건 재진입 시험은 소스 월드 종료 뒤까지 접수분을 처리하고 중복 콜백을 무시했다. 타임아웃은 최초 1회와 재시도 5회까지만 진행한다. 큐는 프로세스 메모리에 있으므로 서버가 재시작하거나 재시도 한도를 소진한 작업의 복구에는 영속 작업 기록이 필요하다. 온라인 퀘스트 진척은 여전히 별도 직접 제출이며 보스 2개와 서로 다른 기여자 400명이 겹치면 이론상 보상 64 + 퀘스트 400건까지 동시에 경쟁할 수 있다. 던전 클리어는 별도 인스턴스 키의 불변 본문을 워커 생존·유효 기간에만 재시도한다. [던전 클리어](../../../Source/Project_J/World/Project_JDungeonInstanceSubsystem.cpp)

원격 표현 다섯 소비 지점이 무기 표시 캐시를 공유하고 기존 등록·해제에서 무효화한다. UE는 오프스크린에서 몽타주와 슬롯 노티파이를 진행하고 AnimGraph 노티파이를 건너뛴다. 보이는 소스에서 엔진 포즈 갱신으로 돌아오는 계약을 별도로 확인하며, 실제 원격 공격의 첫 가시 프레임 무기 트레일·Cloth 화질은 다인원 렌더링 증거가 남아야 한다.

#### 빌드·확장 회귀·그래픽 설정

- 설치 UE 5.8 Direct UBT의 `Project_JEditor Win64 Development` 성공, 컴파일 오류·경고 0건. 아키텍처 검사에서 8모듈 DAG, 205 기능, 20 도메인을 확인했다. 마지막 빌드 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3IndependentFixtureBuild_20261125/UBT.log`)
- 최초 120건에서 빠진 Gateway 실제 HTTP, 영속 저장, 무기·장비, Reaudit/SourceAudit, 원격 군중 애니메이션·표현 테스트를 추가했다. **고유 225/225 통과.** 첫 확장 실행에서 장비 Instant 효과 거절을 기존의 스탯 대체 적용으로 잘못 기대한 fixture가 실패했다. 거절과 정상 대체 적용을 각각 검증하도록 수정했다. 재로드 fixture는 UE 월드 종료 순서를 바로잡았다. 두 시험을 같은 런타임 코드에서 재실행했다. 남은 경고는 Gateway 실제 HTTP의 고의 초과 응답 거절을 기록한 libcurl 로그다. 테스트별 결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3IndependentFixtureRegression_20261125/Wave3RegressionSummary.json`)
- D3D12 설정 창을 통해 Effects 행만 변경했을 때의 커스텀 유지, 전체 프리셋 변경에 따른 커스텀 해제, 0.35/0.65/0.85의 수치 왕복과 재적용을 확인했다. 두 번째 프로세스에서 같은 프로필의 `False` 플래그와 Effects@3의 0.85를 재확인했다. 설정 적용 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3Independent_GraphicsFinal_20261125/Settings.log`) · 프로필 재시작 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3Independent_GraphicsFinal_20261125/Restart.log`)
- 원격 몽타주 회귀는 실제 Spellblade BP와 공격 몽타주를 PIE의 원격 역할로 실행했다. 시험에서 소스를 보이게 한 뒤 오프스크린 몽타주 진행과 렌더 진입 시 갱신 허용, 긴급 애니메이션 요청과의 공존, 소스 숨김·CVar 변경·역할 변경 시 full pose 복귀, 마지막 몽타주 종료 시 원래 설정 복원을 확인했다. 저장된 Spellblade의 숨은 소스는 full pose 보호를 유지한다. [몽타주 회귀](../../../Source/Project_JCharacter/Private/Tests/Project_JUpdateOwnershipTests.cpp)

#### 24인 Iris 실접속과 확인된 범위

동일 호스트의 NullRHI 전용 서버와 ThinClient를 코어 집합별로 분리했다. 물리 메모리 최소 여유는 4,472 MiB, 커밋 최소 여유는 7,760 MiB였다. 둘 다 종료 코드 0이었다. 24인 실측 결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3Independent_FieldIris_20261125/verification.json`)

| 항목 | 재측정 |
| --- | --- |
| 연결 / 확정 타격 / 클라이언트 체력 확인 / 정상 종료 | 24 / 24 / 24 / 24 |
| 권위 서버 보스 체력 | 25,000 → 24,760 |
| 설정 틱률 / Burst 틱 시작 간격 p95 | 30 Hz / 34.053 ms |
| Idle 월드 작업 p95 / Burst p95 / p99 / 최대 | 5.197 / 5.362 / 7.068 / 7.496 ms |
| Burst 33.3ms 초과 / 입력 요청부터 응답 p95 | 0틱 / 627.513 ms |

코호트는 보스 피해와 복제를 검증하고 살아 있는 보스 체력에서 종료한다. 보스 처치 후 보상 동시 제출이나 GPU 부하와 섞이지 않는 실험이다. Wave 2와 Wave 3의 수치 차이를 단일 코드 변경의 성능 효과로 귀속하려면 별도 A/B가 필요하다.

- D3D12 관측 프로세스를 포함한 12인 실접속은 접속 전 물리 여유 4,067 MiB에서 4,096 MiB 보호선에 걸려 종료됐다. 해당 구성의 FPS와 연결 결과는 미검증이다. 메모리 중단 기록 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave3Independent_RenderedIris_20261125/verification.json`)
- 별도 단일 D3D12 게임 프로세스에서 실제 Spellblade 24명, 로컬 플레이어 1명과 이동·동시 몽타주를 1280×720로 검증했다. 정상 종료와 5,623프레임을 기록했다. 공격 단계에서 최대 24개 몽타주가 동시에 재생됐으며, 캡처 요청 프레임과 구간 첫 0.25초를 제외한 프레임 시간 p50/p95는 **8.491/11.026 ms**였다. 단순 로컬 장면이므로 원격 트레일 지연과 서버 동시 구동 시 FPS는 여기서 확인할 수 없다. 검증 (로컬 `Saved/Validation/WholeProjectAudit_20261013/MassiveScaleWave3Independent_rendered24_20261125/verification.json`) · 프레임 원자료 (로컬 `Saved/Validation/WholeProjectAudit_20261013/MassiveScaleWave3Independent_rendered24_20261125/frames.csv`)
- **판정은 로컬 조건부 통과.** 보상 큐는 월드 종료를 견디고 서버 프로세스 재시작 전까지 유지된다. 프로세스 손실 이후 미전송 보상 복구, 실제 Nakama/PostgreSQL 동시 처치, 100~200인 네트워크 교전과 원격 화질 A/B는 검증 범위에 남는다. 재현 로그·수치·캡처는 기존 로컬 중앙 보관소에 SHA-256을 확인해 등록했다. [복원 규칙](../../Maintenance/Local_Archive_Policy.md)

#### Wave 1~3 종합 잔여 과제

1. 보상 의도의 프로세스 재시작 복구와 재시도 한도 이후 재처리를 우선한다. 기존 SQL 멱등 영수증을 사용하고, 서로 겹치는 보스 처치·퀘스트 진척·인벤토리 쓰기·던전 종료에서 누락과 중복을 실제 Nakama/PostgreSQL로 확인한다.
2. 부하 생성기와 서버를 분리해 100~200명의 반복 교전과 동시 이동을 검증한다. 설치형 Iris 슬롯 제약을 넘는 규모는 소스 빌드에서 별도로 확인한다. 피해 응답은 몽타주, 권위 처리, 복제 지연으로 나눠 측정한다.
3. 숨겨진 포즈 소스를 쓰는 실전 캐릭터는 리타깃·모듈러 몸체·무기 접촉·Cloth의 가시성 소유권을 먼저 확인한다. 실제 원격 클라이언트의 카메라 진입 시점과 동시 몽타주를 관측해 Iris 전투 이펙트 복제, ABA 이탈, 본 평가, Cloth와 GPU 비용을 구분한다.
4. 드라이버 교체·seamless travel·반복 입퇴장과, 큰 보상·전투 부하의 GC/GT 역직렬화, 24시간 상주 검증은 별도 운영 게이트로 남긴다.

---

### Wave 4 · 크래시 안전 영속 Outbox 및 재시작 복구 (`2026-11-25`)

Wave 3 독립 감사에서 최우선 운영 잔여 과제로 지목된 **서버 프로세스 종료/크래시 이후 미전송 보상 복구 및 데드레터 재처리**를 구현했다. 아래 125건과 24인 수치는 1차 기록이며 최종 판정은 독립 감사 결과를 따른다.

#### 1차 구현 및 아키텍처

| 영역 | 변경 파일 | 핵심 설계 |
| --- | --- | --- |
| **SQLite 영속 저장소** | [Project_JOnlineOutboxStore.h](../../../Source/Project_J/Backend/Project_JOnlineOutboxStore.h), [Project_JOnlineOutboxStore.cpp](../../../Source/Project_J/Backend/Project_JOnlineOutboxStore.cpp) | 프로젝트 기존 Win32 롤백 저널 VFS(`FProject_JProfileDatabase`, `journal_mode=DELETE`, `synchronous=FULL`, `application_id='PJOB'`, `user_version=1`, `PRAGMA quick_check`) 위에 `outbox_entries(idempotency_key PK, created_seq, service, operation, payload, debug_context, state, total_attempts, last_status, updated_utc_ms)` 구축. `TUniquePtr` 피임플로 SQLite 헤더 누출 차단. 200인 보스 처치 폭주를 단일 `BEGIN IMMEDIATE ... COMMIT` 배치 트랜잭션으로 커밋하고, 데드레터는 FIFO 상한(`MaxDeadLetterEntries=16384`)으로 bounded 유지. |
| **비동기 단일 파이프라인 · 복구 · 리드라이브** | [Project_JOnlineWorldSubsystem.h](../../../Source/Project_J/Backend/Project_JOnlineWorldSubsystem.h), [Project_JOnlineWorldSubsystem.cpp](../../../Source/Project_J/Backend/Project_JOnlineWorldSubsystem.cpp) | 모든 SQLite I/O를 `Async(EAsyncExecution::ThreadPool, ...)` + `TFuture<void>` 단일 워커 파이프라인으로 격리해 GT 블로킹·인라인 retract를 원천 차단(`check(!IsInGameThread())` 검증). **쓰기 선행(write-ahead) + fail-closed**: SQLite 배치 커밋이 성공한 항목만 네트워크 발송 큐로 승격하며 디스크/VFS 오류 시 네트워크 미발송 후 실패 보고. 온라인 전용 서버(`IsRunningDedicatedServer()` + 비-Mock 모드)에서 `Saved/OnlineOutbox/ServerOutbox[_<Port>].sqlite` 자동 활성화(CLI `-ProjectJOnlineOutboxPath=` 및 INI `OnlineOutboxDatabasePath` 우선), PIE/단위 테스트 순수 메모리 계약 100% 보존. `ActiveOutboxKeys` 동시 중복 차단, 시작 시 `state=0` FIFO 자동 복구, 영구 실패(`INVALID`/`KEY_REUSED`/`STALE`/`CONFLICT`/재시도 5회 소진) `state=2` 격리, 운영자 리드라이브 API(`RedriveDeadLetters`) 및 콘솔 명령 `ProjectJ.Online.RedriveOutboxDeadLetters` 제공. |
| **Wave 4 자동 회귀 테스트** | [Project_JWave4OutboxPersistenceTests.cpp](../../../Source/Project_J/Private/Tests/Project_JWave4OutboxPersistenceTests.cpp) | `ProjectJ.Online.OutboxCrashRestartRecovery`(64건 폭주 → 20건 확정/44건 미확정 재시작 → 3회 부트 왕복으로 정확히 44건 FIFO 복구·UTF-8 페이로드 비트 일치·`REPLAYED` 확정·잔여 0건 확인) 및 `ProjectJ.Online.OutboxDeadLetterRedriveAndFaultClose`(`INVALID` + 6회 타임아웃 데드레터 격리·재시작 비자동재생·중복 키 중복제거·실제 VFS 쓰기 오류 주입 시 네트워크 미발송 fail-closed·명시적 `RedriveDeadLetters()` 재전송 확정) 추가. |

#### Wave 4 1차 검증 결과

- **아키텍처 DAG 검사**: `python -B Scripts/Validation/Validate-MMOArchitecture.py` → `passed: true` (8모듈 DAG, 205 기능, 20 도메인 무결성 확인).
- **직접 UBT Editor 빌드**: `UnrealBuildTool.exe Project_JEditor Win64 Development` → **성공 (오류 0, 경고 0, 30.08s)**.
- **확장 회귀 스위트** (Automation.log (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4Reg_20261125/Automation.log`)): **125 / 125 통과 (`FAILED=0, NOTRUN=0`)**. Outbox 5종(`DurableOutboxBurstAndRetry`, `OutboxCrashRestartRecovery`, `OutboxDeadLetterRedriveAndFaultClose`, `OutboxGatewayHTTP`, `OutboxSynchronousReply`) 전원 통과.
- **실접속 24인 Iris 필드 보스 재검증** (verification.json (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4_Field24_Iris_20261125/verification.json`)): 연결/확정 타격/체력 확인/정상 종료 **24 / 24 / 24 / 24** (`25,000 → 24,760`, `serverExit=0`, `generatorExit=0`), Idle `worldTickMs` p95 **4.835 ms**, Burst `worldTickMs` p50 **4.198 ms** / p95 **5.377 ms** / p99 **6.675 ms** / 최대 **7.572 ms** (`over33ms=0`, `worldTickBudgetMet=true`, 틱 시작 간격 p95 **34.057 ms**).
- **남은 인프라 규모 게이트**: 실제 Nakama/PostgreSQL 다중 보스·퀘스트·인벤토리·던전 동시 부하, 소스 빌드 UE 5.8 256슬롯 Iris 및 호스트 분리 100~200인 교전, 숨은 포즈 소스 원격 화질 A/B, 24시간 상주·seamless travel 게이트는 별도 운영 인프라 검증 단계로 유지한다.

#### Wave 4 독립 감사

| 우선순위 | 문제와 수정 |
| --- | --- |
| P1 | 새 접수와 확정 삭제·격리가 한 배치에서 롤백되면 정리 작업도 사라졌다. 최종 작업의 키와 용량을 SQLite 커밋까지 유지하고, 롤백 시 새 작업은 송신 없이 실패 처리하며 기존 정리만 백오프로 재시도한다. 정리 재시도로 HTTP를 다시 호출하지 않는다. |
| P1 | 복구가 늦는 동안 같은 키가 접수되면 `INSERT OR IGNORE`로 디스크 본문을 그대로 두고도 새 본문이 송신될 수 있었다. 로드와 쓰기 모두 디스크의 키·서비스·작업·본문을 대조하고, 충돌 시 먼저 커밋된 본문만 사용한다. 격리된 키는 명시적 리드라이브 전까지 송신하지 않는다. |
| P1 | 초기 복구가 4,096건에서 멈췄고, 리드라이브가 작업 중인 슬롯을 새 접수가 차지할 수 있었다. 고유 `created_seq` 기준으로 가용 용량만큼 다음 페이지를 읽고 오래된 대기가 남으면 신규 접수를 미룬다. 리드라이브는 SQLite 작업 시작 전에 슬롯을 예약한다. 데드레터 16,384건에서 이전 기록을 삭제하던 정책을 신규 접수 차단으로 변경했다. 이미 들어온 작업은 격리까지 진행한다. |
| P1 | 실제 SQLite 커밋 결과를 기다리지 않은 콜백과 경로 교체가 같은 세대를 공유했다. 저장소 경로는 GameInstance 수명 동안 고정하고 같은 경로 재호출은 그대로 둔다. 종료 시 세대를 무효화하고 워커와 최종 커밋 결과를 확인한다. 오픈 실패와 워커 비활성 환경은 메모리 전송으로 넘어가지 않는다. |
| P1 | 0/0 헤더의 기존 외부 DB를 초기화하거나 두 프로세스가 같은 경로를 열 수 있었다. Win32 VFS의 SQLite 배타 잠금, 실제 빈 스키마·헤더 확인, DELETE 저널 및 `quick_check`로 열기 조건을 확인한다. 손상되거나 소유권이 일치하지 않는 파일은 유지한 채 핸들을 닫는다. |
| P2 | 잘못된 콘솔 인자를 기본 전체 리드라이브로 해석하던 동작을 사용법 출력으로 변경했다. 활성 키의 중복 접수는 기존 반환 계약대로 `false`이며 계측 카운터에 기록한다. |

정상 틱의 SQLite 열기·읽기·쓰기·종료는 단일 스레드풀 작업으로 직렬화한다. UE 5.8 `Async.h`의 비멀티스레드 인라인 분기를 피하기 위해 스레드풀이 없는 환경에서는 접수를 막는다. 정상 종료에서는 마지막 디스크 작업의 완료를 기다리며, 워커는 GT 완료 콜백을 기다리지 않는다. 저장소를 열지 못한 경우 와이어 송신 없이 종료하고 프로세스 재시작으로 복구를 재시도한다. [저장소](../../../Source/Project_J/Backend/Project_JOnlineOutboxStore.cpp) · [워커·종료](../../../Source/Project_J/Backend/Project_JOnlineWorldSubsystem.cpp)

SQLite 커밋 도중 Win32 VFS의 파일 동기화 직후 테스트 프로세스를 종료했고, 다음 프로세스에서 핫 저널의 존재와 전체 혼합 배치 롤백을 확인했다. 별도 실행에서 커밋 직후 종료했을 때는 신규 추가·확정 삭제·격리가 모두 유지됐다. 각 재오픈에서 `quick_check`를 통과했다. 종료 코드는 준비·강제 종료·롤백 확인·커밋 후 강제 종료·커밋 상태 확인 순으로 **0 / 99 / 0 / 99 / 0**이었다. 프로세스 기록 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentNative_20261125/processes.json`)

실제 전용 서버 시작 다섯 건에서 Mock은 기본 DB를 만들지 않았고, 비-Mock의 19142·19143 포트는 서로 다른 DB를 만들었다. INI를 지정하면 기본 경로를 건너뛰었고, CLI와 INI를 함께 주면 CLI 파일만 생성됐다. 생성된 DB의 `PJOB` 앱 ID, 스키마 버전 1과 `quick_check`를 외부 읽기로 대조했다. 경로 결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentPaths_20261125/paths.json`)

동일 호스트의 24인 NullRHI Iris 필드 보스 재측정에서 접속·확정 타격·체력 확인·종료 **24/24**, 보스 체력 25,000→24,760을 확인했다. Burst 월드 작업 p95/p99/최대는 **5.436 / 6.399 / 6.439 ms**, 33.3ms 초과는 0틱이었다. Burst 틱 시작 간격 p95는 34.069ms, 최소 가용 물리 메모리는 5,609MiB였다. 이 코호트는 전투·복제 기준선이며, 처치 후 보상 시험은 SQLite·Gateway 회귀에서 별도로 실행했다. 실측 원본 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4Independent_FieldIris_20261125/verification.json`)

#### 최종 마감 검증

- Direct UBT `Project_JEditor Win64 Development` 빌드 성공, 컴파일 오류·경고 0건. 8모듈 DAG와 205개 기능/20개 도메인 검사를 통과했다. 마지막 빌드 로그 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentShutdownBuild_20261125/UBT.log`)
- 확장 RHI 회귀 **233/233**, 실패·미실행 0. `ProjectJ.Online` 11건을 포함한다. 4,099개 대기 보상의 페이지 복구, 새 접수의 가용 슬롯 경쟁, 데드레터 한 자리 부분 리드라이브, 혼합 배치 롤백·재정리 및 외부/손상 파일을 확인했다. 실제 인벤토리 12,289건과 합성 저장 보존 한도 16,384건도 재통과했다. Gateway 의도적 초과 응답의 libcurl 경고와 군중 렌더링의 RHI 가상 예약 경고가 각각 기록됐다. 후자는 단독 RHI 시험에서 재현되지 않았다. 확장 회귀 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentExpanded_20261125/index.json`) · RHI 단독 재검증 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentRHICrowdCheck_20261125/index.json`)
- 종료 회귀를 보강하고 런타임 코드 변경 없이 다시 통과시켰다. 콜백이 GT에 도달하기 전에 종료를 시작한 상태에서 실패한 활성 쓰기와 대기 중 추가·삭제·격리 작업을 최종 배치로 커밋했다. SQLite 재오픈으로 두 대기 키와 두 격리 레코드를 확인했다. 종료 경합 회귀 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentShutdownCheck_20261125/index.json`)
- 실제 Nakama ServicePort → Gateway → 로컬 TCP/HTTP 경로에서 SQLite 커밋 이후 송신, 503 뒤 같은 멱등 키·본문 재시도, `REPLAYED` 한 번 확정, 로컬 삭제 커밋과 재오픈 대기·격리 0건을 확인했다. 별도 PGlite에서는 Nakama RPC가 생성한 SQL이 같은 보상 영수증·우편을 복구하고 충돌한 본문을 거부했다. [HTTP 회귀](../../../Source/Project_J/Private/Tests/Project_JWave4OutboxPersistenceTests.cpp) · SQL 결과 (로컬 `Saved/Validation/Continuation_20261014/MassiveScaleWave4Independent_SQL_20261125/Run.log`)

**판정은 로컬 최적화 사이클 종결 가능.** 내구성의 경계는 SQLite가 처음 커밋한 시점이다. `EnqueueDurableMutation()`의 성공 반환은 비동기 접수이며, 커밋 전에 프로세스를 잃으면 해당 요청은 복구할 수 없다. 실제 운영에서는 생산자 측의 영속 처치 ID/보상 의도, 파일의 볼륨 장애·호스트 교체 대비, 데드레터·용량·쓰기 실패 알림과 재처리 운영을 마련한다. 전용 서버의 실제 Nakama/PostgreSQL 동시 처치·퀘스트·던전 종료와 호스트 분리 100~200인 반복 교전은 운영 인프라 검증으로 이어진다.

### 로컬 최종 후속 · 영속 쓰기, Hidden Leader, 전투 지연 분리 (2026-11-25)

- **A-1 온라인 쓰기**. 보스 보상과 같은 SQLite Outbox가 퀘스트 진척과 던전 클리어의 저장·전송·재시도·격리를 소유한다. `World.Quest_Progress`는 서버가 획득한 델타를 캐릭터별 FIFO로 처리하며 현 세션의 직접 인벤토리·퀘스트 편집과 교차 전송하지 않는다. 직접 편집의 정확한 세션 검사, 세션·월드 수명 경계의 콜백 검사, 인벤토리·퀘스트 스냅샷의 단조 리비전을 유지한다. 마이그레이션 011은 종료된 세션에서 획득한 델타도 현재 캐릭터 행 잠금 아래 적용하고 원본 요청·키로 중복을 차단한다. 재생을 허용하는 6일 창은 7일 영수증 보존 기간보다 짧다. 운영 적용 순서는 스키마 011, Nakama 모듈, 게임 서버다.
- **던전 워커 경계**. 인스턴스 키와 보상 본문은 고정하며 DB가 기존 워커·만료·참가자 조건을 검사한다. 커밋된 클리어의 응답 유실은 `REPLAYED`로 확정된다. 커밋 전에 원 워커 리스가 만료되거나 인스턴스가 이관된 항목은 격리한다. 같은 월드·워커·인스턴스의 성공 콜백에서만 클리어 플래그와 참가자 인벤토리 재조회를 수행한다. SQLite 커밋이 크래시 복구의 시작점이며, 저장소 오류·용량 초과로 커밋되지 않은 항목은 복구 증거에 포함하지 않는다.
- **A-2 애니메이션**. UE 5.8의 스켈레탈 렌더 시계는 컴포넌트 로컬이다. 클라이언트의 원격 Hidden Leader 몽타주는 카메라 갱신 뒤 월드별 로컬 뷰를 공유해 실제 모듈러 파츠·부착 메시를 검사한다. 최근 렌더된 파츠, 여유 폭을 포함한 뷰 교차, 긴급 창, 뷰 미확인은 Full Pose를 유지한다. 몽타주 종료 시 저장된 visibility policy와 tick group을 복원한다. 로컬·서버와 기존 직접 가시 메시의 경로를 유지한다.
- **A-3 전투 관측**. `strikeRoundTripMs`의 기존 정의는 서버의 하네스 공격 요청 발행부터 서버 GAS 피해 확정까지다. 새 구간은 요청 발행→실제 타격 검증 진입, 첫 유효 검증 진입→GAS 체력 감소 확정(접촉까지의 후속 서버 틱 포함), 확정→공격자 클라이언트의 체력 복제 수신이다. 동일 Windows 호스트의 QPC를 사용한다. 여러 타격이 한 복제 업데이트에 묶이면 각 타격의 확정 체력을 처음 통과한 수신 시각을 사용한다. 타격 판정/타이밍은 기존 경로를 이용한다.

#### 24인 Iris 필드 보스 · 동일 호스트

- 서버 한 대와 ThinClient 생성기 한 대에서 24명 접속·타격 확정·체력 수신·정상 연결 종료까지 완료했다. 보스 체력은 25,000→24,760, 판정·검증 위반은 0건이다. 30Hz 예산을 충족했으며 Idle 월드 틱 p95 5.102ms, Burst p95 5.489ms / p99 5.936ms / 최대 6.060ms, Burst 33.3ms 초과 0회다. 클라이언트 생성기의 물리 메모리 여유 최저치는 4,962MiB였다.
- 각 구간은 24건 전부 수집했다. 아래 수치는 틱 대기를 포함한 경과 시간이다. 권위 구간의 ~100ms는 스윕 함수 단독 CPU 시간으로 해석하지 않는다.

| Burst 지연, ms | p50 | p95 | p99 | 최대 |
| --- | ---: | ---: | ---: | ---: |
| 공격 요청 → 첫 유효 타격 검증 | 497.892 | 530.978 | 531.000 | 531.000 |
| 첫 유효 검증 → 서버 권위 피해 확정 | 99.560 | 99.574 | 99.599 | 99.599 |
| 서버 확정 → 클라이언트 보스 체력 복제 수신 | 48.083 | 149.614 | 153.468 | 153.468 |
| 기존 `strikeRoundTripMs` | 597.453 | 630.552 | 630.599 | 630.599 |

원자료는 `Saved/Validation/Continuation_20261014/LocalFinalA123_Field24_Iris_20261125/verification.json`, `combat-strikes.csv`, `combat-ticks.csv`에서 확인한다.

#### 검증 근거와 범위

- 설치형 UE 5.8 `UnrealBuildTool.exe Project_JEditor Win64 Development` 전 범위 대상이 정상 종료했다. 컴파일러 오류·경고 각 0건이다. UBA의 기존 세션 폴더에 대한 접근 거부 메시지는 로그에 남겼다. 아키텍처 검증은 8모듈 DAG, 20도메인, 205기능에서 통과했다.
- 집중 UE 회귀 4/4 통과. 혼합 부하는 400캐릭터의 보스 보상 400건, 퀘스트 진척 401건과 던전 클리어 1건을 합쳐 802건을 SQLite에 커밋했다. 공유 64슬롯을 지키며 HTTP 503, 선전송 거부, 커밋 후 응답 유실을 모델링했다. 재시작 뒤 같은 본문·키로 재생했고 서비스 효과는 키별 한 번이었다. 별도 네이티브 HTTP 회귀는 NakamaServicePort·Gateway·SQLite와 루프백 응답기를 연결해 세 RPC와 실제 503 재시도 뒤 `REPLAYED`, SQLite 잔여 0건을 확인했다. PGlite 001–011은 이전 세션 재생, 재시작, 변조된 재생 거절, 만료·이미 수령한 퀘스트, 던전·보상 회귀를 통과했다.
- 실제 저장된 `BP_Spellblade`를 Hidden Leader 플래그 그대로 로드한 PIE 회귀에서 오프스크린 몽타주 진행·본 갱신 생략, 첫 시야 진입 틱과 긴급 창의 Full Pose, 최근 렌더된 Follower 보호, 시야 자료 미확인 시 보호, 원래 정책 복원을 확인했다. 검증용 절두체를 주입해 가시성의 시점을 결정했다.
- 별도 단일 D3D12 1280×720 군중에서 24캐릭터의 이동·착지와 동시 몽타주를 실행했다. 캡처 요청 프레임을 제외한 공격 구간 1,003표본의 중앙은 127.95 FPS(7.816ms), p95 10.738ms였다. 군중 캡처에서 대규모 포즈 붕괴는 관찰되지 않았다. 이 장면은 Standalone이므로 네트워크 프록시 최적화의 FPS 절감분이나 의상·무기 근접 검수는 이 수치로 판단하지 않는다.
- 확장 D3D12 회귀 235/235 통과(경고 없이 성공 234, 경고와 함께 성공 1, 실패/미실행 0). 기존 `GatewayRealHTTP`의 초과 응답 차단에서 발생한 libcurl 메시지가 경고를 기록했다. 확장 회귀 보고서 (로컬 `Saved/Validation/Continuation_20261014/LocalFinalA123_Expanded_20261125/index.json`)
- 근거는 `Saved/Validation/Continuation_20261014/LocalFinalA123_{Build2,Focused1,PGliteFinal,Field24_Iris,Expanded}_20261125`, `Saved/Validation/WholeProjectAudit_20261013/LocalFinalA123_Rendered24_20261125`에 남겼다. 완료한 실행의 근거는 중앙 보관소에 SHA-256 검증 후 등록했다.

**마감 판정.** 로컬 코드 및 지정 검증을 종결한다. 실제 네트워크 24명에서 D3D12 클라이언트를 동시 실행하는 조합은 4GiB 보호선과 이번 무렌더 시험의 최저 여유를 고려해 이번 사이클에서 분리했다. 운영 단계에서 Nakama·PostgreSQL 011 배포, 워커 리스가 만료된 데드레터 처리, 분리 호스트의 RHI·지연·채널 수용량을 확인한다.
