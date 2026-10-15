# 데이터 주도형 이동 컨텍스트 확장 가이드

**sample graph 보정 분기 계약:** selector는 아직 Update되지 않은 Input.Player의 lazy 연결을 전제로 판단하면 안 된다. 엔진이 graph 진입 전에 설치한 현재 sample helper의 Player와 authored pose index/asset 일치를 확인한다. 전방 clip을 재사용하는 대각 Jump/Land의 실제 보정 활성까지 확인해야 하며 선택 존재만으로 완료 처리하지 않는다. [R20 원인·수정·회귀](../../Review/Audits/Project_J_Strafe_Diagonal_Warp_Repair_2026-10-15.md)를 참조한다.

## 목적

Project J의 플레이어 전신 애니메이션은 **큰 이동 문맥(Context)** 과
**문맥 안의 콘텐츠 프로필(Profile)** 을 분리한다.

```text
Locomotion Context              Content Profile
------------------              -----------------------------------
OnFoot                           직업, 장비, 전투 상체 레이어
Mounted                          말/용/늑대, 지상/비행, 라이더 포즈
Swimming                         수영 스타일, 장비 제한, 수중 이동 세트
Vehicle                          차량 종류, 좌석 역할, 조종/승객 포즈
Transformed                      변신 종족, 형태, 전용 능력/애니메이션
```

이것은 데이터 주도형 구조가 맞다. 단, 역할은 엄격히 나눈다.

- **Data Asset(DA)**: 콘텐츠 선택과 튜닝 값. 예: 사용할 Rider AnimBP,
  태그, 손 IK 사용 여부, 전환 시간, 좌석 포즈.
- **C++**: 서버 권한, 복제, 입력 소유권, 실제 이동 모드, 상태 전이 검증,
  스레드 안전 애니메이션 스냅샷.
- **Blueprint/ABP**: DA가 고른 애셋을 사용해 상태 머신, Blend Space,
  Linked Anim Layer, 소켓/IK와 시각 표현을 작성.

따라서 콘텐츠를 하나 추가할 때 `BP_Player` 또는 `ABP_Player`를 복제하거나
마스터 AnimGraph에 종별 분기를 늘리지 않는다.

## 현재의 고정 계약

### 방향별 Chooser의 콘텐츠 범위

Strafe 방향은 이동 sector이고 Foot은 접촉 변형이다. Stand 착지의 입력 의도와 잔여 이동 방향도 서로 다른 값이다. 필요한 대각 clip이 없으면 기존 제작 계약처럼 FL/FR→F, BL/BR→B 등의 authored alias를 둘 수 있지만, **입력 대각 조건과 Light/Heavy·Foot·선택 출력을 보존**한다. native sector를 Forward나 과거4방향으로 강제 바꾸어 빈 행을 숨기지 않는다. R19에서 Stand Land의 대각 조건16행을 보완했고 원본19행·비활성 제작 행·출력을 유지했다. [재현·영향 회귀와 실행 한계](../../Review/Audits/Project_J_PC_Source_Data_Reconciliation_2026-10-15.md)를 함께 확인한다.

Chooser를 확장할 때 root 파일 목록뿐 아니라 result/fallback의 Asset/Evaluate/Nested table, 내부 표와 각 context/출력 소비자를 대조한다. PoseSearch cost로 선택하는 겹치는 조건·양발 행은 단순 중복이 아니다. 미설치 예제의 GUID context나 enum을 확인 없이 현재 player root로 바꾸지 않는다.

`EProject_JAnimationLocomotionMode`는 전신 문맥만 선택한다.

```text
ABP_Humanoid_Master
  OnFoot chain                 -> Enum Blend: Default Pose
  MountedLocomotion linked layer -> Enum Blend: Mounted Pose
  Enum Blend                  -> Output Pose
```

`Mounted`는 “탈것에 올라탔다”라는 큰 문맥이고, `Horse`, `Wyvern`,
`Carriage` 같은 구체 모델 목록이 아니다. 종/모델/코스메틱을 Enum에 추가하지
않는다. 그 선택은 프로필과 Linked Layer가 담당한다.

현재 `BP_Player`와 `ABP_Player`는 호환용 기본 경로로 유지한다. 새 기능의
런타임 연결은 `UProject_JMountedAnimationLayerComponent`와 Rider Profile이
담당하므로, 플레이어 BP를 탈것마다 복제할 필요가 없다.

## 새 콘텐츠를 추가할 때의 판단 순서

| 질문 | 예 | 작업 방향 |
| --- | --- | --- |
| 기존 문맥 안에서 포즈만 달라지는가? | 말/용의 Idle, 비행 라이더 | 기존 `Mounted` + 새 Rider Profile/ABP |
| 기존 문맥 안에서 그래프가 크게 달라지는가? | 2인승 승객, 전차 포수 | 기존 `Vehicle` 또는 `Mounted` 안에서 전용 Linked Layer/ABP |
| 이동·입력·카메라·충돌 소유권까지 달라지는가? | 수영, 조종 차량, 변신 | 새 또는 기존 전신 Context + C++ 상태 구현 |
| 단발 행동/전투 표현인가? | 공격, 캐스팅, 피격, 감정표현 | 새 Context가 아니라 몽타주/오버레이 |

새 Enum 값은 마지막 선택이다. 같은 전신 처리 규칙을 공유할 수 있다면,
프로필과 태그로 해결하는 편이 마스터 그래프와 런타임 비용을 안정적으로
유지한다.

## 공통 구현 체크리스트

수영, 차량, 변신처럼 실제 새 전신 문맥을 추가할 때는 아래 순서를 따른다.

### 1. 게임플레이 계약을 먼저 정의

- 누가 Pawn을 Possess하는가? 플레이어 자신, 탈것, 차량 중 하나를 명확히 한다.
- 서버가 전이를 허용할 조건은 무엇인가? 전투, 행동 중, 사망, 지형, 좌석 여유,
  쿨다운, 수역 등을 서버에서 검사한다.
- 입력, 카메라, 충돌, 이동 컴포넌트, 무기/전투의 소유권을 표로 정한다.
- 취소·강제 해제 조건을 정한다. 사망, 탈것 파괴, 물 밖 이탈, 차량 폭발,
  컷신, 맵 이동이 대표적이다.

### 2. C++의 권한과 복제 구현

- 서버 RPC에서 요청을 재검증하고, 최종 상태는 복제된 속성/컴포넌트/액터로
  전달한다. 클라이언트 ABP가 상태를 결정하면 안 된다.
- 필요한 최소 상태만 복제한다. 예: `VehicleSeatRole`, `bIsInWater`,
  `TransformationId`, 이동 속도/방향/비행 단계. 매 프레임 포즈 결과나 DA
  전체를 복제하지 않는다.
- `GetAnimationLocomotionMode`에 **권위 있는 문맥 선택 조건**을 추가한다.
- 애님 프록시의 게임 스레드 스냅샷에 ABP가 필요한 값만 복사하고,
  `GetThreadSafe...` getter로 노출한다. 워커 스레드 AnimGraph에서 Pawn,
  Mount, Vehicle 또는 DA를 직접 조회하지 않는다.
- 상태 변경 이벤트에서만 Linked Layer를 갱신한다. Tick에서 `LinkAnimClassLayers`
  또는 동기 에셋 로드를 반복하지 않는다.

### 3. DA와 콘텐츠 계약 추가

새 문맥에 여러 콘텐츠 변형이 예상되면 전용 프로필 DA를 만든다.

```text
DA_VehicleRiderProfile (예시)
  - AnimationLayerClass       : Soft Class Reference
  - Tags                      : Animation.Vehicle.*
  - SeatRole                  : Driver / Passenger / Gunner
  - CameraPolicy              : FirstPerson / Chase / Fixed
  - HandIKPolicy              : None / SteeringWheel / Reins
  - TransitionBlendTime

DA_TransformationProfile (예시)
  - FormId / Tags
  - AnimationLayerClass       : Soft Class Reference
  - Movement tuning reference
  - Ability set reference
  - Camera policy
```

- DA는 **정적 콘텐츠 정책**만 가진다. 런타임 체력, 속도, 현재 좌석 점유,
  쿨다운 같은 동적 상태는 컴포넌트/ASC/복제 상태에 둔다.
- AnimBP 클래스는 Soft Reference로 두고, 로컬 플레이어에서 상태 변경 전에
  비동기 프리로드한다. 원격 플레이어의 아직 타지 않은 모든 콘텐츠를 미리
  로드하지 않는다.
- 같은 행동을 공유하는 콘텐츠는 같은 프로필을 사용한다. 예: 말·엘크·늑대가
  같은 지상 라이더 레이어를 공유할 수 있다.

### 4. 에디터/ABP 작업

- `ABP_Humanoid_Master`에는 해당 Context 핀 하나와 Linked Layer 진입점만
  추가한다. 구체 종별 State Machine은 자식/전용 AnimBP에 둔다.
- 전용 Rider/Vehicle/Swim AnimBP는 해당 Animation Layer Interface를 구현한다.
- 속도/방향/좌석 역할/비행 단계 같은 getter만 State Machine과 Blend Space의
  조건으로 사용한다. 플레이어 입력을 직접 읽지 않는다.
- OnFoot 전용 노드(Motion Matching, standing Aim Offset, Foot Placement,
  Leg IK)는 비-OnFoot 문맥에서 실행 또는 영향이 없도록 C++ 스냅샷과 그래프를
  함께 정리한다. 전용 레이어에서 필요한 IK만 다시 작성한다.
- 소켓, IK, 카메라 오프셋은 프로필/Blueprint 기본값으로 조정하고, 매 프레임
  Blueprint Cast로 찾아가지 않는다.

### 5. 검증과 성능 확인

- Dedicated Server, Listen Server, 2인 이상 PIE에서 탑승/하차·좌석 변경·강제
  해제·늦은 Join을 확인한다.
- 원격 클라이언트에서도 올바른 Linked Layer가 늦은 로딩 후 적용되는지 확인한다.
- 애님 디버거와 Unreal Insights로 전환 순간의 async load, AnimGraph 시간,
  Motion Matching search, IK 비용을 측정한다. “분기가 있다”는 것만으로 성능을
  단정하지 않는다.
- 군중 상황에서는 거리/가시성 기반 Animation Budget, Update Rate Optimizations,
  탈것 메시 LOD, 먼 플레이어의 IK·발 배치 비활성화를 별도 정책으로 둔다.

## 문맥별 추가 시 필요한 작업

| 기능 | C++ 작업 | DA/에디터 작업 |
| --- | --- | --- |
| 새 지상 탈것 | 보통 불필요. 기존 Mount 계약 재사용 | Rider Profile, 탈것 BP, 전용/공유 Rider ABP |
| 비행 탈것 | 비행 상태가 기존과 다르면 상태/스냅샷 추가 | 비행 Rider State Machine, 소켓/IK |
| 다인승 탈것 | 좌석 예약·복제·하차 검증 필요 | Seat Role Profile, 운전석/승객/포수 ABP |
| 수영 | 수역 판정, 이동/전투 제약, Context 선택·스냅샷 필요 | Swimming Layer, Blend Space, 수중 카메라/VFX |
| 조종 차량 | Possess/좌석/입력/카메라/파괴/복제 필요 | Vehicle Profile, 조종/승객 Layer, 핸들 IK |
| 폼 체인지 | 형태 권한, 충돌/이동/능력 전환, Context와 복제 필요 | Transformation Profile, 전용 Mesh/ABP/카메라 |
| 감정표현·스킬 | 보통 새 Context 불필요 | Montage/Slot/상체 Overlay |

## 피해야 할 구조

- `EProject_JAnimationLocomotionMode`에 말 이름, 차량 이름, 코스메틱 이름을
  계속 추가하는 방식
- `ABP_Humanoid_Master`에서 Blueprint class cast로 종별 포즈를 선택하는 방식
- 클라이언트 ABP가 “탑승 가능/수영 중/운전석”을 판정하는 방식
- 모든 원격 캐릭터의 Soft Class를 미리 동기 로드하는 방식
- 새 기능을 위해 `BP_Player`/`ABP_Player`를 복제해 독립 경로를 만드는 방식
- 아직 구현하지 않은 수영/차량/변신을 위해 빈 Enum 핀과 빈 레이어를 미리
  추가하는 방식

## 기능 추가 전 짧은 설계 문서 템플릿

새 이동 기능 하나마다 구현 전에 아래 항목을 짧게 작성한다.

```text
기능: 예) 4인승 마차
전신 Context: Vehicle (기존 사용 / 새 Context 불필요)
Possess 대상: 운전석만 마차 Pawn, 승객은 Player 유지
복제 상태: 마차 Actor, SeatRole, SeatIndex, 승하차 상태
서버 검증: 거리, 좌석 비어 있음, 전투/사망/행동 태그, 안전 하차 위치
프로필 DA: DA_VehicleRiderProfile_Carriage
애님 레이어: Driver / Passenger / Gunner 중 필요한 것
스냅샷 getter: SeatRole, Speed, Steering, HandIKTargets
성능 정책: 원거리 승객 IK 비활성화, Layer async preload 범위
PIE 검증: 2~4인, 늦은 Join, 좌석 경쟁, 파괴/강제 하차
```

## 관련 문서

### 2026-10-14 이동 수치 제작 계약

이동 프로필의 속도·회전·가속·제동·마찰·방향 배율은 finite/nonnegative 공통 validator로 검사한다. 방향 속도 배율의 곱도 finite여야 한다. 런타임의 Player movement publisher는 선택한 CMC 출력 5개를 모두 검사한 뒤 함께 게시하며, 잘못된 tuple은 기존 정상 출력에 부분 적용하지 않는다. 기존 0 허용 항목과 기본 수치를 유지한다. 단발 지속 곡선 기준 0.35초·100cm·3도·85%는 변경하지 않았다.

제작 IsDataValid, 런타임 진단과 실제 CMC publication 회귀의 증거는 [재점검 기록](../../Review/Audits/Project_J_Reaudit_2026-10-14.md)에 있다. 설정 validator의 통과와 실제 모든 클립·Chooser 품질은 별도 검증이다.

- [탈것 시스템 구조](../../Gameplay/Architecture/MountSystemArchitecture.md)
- [전투·로코모션 구조](../../Combat/Architecture/Combat_Animation_System.md#layers)
- [전투 애니메이션 조합](../../Combat/Architecture/Combat_Animation_System.md#composition)

