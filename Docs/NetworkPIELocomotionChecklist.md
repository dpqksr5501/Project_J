# Network PIE Locomotion Checklist

MMORPG 로코모션 변경 후 Listen Server와 Client에서 반복 확인하기 위한 체크리스트입니다.

## 기본 설정

- PIE Net Mode: Listen Server
- Number of Players: 2 이상
- 각 창에서 로컬 플레이어와 원격 플레이어가 모두 보이도록 배치
- 필요하면 `GetDebugSummary`와 `GetAnimationDebugSummary`를 임시 `Print String` 또는 디버그 위젯에 연결

## 로컬 플레이어

- Idle에서 WASD 입력 시 `Start -> Locomotion`으로 전이되는지 확인
- Idle에서 Sprint 입력 시 `Sprint_Start -> Sprint Locomotion`으로 전이되는지 확인
- `Sprint_Start` 중 WASD 방향을 빠르게 바꿔도 Start에 머무르지 않는지 확인
- `Sprint_Start` 중 마우스를 빠르게 회전해도 Start에 머무르지 않는지 확인
- WASD 방향 급변 시 `SharpTurn`이 true가 되는지 확인
- 마우스 회전만으로는 `SharpTurn`이 true가 되지 않는지 확인
- 이동 입력 해제 시 `Stop -> Idle` 또는 `Locomotion -> Idle` 흐름이 자연스러운지 확인

## 점프와 착지

- Idle 점프: `Jump_Start -> Fall/FallLoop -> Land -> Idle`
- 이동 점프: `Jump_Start -> Fall/FallLoop -> Moving Land -> Locomotion`
- Sprint 점프: Sprint 상태가 착지 선택에 반영되는지 확인
- 착지 직후 다시 점프해도 `Jump_Start`와 `Land`가 비정상적으로 겹치지 않는지 확인
- 희귀 칼타이밍 이슈가 의심되면 `JumpDebug IgnoredLandings` 값이 증가했는지 확인
- 반복 테스트 전 필요하면 `ResetJumpStartLandingDebugState`로 점프 계측값 초기화

## 원격 플레이어

- Client에서 움직인 캐릭터가 Server 창에서 `Start -> Locomotion`으로 보이는지 확인
- Client에서 멈춘 캐릭터가 Server 창에서 `Stop -> Idle`로 보이는지 확인
- Client Sprint가 Server 창에서 Sprint Start/Locomotion으로 보이는지 확인
- Client 점프/착지가 Server 창에서 Jump/Fall/Land로 보이는지 확인
- 원격 플레이어가 짧은 착지 구간을 놓쳐서 공중 상태에 머무르지 않는지 확인
- 원격 Stop 직후 잔여 속도로 가짜 Start가 반복되지 않는지 확인

## Chooser / Motion Matching

- Idle PSD가 Idle 상태에서 선택되는지 확인
- Run/Sprint Start 행이 Start 요청 시 선택되는지 확인
- Run/Sprint Locomotion 행이 Locomotion 상태에서 선택되는지 확인
- Run/Sprint Stop 행이 Stop 요청 시 선택되는지 확인
- Jump_Start, FallOff/FallLoop, Light/Heavy Land 행이 의도대로 선택되는지 확인
- 멀리 있는 원격 캐릭터가 Far 최적화 행으로 단순화되어도 큰 포즈 튐이 없는지 확인

## 전투 모드 준비 상태

- Combat Mode만 켠 상태에서는 현재 정책상 점프가 가능해야 합니다.
- `Attack`, `Dodge`, `HitReact` 상태가 true인 경우 `JumpAllowed=false`가 되어야 합니다.
- 전투 구현 전에는 위 상태를 실제 액션 대신 임시 디버그 값이나 태그로 확인해도 됩니다.

## 이상 징후 기록

문제가 재현되면 아래 값을 같이 기록합니다.

- 로컬/원격 여부
- Net Mode, Local Role, Remote Role
- `GroundMotionMode`
- `bHasMoveInput`, `MoveInputSize`, `MoveInputTurnAngle`, `SharpTurn`
- `SprintAllowed`, `JumpAllowed`, `Combat`, `Attack`, `Dodge`, `HitReact`
- `bIsInAir`, `bIsJumping`, `bIsLanding`, `LastFallSpeed`
- `JumpDebug IgnoredLandings`, 마지막 ignored landing 속도 값
