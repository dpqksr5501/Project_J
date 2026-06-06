# Project J Docs

Project J의 C++ 구조, MMORPG 확장 방향, 애니메이션/전투 아키텍처, 네트워크 최적화 기준을 정리하는 문서 모음입니다.

## 문서 목록

- [Deferred MMORPG Systems](DeferredMMORPGSystems.md)
  - 지금 당장 구현하지 않을 대형 MMORPG 시스템과, 대신 유지해야 할 최소 기반을 정리합니다.
- [Motion Matching Next Steps](MotionMatchingNextSteps.md)
  - Motion Matching, animation budget, Significance/URO 기반 최적화 작업 순서를 정리합니다.
- [Combat Animation Architecture Notes](CombatAnimationArchitectureNotes.md)
  - Locomotion과 Combat의 책임 경계를 정리하고, 무기/공격/회피/피격 애니메이션 확장 방향을 기록합니다.

## 현재 구조 원칙

- PlayerState에는 공개 가능한 플레이어 메타데이터만 둡니다.
- GameState에는 모든 클라이언트가 알아야 하는 얇은 월드/인스턴스 상태만 둡니다.
- 캐릭터 본체가 입력, 전투, 애니메이션, UI, 복제 정책을 계속 끌어안지 않도록 component와 policy 타입으로 분리합니다.
- 복제 최적화는 actor별 비용을 측정하고, distance/combat/party/event relevance 같은 이유를 명시적으로 남긴 뒤 확장합니다.
- Motion Matching 최적화는 Near/Mid/Far/Hidden budget tier를 기준으로 측정 후 조정합니다.
- 리팩터링은 모션 매칭 의미를 바꾸지 않고, 기존 gameplay method 호출 순서와 replicated event counter 의미를 보존하는 방식으로 진행합니다.

## 최근 반영된 기반

- `FProject_JReplicationPolicyDecision`으로 복제 여부, relevance reason, priority multiplier를 표현합니다.
- distance replication filter와 combat prioritizer가 실제 정책 계산 함수를 갖습니다.
- `FProject_JAnimationBudgetSettings`와 `FProject_JAnimOptimizationPolicy`로 animation budget 용어를 공유합니다.
- `UProject_JCharacterUIBindingComponent`가 캐릭터 UI ViewModel 바인딩을 담당합니다.
- `UProject_JPlayerInputBindingComponent`가 EnhancedInput binding을 담당하고 기존 캐릭터 gameplay method를 호출합니다.
- `UProject_JReplicatedAnimEventComponent`가 replicated animation event counter 갱신/적용 의미를 담당합니다.
- `DumpMMOState`, `DumpAnimBudget`, `DumpReplicationPolicy`, `DumpCharacterComponents`, `DumpCombatState` exec command로 PIE에서 상태를 확인합니다.
