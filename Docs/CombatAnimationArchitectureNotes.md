# Combat Animation Architecture Notes

이 문서는 Locomotion과 Combat의 책임 경계를 유지하기 위한 메모입니다. MMORPG 캐릭터는 직업, 무기, 스킬, 상태이상이 계속 늘어나므로 combat 조건이 locomotion 내부에 직접 섞이지 않도록 관리해야 합니다.

## 현재 기준

- Locomotion은 이동 상태 해석을 담당합니다.
- Combat은 무기 장착, 공격, 회피, 피격, combat montage와 overlay 정책을 담당합니다.
- AnimInstance는 두 상태를 읽어 AnimGraph와 Chooser에 전달합니다.
- Motion Matching은 기본 locomotion에 사용하고, combat action 자체는 montage/slot/overlay/profile로 확장합니다.

## Locomotion 책임

- Idle, Start, Locomotion, Stop 상태 판단
- JumpStart, FallOff, Landing 상태 판단
- local input과 replicated movement event를 animation-facing state로 해석
- sprint intent와 이동 상태를 Chooser에 제공
- 공중/착지/회전/방향 전환 context 제공

## Combat 책임

- 무기 장착/해제
- 공격 시작/종료
- 회피 시작/종료
- 피격 반응 시작/종료
- 공격/회피/피격 중 sprint, jump, rotation policy 결정
- combat aim offset, upper body slot, montage playback policy 결정
- combat 관련 gameplay tag 관리

## 경계 함수

Combat 상태가 Locomotion 조건문 안에 직접 섞이지 않도록 character 또는 combat policy 함수로 제어합니다.

현재 사용 중:

- `IsSprintLocomotionAllowed`
- `IsJumpLocomotionAllowed`
- `IsGroundStartAllowed`
- `IsGroundStopAllowed`
- `IsCombatLocomotionOverlayAllowed`

추가하면 좋은 함수:

- `GetCombatRotationPolicy`
- `GetCombatAimPolicy`
- `IsCombatActionBlockingMovement`

## 최근 구조 변화

- `UProject_JCombatComponent`는 GAS ability tag activation과 combat state tag 소유를 담당합니다.
- `UProject_JCharacterUIBindingComponent`가 UI ViewModel attribute binding을 담당하여 PlayerCharacter의 UI 책임을 줄였습니다.
- `UProject_JPlayerInputBindingComponent`가 EnhancedInput binding을 담당하여 입력 에셋 바인딩과 gameplay method 실행을 분리했습니다.
- `UProject_JReplicatedAnimEventComponent`가 replicated animation event counter 갱신/적용 의미를 담당합니다.
- `UProject_JNetObjectPrioritizer_Combat`가 combat gameplay tag를 replication priority로 변환합니다.

## 추천 구현 순서

1. 무기 장착/해제 상태 확정
2. 공격 montage 재생과 종료 이벤트 연결
3. 공격 중 sprint/jump/rotation policy 확인
4. 회피 action 추가
5. 피격 reaction 추가
6. CombatAnimProfile에 무기별 속도, 회전, aim offset, montage 설정 추가
7. 필요한 경우에만 combat 전용 locomotion overlay 추가

## 피해야 할 방향

- 공격/회피/피격 조건을 `LocomotionAnimStateComponent`의 start/stop/jump/land 조건문 안에 직접 흩뿌리지 않습니다.
- Chooser variable 이름을 combat 구현 중 자주 바꾸지 않습니다.
- combat mode만 켰다는 이유로 기본 locomotion을 완전히 다른 구조로 갈아타지 않습니다.
- 실제 무기/스킬 데이터가 생기기 전에 과도한 추상 계층을 만들지 않습니다.

## 에디터 확인 기준

- Combat mode만 켠 상태에서는 기본 locomotion이 정상 동작해야 합니다.
- 공격/회피/피격 중 sprint/jump 제한 여부가 policy 함수 기준으로 명확해야 합니다.
- `IsGroundStartAllowed`, `IsGroundStopAllowed`, `IsCombatLocomotionOverlayAllowed`가 locomotion 내부 조건 대신 combat boundary 역할을 해야 합니다.
- 상체 공격 montage가 하체 locomotion을 불필요하게 깨지 않아야 합니다.
- 원격 캐릭터도 공격 상태와 이동 상태가 서로 다른 타이밍으로 어긋나지 않아야 합니다.
