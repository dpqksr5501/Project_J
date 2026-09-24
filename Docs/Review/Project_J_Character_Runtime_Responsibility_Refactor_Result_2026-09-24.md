# 캐릭터 런타임 책임 분리 결과

기준: `Character_Test` / 작업 시작 HEAD `6a198dc4100068c80979617c51fe988fc98a101d` / Unreal Engine 5.8.

## 1. 구조 결정

현재 코드와 `Docs/CombatLocomotionArchitecture.md`, `Docs/MotionMatching_StateController_Handoff_2026-08-01.md`, `Docs/Review/Project_J_Code_Audit_Result_2026-09-23.md`, `Docs/Review/Project_J_Final_Audit_Followup_Result_2026-09-23.md`를 기준으로 판단했다. 오래된 문서의 자산 저작 지침보다 현재 C++ 호출 경계를 우선했다. 새 UObject, ActorComponent, Tick, worker는 만들지 않았다. 기존 `UFUNCTION`, `UPROPERTY`, Chooser 열, 데이터 에셋, GameplayTag 계약은 유지했다.

| 구분 | 소유자 | 데이터 성격 |
| --- | --- | --- |
| 전투 게임플레이 | GAS / `CombatStateComponent` | 서버 권위의 전투 상태 원본 |
| 발도·납도 | `CombatIntroComponent` | 전환 상태의 수명 |
| 지상·공중·착지·Pivot·TIP 요청 | `LocomotionAnimStateComponent` | 이동 의미 상태의 원본 |
| Trajectory | `MotionMatchingTrajectoryComponent` | 게임 스레드에서 채집한 궤적 캐시 |
| MM 선택 리비전·방향 전환 대기 시간 | `FProject_JMotionMatchingSelectionPolicy` | 의미 선택 정책. 컴포넌트의 리플렉션 리비전은 게시용 복사본 |
| MM Chooser 평가 간격·마지막 컨텍스트 | `FProject_JMotionMatchingRuntime` | 게임 스레드의 표현용 캐시 |
| State Controller 유지 상태·Stop 소비·Landing 리비전·TIP 순번·Pivot 확정 | `FProject_JStateControllerRuntime` | 게임 스레드의 표현 상태 |
| 원격 MoveStop/TIP 이벤트 유효 기간 | `FProject_JRemoteLocomotionRuntime` | 네트워크 이벤트로 재구성한 표현 상태 |
| TIP 루트 yaw 선택·전송 대기 시간 | `FProject_JTurnInPlacePresentationRuntime` | 캐릭터 연결부의 게임 스레드 표현 상태 |
| PSD/Chooser/AnimGraph | `CharacterAnimInstance` / 애니메이션 프록시 | 엔진 연동과 리플렉션 인터페이스 |
| 무기 시각 효과 | `WeaponPresentationComponent` | 무기 표현 |
| 공격 판정/SSR | `CombatHitValidationComponent` 등 기존 판정 경로 | 서버 권위의 게임플레이 판정 |
| 탈것 | `MountComponent` | 탑승 상태의 수명 |
| 입력 | `PlayerInputBindingComponent` → `SkillInputRouterComponent` → `SkillInputExecutionComponent` | 입력 바인딩 → 의도 전달 → GAS 실행 |
| 애니메이션 이벤트 복제 | `ReplicatedAnimEventComponent`, `ReplicatedJumpStateComponent` | 네트워크 이벤트 전달. Character는 전달 연결부 |

## 2. 데이터 흐름과 스레드 경계

`Movement / GAS / replicated events → LocomotionAnimStateComponent → BuildThreadSafeData → State Controller/MM game-thread runtime → reflected Chooser mirror 및 proxy snapshot → animation worker/AnimGraph` 순서다. `FProject_JLocomotionContextBuilder`는 값만 받아 ground MM 이동 의도와 phase 우선순위를 계산한다. Remote는 복제된 이동·이벤트를 사용하며 local input을 재사용하지 않는다.

새 런타임과 selection policy는 **Game Thread 전용**이다. `FProject_JLocomotionContextBuilder`의 계산 자체는 값 기반이지만 현재 호출은 컴포넌트의 game-thread snapshot 빌드에서 이뤄진다. UObject 조회, 충돌 probe, `UAnimSequence::ExtractRootMotionFromRange`, Chooser/PoseSearch 호출, actor 회전 적용, RPC 호출은 기존 adapter에 남겼다. worker에는 기존 compact proxy snapshot만 게시한다.

## 3. 변경 범위

- AnimInstance의 MM scheduling 및 마지막 선택 컨텍스트를 `FProject_JMotionMatchingRuntime`으로 옮겼다. PSD 포인터는 GC/reflection과 AnimGraph 계약 때문에 기존 `UPROPERTY`에 남겼다.
- State Controller의 hold clock, Stop episode, Landing revision, local/remote TIP sequence, Pivot commit/cancel/suppression을 `FProject_JStateControllerRuntime`으로 옮겼다. Start/Stop/Pivot/TIP 우선순위와 chooser 자산 호출은 보존했다.
- Locomotion 컴포넌트의 phase family 및 MM moving 판정을 `FProject_JLocomotionContextBuilder`로, selection revision과 reselect cooldown을 `FProject_JMotionMatchingSelectionPolicy`로 옮겼다. reflected 선택 값은 component가 계속 게시한다.
- Remote Stop 잔여 속도 억제와 remote TIP 이벤트 타이머를 `FProject_JRemoteLocomotionRuntime`으로 옮겼다. 실제 world/ground probe와 local TIP semantic target은 컴포넌트에 남겼다.
- Character의 TIP 선택 anchor, root yaw clamp, RPC 전송 간격·종료 edge를 `FProject_JTurnInPlacePresentationRuntime`으로 옮겼다. Character는 애니메이션 루트 모션 추출, actor 회전 적용 및 RPC 전달만 수행한다. Possess/UnPossess/EndPlay에서 해당 런타임을 리셋한다.
- AnimInstance 재초기화와 owner reference 변경에서 두 presentation runtime 및 활성 PSD mirror를 리셋한다. Remote 런타임은 컴포넌트 EndPlay에서 리셋한다.

Chooser 선택 자산/출력, contact curve 읽기, procedural IK, Aim, mount snapshot 및 reflected AnimBP 필드는 engine/asset 경계에 붙어 있어 AnimInstance에 남겼다. 이를 별도 helper에 옮기면 UObject 호출과 reflection mirror 간 양방향 복사가 증가한다. Component의 local TIP target과 remote jump/landing 판단은 실제 CharacterMovement·replication·collision 입력을 사용하므로 현 컴포넌트가 소유한다.

## 4. 검증

- `Project_JEditor Win64 Development`: 직접 UBT 성공. 한 번 Google Drive File Stream이 generated `.obj`/`.dep.json`을 잠가 UBA 빌드가 실패했지만, `-NoUBA -MaxParallelActions=1` 재빌드는 성공했다. 최초 실패는 컴파일 진단이 아닌 파일 접근 거부였다.
- `Project_J Win64 Development`: 직접 UBT 성공.
- `Project_J Win64 Shipping`: 직접 UBT 성공.
- Headless `UnrealEditor-Cmd.exe`, `-NullRHI`, `-NoLiveCoding`: Animation/Components/Combat/Presentation 27개 성공. 신규 런타임 테스트 6개 포함.
- Architecture Animation/MM, snapshot, mount, presentation, GroupB 17개 성공. `CombatContinuity` 5개는 fixture의 시작 CVar `a.Budget.BudgetMs 0.1` 누락으로 첫 실행이 실패했고, CVar를 실행 전 적용한 별도 재실행에서 5개 모두 성공했다.
- 마지막 Pivot 이동 후 StateController, TurnInPlaceAndCombatStop, CombatStrafeRunPivotPolicy 3개 재실행 성공.
- 테스트 보고서: `Saved/Automation/ResponsibilityRefactor`, `Saved/Automation/ResponsibilityRefactorContracts`, `Saved/Automation/ResponsibilityRefactorCombatContinuity`, `Saved/Automation/ResponsibilityRefactorFinalPivot`.

이 검증은 C++과 터미널 자동화 범위다. 현재 사용자 작업 트리에 있는 Greatsword AnimBP·Character BP·의상/물리/Presentation 에셋은 수정하거나 저장하지 않았다. 그 에셋의 실제 연결, 화면상 motion matching/TIP/retarget 결과는 이 테스트만으로 판정할 수 없다.

## 5. 성능 및 후속

측정된 FPS 개선은 없다. 기존 MM 갱신 간격, hidden remote throttle, ABA/URO, dedicated server 경계는 유지했다. 값 기반 정책을 분리해 중복 context 비교와 상태 초기화 위치를 명확히 했지만 성능 수치는 별도 프로파일링이 필요하다.

현 범위에서 추가 코드 분해는 필요하지 않다. 에셋을 통한 실제 AnimBP/Chooser 연결과 화면상 로컬·원격 TIP, 착지, Pivot, mount 전환은 PIE에서 시각 확인해야 한다. 특히 작업 트리에 이미 변경된 Greatsword 에셋의 결과는 이번 C++ 리팩터링과 분리해 확인한다.
