# 캐릭터 리깅과 UE5 후처리 제작·문제 해결 가이드

이 문서는 Blender 리깅 → UE 실제 익스포트와 비교 → FBX 왕복 → UE 임포트 → 몸체 후처리 → 플레이 검수의 재사용 절차다. GreatSword 작업에서 확인한 원인·실패·해결을 일반 제작 기준으로 정리한다. 특정 캐릭터의 수치나 해결책을 모든 캐릭터에 복사하는 매뉴얼은 아니다.

공통 골격/애니메이션 계약은 [휴머노이드 표준](HumanoidSkeletonRetargetingStandards.md), 실제 연결·소유권과 최신 검증은 [플레이어 마스터](../../Architecture/Runtime/Player_Character_Master.md), GreatSword의 수정 blend/FBX/비교 장면은 [제작 원본](../../../SourceArt/Characters/GreatSword/UE5/README.md)을 따른다. 이 문서가 그 자원이나 런타임 설정의 두 번째 권위가 되지 않도록 한다.

## 1. 먼저 고정할 제작 계약

| 결정 | 직접 재생 | 실시간 리타깃 |
| --- | --- | --- |
| 대상 | 공유 본의 이름·계층·축·기준 포즈/재생 계약을 검증한 UE5 호환 SKM | 직접 호환 계약을 맞추기 어려운 몸체 |
| 공통 이동/전투 포즈 | 실제 몸체의 공통 master ABP | canonical source의 같은 master ABP |
| 몸체의 입력 | 공통 master 출력. 추가 처리가 필요할 때만 InputPose Post Process | 명시적 Family/Body의 RetargetPoseFromMesh |
| 손 접촉·추가 본 | 몸체 최종 단계가 한 번 소유 | follower 최종 단계가 한 번 소유 |
| 일반 의상 | 최종 몸체의 LeaderPose | 최종 follower의 LeaderPose |

리깅이 검증된 새 UE5 SKM은 같은 직접 재생 입구를 사용한다. master ABP/캐릭터 부모에 메시 이름별 조건을 추가하지 않는다. 자식 캐릭터는 source·표시 파츠 구성, 프로필은 체형/접촉 값, 몸체 Post Process는 추가 뼈와 몸체 처리, 무기 정의는 무기 Grip과 궤적을 소유한다. 골격 이름이나 본 개수만으로 런타임에서 두 경로를 자동 전환하지 않는다.

`UProject_JRetargetAnimInstance`는 역사적 이름을 가진 기존 접촉·품질 컨트롤러다. 이 클래스를 부모로 사용한다는 사실은 리타깃 실행의 증거가 아니다. 그래프 노드와 실제 processor 생성 여부를 검사한다. 직접 몸체에는 리타깃 노드/processor가 없어야 한다.

### 앞으로 임포트하는 에셋 종류별 계약

여기서 미래 에셋은 발·신발에 한정하지 않는다. 캐릭터·신체 파츠·장비·의상·무기·장식 등 앞으로 사용할 자산 전반이다. 모든 에셋을 Manny에 맞추거나 같은 ABP로 평가하는 것이 목표가 아니다.

| 에셋 | 비교/연결 기준 | 고려할 분리·교체 조건 |
| --- | --- | --- |
| 전체 인간형 캐릭터 | 프로젝트의 실제 UE5 코어와 몸체 계약 | 외형·비율·추가 본 차이, 직접/리타깃 선택, gameplay source는 하나 |
| 머리·손·맨발 등 분리 신체 | 최종 몸체의 공유 본·bind·가중치 | 표시/숨김 경계, seam·겹침, face/morph 보존, 신발이 없는 상태 |
| 스킨 갑옷·장갑·신발 | 현재 몸체가 생산하는 pose와 본 대응 | 장비 자체 bind/shape, 교체한 메시의 실제 지지/파지 형상, 탈착 시 남는 보정 |
| 소켓 부착 무기·방패·장식 | 부모 본/소켓과 장비 local anchor | static/rigid 자산에는 불필요한 Skeleton 호환·리타깃을 추가하지 않음, L/R grip·수납·독립 동작 |
| 자체 본이 있는 장비·의상 | 공유 코어와 독립 가지의 소유권 | LeaderPose가 모든 독립 본을 해결하는지 확인, 필요할 때만 CopyPose/자체 PP |
| 머리카락·치마·망토 | body anchor와 물리/표면 solver | RigidBody/Cloth 중복, 파츠 교체·숨김·서버/거리/LOD, 충돌 자원 |
| 별도 생물·탈것·기계 리그 | 해당 종/기능의 실제 기준 리그 | Manny 호환을 억지로 부여하거나 플레이어 부모/입력을 복제하지 않음 |

캐릭터와 장비의 골격이 서로 다를 수 있다. 이름이 같더라도 다른 bind/부모를 가진 파츠는 단순 LeaderPose로 승인하지 않는다. 장착된 자산이 제공하는 선택적 보정 데이터와 실제 pose 소비자를 연결하고, 교체·숨김·제거하면 이전 자산의 보정도 사용하지 않게 한다. 여러 파츠가 같은 다리를 표시하면 지지 형상은 함께 검토하되 사지 solve writer를 중복 실행하지 않는다.

몸체 크기가 바뀌면 시각 스킨 외에 캡슐·카메라·타격/피격 기준과 부착 anchor도 기존 제작 데이터에서 검토한다. 발·손의 cosmetic 보정으로 액터 위치나 서버 이동 상태를 바꾸지 않는다. 화면의 보정된 소켓을 독립 무기 궤적의 새 원천으로 다시 읽어 순환시키지 않는다. 장비의 비동기 로드·실패·취소·교체는 기존 revision/lease를 유지하며, 새 외형을 위해 입력·GAS·예산 소유권을 복제하지 않는다.

### 후처리 추가 전 원인과 통과 조건

문제가 **처음 나타나는 단계**를 찾고 그 단계의 원인을 수정한다. 아래 검사는 작업 사본/미저장 검증 월드에서 같은 clip·시각·단위·공간으로 비교한다. Post Process만 꺼도 공통 ABP의 foot/stride/warping 등은 남을 수 있으므로 이를 raw animation 검사라고 부르지 않는다. 기존 writer를 끄는 시험은 원인 분리용이며 검증 뒤 원래 구성으로 복원한다. 같은 프레임 번호 안에도 여러 update/evaluation과 늦은 무기 writer가 있을 수 있으므로 실제 입력 pose·평가 완료와 실행 순서를 함께 기록한다.

| 처음 달라지는 단계 | 수정할 책임 | 다음 단계로 넘길 조건 |
| --- | --- | --- |
| DCC rest/기본 skin | 모델·관절 위치·bind·weight·축/scale | 단일 의도한 변형 경로와 올바른 기준 형상 |
| DCC 굽힘·비틀기·표정 | weight 분배·twist·교정 형상/driver·morph | 관절 변형·표정·장비 경계의 의도한 형태, 좌우/극단 포즈 검수 |
| FBX 왕복/UE reference | export/import recipe·좌표/단위·실제 mesh bind | 원본 대비 의도한 결과, 의도한 체형 차이와 임포트 결함 구분 |
| 공통 ABP 이전의 압축 clip | 원본/변환 clip·retarget policy·additive base·root/curve metadata | 기대한 체형·관절 길이·포즈와 재생/이동 계약 |
| 공통 ABP의 합성·절차 처리 | Slot/layer·선택/phase·IK/warping·평가 순서 | 이중 writer·잘못된 기본값/공간 없이 의도한 포즈 |
| 몸체 접촉/후처리 | 실제 밑창·grip·환경 적응·명시적 변형 feature | 보정 전후 이득과 부작용, 도달 한계·root/사지 길이 보존·비용을 실측 |
| 최종 물리·렌더·장비/LOD | collider/constraint·solver 소유권·표시/fit·LOD 데이터 | 같은 완료 포즈의 스킨·실제 표시 파츠·숨김/교체/거리 전환 검수 |

가중치 합 1·본 수 보존·helper 위치 일치는 구조 검사의 일부다. 잘못된 관절 위치, weight 분배, skin collapse, 표정/손가락/옷 seam, normal/tangent 문제까지 정상이라는 증거는 아니다. 원본에서도 찌그러지는 부위를 pelvis offset·stretch·solver 반복·충돌 두께·damping 증가로 가리는 것을 제작 완료로 삼지 않는다.

반대로 의도적으로 제작한 pose-driven corrective bone/morph를 UE Post Process에서 평가하는 것은 정상적인 변형 feature일 수 있다. 교정 형상/driver·적용 포즈·LOD·소유권을 제작하고 검수했는지로 판단한다. 후처리 사용 여부나 보정량 하나만으로 좋은/나쁜 리깅을 판정하지 않는다.

환경/장비 적응 보정은 선택적이다. 적용 이유·대상·입력 공간·다른 writer와의 순서·품질/LOD 조건·상한과 실패 시 동작을 기존 제작 데이터에 명시한다. 보정이 계속 상한에 걸리거나 원래 정상인 포즈를 망가뜨리면 원인 단계로 돌아간다. 리깅 결함 때문에 실패한 직접 재생을 실시간 리타깃으로 숨겨 승인하지 않는다. 리타깃 경로도 대상의 skin/bind/변형 검수를 통과해야 한다.

## 2. 원본을 보존하고 비교 기준을 준비한다

1. 외부 패키지·원본 blend는 그대로 둔다. 작업 사본, 실험 브랜치, 수정 전 대상 에셋 백업을 준비한다. 이미 사용자가 수정한 파일을 초기 상태라고 가정하지 않는다.
2. 프로젝트가 실제 사용하는 UE 버전·SKM·Skeleton·master·몽타주·무기·의상·PA·Cloth를 좁게 확인한다. 인터넷의 임의 Manny FBX보다 **현재 UE 프로젝트에서 내보낸 실제 기준**을 우선한다.
3. Unreal의 `SkeletalMeshExporterFBX`로 기준 Manny/Quinn과 작업 대상 SKM을 각각 익스포트한다. Blender의 같은 장면에 단위·원점·좌표 변환을 일치시켜 가져온다.
4. 원래 크기 비교와 시각적 overlay를 구분한다. 몸체를 강제로 늘여 차이를 지우거나 Object scale을 적용한 뒤 원래 비율로 기록하지 않는다. root·pelvis·사지·손발 확대 보기를 준비한다.
5. 비교 결과에 UE 버전, 실제 asset 경로, exporter/importer와 옵션, 단위, 비교한 reference pose의 출처를 기록한다. Armature Object transform과 bone transform을 함께 본다.

GreatSword는 Manny Simple/Quinn Simple/Complete를 실제 익스포트해 비교했다. Blender에서 UE root가 Armature Object로 표현되어 본 목록이 88/317개였지만 UE의 root 포함 89/318본을 잃은 것이 아니었다. 이 수치는 이 작업의 공유 코어/대상 수이며 모든 UE5 캐릭터의 필수 총 본 개수가 아니다.

## 3. 마네킹과 무엇을 비교하는가

| 비교 | 정상적인 모델 차이 | 수정/계약 검토가 필요한 차이 |
| --- | --- | --- |
| 공통 계층 | 말단 추가 본, 별도 헤어·치마·표정 가지 | 공유 본 사이에 새 부모 삽입, 잘못된 root/side/사지 부모 |
| 길이·체형 | 키, 어깨 폭, 팔/다리 길이, 하이힐 발목 높이 | 단위/scale 중복, 한쪽 사지만 비대칭 변환, 부모 scale 오염 |
| 기준 회전/축 | 검증하고 보정 가능한 bind/pose 차이 | bone 이름만 바꾸고 축은 이전 리그 그대로, 좌우 회전 반전 |
| IK helper | root 기준점·authored 목표·solved-limb 파생 목표의 역할을 명시 | 움직여야 하는 목표의 bake/추종 누락, authored 궤적을 무조건 FK로 덮음 |
| 스킨 | 의도한 형태·정규화된 가중치 | control/IK helper의 불필요한 가중치, 빠진 influence, 잘못된 side |
| 추가 본 | 표준 본 아래에 보존한 원래 가지 | 표준/legacy 가지의 중복 writer, 잘못된 local bind 변환 |
| UE mesh/Skeleton | 의도한 체형별 mesh bind와 공유 Skeleton 기준의 차이를 기록 | 원본/bake/import의 예상 bind와 불일치, mesh 기준값을 Skeleton 화면만 보고 오판 |

기준 포즈에서 root-relative 관절 위치와 component-space 기준 회전을 수치로 비교한다. 계층은 이름으로 대응하고 위치/회전은 같은 공간과 단위로 환산한다. 본 순서나 화면에서 보이는 roll 값만으로 판단하지 않는다. FBX round-trip의 허용 오차와 실제 체형 차이를 별도로 기록한다.

GreatSword의 실제 차이는 pelvis 약 110.17cm 대 Manny 95.90cm, 발목 15.95cm 대 8.24cm, 발목→발볼 기울기 61.95도 대 26.50도였다. 어깨가 좁고 팔도 짧았다. **이 차이 자체를 리깅 결함이나 실시간 리타깃 필수 조건으로 판정하지 않았다.** 반면 이 패키지에서 확인한 움직여야 하는 helper의 추종 누락과 의도하지 않은 reference/bind 불일치는 수정 대상이었다.

Full Manny의 전체 본 목록과 Simple Manny의 재생 코어도 구분한다. 초기 `BlenderRigAudit.json`의 `missing_core`에는 `lowerarm_correctiveRoot_*`, `clavicle_*`, `ankle_*` 등 Full Manny의 교정/보조 가지도 포함돼 있었다. 그 필드 이름만으로 필수 코어가 빠졌다고 판정하면 안 된다. 실제 공유 코어 89본의 계층/왕복 비교와, 사용할 그래프·스킨이 요구하는 추가 본을 따로 검사한다. 교정 본이 필요하면 해당 weight/pose-driver/corrective 제작까지 검토한다. 이름만 있는 무가중 본을 추가한다고 허리·관절 볼륨이 자동 복원되지는 않는다.

## 4. Blender 수정과 보존 검증

### 변형 본과 제작용 제어를 구분한다

작업용 control rig·위젯·제약과 UE에 내보낼 골격을 분리한다. 표준 IK helper를 제거하거나 `Only Deform Bones`로 누락시키지 않는다. 필요한 helper는 스킨 가중치 없이 내보낸다. 새 leaf bone 생성으로 계층을 바꾸지 않는다.

GreatSword의 손발 목표 4개는 실제 해결된 `RES_foot_l/r`, `RES_hand_l/r`를 WORLD Copy Transforms로 따라가게 고쳤다. 기존 부모·기준 회전을 유지한다. **`ik_hand_gun`은 독립 무기 기준일 수도 있으므로 손발 helper와 같은 규칙으로 강제하지 않는다.** 이 리그의 기본값은 기존 오른손 추종이며 `Snap independent`로 현재 결과를 보존한 채 무기 컨트롤을 독립시킬 수 있다. `Follow wrist`는 의도적으로 오른손 기준으로 복귀한다. 모드 전환은 애니메이션에 명시적으로 키를 넣으며, 런타임 몸체의 기준 본 정책과 맞춘다. 이름만 `RES_*`라고 같은 역할이라고 가정하지 않는다.

표준 `ik_*` 7개는 가중치가 없음을 확인한 뒤 Deform을 껐다. FBX는 `use_armature_deform_only=False`로 이 본들도 유지한다. 비변형 플래그와 수출 제외는 다른 결정이다.

### 새 리그의 Root·FK/IK·변형 계약

| 역할 | 제작 기준 | 승인 전에 확인할 것 |
| --- | --- | --- |
| export root | 공통 계층의 단일 root, 의도한 단위·축·균일 양의 scale | 제자리/이동 클립의 root 궤적, 원점·첫 프레임 lock, loop와 UE 왕복 |
| pelvis/몸통 | root의 이동과 별도 body pose, 체형에 맞는 관절 위치 | 허리 압축·회전 분배·volume, 골반을 올려 발 결함을 감추지 않음 |
| FK/IK 제어 | 제작 컨트롤과 export 결과를 분리, 방향 일관된 pole·길이 유지 | 네 사지 전환 위치/회전, 직선 무릎·팔꿈치, 굽힘 방향·극점 특이 자세 |
| 공간/부모 | root·몸통·무기 기준의 명시적 공간 | 부모 이동·회전 시 결과, 공간 전환 snap, 이중 부모/constraint cycle |
| hand/foot 목표 | 클립/몸체 계약에 따라 authored 목표 또는 solved-limb 목표 | 움직여야 하는 목표의 rest 고정 금지, root 기준 helper는 의도대로 유지, authored 목표 보존 |
| 무기 기준 | 독립 무기 궤적과 보정 전 wrist reference 중 명시적 선택 | 주손 자식 feedback, 양손 grip 거리·도달, 소켓 local basis |
| twist/교정 | 실제 skin에 필요한 가지·가중치·driver | 원본 LBS 변형에서 팔 비틀기·깊은 굽힘, DQ 전용 결과와 구분 |
| 얼굴/머리/의상 | 코어의 peripheral branch, 각 최종 writer 하나 | 표정·morph·Cloth·RigidBody, body/장비별 평가 소유권 |
| bake/export | 평가된 export 골격을 샘플링, 필요한 비변형 marker 포함 | FPS·범위·root·네 사지·추가 본의 FBX 왕복, control/action 원본 보존 |

새로 제작하는 모델은 코어와 필요한 변형 가지로 설계한다. 이번에 보존한 Bip01 가지는 기존 skin/장비/얼굴을 이관하기 위한 유효한 연결이며, 모든 새 모델에 318본이나 legacy bridge를 복제하라는 표준이 아니다. 삭제·재부모화는 기존 애니메이션·PA·소켓·스킨 소비를 먼저 확인해야 한다. heel/toe pivot, stretch, 다중 공간, 자동 bake-back은 실제 제작 요구가 있을 때 추가하고 검증한다. 현재 커스텀 리그를 Rigify 또는 UE Control Rig의 완전한 기능 구현으로 부르지 않는다.

현재 GreatSword의 네 사지는 `0=FK, 1=IK`이며 `Match to IK/FK`가 결과를 맞춘다. 리그별 숫자 방향은 다를 수 있다. 저장 기본 포즈에서 전환/루트 검사를 통과한 것이 모든 극단 포즈·Action 전체 bake를 승인하지는 않는다. UE Control Rig의 Backwards Solve에 해당하는 역매핑은 별도 구현/왕복 검증 대상이다.

### 바인드 포즈를 고칠 때

Edit Bone rest 변경, Pose transform, Object transform, Armature modifier의 bind 관계는 다른 작업이다. rest만 돌리고 스킨 bind를 그대로 두면 기준 포즈에서 형상이 변할 수 있다. 필요한 변경만 하고, 변경 전후 메시의 evaluated positions·정점 수·가중치·추가 본을 비교한다. 바인드/weight 자체가 결함이면 수정 후 그 값이 달라지는 것은 정상이다. 의도한 변경 범위와 나머지 보존 범위를 분리해 검수하며, hash 불변을 모든 재리깅의 통과 조건으로 요구하지 않는다. 축을 맞춘다는 이유로 발/허리/얼굴 형태를 몰래 바꾸지 않는다.

GreatSword는 38개 메시의 정점·웨이트 SHA-256이 수정 전후 같았고 기준 포즈 evaluated geometry 변화가 0이었다. FK/IK 회전 4조건과 IK 목표 이동 4조건에서 helper/solved limb 오차가 0이었다. 이것은 Blender 리깅 검증이며 게임에서 무기를 잡거나 바닥에 닿는 품질까지 승인한 결과는 아니다.

### 추가 가지와 표정

표준 core 아래의 legacy·face·hair·skirt·cape 가지와 가중치를 보존한다. 공유 본 사이에 추가 부모를 끼워 넣지 않는다. 얼굴 본 보존과 표정/립싱크 제작은 별개다. shape key가 있다면 개수·이름·값·export/import 결과도 별도로 확인한다. 이번 GreatSword에서는 얼굴 가중 본 44개를 보존했으며 새 표정 애니메이션을 만들지는 않았다.

표준 본 아래의 legacy 변형 가지를 유지한 경우, 공유 애니메이션이 표준 본을 움직이고 legacy local bind가 피부를 따라가게 할 수 있다. 이때 이전 clip/별도 CopyPose가 legacy 가지까지 다시 움직이는지 확인한다. 서로 다른 이름의 본이 같은 부위를 변형한다고 무조건 중복도 아니고, 이름이 다르다고 두 번의 변형이 안전한 것도 아니다.

## 5. FBX 왕복과 UE 임포트

1. 컨트롤/위젯을 제외하고 export 골격과 해당 메시만 선택한다. root 처리, forward/up, cm 단위, scale 적용, leaf bone, smoothing/tangent, 필요한 모든 본을 명시한다. 프로젝트의 검증된 export recipe를 재사용한다.
2. 새 FBX를 Blender로 다시 가져와 UE에서 실제 익스포트한 기준과 비교한다. 누락 본, axis flip, 중복 root, 100배 scale, geometry/weight 변화를 확인한다.
3. UE에서는 수정 전 SKM/Skeleton/재질/PA/Post Process/Nanite/Cloth 연결을 기록한다. 기존 asset identity를 유지해 재임포트하고, 필요 없는 재질·텍스처·애니메이션·PhysicsAsset 재생성을 끈다.
4. 임포터 종류를 기록한다. 이번 작업은 UE5.8 legacy FBX 재임포트를 사용했다. 임포터 전환을 같은 옵션/결과라고 가정하지 않는다. 재현 가능한 importer 선택으로 관리하고 엔진 전체 설정을 무작정 변경하지 않는다.
5. `.004`/`_004` 등의 재질 suffix는 슬롯 이름/파츠 override를 실제 기존 재질과 대응해 처리한다. 이름 정규화만으로 모든 슬롯을 같은 재질로 덮지 않는다.
6. 임포트 뒤 **원본 export의 예상 bind·SKM 실제 bind·USkeleton reference pose를 구분해 비교**한다. 같은 Skeleton을 공유하는 체형별 mesh의 bind 길이가 모두 같아야 하는 것은 아니다. 차이가 의도한 체형인지, 잘못된 변환/임포트인지 먼저 판별한다. 공유 Skeleton ref를 한 몸체에 맞춰 덮지 않으며, 갱신이 필요하면 연결된 다른 SKM·애니메이션·translation 정책·PA·소켓의 영향을 먼저 확인한다.

실패 사례: 초기 Skeleton 값만 보고 -90도 mesh basis를 추정했으나 실제 SKM bind와 달랐다. 이 추정은 폐기했다. UE 익스포트→Blender 비교와 mesh/Skeleton 직접 비교 후 ref를 일치시켰으며 최종 bind 위치 차이는 약 0.000016cm 미만이었다. 같은 추정을 다른 캐릭터의 shape/constraint 전체에 적용하지 않는다.

## 6. UE 후처리의 책임과 순서

추가 처리가 없는 직접 호환 몸체는 공통 master 출력만 사용한다. 다음은 GreatSword의 필요한 단계들을 배치한 순서이며 모든 몸체의 필수 노드 목록은 아니다: 공통 master → 몸체 InputPose → 선택적 밑창 보정 → 접촉 목표 전달/일반 몸체 IK/팔 정밀 보정 → 2차 물리 → 최종 표시 파츠. 발 보정이 손의 기준 골격을 바꾸면 손 목표 계산보다 먼저 둔다. 의상에는 동일 보정을 다시 실행하지 않는다.

- 공통 master의 이동 선택·Slot·전투/탑승 레이어·기존 시각 예산 계약을 유지한다.
- 공통 발 Rig는 `bSetRefPoseFromSkeleton`으로 실제 owner mesh의 초기 transform/사지 길이를 사용한다. 기존 Rig의 editor default와 런타임 override가 둘 다 맞는지도 확인한다.
- 입력 FK에서 목표를 재구성하기로 정한 몸체만 사지 marker 4개를 필요할 때 `InstallBodyPoseMarkers(Blueprint, false)`로 해당 Post Process가 소유한다. 이 API의 기본값은 authored `ik_hand_gun`을 보존하지만 사지 marker 4개는 재작성한다. authored 손발 목표를 유지할 몸체에는 이 도구를 일괄 호출하지 않는다. 현재 GreatSword처럼 보정 전 오른손을 독립 기준으로 사용하는 몸체만 `true`를 명시해 다섯 번째 복사를 만든다. 제작 도구 소유의 4/5개 체인 전환만 허용하고, 충돌 writer·불명확한 연결은 거절한다. 기존 component-space 변환을 재사용한다. 보존 모드는 아티스트의 gun writer를 덮지 않지만, 임의의 downstream bone-control/부모 공간 변경까지 접촉 품질을 자동 승인하지 않는다. gun은 hand helper의 부모일 수 있으므로 각 작성 단계의 최종 goal과 동일 평가의 grip을 검수한다. 공통 master에 몸체용 최종 CopyBone writer를 계속 추가하지 않는다.
- 직접 몸체의 RetargetBodyProfile은 비워 두고, 비호환 follower는 명시적 Family/Body를 쓴다. follower가 접촉/물리를 소유하면 같은 mesh의 중복 Post Process를 끈다.
- 일반 모듈러 파츠는 LeaderPose를 공유한다. 독립 추가 본은 명시적 CopyPose/해당 Post Process, 표면 Cloth는 별도 cloth 소유권을 사용한다.

초기에 helper 복사를 공통 master 끝에 넣었던 경로는 직접 몸체 입력으로 옮겼다. 공유 기능과 몸체 설정의 책임을 분리한 결과다. 공통 그래프의 기존 발 CopyBone 네 개는 이번 조회에서 translation/rotation/scale 복사가 모두 꺼진 placeholder였다. 노드 개수만 보고 활성 writer 네 개라고 해석하거나, 기존 BlendStack/래그돌 경로를 조사하지 않고 삭제하지 않는다.

### 직접 호환도 실제 translation 정책을 확인한다

직접 재생은 별도 Manny producer와 `RetargetPoseFromMesh`/IK Retargeter processor를 쓰지 않는다는 뜻이다. UE 내장 compatible skeleton remapping과 translation retarget mode까지 없다는 뜻은 아니다. `PrepareCompatibleBody`는 공유 계층·Slot·BlendProfile 준비 도구이며 축·skin·극단 포즈 전체를 승인하는 자동 리깅 검사기가 아니다.

UE5.8에서는 대상의 `Use Retarget Modes From Compatible Skeleton`도 확인한다. GreatSword는 이 값이 True여서 보이는 대상 BoneTree의 Animation만으로 실제 사지 길이가 Manny로 덮인다고 판정할 수 없다. 로컬 엔진 `AnimationRuntime.cpp`의 effective mode와 같은 시각의 압축 클립 평가를 함께 확인했다. 팔/다리 길이는 실제 GreatSword 길이를 유지했다. Manny source의 팔 정책(clavicle/upperarm/lowerarm=Skeleton, hand=AnimationScaled), pelvis/척추의 설정을 검증 없이 대상에 일괄 복제·변경하지 않는다. 이전 UEFN→Manny 변환기의 flag=False와 현재 GreatSword 대상의 flag=True는 서로 다른 역할이다.

공통 발 Rig가 owner mesh 초기 transform을 사용하는 것은 범용화에 필요한 조건이다. 모든 체형·원본 모션·소켓·의상을 자동으로 완성하는 보장은 아니다. 호환 구조를 충족한 새 SKM은 같은 입구를 쓰되 필요한 접촉/추가 본 데이터와 실제 pose 검수를 통과해야 한다.

모션매칭도 Skeleton 재생 호환과 검색 품질 승인을 구분한다. UE5.8 Pose History는 실제 평가된 component-space pose를 저장하고 compatible skeleton의 bone index를 매핑한다. 이 매핑이 사지 길이를 canonical Manny 좌표로 정규화하는 것은 아니다. 채널이 continuing pose 값을 재사용하는 조건과 실제 character pose를 읽는 조건도 다르다. 새 체형은 실제 schema의 query mode·발 위치/속도·trajectory 가중치, 후보/전환·loop·foot phase를 검수한다. 이상이 없는데 캐릭터별 DB/ABP를 복제하거나 공통 비용을 무조건 바꾸지 않는다. 차이가 확인되면 기존 데이터/프로필 소유권에서 조정 근거를 마련한다.


## 7. 증상별 원인 분리와 해결 기록

| 증상 | 먼저 비교할 것 | 확인된 해결/다음 판단 |
| --- | --- | --- |
| 발이 뒤틀리거나 엉뚱한 방향 | 실제 bind axis, root/object 변환, 좌우 본, 실제 실행 Rig class | 이름 호환만 검사하지 않는다. owner mesh 초기 transform과 정확한 Rig 참조를 확인한다. |
| 하이힐의 뒤꿈치/앞코가 들림 | 밑창·굽의 지지점, ankle→ball 벡터, heel-lift 규칙, 애니메이션의 foot roll | 발목 높이를 곧 밑창 높이로 취급하지 않는다. 형상을 평평하게 펴거나 전체 mesh Z를 올려 숨기지 않는다. |
| 이동 중 신발이 땅을 뚫음 | 같은 최종 포즈의 스킨 정점과 발/발볼/IK 목표, 접지 평면, swing/plant, 보정 전후 단계 | bone origin 접지만으로 sole 지지가 보장되지 않는다. 관통 자세를 고정해 어느 단계에서 부족한지 수치로 분리한다. 최신 GreatSword 수정/검증은 아래 발 접지 절에 기록한다. |
| 무기가 손에서 튐 | Palm/WeaponGrip, 부착 부모/소켓, 현재 pose와 목표의 좌표 공간, motion/복귀 시점 | 독립 무기 목표를 이미 보정된 손에서 다시 읽지 않는다. 프레임당 목표/무기 변환을 한 번 전달한다. |
| 보조손이 닿지 않음 | shoulder→target 거리와 실제 팔 길이, grip 간격, 주손 목표를 따라가는 공간 | 잘못된 target space/중복 alpha부터 수정한다. 실제 도달 부족은 몸통·쇄골 참여/그립 제작을 검토한다. 팔 stretch나 클립별 예외로 먼저 감추지 않는다. |
| 허리가 구겨지거나 골반이 내려앉음 | rest 자체, 압축 clip, root/pelvis/척추의 local 차이, skin weights, twist/교정 본, PP/물리 출력 | 이 작업에서 특정 허리 결함의 해결을 검증한 것은 아니다. 아래 진단 절차로 위치를 좁혀야 한다. 키 차이만 보고 pelvis를 강제 이동하지 않는다. |
| 헤어/치마가 굳거나 폭발 | 실제 RigidBody 실행/품질 gate, PA bone 이름/shape/constraint frame, 충돌 제외 | 보존된 legacy local 축은 검증한 mapping과 bone-local frame으로 옮긴다. 추정 mesh basis를 일괄 곱하지 않는다. |
| 망토가 몸을 관통 | Cloth 실제 표면, 고정 마스크, 몸 충돌 PA, 충돌 제외/해상도/solver | solver 존재만으로 판정하지 않는다. RigidBody cape와 surface Cloth를 같은 파츠에 중복 연결하지 않는다. |
| 보이는 의상이 멈춤 | 숨은 producer tick, LeaderPose source/등록/가시성, source bone revision | source의 gameplay/visual 소비 계약을 유지한다. 매번 anim instance/physics를 reset하지 않는다. |
| 재임포트 뒤 재질/Cloth 소실 | 슬롯 대응, importer, section/clothing binding, component override | 같은 UObject를 갱신하고 기록한 제작 연결을 비교한다. 원본 의도된 빈 슬롯과 새 결함을 구분한다. |
| 에디터에서 맞고 재시작하면 틀림 | 실제 저장 여부, default/override, 디스크 재로드 | UE5.8 solver setter의 dirty 누락 사례가 있었다. 성공 반환/메모리 설정 문자열만 저장 증거로 삼지 않는다. |
| 종료/다음 PIE에서 GC ensure | 등록 해제 순서, LeaderPose prerequisite, Cloth physics state, 임시 World 참조 | `Super::OnUnregister()` 후 LeaderPose를 해제해 엔진이 제거한 physics state를 재생성하지 않게 했다. |

### 허리 변형을 조사하는 순서

1. Blender rest에서도 구겨지면 geometry·bind·weight 문제부터 본다. Weight 합, 좌우/척추/골반 영향, 불필요한 legacy/control influence와 scale을 확인한다.
2. UE reference pose부터 다르면 FBX 변환, 실제 Skeleton/mesh bind, import 기준 포즈를 검사한다.
3. reference는 맞고 raw/압축 clip부터 다르면 pelvis/root translation retargeting, 기준 포즈/축, 척추 수·local rotation 분배를 비교한다. 모든 spine translation mode를 일괄 변경하지 않는다.
4. 공통 ABP에서만 다르면 layered blend·Slot·additive base·root offset·동시 writer를 확인한다. 같은 입력을 body PP에 넣기 전후로 비교한다.
5. 접촉 보정 이후만 다르면 목표 도달 범위, pelvis/쇄골 stiffness, 발 anchor와 몸통 이동을 검사한다. 피부 볼륨 보존은 필요 시 weight/twist/corrective 작업으로 해결하며 solver로 모든 스킨 문제를 해결하려 하지 않는다.
6. 물리 이후만 다르면 잘못된 body mode/constraint frame·충돌을 확인한다. 원인이 확인된 층만 수정한다.

## 8. 손·무기에서 실제 겪은 문제

GreatSword의 짧은 팔은 기존 Basic 무기 목표 중 일부에 닿지 않았다. 하지만 첫 큰 오차는 길이만의 문제가 아니었다. 보정된 손으로 독립 궤적을 다시 읽는 feedback, 이전 frame의 공간, 중복 가중치, source notify 이후 늦은 Tick의 재평가를 먼저 구분해야 했다.

현재 직접 몸체는 같은 입력 포즈의 독립 기준 본과 상대 Grip에서 손목 목표를 만든다. Palm→Wrist는 GT에서 한 번 환산하고 worker에는 값 snapshot을 전달한다. 손 가중치는 `HandContactGoals`에서 한 번 적용하고 일반 전신 IK와 팔 정밀 보정은 같은 비변형 goal을 사용한다. 손 부착 모드의 보조손은 전달된 주손 목표를 따라간다. 자기 팔 아래의 독립 기준은 거절한다.

장착·복귀·진단은 실제 main/follower 또는 source PP의 컨트롤러를 함께 찾는다. 엔진 frame당 전달된 목표를 소비한 뒤 늦은 Tick이 무기 위치를 다시 쓰지 않는다. 해제/재초기화/대상 소실에는 snapshot과 등록을 비운다. 다른 캐릭터에서도 이 계약을 재사용하고 `R 스킬이면 보정` 같은 분기를 추가하지 않는다.

128회 설정으로 실제 저장된 리그를 재시작했던 7종/536표본 검사에서 full-alpha 최대 오차는 주손 약 0.00844cm, 보조손 0.62104cm였다. 신발 보완 뒤 최종 7종/560표본에서도 각각 약 0.00848/0.44005cm였다. 후속 A/B에서는 32회/541표본의 주손 최대 약 0.02022cm·보조손 최대 약 0.64731cm와 242개 montage 포즈/발 anchor 검사를 확인해 128→32회를 저장했다. 20회는 활성 montage의 보조손 최대 약 2.33cm로 채택하지 않았다. 함께 관측한 montage 없는 전환의 약 22.99cm는 아래의 늦은 무기 재시작 문제와 분리하며, 반복 수 부족의 증거로 사용하지 않는다. 서로 다른 일정의 최대값을 동일 프레임 A/B라고 부르지 않는다. 이 수치는 해당 몸체/Basic 무기/검사 일정의 결과다. 모든 체형·무기·클립의 파지를 승인하는 범용 오차 보장은 아니다. 초기에는 montage가 사라진 전환의 보조손 17.65cm 표본이 미확정 후보였다. 후속 재시작 검사에서 27.15cm, RMB 4회 반복에서 15.77/14.17cm의 두 표본으로 재현됐다. 아래처럼 최종 IK와 실제 무기 writer의 실행 순서까지 분리해야 했다. 알파·현재 montage·부착 root와 같은 프레임의 목표를 함께 기록한다.

### 정상 solve 뒤 무기가 다시 움직인 전환 사례

기존 GuidedIK·GripTrace를 같은 프레임으로 대조했다. `preFrame=31441`의 양손 solve/post-alpha contact 오차는 0이었고, 이후 같은 프레임에 `MotionBegin`이 들어와 이미 시작된 ContactRecovery를 취소하고 무기를 다시 움직였다. 해당 montage instance는 stopped/blend-out 상태였다. 따라서 리깅을 다시 하거나 IK 반복·alpha를 크게 바꾸어 해결할 문제가 아니었다.

`WeaponMotion`의 NotifyBegin에서 엔진 `FAnimNotifyMontageInstanceContext`의 실행 ID로 실제 인스턴스를 확인한다. 없는 실행이나 이미 비활성인 montage는 새 무기 motion lease를 얻지 못한다. 살아 있는 실행과 같은 asset의 새 실행은 허용하고, 기존 token 기반 Tick/End 소유권은 유지한다. montage context가 없는 sequence/editor 호출은 기존 동작을 유지한다. montage 이름·스킬 키 예외, 임의 timer, 두 번째 상태 관리자 없이 시작 승인을 바로잡는 수정이다.

이런 사례는 "최종 손 본이 IK 목표에 맞는다"와 "같은 프레임의 보이는 무기까지 맞는다"를 따로 검수해야 발견한다. 활성 montage만 필터링해 복귀 경계의 큰 오차를 성공 통계에서 지우지 않는다. 수정 후 68개 회귀와 RMB 네 번/매 프레임 655표본을 통과했고, montage 없는 보조손 full-alpha 38표본의 최대는 약 0.000000554cm였다. 이는 해당 반복의 확인이며 다른 montage/실행 조건의 검수도 유지한다. 마지막 7종 재검사에서는 Q 복귀에 보조손 1.63cm의 실제 goal 도달 잔차가 남았다. 추가 64회 시험도 일관된 개선이 없어 저장하지 않고 32회로 복원했다. 늦은 무기 writer 문제의 해결과 모든 자세의 완벽한 파지는 별개다. 도달 한계는 무기/체형·회복 자세·접촉 제작 계약에서 검토하며 사지 stretch나 반복 수 증가를 기본 처방으로 삼지 않는다. 전체 수치·조건은 런타임 문서의 후속 검증을 따른다.

## 9. 물리·Cloth·표시 파츠 보존

PA는 body 이름뿐 아니라 shape의 local 위치/축/크기, constraint 양쪽 frame·제한·질량·감쇠·충돌 제외를 비교한다. reposed legacy 축이 보존된 경우 bone-local 형상/관절을 유지하는 것이 맞을 수 있다. mesh-space 전체 회전을 적용할지는 실제 bind 비교로 결정한다.

Cloth에는 section binding, 물리 정점, 고정점/마스크, shared solver와 노출되지 않은 enum 설정도 있다. 이번 망토는 native 전체 설정 복제로 옮겼고 3개 clothing asset/2,534개 물리 정점을 확인했다. Python에서 읽을 수 있는 필드만 일치한다고 전체 복제 완료로 보지 않는다.

장비는 원래 visual revision/lease와 교체·해제 경로를 유지한다. 같은 LeaderPose 재연결로 physics를 반복 생성하지 않는다. 파츠 자체 뼈를 따로 계산할 필요가 없으면 별도 AnimInstance를 만들지 않는다. surface Cloth 파츠는 필요한 component tick을 유지하되 bone pose는 공유한다.

## 10. 발·신발 접지 검수

앞코가 높은 디자인, 굽, 발볼 폭·길이, 발목 위치, toe/ball joint와 스킨 형상은 서로 다른 값이다. 땅에 닿아야 할 지지점과 장식이 어디인지 메시에서 확인한다. 새 신발로 교체하면 지지 형상도 재검수한다. 몸체 기본 신발의 보정을 모든 장비에 무조건 재사용하지 않는다.

### 바닥 관통과 발 피부가 신발을 뚫는 현상을 구분한다

바닥 관통은 최종 스킨과 지면의 관계다. 발 피부가 신발 밖으로 나오는 현상은 두 스킨의 fit·bind·가중치·표시 범위 문제다. 지면 보정으로 둘을 함께 들어도 서로의 관통은 해결되지 않는다.

발/몸체와 신발이 별도 자산이면 같은 실제 bind와 완료 포즈로 먼저 비교한다. rest에서도 겹치면 신발 내부 공간·체형 fit을 수정하고, 움직일 때만 겹치면 foot/ball/legacy 가지의 weight 분포·toe 굽힘·중복 변형을 비교한다. 필요하면 발과 신발을 함께 DCC에서 조정한다. 서로 다른 체형·굽을 같은 이름의 뼈만으로 호환 승인하지 않는다.

장비가 완전히 가리는 피부는 기존 장비 표현 소유권에서 해당 신체 파츠/section 또는 제작된 coverage mask를 제어할 수 있다. 공용 발 본 자체를 숨기거나 scale 0으로 만들면 신발·장식·소켓과 다른 파츠까지 영향을 받을 수 있으므로 단순 관통 처방으로 사용하지 않는다. 장비 해제·교체 시 원래 피부 표시를 복원하고 노출 경계와 seam을 검사한다. 현재 작업의 지지 표본은 지면 관계를 다루며 모든 미래 발/신발의 상호 관통을 자동 해결하는 기능이 아니다.

### 지면 관계와 실제 스킨을 검사한다

최소 검사에는 정지 양발, 전진·후진·횡이동, Walk/Run/Sprint, Start/Stop/180도 회전, 발목 roll, 점프/착지, 전신 공격, 경사/단차가 포함된다. 실제 바닥 trace와 최종 스킨 표면을 같은 frame에서 비교하고 양쪽 발의 plant/swing·속도·mode도 기록한다. 화면 한 장, ref pose 최저점, 발 bone Z만으로 무관통을 판정하지 않는다.

GreatSword의 이전 직접 재생 검사에는 순간 약 -4.24cm의 신발 최저점이 남아 있었다. 후속 조사에서 heel-lift만 끈 A/B도 약 -4.07cm로 남아 단순 옵션 해제로 해결되지 않음을 확인했다. 최초 밑창 보정의 지상 322회 표면 표본은 최저 약 +0.15cm였지만 공중/착지 확대 검사에서 순간 누락이 확인됐다. 지상 성공만으로 완료하지 않고 착지 경계도 다시 검사한다. 최종 결과는 아래의 실측과 담당 런타임 문서에 기록한다.

Blender 재검사에서는 `foot_00`의 Armature modifier가 하나였고 밑창 표본의 가중치 합은 약 1이었다. 표준 foot/ball과 legacy Foot/Toe0의 기준 위치도 일치했다. 이 확인 범위에서 전체 재리깅을 요구하는 결함은 찾지 못했다. 관통 순간의 발볼 본 자체가 지면 아래 약 2.7cm였으며, 관절 기준 높이만 하한으로 둔 시험도 실제 신발에서는 약 -1.8cm가 남았다. 그 공통 Rig 시험은 원본 hash와 일치하도록 복원했다. 신발 형상·스킨을 무시하고 bone origin만 맞추는 방식의 한계다.

최종 구성은 실제 메시의 `UProject_JFootContactData`에 낮은 밑창 표면의 bind-local 스킨 표본을 저장하고, 선택적 `FootClearance`를 몸체의 기존 contact 처리 앞에 두는 방식이다. `AuthorFootSupport`는 명시한 발 본과 하위 스킨을 기준으로 생성하며 메시/가중치를 바꾸지 않는다. `InstallFootClearance`는 공통 master에 붙이지 않으며 body InputPose 또는 명시적 retarget 입력의 기존 component-space 흐름을 사용한다. GreatSword는 좌우 80개씩 사용하며 최종 설정은 clearance 0.15cm, 허용 lift 30cm다. 최종 착지 검사에서 실제 최대 필요량 약 26.50cm를 확인했다. **상한을 늘려 통과한 사실만으로 리깅이 정상이라는 증거가 되지는 않는다.** 이 상한은 해당 신발/검사 동작의 선택적 환경 보정 범위이며 미래 에셋의 기본값으로 복사하지 않는다.

### 큰 보정량은 리깅·원본 포즈·이동 위상을 분리한다

동일한 압축 클립 `M_Neutral_Jump_F_Start_Cliff_Rfoot`의 1.2167초를 실제 Manny와 GreatSword에서 root-relative로 평가했다. Manny의 왼발볼은 -21.50cm, GreatSword는 -21.41cm였으며 GreatSword의 실제 밑창 표본은 -25.21cm였다. 원본 Manny에서도 루트 아래로 발이 내려가는 공중 포즈다. 정상 landing 클립의 다른 표본에서는 Manny 발볼 +0.81cm, GreatSword -1.74cm·밑창 -3.30cm였다. 따라서 큰 lift 전체를 하이힐/리깅 불량으로 단정하거나 이를 없애려고 캐릭터를 Manny 길이로 다시 늘이지 않는다.

이는 같은 시각의 raw pose 비교 근거다. **Manny에도 같은 포즈가 있다는 사실은 26cm 환경 보정의 근본 원인이 모두 해결됐다는 증거가 아니다.** 실제 플레이에서 어느 클립/phase와 물리 접지가 만나는지의 원인은 이 비교만으로 확정하지 않았다. 현재 surface 보정의 무관통 결과와 원본/phase 원인의 해결 여부를 분리한다. 조사 중 Manny를 PIE에서 임시 교체한 실행은 필요한 공중 case가 수집되지 않아 비교 승인에서 제외했다. 입력 포즈→각 PP 단계→완료 스킨과 같은 프레임의 물리 지면을 연결해야 한다. 지속적인 큰 lift·무릎 반전·몸통 압축이 보이면 원본/변환 클립과 상태 선택부터 원인을 고치고, 보정 상한을 늘리는 것으로 마감하지 않는다. 이번에는 skin/bind 결함이 확인되지 않아 재스키닝하지 않았고 source clip도 측정만 했다.

데이터는 신발·분리 신체·전체 몸체 중 실제 표시하는 스킨 자산에 귀속시킨다. 몸체 PP는 같은 pose를 공유하는 현재 표시 파츠만 수집하고, 같은 다리의 여러 표면을 한 solve로 모은다. 별도 CopyPose/독립 변형 파츠는 자동으로 같은 pose라고 가정하지 않는다. 자산을 교체·숨김·해제하면 이전 데이터에 의존하지 않는다. 현재 자동 수집은 같은 Pawn이 소유한 메시와 그 LeaderPose 파츠 범위다. 별도 장비 Actor, 독립 CopyPose/물리 변형, section/material mask로 숨긴 피부는 같은 계약이라고 추정하지 않는다. 특히 공유 SKM의 userdata를 장착 때 전역 변경해 다른 캐릭터까지 바꾸지 않는다. 분리 발 파츠의 가시성·coverage 또는 해당 장비 표현 계약에서 활성 지지 형상을 정한다. geometry·weight·bind를 바꾸거나 발 모양을 바꾸는 morph/WPO를 추가하면 지지 표본을 다시 제작·검수한다.

현재 제작기는 발 하위 본에만 가중된 LOD0 정점 중 지정한 하단 높이 구간을 선택하고 중복 표본을 제거한다. 한 다리 최대 256개이며 초과하면 제작을 거절한다. 이 경우 실제 지지 형상을 유지하는 표본/구간을 검토한다. 큰 수로 제한을 풀어 고밀도 메시 전체를 매 프레임 계산하지 않는다. 여러 표시 파츠를 합칠 때 clearance는 최대, 허용 lift는 최소를 사용하므로, 동시에 노출하는 맨발·신발의 허용 범위도 함께 조정한다. 서로 다른 thigh/calf 체인이나 맞지 않는 component transform은 같은 solve로 합치지 않는다.

최종 검수에서 지상 이동 1,607표본, 착지 8조건/1,840표본, 전신 공격 6종/223표본과 정지 8도 경사를 검사했다. 별도 스킨 표면 관측은 지상·착지 691회/공중 178회와 지상 이동 321회/공격 234회, 경사 63회에서 최저 약 +0.15cm였다. 관련 native 67개도 통과했다. 조건·원자료·손 접촉 반복과 관측 경고는 [현재 런타임 검증](../../Architecture/Runtime/Player_Character_Master.md#신발-지지-표면-후속-보완)을 따른다.

실행 중에는 렌더 정점 전체를 CPU 스키닝하지 않고 제한된 표본과 GT ground plane snapshot을 사용한다. 평가용 point/influence 버퍼는 재사용하고 활성 개수·유효성은 매 준비 단계에서 비워, 장비 교체 시 이전 표본이 남지 않게 한다. 현재 구현의 정책은 실제 물리 이동의 접지 또는 낙하 하강, Z-up, 양의 균일 scale, 기존 foot IK 품질 허용, 서버/숨김 제외다. 지면 위에 있는 발에는 보정량이 0이므로 공중의 발을 바닥에 강제로 붙이지 않는다. 착지 시험에서는 애니메이션의 이전 공중 snapshot으로 접지를 거절하거나, 이전 포즈에서 높이 든 발 기준의 짧은 trace가 바닥을 놓칠 수 있었다. 접지 admission은 기존 물리 이동 관측을 사용하고 trace Z는 캡슐 지지 높이를 기준으로 한다. 이전 포즈의 foot XY는 검사 위치에만 사용한다. 애니메이션 상태를 접지의 별도 권위로 만들지 않는다.

호출 시점을 옮기는 것만으로 해결됐다고 판단하지 않는다. 실제 시험에서 평가 시점을 옮긴 뒤에도 남은 관통은 초기 10cm 상한에 걸린 것으로 A/B에서 확인됐다. 50cm는 원인 분리용 임시값이며 최종 제작값이 아니다. 일반 node `PreUpdate`에서만 준비하는 경로도 함께 검토했다. Mover/root motion 추출을 위한 조기 animation update와 실제 pose evaluation은 같은 시점이 아니다. 새 `PrepareContact`는 기존 몸체 Proxy의 GT `PreEvaluateAnimation`에서 선택적 노드만 준비하고, worker는 그 값과 현재 입력 포즈만 읽게 구성했다. 타이머·두 번째 이동 상태·worker의 월드 조회를 추가하지 않는다. fallback의 기존 retarget source capture는 원래 `PreUpdate` 순서를 유지한다.

정지 경사 fixture는 캐릭터가 바닥을 점유하기 전에 만들고 PIE 종료로 폐기한다. 점유 중인 바닥을 크게 이동·복원하면 이동 베이스 효과가 섞여 정지 경사 검증이 되지 않는다. `CaptureViewport`의 에디터 월드와 실제 PIE 게임 카메라 캡처도 구분한다. 시각 검수 전 현재 Pawn의 위치·mode와 촬영 대상을 확인한다.

허용 lift를 넘는 큰 오류는 리깅·애니메이션·접지 정책을 다시 조사한다. 최대 lift나 clearance를 크게 늘려 잘못된 제작을 감추지 않는다. `InspectFootClearance`는 완료 포즈의 실제 필요량과 적용량을 에디터에서 조회하므로, 상한 초과와 표본 누락·다른 단계의 변형을 구분할 수 있다. 이 제작용 관측값은 Shipping 런타임 상태로 유지하지 않는다. 조회 전에 해당 메시의 병렬 pose 완료를 기다려 이전/현재 포즈 관측을 섞지 않는다. 경사·단차·움직이는 발판, 스케일·LOD·장비 교체와 실제 비용은 각각 검수해야 한다.

## 11. 저장·실패 복구에서 반복하지 않을 일

- UE5.8 solver-controller struct setter는 asset dirty를 남기지 않은 사례가 있다. 소유한 대상만 필요 시 `save_loaded_asset(asset, False)`로 저장하고 새 프로세스에서 다시 읽어 확인한다. 전체 콘텐츠를 무차별 강제 저장하지 않는다.
- Control Rig의 runtime class override와 authored default는 다를 수 있다. graph에 보이는 class만 보고 실제 실행 class를 판정하지 않는다.
- Blender 비교 씬을 분리 저장하는 `bpy.data.libraries.write`에서 내부 접근 오류가 났다. crash log를 보존하고 일반 `save_as_mainfile` 사본 저장·재열기로 복구했다. 실패한 원본/작업 파일을 곧바로 덮거나 같은 충돌 작업을 반복하지 않는다.
- C++ compile 성공, native 계약 통과, 실제 입력 성공, 외형 품질, 군중 성능은 각각 다른 증거다. 실패 기록과 이전 리타깃 경로의 수치를 현재 직접 재생의 성공으로 덮어 해석하지 않는다.
- MCP 콘솔/스크립트 호출의 성공은 제출 성공일 수 있다. 결과 JSON의 `completed`, 시각과 실제 수행 표본을 읽은 뒤 다음 단계로 진행한다. 아직 끝나지 않은 PIE callback을 종료하고 예전 결과 파일을 최신 결과로 읽지 않는다.
- 임시 PIE floor/카메라/fixture World는 제작 레벨로 저장하지 않는다. 임시 파일·백업은 실행/Cook 의존성이 되지 않게 한다. 마지막 dirty package와 설정 복원·정상 종료도 확인한다.

## 12. 다음 캐릭터의 빠른 작업 순서와 예외 기록

| 단계 | 통과 기준/다음 작업 |
| --- | --- |
| 입구 조사 | 실제 source·몸체·장비·물리 소유권 확인, 직접/리타깃 경로 결정 |
| Blender 비교 | 실제 UE export overlay, 공유 계층/축/rest/단위와 체형 차이 표 |
| 리깅 수정 | helper 역할별 추종/독립 목표, 의도한 skin/bind 변경과 비대상 보존, FK/IK 조건별 확인 |
| FBX 왕복 | 예상 root/scale/본/geometry/weights와 실제 mesh bind, 공유 Skeleton 차이의 의도·소비 정책 확인 |
| 정적 UE 연결 | Skeleton 호환·Slot·BlendProfile·소켓·PA·Cloth·Nanite/재질 유지 |
| 몸체 후처리 | 단일 소유권, 좌표 공간/alpha, 접지·도달 범위, 추가 본 실행 |
| 실제 입력 | 이동/공중/착지/공격/취소/복귀, 스킨 표면/접촉 오차/소스 갱신, 체형별 검색·전환 품질 |
| 수명/비용 | 장비 교체·해제·등록·leader 변경, 서버/숨김/거리·LOD 계약 |
| 저장 마감 | 재시작 재현, 원본 보존, dirty 0, 작업 기록/잔여 검수와 에디터 종료 |

LOD별로 제거되는 필수 본·소켓·morph/교정 driver·가중치 제한, 의상 seam·cloth binding과 전환 pop을 확인한다. LOD0 정점 검증을 모든 LOD로 확대하지 않는다. 리깅/재임포트로 만든 새 root는 root-motion 추출/적용·캡슐 이동·루프/취소에서 이중 이동이 없는지도 확인한다.

캐릭터별 기록에는 다음을 남긴다. 문제가 없는 캐릭터에 불필요한 보정 노드/프로필을 자동 추가하지 않는다.

```text
몸체 / 신발 / 무기 / 엔진·임포터 버전:
직접 재생 또는 Family/Body 경로와 선택 근거:
UE 기준 asset / export / Blender 비교 파일:
공유 계층·축·rest 차이와 정상적인 체형 차이:
추가 본·정점·웨이트·재질·Cloth 보존 근거:
증상 재현 조건과 문제가 생긴 첫 단계:
측정한 입력·최종 값 / 단위·공간·LOD:
배제한 원인과 실패한 방법 / 실패 이유:
최종 수정의 소유자(asset/profile/node), 선택 이유:
후처리 필요성 / 우회 A/B / 상한·품질/LOD·실패 시 동작:
근본 원인 해결 여부와 남은 증상 완화·미확정 원인:
기존/다른 경로 영향, 교체·취소·종료·재등록 확인:
실제 전/후 증거와 미검증·잔여 외형/성능:
```

MMORPG의 새 SKM에는 geometry LOD/Nanite뿐 아니라 bone LOD, pose update, 손/발 보정, RigidBody, surface Cloth, material/draw 비용이 있다. 기존 quality coordinator와 gameplay pose 권위를 공유하고 별도 timer/예산 관리자를 추가하지 않는다. 전용 서버의 cosmetic solve와 숨은 producer의 필수 pose는 구분한다. 새 메시·새 Cloth의 군중/원격 비용을 이전 200명 결과로 승인하지 않는다. 이번 작업의 검증으로 Cook/Shipping·래그돌·모든 경사/장비 조합까지 통과했다고 확대하지 않는다.

군중 비용 검수 예시는 [직접 ABP 200명 비용 보고서](../../Performance/Reports/ProjectJ_DirectRig_200_2026-10-25.md)와 [Cloth 병목의 분리 측정](../../Performance/Reports/ProjectJ_Cloth_Budget_200_2026-10-25.md)을 따른다. main과 PostProcess의 실제 pose 소유자·Leader Pose 파츠·Cloth를 함께 집계한다. 새 메시가 몸체/머리를 분리하면 파츠 수가 달라질 수 있으므로 기존 개수에 맞추려고 에셋을 제거하지 않는다. 리타깃 엔진 scope의 소멸과 전체 frame/GT/GPU 비용을 별도로 검증하고, 본 수·LOD·물리·실행 역할이 다른 과거 결과를 순수 연산 절감률로 바꾸지 않는다.

| 제작·성능 문제 | 먼저 확인할 것 | 피할 처방 |
| --- | --- | --- |
| 직접 ABP 전환 뒤 전체 frame이 비슷함 | 실제 리타깃 scope 소멸, GT 대기 구간, Cloth task, 같은 부하의 기능별 진단 | 본 이름/클래스 이름만 보고 리타깃이 남았다고 판단하거나 worker CPU를 frame에서 뺌 |
| 망토가 군중 비용을 지배함 | simulation 정점·충돌체·CCD·substep/iteration, 실제 solver 값, 원격/로컬 품질 차이 | 전체 Cloth OFF나 전역 병렬화 변경을 검수 없이 생산 해법으로 적용 |
| 모듈러 몸체가 먼 거리에서도 LOD0임 | 표시 파츠의 하위 LOD 존재, attach-parent·Leader Pose LOD 동기화와 숨은 producer의 별도 역할 | producer의 필수 본을 먼저 제거하거나 모든 캐릭터에 강제 LOD 적용 |
| head에 피부와 헤어 카드가 섞임 | 재질별 형상·투명도·카드 구조, 얼굴/헤어의 극단 포즈와 silhouette | head 전체를 일반 피부용 자동 reducer에 넣어 카드·속눈썹까지 축소 |
| 원격 물리만 품질 예산을 적용함 | 로컬 원본 복구, 장비·시뮬레이션 교체, 외부 interactor writer와 opt-out, 종료 경로 | 공유 Cloth asset 설정을 런타임에 수정해 로컬·다른 인스턴스까지 변경 |
| Editor 검사만 되고 Game에서 빌드가 실패함 | preview mesh·Nanite 제작 설정 등 editor-only API와 실제 런타임/cooked 데이터의 경계 | 에디터 preview mesh를 생산 fallback의 런타임 mesh source로 사용하거나 제작 설정이 Cook에도 있다고 가정 |

원격 표현의 solver 예산은 skin/bind·축·관통 같은 근본 리깅 결함의 해결책이 아니다. 검수한 의상의 비용을 줄이는 단계로만 사용하며, 로컬은 authored 설정을 유지한다. 렌더 LOD를 추가해도 Cloth simulation 정점 수가 자동으로 줄지는 않는다. Cloth 자체의 저해상도 simulation mesh/LOD가 필요하면 대응 render LOD·collision·전환을 별도로 제작·검증한다. 예산 opt-out이 필요한 새 의상은 품질 이유와 군중 비용도 함께 확인한다.

검증도 역할을 분리한다. 거리/quota 같은 값 정책은 NullRHI에서 검사할 수 있지만 실제 Cloth interactor·시뮬레이션은 real RHI가 필요하다. 해당 자동화는 `NonNullRHI` 기능 플래그로 등록하며, NullRHI에서 실행하지 않은 실물 검사를 통과 개수로 대신 승인하지 않는다. Editor·Game 빌드 성공, Cook/Shipping 실행, 원격 네트워크 부하와 시각 품질은 각각 별도 근거다.

임시 비교 캡처·실패 로그·실험 스크립트·중간 백업의 상세 원문은 [해시 검증과 복원이 가능한 보관소](../../../Archives/Validation/README.md)에 모은다. 현재 제작 소스와 재임포트 FBX는 `SourceArt`에 유지하며, 로컬 Saved 경로의 자료를 정리했을 때는 보관 명세의 원래 경로로 찾는다.

## 13. 공식 자료와 이번 판단의 구분

2026-10-25 확인. 아래는 제작 원리의 근거다. 현재 프로젝트의 수치·통과 범위는 위 실측이며, 특정 수치나 모든 미래 자산의 품질을 공식 문서가 보증하는 것은 아니다.

| 1차 자료 | 적용하는 원리 / 적용 한계 |
| --- | --- |
| [Epic Skeletons (UE5.8)](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletons-in-unreal-engine) | 공유 이름/계층과 peripheral branch, Compatible Skeletons로 애니메이션 자산 공유. 비율이 달라도 접촉이 자동 완성된다는 뜻은 아님. |
| [Epic Animation Retargeting](https://dev.epicgames.com/documentation/unreal-engine/animation-retargeting-in-unreal-engine?lang=en-US) | translation의 Animation/Skeleton/AnimationScaled와 별도 end-effector 정책. 프로젝트 실제 effective mode를 읽고 선택. |
| [Epic Motion Matching](https://dev.epicgames.com/documentation/unreal-engine/motion-matching-in-unreal-engine) | Schema의 bone/채널·query와 history를 함께 검수. 이름 호환이 검색 품질의 자동 보장은 아님. |
| [Epic Root Motion](https://dev.epicgames.com/documentation/en-us/unreal-engine/root-motion-in-unreal-engine) | 전체 구조 기준 root와 추출/적용을 구분. 프로젝트 Mover의 기존 root-motion 소유권을 유지하며 문서의 일반 Character 이동 규칙을 그대로 이식하지 않음. |
| [Epic Modular Characters](https://dev.epicgames.com/documentation/unreal-engine/working-with-modular-characters-in-unreal-engine?lang=en-US) | LeaderPose는 동일 subset 구조·공유 pose에 사용. extra bone 독립 평가에는 별도 경로가 필요하고 파츠 draw 비용은 남음. 현재 vertex Cloth 실행은 별도 엔진/실기 검증 근거. |
| [Epic FBIK 설정 API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/IKRig/FIKRigFBIKSettings), [FBIK 제작](https://dev.epicgames.com/documentation/en-us/unreal-engine/control-rig-full-body-ik-in-unreal-engine) | root/effector/길이·preferred angle·제약과 반복 비용을 같이 조정. 32회는 이 자산의 검증값이며 엔진 보편 최적값이 아님. |
| [Epic Solve Directions](https://dev.epicgames.com/documentation/unreal-engine/control-rig-forwards-solve-and-backwards-solve-in-unreal-engine?lang=en-US) | Forward와 skeleton→control의 Backward bake는 별도 계약. 컨트롤 존재만으로 역방향 bake 지원을 주장하지 않음. |
| [Epic Montage notify context](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FAnimNotifyMontageInstanceContex-?lang=en-US), [Montage instance API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FAnimMontageInstance) | 같은 montage asset의 중첩·재실행을 실행 ID로 구분. 새 제어의 시작 승인과 이미 남은 outgoing pose/정리 수명을 구분. |
| [Blender Armature Modifier (5.2)](https://docs.blender.org/manual/en/latest/modeling/modifiers/deform/armature.html) | vertex groups/envelopes와 Preserve Volume의 변형 차이. 이번 38개는 단일 armature·groups 사용·envelopes/DQ 비사용을 확인. |
| [Blender Rigify Generated Features](https://docs.blender.org/manual/en/dev/addons/rigify/rig_features.html) | FK/IK snap, pole·부모 공간·heel/toe control 검수의 참고. 커스텀 GreatSword가 Rigify라는 뜻이나 모든 옵션을 추가해야 한다는 요구는 아님. |

로컬 근거는 `Saved/Validation/RigPostReview_20261025`의 `InspectSource.json`, `SourceReview.json`, `RawPoseAudit_Extended.json`, 32/20회 접촉 A/B와 빌드/회귀 보고서다. Saved는 실행 의존성이나 배포 문서가 아니며 중요한 결과·한계는 이 가이드와 런타임 문서에 남긴다.
