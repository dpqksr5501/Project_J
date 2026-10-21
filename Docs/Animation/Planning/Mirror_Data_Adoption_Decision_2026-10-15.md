# Mirror Data 도입 보류 결정

> **과거 소스 경로 안내 — 2026-10-21:** 아래 현재 파일이 없는 소스 경로는 당시 구현의 위치를 텍스트로 보존했다. 이를 현행 API나 실행 가능한 제작 단계로 해석하지 않는다. 이동·애니메이션의 현재 계약은 [플레이어 로코모션](../Architecture/Player_Locomotion_Architecture.md)을 따른다. 당시 수치·실패·외부 출처는 유지한다.

2026-10-19 후속은 [GASP 5.8 전환 책임 정리](../Architecture/Player_Locomotion_Migration_2026-10-19.md)를 따른다. 아래는 당시 판단·로그·검증 이력이다. Mirror 신규 도입 보류는 유지하지만, 당시 locomotion 전체 유지 결정을 이후 단발 수명·대각 Pivot 개선의 금지로 해석하지 않는다.

결정일: 2026-10-15. 조사 기준: `a28b665718a802660e4dd2ae5494f27e84fa1ee3`.

**Mirror Data를 새로 도입하지 않고 현재 locomotion을 유지한다.** 사용자는 현재 움직임이 자연스럽다고 판단했다. 특정 연결의 개선 목표가 없는 상태에서 후보를 일괄 확대하지 않는다. 나중에 특정 동작 연결이 어색하다고 확인되면 그 구간만 다시 조사한다. 기존 MDT를 삭제하거나 미러를 지원하는 엔진·통합 재생 경로를 제거한다는 결정은 아니다.

## 역할과 도입을 보류하는 이유

Mirror Data는 왼팔과 오른팔, 왼발과 오른발 등을 어떻게 대응시킬지 알려주는 표다. 미러링하면 같은 원본에서 좌우 방향·반대 발·반대쪽 몸 자세의 후보를 얻을 수 있다. 예를 들어 회전 동작을 연결할 때 현재 발 자세에 원본보다 미러 자세가 잘 맞는다면 연결이 부드러워질 가능성이 있다. 이 예시는 기능의 용도이며 Project_J에 해당 결함이 있다는 관찰은 아니다.

Project_J는 좌우 회전·여러 이동 방향·양발 authored 후보를 이미 갖추고 있다. 미러 후보가 기존 후보와 겹칠 수 있으며, 후보 수 증가만으로 자연스러움이 좋아졌다고 판단할 수 없다. 미러링은 새로운 Walk gait, 전후 이동 방식, 전방 점프에서 새로운 대각 점프를 제작하는 기능도 아니다.

후보 확대는 검색 데이터·메모리와 CPU 검색 비용에 영향을 줄 수 있고 재생 시 좌우 반사 처리도 필요하다. 후보 수가 두 배라고 전체 애니메이션 비용이 두 배가 되는 것은 아니다. 이번에는 미러 적용 A/B나 CPU/GPU 측정을 하지 않았으므로 증가폭과 품질 개선율은 정하지 않는다. 반복 성능 측정과 대규모 부하는 계속 보류한다.

## 현재 데이터와 실행 경로

| 항목 | 2026-10-15 확인 상태 | 의미 |
| --- | --- | --- |
| MDT | `MDT_UEFN_Mannequin`, X축, root motion 미러 활성, Bone 87행: 중앙 self 11개와 좌우 38쌍 | 표의 존재만으로 실제 미러 사용이나 품질을 입증하지 않는다. `FullSkeleton`은 뼈 행 갱신 범위다. |
| 좌우 의미 매핑 | MDT의 Curve·AnimationNotify·SyncMarker 행은 0개 | 뼈 매핑과 발 접촉·소리의 좌우 의미는 별개다. |
| PSS | 6개 모두 `SK_Mannequin`, MDT `None` | 현재 설정 경로는 미러 후보를 공급하지 않는다. |
| skeleton | MDT는 `SK_UEFN_Mannequin`. UEFN의 compatible 목록에 `SK_Mannequin`이 있다. | 이름 차이만으로 불호환이라 단정하지 않는다. 적용 시 필요한 뼈·참조 자세·LOD의 실제 매핑을 확인한다. |
| PSD | 53개, 460항목 중 445활성, 전 항목 Original Only | authored 설정의 수치다. 동시에 검색하는 DB 수나 실제 index pose 수가 아니다. |
| 일반 MM | 검색 결과의 mirror 여부와 schema의 MDT가 엔진 BlendStack player로 전달된다. | 기존 미러 재생 기반을 다시 만들 필요가 없다. 현재 mirrored 결과의 실제 선택·출력은 미검증이다. |
| Chooser 단발 | `FAnimNode_ProjectJLocomotion`의 단발 `BlendTo`는 `false, nullptr`로 재생한다. 선택적 entry-MM은 asset/time만 소비한다. | PSS·PSD 설정만 바꿔 단발 미러가 완성되지 않는다. 향후 범위에 포함한다면 mirror 결과 전달도 검토한다. |
| sample 보정 | 통합 스택은 각 player의 asset/time/mirror/table을 읽는 보정 연결을 이미 갖춘다. | mirrored sample의 첫 프레임과 블렌드 중 실제 입력 일치는 별도 검증 대상이다. |

PSS 6개는 `PSS_Player`, `PSS_InAir_Player`, `PSS_Combat`, `PSS_Combat_Run_Continuity`, `PSS_Combat_Run_ForwardTrajectory`, `PSS_Combat_TurnInPlace`다. 기존 authored 감사 도구는 `searchIndexNotInspected=true`이며 실제 built index의 mirrored pose 수는 확인하지 않았다.

## 전체 locomotion에 대한 판단

| 영역 | 현재 근거와 판단 |
| --- | --- |
| Idle와 Start·Stop 연결 | Idle loop와 방향·양발 Start/Stop이 있다. Strafe Run Start/Stop은 각각 8방향×양발을 갖춘다. 현재 단발 경로를 유지하며 특정 발·자세 연결 문제를 재현했을 때만 검토한다. |
| Walk·Run·Sprint 가감속 | 현재 이동 gait 판정은 Run/Sprint 중심이고 별도 Walk PSD군이 없다. Mirror로 Walk 공급·가감속·전환 타이밍을 해결하려 하지 않는다. |
| OTM과 작은 곡선 | Run Cycle 활성 7개는 전방 loop와 Small/Tight/Wide Arc 좌우다. 이미 좌우 곡선이 있으므로 미러의 추가 가치는 반대 발·자세가 실제 연결 공백을 채우는지에 달려 있다. |
| Strafe 전후·측면·대각 | Combat Run Cycle 70개와 ForwardCycle 14개에 Arc·Box·Diamond·Hourglass·8방향 loop가 있다. 이들을 동시에 모두 검색한다는 뜻은 아니다. 전면 미러 대신 필요한 구간만 검토한다. |
| Sprint 방향 | Start/Stop과 Cycle은 전방 계열 중심이다. 좌우 반사로 새로운 측면·후방 Sprint를 얻지는 못한다. |
| 이동 회전·방향 반전 | 좌우 GeneralTurn·양발 후보와 후진 Reface 좌우 후보·정책이 이미 구현돼 있다. 과거 조사에서 제안했던 기능을 다시 구현하지 않는다. |
| Pivot·제자리 회전 | Pivot의 활성 cardinal 조건과 TIP의 좌우 90/180도 authored 단발을 유지한다. Mirror는 비활성 대각 조건이나 상태 자격을 활성화하지 않는다. |
| 점프·낙하 | 방향·양발 후보와 Fall loop를 유지한다. 대각 alias와 Orientation Warping 계약을 미러 도입 이유로 바꾸지 않는다. |
| 이동·제자리 착지와 MM 복귀 | R19의 대각 Stand 착지 조건과 R20의 sample 보정 초기화 수정을 보존한다. 미러 후보 추가로 선택·보정 소비·복귀 수명 문제를 대신 해결하려 하지 않는다. |

## 재검토 조건과 최소 비교

재검토는 사용자가 특정 연결의 어색함을 확인하거나 재현 로그·연속 영상에서 좌우 발 또는 자세 후보의 실제 공백이 드러날 때 시작한다. 먼저 입력·실제 이동·선택 조건·재생 시점·보정·리타깃 문제를 구분한다. 후보가 있는데도 소비가 잘못된 경우에는 그 원인을 먼저 다룬다.

미러가 필요한 가설이 남으면 기존 반대쪽 authored와 미러 결과를 비교한다. 실질 중복이면 후보를 늘리지 않는다. 필요할 때만 테스트 전용 MDT·PSS·PSD 사본으로 원본-only A와 해당 클립만 미러를 허용한 B를 비교한다. 동반 DB의 공유 schema·Normalization 계약을 유지하고 가중치·검색 간격·blend를 함께 튜닝하지 않는다. 변경 대상·보존 조건·검증 방법을 먼저 제안한 뒤 진행한다.

검증은 실제 운영 ABP·CMC·리타깃 follower를 사용해 양쪽 회전과 초기 발 상태를 맞춘다. 검색 결과뿐 아니라 기여하는 각 sample의 asset/time/mirror/table·weight, 접촉 중 발 미끄러짐·자세 튐·입력 반응·선택 전환을 확인한다. 후보 수나 검색 cost 감소만으로 합격시키지 않는다.

| 영향 | 재검토 시 필요한 확인 |
| --- | --- |
| 발과 위상 | `contact_l/r`, 좌우 IK 제어 커브와 `Phase` 의미를 확인한다. 대표 Start·Stop·Arc·착지에 실제 접촉 커브가 있다. 조회한 대표 클립의 sync marker는 비었지만 전체 클립으로 일반화하지 않는다. |
| Foley·노티파이 | 현재 커스텀 Foley Notify는 원본 `FoleyEvent.Side`를 그대로 전달한다. MDT의 notify 이름 매핑만으로 객체 내부 Side가 반전되지는 않는다. 실제 좌우 소리 위치·시점·중복 억제를 검증한다. |
| root와 후단 보정 | 검색 특징·실제 포즈·미래 root 추출에서 같은 미러 상태를 사용한다. OW·Steering·Offset Root·IK와 각 sample의 초기화·블렌드 입력을 함께 확인한다. |
| 체형·무기 | 리타깃 체형별 발·손 접촉, 대검의 primary/secondary grip·소켓·비대칭 전투 자세를 확인한다. 무장 전신 미러를 일괄 적용하지 않는다. |
| 네트워크 | 서버 이동 권위·CMC 예측과 클라이언트 표현을 분리한다. 새 이동 writer·RPC·재생 스택·상태 소유자를 불필요하게 추가하지 않는다. |

제자리 회전·OTM/Strafe·Run/Sprint·Pivot·단발/MM 복귀·Land·전투·장비·리타깃 및 지속 곡선 **0.35초·100cm·3도·85%**를 보호한다. 기존 테스트·제작 자산은 보존한다. Traversal은 이번 판단 범위에서 제외한다.

## 근거와 시점 구분

- [통합 Locomotion 구현](Unified_Locomotion_Implementation_2026-10-12.md)과 [회전 연속성 구현](GASP_ProjectJ_Naturalness_2026-10-06/07_LOCOMOTION_CONTINUITY.md): 이미 구현한 공통 스택과 player별 보정 계약.
- [GASP 비교](GASP_ProjectJ_Naturalness_Comparison_2026-10-12.md): 당시 후진 Reface 비교 기록. 현재 후보·정책은 후속 코드와 구분한다.
- [R19 소스·데이터 대조](../../Review/Audits/Project_J_PC_Source_Data_Reconciliation_2026-10-15.md)와 [R20 보정 복구](../../Review/Audits/Project_J_Strafe_Diagonal_Warp_Repair_2026-10-15.md): 선택 공백과 sample 소비 오류를 구분한 기존 수정·검증.
- 현재 단발 재생 (`Source/Project_JCharacter/Private/Animation/AnimNode_ProjectJLocomotion.cpp`, 당시 경로·현재 파일 없음), entry-MM 결과 소비 (`Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Presentation.cpp`, 당시 경로·현재 파일 없음), [Foley Notify](../../../Source/Project_JCharacter/Private/Animation/Project_JAnimNotify_FoleyEvent.cpp): 실제 실행 계약.

GASP 로컬 원본은 5.7.4, Project_J는 5.8.3이다. GASP CMC ABP·Chooser의 기존 구조 자료는 현재 원본 해시와 일치하지만 현재 실행 pawn/CMC·Mover 경로와 미러 선택 사례는 확인하지 않았다. GASP의 자연스러움을 미러 한 가지의 효과로 설명하지 않는다.

이번 읽기 전용 조회의 로컬 원자료는 ignored `Saved/Validation/MirrorResearch_20261015_Read3/Read.log`와 `Saved/Validation/MirrorResearch_20261015_Native/User/Saved/Validation/MirrorResearch_20261015_Current.json`에 있다. 대표 클립 메타데이터와 authored 후보는 확인했으며, 새 렌더 A/B·성능 검증은 하지 않았다. 조회용 비표시 에디터는 감사 출력 완료 후 일반 종료 명령에 반응하지 않아 해당 프로세스만 정리했다. 실행 중인 빌드·시험은 중단하지 않았다.
