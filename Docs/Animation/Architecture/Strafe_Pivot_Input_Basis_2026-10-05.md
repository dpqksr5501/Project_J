# Strafe Pivot의 이동 입력 기준 통일 — 2026-10-05

> **후속 로그에서 추가 원인 확인.** 실제 PIE는 새 Pivot 감지가 성공해도 이전 Pivot의 입력 redirect 정책이 새 요청을 억제했음을 보여준다. 같은 착지 epoch의 모드 변경도 중복 재생을 만들었다. 최신 수정과 검증은 [일회성 동작의 요청 수명](OneShot_Command_Lifetime_2026-10-05.md)을 따른다. 아래는 이전 입력 기준 수정의 역사적 기록이며 시각적 해결 증거가 아니다.

## 증상과 확인한 결함

사용자는 Combat Strafe에서 좌→우와 앞→뒤만 Pivot이 보이고, 우→좌와 뒤→앞은 기존처럼 나오지 않는다고 보고했다. 대각선 지원 요청은 아니다.

Pivot 선택은 locomotion component의 확정된 이동 의도와 revision을 사용한다. 그러나 `NativeUpdateAnimation`의 공통 one-shot 취소 경로는 `GetLastMovementInputVector()`의 방향을 별도로 비교했다. 이 값은 직전 CMC 입력이거나 반대 키가 겹친 aggregate IA_Move 값일 수 있다. 확정된 이동 의도가 그대로여도 두 입력 기준이 다르면 이미 선택된 Pivot을 취소할 수 있다.

기존 취소 블록을 그대로 추출한 상태에서 `ProjectJ.StrafePivot.RedirectBasis`를 실행했다. 확정된 이동 의도를 유지하면서 마지막 CMC 입력만 반대 방향으로 둔 네 경우가 모두 실패했다. [검증 JSON](Strafe_Pivot_Input_Basis_Validation_2026-10-05.json)에 재현 report와 최종 report를 기록한다. 이는 소스의 취소 충돌 재현이며 사용자의 PIE 입력 순서와 시각적 증상을 그대로 재현한 기록은 아니다.

## 변경

확정된 Pivot의 이동 입력 취소는 기존 `FProject_JStateControllerRuntime::ReconcilePivot`이 비교하는 `MoveIntentRevision` 하나로 처리한다. 이후 새로운 이동 의도는 여전히 Pivot을 중단하고 Cycle MM으로 복귀한다. 같은 의도를 유지하는 동안 aggregate/직전 CMC 입력이 달라져도 다시 취소하지 않는다.

Pivot 확정 때는 자체 카메라 yaw만 저장한다. Start/Land의 raw 이동 입력 각도 임계값과 Pivot의 마우스 회전 취소는 유지한다. Stop·새 요청 억제·착지·전신 동작·탑승 경계의 기존 수명 처리와, [MM 복귀 검색 및 군중 예산](MotionMatching_Return_Crowd_2026-10-05.md)은 유지했다. 추가 검색 루프나 tick, manager, per-frame 할당은 없다.

ABP·Chooser·PSS·입력 에셋을 수정하거나 저장하지 않았다. 대각선 제한과 기존 Pivot 최소 속도/반전 각도 정책도 변경하지 않았다.

## 실제 Chooser 확인

`ProjectJ.StrafePivot.CardinalSelection`은 실제 `/Game/Animation_Logic/Chooser/CHT_Player_StateControllerAnimations`를 읽고 기존 C++ 선택 경로를 실행한다. 네 반전 × 좌/우 발 × actor/control yaw 0·90·179도, 총 24개 조합에서 반전 수락, 이전/목표 방향, 유효 asset, Pivot commit과 외부 Blend Stack override를 확인했다.

| 반전 | 실제 선택 asset family (좌/우 발 각각) |
| --- | --- |
| 좌→우 | `M_Neutral_Run_Pivot_LR_RR_Lfoot` / `Rfoot` |
| 우→좌 | `M_Neutral_Run_Pivot_RL_LL_Lfoot` / `Rfoot` |
| 앞→뒤 | `M_Neutral_Run_Pivot_F_B_Lfoot` / `Rfoot` |
| 뒤→앞 | `M_Neutral_Run_Pivot_B_F_Lfoot` / `Rfoot` |

테스트 속도는 500으로 기존 최소 속도 350을 충족한다. 중간 실행에서 이 fixture를 300으로 둬 12개 반전 수락 검사가 실패했고, 소스 정책을 바꾸지 않고 fixture를 교정했다. 따라서 이 결과는 최소 속도보다 느린 이동에서도 Pivot을 허용한다는 의미가 아니다.

## 검증과 남은 PIE 확인

직접 `UnrealBuildTool.exe`를 사용하며 기존 build/editor 프로세스를 중단하거나 겹쳐 실행하지 않았다. 최종 Editor/Game 빌드와 회귀 결과는 검증 JSON에 기록한다. 기존 116개와 새 2개, 총 118개 회귀는 최종 단일 실행에서 성공했다(정상 114, 경고 포함 4, 실패·미실행·실행 중 0). 신규 테스트 경고/오류는 0이다. 기존 fixture 경고 8건은 이전 report와 대조한다.

최종 회귀 report: `Saved/Automation/StrafePivot_Final_20261005/index.json`. 작업 전 소스 backup: `Saved/Validation/StrafePivot_20261005/Before/`.

새 빌드로 에디터를 열고 충분히 이동한 뒤 좌→우, 우→좌, 앞→뒤, 뒤→앞을 각각 확인한다. 반대 키가 잠깐 겹치는 입력과 이전 키를 먼저 놓는 입력을 모두 확인한다. 선택 후 새 이동 입력/마우스 회전은 MM 복귀, 입력 유지 시에는 authored Pivot 종료가 기준이다. 자동화는 실제 ABP pose blending의 시각적 품질을 검증하지 않는다.

계속 특정 방향이 누락되면 `p.ProjectJ.MMTransitionDebug 1`로 `CombatStrafeRunPivotAccepted/Rejected`, `Committed/Cancelled`, `StateControllerChooser`를 함께 기록해 감지·최소 속도·선택·취소 중 어느 단계인지 구분한다. 확인한 취소 충돌만으로 모든 PIE 원인을 단정하지 않는다.

## 후속 재현 로그 — StrafePivotDiag (제거된 임시 진단)

사용자 정상 동작 확인 후 임시 진단 CVar·입력/상태 sample·worker node 조회를 제거했다. 아래 실행 방법과 기본값은 당시 재현 기록이며 현재 코드에서 사용할 수 없다. 수정 로직과 회귀 테스트는 유지한다.

`p.ProjectJ.StrafePivotDebug`를 추가했다. 진단 중 Editor 기본값은 1, Game 기본값은 0이다. Editor에서 `p.ProjectJ.StrafePivotDebug 0`으로 끌 수 있다. 새 기능·상태 판정·애니메이션/에셋 변경 없이 기록만 추가했다.

1. 진단 빌드로 Editor를 새로 실행하고 PIE에 들어간다. `StrafePivotDiag Stage=Session Version=20261005_Diag1`이 새 진단 코드의 실행 표식이다.
2. 같은 Combat Strafe Run 조건에서 충분히 이동한 뒤 좌→우, 우→좌, 앞→뒤, 뒤→앞을 한 번씩 시도한다. 각 시도 사이 2초 정도 두고, 안 되는 방향을 한 번 더 시도한다.
3. PIE를 끝낸다. 전체 `Saved/Logs/Project_J.log`를 사용한다. `StrafePivotDiag`만 필터링하면 기존 `CombatStrafeRunPivot...`와 `StateControllerChooser` 근거를 놓칠 수 있다.

로컬 입력과 primary AnimInstance를 중심으로 기록한다. 입력/의도/요청 경계와 그 뒤 1.5초 동안 최대 약 10Hz의 snapshot을 남긴다. Pivot 거절은 동일 의도 revision에서 사유가 바뀔 때도 남겨, 임시 입력·최소 속도·이미 소비된 요청을 구분한다.

| 기록 | 확인하는 단계 |
| --- | --- |
| `StrafePivotDiag Stage=Bindings` | Move와 네 semantic action 경로 및 완전한 바인딩 여부 |
| `Stage=Key`, `RawInput`, `BeginChord`, `ResolvedIntent` | 반대 키 이벤트, raw/semantic 차이, callback 순서와 CMC 속도 capture |
| `CombatStrafeRunPivotAccepted/Rejected` | 최소 속도·반전 각도·Run/Strafe·공중 조건·요청 소비 등 |
| `StateControllerChooser`, `Committed/NotCommitted/Cancelled/Released` | 방향·발·에셋 선택과 one-shot 수명 |
| `StrafePivotDiag Stage=Snapshot` | 원래/최종 phase, override, force-blend, committed/suppressed revision, 재생 시간 |
| `Stage=Worker`, `ExternalStack`, `StackPlayer`, `MMNode` | 실제 graph update, 외부 Stack의 asset/time/weight, MM 결과와 pending 검색 |

worker 조회는 native node 상태를 읽기만 한다. 캐시된 node weight와 stack player weight는 최종 렌더 pose의 기여를 단독으로 보증하지 않는다. 로그가 selected asset과 실제 Stack의 불일치까지 확인하면, 그때 실제 ABP callback/branch를 추가로 좁혀 본다.
