# 200명 전원 표시와 로컬 품질 보존 최적화 계획

2026-10-22 Mover/native animation의 현재 → 결과 비교는 [별도 수정·재검증 보고서](../Reports/ProjectJ_Mover_200_2026-10-22.md)를 따른다. 아래 LOD 제작·채택 수치는 당시 조건의 기록이다.

> **과거 소스 경로 안내 — 2026-10-21:** 아래 현재 파일이 없는 소스 경로는 당시 구현의 위치를 텍스트로 보존했다. 이를 현행 API나 실행 가능한 제작 단계로 해석하지 않는다. 이동·애니메이션의 현재 계약은 [플레이어 로코모션](../../Animation/Architecture/Player_Locomotion_Architecture.md)을 따른다. 당시 수치·실패·외부 출처는 유지한다.

## 목표와 현재 상태

목표는 평상시 유휴·이동부터 밀집 전투까지 총 200명의 캐릭터를 표시하는 MMORPG다. 대표 클라이언트는 로컬 플레이어 1명과 다른 플레이어 표현 199명으로 구성한다. 로컬의 움직임·입력 반응·애니메이션 품질을 유지하면서 중복 작업과 불필요한 계산을 줄이고, 원격 표현·네트워크·서버 판정 비용을 각각 관리한다. 인원 제한이나 가까운 플레이어 숨김으로 목표를 달성하지 않는다. 화면 밖·가려진 대상의 시각 계산 생략은 전원 표시 정책과 구분한다.

500명 공성전은 이번 목표에서 제외한다. 목표 PC·해상도·FPS·서버 tick rate는 미정이므로 절대 성능 달성을 약속하지 않는다. 우선 같은 조건의 비용·품질 비교와 병목 원인 확인을 진행한다.

최초 조사 기준은 `main`의 `ac9e2b5a8a583dee5718f6a7b32e49ee8f6b1868`이다. 계획 작성 시 작업 폴더는 깨끗했고 관련 Editor·UBT·dotnet·Live Coding·MSBuild·ShaderCompileWorker 프로세스는 없었다. 프로젝트는 UE 5.8을 지정하며 설치 엔진은 5.8.3 / CL 58210709다. 최초 조사 후 사용자가 실행·최적화를 요청해 아래 **10월 15일 첫 적용과 검증**까지 진행했다. 작업 중 별도 공통 정책 커밋 `eb75019e`·`418a422e`가 추가됐으며 Source/Config/스크립트 해시 714개는 검증 대상과 일치했다.

**현재는 7차 제작 경로까지 구현·검증했으며, 10월 16일 사용자가 현재 몸체의 LOD0–2를 채택했다.** 아래 단계별 기록은 당시 결과다. 현재 결정은 다음 표를 따른다. 200명 전원 렌더와 로컬 품질 조건은 확인했지만, 전체 성능 개선·CPU 회귀 해소·지속 공성전 성능을 확정하지 않았다. 실제 수치·source/DLL/보호 에셋 해시·원자료 지문은 [통합 결과](../Reports/ProjectJ_200_Player_Optimization_2026-10-15.json)에 기록했다.

| 현재 결정 | 적용 범위와 경계 |
| --- | --- |
| 거리 관측·중복 조회 정리 유지 | 기존 예산 subsystem·snapshot·시각 조정기를 재사용한다. 로컬 입력/포즈 품질과 서버 이동·전투 권위를 유지한다. |
| 현재 몸체 LOD0–2 채택 | GreatSword_Woman의 로컬LOD0·원격 화면 크기 LOD1/2. 사용자 화면 검토와 기존 물리 동등성 근거에 한정하며 물리 CPU 절감으로 해석하지 않는다. |
| 공통 LOD 제작 경로 유지 | 기존 LOD 체인을 보존한다. 새 일반 표면 메시의 누락 LOD만 명시적으로 생성·검증하며 Cloth·헤어·morph 등은 별도 native 검수한다. 모든 에셋을 자동 승인한 것이 아니다. |
| LOD3–4 추가 보류 | 현재 체인으로 시작한다. 이후 원거리 geometry가 실제 GPU 병목으로 남고 전환·접촉·외형 비교에서 이득이 확인될 때만 검토한다. |
| 원격 IK 생략·물리 actor 축소 미채택 | 비용 이득이 입증되지 않아 후보 코드·옵션·축소 helper는 제거했다. 원본 solver·PA와 유효한 회귀 테스트를 유지한다. |

커밋 전 정리에서는 기존 메뉴→검사/명령줄 제작→native 생성·검증·저장 및 EditorValidator의 실제 연결을 확인했다. 공통 설정 에셋은 제작 입력, 비교 레벨은 사용자가 요청한 LOD 확인 자료, 테스트와 명시적 fixture 생성은 새 PC의 재현·회귀 자산이므로 유지한다. 실험용 생성 메시 두 개는 이미 Content 밖의 로컬 검증 보관소로 옮겼으며 추가 출시 참조를 만들지 않았다. 이번 마감에서 새 소스·에셋 삭제는 하지 않았고, 검증된 native 동작을 바꾸는 형식적 정리도 추가하지 않았다.

10월 16일 마감 확인에서 최종 Source/Config/스크립트729개, Editor/Game 바이너리3개와 보호 입력10개의 해시가 기록된 최종 검증본과 일치했다. 기존 Editor/Game 빌드·RHI13개·200명 표시·실제2연결 네트워크 회귀를 재사용하며 새 실행으로 세지 않는다. 이번 확인은 문서 링크·JSON·제작 스크립트 구문·커밋/LFS 연결을 대상으로 한다.

같은 날 **2차 공통 경로 조사·작은 조회 정리·64행 비계측 교대 비교**, **3차 원격 리타깃 IK 소비 후보의 검증과 미채택**을 추가했다. 아래 각 절과 같은 요약의 `second_pass`·`third_pass`를 구분해서 읽는다. 첫 적용과 2차의 해시·수치는 당시 근거로 보존하며, 과거 CPU 증가가 해소됐다고 판정하지 않는다.

**4차는 실제 retarget op·physics actor 진단과 의상 데이터 분리의 사전 검증**이었다. 당시 단순 복제 대조군의 동등성도 확보되지 않아 보류했다. `fourth_pass`는 당시 근거로 보존한다. 후속 **5차는 비교 시험의 비대칭 초기화 원인을 수정하고 동등성과 비용을 확인**했다. 900표본 동등성은 통과했지만 91→44 actor 축소의 비계측 변화는 대부분1% 안팎이며 trace의 대조 scope 변동도 남았다. 제작 적용은 하지 않고 Editor 회귀만 추가했다. 같은 요약의 `fifth_pass`를 따른다.

## 기존 구현 재사용

| 영역 | 현재 구현과 유지할 소유자 |
| --- | --- |
| 입력·이동·예측 | CMC와 SavedMove, GAS 및 서버 권위. 애니메이션 예산으로 Actor/CMC/input tick을 일괄 낮추지 않는다. |
| 의미 상태와 단발 | native StateController·Chooser·기존 presentation runtime. TIP·Start/Stop·Land 등의 선택·취소·자연 종료를 계속 소유한다. |
| MM와 재생 | Unified Locomotion 공통 스택. 단발 중 MM 검색 생략, 첫 복귀의 Pose History 검색, 실제 검색 완료 acknowledgement를 유지한다. |
| 검색 적용 | Proxy의 클래스별 generated MM 인덱스 캐시와 primary/companion, 기존 검색 주기·분산 스케줄. 전체 노드 탐색 제거를 다시 구현하지 않는다. |
| 스레드 경계 | GT에서 UObject·이동·GAS를 수집하고 immutable snapshot을 Proxy로 전달한다. 엔진 병렬 update/evaluation을 사용하며 새 Actor별 worker task/큐를 만들지 않는다. |
| 원본 애니메이션 예산 | BudgetedSkeletalMeshComponent·CharacterAnimationBudgetSubsystem과 requester별 Presentation/GameplayPose 요구. |
| 시각 메시 예산 | AnimationUpdateCoordinator의 몸체·의상 평가/보간·분산·wake·downgrade 유예·authored 설정 복구. 별도 군중 tick 관리자로 대체하지 않는다. |
| 몸체·장비 | Family/Body 프로필과 기존 retarget node 캐시, WeaponPresentation의 장비 표현·접촉 수명. 몸체별 MM DB나 애니메이션 복사본을 만들지 않는다. |
| 궤적 | 생성·history·후처리에 이미 프레임 중복 방지가 있다. Player Tick 호출이 있다는 이유로 중복 생성 결함으로 판단하지 않는다. |
| 원격 의미 이벤트 | 기존 compact recovery snapshot·unreliable 전송·서버 순서·event age·landing revision·SkipOwner. 매 프레임 포즈나 선택 clip을 새로 복제하지 않는다. |

기준: [애니메이션 목차](../../Animation/README.md), [파이프라인](../../Animation/Architecture/Animation_Pipeline_Refactor_2026-10-13.md), [통합 재생](../../Animation/Planning/Unified_Locomotion_Implementation_2026-10-12.md), [시각 예산](../../Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md), [리타깃 프로필](../../Animation/Architecture/Runtime_Retarget_Profiles_2026-10-13.md), [원격 단발](../../Animation/Locomotion/RemoteOneShotReplication.md).

## 현재 근거와 고비용 후보

### 과거 실측으로 우선 확인할 영역

[10월 13일 MM 캐시 최적화](../../Animation/Planning/Animation_Runtime_Optimization_2026-10-13.md)는 이미 반영됐다. 당시 200명 동시 공격 ABA on의 ApplyDatabase 누적 CPU가 0.267075→0.085192ms/tick으로 줄었지만, 비계측 World tick 평균 차이 0.330ms는 변경 전 반복 평균 표준편차 0.980ms보다 작았다. 이미 얻은 국소 감소를 새 과제로 제안하거나 전체 개선으로 계산하지 않는다.

[10월 14일 단계 trace 원자료](../../Review/Audits/ReauditEvidence_2026-10-14/Reaudit/CPUStageTraceSummary.json)의 B / AttackBurst / ABA on / 반복 0·1을 120 측정 tick으로 나누면 다음과 같다.

| 당시 timer 묶음 | 반복 0 | 반복 1 | 해석 |
| --- | ---: | ---: | --- |
| RigidBody | 4.7473ms | 5.0113ms | 의상 물리·PreUpdate를 우선 분리 조사할 근거 |
| 리타깃 snapshot/profile/contact math | 0.8122ms | 0.8010ms | GT 수집과 후단 수학의 비용 분리 필요 |
| MM·재생·DB/search policy | 0.2508ms | 0.2429ms | 순수 Pose Search 전체 비용을 뜻하지 않음 |

이 값은 묶음별 **exclusive 누적 thread CPU/tick**이다. worker 누적값을 프레임 wall time·GT 시간·GPU 시간으로 읽거나 다른 중첩 timer와 합산하지 않는다. 당시 전체 trace는 비계측 A/B/B/A와 비용 방향도 달랐다. [64행 비계측 비교](../../Review/Audits/ReauditEvidence_2026-10-14/Reaudit/CPUComparisonSummary.json)에 남은 평균 증가 +2.40~8.59%는 미해결이다. 이번 기준 소스에 대한 새 측정으로 대체해야 한다.

기존 fixture는 controlled kinematics에서 CMC tick을 끄고, 공격은 한 번 요청한 뒤 복귀까지 측정한다. 200 authored body/장비 구성, native NPC 200, 실제 접속 수는 서로 다른 부하다. 기존 200명 수치를 지속 공성전·로컬 입력 부하·200개 접속 성능으로 확대하지 않는다. 실제 RHI 결과와 독립 Server target 미지원 한계는 [재감사 검증](../../Review/Audits/Project_J_Reaudit_Verification_2026-10-14.md)을 따른다.

[현재 AuthoredAnimation fixture](../../../Source/Project_J/Testing/Project_JAuthoredAnimationFixture.h)는 `BP_GreatSword`를 명시적으로 생성한다. Config의 다른 시험용 `PlayerCharacterClassPath=BP_Player`와 혼동하지 않는다. 실제 플레이의 Map→GameMode→Pawn/Controller→Master/Body/profile 연결과 각 시험의 spawn 경로를 먼저 기록하고, 유사한 시험 이름만으로 같은 실행 경로라고 판단하지 않는다.

### 현재 소스로 확인한 실행 조건

| 우선순위 | 확인한 사실 | 다음 확인과 적용 조건 |
| --- | --- | --- |
| P0 수정 | 최초에는 `CurrentSignificance` 기본값 0과 manager 등록만 있었고 fixture는 등급을 직접 주입했다. 실제 local camera+원격 캐릭터 재현에서 300m 거리에도 LegacyViewpoints=0·global tier=0·animation Near를 확인했다. | 기존 animation budget subsystem이 GT에서 애니메이션 전용 관측을 공급하도록 수정했다. 전역 significance/AI는 새로 구동하지 않는다. 아래 실행 결과 참조. |
| P0 | montage는 BudgetedMesh의 독립 GameplayPose 요구로 유지되고, CanUseBudget도 재생 중인 montage를 제외한다. QualityPolicy는 GameplayPose에서 Near 품질을 보호한다. | 공격 구간의 원본 보호 사유·기간·실제 평가 횟수를 계측한다. blend-out/end notify가 끝나기 전에 예산에 재가입시키지 않는다. |
| P1 | Coordinator의 보호 조건에는 local·combat critical·모든 animation update 요구·montage·root motion·단발/공중/회전·복귀 유예가 포함되고, 보호 시 몸체·의상 그룹은 full rate다. | 원본의 판정 보호와 부가 표현 보호가 함께 상승하는 비용을 확인한다. 원본 유지 조건 아래 원격 의상/부가 IK만 별도로 조절할 수 있는지 검토한다. 이는 아직 구현되지 않은 확장이다. |
| P1 | Retarget Proxy는 클래스별 노드를 캐시하지만 각 PreUpdate에서 source/target·Family·override를 확인하고 엔진 PreUpdate도 수행한다. | profile 확인, 엔진 source pose/curve capture, 실제 retarget evaluation을 분리한다. 엔진 호출 전체를 생략하는 캐시를 만들지 않는다. |
| P1 | Retarget NativeUpdate와 두 GuidedHandIK PreUpdate는 calibration/contact 자료를 읽는다. WeaponPresentation은 pull 시 grip을 갱신하고, 독립 motion은 이미 프레임 단위 중복 방지가 있다. | 실제 호출 수·동일 입력의 반복 해석을 확인한다. 동적 grip/weapon transform은 매 프레임 보존하고, 공유 가능한 설정값만 기존 소유자의 좁은 범위에서 재사용한다. |
| P1 | Snapshot의 trajectory 배열은 GT 데이터·queued 데이터·worker 소비 데이터로 전달된다. revision guard는 같은 publication의 반복 소비를 막는다. | 복사 크기·호출 횟수·할당·실제 GT 대기를 먼저 확인한다. GC·수명·동시성 경계 없이 pointer 공유/zero-copy로 바꾸지 않는다. |
| P2 | Melee NotifyTick은 socket endpoint·radius·authority trace 과정에서 Validation component를 반복 조회하고, sweep 결과마다 GAS TargetData/event를 만든다. | 중복 조회와 hit 후보/중복 event 비용을 분리한다. 조회 재사용은 로컬 예측과 서버 모두의 소규모 개선 후보다. sweep 생략·피해 중복 필터의 소유권 이동은 별도 정확성 변경이다. |
| P2 | ApplyCombatRotationMode는 현재 입력·공중·속도·TIP root yaw를 매 tick 소비한다. CharacterAnimProfile getter는 포인터 반환이다. | 단순 반복 호출이라는 이유로 이동 정책을 이벤트 전용으로 바꾸거나 저렴한 getter에 장기 캐시를 추가하지 않는다. root-motion 추출과 실제 상태 수집 비용을 먼저 잰다. |

주요 소스 위치:

- [BaseCharacter](../../../Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp): BeginPlay의 Significance 등록.
- [PlayerCharacter Tick](../../../Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp), [이동 정책](../../../Source/Project_JCharacter/Private/Project_JPlayerCharacter.Movement.cpp), 궤적 (`Source/Project_JCharacter/Private/Animation/Project_JMotionMatchingTrajectoryComponent.cpp`, 당시 경로·현재 파일 없음).
- [Budget](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Budget.cpp), [QualityPolicy](../../../Source/Project_JCharacter/Private/Animation/Project_JAnimationQualityPolicy.cpp), [BudgetedMesh](../../../Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp), [Coordinator](../../../Source/Project_JCharacter/Private/Components/Project_JAnimationUpdateCoordinatorComponent.cpp).
- Snapshot (`Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Snapshot.cpp`, 당시 경로·현재 파일 없음), Search (`Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Search.cpp`, 당시 경로·현재 파일 없음), [Proxy](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstanceProxy.cpp).
- [Retarget Proxy](../../../Source/Project_JCharacter/Private/Animation/Project_JRetargetAnimInstanceProxy.cpp), [Retarget Instance](../../../Source/Project_JCharacter/Private/Animation/Project_JRetargetAnimInstance.cpp), [GuidedHandIK](../../../Source/Project_JCharacter/Private/Animation/Project_JAnimNode_GuidedHandIK.cpp), [WeaponPresentation](../../../Source/Project_JCharacter/Private/Components/Project_JWeaponPresentationComponent.cpp).

## 로컬 품질을 유지하는 개선 순서

로컬은 입력·CMC·궤적·MM 검색·단발 반응·보정·손/발 IK의 품질이나 주기를 낮추지 않는다. 동일 결과를 더 적은 작업으로 만드는 개선을 먼저 적용한다.

1. **호출 안에서 중복 조회 제거:** Melee Notify의 Validation/AttackDefinition, animation update의 동일 profile/calibration 해석 등을 계측한다. 같은 호출에서 재사용할 수 있는 값부터 묶고, 새 영속 캐시·전역 manager·반영 필드를 만들지 않는다. 실행 도중 장비/공격 변경이나 동기 GAS callback이 있는 경계는 별도다.
2. **구성 정보와 동적 입력 구분:** bone/socket 설정·Family/Body·노드 위치는 기존 클래스/프로필/mesh/장비 변경 경계에서 처리할 수 있는지 본다. 위치·회전·접촉 curve·ground trace·현재 root-motion·예측 입력을 오래 캐시하지 않는다. 현재 getter처럼 저렴한 경로는 유지한다.
3. **snapshot 복사와 관측 대기:** 실제 할당/복사 비용이나 Proxy getter의 기다림이 관측될 때만 게시 횟수·중복 관측을 줄인다. worker가 읽는 immutable 데이터·GC 참조·선택 revision은 유지한다.
4. **게임플레이와 무관한 진단:** debug가 꺼진 상태에서 문자열·배열·UObject 생성이 남는지 확인한다. 기존 trace와 검증 기능은 삭제하지 않고 활성화 경계에서 비용을 제한한다.

채택 전후에 같은 입력·dt 기록으로 asset/time/selection revision·복귀 이유·root/capsule·curve·발 접촉·무기/손 접촉을 비교한다. deterministic하지 않은 엔진 MM 선택은 후보·비용·입력 차이까지 확인하며 무조건 바이트 동일성을 요구하지 않는다. 사전 정의한 수치 허용오차와 실제 영상/접촉 검토를 함께 사용한다. 참조 횟수나 파일 줄 수 감소는 성능 판정 기준이 아니다.

## 거리 외의 원격 표현 예산

거리만으로 200명 밀집 전투를 해결하지 않는다. 화면 크기·view 방향·최근 표시 여부·현재 대상·직접적인 위협·파티의 즉시 상호작용을 관측 후보로 삼되, 매 프레임 200×200 전수 관계 검색을 추가하지 않는다. 기존 대상/위협/전투 상태 이벤트와 값 snapshot을 우선 재사용한다. 파티/길드 소속만으로 무제한 full rate를 부여하지 않는다.

정확성 요구와 시각 정밀도를 분리한다. 원본 gameplay pose·root motion·notify는 필요한 동안 보호하고, 표시 몸체의 세부 보정과 의상 물리는 남은 예산에서 결정하는 방향을 실험한다. 원격 공격의 윤곽·방향·타이밍과 위험 표시를 잃으면 실패다. 모든 상대가 공격 중인 경우에도 부가 표현 비용을 통제할 수 있어야 한다.

기존 Coordinator가 시각 메시 갱신의 소유자로 남는다. 의상 물리만 별도 주기를 주는 기능은 현재 구현에 없으므로, 엔진 pose/copy/physics 의존성과 실제 graph 분리 비용을 먼저 조사한다. 새 캐릭터별 의상 manager나 두 번째 외부 tick 제어자를 먼저 만들지 않는다. 현재 분산 스케줄·보간·wake·유예를 재사용하고 원본→몸체→장비 순서를 보존한다.

추가 렌더 후보는 장비 component/section/material 수, bone LOD·skinning·그림자·이펙트·이름표다. Leader Pose·Copy Pose·mesh merge·Animation Sharing은 각각 물리/개별 포즈·초기 생성·메모리·커스터마이징 제약이 있어 실제 비용에 따라 선택한다. 플레이어별 MM 결과와 발 위상을 일괄 공유하지 않으며, Mass/VAT/군중 대체 표현이나 장비 메시 통합은 별도 실험이 필요한 후순위다.

## 네트워크와 전투 판정의 경계

### 현재 구조

- 설정은 Iris 사용을 지정한다. 실제 실행의 NetDriver/IrisActive·연결 수를 매번 기록한다. 거리 필터·전투 prioritizer는 현재 UObject 정책 계산기이며 controller 진단에서 사용된다. 실제 Iris adapter가 설치된 것으로 취급하지 않는다. [확장 게이트](../../Networking/Planning/ProjectJ_Iris_AOI_Scale_Gates_2026-09-05.md).
- [MeleeHit Notify](../../../Source/Project_JCharacter/Private/Animation/Project_JAnimNotifyState_MeleeHit.cpp)는 **Leader socket + optional canonical blade tip offset**을 sweep한다. 표시 무기·리타깃·손 IK는 판정 endpoint를 소유하지 않는다.
- [CombatHitValidation](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp)은 공격 시 GameplayPose를 요구하고 notify window·장비 revision·prediction key·server trace를 검증한다. [Melee Ability](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp)는 authority에서 피해를 처리하고 autonomous proxy에서 SSR 요청을 보낸다.
- [ServerSideRewind](../../../Source/Project_JCharacter/Private/Combat/Project_JServerSideRewindComponent.cpp)은 authority에서 PostPhysics capsule history를 제한된 ring buffer로 기록한다. 현재 구조를 골격 전체 rewind로 오해하거나 별도 history 소유자를 추가하지 않는다.
- Notify는 mesh/실행별 상태와 window token으로 구분한다. 일반 simulated proxy가 모든 공격 sweep을 실행한다고 단정하지 않고, 유효 attack/window와 실제 role별 호출을 먼저 확인한다.

### 변경별 영향과 검증

| 변경 후보 | 영향 | 유지·검증 조건 |
| --- | --- | --- |
| 원격 손 IK·의상 조절 | 클라이언트 표시 | source 판정 endpoint/notify와 이동 패킷이 같아야 한다. 몸체/무기 시각 접촉과 공격 가독성은 별도 확인한다. |
| 원본 애니메이션 갱신 축소 | notify·root motion·무기 socket·server trace 가능 | 단순 성능 조절로 취급하지 않는다. 공격 창/끝 callback/SSR·실제 판정 검증 전 적용하지 않는다. |
| 같은 호출의 중복 조회·값 계산 제거 | 로컬 예측·서버 양쪽 | mesh/공격 교체·동기 취소·window token·장비 revision·기존 sweep 순서 유지. 새 RPC 없음. |
| 복제 주기·관심 영역·우선순위 | 갱신 지연·대역폭·최초 진입·공정성 | 상대가 보이지 않아도 필요한 위협 정보 전달. 소유자/직접 전투 상대 보호, 낮은 우선순위 대상의 장기 굶주림·취소 지연 확인. |
| 이벤트 압축·합치기 | 순서·유실 복구·late relevance | 기존 recovery snapshot·server event order·age·landing epoch 유지. reliable로 일괄 전환하지 않는다. |

네트워크는 화면 품질 등급과 별도 결정으로 유지한다. 스킬/공격/장비/PlayerState·GAS가 실제 payload와 서버 처리에서 차지하는 비중부터 확인한다. 파티의 상태 정보와 먼 캐릭터의 세부 장비/이동 정보도 분리한다. Iris filtering/prioritization 검토 중 Replication Graph를 동시에 추가하는 방식으로 진행하지 않는다.

AOI 재진입·순간이동·late join의 초기 복제와 에셋 로딩이 몰리는 경우를 steady state와 별도로 측정한다. 네트워크 제외와 단순 시각 생략을 혼동해 적이 보이기 전에 공격하는 문제를 만들지 않는다.

## 실행과 채택 순서

| 단계 | 작업·대상 | 산출물과 통과 조건 |
| --- | --- | --- |
| 0 완료 | 현재 구조·기존 결과·소유권 대조 | 아래 실행 결과와 과거 수치를 분리했다. |
| 1 완료 | 빌드·실행 경로·거리 정책 재현 | 직접 Editor 빌드, GameMode/Pawn/Master/Body/profile 읽기와 실제 authored pawn 시험, 수동 등급 없는 관측 결함 재현. |
| 2 일부 완료 | 기존 Insights scope 사용 | 1명·50명 CPU trace와 전후 200명 RHI 수집. retarget engine capture와 일부 세부 할당/대기 분해는 남아 있다. |
| 3 완료 | 작은 실제 입력·CMC·GAS 확인 | 기존 격리 CPU 및 실제 CMC Sprint·기록 착지·원격 입력 회귀를 재사용했다. |
| 4 첫 적용 완료 | 같은 호출의 중복 계산 제거 | Sprint 정책 조회·Melee component/attack 조회·무기 부착 판별을 줄였다. 각각의 전체 CPU 개선율은 입증하지 않았다. |
| 5 일부 완료 | 200명 렌더와 로컬 보호 | 실제 local ownership 1+199 전원 표시 검증. CMC는 이 장면에서 controlled kinematics다. 지속/분산 전투와 통제된 교대 성능 비교는 남아 있다. |
| 6 작은 원격 완료 | 실제 연결 2+NPC200 | 50ms/3% loss·parallel net·원격 locomotion/전투 3 trial 통과. N10/N30·WAN·200접속/운영 독립Server는 남아 있다. |
| 7 첫 적용 기록 완료 | 빌드·집중 회귀·보호 해시·원자료 | Editor/Game·19개 집중·30개 RHI 회귀, 원격과 렌더. 전체 CPU 회귀 해소로 판정하지 않는다. |

후속 반복 부하는 아래 미해결 항목에 범위를 맞춘다. 장비·에셋 변경이 필요한 실험은 대상·설정·원복값·보호 해시를 먼저 명시하며 기존 에셋/시험 자산을 덮어쓰지 않는다.

### 대표 상황과 관측값

| 상황 | 확인할 문제 |
| --- | --- |
| 로컬 단독 실제 입력 | OTM/Strafe·Run/Sprint·TIP·Pivot·Jump/Land·단발/MM 복귀, 입력 반응·카메라 회전·local snapshot 비용 |
| 200명 idle/이동 | 검색/평가·스키닝·의상·장비 component·그림자·등급 생산·동일 프레임 동시 작업 |
| 분산 공격과 동시 공격 | 실제 hit window·공격 참가자 수/시간·보호 사유·montage blend-out·GAS/event/sweep·원본과 부가 표현 비용 |
| 카메라 급회전·집단 진입 | wake·보간·pose/cloth 복구·초기 복제·asset/PSO/GC hitch |
| 장비/체형/Avatar 교체·사망/부활 | tick lease 복원·cache 무효화·socket/hand contact·오래된 callback/요구 해제 |
| 실제 지연·손실·지터·재관련 | 이동 보정·입력 예측·Start/Stop/Land/TIP 순서·중복/유실 복구·공격 취소·SSR 판정 |

성능은 GT·worker 누적 CPU와 실제 task wait/critical path·Render Thread·GPU·프레임 p50/p95/p99/최대·할당/메모리를 분리한다. 짧은 smoke에서 p99를 안정적인 값으로 주장하지 않는다. 로컬 품질/조작 반응도 군중 부하 상태에서 관측한다. 서버는 tick/지연된 갱신/연결당 bytes·RPC·pending/write 실패/장기 미갱신을 기록한다.

실제 RHI와 NullRHI, 비계측과 trace, Editor/packaged, warm/cold load를 섞지 않는다. 전후는 같은 workload·엔진·빌드 구성·주기·장비·카메라·DDC 조건을 사용하고 실제 작업량도 함께 비교한다. B 변경마다 이전 전체 빌드가 다른 A와 비교하지 않도록 관련 모듈/소스 해시를 맞춘다. 대표 조건의 교대 실행으로 편차를 확인하고 평균만 좋아졌는데 p95/최대가 나빠진 경우 원인을 남긴다.

## 구조 정리와 회귀 보호

공통 삭제·중복 판정·변경 마감 기준은 [저장소 정책](../../Maintenance/Change_Completion_Policy.md)을 따른다. 아래는 이 최적화에서 특히 보호할 실행 경계다.

새 상태·캐시·timer를 추가하기 전 기존 소유자와 무효화 시점을 지정한다. source 예산은 기존 mesh/subsystem, visual 예산은 Coordinator, 장비 표현은 WeaponPresentation, 단발 수명은 StateController, 판정은 CombatHitValidation이 유지한다. 하나의 전역 중요도 값으로 서버 전송·판정·시각 품질을 모두 제어하지 않는다.

미사용 로직은 C++ 호출뿐 아니라 reflection·BP/Chooser/DA·직렬화·엔진 override·테스트/디버그 확장 사용까지 확인한 뒤 제거한다. 기존 Handoff reflected 타입·native MM fallback·공용 getter는 이름이 오래됐다는 이유로 삭제하지 않는다. 에셋 참조 확인이 필수일 때만 관련 에셋을 최소 범위로 조회한다. 중복 문서는 새 요청 프롬프트를 계속 추가하지 않고 이 계획의 상태와 해당 구현/검증 기록을 연결한다.

보호 계약:

- TIP·OTM/Strafe 전 방향·Walk/Run/Sprint·가감속·작은 곡선·이동 회전/반전/Pivot·점프/낙하·이동/제자리 착지·MM 복귀.
- R19 대각 착지 선택과 R20 sample 보정 초기화, 각 sample의 실제 asset/time/mirror/table 일치.
- 지속 곡선 **0.35초·100cm·3도·85%**, 기존 발 위상/접촉 curve·sync·notify/Foley 의미.
- root motion·Orientation Warping·Steering·Offset Root·발/손 IK·대검·소켓·체형·리타깃·장비/전투 자세.
- 서버 이동/피격 권위·SavedMove 예측·GAS/SSR·공격 창/종료/cancel/장비 revision·late callback 방지.
- Coordinator/allocator가 취득한 설정만 복원하고 최신 외부 정책을 덮어쓰지 않는다. GC·world teardown·장비/Avatar 교체 후 오래된 요구/캐시를 유지하지 않는다.
- Mirror Data는 [현재 보류 결정](../../Animation/Planning/Mirror_Data_Adoption_Decision_2026-10-15.md)을 유지한다. Traversal은 제외한다.

검증 도구는 기존 `AuthoredAnimation`(당시 실행기; 2026-10-20 구형 fixture 부재로 퇴역), [RenderedCrowd](../../../Scripts/Validation/Measure-RenderedCrowd.ps1), [Network](../../../Scripts/Validation/Measure-Network.ps1), [trace export](../../../Scripts/Validation/Export-DTrace.ps1)를 재사용했다. RenderedCrowd의 새 `-LocalPlayer` 옵션은 기존 controlled kinematics 장면에 실제 로컬 소유권과 full-quality 조건 검사를 추가한다. 실제 local CMC 이동은 별도 시험이며, 현재 fixture에 없는 부하를 실행한 것으로 표기하지 않는다.

runtime 변경 후 direct UnrealBuildTool.exe로 관련 Editor/Game을 검증하고, 첫 오류부터 처리한다. UBT/dotnet/Editor/Live Coding/MSBuild/ShaderCompileWorker가 실행 중이면 새 빌드/검증을 겹치거나 중단하지 않는다. 기능 변경 범위에 맞춰 기존 AnimationDemandLifetime·ProxyLifetime·PipelinePolicy·RetargetBody·VisualBudget·OneShot/Turn/Continuity·CanonicalMeleeTrace/BladeTrajectory·MeleeLifetime·CombatPose·SSR/authority 시험과 실제 재생/원격 시험을 선택한다. 구현을 그대로 따라 쓰는 시험이나 무관한 전체 부하를 매번 반복하지 않는다.

## 10월 15일 첫 적용과 검증

### 실제 경로와 수정한 원인

읽기 전용 조회에서 기본 맵 `Lvl_ThirdPerson`의 WorldSettings GameMode override가 Config와 같은 `BP_Project_JGameMode`임을 확인했다. 해당 GameMode CDO의 Pawn은 `BP_GreatSword`, Controller는 `BP_ThirdPersonPlayerController`, Source는 `ABP_Humanoid_Master`·`SKM_Quinn_Simple`다. `DA_GreatSword_AnimProfile`의 locomotion은 `DA_Player_Profile`다. Character profile의 Body 필드가 None이어도 실제 Body ABP의 `DA_RetargetBody_Greatsword` 명시 override가 우선한다. 이 None을 리타깃 미사용으로 판정하지 않았다. 실제 생산 BP를 생성한 시험에서도 native Master와 동적 profile 거리 소비를 확인했다. 초기 Python getter·미초기화 World Actor 조회 실패와 수정한 최종 조회는 별도 raw 로그로 보존했다.

기존 엔진 SignificanceManager는 게임이 Update를 호출해야 하며, Sequential은 post callback만 직렬 실행한다. 전역 경로를 단순 활성화하면 기존 worker의 Actor 읽기와 여러 view의 max/0=Near 역전, AI/BP 공개 API 영향까지 다뤄야 한다. 이번에는 전역 경로를 변경하지 않고 기존 `CharacterAnimationBudgetSubsystem::Refresh`의 GT 순회에 관측을 연결했다.

- 해당 world의 local PlayerController 시점을 한 번 수집하고, 등록 메시 owner와 가장 가까운 view까지 거리 제곱으로 분류한다. 현재 Native AnimInstance의 유효 locomotion 설정을 사용한다. native instance가 없는 mesh는 애니메이션 기본 설정을 사용하며 BaseCharacter의 별도 BP significance 거리 필드를 새로 구동하지 않는다.
- 기존 budget mesh가 작은 optional 관측값을 보유한다. MM Far 제한·SourceQuality·ABA 우선순위·시각 Coordinator가 공통 getter로 소비한다. 새 subsystem·tick·task·timer·RPC·복제 필드는 없다.
- local player는 camera 거리에 관계없이 즉시 tier 0이다. view가 없거나 DS이면 기존 tier fallback을 사용한다. ABA off와 관측은 독립이다. reinitialize/unregister/detach/cleanup에서 관측을 해제한다.
- post-actor pass를 재사용하므로 camera 이동·unpossess·설정 변경은 다음 관측 pass에 반영된다. local possess는 getter에서 즉시 보호한다. tier 3도 가시성/Actor 숨김/net cull을 바꾸지 않는다.
- 원본 GameplayPose·montage drain·notify·단발/회전·visual full-rate 보호는 그대로다. 로컬의 MM 검색 주기·후보·IK 품질을 낮추지 않았다.

같은 호출에서만 유지되는 세 가지 계산도 정리했다. Sprint의 `(intent || allowed) && allowed`를 동일한 `allowed` 한 번 조회로 바꿨고, Melee Tick은 endpoint/radius/authority 기록 전의 Validation·AttackDefinition을 재사용한다. GAS event 이후까지 포인터/상태를 캐시하지 않는다. 무기의 hand-driven attachment 판별도 한 번만 한다. 새 영속 캐시·등록자나 전투 판정 경로를 만들지 않았다.

일반 Significance 공개 API, native MM fallback, reflected Handoff/미리보기 필드, standalone tick fallback은 역할이 달라 유지한다. Retarget/GuidedIK calibration을 합치는 새 게시 상태도 추가하지 않았다. 아직 비용 이득보다 같은 프레임의 profile/socket/mesh 교체 무효화 부담이 클 수 있기 때문이다. 기존 fixture와 제작 에셋은 삭제하지 않았다.

### 검증 결과와 범위

| 검증 | 결과 | 한계 |
| --- | --- | --- |
| 관측 수정 전 재현 | 실제 local view 1·거리300m·LegacyViewpoints0·global0·animation Near. Far 조건 2개 실패 | 수동 tier 주입 시험과 구분한 원인 재현 |
| 집중 회귀 | 19개 Success: clean17·기존 경고2·errors0 | 경고는 고립된 Character의 None socket Draw/Sheathe fixture. 실제 충돌 radius/GAS 동기 취소 전 조합을 전수 확인한 시험은 아님 |
| 실제 RHI 집중 | 30개 clean·errors0 | RecordedStrafeLand, SprintTurnCMC, VisualBudget lifecycle 및 회전/곡선 계약. 모든 clip/체형의 최종 체감 승인 아님 |
| 200명 실제 D3D12 렌더 | 전후와 local 포함 실행 모두 정상 종료, 전원 표시 조건 통과 | 단순 Entry scene·1280×720·offscreen, 실제 연결0·CMC controlled kinematics |
| local1+199 렌더 | local quality1027 samples: Local 정책·hand/foot IK·visual interval0 유지 | 최종 자연스러움 전체의 영상 검수와 별개 |
| 실제 원격 | 서버+client2·native NPC200·50ms/3% loss·parallel net·Iris=1, exit0/0/0·timeout0 | R→RMB authority/owner prediction/remote target/end 3 trial 및 AOI/FastArray. 200 authored remote player나 200접속 시험 아님 |
| direct UBT | 최종 Editor5.14초·Game51.98초 성공 | 첫 시험 include/TObjectPtr/numeric API 컴파일 오류도 raw에 보존. 새 Cook/독립 운영 Server 실행은 하지 않음 |
| 보존 | 기준 보호 콘텐츠4개·uproject 해시 동일, Config/Content 변경0, 검증 소스/스크립트714개 hash 불일치0 | 모든 외부 작업 파일을 이 변경에 포함하지 않음 |

### 성능 관찰과 남은 비용

실행 PC는 Ryzen 7 9800X3D·D3D12 RX9070 XT 환경이다. 목표 최소 사양이 아니다. 아래는 화면 캡처 요청 프레임을 제외한 단일 실행 표본이다. 캡처 후속 프레임의 영향·열·스케줄·계측 차이를 통제한 교대 비교가 아니므로 개선율을 확정하지 않는다.

| 200명 실제 렌더 구간 | 이전 | 이후 | local1 포함 이후 |
| --- | ---: | ---: | ---: |
| 유휴 평균 frame ms | 15.978 | 12.712 | 13.341 |
| 유휴 p95 ms | 17.24 | 13.18 | 15.17 |
| 전원 montage 재생 평균 frame ms | 30.499 | 30.607 | 31.322 |
| 전원 montage 관측 frames | 147 | 147 | 143 |

전체 phase3은 공격 뒤 복귀가 포함된다. 그 평균 감소를 동시 공격 성능 개선으로 표시하지 않는다. **전원 공격 구간은 개선되지 않았다.** 공격 중 현재 보호를 풀어 얻은 유휴 감소도 아니다. local 포함 실행은 workload가 달라 전후 A/B 개선율에 합치지 않는다. 정지 화면에서도 20×10 표시 배열을 확인했지만, 이를 근접 발/손 접촉 품질 검수로 사용하지 않는다.

1명 trace로 경로를 확인한 뒤 50명/120 warmup/180 measured/8조건의 전후 trace를 수집했다. no-view fixture의 직접 tier 주입과 actual visual/protected 작업량은 유지됐다. AttackBurst ABA on은 body retarget9000·RigidBody27000회가 양쪽에서 같았다. 현재 trace의 `IK Retarget Processor Run`은 누적 exclusive CPU3.094ms/tick, RigidBody inclusive5.322ms/tick(내부 Chaos4.750 포함)이다. 병렬 누적 비용을 합쳐 frame wall time으로 계산하지 않는다. 기존 profile 사전 확인 scope만 줄이는 것은 주력 이득을 설명하지 못한다.

같은 50명 단일 traced 비교의 AttackBurst ABA on World tick은 **4.659→4.958ms**로 증가했고 다른 조건에서도 증가가 남았다. 기존 ABA policy pass는 0.0122→0.0127ms/tick이었다. 이 수치만으로 원인을 observer 비용, 빌드 구성/배치, 환경 변동 중 하나로 확정할 수 없다. **전체 CPU 개선·회귀 부재는 입증하지 않았다.** 실제 local view가 있는 200명 RHI 결과와 no-view/수동 tier CPU fixture의 결과를 합산하지 않는다.

다음 고비용 검토는 source GameplayPose를 유지하면서 원격의 retarget IK·손 보정·의상 작업을 선택적으로 관리하는 영역이다. 기존 QualityPolicy와 Coordinator를 재사용하되, 현재 Body graph 안에 결합된 의상 물리를 native 임시 writer/강제 Alpha/reset으로 우회하지 않는다. source socket/notify·body contact·cloth wake와 가까운 위협의 공격 가독성을 실제로 비교할 수 있는 좁은 실험부터 해야 한다. 이 부가 표현 분리는 아직 적용하지 않았다. 동시에 동일 구성의 비계측 교대 비교로 위 CPU 증가를 분리 조사해야 한다.

원자료는 `Saved/Validation/CrowdOptimization_20261015_Execution1`, 렌더는 `Saved/Validation/WholeProjectAudit_20261013/CrowdOpt_*`, 원격은 `Saved/Validation/GroupD_20260910/CrowdOpt_Network1`에 보존했다. [이식 가능한 요약·source/DLL/asset 해시와 원자료 지문](../Reports/ProjectJ_200_Player_Optimization_2026-10-15.json)을 함께 읽는다. 에셋 저장·엔진 변경·옛 바이너리 복원·빌드/검증 중단·중복 실행은 하지 않았다. 실행용 로그·스크립트·trace·화면은 ignored Saved에 두고 영구 fixture/시험만 Source에 남겼다.

## 10월 15일 2차 — 공통 비용과 실제 소비 확인

### 적용과 검토 후 유지

전투만 대상으로 삼지 않았다. 기존 카메라·스프링암의 원격 비활성화, 궤적 프레임 중복 방지, MM 인덱스 캐시, source ABA와 visual 평가 분산·보간을 재확인했다. 이미 구현된 기능을 새로 만들지 않았다. 유휴·OTM/Strafe 이동·Run/Sprint·곡선/회전·착지/MM 복귀·리타깃·장비/의상과 전투 보호를 같은 실행 흐름에서 검토했다.

최종 runtime 변경은 기존 세 파일의 작은 정리다.

- OTM snapshot은 이미 수집한 `Data.MotionMatchingSearchPolicy`를 참조한다. 같은 프로필을 다시 조회하고 조건식으로 정책 전체 임시 값을 만들던 작업을 제거했다. 배열/heap 할당 제거로 해석하지 않는다.
- Coordinator는 source/local/단발 등의 조건으로 이미 보호 중이면 부가 메시의 보호 여부를 다시 조사하지 않고, 조사 중 첫 보호 조건에서도 종료한다. 이후 모든 메시의 평가·보간·해제 순회는 유지한다.
- 기존 animation budget pass의 allocator·CVar 조회를 pass 안의 지역값으로 재사용하고, 빈 registry의 시점 수집을 생략한다. 영속 캐시·새 manager·tick·task·timer·RPC·복제 필드는 없다. 모든 메시가 보호 중이거나 registry가 비었을 때도 allocator 조회 1회가 가능하고, ABA off에도 CVar 1회가 추가되는 작은 비용은 있다.

궤적 배열을 기존 GT snapshot에서 꺼내 재사용하는 안은 적용하지 않았다. snapshot 작성 중 이전 가속·Pivot·OneShot·Stance를 읽고, `GetAnimationLocomotionMode` BlueprintNativeEvent 경계도 있다. 먼저 초기화하면 이전 상태 의미가 바뀌며 Samples만 이동해도 BP가 일시적인 빈 배열을 읽을 수 있다. worker/pending/PoseHistory의 독립 복사도 유지한다. 측정 이득이 입증되지 않은 hand calibration 공유 캐시·visibility 캐시·setter 변경 감지 상태를 추가하지 않았다.

### 50명 비계측 A→B→B→A

A는 **1차 적용 상태**, B는 위 세 runtime 파일 정리다. `ac9e2b5a`나 과거 리팩토링 이전 코드와의 비교가 아니다. 같은 기존 v2 fixture·NullRHI·50명·8조건·실행당2회 반복·120틱 준비/240틱 측정, trace off로 총64행을 수집했다. 각 조건의 네 반복 평균 World tick이다. 제어한 이동·수동 tier·실제 연결0이며 렌더 FPS가 아니다.

| 조건 | ABA | A ms | B ms | 변화 |
| --- | --- | ---: | ---: | ---: |
| Near | off | 4.053 | 4.120 | +1.65% |
| Near | on | 4.133 | 4.174 | +0.99% |
| Far | off | 3.735 | 3.690 | −1.22% |
| Far | on | 3.829 | 3.664 | −4.33% |
| Hidden | off | 2.854 | 2.833 | −0.75% |
| Hidden | on | 1.924 | 1.945 | +1.07% |
| AttackBurst | off | 4.576 | 4.648 | +1.57% |
| AttackBurst | on | 4.599 | 4.577 | −0.48% |

**전체 조건의 개선이나 성능 무회귀를 입증하지 않았다.** 같은 A의 Far/on 반복 평균에도 3.681~4.238ms 차이가 있고 Near에서는 p95도 증가했다. 작은 정리의 전체 효과는 미확정이며, 과거 및 1차 CPU 증가 원인도 미해결이다. 큰 리타깃/물리 비용의 해결로 취급하지 않는다. p95 반복값·실제 평가 수·protected 수는 공유 JSON에 남겼다.

A끼리·B끼리 대상 source hash 차이는0, A/B 차이는 위3파일뿐이었다. A2 복사 시각 때문에 UBT가 한 번0 actions로 판단했으므로, 내용 확인과 수정 시각 갱신 후3소스를 실제 재컴파일하고 그 이후에만 A2를 측정했다. 옛 바이너리 복원·캐시 삭제는 하지 않았다. B 측정 후 기존 시험2개에 읽기 전용 진단을 추가했고, runtime3파일 내용을 유지한 최종 Editor/Game에서 기능을 다시 검증했다.

### 고비용 경로의 실제 데이터

현재 에셋의 읽기 전용 조회에서 표시 `GreatSword_Woman`은 **LOD1개**, 원본 `SKM_Quinn_Simple`은3개였다. 기존 visual lifecycle의 실제 프레임에서 다음을 관측했다.

| 관측 | Body tier | Policy retarget IK | Body LOD | Node LOD / IK LOD | Custom Force IK Off |
| --- | --- | --- | --- | --- | --- |
| Near Frame140 | Near | on | 0 | −1 / −1 | false |
| Far Frame300 | Far | off | 0 | −1 / −1 | false |
| Local Frame620 | Local | on | 0 | −1 / −1 | false |

**정책값과 생산 노드의 실제 소비가 일치하지 않는다.** 현재 Retargeter의 `Run IK Rig`는 활성이고, 좌우 팔·다리4체인과 Limb solver4개가 활성이다. Far에서도 모든 LOD를 허용하는 노드 값이 남으므로 필드가 off라는 사실만으로 retarget IK 비용이 줄었다고 판단할 수 없다. 이것이 과거 CPU 증가의 원인임을 입증한 것도 아니다.

UE5.8 node는 LOD 판정과 merged profile로 실제 IK 실행을 결정한다. LOD0만 있는 현재 표시 mesh에서는 임계값0에서도 IK가 활성이다. 엔진 `GetMergedRetargetProfile`도 force-off 값을 마지막에 설정하므로 Custom profile의 bool만 쓰는 안을 해결책으로 채택하지 않는다. 기존 Body/Family·node 설정 소유자와 입력 pin 계약을 보존해야 한다. 이번에는 node/asset/engine을 바꾸지 않았다.

PhysicsAsset의 Python property가 공개되지 않아 읽지 못한 배열은 body 수0으로 해석하지 않았다. 기존 ProductionRuntime 시험의 native 진단으로 보완했다.

| PhysicsAsset | Body / constraint 수 | 명시적 Simulated 수 | 대상 예 |
| --- | --- | --- | --- |
| cloak_Physics | 35 / 34 | 24 | `phy_cloth_cape_*` |
| GreatSword_Woman_Physics | 49 / 48 | 4 | `phy_cloth_Hair_*` |
| sho_Physics | 7 / 6 | 2 | `phy_cloth_Skirt_*` |

명시적 Simulated 뼈는 세 자산 사이에 중복되지 않는다. 공통 Default 뼈/충돌체도 각 시뮬레이션의 충돌·제약에 필요할 수 있다. 이름이나 노드3개라는 수만으로 통합·삭제하지 않는다. 기존 노드의 Alpha1·LOD−1은 보존했다. `EvaluationResetTime=.01`은 경과 world 시간과 누적 dt 차이를 판단하므로, 15Hz 주기만으로 매번 reset한다는 뜻이 아니다.

### 최종 검증과 다음의 구체적 실험

- 최종 direct UBT Editor8.51초·Game16.59초 성공. 진단용 TObjectPtr 순회의 첫 컴파일 실패도 보존했다.
- RHI 집중34개 clean 성공, error/warning/notRun/inProcess0. 관측·profile·snapshot·visual lifecycle·생산 retarget·SprintTurnCMC·기록 StrafeLand·MovingTurn/GeneralTurn/Continuity를 포함한다. lifecycle은 Near120회·Far30회 실제 갱신과90보간 프레임을 확인했다.
- 최종 local1+199의 D3D12 렌더 시험 성공, 전원200명 표시와 local full-quality1011 samples를 확인했다. 1280×720의 단순 offscreen 장면·controlled kinematics·연결0이며, 이 실행을 새로운 성능 A/B나 근접 접촉 품질 승인으로 사용하지 않는다.
- 검증 소스/설정/스크립트714개와 최종 Editor8module을 기록했다. Config/Content 변경0, 기존 보호 hash 일치. 네트워크/CMC/SSR/GAS/판정 writer는 미변경이며, 1차 실제 연결2+NPC200 결과를 재사용했다. 이번에 새 원격 접속 시험을 실행하지 않았다.

다음 고비용 실험은 **원격 retarget IK 소비를 기존 QualityPolicy에서 일관되게 연결하는 것**과 **같은 Coordinator 안에서 몸체/의상 품질을 관리하는 것**에 집중한다. 전체 retarget을 끊어 ref pose로 만들거나 GameplayPose 보호와 local solver를 낮추지 않는다. 먼저 대상 Body graph/Family/Retargeter, 기존 pin과 native writer, 원래 값·보호 hash를 고정한다. 원격 한정 비교에서 팔/다리/접지·무기 방향·곡선/회전·jump/land·장비 교체/화면 복귀를 확인하고, 가까운 직접 위협·초동/종료 보호와 보간 필요성을 비교한다. retarget 에셋의 복사본이나 두 번째 manager를 늘리지 않는다.

LOD 제작·material/section/그림자·장비 component·GPU skinning도 전반적 최적화 후보지만, LOD1개라는 사실만으로 자동 생성·삭제하지 않는다. 근접 local 형상을 보존하고 원격의 화면 점유율·view·최근 표시·기존 대상/위협을 이용할 수 있는지, 200×200 전수 관계 검색을 추가하지 않고 검토한다. 네트워크는 화면 품질과 별도 소유자로 AOI 재진입/초기 복제·접속 수 단계 증가·server tick/대역폭/순서/예측을 다룬다. 이번에 이 최적화들을 구현했다는 뜻은 아니다.

2차 원자료는 `Saved/Validation/CrowdOptimization_20261015_Execution1/Stage2*`, 최종 렌더는 `Saved/Validation/WholeProjectAudit_20261013/CrowdOpt_Stage2_Local1`에 있다. 공유 JSON의 `second_pass`에 조건·64행 요약·hash·실제 node/PA/solver 관측과 미확인 범위를 남겼다. 새 요청 프롬프트 문서는 만들지 않고 이 문서를 갱신했다.

## 10월 15일 3차 — 원격 리타깃 IK 소비

### 시험한 후보와 미채택 결정

**원격 리타깃 IK 생략 후보는 구현·실행 비교 후 채택하지 않았다. 최종 runtime은 검증된 2차 상태다.** 정책의 false를 실제 Body solver 생략으로 해석하지 않는다. 일반 이동부터 전투/착지/화면 복귀까지 조사했으며, 전투만 대상으로 삼지 않았다.

2차에서 `bEnableRetargetIK=false`여도 실제 Body의 Run IK Rig op가 계속 실행됨을 확인했다. 후보는 기존 Retarget proxy가 source quality policy와 기존 Coordinator의 관리·보호 판단을 함께 소비하는 방식이었다. 새 중요도 manager나 몸체별 품질 상태를 만들지 않았다. 비보호 원격에서만 GT에 유효 Run IK Rig 설정 전체를 읽어 해당 op의 `bEnabled`만 끄고, exposed input 뒤 원래 그래프의 한 번 평가를 감싸는 scope에서 profile을 교환·복원했다. FK·루트·손 후처리·세 RigidBody와 interpolation은 유지했다.

로컬·근거리·montage/root motion·GameplayPose·단발/급회전/공중/착지 복귀·wake·physics와 opt-out은 full 품질이었다. Family/canonical source/표준 node/Coordinator 소유권을 확인하고 asset override sets·type-wide profile·child op·nonempty node custom profile은 개입하지 않았다. partial 설정 복사와 stale version cache를 피하고 GC·재초기화·같은 주소/version의 런타임 수정도 검사했다. 엔진의 LOD 병합이 `bForceAllIKOff`를 덮어쓴다는 사실도 확인했다. 이 계약은 후속 실험의 참고 근거이며 현재 구현이라는 뜻은 아니다.

### 실제 소비와 동일 입력 품질 검사

실제 생산 Pawn/Body의 Near→Far와 **처음부터 Far→Near** 수명을 검사했다. processor 안의 Run IK Rig가 초기화를 유지하며 reduced/full로 전환되는지 읽었다. 숨김·wake·GameplayPose·local possess·opt-out, transient body/retargeter 교체, 같은 version의 chain 설정 수정, custom input·override map·재초기화와 원래 설정 복구를 함께 확인했다. 시험에 쓴 복제 asset은 transient이고 제작 asset 저장은 없었다.

최종 후보 D3D12 시험은 두 lifecycle과 ProductionRuntime 3개 모두 clean Success였다. 실제 reduced op 평가가 각각 30·38회, 보호된 full 평가가 360·317회였고 off 비교에서는 reduced가 0회였다. 같은 이동 source pose를 두 독립 processor에 넣은 20개씩 총40표본은 원본 IK 활성/초기화·reduced 비활성·source 뼈 변화까지 확인했다. 발·손·루트 위치 차이 최대는 0cm, 전체 뼈 회전 차이 최대는 약 0.000003도였고 루트 출력 동일 조건도 통과했다.

이는 한 생산 몸체의 제한된 이동 입력이다. 모든 clip·체형·무기·지형·장기 접지 품질 승인으로 확대하지 않는다. on/off 근접 화면도 확인했지만 실행 사이 MM 위상·물리가 달라 픽셀 일치를 주장하지 않는다. 기능 성공만으로 최적화 채택을 결정하지 않았다.

### 같은 바이너리 off→on→on→off 64행

같은 v2 fixture·NullRHI·50명·8조건·실행당2회 반복·120틱 준비/240틱 측정, trace off다. CVar 하나만 변경했고 네 실행의 기록된 source·스크립트 및 두 핵심 DLL/보호 asset 해시는 같았다. 각 조건 네 반복의 World tick 평균이다. 실제 연결0·controlled movement·수동 tier이며 렌더 FPS가 아니다. 이전 1/2차와 비교 기준이 다르다.

| 조건 | ABA | off ms | on ms | 변화 |
| --- | --- | ---: | ---: | ---: |
| Near | off | 4.175 | 4.191 | +0.36% |
| Near | on | 4.145 | 4.255 | +2.65% |
| Far | off | 3.672 | 3.827 | +4.21% |
| Far | on | 3.711 | 3.725 | +0.38% |
| Hidden | off | 2.880 | 2.831 | −1.71% |
| Hidden | on | 1.932 | 1.932 | −0.03% |
| AttackBurst | off | 4.758 | 4.620 | −2.88% |
| AttackBurst | on | 4.660 | 4.721 | +1.31% |

**가장 직접적인 적용 대상인 Far에서도 절감이 입증되지 않았다.** 설정 준비/복사·기존 분산 평가·병렬 임계 경로·실행 편차를 추가 분해하지 않아 위 증가를 후보만의 회귀로 단정하지 않는다. 보호로 후보가 적용되지 않는 Near/Attack에서도 방향이 섞였으므로 이들의 감소를 후보 이득으로 주장하지 않는다. p95·네 반복값·실제 visual/protected 작업량은 JSON에 보존했다. 이 근거로 200명 성능 달성을 선언하거나 더 큰 반복 부하를 이어가지 않았다.

### 제거·복구와 네트워크 경계

후보 proxy/Coordinator 변경·CVar·후보 전용 시험·wrapper 옵션은 최종 Source/Scripts에서 제거했다. 3차 시작 전 다섯 파일의 SHA-256을 확인해 **앞선 1/2차 변경이 포함된 정확한 바이트**로 복구했으며, 오래된 바이너리를 복원하지 않고 새 source timestamp로 direct UBT를 다시 실행했다. 기존 시험/제작 자산과 다른 사용자 변경은 보존했다. 측정 wrapper의 `-DisableEditorPython` 옵션은 기존 실행 방식 재현에 필요한 유효 옵션으로 유지한다. 실험 코드를 기본 off로 방치하지 않았다.

이 후보는 서버 source pose·소켓·CMC/SavedMove·GAS/SSR·복제 payload/빈도/AOI를 바꾸지 않았다. DS를 제외한 기존 visual 관리만 사용했다. SSR은 Actor/Capsule, Combat socket API/GameplayPose는 원본 GetMesh를 사용한다. Melee Notify 자체는 **발생 메시**를 사용하므로 Body에 동일 Notify를 추가 재생하는 변경은 별도 판정 계약 검토가 필요하다. 현재 원본 재생 계약은 유지한다. Body 손/Palm은 시각 무기 부착과 접촉의 소비자이므로 판정 불변을 시각 동등성의 근거로 삼지 않았다.

후보 제거 후 runtime은 2차와 동일하므로 실제 소켓의 첫 적용 2clients+NPC200·50ms/3% loss·Iris/authority/prediction 결과를 재사용한다. 새 payload나 네트워크 빈도를 도입한 것으로 표기하지 않는다. 보호된 200명 지속 전투와 WAN/200접속은 여전히 미검증이다.

복구 후 direct UBT Editor **30.45초**, Game **25.07초**가 성공했다. 기존 RHI 집중 회귀 **34개 clean Success**이며, RecordedStrafeLand·SprintTurnCMC·회전/곡선·snapshot·budget·retarget/visual 수명을 포함한다. 최종 RHI fixture는 **200명 전원 표시·local1+199, local full 품질1,029표본**과 정상 종료를 통과했다. 실제 네트워크 연결0·단순 offscreen scene·controlled movement이므로 지속 공성전/실제 local 입력 전체 품질이나 FPS 개선 인증은 아니다. 다섯 복구 파일의 시작 전 해시와 최종 바이트가 같고, 최종 Source/Scripts/Config 714개·Editor 모듈8개를 후보 측정 해시와 구분해 기록했다. Config/Content 변경0, 기존 보호 asset 해시 동일이다.

### 다음 적용의 우선순위

1. **retarget 전체와 RigidBody를 구분한 critical path**: `IK Retarget Processor Run` 전체 시간을 IK solve 비용으로 보지 않는다. 기존 trace에서 op/GT source capture/FK·goal 처리/worker wait·RigidBody 내부 Chaos를 나눠 다음 후보의 실제 절감 가능량부터 확인한다. 같은 설정·작업량으로 비교하고 무관한 광역 부하는 추가하지 않는다.
2. **원격의 부가 표현 분리**: 세 PA는 서로 다른 simulated bone 역할을 유지한다. 필요 시 생산 Body ABP의 cosmetic physics만 기존 Coordinator 정책으로 소비하는 최소 변경을 검토한다. source gameplay pose·손 접촉·root motion은 유지하며 정지/재개·teleport·사망/부활·장비/체형 교체에서 의상 튐과 누적 상태를 먼저 검증한다. 이번에 구현하지 않았다.
3. **렌더/콘텐츠 경로**: 생산 Body LOD가 1개라는 사실은 확인했지만 자동 생성·삭제 근거는 아니다. local LOD0을 보존하고 원격 silhouette·대검 가독성·sections/materials·skin·shadow를 실제 GPU 병목과 함께 확인한 뒤 제작 변경을 정한다. 200명 전원 표시를 유지한다.
4. **전송과 서버 별도 예산**: 기존 AOI/Iris·이동 예측·compact semantic event를 재사용한다. 전송 빈도를 animation tier에 묶지 않고 실제 연결 단계·초기 복제/재관련·지터/손실·중복/취소·SSR 과거 시점·late equipment revision을 확인한 뒤 조정한다. 매 프레임 pose/clip 복제나 200×200 관계 전수 검색을 추가하지 않는다.
5. **local 유지와 정리**: 입력/카메라·snapshot·MM 검색/선택·한 프레임 중복 방지의 기존 구현을 유지한다. BP/reflection 경계가 있는 snapshot 배열 재사용·접촉 공유 캐시는 입증된 비용과 무효화 계약 없이는 도입하지 않는다. 이름/직접 참조 0만으로 fallback·테스트·제작 asset을 삭제하지 않는다.

원자료는 `Saved/Validation/CrowdOptimization_20261015_Execution1/Stage3`, CPU는 `Saved/Validation/AuthoredAnimation_20261005/CrowdOpt_Stage3_{A1,B1,B2,A2}`다. 후보 source snapshot·최종 복구 source·모듈 해시와 정상 종료 근거를 분리하고 공유 JSON의 `third_pass`로 연결했다. startup의 기존 `LogAutomationTest: Error: Condition failed`는 2차/후보 실행 모두에서 named test 시작 전 발생하므로, named test clean 통과와 전체 로그 무오류를 구분한다.

## 10월 15일 4차 — 실제 연산 비용과 의상 데이터 분리 검증

### 현재 남긴 변경

기존 `ProjectJ.Retarget.ProductionRuntime` 시험에 **읽기 전용 `-ProjectJRetargetCostAudit`**를 추가했다. 새 runtime manager·품질 상태·복제 필드를 만들지 않는다. flag가 없으면 기존 12틱 수명 회귀가 유지된다. flag가 있으면 정지/제어 이동 구간에서 엔진의 retarget op 30평가 EMA, 실제 생성된 physics actor 수, 명목 PA의 충돌/제약/부모 역할을 읽어 기존 audit txt에 기록한다. deferred simulation이 켜진 node의 내부 actor 읽기는 생략해 비동기 시뮬레이션과 경합하지 않는다.

진단은 world tick 뒤 갱신을 확인하고, 뼈 게시가 없는 틱은 대기한다. bone finalization은 보간도 포함하므로 120개 게시를 120번의 독립 graph 평가로 주장하지 않는다. op 값은 엔진이 **마지막 실제 실행까지 누적한 30평가 EMA**이며, 두 구간의 끝에서 읽는다. 새 frame 평균·GT critical path·FPS가 아니다. 진단을 위해 기존 ABA/시각 보호를 강제로 끄지 않는다.

최종 Source의 추가 변경은 이 **Editor 전용 시험 한 파일**이다. 3차 최종 source 714개와 대조해 다른 Source/Scripts/Config 변경은 없다. transient 축소·원본/복제 비교 코드, `PhysicsUtilities` 의존성, 새 helper 및 실험 flag는 제거했다. 실험 원자료·마지막 source는 ignored Saved에 보존했다. 기존 1/2차 runtime·테스트·사용자 변경을 그대로 둔다.

### 확인한 비용과 실제 대상

기존 1차 50명 trace의 AttackBurst/ABA on 구간을 재사용해 asset별 timer를 나눴다. 180틱의 누적 inclusive CPU를 틱으로 나눈 값이며 worker 중첩을 포함한다. 현재 전체 frame 시간으로 합산하지 않는다.

| 기존 trace 범위 | ms/틱 | 의미 |
| --- | ---: | --- |
| RigidBody 평가 전체 | 5.322 | 시뮬레이션과 node 준비/출력을 포함 |
| 망토 PA simulation scope | 3.147 | RigidBody 전체의 약59%; 24개 dynamic |
| 몸체/머리 PA simulation scope | 1.051 | 4개 dynamic Hair와 운동학 구성 |
| 스커트 PA simulation scope | 0.570 | 2개 dynamic |
| retarget processor 전체 | 3.094 | IK solve만의 timer가 아님 |

새 최종 단일 생산 Pawn D3D12 진단의 이동 구간은 retarget node EMA **0.063968ms**, Pelvis **0.009123ms**, FK **0.013269ms**, Run IK Rig **0.013667ms**, Root Motion **0.000392ms**, curve remap **0.000112ms**였다. node 전체와 op 합계의 차이는 설정/pose 적용 등 다른 범위를 포함하므로 모두 특정 함수 비용으로 귀속하지 않는다. 단일 끝점 EMA와 과거 50명 trace는 조건이 다르며, IK만 생략한 3차의 무이득을 인과적으로 전부 설명하는 측정도 아니다.

실제 LOD0의 세 시뮬레이션에서 아래 actor 수를 직접 읽었고 idle/이동에서 같았다. 명목 PA 개수만으로 실행을 단정한 것이 아니다. 동적 분류는 현재 생성 actor의 `IsSimulated`이며 모든 LOD의 활성/절전 상태를 증명한 수치는 아니다.

| PA 역할 | 실제 전체 | Dynamic | Kinematic | 직접 충돌/동적 제약 역할 없는 명목 후보 | 후보 중 simulated 뼈의 조상 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 망토 | 35 | 24 | 11 | 6 | 1 |
| 몸체/머리 | 49 | 4 | 45 | 44 | 5 |
| 스커트 | 7 | 2 | 5 | 3 | 0 |

**마지막 두 열은 삭제 목록이 아니다.** 부모 body는 초기화·locked translation 보정·pose scale propagation에 쓰인다. cooked/complex geometry, constraint별 collision suppression, 실제 LOD, base/simulation space, shared PA의 ragdoll·다른 graph·외부 cloth 소비도 별도 경계다. 현재 명목 potential collider가 0인 것도 모든 런타임 충돌이 없다는 전수 인증으로 사용하지 않는다. full-body PA의 다른 역할과 의상 전용 데이터를 무조건 합치거나 원본에서 body를 삭제하지 않았다.

### transient 축소 실험과 대조군

source PA를 저장하지 않고 복제한 뒤, simulated/contact potential/dynamic joint anchor/base bone과 이들의 skeleton 전체 조상을 보존하는 후보를 만들었다. engine `PhysicsAssetUtils::DestroyBody`로 pair index·constraint·bounds를 재구성했다. 후보 크기는 **35→30, 49→10, 7→4**였다. 실제 배포 에셋은 생성·교체하지 않았다.

같은 source를 소비하는 두 몸체를 한 world에서 비교했다. 첫 시험은 비교 메시의 갱신 조건이 부족해 품질 근거에서 제외했다. 이를 고친 뒤 양쪽 bone 게시·동일 source pointer·retarget 기본 포즈 차이를 확인했다. 원본/원본 대조군은 840표본에서 위치0cm·회전 약0.000003도였다. 제어 이동·곡선·공중 모드/지면 복귀·teleport·균일 scale 변화·실제 LMB montage를 입력했지만, 모든 Run/Sprint/Jump/Land semantic 경로를 도달 assertion으로 확인한 시험은 아니다. world/component scale 자체까지 전수 비교한 품질 승인도 아니다.

축소본은 같은 검증에서 최종 뼈 최대 위치 **150.588cm**, 회전 **179.922도** 차이로 실패했다. 그러나 **body를 전혀 제거하지 않은 단순 transient 복제 대조군도 53.232cm/179.737도 차이로 실패**했고, retarget 기본 포즈 위치 차이는 두 경우 모두 0cm였다. 따라서 축소 규칙 자체의 품질 회귀로 단정할 수 없다. 원본과 복제본의 physics 초기화·내부 상태/geometry까지 동등한지부터 해결해야 한다.

solver legacy 변환 가능성은 설치 엔진 소스를 보고 가설로 검사했으나, 실제 linear/manifold/cull 값은 원본·복제본이 같았다. reflected constraint/body 기본값 차이도 0개였고 보존 복사 후에도 실패했다. 이 가설을 원인으로 확정하거나 특별한 runtime 보정으로 남기지 않았다. 강제 reset·허용 오차 확대·clip 예외로 통과시키지 않는다.

**적용 보류:** 원본 대비 복제 대조군의 동등성도 확보되지 않아 데이터 축소를 채택하지 않았다. 성능 교대 부하·새 200명 반복 렌더·새 네트워크 부하는 진행하지 않았다. 품질 검증 전에 후보 수 감소를 절감 효과로 보고하지 않는다.

### 보존·검증과 다음 실행 경계

최종 direct UBT Editor **8.84초**·Game **10.65초**가 성공했고, opt-in 진단을 포함한 생산 리타깃·VisualBudget lifecycle·AuthoredObserverProfile **3개 RHI 회귀가 clean Success**다. 현재 runtime은 3차 최종 상태와 같으므로 당시 34개 RHI·local1+199 전원 표시/로컬 품질1,029표본, 첫 실제 server+client2/NPC200/lag50/loss3 결과는 유효한 기존 근거로 재사용한다. 이를 새 실행이나 200소켓 시험으로 바꾸어 표기하지 않는다. 서버 이동·예측·GAS/SSR·원본 pose/socket·복제 빈도/AOI 변경은 없다.

다음 적용은 **원본과 단순 복제의 physics 동등성 확보 → 한 의상 역할만 분리 → 동일 입력의 포즈/접촉·재개 검증 → 실제 critical path/교대 CPU → 200명 표시/원격 회귀** 순서다. 기본값 비교만으로 통과하지 않고, 첫 차이 시점의 초기 pose·reset/scale·생성 shape/질량/COM·constraint runtime 상태와 현재/저장 프로필의 실제 소비를 좁혀 확인한다. 현재 망토의 큰 timer는 우선순위 근거이며, 단순한 kinematic 제거의 절감량은 미확인이다. 생산 기본 PA·래그돌·서버 collision은 유지하고 필요하다면 시각 node 전용 데이터로 분리한다. 새 actor별 worker queue·거리 manager·전송 권위 상태는 필요하지 않다.

원자료는 `Saved/Validation/CrowdOptimization_20261015_Execution1/Stage4`다. 실패한 최초 비교·원본 대조군·단순 복제 대조군·보존 가설·최종 읽기 진단을 각각 보존했다. 공유 JSON의 `fourth_pass`는 적용된 진단과 미채택 실험을 구분한다. 기존 startup Condition failed 로그와 named test 결과도 구분한다.

## 10월 15일 5차 — 물리 복제의 초기화 동등성과 비용 판정

### 원인과 실제 수정

이전 시험은 **평가 이력이 다른 두 시뮬레이션**을 비교했다. `SetAnimInstanceClass`의 초기화는 설정에 따라 즉시 `TickAnimation(0)`과 뼈 평가를 수행한다. 그 뒤 복제본에만 PA override를 설치하면 UE5.8 `RigidBody::PreUpdate`가 포인터 변경을 감지해 physics를 다시 생성한다. 이 재생성은 AnimInstance의 새 수명과 같지 않아 기존 평가 이력과 속도/target이 비대칭이 됐다. 첫 프레임부터 실제 질량·관성·COM·감쇠·기본 관절 프로필은 같았지만 운동학 속도와 동적 출력이 달랐다.

양쪽 `InitializeAnimation` 시점을 맞춘 진단은900표본 위치0cm·최대회전약0.000003도로 통과했다. 이 호출은 원인 분리용이며 생산 runtime의 강제 reset으로 추가하지 않았다. 특정 내부 필드 하나의 책임이나 엔진의 일반적인 hot PA 교체 결함까지 확정한 것은 아니다.

영구 Editor 회귀 **`ProjectJ.Retarget.SecondaryPhysicsEquivalence`**는 실제 생산 Pawn/몸체 그래프를 쓰되 독립 시험 메시 두 개에 `bUseRefPoseOnInitAnim`을 등록/클래스 설치 전에 설정한다. 양쪽 미갱신을 확인하고 PA 복제·body index/bounds 구성을 끝낸 뒤 같은0초 초기 평가와 world tick을 시작한다. 생산 몸체에는 PA 교체/reset을 하지 않는다. 같은 source·컴포넌트 변환·전체 뼈 위치/회전/scale·실제 사용 PA·physics 참여를 검사한다. simulation 부재나 deferred 내부 읽기를 조용히 통과시키지 않는다.

### 동등성 결과와 한계

단순 복제와 transient 축소본은 각각900표본 위치0cm·최대회전0.000003415도·scale차이0을 기록했다. canonical source와 retarget base 위치도 같았다. 실제 actor는 원본91개, 후보44개였다. 후보 크기는 망토35→30, 몸체/머리49→10, 스커트7→4다. 동적30개와 충돌·동적 관절 anchor·base bone·skeleton 조상은 보존했고 complex collision 경로는 지원 범위에서 제외했다.

정지·제어 속도200/600·곡선 회전·Falling/Walking 전환·teleport·균일scale0.85/1.15/1·실제 LMB montage를 포함했다. 모든 Run/Sprint/Jump/Land 의미 경로, 실제 지면 접촉, 모든LOD/체형/장비·wake·장기 의상 안정성의 승인은 아니다. 4차 실패를 축소 규칙의 확정 회귀로 해석하지 않되 이번 통과도 전체 제작 품질 승인으로 확대하지 않는다.

### 동일 바이너리 비계측64행과 별도 trace

50명·NullRHI·8조건·각2회 반복·120틱 준비/240틱 측정, 전체 복제본→축소본→축소본→전체 복제본 순서다. 양쪽 모두 데이터 설치 뒤 같은 graph 수명으로 준비했고 측정 중 PA 교체/reset은 없다. 네 실행의 source·스크립트와 기록한 DLL/보호 asset 지문이 같다. 모든 조건에서 실제 사용 PA와 첫 캐릭터의91/44 actor를 확인했다. World tick wall time은 task 완료를 포함하며 외부 engine frame·렌더/연결은 제외한다.

| 조건 | ABA | 전체 ms | 축소 ms | 변화 |
| --- | --- | ---: | ---: | ---: |
| Near | off | 3.915 | 3.909 | −0.16% |
| Near | on | 3.989 | 3.855 | −3.35% |
| Far | off | 3.466 | 3.442 | −0.70% |
| Far | on | 3.447 | 3.420 | −0.81% |
| Hidden | off | 2.676 | 2.700 | +0.89% |
| Hidden | on | 1.705 | 1.697 | −0.44% |
| AttackBurst | off | 4.356 | 4.351 | −0.11% |
| AttackBurst | on | 4.424 | 4.415 | −0.19% |

Near/Far/Hidden의 visual update 수와 공격 보호 ticks는 양쪽 같다. AttackBurst/ABA on 전체본 한 반복은 종료 후 예산 평가 수가 달랐으므로 순수 고정 작업량 절감으로 해석하지 않는다.

별도50명 trace 각1회/8조건의 RigidBody 누적 inclusive scope/tick은 약8.75–30.84% 감소했으나, 변경하지 않은 retarget processor도 약−24.98%부터+1.05%까지 변했다. Near/ABA off는 RigidBody5.981→4.400ms, retarget3.220→2.678ms였다. worker 중첩·선점·trace overhead를 포함하는 scope 시간이며 활성 CPU·GT critical path·FPS로 합산하지 않는다. `WaitForTasks` 합계도 모든 thread의 대기를 포함한다. trace 한 쌍만으로 축소의 독립 절감량을 확정하지 않는다.

### 최종 판정과 보존

**초기화 비교 문제는 해결했고 데이터 축소는 현재 제작 경로에 채택하지 않았다.** 비계측 이득이 대부분1% 안팎이며 Far/공격 이득도 작고 trace의 대조 scope 변동을 분리하지 못했다. 추가 PA3개와 원본 변경 동기화·로드/Cook·모든 제작 소비자 검증의 부담을 늘릴 근거가 부족하다. 망토 동적 solve·표시 몸체LOD/section 등의 실제 비용은 후속 원인 분리 대상이다.

축소 builder·후보 분기·벤치 옵션·PhysicsUtilities/PhysicsCore 추가 의존성은 제거했다. 기존 Source/Scripts/Config714개는4차 최종 hash와 같고 새 Editor 동등성 회귀 한 파일만 추가했다. 기존1/2차 runtime·테스트·사용자 변경과 제작 PA/Body ABP는 보존했다. 서버 CMC/SavedMove·예측·GAS/SSR·판정·복제/AOI와 로컬 품질·200명 표시 정책도 변경하지 않았다.

최종 direct UBT Editor15.25초·Game24.44초 성공, RHI 집중4개가 warning/error/notRun/inProcess0으로 통과했다. 새 회귀는 실제91 actor/30 dynamic과 물리 운동도 확인했다. named test clean과 startup의 기존 Condition failed 로그는 구분한다. runtime/asset이 같으므로 기존34개RHI·local1+199 표시/품질1029표본·실제2연결+NPC200/lag50/loss3 근거를 재사용하고 새200명/네트워크 부하는 반복하지 않았다. Cook·전체 시각 승인이나200연결 검증을 새로 수행했다는 뜻은 아니다.

원자료와 후보 소스는 ignored `Saved/Validation/CrowdOptimization_20261015_Execution1/Stage5`, CPU/trace는 `Saved/Validation/AuthoredAnimation_20261005/CrowdOpt_Stage5_Physics_*`다. JSON의 `fifth_pass`에 실행·포즈·64행·trace·최종 source/asset 지문을 보존한다. 후보 옵션은 현재 지원 API가 아니며 새 PC에서 실험을 재현하려면 기록한 source snapshot이 필요하다. 영구 회귀는 현재 Source와 위 Automation 이름으로 실행한다.

## 10월 15일 6차 — 다른 에셋 호환성과 실제 몸체 LOD

### 공통 런타임과 에셋별 제작의 경계

거리 관측·소스 ABA·시각 갱신 조정은 기존 컴포넌트/서브시스템을 재사용한다. 캐릭터·망토·헤어 이름이나 특정 경로로 runtime을 분기하지 않는다. 다만 새 에셋에 적합한 LOD·Cloth 바인딩·체형/손 접촉까지 자동 제작하거나 모든 에셋의 품질을 검증했다는 뜻은 아니다.

이번에 다음 호환 경계를 보완했다.

- 소스 ABA는 이미 외부 tick 제어 중인 컴포넌트를 새로 인수하지 않는다. 자신이 등록한 ABA 상태는 계속 유지하며, 외부 제어가 해제되면 기존 등록 경로로 복귀한다.
- 시각 조정기는 탐색 시점과 매 갱신에 같은 적합성 검사를 쓴다. 마지막 main/PP AnimInstance 소멸, LeaderPose 전환, 물리 시뮬레이션, RootMotionFromEverything, opt-out 등으로 부적합해지면 기존 제어를 반환한다. PP montage도 full-rate 보호에 포함한다.
- 독립 Cloth는 실제 simulation instance뿐 아니라 메시의 활성 Clothing 바인딩으로도 감지한다. 시각 조정기와 기존 follower fallback 양쪽에서 제외하여 엔진 Cloth의 별도 tick/visibility 계약을 보존한다. 현재 cloak는 LeaderPose 의상이며, 이 판정은 다른 독립 Chaos Cloth까지 동일한 예산을 검증했다는 뜻이 아니다.
- 몸체 resolver는 같은 owner의 등록된 direct child 중 고유한 명시 역할(tag)을 우선하고, 없으면 고유한 native main/PP retargeter를 사용한다. body와 retargeted hair/garment가 여러 개이거나 명시 역할도 충돌하면 임의의 첫 메시를 선택하지 않는다. 명시 body가 asset 로딩 중이면 역할만 유지하고 bone 소비는 대기한다. 먼저 로드된 retargeted garment로 역할을 넘기지 않는다. 미등록 placeholder·다른 actor의 부착물도 몸체 역할에서 제외한다. 장비/손 소비자는 기존 소켓·skeleton 검사와 fallback을 유지한다.
- 시각 제어 해제 시 schedule·평가 허용 상태도 원래 대기 계약으로 돌아간다. fallback의 기존 authored tick 복원/재활성화 계약을 새 구조로 교체하지 않았다.

외부 시스템이 이미 관리 중인 raw tick flag에 나중에 직접 덮어쓰는 것까지 자동으로 소유자를 식별할 수는 없다. 새로운 allocator/LODSync/cloth·groom 통합은 기존 요구 API와 `NoVisualBudget` 경계를 사용하고, 같은 필드를 동시에 쓰지 않아야 한다. 새 전역 관리자나 물리/상태 복제 계층을 추가하지 않았다.

### 실제 비용 원인과 채택 범위

생산 body는 **30,070정점/52,566삼각형/6section·LOD1개**였다. 8개 LeaderPose 의상까지 합하면 약60,516정점/94,683삼각형/22section이다. canonical Quinn에는 이미3LOD가 있었지만, 표시 body의 `bSyncAttachParentLOD`가 보이지 않는 source를 따라 LOD0에 고정되는 경로도 있었다. 단순히 데이터에 LOD를 추가하는 것만으로는 실제 선택이 바뀌지 않았다.

기존 조정기에서 **고유한 표시 body·다중 LOD·보이지 않는 canonical source·원격**인 경우에만 parent LOD 동기화를 해제해 엔진의 화면 크기 LOD를 사용한다. 로컬은 기존 parent/LOD 정책을 그대로 사용한다. forced rendered/streaming LOD, MinLOD, 플랫폼 설정에는 쓰지 않는다. 역할/소유권 해제 시 바꾼 parent sync만 반환한다. 기존 authored parent sync=false는 그대로 유지한다. LeaderPose 의상은 엔진의 body 추종 경로를 사용하며 자체 LOD가 없는 의상 데이터까지 자동 축소하지 않는다.

UE5.8의 기본 skeletal reduction으로 **기존 GreatSword_Woman 메시 하나에 LOD1·2**를 추가했다. 별도 캐릭터/PA/몸체 에셋을 복제해 제작 경로를 늘리지 않았다. screen-size 기본 임계값0.35/0.18, 삼각형 목표50%/25%, 기존 influence 한도·법선·뼈 경계·morph remap을 사용했다. 이 수치는 다른 체형/헤어에 강제할 공통 기본값이 아니다.

| 실제 LOD | 정점 | 삼각형 | section | RequiredBones |
| --- | ---: | ---: | ---: | ---: |
| 0 | 30,070 | 52,566 | 6 | 338 |
| 1 | 15,788 | 26,282 | 6 | 338 |
| 2 | 8,303 | 13,140 | 6 | 338 |

PA3개·RigidBody solve·retarget IK는 그대로다. LOD1·2에서도 동적30개가 활성화됐고900쌍 포즈의 최대 위치0cm·회전약0.000003415도·scale차이0이었다. 이것은 모든 clip/착지·접촉·체형·morph/대체 skin-weight profile의 시각 승인과 다르다. 원본 메시 백업과 비교 근거는 ignored Stage6에 보존한다.

### 동일 소스·바이너리 교대 렌더 비교

transient 후보B→원본A→A→B 순서로 각2회, 실제 D3D12 offscreen1280×720·local1+199·유휴8초/일회 동시공격8초를 측정했다. 각 phase 첫5행과 캡처 전후2행은 제외했다. 아래 값은 각 실행 평균의 평균이다. GPU frame counter는 지연될 수 있고 전투 phase에는 montage 종료 뒤 구간도 포함한다.

| 조건 | 원본 GPU ms | 후보 GPU ms | 변화 | 원본 frame ms | 후보 frame ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 유휴 | 10.526 | 6.188 | −41.21% | 12.597 | 12.637 |
| 일회 공격 phase 전체 | 11.030 | 6.665 | −39.57% | 19.100 | 19.591 |
| 실제200 montage 동시 활성 행 | 11.922 | 7.421 | −37.75% | 33.518 | 33.001 |

**GPU 비용 감소는 반복됐지만 전체 frame/CPU 개선은 확정하지 않는다.** 후보B2의 CPU 측 프레임 변동이 컸고 p95도 일관되게 개선되지 않았다. worker 물리/retarget 비용·지속 공격 최적화가 해결됐다는 결과가 아니다. 후보는 원격199명이 실제LOD2를 사용했고 로컬은LOD0, 전원 표시·기존 local 품질 조건을 유지했다. 앞선 LOD 데이터만 만들고 실제 선택은LOD0이었던 시험3개는 성능 근거에서 제외했다.

### 재사용·보존·남은 제작 검증

새 에셋은 기존 Body/Family profile·skeleton·명시 몸체 역할·소켓·tick prerequisite를 먼저 연결한다. 기존 LOD가 있으면 재사용하고, 독립 Cloth/추가 뼈·Groom/LODSync·다른 retarget source가 있으면 해당 소비 계약을 확인한다. 가까운 거리/LOD 전환·장비 관통·헤어/망토 안정·손 접촉·화면 복귀·아바타 교체를 실제 에셋으로 검수한다. LOD 수만 늘리거나 Compatible Skeleton 표시만으로 적용 완료로 보지 않는다.

일회 생성/저장 helper와 transient 후보 옵션·추가 editor 의존성은 정리했다. 기존 엔진의 skeletal mesh LOD 제작 기능을 사용하고, 영구 회귀는 실제 설치된 LOD0/1/최하위/복귀의 포즈·물리 참여를 검사한다. 명시한 원본 백업과 비교하는 `ProjectJ.Retarget.MeshLODPreservation`은 읽기 전용이며 LOD0 geometry/weights/section BoneMap/active·required bone 배열과 재질·skeleton·PA·reference pose를 검사한다. 일반 시험 실행에서 메시 생성/저장은 없다.

엔진 ABA/URO·병렬 애니메이션·LeaderPose·skeletal reduction과 화면 크기 LOD를 실제 사용/검증했다. RigidBody DeferredSimulation은 엔진 소비를 읽어 검토했으며 이번에 실행·채택한 기능은 아니다. 다음 애니메이션 평가까지 결과 소비가 미뤄지므로15/30Hz에서는 접촉 지연이 커질 수 있고 solve 작업량을 자동으로 줄이지 않는다. Animation Sharing·Groom/Chaos Cloth의 대체 예산 경로도 적용 완료로 주장하지 않는다.

서버 CMC/SavedMove·GAS·SSR·판정 포즈/소켓·replication/AOI는 이번 LOD에 맞춰 낮추지 않았다. 시각 몸체의 render LOD와 서버 판정의 권위를 분리하고, 기존 게임플레이 pose 요구/full-rate 보호를 유지한다. R19/R20·TIP·OTM/Strafe·Run/Sprint·Pivot·단발/MM·Land·지속 곡선0.35초/100cm/3도/85%의 선택/재생 코드는 바꾸지 않았다. 미래 에셋 전수 품질이나200실제 연결·공성전 완료는 여전히 미검증이다.

원자료는 `Saved/Validation/CrowdOptimization_20261015_Execution1/Stage6`, 교대 렌더는 `Saved/Validation/WholeProjectAudit_20261013/CrowdOpt_Stage6_ActualLOD_*`다. `CandidateSources`/`AuthoringSources`는 폐기한 시험 코드를 재현하기 위한 기록이며 현재 지원 옵션이 아니다. 공유 JSON의 `sixth_pass`에 적용·비교·최종 검증과 재사용 근거를 연결한다.

최종 에셋 로딩 경계 보완 후 direct UBT Editor5.41초·Game9.65초가 성공했다. 저장본의 새 프로세스 확대 RHI 회귀62개는60 clean Success·2 warning Success/실패0이며, 경고는 두 기존 socket None 손 접촉 fixture의 Draw failed 로그다. 이를 clean62개로 표기하지 않는다. 이후 원본 백업을 읽은 정확 보존 회귀5개와 에셋 로딩 경계를 포함한 최종6개도 모두 clean Success였다. 일회 생성 도구/실험 분기를 제거한 최종 바이너리의200명 렌더는928표본 모두 전원 표시·localLOD0/기존 품질·remote199LOD2를 확인했다. 직전 저장본 실행의1,084표본도 별도 보존했다. 유휴/공격 캡처를 확인했으나 작은 원격 body 화면은 LOD 전환·모든 morph의 근접 품질 승인을 대신하지 않는다.

실제 Iris 서버+client2·NPC200·parallel net/all relevant·50ms lag/3% loss의 기존 애니메이션·R→RMB 전투 시험도 세 프로세스exit0/timeout0/startup Python error0으로 통과했다. 이는200연결 시험이 아니다. 마지막 missing-asset 역할 분기 보완 후에는 새6개 RHI와200명 렌더를 실행했다. 앞선 원격/확대 회귀와 Cook은 동일한 loaded-body 활성 경로·권위 코드·동일 에셋의 근거로 재사용하며 새 원격 실행으로 표기하지 않는다. 변경 메시 하나의 Windows single-package/no-refs Cook은 전체 의존성·완전 패키지 실행 검증과 구분한다.

**Cook 실행 영향:** 첫 제한 Cook은 엔진exit0/1package 저장에 성공했지만 기본 Zen 출력 때문에 loose 파일을 가정한 wrapper가 실패했다. 출력 폴더와 별개로 공유 `Project_J` Windows Zen oplog가 재생성됐다. 소스/제작 원본·이전 시험 로그를 삭제하지 않았으며, 이전 공유 Cook 저장소는 다음 전체 Cook에서 재생성해야 한다. 후속 제한 Cook은 `-SkipZenStore`와 새 출력 폴더로 분리해exit0·대상 메시1개의 loose `.uasset` 출력을 확인했다. 프로젝트 Config나 캐시를 일괄 삭제해 해결하지 않았다.

최초 transient LOD generator의 model/LODInfo 비대칭으로 난 async rebuild assert와 synthetic Cloth flag fixture의 실패도 Stage6에 보존했다. 전자는 원본 저장 전 실패였고 엔진의 순차 LOD 생성/개별 compilation 완료로 교정했다. 후자는 실제 Clothing 바인딩을 가진 에셋으로 바꿔 검증했다. 최종 성공 결과로 초기 실패 근거를 덮어쓰지 않는다.

## 10월 15일 7차 — 새 캐릭터·의상·장비의 공통 LOD 제작

6차의 일회 생성 시험과 달리, 이번 요청으로 **재사용할 Editor 제작 경로**를 추가했다. 자세한 사용법·보존·예외·실패 복구는 [메시 LOD 제작](../../Animation/Authoring/Mesh_LOD_Authoring.md)을 따른다. 기존 Editor 모듈·도구 메뉴·엔진 Data Validation을 사용하고, 공통 설정 에셋 하나가 native skeletal LODSettings와 static reduction options를 소유한다. 기존 체인은 그대로 보존하며 누락된 표면 메시만 선택 목록으로 검사/preview/생성한다. runtime 축소·임포트 자동 일괄 생성·새 예산 manager는 없다.

Cloth·Groom/카드 헤어·morph·별도 weights/deformer·Nanite·기존 native LOD 정책·잠재 플랫폼 MinLOD를 검수 대상으로 돌린다. geometry만으로 hair/WPO/graph의 의미를 완전히 알아낼 수 없어 명시 `Surface` 검수가 필요하다. 생성한 geometry도 실제 ABP/PP의 node LODThreshold·물리/접촉·장착을 확인해야 한다. 에셋별 품질 검수를 통과했다는 자동 인증은 아니다.

저장은 격리된 unattended commandlet에서 복제본 검증→원본 백업→동일 native 생성→staging/보호값 비교→파일 지문 확인→패키지 교체 순서다. 실패 복구와 처리 수 대조를 포함하며 기존 LOD/dirty/readonly/sidecar/redirector/프로젝트 밖/중복 목록을 덮어쓰지 않는다. native minimal API를 무리하게 상속하거나 live-editor 전용 subsystem을 commandlet에 가정하지 않고, native 설정 구성과 공개 메시 빌드 API를 사용한다.

정적 무기 LOD를 나중에 추가해도 로컬 품질이 내려가지 않도록 공용 무기 표시 Actor의 기존 수명에 최고LOD 제어를 연결했다. 실제 local PlayerController의 빙의·소유자 변경을 사용하고 PlayerState 도착에 의존하지 않는다. authored/외부 forced 값을 보존하며 원격/해제/종료에 자동LOD 제어를 반환한다. actor tick·RPC·복제/판정 상태나 재생 구조는 늘리지 않았다.

실제 별도 명령에서 새 skeletal/static 시험 입력2개를 각각1→3LOD로 생성·저장했고, 새 프로세스에서 같은 Apply를 반복했을 때 둘 다 보존되며 파일 지문이 같았다. 저장된 skeletal LOD0은 원본 백업과 full geometry/weight/BoneMap/reference/PA/material 비교를 통과했다. 다른 실제 캐릭터 Manny의 복제본도 같은 기본 설정으로 생성 검증했다. 최종 보존·제작·플랫폼 최소값·엔진 검증·로컬 무기 수명·생산 retarget/physics·canonical melee/SSR의 RHI13개는 모두 clean Success였다. 이후 public dependency와 미사용 include/복사만 정리했으며 동작 검증을 새 실행으로 바꾸어 표기하지 않는다.

초기 실패도 `Saved/Validation/MeshLODAuthoring_20261015`에 보존했다. native MinimalAPI의 미export virtual 상속은 구성으로 바꿨고, auto UV streaming density 재계산은 authored material/slot/수동 override와 분리했다. live-editor static subsystem 부재와 원본 linker 잠금으로 발생한 최초 Apply 실패는 원본 지문 복구를 확인한 뒤 공개 native asset build/로더 해제로 고쳤다. 최초 batch parser의 쉼표 종료는 명시 목록 전체 처리와 입력/출력 개수 대조로 교정했다. 테스트 fixture의 abstract UObject·PlayerState 없는 local 판별·Manny 최소LOD/월드 종료 경고도 원인에 맞게 정리했다.

생산 대조에서 작업자가 Inspect 대신 Apply를 선택해 무기1개에도 LOD가 생성된 실행은 별도 기록했다. 해당 작업의 직전 backup와 before/after 지문을 확인해 **SM_Sword를 바이트 단위 원복**했고, 이후 실제 body/cloth/sword의 읽기 전용 Inspect는 생성0이었다. 기존 생산 asset·이전 시험 자산을 일괄 교체하지 않았다. 이번에 배포할 신규 에셋은 공통 Editor 설정 하나다. 임시 통합 입력은 ignored Saved에 보존하고 기존 시험 코드/fixture는 유지한다.

최종 direct UBT Editor14.60초·Game27.30초가 정상 종료했다. 최종 바이너리의 실제200명 렌더는 유휴/공격1,113표본 모두 전원 표시·localLOD0을 유지했다. 실제 Iris 서버+client2·NPC200·parallel net/all relevant·50ms lag/3% loss의 애니메이션·전투 회귀도 세 프로세스exit0/timeout0/startup Python error0으로 통과했다. 200명 렌더와 실제2연결은 각각의 검증이며 운영200연결 시험이 아니다.

세부 결과와 새 실행/재사용 범위는 공유 JSON `seventh_pass`에 연결한다. 새 CPU/GPU 반복 성능 비교나 망토 physics 절감을 주장하지 않으며, 다른 모든 에셋의 시각 품질·운영200연결·완전 패키지 검수는 미완료다.

### 사용자 시각 검토와 몸체 LOD 채택

후속 요청으로 개발자 전용 `/Game/Developers/LODReview/L_MeshLODReview`를 추가했다. 기존 몸체 LOD0/1/2를 컴포넌트별로 고정하고 몸체만·의상 정면·의상 뒷면의9개 몸체와 기존 의상48개를 배치했다. 생성 후 별도 에디터 재로딩에서 강제LOD와 LeaderPose 참조가 유지됨을 확인했고, 입력12개 파일의 지문이 같았다. 실제 렌더가 선택한LOD를 관측한 것과 컴포넌트 설정 재로딩 확인은 구분한다. [열기·카메라·검토 범위](../../Animation/Authoring/Mesh_LOD_Authoring.md#에디터에서-lod-비교와-적용-판단)를 따른다.

이 장면은 같은 기준 자세의 geometry 비교용으로 PP ABP·시뮬레이션을 장면 컴포넌트에서만 껐다. 생산 메시·물리·게임 레벨·Config를 변경하지 않았다. 현재 몸체에 이미 있는6차LOD1/2는 유지하고, **다른 생산 에셋에 추가 적용하는 것은 사용자 검토 후 판단**한다. 정적 장면의 확인은 모든 동작/Cloth 전환의 승인과 다르다. 재현 스크립트는 기존 검토 맵과 사용자 배치를 덮어쓰지 않는다.

추가 화면 검증 중 최초 Slate 캡처가 이미지 없이 종료 요청 후 대기하여, 사용자 허용을 받은 캡처용 PID8224만 종료했다. 이후 재진입하는 캡처 콜백 대신 임시 native SceneCapture2D로 실제 RHI 화면을 읽고 저장 없이 종료하는 경로를 사용했다. 회전 인자·줄별 카메라 가림·검은 의상의 환경광은 새 검토 맵 안에서 교정하고, 앞선 맵 백업·실패/중간 캡처도 `Saved/Validation/MeshLODReview`에 보존했다. 새 성능 비교나 전체 움직임 품질 승인으로 계산하지 않는다.

최종 저장본의 새 프로세스에서9개 몸체의 방향/요청LOD·48개 의상 연결·입력12개 파일 보존을 확인하고, 몸체만·정면·뒷면·전체의 실제 RHI 캡처4장을 읽었다. 마지막 캡처 실행은 정상exit0이며 결과는 `Saved/Validation/MeshLODReview/Complete`에 있다. viewport screenshot이 아닌 native SceneCapture2D 출력이고, 에디터/게임의 동적 품질 전수 승인과 구분한다. 공유 JSON `lod_review_scene`에 최종 맵·스크립트·캡처 지문과 이전 실패 처리를 연결했다.

**2026-10-16 현재 GreatSword 몸체LOD1/2를 채택했다.** 사용자가 외형 차이를 구분하기 어렵다고 확인하고 물리 동등성을 조건으로 적용을 허용했다. 현재 메시 정점30,070/15,788/8,303과 강제모델1/2/3을 MCP로 다시 읽었으며, 실제 에디터의 메시 LOD 배색도 오른쪽LOD0흰색·가운데LOD1빨강·왼쪽LOD2초록으로 구분됐다. 동일LOD 표시라는 의문을 설정값 외 실제 렌더 진단으로도 확인했다.

물리는 이전 실제900표본의LOD1/2 동적30개 유지·전체91 actor·위치0cm/회전약0.000003415도/scale0 근거를 재사용했다. 생산 메시·PA3개·ABP/원래 source 경로의 지문이 당시 최종본과 같아 새 시험 실행으로 표기하지 않는다. 현재 생산 체인과 원격 화면 크기 LOD/로컬LOD0을 채택 상태로 유지하고, 기존 적용을 다시 생성하지 않았다. 이번 변경은 검토 맵의 승인 표지6개와 재현 스크립트·문서 상태이며 서버 CMC/GAS/SSR·물리 solver·다른 의상/헤어 LOD에는 쓰지 않았다. 기존GPU 감소 근거와 미확정인 전체frame/CPU 효과는 그대로다.

## 아직 결론낼 수 없는 부분

현재 CPU 증가 원인과 실제 frame critical path의 인과 분리, 원격 공격 중 부가 표현을 줄였을 때의 체감·의상 안정성, 실제 local CMC 입력을 포함한 지속 200명 공성전, 운영 서버/WAN/200연결의 한계는 미확인이다. 목표 하드웨어/FPS와 대표 전투 밀도도 미정이다. 한 번의 유휴 표본 감소로 최적화 완료나 모든 원격/장비/체형 품질 보존을 확정하지 않는다. 기존 보호를 약화하지 않은 첫 적용이며, 후속 부가 표현·네트워크 빈도 변경은 별도 측정/기능 검증이 필요하다.
