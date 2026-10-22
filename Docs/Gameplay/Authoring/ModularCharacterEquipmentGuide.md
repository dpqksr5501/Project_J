# 모듈러 캐릭터 및 의상 파이프라인 가이드 (Modular Character & Equipment Guide)

이 문서는 `Project_J`의 테스트 캐릭터(`GreatSword_Woman`) 및 모듈러 의상(`Clothes/`) 에셋을 프로젝트에 임포트하고, 머티리얼을 올바르게 매핑하며, 리더 포즈 컴포넌트와 본 기반 2차 물리 시뮬레이션(`RigidBody`)을 구축하는 표준 워크플로를 정리한 가이드입니다.

현재 캐릭터 제작 계층은 `BP_Player_Master → BP_GreatSword`, 공통 모션은 Manny `ABP_Player_Master`입니다. 대검의 Bip01 몸체에는 별도 Family 리타깃을 적용합니다. 상속·프로필·native 의상 연결의 기준은 [플레이어 마스터](../../Architecture/Runtime/Player_Character_Master.md)를 따릅니다. 아래 머티리얼·임포트 수치는 해당 에셋의 제작 기록이며 모든 신규 몸체의 공통 설정값을 뜻하지 않습니다.

---

## 1. 아키텍처 개요 (Architecture Overview)

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

1. **단일 마스터 스켈레톤 공유**: 모든 모듈러 의상 FBX는 메인 캐릭터와 동일한 `GreatSword_Woman_Skeleton`을 공유합니다.
2. **리더 포즈 컴포넌트 (`SetLeaderPoseComponent`)**:
   * 의상 파츠 컴포넌트들은 메인 캐릭터 메시를 Leader(부모)로 설정하여 애니메이션 본 트랜스폼을 실시간 동기화합니다.
   * 대검의 authored 의상 8개는 `UProject_JModularMeshComponent`의 **Follow Attach Parent Pose**를 켜고 실제 표시 몸체의 자식으로 배치합니다. 등록·재등록·부모 교체 때 native 연결이 적용되므로 Construction Script에서 `Attach and Set Leader`를 반복 호출하지 않습니다. 런타임 장비는 기존 EquipmentRuntime이 명시적으로 연결합니다.
3. **런타임 물리 시뮬레이션 합성 (`RigidBody`)**:
   * LeaderPose 파츠는 자신의 애니메이션 그래프를 평가하지 않습니다. 표시 몸체가 평가한 보조 뼈는 공유할 수 있지만 파츠만의 추가 뼈를 독립 시뮬레이션하려면 자동 LeaderPose를 끄고 기존 CopyPose + 해당 Post Process 경로를 사용합니다.

---

## 2. 텍스처 임포트 파이프라인 (DDS to PNG Batch Conversion)

### 2.1. 배경 및 문제 원인
* 검은사막(BDO) 등 원본 소스에서 추출된 DDS 텍스처는 블록 압축 포맷인 **BC1 (DXGI 71)** 및 **BC3 (DXGI 77)**을 사용합니다.
* UE 5.8의 공식 Interchange Importer(`InterchangeDDSTranslator`)는 해당 DXGI 포맷의 DDS 직접 임포트를 지원하지 않아 에러를 발생시킵니다.

### 2.2. 해결 워크플로
* 원본 DDS 파일을 무손실 PNG 포맷으로 일괄 변환하여 언리얼 엔진에 임포트합니다.
* 변환 완료 목록: `characters/textures` (14개), `armor/textures` (45개), `weapons/textures` (4개) 총 63개 파일 무손실 100% 변환.

---

## 3. 메인 캐릭터 (`GreatSword_Woman`) 머티리얼 구성

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
`pdw_00_nude_0001_w` 텍스처는 4개 채널에 특수 기능이 패킹된 멀티 채널 팩입니다:
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

현재 대검은 표시 몸체 `ABP_Greatsword_Woman_RunTIme`에서 세 RigidBody 노드가 망토 24개·헤어 4개·치마 2개의 dynamic bone을 계산합니다. 의상 8개는 최종 포즈를 LeaderPose로 공유하고 별도 AnimInstance/Tick을 평가하지 않습니다. 실험용 망토 Chaos Cloth 데이터 3개는 원본 메시 백업 후 제거했으며 `cloak_Physics`와 물리 본·skin weights는 유지했습니다. Physics Asset과 vertex Chaos Cloth 데이터는 다른 자원입니다.

향후 독립 Chaos Cloth 장비는 bone 평가를 공유해도 component Tick을 유지합니다. 기존 modular component가 활성 cloth metadata/resources로 구분합니다. 추가 본을 가진 장비의 CopyPose/Post Process 물리는 별도 그래프가 필요합니다. 등록 전 source 연결, 같은 source의 강제 재연결 금지, 교체·비동기 취소·해제에 대한 계약은 [플레이어 마스터](../../Architecture/Runtime/Player_Character_Master.md)를 따릅니다.

스켈레톤에는 이미 모션 물리 전용 뼈대 체인이 구축되어 있습니다:
* **망토**: `phy_anchor_cape_00~40` (고정점) ➔ `phy_cloth_cape_01~05` (5단계 체인)
* **머리카락**: `phy_anchor_Hair_00~60` (고정점) ➔ `phy_cloth_Hair_01~64`
* **치마/하의 자락**: `phy_anchor_Skirt_00~50` (고정점) ➔ `phy_cloth_Skirt_01~53`

### 5.1. 피직스 에셋(Physics Asset) 생성 황금 옵션
피직스 에셋 생성 대화상자에서 다음 수치를 준수합니다:
* **최소 본 크기 (Minimum Bone Size)**: **`3.0` ~ `5.0`** (기본값 20.0 사용 시 짧은 마디 본들이 누락됨)
* **작은 본 지나치기 (Skip Small Bones)**: **`OFF (체크 해제)`**
* **디폴트로 콜리전 비활성화 (Disable Collision by Default)**: **`ON (체크)`** (체인 본끼리 비벼지며 지터링/폭발하는 현상 방지)
* **컨스트레인트 생성 & 각도 모드**: **`Limited`**

### 5.2. 피직스 타입 설정 규칙
* **고정 지지대 (Kinematic)**:
  * 캐릭터 몸통 본 (`Bip01-Pelvis`, `Bip01-Spine*`, `Bip01-Neck`, `Bip01-Head`, `Bip01-Clavicle` 등)
  * 물리 앵커 본 (`phy_anchor_...` 계열)
  * *이유: 애니메이션 키프레임을 단단히 따라가며 흔들림의 기준점이 되어야 함.*
* **물리 시뮬레이션 (Simulated)**:
  * 실제 흔들릴 체인 마디들 (`phy_cloth_cape_01~05`, `phy_cloth_Hair_01~64` 등)
  * *이유: 중력, 관성, 원심력에 의해 실시간으로 펄럭여야 함.*

### 5.3. 몸체 관통 방지 콜리전 (Body Collision)
1. 피직스 에셋 뷰포트에서 등 캡슐(`Bip01-Spine2`) 또는 머리 캡슐(`Bip01-Head`)을 클릭합니다.
2. `Ctrl` 키를 누른 채 망토(`phy_cloth_cape_...`) 또는 머리카락 캡슐들을 다중 선택합니다.
3. 상단 툴바의 **`콜리전` ➔ `선택 간 콜리전 켜기 (Enable Collision Between Selected)`**를 실행합니다.
4. 실제 이동·급회전·착지·몽타주에서 collider 크기/위치, constraint 범위, 체인 길이와 skin weight를 함께 확인합니다. 충돌 pair 활성화만으로 모든 관통이 해결되지는 않습니다. 불필요한 체인 전체 상호 충돌은 추가하지 않습니다.

### 5.4. 런타임 애님 그래프 (`RigidBody` 노드)
* Bip01 의상 물리는 해당 표시 몸체의 ABP/Post Process에서 구성합니다. 공통 Manny `ABP_Player_Master`에 몸체 전용 뼈나 피직스 에셋을 추가하지 않습니다.
* CopyPose 파츠가 독립 물리를 평가하면 source 선행 평가·충돌·LOD·숨김/재표시·부모 해제를 확인합니다. 피직스 에셋 지정만으로 모든 체인의 품질을 보장하지 않습니다.
* **묵직한 질감 팁**: 피직스 에셋에서 캡슐 바디의 `Angular Damping (각 댐핑)`을 `1.5 ~ 3.0`으로 주면 펄럭임이 차분하고 고급스러워집니다.

---

## 6. 점검 체크리스트 (Authoring Checklist)

1. [ ] **DDS 임포트 실패 확인**: 텍스처 임포트 실패 시 무손실 PNG로 변환되었는가?
2. [ ] **메인 메시 슬롯 확인**: `MT_Body`, `MT_Eye`, `MT_Hair`, `MT_Eyeline`, `MT_Head`, `MT_Eyedeco` 6개가 정상 할당되었는가?
3. [ ] **눈알 투명도 확인**: `MT_Eyedeco`가 `Translucent` 모드이며 눈동자가 검게 가려지지 않는가?
4. [ ] **헤어 숱 확인**: `MT_Hair`의 `Opacity Mask Clip Value`가 `0.05` 수준이며 `Hair` 쉐이딩 모델이 적용되었는가?
5. [ ] **의상 소매 매핑 확인**: `sho`(어깨) 및 `upper`(상의) 슬롯 0에 `pdw_00_hand_0001` 머티리얼이 할당되어 회색 격자가 없는가?
6. [ ] **양면 렌더링 확인**: 망토(`cloak`), 치마, 머리카락 머티리얼의 `Two Sided`가 켜져 있어 뒷면이 투명해지지 않는가?
7. [ ] **리더 포즈 부모 연결**: authored 파츠의 Follow Attach Parent Pose가 켜져 있고 실제 표시 몸체에 붙어 있는가? 추가 뼈 독립 평가 파츠는 기존 CopyPose 경로를 사용하는가?
8. [ ] **피직스 에셋 바디 검증**: `Bip01` 및 `phy_anchor` 본은 `Kinematic`, `phy_cloth` 체인은 `Simulated`인가?
9. [ ] **몸체 관통 방지**: 등/머리 캡슐과 클로스 캡슐 간 `선택 간 콜리전 켜기`가 수행되었는가?
10. [ ] **인게임 물리 구동**: 해당 몸체/독립 파츠의 물리 그래프가 평가되며 공통 Manny 로코모션과 중복 실행되지 않는가?
