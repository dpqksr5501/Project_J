# Project J - UE5.8 기반 3인칭 액션 MMORPG 프로토타입

> **학습 및 포트폴리오 제작을 목적으로 진행 중인 개인 MMORPG 개발 프로젝트입니다.**
> 탄탄한 C++ 시스템 뼈대를 바탕으로, 구현하고 싶은 다양한 액션과 비주얼 요소를 안정적으로 얹어 나가는 것을 목표로 합니다.

---

## 📊 Profiling Snapshot — 측정으로 확인한 현재 기반

`Development Editor PIE`에서 재현 가능한 harness와 Unreal/Network Insights로
수집한 결과입니다. 아래 숫자는 프레임 평균 FPS가 아니라 선택 구간의 timer 또는
network aggregate이며, workload별 조건이 다르므로 서로의 절대 우열 비교에는 사용하지 않습니다.

| 영역 | 측정 workload | 확인한 결과 |
|---|---|---|
| Animation CPU | local player + 이동 visual clone 100명 | `AnimNativeUpdate` 평균 **4.44 µs/call**, thread-safe snapshot **1.84 µs/call**, PoseSearch DB Chooser **0.21 µs/call** |
| Parallel animation | 동일 100명 local visual workload | thread-safe update 표본의 약 **99.99%**가 parallel-evaluation 상태 |
| GPU | 동일 camera의 `ProfileGPU` 1-frame sample | Frame **3.99 ms**, SceneRender **3.70 ms** |
| Iris runtime | PIE dedicated server + client 2 | server/client 모두 **IrisActive=1**, N2 baseline에서 write failure/pending backlog **0** |
| Movement replication | server-authoritative mover 50명 → client 2, 30 Hz | aggregate **62.3 KB/s**, 연결당 약 **31.2 KB/s**, mover 1개·연결 1개당 약 **4.84 kbit/s** |

N50은 전투, TIP, inventory mutation, 실제 player input을 제외한 **이동 복제 전용**
baseline입니다. mover를 always-relevant로 둔 의도적 worst-nearby 조건이므로,
AOI가 적용된 production bandwidth 예산이나 50개 실제 client connection 결과로
해석하지 않습니다.

**Engineering decision:** 현 측정에서는 animation worker를 더 쪼개거나
NetUpdateFrequency를 성급히 낮추지 않습니다. 다음 확장 지점은 대규모 NPC에 대한
representation tier와 connection별 AOI/relevance 정책입니다.

### Why the current baseline holds up

현재 결과가 단순히 “운 좋게 낮게 나온 sample”이 아니라는 근거는 아래 구조에 있습니다.

| 시스템적 선택 | 측정에서 확인된 효과 | 확장 시의 의미 |
|---|---|---|
| **C++ semantic locomotion snapshot → AnimInstance proxy** | `BuildThreadSafeData`가 100명에서 평균 1.84 µs/call로 작고, gameplay UObject 접근을 animation evaluation과 분리 | worker path에 Actor/GAS 조회나 임의 동기화를 밀어 넣지 않고, immutable input을 소비하는 구조 유지 |
| **엔진 지원 parallel animation 경로 준수** | `CanRunParallel=1`, thread-safe update의 약 99.99%가 parallel-evaluation 상태 | 임의 Task Graph 분리보다 엔진의 evaluation scheduling을 활용; dispatch/fence/copy overhead 회피 |
| **distance tier / URO 중심 cadence 제어** | S70에서 actor tick 약 7,369회/s와 animation native update 약 2,495회/s가 분리됨 | 모든 보이는 character를 매 frame 같은 빈도로 animation update하지 않는 기반 |
| **server-authoritative mover와 Iris batching** | N50에서 50 mover가 30 Hz로 두 client에 실제 전달되고, payload가 `Location`/`ReplicatedMovement`에 집중 | 전투·TIP·inventory traffic과 이동 baseline을 분리해 class별 bandwidth 정책을 설계할 수 있음 |
| **측정용 workload 분리** | local visual CPU harness와 replicated-movement harness를 별도로 유지 | “animation CPU가 좋다”와 “network가 좋다”를 잘못 섞어 최적화하는 판단 오류 방지 |

이는 현재 단계의 **좋은 기반**이지, 모든 규모에서 최적화가 끝났다는 주장은 아닙니다.
특히 N50은 always-relevant mover 50개를 client 2에 보내는 조건이고, custom AOI/Iris
filter, 50 real client input, NPC/Mass, 전투·VFX worst case는 아직 별도 검증 대상입니다.

→ [통합 프로파일링 결과와 해석](Docs/Architecture/ProjectJ_Profiling_Consolidated_Summary_2026-09-06.md) · [원본 trace/세부 결과](Docs/Architecture/ProjectJ_Profiling_Baseline_Results_2026-09-03.md) · [네트워크 세부 결과](Docs/Architecture/ProjectJ_Network_Baseline_Results_2026-09-04.md)

---

## 🎨 아티스트 / 디자이너 분들을 위한 협업 안내
본 프로젝트는 기획하신 다양한 비주얼 리소스와 애니메이션이 게임 속에서 실제로 구동되는 모습을 빠르게 확인하고 검증할 수 있도록 설계되었습니다. 

### 1. 다양한 키 입력 조합을 지원하는 스킬 시스템
* `Shift + 좌클릭` 등 여러 마우스와 키 입력 조합을 통해 다채로운 스킬 발동 경로를 구성할 수 있는 입력 시스템이 구현되어 있습니다. 
* 기획하시는 고유의 액션 메커니즘과 스킬 연출을 프로그램 제약 없이 다양하게 실험해 보실 수 있습니다.

### 2. 캐릭터 모델 호환성 (표준 스켈레탈 메시 규격 지원)
* 제작하시는 캐릭터 모델이 **언리얼 엔진의 표준 규격에 맞춘 스켈레탈 메시(Skeletal Mesh)**이기만 하면, 프로젝트 내의 모션 매칭 및 리타게팅 시스템을 통해 기존 애니메이션을 문제없이 적용하고 활용할 수 있습니다. 
* 새로운 형태의 아바타나 몬스터 모델링이라도 표준 뼈대 규격만 충족하면 큰 번거로움 없이 즉시 인게임에서 구동 가능합니다.

### 3. 제한 없는 자유로운 창작 환경 (캐릭터, 의상, 이펙트, 장비 등)
* 특정 콘셉트에 국한되지 않고, **다양한 클래스(직업)의 캐릭터, 독창적인 의상, 화려한 마법/전투 이펙트(Niagara), 다채로운 장비** 등 만들어보고 싶으신 리소스가 있다면 무엇이든 자유롭게 시도해 보실 수 있습니다.
* 아티스트님의 창의적인 아이디어와 개성이 담긴 작업물들이 인게임에 원활하게 적용될 수 있도록, 유연하고 확장성 있는 구조를 계속 유지해 나갈 예정입니다.

### 4. 자연스러운 캐릭터 locomotion (Motion Matching)
* 언리얼 엔진 5의 최신 애니메이션 기술인 **모션 매칭(Motion Matching)** 및 Chooser 시스템이 구현되어 있어, 캐릭터의 이동과 회전이 슬라이딩 현상 없이 자연스럽고 매끄럽게 연결됩니다.

### 5. 장착 시 비주얼이 동적으로 바뀌는 장비 시스템
* 장비를 장착할 때 동적으로 3D 메시가 교체되고, 장비 종류에 따라 대기 자세나 공격 모션 프로필이 실시간으로 전환되는 서버 권위형 시스템이 완비되어 있습니다.

---

## 🧠 기술 아키텍처의 강점 및 고도화 부분
대규모 MMORPG로의 확장성과 성능 최적화를 고려하여 설계된 주요 기술적 강점입니다.

### 1. 영속성 상태와 캐릭터 표현의 엄격한 분리 (`AProject_JPlayerState` & `UProject_JEquipmentManagerComponent`)
* **PlayerState 중심 구조**: 네트워크 불안정으로 인한 재접속이나 캐릭터 사망/리스폰 시 데이터 유실을 방지하기 위해 ASC(AbilitySystemComponent), AttributeSet, 인벤토리 및 장비 관리의 실질적 소유권을 [AProject_JPlayerState](Source/Project_J/Game/Project_JPlayerState.h)에 부여했습니다.
* **Avatar로서의 Character**: [AProject_JPlayerCharacter](Source/Project_JCharacter/Public/Project_JPlayerCharacter.h) 클래스는 입력 처리, 이동, 애니메이션, 비주얼 메시와 같은 렌더링 및 클라이언트 표현(Avatar) 역할만 전담하여 아키텍처의 안정성을 확보했습니다.
* **NPC와의 전략 분리**: `PlayerState`가 없는 일반 NPC들의 경우, `Character` 클래스 내부에 자체적인 `EquipmentManager` 등을 소유 기반으로 동작하도록 분기 처리하여 코드 재사용성과 런타임 효율성을 모두 챙겼습니다.

### 2. 멀티플레이어 환경을 고려한 모션 매칭 최적화 (`UProject_JMotionMatchingTrajectoryComponent`)
* **원격 캐릭터(Simulated Proxy) 궤적 보정**: 로컬 예측 위주인 모션 매칭의 한계를 극복하기 위해, [UProject_JMotionMatchingTrajectoryComponent](Source/Project_JCharacter/Public/Animation/Project_JMotionMatchingTrajectoryComponent.h)에서 복제된 이동 정보(Replicated Movement)와 시각적 부드러움(Visual Smoothing)을 기반으로 원격 플레이어의 이동 궤적을 보정 및 복원하는 `RepairRemoteTrajectoryFacing` 메커니즘을 구축했습니다.
* **거리별 애니메이션 버젯팅**: 캐릭터의 거리 단계(Near/Mid/Far/Hidden)에 따라 연산 주기를 조절하여 대규모 멀티플레이 상황에서도 CPU 오버헤드를 최소화하도록 구조화했습니다.

### 3. 분산 세션 확장을 고려한 스레드 안전 핸드오버 직렬화 (`UProject_JHandoverManager`)
* **심리스 서버 이동 준비**: 향후 메가서버나 분산 세션 게이트웨이 환경으로의 매끄러운 캐릭터 이동을 위해, 유저 상태 데이터를 스레드 안전(Thread-Safe)하고 가비지 컬렉터(GC)에 안전하게 직렬화/역직렬화(Round-trip)하는 [UProject_JHandoverManager](Source/Project_J/Backend/Project_JHandoverManager.h) 처리 구조를 마련했습니다.

### 4. 의존성 격리를 위한 단방향 모듈화 구조
* **Core ➡️ GAS ➡️ Character ➡️ Game Logic** 순서의 엄격한 단방향 의존성 규칙을 적용하여 코드 간 커플링을 방지했습니다. 이로 인해 특정 모듈의 변경이 다른 핵심 모듈의 손상으로 이어지지 않아 안정적인 확장이 가능합니다.

---

## ⚡ 성능 최적화 및 안정성 설계 (Performance & Reliability)
대규모 멀티플레이 환경과 견고한 런타임 예외 처리를 목표로 구현한 핵심 최적화 기술입니다.

### 1. 렌더링 및 애니메이션 연산 최적화 (Modular Mesh & Leader Pose)
* **모듈러 메시(Modular Mesh)** 시스템을 도입하여 장비 탈착 시 Main Mesh의 Bone 트랙 동작에 맞춰 부위별 파츠 메시들이 동적으로 결합(Leader Pose Component 방식)되도록 설정했습니다. 이로 인해 애니메이션 연산(Evaluation)의 중복 계산을 원천 차단하여 캐릭터 스킨 구성 시의 CPU 비용을 크게 낮췄습니다.

### 2. 네트워크 패킷량 최적화 (`FFastArraySerializer` & Distance Relevance)
* **변경분 전송 기반**: 인벤토리 및 장착 중인 아이템 데이터는 [FFastArraySerializer](Source/Project_JCharacter/Public/Components/Project_JEquipmentManagerComponent.h) 기반으로 모델링했으며, Iris runtime activation과 초기 state 전달을 확인했습니다. 실제 add/remove/equip delta bandwidth는 기능 확장 시 별도 측정합니다.
* **독립형 네트워크 정책 초안**: 거리(Distance)와 전투 상태(Combat Tag)를 입력으로 relevance/priority를 계산하는 정책 helper를 마련했습니다. 현재는 live Iris filter/prioritizer로 연결하지 않았으며, AOI 기준선이 필요한 시점에 connection별 정책으로 적용·측정합니다.

### 3. 리소스 로딩 예외 처리 (Async Asset Load Safety)
* 장비 아이템의 3D 에셋을 비동기(Asynchronous)로 로딩하는 동안, 로딩이 완료되기 전에 장비를 해제하는 빠른 입력 레이스 컨디션(Race Condition)이 생길 수 있습니다. 이를 예방하기 위해 인스턴스 고유 ID와 비주얼 요청 ID를 상호 매칭하여 검증하는 방어 코드를 적용해 에셋의 좀비 장착이나 크래시를 방지했습니다.

### 4. 이벤트 기반 URO 제어 (`Skeletal Mesh URO Guard`)
* 원격 캐릭터의 이동 및 회전 애니메이션 프레임을 생략하여 최적화하는 URO(Update Rate Optimization) 기능을 상시 켜두면서도, **원격 점프 신호가 올 때만 순간적으로 URO 예외 처리(프레임 강제 갱신)**를 수행해 모션 전송 지연을 지연 없이 매끄럽게 잡았습니다.

### 5. 데이터 안정성 자가 검증 (Global Validation System)
* 에디터 시작(PIE) 및 빌드 빌딩 단계에서 모션 매칭용 애니메이션 에셋(Pose Search Database)의 유효성, 잘못 설정된 속도 스레숄드값, 장비 데이터 정의(`IsDataValid`) 등을 자동으로 에러 및 워닝으로 검출하는 검증 시스템이 내장되어 있습니다. 작업 과정에서 발생할 수 있는 데이터 휴먼 에러를 즉각 예방합니다.

### 6. Mass Entity 기반 대규모 개체 최적화 초안 마련
* 추후 대량의 몬스터나 NPC 무리가 스폰되는 상황에 대비해, 개별 캐릭터 클래스 대신 가벼운 Mass Entity 프레임워크를 기반으로 스탯 조각(Stat Fragment)을 할당하고 개체를 대량 생산할 수 있는 Mass Spawner 최적화 기반이 선제 적용되어 있습니다.

---

## 🛠️ 주요 기술 스택 및 구현 현황
* **Engine Version**: Unreal Engine 5.8
* **Locomotion**: [LocomotionAnimStateComponent](Source/Project_JCharacter/) 및 C++ Motion Matching 기반 이동 제어
* **Combat**: GAS(Gameplay Ability System) 기반 어빌리티, 속성 세트, 콤보 메커니즘
* **Network**: 서버 권위형(Server-Authoritative) 판정 및 FastArray 기반 인벤토리/장비 복제
* **Handover**: 분산 서버 환경을 고려한 캐릭터 데이터 직렬화 및 심리스 레벨 이동 바인딩

---

## 🚀 개발 로드맵 (Roadmap)
* [ ] 🏇 **탈것(Mount) 시스템**: 이동의 편의성과 다양성을 더할 탈것 승하차 및 전용 locomotion 구현
* [ ] ⚔️ **전투 시스템 고도화**: 공중 콤보, 회피/패링 판정 및 피격 반응 애니메이션 다양화
* [ ] 몬스터 AI 및 보스전 프로토타이핑
* [ ] UI/UX 시스템 스킨 리뉴얼

---

## 📁 관련 상세 문서
* 더 자세한 시스템 구조와 정책은 [Docs/README.md](Docs/README.md)에서 확인하실 수 있습니다.
