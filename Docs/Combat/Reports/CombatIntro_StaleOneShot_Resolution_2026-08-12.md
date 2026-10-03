# Combat Draw/Sheathe 중 stale one-shot 차단 (2026-08-12)

## 증상

비전투 상태에서 Land/Start/Stop 같은 State Controller direct Blend Stack one-shot이 선택된 직후 전투 전환(Tab)을 시작하거나, 전투 상태에서 같은 one-shot 도중 장비 해제(Outro)를 시작하면 FullBody 몽타주가 화면을 덮는다.

문제는 몽타주가 끝난 뒤 발생한다. direct Blend Stack의 `StateControllerPlaybackHoldState`와 선택 애셋 캐시는 계속 살아 있으므로, 몽타주 아래에서 멈춰 있던 **비전투 Land**가 다시 최종 포즈를 차지할 수 있다. 전투 Strafe로 진입했는데 뒤늦은 착지 모션이 보이는 이유다.

## 해결 원칙

착지 자체를 취소하지 않는다.

- `CharacterMovement`, locomotion component의 `bIsLanding`, 네트워크/복제 상태는 원래대로 유지한다.
- 해결 대상은 오직 **애니메이션 표현(presentation) 소유권**이다.
- 전투 intro는 presentation epoch(경계)로 취급한다. 경계 이전에 고른 direct one-shot은 이후에 재개될 권한이 없다.

## 구현 흐름

`UProject_JCharacterAnimInstance::NativeUpdateAnimation()`에서 `BuildThreadSafeData()` 직후 처리한다.

```text
Combat Intro rising edge
  -> 현재 State Controller hold/cache 폐기
  -> (물리 Landing 중이면) 해당 착지 이벤트가 끝날 때까지 stale Land 차단
  -> Intro 동안 direct ground one-shot 대신 Idle/Cycle MM context 발행
  -> 몽타주 종료 시 현재 Combat 상태에서 MM 재선택
```

### 1. 캐시와 hold 무효화

Intro 또는 Outro가 시작되는 한 프레임에 다음을 초기화한다.

- `StateControllerPlaybackHoldState`
- `CachedStateControllerChooserTable`
- `CachedStateControllerSelectedAnimation`
- chooser output/cache validity
- Start/Land mouse-turn latch

따라서 단순히 AnimGraph branch만 끄는 방식과 달리, 예전 Land 애셋이 몽타주 뒤에 다시 살아날 수 없다.

### 2. montage 중 direct one-shot 억제

Draw/Sheathe 몽타주가 pose를 소유하는 동안 지상 direct one-shot 요청을 `IdleLoop` 또는 `LocomotionLoop`으로 평탄화한다.

- 정지: `Idle` phase -> regular MM의 현재 Idle PSD
- 이동: `Cycle` phase -> regular MM의 현재 Strafe/OTM Cycle PSD

`bUseHeavyLand`, `bLandWasMoving`, `bLandWasSprinting`은 이 **MM 선택 snapshot에서만** 비활성화한다. 원본 landing component 상태를 변경하지 않으므로 실제 착지 후속 처리와 복제에는 영향이 없다.

매 프레임 강제 Pose Search는 하지 않는다. Draw/Sheathe 시작·끝 또는 기존 gameplay reselect 요청 때만 force-reselect가 발생한다. 따라서 몽타주 재생 중 불필요한 MM 재검색 비용을 만들지 않는다.

### 3. 기존 착지와 새 착지의 구분

Draw/Sheathe 시작 당시 실제 landing event가 활성화되어 있었다면 `bSuppressPreTransitionLandingPresentationUntilLandingEnds`를 유지한다. 해당 기존 이벤트가 semantic landing phase와 물리 landing 상태 양쪽에서 끝난 후에만 해제한다.

따라서:

- **전환 이전의 Land**: 절대 복귀하지 않는다.
- **전환 완료 후 새로 발생한 Land**: 정상적인 현재 모드의 landing 정책으로 선택된다.

## OTM / Combat Strafe 안전성

이 정책은 `bIsPlayingCombatIntro`와 `bIsPlayingCombatOutro`라는 장비 장착/해제 몽타주에만 적용된다.

- 비전투 OTM의 일반 Start/Stop/Land 동작은 바뀌지 않는다.
- Combat Strafe 이동 중에는 fallback이 `Cycle`이므로 기존 Strafe MM이 유지된다.
- 대각 점프/착지의 물리 상태와 trajectory/selection 입력을 삭제하지 않는다.
- TIP steering, State Controller TIP re-entry, Blend Stack Force Blend 경로와는 독립적이다.

## 확인 방법

1. 비전투에서 착지/stop one-shot이 아직 끝나기 전에 Tab을 누른다. 장비 착용 몽타주 뒤 비전투 Land가 다시 보이지 않아야 한다.
2. 전투 상태에서 착지/stop one-shot 중 장비 해제를 한다. Sheathe 몽타주 뒤 전투 Land/Stop이 다시 보이지 않아야 한다.
3. 장착 뒤에는 정지면 Combat Idle MM, 이동 중이면 Combat Strafe Cycle MM으로 복귀한다. 해제 뒤에는 현재 OTM Idle/Cycle MM으로 복귀한다.
4. 이후 실제로 다시 점프 후 착지하면 현재 모드의 landing 정책은 정상 동작해야 한다.

`p.ProjectJ.MMTransitionDebug 1`에서 다음 로그가 한 번 보이면 전환 캐시 폐기가 적용된 것이다.

```text
StateControllerCombatPresentationReset: Intro=... Outro=... HeldLand=... PhysicalLanding=... PreviousAssetDiscarded=true
```
