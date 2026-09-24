# StateController Runtime Finalization Result

## 1. HEAD / Build / Tests

- 작업 시작 HEAD: `289007178460409bedcc63ed1f5abca438dadb5e` (`Character_Test`, Unreal Engine 5.8).
- Editor: `UnrealBuildTool.exe Project_JEditor Win64 Development` 성공.
- Game: `UnrealBuildTool.exe Project_J Win64 Development` 성공.
- Shipping: 변경 중간 단계에서 `UnrealBuildTool.exe Project_J Win64 Shipping` 성공. 이후 재검증에서는 C++ 컴파일·링크 완료 뒤 `Project_J.uhttimestamps`가 다른 프로세스에 잠겨 UBT가 종료 코드 1을 반환했다. 최종 성공으로 표기하지 않는다.
- 빌드는 모두 터미널에서 `-NoHotReload -NoUBA -MaxParallelActions=1`로 직접 실행했다. 첫 Editor 빌드의 남은 지역 변수 참조 컴파일 오류는 수정 후 재빌드해 성공했다. 마지막 Game 빌드는 중간 `.obj` 접근 거부 뒤 같은 명령 재시도로 성공했다. Shipping 재검증은 중간 `.obj` 접근 거부에 이어 위 타임스탬프 잠금으로 실패했다. UBT의 첫 오류와 Windows Application Event Log를 확인했으며 관련 이벤트는 없었다.
- `UnrealEditor-Cmd.exe -unattended -NullRHI -NoLiveCoding` 자동화: `ProjectJ.Animation`, `ProjectJ.Components`, `ProjectJ.Combat`, `ProjectJ.Mount`, `ProjectJ.Presentation`, 관련 Architecture 및 Internal 그룹 합계 38개 성공, 실패 0개. 보고서: `Saved/Automation/StateControllerFinalizationOwnerReset/index.json`.

## 2. Why the previous refactor stopped at partial extraction

### 의도적으로 유지한 경계

- 이전 분리에서 `FProject_JStateControllerRuntime::PrepareDesiredState`로 Stop의 단일 소비, TIP 순번 재진입 금지, 착지 리비전 가드가 이미 이동했다. `UProject_JCharacterAnimInstance`에는 Chooser 평가, Pose Search, `UAnimationAsset::GetPlayLength`, 몽타주 가중치 관찰, 접지 곡선, AnimBP 리플렉션 필드 및 프록시 게시가 남았다. 이 경계는 엔진 객체 수명과 GC 계약 때문에 적절하다.
- `UProject_JLocomotionAnimStateComponent`는 지상·공중·착지·Pivot·TIP 요청의 의미 상태 원본이다. StateController 런타임은 해당 요청의 **애니메이션 표현 수명**만 관리한다.

### 단계적 이전과 실제 미완성 부분

- 이전 작업은 선택 캐시와 일부 값 기반 판정을 먼저 추출해 엔진 경계와 테스트 가능성을 확보했다. 이는 동시 변경 위험을 낮추는 타당한 단계였다.
- 다만 이전 `FProject_JStateControllerRuntime`의 hold 상태, 시작 시각, Pivot 커밋, Stop 소비 플래그, TIP 재선택 플래그, 착지 리비전이 모두 공개되어 있었다. `UProject_JCharacterAnimInstance.cpp`가 직접 상태를 읽거나 고치는 코드가 단순 행 검색 기준 90줄이었다. `FillLocomotionStateThreadSafeData`는 Pivot 중단·대체·방향 전환·억제 정책을 직접 결정했고, `ResolveStateControllerPresentationStateWithPlaybackHold`는 hold 진입과 소비 정책을 직접 실행했다. 따라서 부분 추출은 최종 책임 경계로는 미완성이었다.
- 이번 작업은 그 표현 상태 전이를 런타임으로 옮겼다. 엔진 호출과 자산 선택을 값 기반 런타임에 옮기면 UObject 의존성과 양방향 복사가 생기므로 그 지점에서 분리를 멈췄다.

## 3. Before

| 영역 | 이전 책임 |
| --- | --- |
| `UProject_JCharacterAnimInstance` | 엔진 관찰·Chooser 호출 외에 hold 시각과 상태 직접 수정, Pivot 중단·대체·억제, TIP 순번 재선택, 몽타주 경계 초기화 |
| `FProject_JStateControllerRuntime` | `PrepareDesiredState` 일부 정책과 공개 필드 저장소. `AnimInstance`가 내부 필드를 직접 조작 |

## 4. After

| 영역 | 현재 책임 |
| --- | --- |
| `UProject_JCharacterAnimInstance` | 스냅샷 작성, 엔진·자산 상태 읽기, Chooser/Pose Search 호출, 리플렉션 필드와 프록시 게시, 런타임에 사건과 값 전달 |
| `FProject_JStateControllerRuntime` | `BeginDesiredHold`, `ReconcilePivot`, `CommitPivot`, `RestartTurnSequence`, `OnFullBodyActionStart/End`, `OnCombatPresentationBoundary`, `OnMountBoundary`를 통한 표현 상태 전이와 낡은 요청 억제 |

`FillLocomotionStateThreadSafeData`는 Pivot 입력을 작은 값 구조체로 전달하고, 런타임의 중단 이유와 Cycle 유지 판정만 스냅샷에 반영한다. `ResolveStateControllerPresentationStateWithPlaybackHold`는 시간·에셋 길이·점프 재선택 같은 엔진 관찰을 유지하지만 hold 진입·착지 리비전·Stop 소비·TIP 순번 전이를 직접 쓰지 않는다. `EvaluateStateControllerAnimationChooserOnGameThread`는 자산 선택 성공 뒤에만 Pivot을 커밋한다.

## 5. Ownership

| 상태 또는 작업 | 소유자 |
| --- | --- |
| 게임플레이 이동·전투 원본 | GAS, `CombatStateComponent`, `LocomotionAnimStateComponent` |
| 애니메이션 표현 상태·hold·Stop 소비 | `FProject_JStateControllerRuntime` |
| Pivot 표현 중단·대체·방향 전환 억제·커밋 | `FProject_JStateControllerRuntime` |
| TIP 로컬·원격 순번 소비와 재선택 요청 | `FProject_JStateControllerRuntime` |
| 착지 표현 리비전과 전투/탑승 경계의 낡은 착지 방지 | `FProject_JStateControllerRuntime` |
| Chooser, Pose Search, 애니메이션 에셋, 접지 곡선, 몽타주 관찰 | `UProject_JCharacterAnimInstance` |
| AnimBP 리플렉션 값과 작업 스레드 프록시 게시 | `UProject_JCharacterAnimInstance` |

런타임은 게임 스레드에서만 갱신하며 살아 있는 UObject를 조회하지 않는다. 작업 스레드에는 기존 불변 프록시 스냅샷만 전달한다.

## 6. Direct State Access

- 이전: `UProject_JCharacterAnimInstance.cpp`에서 공개 런타임 필드 직접 접근 90줄(행 기준, 테스트 제외).
- 이후: 같은 기준 0줄. 필드 자체는 `private`이며, `AnimInstance`의 상태 관찰용 `GetHeldState`, `GetPivot`, `GetHoldElapsed` 호출은 29줄이다. `GetPivot`은 `const` 참조를 반환하므로 어댑터가 Pivot을 수정할 수 없다.
- 남은 관찰은 디버그 메시지, 프록시 스냅샷, Chooser 에셋 잠금과 애니메이션 재생 시간 계산에 필요하다. 값마다 setter를 만드는 대신 실제 전이에 해당하는 메서드만 노출했다.

## 7. Regression Protection

- 순수 런타임 테스트에 Stop 1회·같은 입력 해제 중 재진입 금지·새 이동으로 재무장, Pivot 방향 전환 억제·새 Pivot 대체·낡은 요청 재진입 금지, 로컬/원격 TIP 순번 분리, 중단된 착지 리비전 방지, 전신 동작과 전투 표현 경계, 탑승 경계 초기화를 포함했다.
- 탑승·하차 경계에서는 이전 Pivot 요청 리비전, TIP 순번, 활성 착지 리비전을 소비하고 Chooser 에셋 캐시를 비운다. 소유자 참조 교체와 AnimInstance 재초기화는 런타임뿐 아니라 이전 Chooser 자산, 몽타주 경계 플래그와 탑승 상태도 함께 초기화한다.
- 기존 `TurnInPlaceAndCombatStop`, `CombatStrafeRunPivotPolicy`, MM 정책, 애니메이션 스냅샷, 전투 전환, 원격 이동, 무기 표현, 탈것 및 SSR 자동화 검증을 유지했다. 애셋의 실제 연결과 시각적 전환은 headless 테스트만으로 판정할 수 없으므로 PIE 확인이 별도로 필요하다.

## 8. Performance

- 측정: FPS 및 프레임 시간 측정 없음.
- 미측정: 상태 전이 코드가 `AnimInstance`에서 값 기반 런타임으로 이동했다. 매 프레임 거대한 스냅샷 복사나 새 UObject/Tick/Subsystem은 추가하지 않았다. 성능 개선 수치는 주장하지 않는다.

## 9. Remaining Work

No material code follow-up is required for the StateController responsibility boundary.

PIE에서는 실제 AnimBP·Chooser 연결을 사용한 로컬/원격 TIP, 착지, Pivot, 공격·발도·납도, 탑승·하차 화면 전환을 확인해야 한다. 사용자 작업 트리의 Greatsword 에셋은 이번 작업에서 수정하거나 저장하지 않았다.

Shipping UBT의 `Project_J.uhttimestamps` 잠금이 해제된 환경에서 빌드 종료 코드 0을 다시 확인해야 한다. 직전 시도에서는 실행 파일까지 링크됐지만 UBT 프로세스가 성공으로 종료되지 않았다.
