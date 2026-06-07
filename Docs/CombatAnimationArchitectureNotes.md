# Combat Animation Architecture Notes

이 문서는 Locomotion과 Combat의 책임 경계를 유지하기 위한 메모입니다. MMORPG 캐릭터는 직업, 무기, 스킬, 상태이상이 계속 늘어나므로 combat 조건이 locomotion 내부에 직접 섞이지 않도록 관리합니다.

## Combat Movement Policy

`FProject_JCombatMovementPolicy`는 combat state와 locomotion decision 사이의 현재 boundary입니다.

- PlayerCharacter는 replicated state와 GAS tag read를 소유합니다.
- Policy struct는 sprint, jump, ground start, overlay, combat rotation, intro interruption 결정을 plain value에서 계산합니다.
- Weapon/job-specific profile setting은 policy에 입력되고 locomotion state machine 내부로 직접 들어가지 않습니다.
- Motion Matching timing을 안정적으로 유지하면서 class/weapon combat variant를 추가할 수 있습니다.

## Responsibility Boundary

### Locomotion

- Idle, Start, Locomotion, Stop 상태 판단
- JumpStart, FallOff, Landing 상태 판단
- local input과 replicated movement event를 animation-facing state로 해석
- sprint intent와 이동 phase를 AnimInstance/Chooser에 제공
- 공중, 착지, 회전, 방향 전환 context 제공

### Combat

- 무기 장착/해제
- 공격 시작/종료
- 회피 시작/종료
- 피격 반응 시작/종료
- 공격/회피/피격 중 sprint, jump, rotation policy 결정
- combat aim offset, upper body slot, montage playback policy 결정
- combat 관련 gameplay tag 관리

## Current Boundary Functions

Combat 상태가 Locomotion 조건문 안에 직접 섞이지 않도록 character 또는 policy function으로 제어합니다.

- `IsSprintLocomotionAllowed`
- `IsJumpLocomotionAllowed`
- `IsGroundStartAllowed`
- `IsGroundStopAllowed`
- `IsCombatLocomotionOverlayAllowed`
- `ShouldUseCombatRotationMode`
- `ShouldInterruptCombatIntroOnHit`
- `GetEffectiveCombatAimAlpha`

## Current Supporting Pieces

- `UProject_JCombatComponent`: GAS ability tag activation과 combat state tag 소유를 담당합니다.
- `UProject_JCharacterUIBindingComponent`: UI ViewModel attribute binding을 담당합니다.
- `UProject_JPlayerInputBindingComponent`: EnhancedInput binding과 gameplay method 호출을 분리합니다.
- `UProject_JReplicatedAnimEventComponent`: replicated animation event counter 갱신/적용 의미를 담당합니다.
- `UProject_JNetObjectPrioritizer_Combat`: combat gameplay tag를 replication priority로 변환합니다.
- `Project_J::AnimationProfileValidation`: combat/weapon/locomotion profile 설정 실수를 PIE 시작 시 경고합니다.

## Recommended Implementation Order

1. 무기 장착/해제 상태 확정
2. 공격 montage 재생과 종료 이벤트 연결
3. 공격 중 sprint/jump/rotation policy 확인
4. 회피 action 추가
5. 피격 reaction 추가
6. CombatAnimProfile에 무기별 속도, 회전, aim offset, montage 설정 추가
7. 필요한 경우에만 combat 전용 locomotion overlay 추가

## Guardrails

- 공격/회피/피격 조건을 `LocomotionAnimStateComponent`의 start/stop/jump/land 조건문 안에 직접 섞지 않습니다.
- Chooser variable 이름을 combat 구현 중 자주 바꾸지 않습니다.
- combat mode만 켰다는 이유로 기본 locomotion이 다른 구조로 갈아타지 않습니다.
- 실제 무기/스킬 데이터가 생기기 전에 과도한 추상 계층을 만들지 않습니다.

## Editor Test Baseline

- Combat mode만 켠 상태에서도 기본 locomotion이 정상 동작해야 합니다.
- 공격/회피/피격 중 sprint/jump 제한 여부가 policy function 기준으로 명확해야 합니다.
- `IsGroundStartAllowed`, `IsGroundStopAllowed`, `IsCombatLocomotionOverlayAllowed`가 locomotion 내부 조건 대신 combat boundary 역할을 해야 합니다.
- 공격 montage가 하체 locomotion을 불필요하게 깨지 않아야 합니다.
- 원격 캐릭터도 combat state와 movement state가 서로 다른 타임라인으로 어긋나지 않아야 합니다.
