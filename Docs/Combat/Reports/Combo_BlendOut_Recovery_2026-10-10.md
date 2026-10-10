# 대검 콤보 블렌드아웃 구간 복구

## 증상과 원인

첫 공격이 재생되지만 후속 입력이 다음 콤보로 이어지지 않는다. Unreal MCP로
`DA_Combo_Greatsword`의 11개 노드, 입력 태그, AttackDefinition 및 몽타주 연결을
조회했다. 전이 대상이 실제 노드에 연결되어 있고 전이 노드에는 ComboWindow가 있다.

`Project_JAnimNotifyState_ComboWindow::NotifyBegin`은
`ASC->GetAnimatingAbility()`를 통해 토큰을 소유할 공격 능력을 찾는다. 하지만 기본
`UAbilityTask_PlayMontageAndWait`는 정상 블렌드아웃 시작 시
`ASC->ClearAnimatingAbility(Ability)`를 호출한다. 공격 능력 자체는 완료 전까지 활성
상태인데, 블렌드아웃에 걸친 ComboWindow 알림은 소유자를 찾지 못한다.

에셋 조회에서 확인한 알림 시작점은 RMB1 약 0.931초, LR1 약 0.630초,
Q1 약 1.265초다. 연결 구간을 애니메이션 후반에 작성한 기존 데이터에서 이
수명 차이가 문제가 된다. DA의 전이 경로나 알림 타이밍을 임의로 변경하지 않았다.

## 변경

`Project_JGameplayAbility_Melee::StartComboNode`에서 몽타주 작업의
`bAllowInterruptAfterBlendOut`을 true로 지정한다. 정상 블렌드아웃에도 GAS의
몽타주 소유권을 유지해서 알림이 올바른 능력의 토큰을 얻도록 한다. 몽타주 완료,
외부 중단, 능력 취소, 무기 회수에 따른 종료 경로와 토큰 초기화는 유지한다.

`ProjectJ.Combat.ComboBlendOutWindows`는 실제 BP_Greatsword, 장비 DA, 콤보 DA,
몽타주와 알림을 사용해서 LMB 연속, RMB 연속, Q 연속,
LMB→RMB→RMB→RMB의 Q2 도달 및 취소 후 소유권 해제를 검사한다.

읽기 전용 재점검: 에디터 콘솔에서
`py "C:/Users/I/Documents/GitHub/Project_J/Scripts/Editor/Audit-GreatswordCombo.py"`.
결과는 `Saved/Validation/Combo_20261010/AuthoredCombo.json`에 기록한다.

## 점검 중 확인된 별도 사항

- 기존 PIE 설정으로 시작할 때 원점에서 캐릭터 스폰 충돌이 발생했다. MCP의
  시작 위치 오버라이드로 캐릭터가 정상 생성됨을 확인했다. 레벨은 변경/저장하지 않았다.
- 네트워크 PIE 클라이언트를 Python에서 직접 호출한 최초 자동 입력 점검은
  CharacterMovement 권한 assertion으로 에디터가 종료됐다. 엔진의
  `AActor::GetFunctionCallspace`가 `GAllowActorScriptExecutionInEditor`일 때 RPC를
  로컬로 실행하는 동작과 연결된다. 해당 임시 입력 스크립트는 제거했다.
  콤보 검증에는 게임 월드를 사용하는 C++ 테스트와 실제 UI 입력을 사용한다.

## 검증

- 직접 `UnrealBuildTool.exe Project_JEditor Win64 Development` 빌드 성공.
- 테스트 결과는 `Saved/Validation/Combo_20261010/Automation`에 기록한다.
- `ProjectJ.Combat.ComboBlendOutWindows`, `ProjectJ.Combat.ComboInputSubscription`
  모두 Success. 보고서 기준 성공 2, 실패 0, 미실행 0.
- 네트워크 PIE에서 실제 Tab/Q 키 입력을 사용해 Q1 입력 버퍼→ComboWindow→Q2
  전환을 클라이언트/서버 양쪽 로그로 확인했다. UI 조작 간격을 확보하기 위해 이
  수동 확인만 PIE 시간 배율 0.1로 수행했다. 정상 속도의 네 경로는 위 C++ 테스트가
  검사한다. 로그: `Saved/Validation/Combo_20261010/NetworkPIE_ActualInput.log`.
- 테스트 PIE를 종료해서 일시적인 시간 배율을 폐기했다. 수정된 빌드로 에디터를
  다시 열어 둔 상태이며 에셋과 레벨은 저장하지 않았다.
