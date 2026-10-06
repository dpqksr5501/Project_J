# 04. 비교와 고도화 로드맵

이 문서는 제안(I)이다. 확인된 근거와 가설을 구분하며 제안 자체를 구현 완료로 표현하지 않는다. 자세한 구현 증거는 01-03, 합격 기준은 05를 따른다.

**최상위 목표는 전체 애니메이션 자연스러움 개선이다.** 큰 반전은 그중 한 항목이다. 이동·예측, 데이터·검색, 동작 연결·수명, 자세·발 접지, 원격·군중 예산을 각각 평가한다. 기존 조사 전체를 다시 시작할 필요는 없으며, 현재 상태의 변경점과 미확인 시각효과를 후속 검증으로 채운다.

## 1. 왜 GASP의 자연스러움이 Project_J에서 덜 보일 수 있는가

G/A/S: GASP는 조건부 다중 PSD와 MM 내부 클립별 보정을 사용한다. Project_J는 단일 PSD 선행 선택과 비어 있는 MM 내부 보정, 일반 이동 root Release를 사용한다. Project_J 큰 Turn에는 활성 중에도 speed>=180 자격이 적용되고, 기존 trace에 짧은 수명이 기록되어 있다.

I/U: 이것은 큰 반전에서 효과가 제한될 수 있는 구체 설명이며, 일반 이동 전체가 덜 자연스러운 원인을 입증한 결론은 아니다. 전체 차이는 아래 역할별 구현과 N1-N6 baseline으로 평가한다. 현재 화면을 통제된 A/B 실험으로 관측한 인과관계는 아니다. 실제 출력·입력·데이터·보정의 문제를 분리하고, 관련된 선행 계약을 확인한 뒤 필요한 최소 변경을 시험한다.

## 2. 역할 비교표

| 역할 | GASP 확인 경로 | Project_J 확인 경로 | 결론 |
| --- | --- | --- | --- |
| 물리 이동 | CMC 빠른 회전, 별도 시각 root 처리 | CMC 제한 회전, TIP authored yaw 별도 | 게임플레이 계약 차이, 상수 복사 금지 |
| 미래 이동 | CMC 예측+collision pass | CMC 예측+Strafe Facing 보완 | Project_J 핵심 예측은 존재 |
| 후보 구성 | 조건부 여러 PSD | AssetSet/C++ 단일 PSD | 일반 신규 후보 경쟁 범위 차이 |
| Cycle 데이터 | 방향·곡선·전환 다양성 | Combat에 Arc/Box/Diamond/Hourglass | 이미 좋은 데이터 보존 |
| Pose 특징 | 발 상대/속도/heading, Default/Jump 구분 | 발/골반 위치속도+Z, Player/Combat 구분 | 단순 같은 이름의 기능 차이 아님 |
| 단발 선택 | 기본 MM, 실험 SM 별도 | 단일 leaf animation+authored start time | 의도적 역할 분리 |
| 단발 수명 | 활성 분기별 상태/검색 정책 | episode/revision/epoch/cache/hold | 현재 검증된 구조 보존 |
| MM 내부 보정 | OW/ResetRoot/일반 Steering/TIP | Input→Result | 큰 반전 보정의 후보 차이 |
| 외부 보정 | 실험 경로 별도 | OW→TIP Steering | 일반 Start/Land Steering은 비활성 |
| root offset | 이동 중 accumulate/interpolate | 일반 release, TIP interpolate | combat/network 책임 정리 필요 |
| 발 | DB Stops tag, Foot/Leg | StopRequested, Foot/Leg | 기능 부재보다 policy/data 비교 |
| 검색 안정화 | throttle0+continuing/reselect/범위 | baseline.05+explicit edge+예산 | MMORPG 비용 이점 보존 |
| 원격/군중 | 샘플 기반 정책 | visibility/significance/ABA/follower/one-shot 정책 | GASP를 단순 single-player라고 단정하지 않음 |

## 3. 인과관계별 분석

### 3.1 입력 반응과 몸 방향

**효과:** 입력은 빨리 반영되지만 몸/발은 이전 움직임에서 연결되어야 한다(I).

**근거:** GASP future trajectory+collision, root accumulate/interpolate; Project_J intent 선행 기록, CMC bounded yaw, latest trajectory, Strafe catch-up 예측(G/A/S).

**원리:** 경로/몸/Facing/camera가 서로 다른 값이라는 전제에서 미래 의도를 검색에 전달하고 물리 회전과 visual 회전의 책임을 나눈다(I).

**차이:** Project_J 자체 이동을 부드럽게 하고 GASP는 빠른 capsule에 visual continuity를 더 크게 사용한다. 마우스 180도에서도 world path가 반전되지 않는 Strafe 상황을 전진Turn으로 강제하지 않는다.

**비용·검증:** physics 변경은 게임플레이/서버 회귀가 크다. 먼저 현재 query/target과 final visual yaw를 기록한다. sampling/collision은 local-near 범위부터 실험한다.

### 3.2 후보와 발걸음 지속성

**효과:** 작은 회전은 자연스러운 Cycle 구간에서 해결하고 불필요한 전환을 줄인다(I).

**근거:** Project_J CombatCycle76seq, single PSD, pose features/continuing; GASP conditional overlapping database pools(G/A/S).

**원리:** 작은 회전의 다양한 root path와 발 위상이 같은 후보 집합에 있으면 비용으로 적절한 pose/time을 선택할 수 있다. 다른 PSD를 닫는 policy는 그 PSD와의 신규 후보 비교를 막는다(I).

**차이:** 현재 Cycle 선택을 'Turn 전체를 이겼다'로 해석하지 않는다. 실제 모드/PSD가 무엇인지부터 확인한다.

**비용·검증:** N2/N3에서 속도·곡선·발 위상 coverage, 재검색과 clip 교체를 먼저 비교한다. 가중치·continuing·검색 구간도 데이터와 함께 평가한다. 전역 다중검색은 CPU와 선택 분포를 바꾼다. 큰 반전의 후보 부족이 확인된 경우 해당 구간의 Cycle+Turn 겹침을 시험하고 양쪽 schema/normalization/continuing/entry ranges를 검증한다. 비교 baseline을 유지한다.

### 3.3 단발 연결과 취소

**효과:** Start/Land/Pivot의 시간 재시작을 막고 새로운 유효 명령에는 반응한다(I/H).

**근거:** native runtime/cache/epoch/revision, H01 연속Pivot/모드전환 수정, H02 PoseHistory 복귀(G/A/S/H).

**원리:** 최신 context 갱신과 새 playback command 발행을 분리한다. 동일 사건을 여러 producer 변화로 재발행하지 않는다(I).

**차이:** 기본 GASP의 MM 기반 전환과 Project_J direct one-shot은 동일 시스템이 아니다. 매frame chooser로 바꾸면 이미 해결된 문제가 되돌아올 수 있다.

**비용·검증:** 필요한 구간의 general steering은 기존 command lock을 유지하며 시험한다. 작은 입력 변화는 유지/보정, 큰 redirect는 cancel/reselect 중 하나의 owner가 결정해야 한다. 취소각만 확대하면 반응성이 악화할 수 있다.

### 3.4 큰 Turn의 수명과 보정

**효과:** 충분히 선택된 Turn 구간을 유지하고 170/190도에서도 목표에 연결한다(I).

**근거:** speed>=180 반복 eligibility, .75 cap/.1 cooldown/rearm, 과거8episodes 조기종료; MM 내부 보정 없음(S/A/H/G).

**원리:** entry 사실과 active lifetime을 구분한다. 검색 가능 후보가 있어도 창이 즉시 닫히면 correction/bias 변경의 효과를 관측하기 어렵다(I).

**차이:** GASP의 visual-root/steering 조합을 current CMC/TIP에 넣을 때 이중 회전이 생길 수 있다. 생존시간 문제와 보정 문제를 동시에 바꾸지 않는다.

**비용·검증:** first normal reversal decel만으로 즉시 종료하지 않는 policy를 설계한다. input release/action/air/target change/timeout은 종료를 유지한다. 이후 current asset/time 기반 보정을 제한 실험한다.

### 3.5 발 접지와 최종 화면

**효과:** 검색과 회전 뒤의 접지 오차를 정리한다(I).

**근거:** 양쪽 FootPlacement→LegIK→History, threshold/radius/stop-policy 차이(G/A/S).

**원리:** footprint·root·pelvis·IK가 마지막에 일관되어야 한다. 발 보정된 history가 다음 query에도 영향을 줄 수 있다(I).

**차이:** Project_J에 FootIK가 빠졌다는 판단은 틀리다. 미연결 ControlRig를 실행 비용에 더하지 않는다. 운영 leader PP None과 visual follower 최종 IK는 별도 조사한다.

**비용·검증:** foot trace·plant alpha·root offset·capsule speed·play rate·retarget를 같이 기록한다. drift를 더 많은 IK로 가리는 대신 예측/데이터/루트 원인을 먼저 분리한다.

## 4. 변경 분류와 우선순위

먼저 N1-N6 전체 baseline에서 영향 범위·재현 빈도·근거 확실성·비용을 기록한다. 아래 P0-P3는 조사에서 발견한 구체 이슈의 확인/실험 순서이며 전체 목표를 Turn으로 제한하지 않는다. 실제 검증 결과에 따라 일반 가감속·Start/Stop·착지·발 문제가 우선할 수 있다. 한 경로의 미확인이 독립된 경로의 분석을 막지 않게 한다.

| 순위 | 분류 | 작업 | 기대효과 | 비용/통과 게이트 |
| --- | --- | --- | --- | --- |
| 우선 확인 | 전체 평가 | N1-N6 및 관련 S시나리오 baseline | 전체 품질과 보존/개선 대상 식별 | 실제 출력·영상과 구조 근거 구분 |
| 보존 | 기존 | DynamicCycle Arc/Box/Diamond/Hourglass | 작은 회전 품질 유지 | 실제 선택 분포 regression |
| 보존 | 기존 | 키보드 StrafePivot+authored foot times | 검증된 연속반전 | H01 회귀 재검증 |
| 보존 | 기존 | one-shot episode/epoch/revision/cache | 불필요한 restart 억제 | 동일command pulse0 기준 |
| 보존 | 기존 | latest snapshot+actual-search consumption+budget | 반응성/비용 | pending/force/restore 검증 |
| 문제 확인 후 우선 | 설정/데이터 | 속도·곡선·발 위상·전환 coverage와 PSS/continuing/재검색 비교 | 가감속·작은 이동·클립 연결 개선 가능 | N1-N3, 기존 Cycle 분포와 검색 비용 |
| 문제 확인 후 우선 | 설정/그래프 | root/foot/retarget/playrate owner와 활성 보정 정리 | 일반 자세·접지 오차 개선 가능 | N2/N4/N6, drift·이중보정·CPU 비교 |
| P0 확인 | 출력 | long-fall InAirLoop final output owner | 선택/출력 불일치 판단 | final branch+node weight+actual pose |
| P1 | 코드 | MovingTurn entry vs active eligibility 분리 | 정상감속 조기종료 방지 | S1-S7, action/mode/remote 회귀 |
| P1 | 그래프 | OW curve Alpha/time pin 의도 정리 | authored warp 구간 제어 | TargetTime/Alpha/current asset 추적 |
| P2 | 그래프 | 큰 MMTurn 제한 Steering | 170/190 목표 보정 가능 | visual/capsule 이중회전 없음 |
| P2 | 데이터 | Turn 진입/후반/continuing/foot 범위 | 좋은 진입과 연결 | frame cost+time 확인 |
| P2 | 설정/그래프 | Start/Land 유지 보정과 cancel 역할 | 작은 입력 연결 | latency·redirect·restart 검증 |
| P3 | 후보 구조 | 큰 반전 창만 Cycle+Turn 후보 | selection edge 완화 가능 | normalized costs/CPU/빈도 비교 |
| P3 | 설정/코드 | trajectory density/collision local-near | 급회전/벽 예측 개선 가능 | 원격 예산/trace 비용 |
| 낮음 | 전면교체 | GASP 전체 root/state/search 이식 | 불확실 | 기존 전투·remote·crowd 계약 재설계 |

## 5. 단계별 도입 게이트

### Gate A: baseline 확인

운영 Pawn/Master/profile/compiled state와 조사 이후 변경점을 확인한다. 실제 최종 source/visual mesh를 식별한다. N1-N6의 일반 이동·가감속·동작 연결·자세·발 접지를 baseline으로 남기고 S1-S15의 입력 변화/예외를 추가한다. longFall 최종 분기와 movingTurn activation/exit는 알려진 이슈로 함께 기록한다. 잘되는 작은 Cycle·Pivot·Start/Land를 보존 기준으로 삼는다. 아직 구현 권한 없으면 read-only까지만 수행한다.

### Gate B: 수명/출력 계약

실제로 재현된 Start/Stop/Land/Pivot/TIP/Jump/공중loop의 연결과 수명 문제를 해당 owner에서 다룬다. 기존 episode/epoch/revision의 해결된 문제는 되돌리지 않는다. 알려진 두 이슈도 별도 변경으로 다룬다. InAirLoop는 외부 looping owner를 쓸지 별도 air MM 후보를 쓸지 runtime의 loop/override 계약으로 결정한다. 단순 !loop 조건 삭제는 Idle/LocomotionLoop도 외부가 차지할 수 있으므로 피한다. MovingTurn은 entry qualification과 유지 guard를 분리하고 정상감속/실제stop을 구분한다. 새 manager/tick/복제 필드를 기본 해법으로 추가하지 않는다.

### Gate C: 보정

대상 경로의 출력·수명이 정상인지 확인하고 독립된 경로는 별도로 진행한다. 회전 owner 표를 만든다: capsule=CMC/TIP, visual root=선택한 보정, aim=upperbody/AO, feet=Foot/Leg. 일반 Cycle·가감속·Start/Land·Turn 중 부족한 상황에 한해 currentBlendPlayer asset/time·root motion provider·space·Alpha·LOD gate를 검토한다. OW pin 수정을 별도 비교하고 largeTurn Steering도 제한 실험으로 다룬다. 접지 문제는 속도·보폭·데이터·retarget와 현재 Foot/Leg를 함께 분석한다. root Accumulate와 새 보정 노드는 필수 도입 조건이 아니라 검증할 대안이다.

### Gate D: 후보/데이터

선택 문제는 일반 이동의 방향·속도·곡선·발 위상·전환 데이터 coverage부터 비교한다. 각 PSD 동일/호환 schema, normalization set, sample weight, base/loop/continuing bias, 재검색 간격, branch/exclude/block 구간을 확인한다. 데이터·설정 조정으로 부족하고 후보 제한이 원인으로 확인된 경우 필요한 상황에만 후보를 겹친다. 각도별강제 대신 latest trajectory와 foot pose가 선택하게 한다. active와 rollback 데이터 연결을 구분해 저장 승인을 받는다.

### Gate E: MMO 확장

local-near 성공 후 원격/yaw 복제/net smoothing/ABA/URO/dormancy/visibility/relevance를 별도로 확인한다. near/mid/far/hide tier에 new correction 수요를 연결한다. production visual retarget/follower 비용을 포함해 scale 측정한다. 'far MM disable=false'를 전체 far 매frame update로 오해하지 않는다.

## 6. 추가 고려해야 할 사항

아래는 구현 사실이 아니라 후속 설계/검증 항목(I/U)이다.

- **Frame ownership:** producer/worker snapshot sequence와 selection revision, physics/smoothing pose의 기준시점. 오래된 actor yaw와 최신 input을 섞으면 허위 반전이 될 수 있다.
- **Search relevance:** inactive graph에 남은 result와 actual weighted output을 구분. forced request가 렌더링 이전에 소비되거나 hidden 캐릭터를 깨우지 않게 한다.
- **Angular continuity:** 179→-179는 숫자 경계인지 새 반전인지, camera 동일방향 누적/반대입력과 경로 반전을 구분. Quaternion interpolation과 FindDelta의 정확한 endpoint 동률도 확인한다.
- **Speed contract:** authored root speed, CMC maxspeed, MM rate, external rate1, retarget limb length 차이를 분리. 슬라이딩을 state/IK 문제로 단정하지 않는다.
- **Clip semantics:** 전진180Turn은 뒤로 달리던 캐릭터·strafe path 유지 body spin과 계약이 다르다. S/A/D 방향 입력과 mouse yaw를 같은 angle 하나로 축약하지 않는다.
- **Grounding:** slopes/stairs/uneven ground/moving platforms에서 foot plane/trace, gravity/space, pelvis 범위 검증. HeightMax250 같은 큰 값의 실제 영향도 측정한다.
- **Actions:** attack/dodge/hit/fullbody/montage/rootmotion/mount는 locomotion correction의 owner를 넘겨야 한다. 상체slot이면 body yaw와 attack aim/weapon trajectory가 분리되는지 확인한다.
- **Network:** server authority, simulated proxy의 replicated yaw/velocity, smoothing transform, late/out-of-order one-shot event, relevance 복귀, teleport/reset. local camera intent를 remote에 추정하지 않는다.
- **Determinism:** local/remote가 같은 PSD/pose를 고르는 것이 반드시 네트워크 정합 조건은 아니다. 무엇을 복제하고 무엇을 시각 추정하는지 명시한다.
- **Budget:** MM search, current pose evaluation, internal 4player correction, foottrace, retarget/follower, perframe allocation/copy를 나누어 측정한다. 다중candidate 확대의 메모리/index 비용도 고려한다.
- **Engine upgrade:** 5.7→5.8의 node pin/enum/property/default와 search timer 의미가 다를 수 있다. fixed numeric enum '0=Off' 같은 옛 설명을 현재 엔진에 적용하지 않는다.
- **Data authoring:** branch notify 자동DB 연결, mirrored data, looping marker, root extraction, exclude ranges, contacts/early transition. asset 단독 entryMM 전에 nullable BranchIn을 검증한다.
- **Debug isolation:** diagnostic CVar는 gameplay predicate/throttle/data를 바꾸지 않게 하고 기본off·소량target·기간/측정조건을 기록한다. 임시 config와 debug삭제는 별도 승인 범위다.
- **Migration:** 하나의 문제/owner씩 변경, 기존 좋은 시나리오·데이터 지문 보존, 에셋 저장 전 변경 preview, 실패 시 code/data rollback 경로 준비.

## 7. 완료 기준

구현·테스트 통과나 큰 반전 성공만으로 '전체 자연스러움 완료'라고 쓰지 않는다. N1-N6 일반 이동과 관련 S시나리오의 output owner, 실제 검색 수명, 최종visual 영상, 접촉 품질, 입력 latency, remote/crowd 비용, 기존 기능 회귀를 각각 증거로 남긴다. 합격 조건은 05의 사전기준으로 결정하며 관측 후 목표를 임의로 낮추지 않는다. 전체 목표 대비 완료한 부분과 남은 부분을 별도로 보고한다.
