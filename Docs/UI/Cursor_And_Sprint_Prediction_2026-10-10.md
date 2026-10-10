# 좌측 Ctrl 커서 전환과 달리기 이동 예측 수정

기준 커밋은 `4deae970`이다. Shift 달리기 중 이동이 끊기는 제보와 HUD를 마우스로 조작할 수 없는 문제를 수정했다.

## 달리기와 네트워크

실제 저장된 에디터 사용자 설정에 두 클라이언트 PIE, 송신 지연 150~250ms, 패킷 손실 10%가 활성화돼 있었다. 이전 UI 검증의 런타임 설정 복원은 일반 플레이용 설정 파일의 비활성화를 보장하지 않았다. 이번에는 에디터 종료 후 `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`의 NetworkEmulationSettings까지 비활성화하고 지연·손실 값을 0으로 정리했다. 두 클라이언트 설정은 유지한다. 이 로컬 사용자 설정은 Git 커밋 대상이 아니다.

기존 코드는 Actor Tick에서 GAS의 현재 Sprint 태그를 읽어 속도·가속·마찰·제동을 변경했다. 과거 이동의 달리기 상태를 CMC SavedMove에 기록하지 않았고, 서버/재예측의 가속 제한과 물리 이동 시점에 해당 이동의 정책을 적용하는 경로도 없었다.

플레이어의 기본 CharacterMovement를 `UProject_JCharacterMovementComponent`로 교체했다. NPC와 탈것의 이동 클래스는 변경하지 않는다. `FProject_JSavedMove`의 `FLAG_Custom_0`에 유효한 달리기 의도를 저장하며, 달리기 시작/종료를 가로질러 이동을 합치지 않는다. 재예측은 해당 이동의 플래그를 복원한다. 로컬 입력의 가속 계산 전, 서버/재예측의 가속 제한 전, 실제 물리 이동 전에 같은 캐릭터 이동 정책을 적용한다. 전투 방향별 속도는 소모된 PendingInput 대신 해당 이동의 입력/가속 방향으로 계산한다.

속도 값을 클라이언트가 보내지 않는다. 프로필 값과 서버 GAS Sprint 허용 여부, 공격·회피·피격·전투 정책을 유지한다. 클라이언트 플래그만으로 달리기 능력을 얻거나 서버 위치 보정을 끄지 않는다. GAS 활성화/종료와 이동은 서로 다른 네트워크 경로이므로 큰 지연에서 전환 시점의 일시적인 보정은 남을 수 있다. 비용·스태미나·프로필 변경까지 과거 시점으로 되돌리는 시스템은 이번 범위가 아니다.

## 커서 조작 계약

| 조작/상태 | 동작 |
|---|---|
| 좌측 Ctrl 단독으로 눌렀다 놓기 | HUD 커서 표시/숨김 전환, 길게 눌러도 한 번 |
| 커서 표시 | GameAndUI, 키보드 이동 허용, 카메라 Look 차단 |
| 커서 숨김 | GameOnly, 카메라 조작 복원 |
| 가방·장비 등 메뉴 열기 | UIOnly, 커서 표시, 이동/Look 차단 |
| 마지막 메뉴 닫기 | 메뉴를 열기 전의 커서 모드로 복원 |
| 메뉴의 Ctrl / Ctrl+F / Ctrl+M / Ctrl+마우스 | 메뉴를 숨기거나 커서를 전환하지 않음 |
| Pawn·캐릭터 소스 교체 / 종료 | 보류 중인 Ctrl 조작, 임시 모드와 소유 입력 차단 정리 |
| 앱 비활성화 | 보류 중인 Ctrl 전환, 누른 UI 스킬·Sprint·키 상태 정리 |

Slate 입력 전처리기는 소유 LocalPlayer의 Slate 사용자와 활성 게임 창/게임 뷰포트 포커스를 확인한다. 이벤트를 소비하지 않아 Enhanced Input의 기존 수정 키와 위젯 단축키가 계속 동작한다. Ctrl을 놓을 때 전환해 Ctrl+F/M/드래그를 구별한다. Ctrl 이전부터 누르고 있던 이동 키의 반복 이벤트는 새 조합키로 간주하지 않는다. 에디터 창이나 다른 PIE 클라이언트의 Ctrl로 이 화면을 전환하지 않는다.

IgnoreMove/IgnoreLook은 카운터이므로 이 UI가 소유한 차단만 상태가 바뀔 때 한 번 획득/반환한다. 다른 gameplay 시스템의 차단을 Reset으로 지우지 않는다. 커서 모드의 LMB/RMB가 월드 공격으로 새로 시작되지 않게 입력 바인딩에서 차단하며, 화면의 퀵슬롯 클릭은 기존 UI 경로를 사용한다. 커서/메뉴 모드에 진입할 때 기존 누른 마우스 공격과 조합키 대기 타이머를 취소한다. 취소 과정에서 짧은 공격 탭을 새로 발생시키지 않으며 키보드 수정 키는 보존한다.

커서 입력은 로컬 UI 정책이므로 별도 IA나 매핑 에셋을 생성하지 않았다. 기존 Sprint/Move 에셋을 사용한 실제 입력 경로를 검증했고 레벨·에셋을 저장하지 않았다.

## 검증과 재현

증거는 `Saved/Validation/SprintCursor_20261010/`에 있다. 직접 UBT의 최종 Editor/Game 빌드, UI·RuntimeOwnership·PlayerMaturity 회귀 테스트 결과, 네이티브 플레이 화면과 MCP 표시값을 함께 보관한다.

`BuildEditorComplete.log`와 `BuildGameComplete.log`는 성공이다. `TestsFinal/index.json`은 총 42개 성공(41개 일반 성공, 기존 탈것 테스트 경고를 포함한 1개 성공), 실패/미실행 0개다. `TestsFinal.log`의 종료 코드는 0이다. 문서 링크 검사도 오류 0개다.

에디터 하나에서 두 클라이언트/별도 PIE 서버를 실행했다. `SprintInputTest`는 로컬 PIE에서만 사용할 수 있는 검증 명령이다. PC InputKey를 통해 실제 W/S·좌측 Shift 상태를 8초 동안 유지하고, 1초마다 방향·2초마다 걷기/달리기를 전환한다. MaxWalkSpeed를 직접 바꾸거나 이동을 텔레포트하지 않는다. 종료·Pawn 교체·입력 차단·EndPlay에서 주입한 키를 모두 해제한다. 실행 중에는 사용자 조작과 섞지 않는다.

| 조건 | 관측 |
|---|---|
| 지연/손실 비활성, 8초 | 걷기 500 / 달리기 700, 위치 보정 1회, 2.755cm |
| 송신 150~250ms·손실 10%, 8초와 종료 후 수신 정리 | 걷기 500 / 달리기 700, 보정 메시지 18회 중 위치 변화 6회, 최대 10.906cm |

수치는 한 번씩의 짧은 localhost PIE 관측이다. 방향 반전·종료·GAS 전환도 포함하며 위치 변화 없는 확인 메시지를 큰 위치 끊김으로 계산하지 않는다. 수정 전 동일 조건의 수치 측정은 없어 개선율을 주장하지 않는다. WAN·실제 전용 서버·장시간 전투·비용 있는 Sprint의 검증을 대체하지 않는다.

컴퓨터 유즈로 Ctrl → 가방 버튼 클릭 → 메뉴에서 Ctrl → Escape → 월드 드래그 → Ctrl 복귀를 확인했다. 커서 모드에서는 화면 구도가 회전하지 않았다. 이후 MCP에서 현재 클라이언트의 bShowMouseCursor=true, 상대 클라이언트=false, 재전환 후 현재=false를 확인했다. Ctrl+F로 표시 상태가 바뀌지 않았다. `CursorVisibleState.json`, `PeerCursorState.json`, `CursorHiddenState.json`, `SettingsAfter.json`이 읽기 증거다.

SavedMove 플래그 저장·복원·재사용, 전환 합치기 금지, 서버 권한 거절, Sprint 종료와 GAS 종료 지연의 분리, Ctrl 단독/반복/조합/마우스/포커스 취소, 마우스 취소 시 수정 키 보존을 자동화 테스트로 확인한다.

Epic의 [CharacterMovement 네트워크 이동](https://dev.epicgames.com/documentation/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)과 로컬 UE 5.8 `CharacterMovementComponent` 소스를 대조했다. UI 수명·키보드 조작의 이전 기록은 [UI 수명과 키보드](Continuity_Keyboard_And_Dynamic_Load_2026-10-10.md)를 참고한다.
