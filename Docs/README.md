# Project J Docs

Project J의 C++ 구조, MMORPG 확장 방향, 애니메이션/전투 아키텍처, 네트워크 최적화 기준을 정리하는 문서 모음입니다.

## Latest Architecture Updates

- UE 템플릿 Variant C++ 코드(`Variant_Combat`, `Variant_Platforming`, `Variant_SideScrolling`)를 제거하고 본편 런타임 모듈 중심으로 정리했습니다.
- `Project_J.Build.cs`에서 Variant include path와 샘플 전용 의존성을 제거했습니다.
- `FProject_JRequestId`, `FProject_JIdempotencyKey`, `FProject_JTransactionId`, `FProject_JItemInstanceId`를 Core에 추가해 backend/economy contract ID 기반을 마련했습니다.
- `FProject_JBackendRequestContext`, `FProject_JBackendResponseEnvelope`로 backend request/response observability와 retry 판단을 분리했습니다.
- `FProject_JCombatMovementPolicy`가 combat-driven sprint, jump, ground start, overlay, rotation, intro interruption 결정을 담당합니다.
- `UProject_JCharacterAnimInstance`는 Near/Mid/Far/Hidden motion matching budget을 `FProject_JAnimationBudgetSettings`에서 해석합니다.
- `UProject_JMotionMatchingAssetSet`은 Run/Sprint/Jump/Fall/Landing PSD 슬롯 누락을 PIE 시작 시 경고합니다.
- `Project_J::AnimationProfileValidation`은 locomotion, weapon, combat profile과 Start timing override 튜닝 실수를 경고합니다.
- Ground Start timing은 공통값을 기본으로 유지하면서 LocalRun/LocalSprint/RemoteRun/RemoteSprint override를 지원합니다.
- `DumpMMOProfilingSnapshot [MaxDetailedCharacters]`는 PIE에서 player/NPC count, network role, animation budget tier, chooser policy, replication relevance 샘플을 출력합니다.
- `AProject_JNPCCharacter`는 NPC용 저비용 net/update/animation ticking 기본 정책을 적용합니다.
- `FProject_JReplicationPolicySettings`는 distance filter 설정을 중앙화해 Iris/RepGraph 연결 전 단계의 정책 기준을 제공합니다.

## Documents

- [Deferred MMORPG Systems](DeferredMMORPGSystems.md)
  - 지금 당장 구현하지 않을 MMORPG 시스템과, 나중에 붙일 수 있도록 유지해야 하는 최소 계약을 정리합니다.
- [Motion Matching Next Steps](MotionMatchingNextSteps.md)
  - C++ Motion Matching, animation budget, Significance/URO 기반 최적화, remote proxy 검증 기준을 정리합니다.
- [Combat Animation Architecture Notes](CombatAnimationArchitectureNotes.md)
  - Locomotion과 Combat의 책임 경계, 무기/공격/회피/피격 애니메이션 확장 방향을 기록합니다.

## Current Structure Policy

- `PlayerState`는 공개 가능한 플레이어 메타데이터만 보관합니다.
- `GameState`는 모든 클라이언트가 알아야 하는 public world/zone/instance state만 보관합니다.
- 캐릭터 본체는 입력, 전투, 애니메이션, UI, 복제 정책이 직접 섞이지 않도록 component와 policy로 분리합니다.
- 복제 최적화는 actor별 비용을 측정하고 distance/combat/party/event relevance reason을 명시하는 방향으로 확장합니다.
- Motion Matching 최적화는 Near/Mid/Far/Hidden budget tier를 기준으로 측정 후 조정합니다.
- remote proxy animation은 motion matching 의미를 바꾸지 않고 replicated event counter와 C++ state interpretation을 유지합니다.

## Useful PIE Commands

- `DumpMMOState`
- `DumpAnimBudget`
- `DumpReplicationPolicy`
- `DumpCharacterComponents`
- `DumpCombatState`
- `DumpMMOProfilingSnapshot [MaxDetailedCharacters]`
