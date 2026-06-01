# Combat Animation Architecture Notes

전투와 무기 애니메이션을 추가하기 전, Locomotion과 Combat의 책임 경계를 유지하기 위한 메모입니다.

## 현재 기준

- Locomotion은 이동 상태 해석을 담당합니다.
- Combat은 공격, 회피, 피격, 무기 장착 상태를 담당합니다.
- AnimInstance는 두 상태를 읽어 AnimGraph와 Chooser에 전달합니다.
- Motion Matching은 기본 Locomotion에 사용하고, 전투 액션 자체는 별도 몽타주/상태 레이어로 확장합니다.

## Locomotion이 계속 가져야 할 책임

- Idle, Start, Locomotion, Stop 상태 판단
- Jump_Start, FallOff, Land 전이 판단
- 로컬 입력과 원격 복제 이벤트를 애니메이션 상태로 해석
- Sprint 사용 여부와 Start/Stop 선택에 필요한 이동 상태 제공
- Chooser Table이 읽는 이동/공중/착지 변수 제공

## Combat이 가져야 할 책임

- 무기 장착/해제
- 공격 시작/종료
- 회피 시작/종료
- 피격 반응 시작/종료
- 공격/회피/피격 중 점프, 스프린트, 이동 회전 정책 결정
- 전투용 AimOffset, Upper Body Slot, Montage 재생 정책 결정

## 정책 함수

전투 상태가 Locomotion 내부 조건식에 직접 섞이지 않도록, 캐릭터 정책 함수를 통해 제어합니다.

- `IsSprintLocomotionAllowed`
- `IsJumpLocomotionAllowed`
- 이후 필요 시 `IsGroundStartAllowed`, `IsGroundStopAllowed`, `IsCombatLocomotionOverlayAllowed` 같은 이름으로 확장

정책 함수는 입력 처리와 애니메이션 상태 컴포넌트 양쪽에서 같은 기준으로 호출할 수 있어야 합니다.

## 추천 구현 순서

1. 무기 장착/해제 상태 확정
2. 공격 몽타주 재생과 종료 이벤트 연결
3. 공격 중 Sprint/Jump 정책 확인
4. 회피 액션 추가
5. 피격 반응 추가
6. Combat Anim Profile에 무기별 속도, 회전, AimOffset, Montage 설정 이관
7. 필요할 때만 Combat 전용 Locomotion Overlay 추가

## 피해야 할 방향

- 공격/회피/피격 조건을 `LocomotionAnimStateComponent` 내부의 Start, Stop, Jump, Land 조건식에 직접 흩뿌리지 않습니다.
- Chooser Table 변수 이름을 전투 구현 중 자주 바꾸지 않습니다.
- 전투 모드만 켰다는 이유로 기본 이동 상태를 완전히 새로 만들지 않습니다.
- 실제 무기/애니메이션이 생기기 전에 과도한 추상화 계층을 만들지 않습니다.

## 에디터 확인 기준

- Combat Mode만 켠 상태에서는 기본 Locomotion이 계속 정상 동작해야 합니다.
- 공격/회피/피격 중에는 정책 함수 기준으로 Jump/Sprint 제한 여부가 명확해야 합니다.
- 상체 공격 몽타주가 하체 Locomotion을 불필요하게 끊지 않아야 합니다.
- 원격 캐릭터에서도 공격 상태와 이동 상태가 서로 다른 타이밍으로 튀지 않아야 합니다.
