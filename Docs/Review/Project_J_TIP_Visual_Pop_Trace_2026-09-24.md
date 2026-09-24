# 제자리 회전(TIP) 순간 튐 진단

## 현재 판단

전투 정지 상태에서 회전을 시작하거나 진행 중 반대 방향으로 돌릴 때, 화면의 TIP 포즈가 순간적으로 튄다. 코드만으로는 실제 애니메이션 그래프의 블렌드와 Offset Root Bone 결과를 확인할 수 없어 원인을 확정하지 않았다. 시각적 결과에 영향을 줄 수 있는 경계는 다음과 같다.

- `UProject_JLocomotionAnimStateComponent`가 카메라 입력으로 고정 회전 목표를 만들거나 반전 시 기존 목표를 해제한다. 반전 뒤에는 짧은 재선택 대기 시간이 있다.
- `UProject_JCharacterAnimInstance`가 State Controller의 TIP 유지 시간을 시작하거나 다시 시작하고 Chooser 애셋을 선택한다. 재선택 리비전이 바뀌면 Blend Stack의 강제 블렌드 신호도 새로 나온다.
- `AProject_JPlayerCharacter`가 선택 애셋의 누적 루트 yaw를 캡슐에 적용한다. 최초 선택 프레임에 유지 시간이 이미 진행되었거나, 같은 애셋의 리비전만 변경되면 회전 기준점과 현재 누적 루트 yaw 사이에 큰 차이가 생길 가능성이 있다.

마지막 항목은 **가설**이다. 캡슐 yaw가 실제로 뛰는지, 캡슐은 매끄럽지만 포즈만 튀는지 로그와 에디터 재현으로 구분한다. 이번 변경은 동작을 바꾸지 않고 진단 로그를 추가한다.

## 에디터에서 재현하고 로그 남기기

1. 새로 빌드된 모듈로 에디터를 실행한다. PIE 시작 전 또는 PIE 콘솔에서 `p.ProjectJ.TIPTrace 1`을 입력한다. 기본값 `0`은 기록하지 않는다.
2. 전투 모드에서 이동 입력 없이 가만히 선다. 한쪽으로 카메라를 돌려 첫 TIP의 튐을 재현한다. 이어서 TIP 재생 도중 카메라 방향을 반대로 바꿔 두 번째 튐을 재현한다.
3. 재현이 끝나면 `p.ProjectJ.TIPTrace 0`을 입력하고 PIE를 종료한다. `Saved/Logs/Project_J.log`에서 `TIPTrace`가 포함된 줄을 확인해 로그 파일을 전달한다. 재현 시점이나 좌/우 방향을 함께 알려주면 좋다.
4. 1단계 로그로 시점이 부족하면 같은 절차를 `p.ProjectJ.TIPTrace 2`로 한 번 더 실행한다. 모드 `2`는 TIP 활성 프레임 전체의 루트 회전을 기록하므로 로그가 많아진다.

`TIPTrace`의 `Stage=Semantic`은 목표 시작·연장·반전·완료, `Stage=Hold`는 재생 유지 시간의 시작·재시작·종료, `Stage=Chooser`는 애셋 및 리비전 선택, `Stage=Root`는 적용 전후 캡슐 yaw와 누적 루트 회전량이다. `T`는 게임 월드 시간(초), `Seq`는 의미 상태의 TIP 시퀀스, `Rev`는 Chooser 선택 리비전이다. 같은 재현의 기록을 이 세 값으로 대조한다.

## 로그에서 볼 기준

- 첫 프레임의 `Root NewSelection=1`에서 `Hold`, `RootCum`, `Applied`, `ActorBefore/After` 차이가 크면 캡슐 회전 적용 시점과 재생 시간의 불일치를 우선 확인한다.
- 반전 직후 `Semantic Event=Reverse`와 새 `Begin`, `Chooser Rev`, `ForceBlend` 사이에 포즈가 튄다면 이전 TIP의 해제, 새 애셋 진입 블렌드, Offset Root Bone 경계를 확인한다.
- `ActorBefore/After`는 연속적인데 화면만 튄다면 C++ 캡슐 회전보다 애니메이션 그래프의 Blend Stack/Steering/루트 보정 쪽을 확인한다. 에셋 연결이나 커브 값은 이 코드 로그만으로 단정하지 않는다.

## 검증

- UE 5.8 `UnrealBuildTool.exe`로 `Project_JEditor Win64 Development` 직접 빌드 성공. `-NoHotReload -NoUBA -MaxParallelActions=1`로 실행했다.
- `UnrealEditor-Cmd.exe`의 헤드리스 자동화에서 `StateControllerRuntime`, `TurnInPlaceAndCombatStop`, `TurnInPlacePresentationRuntime` 3개 성공, 실패 0개. 보고서는 `Saved/Automation/TIPVisualPopTrace/index.json`에 있다.
- 실제 순간 튐은 PIE 재현 로그를 받은 뒤 판별한다. 자동화 테스트는 애니메이션 그래프의 시각적 결과를 검증하지 않는다.
