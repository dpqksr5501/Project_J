# 모듈러 캐릭터 및 의상 파이프라인 가이드 (Modular Character & Equipment Guide)

이 문서는 캐릭터/장비의 포즈 공유·독립 변형·물리 소유권을 정하고, 기존 GreatSword 의상 임포트 사례를 참고하는 제작 가이드다. 원본 리깅·임포트 검수는 [리깅·UE5 후처리 가이드](../../Animation/Authoring/Character_Rigging_And_UE5_Postprocess_Playbook.md)의 원인 분리·통과 조건을 따른다.

현재 캐릭터 제작 계층은 `BP_Player_Master → BP_GreatSword`, 공통 모션은 `ABP_Player_Master`다. 시험 브랜치의 UE5 Complete 몸체는 직접 재생하고, 보존한 비호환 Bip01 몸체는 별도 Family 리타깃을 사용한다. 상속·프로필·native 의상 연결의 기준은 [플레이어 마스터](../../Architecture/Runtime/Player_Character_Master.md)를 따릅니다. 아래 머티리얼·임포트 수치는 해당 에셋의 제작 기록이며 모든 신규 몸체의 공통 설정값을 뜻하지 않습니다.

---

## 1. 아키텍처 개요 (Architecture Overview)

### 1.1. 현재 연결과 신규 파츠 승인

| 구성 | 포즈/물리 소유권 |
| --- | --- |
| 현재 UE5 GreatSword | 숨은 Complete에서 공통 master + 몸체 Post Process. Body/Head/의상 8개가 최종 포즈 공유 |
| 헤어·치마 | 현재 몸체의 RigidBody 2개. 같은 가지를 의상에서 다시 계산하지 않음 |
| 현재 망토 | 파츠 vertex Chaos Cloth 3개 데이터/2,534 물리 정점. 필요한 파츠 Tick 유지, 망토 bone RigidBody와 중복 적용하지 않음 |
| 일반 LeaderPose 장비 | producer에 존재하는 공유 본의 pose 사용. 자체 추가 본의 독립 평가를 기대하지 않음 |
| 독립 추가 본 장비 | 명시적 CopyPose + 해당 장비 처리. source 선행 평가·계층/공간·초기 pose·물리 책임 검수 |

같은 Skeleton asset을 지정한 사실만으로 파츠를 승인하지 않는다. 공유 본의 이름/부모·실제 bind와 장비 fit·원점/scale·seam을 검수한다. static/socket 부착 장비에 불필요한 Skeleton 공유나 CopyPose를 강제하지 않는다. reference에서 이미 잘못된 피부/장비 fit이나 weight는 DCC에서 고치며, collision 두께·damping·숨김으로 리깅 결함을 가리지 않는다. 의도적으로 옷에 가려지는 피부의 coverage 처리는 별도 표현 계약으로 기록하고 탈착 때 복원한다.

### 1.2. 이전 Bip01 에셋의 참고 계층

다음은 `GreatSword_Woman`/`Clothes`를 사용하던 이전 계층 기록이다. 신규 파츠의 본 이름/개수나 현재 UE5 Complete의 필수 구조가 아니다.


```text
[GreatSword_Woman (SkeletalMesh: Head + Body)] 
  └── Skeleton: GreatSword_Woman_Skeleton
        ├── Bip01 Body Hierarchy (Pelvis -> Spine -> Neck -> Head / Limbs)
        ├── phy_anchor_Hair_*  -> phy_cloth_Hair_* (헤어 물리 뼈대 체인)
        ├── phy_anchor_cape_*  -> phy_cloth_cape_* (망토 물리 뼈대 체인)
        └── phy_anchor_Skirt_* -> phy_cloth_Skirt_* (치마/하의 물리 뼈대 체인)
              ▲
              │ SetLeaderPoseComponent(MainMesh)
              │
[Modular Equipment Components (Clothes/)]
  ├── upper (상의) / lower (하의) / sho (어깨)
  ├── hand (장갑)  / foot (신발)  / cloak (망토)
  └── hel (투구)   / inner (속옷)
```

1. **이전 자산의 Skeleton 공유**: 아래 예시의 Bip01 의상은 `GreatSword_Woman_Skeleton`을 사용했다. 신규 자산은 실제 producer와 검증한 골격/pose 계약을 사용하고, 모든 장비를 이 Skeleton으로 강제 변환하지 않는다.
2. **리더 포즈 컴포넌트 (`SetLeaderPoseComponent`)**:
   * 의상 파츠 컴포넌트들은 메인 캐릭터 메시를 Leader(부모)로 설정하여 애니메이션 본 트랜스폼을 실시간 동기화합니다.
   * 대검의 authored 의상 8개는 `UProject_JModularMeshComponent`의 **Follow Attach Parent Pose**를 켜고 실제 최종 pose producer의 자식으로 배치합니다(현재 UE5 구성에서는 숨은 Complete 포함). 등록·재등록·부모 교체 때 native 연결이 적용되므로 Construction Script에서 `Attach and Set Leader`를 반복 호출하지 않습니다. 런타임 장비는 기존 EquipmentRuntime이 명시적으로 연결합니다.
3. **런타임 물리 시뮬레이션 합성 (`RigidBody`)**:
   * LeaderPose 파츠는 자신의 애니메이션 그래프를 평가하지 않습니다. 표시 몸체가 평가한 보조 뼈는 공유할 수 있지만 파츠만의 추가 뼈를 독립 시뮬레이션하려면 자동 LeaderPose를 끄고 기존 CopyPose + 해당 Post Process 경로를 사용합니다.

---

## 2. 텍스처 임포트 파이프라인 (DDS to PNG Batch Conversion)

### 2.1. 이전 파일에서 관측한 임포트 문제
* 검은사막(BDO) 등 원본 소스에서 추출된 DDS 텍스처는 블록 압축 포맷인 **BC1 (DXGI 71)** 및 **BC3 (DXGI 77)**을 사용합니다.
* 당시 제공 DDS 파일의 Interchange 임포트가 실패해 PNG 변환을 사용했다. 이 기록만으로 UE5.8의 해당 압축 포맷 전체 지원 여부를 단정하지 않는다. 새 파일은 실제 엔진/translator와 header·압축·채널을 확인한다.

### 2.2. 해결 워크플로
* 원본 DDS 파일을 무손실 PNG 포맷으로 일괄 변환하여 언리얼 엔진에 임포트합니다.
* 변환 완료 목록: `characters/textures` (14개), `armor/textures` (45개), `weapons/textures` (4개) 총 63개 파일 무손실 100% 변환.

---

## 3. 이전 `GreatSword_Woman`의 머티리얼 제작 기록

이 절의 슬롯·alpha 수치와 텍스처 이름은 이전 자산 사례다. 새로운 메시의 슬롯/UV·alpha·의도한 shading과 실제 화면으로 결정하며 아래 수치를 승인 체크리스트로 복사하지 않는다.

`GreatSword_Woman.uasset` (헤드 + 바디 통합 스켈레탈 메시)은 총 **6개의 머티리얼 슬롯**으로 구성됩니다.

| 슬롯 인덱스 | 머티리얼 이름 | 원본 텍스처 | 권장 블렌드 모드 | 핵심 역할 및 설정 |
| :--- | :--- | :--- | :--- | :--- |
| **엘리먼트 0** | `MT_Body` | `pdw_00_nude_0001` 계열 | `Opaque` | 몸통 기본 피부 |
| **엘리먼트 1** | `MT_Eye` | `pdw_00_eye_0001_shadow` | `Opaque` | 동그란 안구 본체 (흰자위 + 홍채) |
| **엘리먼트 2** | `MT_Hair` | `pdw_00_hair_0007_shadow_hair` | `Masked` (Two-Sided) | 머리카락 (숱 복원 설정 필수) |
| **엘리먼트 3** | `MT_Eyeline` | `phw_00_eyeline_0001_dec` 계열 | `Masked` (Two-Sided) | 눈매 아이라인 & 속눈썹 |
| **엘리먼트 4** | `MT_Head` | `pdw_00_head_0003_shadow` 계열 | `Opaque` | 얼굴 및 두피 피부 |
| **엘리먼트 5** | `MT_Eyedeco` | `phw_00_eyedeco_0001_shadow_hair` | `Translucent` | 각막 반사광 & 눈꺼풀 그림자 (안구 덮개) |

### 3.1. 헤어 머티리얼 (`MT_Hair`) 숱 복원 및 쉐이딩 세팅
* **탈모 현상 원인**: 언리얼의 `Masked` 모드 기본 클립값(`Opacity Mask Clip Value = 0.3333`)이 헤어 텍스처의 섬세한 알파 그라디언트를 86.9%나 잘라내어 발생.
* **해결 세팅**:
  1. `Blend Mode`: `Masked`
  2. `Two Sided`: **`True (체크)`** (폴리곤 뒷면 렌더링)
  3. `Opacity Mask Clip Value`: `0.3333` ➔ **`0.05` ~ `0.08`**로 하향 조정
  4. 그래프 보강: 텍스처 Alpha 핀 ➔ `Multiply(2.5 ~ 3.0)` ➔ `Opacity Mask`
  5. `Shading Model`: `Default Lit` ➔ **`Hair`** (머리카락 특유의 빛 반사 하이라이트 링 구현)
  6. 헤어 컬러 염색: 무채색 베이스 텍스처 RGB에 `Multiply`로 원하는 색상(`Constant3Vector`)을 곱해 `Base Color`에 연결.

### 3.2. 안구 및 눈매 레이어 세팅 (`MT_Eye`, `MT_Eyeline`, `MT_Eyedeco`)
* **검은 선글라스 / 너구리 눈 현상 방지**:
  * `MT_Eyedeco`가 `Opaque`이면 눈알 앞을 덮는 얇은 껍질 메시에 검은색이 칠해져 뒤쪽 안구를 완전히 가립니다.
  * **`MT_Eyedeco`는 반드시 `Translucent`**로 설정하고, 텍스처의 Alpha 핀을 `Opacity`에 연결해야 부드러운 눈꺼풀 그림자와 맑은 눈알이 비쳐 보입니다.
  * **`MT_Eyeline`은 `Masked`**로 설정하고 텍스처 Alpha 핀을 `Opacity Mask`에 연결하여 속눈썹 외곽 여백을 투명하게 뚫어줍니다.

### 3.3. 바디 특수 채널 마스크 (`_w` 텍스처) 활용법
`pdw_00_nude_0001_w`의 다음 채널 용도는 이전 제작의 해석/활용 예시이며, 원본 패킹 명세를 검증한 보편 규칙은 아니다. 새 자산은 채널을 각각 표시하고 명세/외형과 대조한 뒤 연결한다:
* **`R` (Red)**: 상처 / 흉터 / 핏자국 (Wound / Scratch) ➔ 피격 시 `Lerp`로 붉은 상처 연출.
* **`G` (Green)**: 근육 음영 및 깊이감 (Cavity / AO) ➔ **머티리얼의 `Ambient Occlusion`에 연결 권장** (복근/쇄골 윤곽선 강화).
* **`B` (Blue)**: 땀방울 / 흘러내리는 물줄기 (Sweat / Wetness) ➔ `Roughness`를 0으로 깎아 젖은 피부 연출.
* **`A` (Alpha)**: 피부 표면 미세 굴곡 음영.

---

## 4. 모듈러 의상 에셋 (`Clothes/`) 머티리얼 및 아틀라스 매핑

검은사막 에셋 아키텍처는 UV 아틀라스를 여러 파츠 간에 공유합니다. 파츠 이름과 일치하지 않는 텍스처가 매핑되는 규칙은 다음과 같습니다:

| 파츠 FBX | 슬롯 번호 | 매핑할 텍스처 접두사 | 비고 및 아틀라스 공유 원리 |
| :--- | :--- | :--- | :--- |
| **`upper`** (상의) | 엘리먼트 0<br>엘리먼트 1 | `pdw_00_hand_0001`<br>`pdw_00_ub_0001` | **소매 부위는 장갑 텍스처 공유**<br>상의 본체 |
| **`sho`** (어깨) | 엘리먼트 0<br>엘리먼트 1<br>엘리먼트 2 | `pdw_00_hand_0001`<br>`pdw_00_lb_0001_hair`<br>`pdw_00_sho_0001` | **소매 부위는 장갑 텍스처 공유 (회색 방지)**<br>어깨 뒤편 깃털/장식 자락<br>어깨 본체 철갑 |
| **`lower`** (하의) | 엘리먼트 0<br>엘리먼트 1 | `pdw_00_lb_0001`<br>`pdw_00_lb_0001_dec` | 하의 본체<br>하의 금속/가죽 장식 |
| **`cloak`** (망토) | 엘리먼트 0<br>엘리먼트 1<br>엘리먼트 2 | `pdw_00_sho_0001`<br>`pdw_00_cloak_0001_hair`<br>`pdw_00_cloak_0001` | 어깨 연결 고정부 (어깨 텍스처 공유)<br>망토 깃털/털 장식<br>망토 본체 천 |
| **`hel`** (투구/머리장식) | 엘리먼트 0<br>엘리먼트 1 | `pdw_00_hel_0001_hair`<br>`pdw_00_hel_0001` | 투구 깃장식<br>투구 본체 |
| **`hand`** (장갑) | 엘리먼트 0 | `pdw_00_hand_0001` | 손 및 팔목 장갑 본체 |
| **`foot`** (신발) | 엘리먼트 0 | `pdw_00_foot_0001` | 부츠 |
| **`inner`** (속옷) | 엘리먼트 0<br>엘리먼트 1 | `phw_99_ub_0001_02`<br>`pdw_00_uw_0001_dec` | 속옷 상의<br>속옷 하의 |

> **중요 (백페이스 컬링 해결)**: 망토(`MT_Cloak`), 치마(`MT_Lower`), 어깨 깃털(`MT_Sho_Hair`) 등 두께가 없는 한 겹짜리 천/가죽 에셋은 머티리얼 디테일에서 **`Two Sided (양면)`를 반드시 활성화**해야 펄럭일 때 뒷면이 투명해지지 않습니다.

---

## 5. 본 기반 2차 물리 시뮬레이션 (`RigidBody` / Physics Asset)

현재 UE5 몸체의 구성은 1.1절을 따른다. **이전 Bip01 경로**는 `ABP_Greatsword_Woman_RunTIme`의 RigidBody 3개(망토 24·헤어 4·치마 2 dynamic bone)로 검수한 이력이 있다. 당시 실험 Cloth 3개를 백업 후 제거한 기록은 현재 UE5 망토 Cloth를 삭제하라는 지침이 아니다. Physics Asset, bone RigidBody와 vertex Cloth는 서로 다른 자원/소유권이다.

향후 독립 Chaos Cloth 장비는 bone 평가를 공유해도 component Tick을 유지합니다. 기존 modular component가 활성 cloth metadata/resources로 구분합니다. 추가 본을 가진 장비의 CopyPose/Post Process 물리는 별도 그래프가 필요합니다. 등록 전 source 연결, 같은 source의 강제 재연결 금지, 교체·비동기 취소·해제에 대한 계약은 [플레이어 마스터](../../Architecture/Runtime/Player_Character_Master.md)를 따릅니다.

이전 자산에는 다음 이름의 물리 가지가 있었다. 새 장비는 이름 대신 실제 역할·가중치·기준 축을 매핑한다:
* **망토**: `phy_anchor_cape_00~40` (고정점) ➔ `phy_cloth_cape_01~05` (5단계 체인)
* **머리카락**: `phy_anchor_Hair_00~60` (고정점) ➔ `phy_cloth_Hair_01~64`
* **치마/하의 자락**: `phy_anchor_Skirt_00~50` (고정점) ➔ `phy_cloth_Skirt_01~53`

### 5.1. 피직스 에셋 생성의 시작값과 검수
다음은 이전의 짧은 장식 체인을 생성할 때 사용한 시작값이며 신규 리그의 필수값이 아니다. 먼저 단위·축·anchor/skin·체인 길이를 검수하고 생성된 shape/constraint를 확인한다:
* **최소 본 크기 (Minimum Bone Size)**: **`3.0` ~ `5.0`** (기본값 20.0 사용 시 짧은 마디 본들이 누락됨)
* **작은 본 지나치기 (Skip Small Bones)**: **`OFF (체크 해제)`**
* **디폴트로 콜리전 비활성화 (Disable Collision by Default)**: **`ON (체크)`** (체인 본끼리 비벼지며 지터링/폭발하는 현상 방지)
* **컨스트레인트 생성 & 각도 모드**: **`Limited`**

### 5.2. 2차 물리 가지의 역할 설정
이 절은 장식용 2차 물리의 예시이며 gameplay body/ragdoll의 전체 body mode 규칙이 아니다.

* **고정 지지대 (Kinematic)**:
  * 캐릭터 몸통 본 (`Bip01-Pelvis`, `Bip01-Spine*`, `Bip01-Neck`, `Bip01-Head`, `Bip01-Clavicle` 등)
  * 물리 앵커 본 (`phy_anchor_...` 계열)
  * *이유: 애니메이션 키프레임을 단단히 따라가며 흔들림의 기준점이 되어야 함.*
* **물리 시뮬레이션 (Simulated)**:
  * 실제 흔들릴 체인 마디들 (`phy_cloth_cape_01~05`, `phy_cloth_Hair_01~64` 등)
  * *이유: 중력, 관성, 원심력에 의해 실시간으로 펄럭여야 함.*

### 5.3. 몸체 관통 방지 콜리전 (Body Collision)

다음은 이전 Bip01 자산의 선택 예시다. 먼저 원래 mesh/장비 fit·weight와 초기 관통을 확인하고, 현재 자산의 실제 collider/constraint와 필요한 pair만 검사한다.
1. 피직스 에셋 뷰포트에서 등 캡슐(`Bip01-Spine2`) 또는 머리 캡슐(`Bip01-Head`)을 클릭합니다.
2. `Ctrl` 키를 누른 채 망토(`phy_cloth_cape_...`) 또는 머리카락 캡슐들을 다중 선택합니다.
3. 상단 툴바의 **`콜리전` ➔ `선택 간 콜리전 켜기 (Enable Collision Between Selected)`**를 실행합니다.
4. 실제 이동·급회전·착지·몽타주에서 collider 크기/위치, constraint 범위, 체인 길이와 skin weight를 함께 확인합니다. 충돌 pair 활성화만으로 모든 관통이 해결되지는 않습니다. 불필요한 체인 전체 상호 충돌은 추가하지 않습니다.

### 5.4. 런타임 애님 그래프 (`RigidBody` 노드)
* Bip01 의상 물리는 해당 표시 몸체의 ABP/Post Process에서 구성합니다. 공통 Manny `ABP_Player_Master`에 몸체 전용 뼈나 피직스 에셋을 추가하지 않습니다.
* CopyPose 파츠가 독립 물리를 평가하면 source 선행 평가·충돌·LOD·숨김/재표시·부모 해제를 확인합니다. 피직스 에셋 지정만으로 모든 체인의 품질을 보장하지 않습니다.
* **damping 조정**: 이전 `1.5 ~ 3.0` 예시는 보편적인 무게감 처방이 아니다. damping은 운동 감쇄 값이며 잘못된 축/constraint·초기 관통·질량·skin 문제를 해결하지 않는다. 원인을 고친 뒤 해당 solver·scale·동작에서 잔떨림과 과도한 정지를 비교해 필요한 값만 사용한다.

---

## 6. 점검 체크리스트 (Authoring Checklist)

1. [ ] 리깅·왕복·raw clip 검수를 통과했는가? 남은 문제가 처음 생긴 단계가 기록돼 있는가?
2. [ ] 파츠 역할에 맞게 socket/LeaderPose/CopyPose/Cloth를 선택했으며 같은 변형을 중복 평가하지 않는가?
3. [ ] 실제 공유 본·bind/scale·장비 fit·seam이 맞는가? 같은 Skeleton 이름만으로 승인하지 않았는가?
4. [ ] 파츠별 실제 슬롯/UV·텍스처 채널·투명/양면/shading이 맞는가? 과거 6슬롯/clip 값의 일괄 적용을 피했는가?
5. [ ] 추가 본을 producer가 계산하는지 파츠가 독립 계산하는지 정했으며, 필요한 source 선행 평가·Tick이 유지되는가?
6. [ ] 물리 anchor·body mode·shape/constraint frame·초기 충돌과 skin을 검수했는가? 두께/damping으로 원본 결함을 감추지 않았는가?
7. [ ] 움직임·착지·공격·교체/해제·숨김/재표시·LOD 전환에서 실제 완료 포즈·표면을 확인했는가?
8. [ ] 실제 cloth/교정/morph/필수 본의 LOD별 연결과 원격/군중 비용을 확인했으며, 미검증 범위는 구분했는가?
