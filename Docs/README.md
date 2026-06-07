# Project J Docs

Project J의 C++ 구조, MMORPG 확장 방향, 모션 매칭, 전투 애니메이션, 네트워크 최적화 기준을 정리하는 문서 모음입니다.

## Documents

- [MMORPG Architecture Review](MMORPGArchitectureReview.md)
  - 전체 C++ 구조를 기준으로 모듈 경계, 캐릭터/GAS, 장비/인벤토리 기반, 네트워크, 백엔드, UI, Mass, 모션 매칭의 작업 우선순위를 정리합니다.
- [Motion Matching Next Steps](MotionMatchingNextSteps.md)
  - C++ Motion Matching, animation budget, remote proxy 검증, Significance/URO 기반 최적화 후보를 정리합니다.
- [Deferred MMORPG Systems](DeferredMMORPGSystems.md)
  - 지금 당장 구현하지 않아도 되지만, 나중에 붙일 때 흔들리지 않도록 유지해야 하는 MMORPG 시스템 경계를 정리합니다.
- [Combat Animation Architecture Notes](CombatAnimationArchitectureNotes.md)
  - Locomotion과 Combat의 책임 경계, 무기/공격/회피/피격 애니메이션 확장 방향을 기록합니다.

## Current Structure Policy

- 플레이어급 Motion Matching은 플레이어 캐릭터와 가까운 원격 플레이어 중심으로 유지합니다.
- NPC는 현재 단계에서 Motion Matching을 사용하지 않는 것을 기본 전제로 둡니다.
- `CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet` 순서로 애니메이션 설정을 해석합니다.
- `LocomotionAnimStateComponent`는 이동 상태를 결정하고, `AnimInstance`는 thread-safe snapshot을 Chooser/Motion Matching에 전달합니다.
- 원격 프록시는 로컬 입력이 없으므로 replicated movement와 visual smoothing 기준으로 별도 정책을 적용합니다.
- 대규모 MMORPG 최적화는 Near/Mid/Far/Hidden animation budget tier를 기준으로 측정 후 조정합니다.
- 복제 최적화는 distance, owner, combat, party/guild, public event relevance reason을 분리하는 방향으로 확장합니다.

## Useful PIE Commands

- `DumpMMOState`
- `DumpAnimBudget`
- `DumpReplicationPolicy`
- `DumpCharacterComponents`
- `DumpCombatState`
- `DumpMMOProfilingSnapshot [MaxDetailedCharacters]`
