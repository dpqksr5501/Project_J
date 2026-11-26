# 오픈월드·MMORPG 후속 세부 구현과 시험

2026-11-26 · GPT-6.2 Sol 작업 인계 · UE 5.8.4 Win64 · 개발자 1~2명, 로컬 PC 1대

엔진이 World Partition 셀·HLOD 프록시를 관리하고 프로젝트 코드가 이동 예측·도착 승인·공격 의미·연결별 군중 선정을 담당한다. GPT‑6.2 Astra가 시스템 설계를 맡고 GPT‑6.2 Sol이 세부 구현·버그 탐지·디버깅·회귀를 진행한다. 피해 권위, 영속 ID의 의미, 네트워크 호환성처럼 여러 시스템의 계약을 바꾸는 경우에는 재현 결과와 대안을 갖고 Astra에서 설계를 재검토한다. [설계안](Project_J_OpenWorld_MMO_System_Design_2026-11-26.md) · [기반 구현과 검증 이력](Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md)

## 독립 검증 이후의 구현 상태

[통합 인계](Project_J_OpenWorld_Project_Handoff.md)에서 조사·설계·후속 검증의 순서를, [맵 신설·교체 절차](../../World/World_Map_Authoring_and_Replacement.md)에서 새 지역의 연결과 배포 조건을 확인한다.

새 콘텐츠를 연결할 때는 [콘텐츠 추가 시 가장 작은 검사](Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#콘텐츠-추가-시-가장-작은-검사)를 사용한다. 선택 에셋 검사, 저장 월드 검사, 실제 왕복·회귀 실행의 범위와 최신 측정 근거를 확인할 수 있다.

메인 월드 HLOD 원본 셀 연결, 다중 바이옴 이동 계측, 원형 경고의 계단·절벽, 직업별 기존 HUD 확장과 효과음, 공개 지역 라우팅은 후속 로컬 재검증에서 다시 확인했다. 현재 제작 절차와 실행 증거는 [로컬 재검증과 제작 경로](Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#로컬-재검증과-제작-경로)에 있다.

Ground Pulse는 제작 Spellblade에 부여되며 서버 마나·쿨다운과 원격 경고의 늦은 진입·재진입이 검증됐다. 다중 바이옴 원경, 서로 다른 맵 간 입장·재접속·복귀, 보스 배치의 SQLite 키 이관, 실제 Nakama/PostgreSQL 왕복도 실행했다. 아래 표는 두 인계의 최초 적용과 후속 확인을 함께 보여 준다. 완료 조건별 입력·결과·현재 남은 범위는 [Astra 독립 검증 결과](Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#독립-검증-결과)에 모았다.

## 2026-11-26 두 인계 문서 대조 결과

| 인계 항목 | 확인한 적용과 근거 | 제작·운영에서 남는 검증 |
|---|---|---|
| 월드 정의·포털·Cook | 저장 MainWorld와 RidgeRoute03의 WorldState→등록된 DA 확인. 두 프록시 통과 및 최소 세 프록시 요구의 정상 거절. 선택 에셋 검증에 기존 DA와 새 지상 탑승 BP 포함. 실제 최종 패키지에서 일반 포털 상호작용 성공. | 로컬 공개 지역 A→B→A와 보스 정산 SQLite 이관도 후속 검증했다. 출시 지역의 등록·정원·영속 키·패치 이전 캐릭터 복귀는 배포 데이터로 확인한다. |
| 수관·랜드마크·근경 | 엔진 기본 레이어 상속·누락·NearOnly 회귀 및 로드된 릿지 원경 경로 검증. WPO 상한은 단일 공통 값으로 사용. | 후속 시험에서 제작 맵 세 바이옴, 프록시 666개 재생성, 별도 중첩 레벨의 인스턴스·병합 전환까지 확인했다. 출시 최소 사양에서의 장시간 주행·실루엣·그림자 수용은 남아 있다. |
| 지상 이동·비행·도착 | 패키지에서 같은 주행·비행 경로와 지상 누락 0건 재현. 집중 회귀에서 순간이동·받는 Pawn/ASC 변경, 승인·허용량·만료, 기존 던전 입장과 복귀 확인. | 로컬의 두 공개 지역 서버 왕복, 던전 원위치 복귀를 확인했다. 지역별 최고 속도·도착 예산과 출시 지형 교체 후 저장 위치는 콘텐츠가 정해지면 확인한다. |
| 전투·VFX·애니메이션 | GAS 활성·사망·ASC 변경·Avatar 종료, 원 안과 밖 판정, 기존 콤보·몽타주 검증, Niagara 풀 수명, 시각/천/애니메이션 예산 및 protocol 15 거절을 재실행. | 제작 Ground Pulse의 부여·마나·쿨다운, 저옵션 늦은 진입과 만료, 계단·절벽 전조를 확인했다. 새 공격별 반응 시간, 팀 전용·다단 경고, 저속망에서의 표시는 별도 인수 대상이다. |
| 단일 PC와 군중 | 렌더 관찰자를 포함한 16인과 경량 40인 기준선, 제작 B01 독립 경량 클라이언트 및 Iris 한도 시험의 실제 소켓 기록 확인. | 제작 MainWorld 16·40인 동시 교전, 동일 인원 밀집/분산 및 파티·길드 비교, CPU/GPU/VRAM 병목 분리. |

재현 시험 `Saved/Validation/OpenWorldReaudit/Before/Automation/index.json`에서 누락·비지도 포털의 제작 검증과 누락 포털의 상호작용이 잘못 허용됐고, ASC 연결 변경 이후 이전 경고가 타이머까지 남았다. 포털의 공통 확인은 Asset Registry의 실제 World 에셋과 존재하는 패키지를 요구한다. 원형 공격은 기존 GAS 연결 변경 통지와 Avatar 종료를 받아 타이머·경고를 즉시 철거한다. 새 빌드의 `After/Automation/index.json`에서 30개를 통과했고, 기존 저장 캐릭터 관찰 시험 1개는 PoseSearch 인덱스 준비 경고를 동반했다.

추가 `PolicyAndHandover/Automation/index.json`의 6개는 수관 WPO 한계, 이관 참여자의 수명·시각·입력, 저장 포맷 마이그레이션을 통과했다. 앞선 30개와 합쳐 재감사 회귀 36개가 통과했다. 실제 서비스에서 새 지역 배치를 변경한 뒤 이전 저장 프로필로 재접속하는 시험은 제작 이관 입력이 정해질 때 이 회귀를 바탕으로 수행한다.

재감사 패키지 `OpenWorldReaudit/PackagedAudit`에서 노면 6,116샘플 중 누락 0건을 확인했다. 4,043회 전환 표본 중 최근 렌더 값이 모두 거짓인 때가 2회 있었다. 멀어질 때는 프록시, 돌아올 때는 원본에 가시 렌더 상태가 있었고 게임 화면으로 저장한 두 표본 모두 수관이 보였다. 관측 코드는 `-ProjectJRouteCapture=<기존 출력 폴더>`를 받으며 근경·원경·복귀와 최대 8개의 전환 표본을 남긴다. 실제 프레임 캡처를 사용해 렌더 이력값과 수관 소실 판정을 구분한다. [최근 렌더 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UPrimitiveComponent/WasRecentlyRendered) · [캡처 API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FScreenshotRequest/RequestScreenshot)

최신 바이너리의 원격 원형 경고 표본 `OpenWorldReauditWarning4`와 한도 20의 `OpenWorldReauditCrowd40`이 각각 4/4, 40/40을 통과했다. 후자는 교전 서버 월드 틱 p95 6.315ms, Iris 한도 제외 720쌍·면제 120쌍을 기록했다. 제작 B01 일반 플레이는 경고 생성·해제, 공격 10회, 개인 보상 우편을 다시 확인했다.

제작 B01 소켓 시험은 첫 재감사에서 복제 오류 없이 두 명이 준비됐으나 5초 창에 한 타격만 확정됐다. 서버 틱 처리 최대 1.625ms였다. 양쪽 GAS·콤보 진단을 추가한 무제한 경량 클라이언트 두 반복은 각각 2/2였다. 한 반복에서 클라이언트 예측과 서버 활성화 승인 사이가 약 0.80초 및 2.44초였고 승인 후 두 콤보 노드가 실행됐다. 같은 두 클라이언트에 `-HeadlessFrameCap 60`만 적용한 두 시험도 각각 2/2였다. 서버 호출→실효 피해 최댓값은 무제한 두 반복에서 3,000ms·2,771ms, 60FPS 두 반복에서 603ms·602ms였다. 이 값에는 몽타주가 포함된다. 60FPS 옵션은 한 PC에서 비교를 안정시키는 시험 조건이며 실제 운영 전송·스케줄링의 원인은 추가로 분리해야 한다. 제작 16인 독립 클라이언트 사전 검사는 여유 19,431MiB에서 예상 예산 75,776MiB를 충족하지 못해 시작을 거절했다. [로컬 소켓 검증기](../../../Scripts/Validation/Run-LoopbackCohort.ps1)

재감사 원자료 34개를 기존 중앙 보관 목록과 복원 명세에 추가했다. 실패한 첫 B01 시험, 무제한·60FPS 반복, 프리플라이트 사유, 실제 화면과 패키지 증거를 함께 찾을 수 있다.

## 2026-11-26 두 번째 지역 적용 결과

검증용 `RidgeRoute03`을 새로 제작했다. 월드 정의와 플레이어 시작점은 상시 로드하고, 노면 16개와 수관 표본은 WP가 스트리밍한다. 프로젝트 기본 게임 모드 및 기존 탑승·이동 경로를 사용한다. 최고 속도 2,600cm/s의 지상 프로필은 격리된 PIE에서 적용했다. [테스트 지도 생성](../../../Scripts/World/Build-StreamingRouteFixture.py) · [이동 측정](../../../Scripts/Validation/Measure-MainWorldFlight.py)

맵 템플릿에서 Runtime Hash Set의 HLOD 등록을 그대로 두고 MainWorld 레이어만 기본값으로 연결한 첫 표본은 저장 월드 검사를 통과했다. UE HLOD 빌더는 `invalid HLOD layer` 진단 뒤 종료 코드 0으로 프록시 0개를 반환했다. 실제 빌더가 사용하는 엔진 질의로 명시적 등록을 검사하도록 루트 배치 검사를 강화했다. 저장 맵 명령에는 이미 생성된 프록시가 필요한 대상에서 최소 수를 지정할 수 있게 했다. 기본 템플릿의 등록된 Instancing 레이어로 만든 두 번째 표본은 빌더 생성 2개와 저장 재개방 2개를 확인했다. 중첩 상속과 프록시 품질은 각 단계의 결과를 계속 확인한다. [UE 빌더와 레이어](https://dev.epicgames.com/documentation/unreal-engine/world-partition-hierarchical-level-of-detail-in-unreal-engine) · [배치 검증](../../../Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetEditorValidator.cpp)

| 증거 | 결과 |
|---|---|
| 에디터 회귀 | 직접 UBT `Project_JEditor Win64 Development` 성공. WP 배치·고속 이동·도착·월드 이벤트 **14/14 통과**, 경고 동반 성공 0건. |
| D3D12 지상 주행 | 다리 왕복과 정지 선회 약 180도. 최종 경로 약 559m, 최고 표본 속도 2,610cm/s, 지정 노면 트레이스 누락 0프레임. 다리 진입점에서 프로젝트 `Loaded`와 `Activated`, 기본 컨트롤러의 `Activated`를 확인했다. 중간 재시험에서는 선회 후 1도 미만의 출발 오차가 누적돼 다리 옆으로 벗어났다. 최종 시험은 기존 조향 입력으로 중앙선을 따라갔고, 같은 노면 판정으로 통과했다. |
| D3D12 비행 | 같은 맵에서 약 579m 비행했고 상승 952cm/s, 하강 −2,132cm/s, 누적 방향 변화 약 200도를 기록했다. 전체 WP 완료가 거짓이었던 표본은 3개였다. 강제 생성으로 바닥과 캡슐이 겹칠 수 있는 시험 초기화는 충돌 보정 스폰과 실제 비행 상태 시작 대기로 바꿨다. |
| D3D12 원경 전환 | 약 420m 위치에서 표본 원본 Actor가 로드 목록에서 사라진 뒤 HLOD의 최근 렌더 상태와 수관 실루엣을 확인했다. 가까운 시점과 복귀에서 원본 렌더를 확인했다. 카메라 테스트는 엔진 기본 제공자를 격리해 사용했고, 지상 주행은 프로젝트 기본 제공자를 사용했다. |
| 패키지 입장 | 기존 선택 에셋 검증, Win64 Development Cook·IoStore Stage 및 D3D12 실행이 성공했다. 패키지의 `/Engine/Maps/Entry`에서 개발 콘솔의 `open`으로 목적지에 여행했고 로드 완료와 정상 종료를 확인했다. 패키지의 실행 파일 SHA-256은 로컬 Game 바이너리와 일치했다. |

기본 맵을 바꾸지 않고 재현할 때 아래 입력을 사용한다. PIE의 JSON에서 `passed`와 실제 로드 맵을 함께 확인한다. 로그·사진은 새 실행 이름 아래로 저장한다.

| 단계 | 실행 입력 |
|---|---|
| 작은 맵 제작 | `PROJECT_J_ROUTE_FIXTURE`에 새 이름을 지정하고 `Build-StreamingRouteFixture.py`를 Editor Python commandlet으로 실행한다. |
| 프록시 생성과 저장 확인 | 해당 맵으로 `-run=WorldPartitionBuilderCommandlet -Builder=WorldPartitionHLODsBuilder -AllowCommandletRendering`을 실행한 후 `PROJECT_J_STREAMING_MIN_HLODS=1`을 지정해 `Validate-StreamingWorld.py`로 다시 연다. |
| 주행 | 기존 `Measure-MainWorldFlight.py`를 `-ExecutePythonScript`로 실행한다. `-ProjectJFlightRun=<고유이름> -ProjectJFlightRoute=GroundBridge -ProjectJRouteMap=<맵> -ProjectJGroundBridgeCm=6000,18000,32000`을 전달한다. 탑승 클래스는 `-ProjectJRouteMount`로 바꿀 수 있다. 경로는 X축 진행, 시작 양의 X 방향을 기준으로 한다. |
| 원경 | `Verify-WorldRouteHLOD.py`에 `PROJECT_J_ROUTE_HLOD_REPORT`와 `PROJECT_J_ROUTE_HLOD_VIEWS`를 지정한다. 뷰 입력은 `map`, `source_tag`, `views`를 담은 JSON이다. 뷰는 순서대로 근경·원경·복귀이며 각각 `[[x,y,z],{"pitch":각도,"yaw":각도}]`다. |

원자료는 `Saved/Validation/OpenWorldRoute/RidgeRoute03`의 `SavedMap.log`, `MissingProxies.log`, `HLOD.log`, `Automation/index.json`, `Visual2.json` 및 근경·원경·복귀 사진, `PackagedTravel.json`과 실행 로그다. `Saved/Validation/Continuation_20261014`의 `Ridge03GroundProof`, `Ridge03Flight3`, `Ridge03Validation2`, `Ridge03Cook2`, `Ridge03Stage2`도 연결된다. 첫 Cook 프로세스는 성공했으나 진행 중 스크립트가 추가되어 기존 도구의 완료 검증이 입력 변경을 감지했다. 패키징을 안정된 입력으로 다시 마친 뒤 추가 주행은 PIE 검증기만 보완해 측정했다. [원경 관찰 스크립트](../../../Scripts/Validation/Verify-WorldRouteHLOD.py)

위 표는 첫 지역 적용에서 확보한 기준선이다. 실제 포털, 새 지상 탑승 BP, 같은 패키지에서의 지상·비행과 연속 원경 전환 결과를 아래 후속 구현 기록에 추가했다.

이 지도는 `/Game/World/Validation`의 재현용 콘텐츠다. 명시적 배포 지도 목록에는 들어 있지 않으며, 테스트 DA는 기존 WorldDefinition 스캔 루트 안에 있다. 출시 Cook 범위를 확정할 때 검증 자료의 포함 여부를 실제 목록에서 확인한다.

원자료 22개는 기존 ArchiveStore에 추가하고 SHA-256을 대조했다. 중단된 두 임시 제작 맵의 파일 37개는 `Saved/Validation/OpenWorldRoute/InterruptedFixtures`로 옮기고 `Restore.csv`에 원래 경로와 해시를 기록했다. [로컬 보관 규칙](../../Maintenance/Local_Archive_Policy.md)

## 기존 기반이 적용되는 조건

### 후속 구현에서 달라진 계약

- **지역 입장** — `WorldPortal.StandaloneDestination`은 기존 상호작용으로 월드 패키지를 여는 독립형 경로다. 로컬 소유 도보 플레이어, 기존 거리·생존 조건, 유효한 맵을 요구한다. 같은 포털에 지역 내 장소나 던전 목적지를 함께 지정하면 제작 검증이 실패한다. 온라인 지역 이동 시험은 기존 인증·티켓 경로를 사용한다. [월드 포털](../../../Source/Project_J/World/Project_JWorldPortal.cpp)
- **이동 이력** — 같은 Pawn이 큰 위치 불연속을 겪어도 활성화·선행 로드 계획의 선회 및 후방 앵커, 선택적 비행 준비도 캐시를 무효화한다. 1,000cm 보정 여유와 관측 간 이동량을 사용하며 작은 복제 보정은 평상시 모션에 포함한다. [이동 제공자](../../../Source/Project_J/World/Project_JWorldStreamingComponent.cpp)
- **콤보 비용** — 첫 유효 노드가 선택된 뒤 서버가 동일 GAS 커밋을 한 번 수행한다. 입력 대기·필수 경고 누락·잘못된 몽타주 노드는 자원을 확정하기 전에 종료한다. 개시 입력은 활성화보다 늦게 도착할 수 있어 원격 클라이언트의 비용은 서버 복제로 수렴한다. [콤보 실행](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp)
- **공간 공격** — `UProject_JGameplayAbility_GroundAttack`은 `GroundArea` 정책, 공격 태그와 피해 효과가 있는 몽타주 독립 `AttackDefinition`을 사용한다. GA 파생 클래스에서 반경·예고 시간·전방 오프셋·표적 상한을 설정하고 기존 스킬 부여 경로로 연결한다. 서버가 확정한 지면 위치·반경·타격 시각은 짧은 수명의 경고 액터가 전달한다. 표식은 기존 렌더 컴포넌트, 피해는 기존 형상 검사·시야·팀·생존 조건과 GAS 효과를 사용한다. 사망·취소는 타이머와 표식을 함께 정리한다. 새 경고 복제 클래스와 비용 확정 시점에 맞춰 실시간 프로토콜을 16으로 올렸고 호환성 시험에 15를 추가했다. 서버와 클라이언트 바이너리는 같이 배포한다. [공간 공격](../../../Source/Project_JCharacter/Private/Combat/Project_JGroundAttack.cpp) · [UE 관련성 규칙](https://dev.epicgames.com/documentation/unreal-engine/actor-relevancy-in-unreal-engine)

D3D12 집중 회귀 `OpenWorldCompletion/CombatFinal/Automation/index.json`의 21개가 성공했다. `OpenWorldCompletion/PresentationAndTravel`에서 기존 Niagara 풀 수명, 원격 애니메이션·천·시각 예산, 던전 포털과 복귀를 포함한 6개도 통과했다. 저장 캐릭터 관찰자 시험 1개는 PoseSearch 비동기 인덱스 준비 경고 2건을 동반했다. 기존 CanonicalNetwork 부하의 독립 기준선은 16/16 실효 피해와 40/40 경량 실효 피해를 확인했다. 서버 월드 틱 p95는 각각 6.466ms와 7.148ms였다. 40인 표본의 공격 예약→히트 창 p95는 597.109ms, 권위 확정→수신 관측 p95는 385.159ms다. 이 수치는 서로 다른 구간의 분위수다. 두 기준선은 렌더 관찰자 구성도 달라 각각의 실행 조건과 함께 해석한다.

최종 Editor 바이너리의 CanonicalNetwork에서 16/16과 40/40의 실효 피해, 체력 수신, 종료를 확인했다. 16명은 D3D12 한 대와 경량 15대로 실행했다. 720p의 렌더 관찰자는 교전 프레임 p95 11.13ms, 화면 내 플레이어 중앙값 16을 기록했다. 40명 기본 설정의 교전 서버 월드 틱 p95는 8.139ms였고 33.33ms 초과는 0건이었다. 두 명씩 파티를 편성한 한도 20 표본에서도 40/40이 통과했고, Iris가 연결과 객체의 조합 720개를 한도 때문에 제외하고 120개를 면제 대상으로 집계했다. 두 설정은 파티 구성도 달라 성능 차이의 원인을 한도로만 귀속할 수 없다. 결과는 `Saved/Validation/Continuation_20261014/StarterWorldFinal16`, `StarterWorldFinal40`, `StarterWorldCrowdCap40`에 있다.

`GroundWarningSocketProof4`에서 실제 소켓으로 원형 경고 두 개를 수신했다. D3D12 캡처에 두 원의 테두리가 보이며, 같은 경고 ID를 대조해 하나의 시각 기반 만료와 다른 하나의 서버 조기 철거를 확인했다. 서버 피해·취소는 별도 GAS 기능 시험에서 실행했다. 이 소켓 표본은 기존 네트워크 보스 씬에 경고를 격리 생산해 검사했다.

제작 MainWorld의 B01은 독립 경량 게임 클라이언트 2/2의 GAS 타격·체력 수신·정상 종료를 통과했다. 서버와 두 클라이언트 로그에 WP 패키지 누락이나 저장 액터의 Iris 핸들 누락이 없었다. D3D12 1대와 경량 1대를 함께 쓴 앞선 표본에서는 두 클라이언트가 전투 준비에 도달한 뒤 가용 커밋 4,077MiB가 4,096MiB 보호선 아래로 내려가 공격 전에 멈췄다. 별도 일반 플레이 경로에서 B01의 경고와 해제, 10회 공격으로 체력 100→0, 재생성 대기와 2,030골드 개인 우편을 확인했다. `StarterWorldB01Headless2`, `StarterWorldB01Independent2`, `OpenWorldCompletion/B01Standalone/verification.json`에 각각 기록했다.

후속 Win64 Development 패키지는 검증용 실제 입구 지도에서 일반 상호작용을 실행해 두 번째 지역으로 들어갔다. 새 지상 탑승 BP를 타고 노면 추적 6,123프레임에서 누락 0건으로 다리를 왕복했다. 비행 탈것으로 교체한 뒤 상승·급강하·225도 회전을 확인했다. 원경에서 원본 언로드와 프록시 렌더, 복귀 후 원본 렌더를 확인했다. 연속 전환에서는 3,935 샘플 중 3개가 150ms 관측창에서 어느 쪽의 최근 렌더도 보고하지 않았다. 이 값은 육안에서 팝인이 전혀 없었다는 판정에 사용할 수 없다. [패키지 경로 검증](../../../Source/Project_J/Testing/Project_JWorldRouteFixture.cpp)

최종 Win64 Game·Editor 직접 UBT가 성공했고 선택 에셋 검증, Cook·IoStore Stage를 다시 끝냈다. `PackagedFinal/result.json`의 실행 파일 SHA-256이 최종 Game 바이너리와 일치한다. 반복 주행에서 노면 5,971샘플 중 누락 0건, 상승 952cm/s·강하 −2,335cm/s·225도 선회, 원거리 프록시와 근거리 원본 복구를 재현했다. 전환 3,992샘플 중 3개에서 최근 렌더 플래그의 공백이 다시 관측됐다.

같은 실행 파일에서 `wp.Runtime.HLOD.WarmupNumFrames`만 기본 5에서 12로 바꾸고 콘솔 값도 조회했다. `PackagedHLODWarmup12`의 전환 4,307샘플에도 공백이 3개 남았다. 이 표본에선 준비 프레임 확대 효과를 확인하지 못해 프로젝트 설정은 기본을 사용한다. 다음 원경 인수에서는 셀 가시성 이벤트와 같은 카메라 프레임의 원본·프록시 픽셀을 함께 기록해 렌더 동기화와 실제 시각 공백을 판별한다.

동일 PC에서 초기 지역 검증 Stage는 1,360,951,888바이트였고 최종 Stage는 60파일 1,361,488,788바이트였다. 약 0.51MiB 차이이며 두 실행의 지도 선택과 바이너리 구성도 기록에 남겼다. 대규모 제작 맵의 텍스처·HLOD·오디오·의존성 증가량은 제작 맵별 실제 Cook/Stage에서 측정한다.

원자료는 `Saved/Validation/OpenWorldCompletion`과 `Saved/Validation/Continuation_20261014`에 있다. 대표 실행·실패·측정 자료 35개의 SHA-256과 복원 관계를 기존 중앙 보관의 `ArchiveStore/INDEX.csv`와 `Deduplicated.jsonl`에 연결했다. 검증 패키지는 Stage 영수증의 고유 작업 디렉터리에 있다.

| 대상 | 기존 코드가 처리하는 범위 | 새 콘텐츠에서 먼저 확인할 경계 |
|---|---|---|
| 월드 정의와 지도 | Asset Manager가 `/Game/World` 아래 지정 타입을 스캔한다. 저장 맵 검사는 런타임 WorldState 한 개와 실제 DA 등록을 확인한다. | 런타임 조회는 첫 유효 WorldState를 캐시한다. 중복·상충 배치는 저장 맵 검사에서 잡는다. 정의 연결이 늦으면 기본 컨트롤러의 짝지어진 고속 제공자는 연결 후 회복한다. 지도 목록 검사는 제작자가 대상 루트에 실행해야 한다. [맵 검사](../../../Scripts/World/Validate-StreamingWorld.py) · [런타임 조회](../../../Source/Project_J/World/Project_JWorldStreamingComponent.cpp) |
| 수관·건축·작은 풀 | 원경 의도를 가진 메시의 배치, 엔진 foliage 컴포넌트, 기존 생성 태그를 검사한다. 수관은 Instancing, 랜드마크는 다른 유효 빌더도 허용한다. | 미분류 수동 메시, 중첩 Level Instance의 상속, 비어 있는 컴포넌트와 생성된 프록시의 실제 실루엣은 별도 확인 대상이다. `NearOnly`는 검사 예외다. 프록시 포함/제외는 엔진 설정으로 결정된다. [배치 검사](../../../Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetEditorValidator.cpp) |
| 새 이동체·포털 | 기본 컨트롤러의 고속 지상 예측은 Pawn 속도의 XY 성분을 사용한다. 공중 모드를 제공하는 Pawn은 3차원 예측을 사용한다. | 탑승자 대신 별도 차량 Actor만 움직이는 구조라면 실제 소유 Pawn 속도를 점검한다. 독립 Loaded 컴포넌트에는 자체 프로필이 있다. 준비 이동 포털에는 현재 정지·접지·도보 등 별도 승인 조건이 있다. 빠른 지상 주행 지원에서 탑승 포털 지원을 추론할 수 없다. [이동 인터페이스](../../../Source/Project_JCore/Public/World/Project_JStreamingMovementSource.h) · [도착 요청과 실행](../../../Source/Project_J/World/Project_JWorldStreamingComponent.cpp) |
| 스킬·VFX | 기본 플레이어 콤보와 Native NPC 공격은 선택한 AttackDefinition의 경고 정책을 확인한다. 공유 몽타주에서도 검사한 진입 섹션을 사용한다. | 검사하는 순서는 진입 섹션의 지정 경고와 첫 근접 hit notify 사이의 순서다. 최소 반응 시간, 독립 장판·투사체·커스텀 GA의 피해 시각, 관찰자의 화면 가시성은 해당 실행 경로에서 확인한다. [공격 계약](../../../Source/Project_JCharacter/Private/Combat/Project_JAttackDefinition.cpp) · [몽타주 역참조 검사](../../../Source/Project_JCharacterEditor/Private/Validation/Project_JCombatWarningAssetValidator.cpp) |
| 캐릭터·군중 | 기본 캐릭터가 Iris 복제를 시작할 때 군중 필터 배정을 시도한다. 거리·한도·면제는 연결별 후보를 기준으로 계산한다. | 독립 소환물·차량·배경 군중은 상속·복제 수명과 배정 성공 여부를 먼저 본다. owner-only·always-relevant는 기본 배정에서 제외된다. 필터 쌍 수로 화면에 그려진 사람 수나 모든 네트워크 액터 수를 산출할 수 없다. [등록 진입점](../../../Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp) · [선정과 집계](../../../Source/Project_JCharacter/Private/Network/Project_JCrowdDensityNetObjectFilter.cpp) |

## 1. 두 번째 지역을 패키지의 일반 입구에서 검증

1. 작은 야외 WP 지도 하나와 실제 입장 경로를 고른다. 지도 루트 목록 → 개별 저장 맵 검사 → WorldState·DA → 입구 또는 포털 참조 순서로 추적한다. 신규 메인 월드 구역이면 기존 월드의 소유자를 유지한다. 독립 지역 월드면 해당 월드에 상시 로드 WorldState 하나를 연결한다. `RegionId`의 현행 DA 검사는 빈 값 여부를 본다. 복제한 DA가 같은 ID를 갖는 경우에는 그 ID를 사용하는 저장·이동 경로와 공유 의도를 확인한다. [월드 정의 검사](../../../Source/Project_J/World/Project_JWorldDefinition.cpp) · [지도 목록·개별 검사 명령](Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md#저장된-야외-지역-검사)
2. 수관, 건축 랜드마크, 근경 소품을 같은 경로에 배치한다. 수관의 HLOD 참여를 끈 음성 표본과 기본 레이어를 상속한 양성 표본을 비교한다. 수종을 공유하거나 배치를 옮긴 경우 이전 셀과 새 셀의 생성 결과를 확인한다. 중첩 상속·미분류 수동 배치·판독 실패는 각각 대상 경로를 기록한다. 기존 생성기 소유 태그는 재생성·삭제 범위를 정하는 역할이 있으므로 보존한다.
3. WPO 거리 검사와 스트리밍 정책의 기본값은 Core의 공통 상한 10,000cm를 사용한다. 기존 DA의 `MaxAllowedWPODisableDistanceCm`는 직렬화·Blueprint 호환용으로 유지하고 세부 패널에 읽기 전용으로 표시한다. 지역별 상한이 실제로 필요하면 BP·DA·생성 스크립트와 검사가 같은 정책을 소비하도록 경로와 회귀를 함께 만든다. [거리 검사](../../../Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetValidator.cpp) · [공통 기준](../../../Source/Project_JCore/Public/World/Project_JDistantMeshPolicy.h)
4. 대표 셀의 HLOD를 생성하고 선택 자산 검증 → Cook → Stage를 진행한다. 기존 패키징 실행기의 기본 맵은 ThirdPerson과 Entry다. 실행 시 실제 입구·목적 맵을 `-Maps`로 지정하고 해당 선택 manifest·검증 실행을 연결한다. 패키지를 일반 입구로 시작해 목적지에 도달하는지도 별도로 확인한다. [선택 검증](../../../Scripts/Audit/Run-ContinuationAssetValidation.ps1) · [패키징 실행기](../../../Scripts/Audit/Run-ContinuationPackaging.ps1)

**완료 증거**는 지도·셀·빌드 식별자, 입장 경로, 소스 셀이 언로드된 뒤 원경 표현이 보이는 프레임과 재진입 후 원본이 복구된 프레임이다. 잘못된 배치, 생성 실패, Cook 누락, 렌더 전환 실패는 수정 위치가 다르므로 최초 실패 단계부터 고친다. 첫 셀에서 피크 RAM과 소요 시간을 확인하고 확대 범위를 정한다.

## 2. 빠른 지상 탈것과 비행을 같은 구역에서 비교

기존 비행 측정기는 MainWorld와 시작 Mover 탈것을 직접 연다. 이 표본은 기존 결과 비교용으로 유지한다. 측정기를 확장할 때 맵·탑승 대상·출발과 도착·경로를 시험 입력으로 받고, 수집 결과에 실제 로드된 맵·Pawn·프로필을 기록한다. 우선 새 지상 탈것 하나로 다리와 급선회를 통과한 뒤 기존 비행의 강하 결과와 대조한다. [기존 측정기](../../../Scripts/Validation/Measure-MainWorldFlight.py) · [고속 계획 회귀](../../../Source/Project_J/Private/Tests/Project_JPhase1SystemTests.cpp)

| 상황 | 확인할 결과 |
|---|---|
| 첫 진입·반복 주행·180도 선회·급강하 | Loaded 요청, Activated 요청, 첫 충돌 가능 시각과 첫 가시 프레임을 같은 경로에 기록한다. 지상에서 필수 지면 미준비 진입 0건, 비행에서 지면 이격을 모를 때 상세 그리드 유지 여부를 확인한다. |
| 같은 Pawn의 착지·재이륙, 같은 프레임의 탑승 교체, 같은 Pawn의 장거리 순간이동 | 이전 고도·방향·후방 앵커·준비도 캐시가 새 이동에 영향을 주는지 따로 시험한다. Pawn 교체 회귀가 통과한 기록은 같은 Pawn의 좌표만 바뀐 경우까지 증명하지 않는다. |
| 포털 취소·시간 초과·재접속·월드 종료 | 오래된 준비 응답이 새 요청을 승인하지 않고 소스·permit이 해제되는지 확인한다. 도보 전용 포털에서 탑승 사용자에게 어떤 결과가 보여야 하는지 먼저 정한다. 별도 던전은 기존 티켓·복귀 경로로 시험한다. [도착 예산](../../../Source/Project_J/World/Project_JWorldArrivalSubsystem.h) |

실제 충돌 미준비가 발생하면 셀 배치와 준비 시간부터 확인한다. 프레임 히치는 같은 구간의 I/O·GT 등록·물리·GPU·텍스처 밉·PSO 첫 사용을 나눠 본다. 충돌이 준비되고 원경 화질이 합의한 수준이면 그 지역의 감속·반경 확대 작업을 종료한다.

## 3. 새 공격 한 개에서 권위 피해와 화면 경고를 연결

공간 경고가 필요한 공격 하나를 골라 **누구에게, 어디에, 어느 시각까지 위험한지**를 정의한다. 실제 피해 생산자가 결정한 형상·방향·서버 시간·취소 시점을 기존 전투 표시 수명에 연결한다. 몽타주와 독립적으로 남는 장판이라면 장판 소유자가 종료·재입장 상태를 관리한다. 기존 기본 경고 표식은 선택 Niagara가 늦어도 읽을 수 있어야 한다. [피해 판정](../../../Source/Project_JCharacter/Public/Components/Project_JCombatHitValidationComponent.h) · [표시 수명](../../../Source/Project_JCharacter/Public/Components/Project_JCombatPresentationComponent.h)

- **비용과 재진입** — 플레이어 콤보는 첫 유효 노드에서 서버 GAS 비용을 확정한다. 시연용 무비용 기본 공격 외에 출시 비용이 있는 스킬을 연결할 때 실제 자원 수치·쿨다운·피해 횟수를 서버와 소유 클라이언트에서 비교한다. 잘못된 첫 노드, 후속 입력 누락, 장비 교체·취소·재입력을 표본에 넣는다. Native NPC도 선택한 override를 커밋 전에 검사한다. [플레이어 경로](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp) · [NPC 경로](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_NPCAttack.cpp)
- **연결별 표시** — 전용 서버와 렌더 관찰자를 사용한다. 처음부터 위험 범위인 대상, 늦게 진입하거나 다시 relevant가 된 대상, 비공개 대상을 나눈다. 첫 가시 프레임부터 타격까지의 여유, 남은 수명, 표식 해제, 비공개 정보 노출 0건을 확인한다. 최소 선행 시간은 공격별로 정한다.
- **제작과 옵션** — 몽타주의 마지막 경고 삭제, 같은 태그 겹침, 시전자 소멸, 낮은 프리셋, 음소거와 과밀 이펙트를 넣는다. 필수 표시·피해·보상이 유지되고 종료 뒤 표식이 남지 않아야 한다. 네트워크 형식이 달라질 때는 버전 조합 및 대응되는 서버·클라이언트·콘텐츠 롤백을 함께 시험한다.

## 4. 한 대에서 병목이 확인된 항목만 최적화

제작 B01 소수 독립 클라이언트 정확성 시험과 CanonicalNetwork 16·40인 부하를 각각 재현한다. `Run-LoopbackCohort.ps1`에서 항상 `-Clients`를 명시한다. 제작 B01은 `-MeasureCombat -BossCombat -EncounterMap '/Game/World/MainWorld/Maps/L_MainWorld' -EncounterId 'WorldBoss.Starter.Lv1_30.B01'`을 사용한다. 이 경로는 저장 조우의 위치·보스 클래스를 사용하는 격리 시험이며 일반 AI·킬 보상 검증은 기존 메인 월드 시험에서 수행한다. 비Cook Game 서버와 PIE 하위 클라이언트는 WP 셀 이름이 달랐고, 같은 프로세스에 여러 Game 컨텍스트를 만들 때는 저장 액터의 Iris 참조를 잃었다. 명시적 조우는 독립 `-game` 프로세스로 실행하고 이 두 복제 오류를 실패로 판정한다. 16~40 렌더 또는 제작 월드 다인 병목 분석은 가용 메모리가 확인된 환경에서 시행한다. `-VerifyCrowdCap`은 경량 실행이며 렌더 관찰자 검증은 별도 실행이다. [부하 실행기](../../../Scripts/Validation/Run-LoopbackCohort.ps1) · [렌더 군중 측정](../../../Scripts/Validation/Measure-RenderedCrowd.ps1)

| 먼저 보이는 증상 | 다음에 분리할 비용 | 변경 후보와 중단 기준 |
|---|---|---|
| HP 표시·필수 경고 지연 | 같은 공격·연결에서 권위 확정, Iris 선정·송신, 수신·GT 적용, 첫 표시를 대응한다. 파티·길드 여부와 밀집·분산을 바꾼다. | 매칭 행별 구간을 비교한다. 서로 다른 p95의 차이로 지연을 추정하면 안 된다. 합성 500후보와 실제 연결 수는 각각 기록한다. |
| 캐릭터가 많을 때 GT/렌더 시간 증가 | 포즈·본·장비 메시·천, 이름표 갱신, 그림자와 픽셀 비용을 나눈다. | 병목 항목의 기존 예산·LOD를 한 가지씩 조정한다. 판정에 필요한 포즈와 주요 경고 가독성을 함께 확인한다. |
| 효과 집중 또는 구역 입장 직후 끊김 | Niagara 생성·활성 수, GPU 오버드로, 비동기 로드 완료 후 한 프레임에 적용한 수, 오디오 음성 포화를 따로 잰다. | 반복 생성이 지배적일 때 풀링·Data Channels를 한 효과에 비교한다. 픽셀 비용이 지배적이면 재질과 화면 면적부터 본다. 취소된 일회성 효과의 지연 재생과 지속 효과의 중복 복구를 검사한다. [시각 자산 로더](../../../Source/Project_JCore/Public/System/Project_JVisualAssetSubsystem.h) · [연출 예산](../../../Source/Project_JCharacter/Public/System/Project_JPresentationBudgetSubsystem.h) · [오디오 예산](../../../Source/Project_J/Audio/Project_JAudioBudgetSubsystem.h) |

같은 빌드·경로·카메라·프리셋·인원에서 한 변수만 바꾸고 반복한다. 서버와 클라이언트별 CPU·메모리, GPU·VRAM, 첫 방문/재방문의 프레임 p95·p99·최대와 연속 초과 구간을 남긴다. 이 PC에서 프로세스 간 자원 경합이 먼저 발생하면 그 인원에서 결과를 닫는다. 개선폭이 반복 편차 이내이거나 가독성·권위 상태가 악화되면 이전 설정으로 복귀한다.

## Sol의 작업 마감

각 변경에서 실제 소비자, 취소·재시도·종료, 이전 데이터의 기본값과 롤백 대상을 확인한다. 영속 배치를 이동·복제·삭제하는 작업은 패치 전 상태로 재접속해 ID 재사용과 중복 보상을 확인한다. 신규 구역 입장과 파티 전투가 겹치는 변경은 도착·장비 로드·첫 경고를 같은 시간축에서 한 번 더 확인한다. [변경 마감 기준](../../Maintenance/Change_Completion_Policy.md)

| 실제 제작 범위가 열릴 때 남는 인수 항목 | 필요한 관측 |
|---|---|
| 새 지역·월드 인스턴스·영속 배치 이동 | 실제 온라인 할당과 도착 승인, 접속 중 다른 인스턴스 격리, 스키마 호환과 패치 이전 프로필 복귀, 재접속과 중복 보상. 이 채팅에서 검증한 이관 도구는 보스 정산과 전달 행을 다룬다. 출시 DB에 적용할 매핑과 패치 이후 복귀 위치는 배포 시 결정한다. |
| 출시 맵의 원경·식생·프레임 체감 | 구릉·겹친 실루엣·중첩 Level Instance, 움직이는 식생과 그림자, LOD/HLOD 교체 픽셀 또는 영상, 서로 다른 렌더 프리셋과 첫 방문의 I/O·PSO. 지역 WPO 상한이 필요해지면 지역 설정과 제작 검사의 소비 경로를 함께 연결한다. |
| 제작용 공간 스킬 | Ground Pulse의 실제 부여·입력·아이콘·사운드·자원 및 저옵션 재진입은 확인했다. 이후 추가하는 기술의 최소 반응 시간과 저속망에서의 가독성을 실제 공격으로 측정한다. 공개 원형 경고를 사용하고 비공개·팀 전용 공격은 표시 대상 설계가 별도로 필요하다. |
| 반복 VFX·대규모 군중·캐릭터 | 기존 Niagara 풀·연출 예산·애니메이션 우선순위와 경쟁시키면서 효과 생성·오버드로·천·그림자·오디오의 비용을 계측한다. 부하가 원인으로 확인될 때 반복 효과의 Niagara Data Channels를 후속 비교한다. |

작업 인계에는 변경 자산·코드, 실행 조건, 예상과 실제, 원자료, 남은 판단을 남긴다. C++ 변경은 기존 프로세스 종료를 확인하고 설치 엔진의 direct UnrealBuildTool로 해당 타깃을 검증한다. 출시 최소 사양·목표 속도·대표 랜드마크의 가시 거리·스킬별 반응 시간은 수치 확정 전까지 측정값과 구별해 적는다. [근거 파일 위치와 시험 범위](Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md) · [로컬 증거 보관](../../Maintenance/Local_Archive_Policy.md)
