# GASP Pivot 아키텍처 정정 (2026-08-24)

> 이 문서는 이전 GASP 관련 handoff/worklog의 Pivot 설명 중 현재 확인된
> `SandboxCharacter_CMC_ABP` 화면과 충돌하는 부분을 정정한다. 충돌 시 이 문서와
> 실제 ABP/Chooser 화면을 우선한다. 과거 문서는 당시의 Project_J 계획과 실험 기록으로만
> 보존한다.

## 확인된 GASP 경로는 둘이다

GASP는 Pivot을 처리하는 단일 경로가 아니다. `Use Experimental State Machine`에 따라
`IsPivoting`이 아래 둘 중 하나를 선택하고, 둘의 결과를 섞어 해석하면 안 된다.

```text
Use Experimental State Machine = false
  -> regular Motion Matching 경로
  -> Update_MotionMatching
  -> CHT_PoseSearchDatabases가 PSD 배열 선택
  -> MM node가 지속 검색/선택

Use Experimental State Machine = true
  -> Experimental State Controller 경로
  -> State-entry가 SetBlendStackAnimFromChooser 호출
  -> Chooser가 후보/metadata/UseMM을 결정
  -> UseMM=true면 그 후보들에 대해 한 번 Motion Match
  -> 결과를 단일 Blend Stack 입력으로 설정
```

따라서 “GASP는 Pivot을 direct sequence로만 재생한다”, “GASP Pivot은 regular PSD만
쓴다”, 또는 “GASP의 State Controller가 포즈를 직접 출력한다”는 모두 불완전한 설명이다.
State Controller는 논리 상태를 관리하며, 실제 포즈는 Blend Stack 또는 regular MM node가
출력한다.

## regular MM에서 확인된 Pivot 보호

`IsStarting`은 다음 의미를 가진다.

```text
IsMoving
AND |Trajectory Future Velocity| >= |Velocity| + 100
AND NOT CurrentDatabaseTags contains "Pivots"
```

여기서 `Pivots`는 **regular MM PSD/database 태그**다. Pivot PSD가 현재 선택되었을 때
후반부가 Start처럼 보인다고 Start 판정이 끼어들지 않게 하는 보호 장치다. 이는 one-shot
Blend Stack hold를 조정하는 규칙이 아니다.

`MM Pivot Conditions`는 `abs(GetTrajectoryTurnAngle)`을 Rotation Mode별 값과 비교한다.
화면상 OTM=45°, Strafe=30°, Aim=0°였지만, 이는 GASP의 넓은 Pivot/turn 자산 범위를 위한
조건이다. Run Pivot만 가진 Project_J에 그대로 복사할 값이 아니며, Project_J는 Combat
Strafe Run의 강한 실제 반전(대략 135~180°)으로 별도 제한해야 한다.

## Experimental State Machine에서 확인된 역할

`SetBlendStackAnimFromChooser`는 state entry에서 다음을 수행한다.

1. State Machine State를 설정하고 이전 Blend Stack 입력을 보관한다.
2. chooser를 평가해 animation, start time, loop, blend time/profile, tags, `UseMM`, cost limit을 받는다.
3. `UseMM=true`면 chooser가 낸 후보를 대상으로 **single-frame Motion Match**를 수행하고,
   선택 asset/entry time으로 Blend Stack 입력을 덮어쓴다.
4. 비루프 애셋의 재진입이 필요하면 `Force Blend`로 같은 asset에도 새 blend를 명시적으로 요청한다.

`OnStateEntry_TransitionToLocomotion`은 Target Rotation을 저장하고 `Force Blend=true`로
위 함수를 호출한다. `OnUpdate_TransitionToLocomotion`의 `RInterpTo`는 rotational
Start/Pivot의 break/re-entry 판정용 Target Rotation 갱신이지 Pivot asset 선택이나 재생
시간을 보정하는 장치가 아니다.

`IsAnimationAlmostComplete`는 현재 Blend Stack의 **비루프** asset remaining time이 0.75초
이하인지 보는 GASP의 transition helper다. 0.75초는 GASP 전환 애셋의 긴 tail에 맞춘 값이며
Project_J의 공용 상수로 복사하면 안 된다.

## 태그 이름을 혼동하지 말 것

| 위치 | 확인된 태그 | 용도 |
| --- | --- | --- |
| regular MM PSD/database | `Pivots` | 현재 MM database가 Pivot인지 알아 Start를 억제 |
| Experimental SM chooser output | `Pivot` | Blend Stack 출력/상태 재진입 조건을 식별 |

복수형과 단수형은 화면상 서로 다른 경로에서 확인된 값이다. Project_J가 둘 중 하나를
도입하더라도 같은 의미라고 가정해 interchange하지 않는다.

## Project_J에 대한 현재 결론

Project_J의 현 단일 direct Blend Stack은 Start/Stop/Pivot의 동시 hold·재진입 충돌을 이미
보였다. 그러므로 **Combat Strafe Run Pivot은 아직 direct State Controller row를 활성화하지
않는다.** 현재 Pivot chooser 행은 비활성 유지가 맞다.

후속 구현 후보 중 우선 검토 대상은 regular MM 경로다.

```text
C++의 Combat-Strafe Run 강한-반전 semantic gate
  -> MM database chooser
     -> Cycle PSD 또는 Pivot 전용 PSD
  -> current PSD tag "Pivots" 동안 Start 억제
  -> regular MM이 Pivot 종료 후 Cycle로 복귀
```

이는 GASP experimental state machine을 복제하는 결정이 아니며, Project_J의 기존 regular
MM 소유권 안에서 Pivot을 고립시키는 선택이다. Start/Stop direct one-shot, TIP, Jump/Fall/Land,
Montage는 별도 정책으로 유지한다. 실제 도입 전에는 current database tags, pivot PSD 선택,
MM interrupt/continuing-pose, remote semantic 정책을 코드와 에셋에서 다시 검증한다.

## 문서 해석 규칙

- `CombatStrafe_Implementation_2026-08-04.md`, `GASP_BranchIn_TIP_Handoff_2026-08-04.md`,
  `MovingReorientation_TIP_WorkLog_2026-08-05.md`의 moving Pivot direct/`UseMM=false` 서술은
  **historical proposal**이며 현재 확정 설계가 아니다.
- Idle TIP의 direct one-shot 설계와 moving Combat Strafe Run Pivot은 별개다. 이 정정은
  Idle TIP를 PSD로 옮긴다는 뜻이 아니다.
- Project_J의 현재 코드와 에디터 에셋 연결이 최종 사실이며, GASP 화면은 참조 구현일 뿐
  그대로 복사할 계약이 아니다.
