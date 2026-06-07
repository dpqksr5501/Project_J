# MMORPG Architecture Audit & Optimization Log

## 1. 점검 개요
대규모 심리스 MMORPG 환경을 대비하여 프로젝트의 핵심 시스템(모션 매칭 네트워킹, 캐릭터 Tick, 네트워크 동기화, 서버 핸드오버, 코어 게임 규칙 클래스들)에 대한 심층 구조 점검 및 최적화를 진행했습니다.

## 2. 긍정적인 코어 설계 및 신규 반영 사항

### [모션 매칭(Motion Matching) 네트워크 & 궤적 최적화] - *신규 적용*
이전 작업에서 모션 매칭의 대규모 멀티플레이 최적화를 위해 다음과 같은 구조 개선을 진행했습니다.
* **Tick-Free 궤적 업데이트**: `Project_JMotionMatchingTrajectoryComponent`의 자체 컴포넌트 틱(`bCanEverTick`)을 끄고, 궤적 업데이트를 `UProject_JCharacterAnimInstance::NativeUpdateAnimation`으로 이관했습니다. 이를 통해 언리얼 엔진의 URO(Update Rate Optimization)와 Significance Manager의 혜택을 온전히 받아 멀리 있는 캐릭터의 연산량을 크게 줄였습니다.
* **Local-Space EMA 스무딩 (네트워크 지터 대응)**: Simulated Proxy(타 플레이어)가 네트워크 지연이나 스냅핑으로 인해 뚝뚝 끊기며 이동하더라도, 로컬 스페이스 기반의 EMA(지수 이동 평균) 필터링을 적용하여 시각적으로 부드럽고 자연스러운 모션 매칭 궤적을 유지하도록 최적화했습니다.
* **MaxWalkSpeed 히스토리 스케일링**: 버프/디버프 등으로 캐릭터의 이동 속도가 급변할 때 발생하는 애니메이션 팝핑(Popping) 현상을 방지하기 위해 궤적 히스토리 스케일링을 구현했습니다.

### [Tick & 동기화 구조]
* **Tick-Free & Significance Manager**: `AProject_JBaseCharacter`에서 Tick을 완전히 끄고, 거리에 따라 Tick 주기를 조절하는 구조가 이미 완성되어 있습니다.
* **FastArraySerializer 기반 데이터 동기화**: 인벤토리(`FProject_JInventoryArray`)와 장비(`FProject_JEquipmentArray`) 시스템이 변경점(Delta)만 복제하도록 최적화되어 있습니다.

### [코어 클래스의 완벽한 경량화 (Thin Data)]
* `AProject_JPlayerState`에는 타인에게 노출할 최소한의 정보(`AccountId`, `PublicClassId`, `Level`)만 담아 브로드캐스트 부하를 차단했습니다.
* `AProject_JGameState` 역시 월드 인스턴스와 이벤트 정보만 가볍게 들고 있습니다.

## 3. 구조적 보완 및 코드 반영 내용 (최적화 완료)
### [NPC의 GAS 네트워크 복제 최적화 (Minimal Mode)]
* **이슈**: 기본 캐릭터가 `Mixed` 복제 모드를 사용하여, 자칫 NPC 몬스터의 자잘한 이펙트 연산 데이터까지 클라이언트들에게 무의미하게 전송될 위험이 있었습니다.
* **해결**: `Project_JNPCCharacter.cpp` 생성자에 코드를 추가하여 복제 모드를 `Minimal`로 강제 변경했습니다. 이로써 몬스터 밀집 구역에서 불필요한 패킷 전송을 완전히 차단했습니다.

## 4. 향후 과제 (극초기 단계라 보류된 내용)
* **심리스 서버 핸드오버(Seamless Server Handover) 직렬화 구현**: 
  * `UProject_JHandoverManager`와 `IProject_JHandoverSerializable` 뼈대는 잘 잡혀있으나, 실제로 캐릭터 데이터를 바이트화(Serialize)해서 옆 서버로 넘기는 구현부는 비어있습니다.
  * 개발 극초기 단계이므로, 향후 서버 이동 로직이 구체화될 때 해당 인터페이스를 상속받아 구현하기로 결정했습니다.
