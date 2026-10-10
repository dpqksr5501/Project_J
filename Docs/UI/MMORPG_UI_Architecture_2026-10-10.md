# MMORPG 최소 조작 UI — 2026-10-10

검은사막 참고 이후 확장: [캐릭터 UI 프로필·격자 가방·개인 창 위치](Character_UI_Profiles_2026-10-10.md).
현재 구현: [Compact HUD·혼합 퀵슬롯·독립 창·지도·소모품·의뢰·경험치](Compact_HUD_And_Gameplay_2026-10-10.md).
아래 초기 검증 기록은 첫 구현 단계의 증거이며, 최신 검증과 범위는 연결 문서를 따른다.

## 목적과 범위

실제 인벤토리·장비·GAS에 연결된 최소 화면을 먼저 만든다. 플레이어의 체력/마나/레벨,
기본 5개 입력 슬롯의 사용 가능 상태와 남은 쿨다운, 격자 가방/장비 목록을 표시한다.
프로필에 따라 스킬·실제 자원 게이지·전용 패널을 변경할 수 있다.
I로 열고 I/Esc/닫기로 닫는다. 각 목록은 마우스 휠로 스크롤한다.
가방에서 호환 장비 슬롯으로 드래그하면 장착, 장비에서 가방 영역으로 드래그하면 해제한다.
우클릭/더블클릭도 같은 요청 경로를 사용한다. 제목 드래그는 창 위치를 이동한다.

가방 순서 재배치, 아이템 분할, 판매/거래/은행, 전체 HUD 자유 배치 편집,
게임패드 포커스 내비게이션은 이번 최소 범위 이후 별도 기능이다.
퀵슬롯 재배치와 HUD 크기 조절은 이후 Compact HUD 단계에서 구현했다.

## 조사한 구조와 프로젝트 적용

상용 MMORPG의 조작 기준은 WoW·FFXIV 공식 안내를 참고하고, Unreal 구현 구조는 공개된 Epic 문서를 참고했다.
상용 게임의 비공개 서버 코드나 내부 UI 구조를 재현했다고 주장하지 않는다.

| 자료 | 적용 판단 |
|---|---|
| [WoW UI/HUD 개편](https://worldofwarcraft.blizzard.com/en-us/news/23837944) | 전투 정보를 잘 보이게 유지하고 통합 가방과 이동 가능한 UI를 제공한다. 가방/장비 통합 창과 제목 이동을 구현하고 캐릭터별 메뉴 위치 저장으로 확장했다. |
| [FFXIV HUD Layout](https://na.finalfantasyxiv.com/uiguide/know/know-hud/hud-layout.html) | 위치·크기·표시·투명도와 레이아웃 저장을 독립적인 사용자 설정으로 취급한다. 메뉴 위치 저장은 gameplay 요청과 분리했다. 사용자 크기 편집/전체 HUD 편집은 후속 범위다. |
| [FFXIV 재사용 대기시간 표시](https://na.finalfantasyxiv.com/uiguide/know/know-hb/hotbar_recasttimes.html) | 쿨다운 수치의 가독성을 스킨 교체 시에도 유지한다. 남은 시간과 사용 가능 상태는 GAS에서 읽고 위젯은 표현만 맡는다. |
| [Lyra Inventory and Equipment](https://dev.epicgames.com/documentation/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine) | 보관된 아이템과 장착 중인 장비의 역할을 분리한다. Project J의 기존 Inventory/EquipmentManager를 그대로 사용한다. |
| [UMG Viewmodel](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-viewmodel-for-unreal-engine) | 화면 데이터와 위젯의 표현을 분리한다. 이번 InventoryViewModel은 이벤트 기반 UObject이며 UMVVMViewModelBase 파생 클래스는 아니다. |
| [UMG Drag and Drop](https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-drag-and-drop-ui-in-unreal-engine) | UDragDropOperation에 아이템 인스턴스 ID를 고정하고, 드롭 시 현재 소유 상태를 재검사한다. |
| [UListView](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/UMG/UListView?lang=en-US) | 표시되는 행만 위젯으로 생성하는 가상 목록을 사용한다. 재활용 행은 이전 아이콘 로드를 취소한다. |
| [Common UI](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-plugin-for-advanced-user-interfaces-in-unreal-engine) | 다중 메뉴·게임패드·입력 라우팅을 확대할 때 적용할 후보다. 이번에는 기존 UMG 입력 경로를 유지한다. |

## 소유권과 흐름

```text
Local PlayerController → PlayerUIComponent → HUD/아이템/스킬 위젯
                                    ↓
                             InventoryViewModel
                                    ↓ 요청(ID, 아이템 인스턴스, 슬롯)
PlayerState.Inventory + EquipmentManager → 서버 검증/기존 장착 처리
                                    ↓ owner 응답 + 기존 FastArray 복제
                             이벤트로 화면 갱신
```

- PlayerState가 인벤토리, 장비, ASC, 성장 데이터를 소유한다. 위젯은 별도 게임 상태를 저장하지 않는다.
- 로컬 Controller만 화면을 생성한다. dedicated server와 다른 플레이어 Controller는 화면을 만들지 않는다.
- 초기 단계는 메뉴 닫힘 시 인벤토리 구독을 정리했다. 현재는 아이템 퀵슬롯 수량/쿨다운을 위해
  ViewModel 구독을 유지하고 PlayerState 교체/종료 시 정리한다. 목록 위젯은 계속 가상화한다.
- Pawn 교체 시 기존 Pawn으로 버튼 입력 해제를 보낸 뒤 새 ASC/성장 데이터를 연결한다.
- 자원 값은 속성 이벤트로, 스킬 상태는 GAS 이벤트로 갱신한다. 쿨다운 중에만 0.2초 간격으로 남은 시간을 읽는다.
- FastArray의 제거 콜백은 실제 배열 제거 전일 수 있어 다음 tick에 배치 결과를 읽는다.

## MMORPG에서 고려한 사항

1. **서버 권한:** 클라이언트가 보낸 정의/스탯을 믿지 않고 서버 가방에서 인스턴스를 찾는다.
   장착 슬롯, 잠김, 중복 장착, 작업 중 상태를 검증한다. UI 요청은 초당 20개로 제한한다.
2. **오래된 드래그:** 장비 A를 드래그하는 동안 B가 같은 슬롯에 장착되면 A 해제 요청이 B를 제거하지 못한다.
3. **응답 순서:** 요청 ID로 UI 대기를 구분한다. 응답과 FastArray가 별도로 도착해도 이후 복제 이벤트가 화면을 갱신한다.
   5초 지연 시 대기 표시를 해제하지만 요청을 자동 재전송하지 않는다.
4. **입력:** 메뉴는 게임을 일시 정지하지 않는다. 이동/시점 입력을 막고 커서를 표시한다.
   닫으면 기존 커서 값과 해당 메뉴가 획득한 이동/시점 차단을 해제한다.
5. **대량 아이템:** 매 프레임 Blueprint 속성 바인딩 대신 이벤트와 가상 목록을 사용한다.
   같은 행 객체를 유지하고 스크롤/선택을 보존한다. 아이콘은 비동기로 읽고 행 해제 시 취소한다.
6. **표현:** 플레이어에게 실패 원인을 표시한다. 내부 enum/클래스 이름은 오류 메시지로 노출하지 않는다.
   텍스트는 NSLOCTEXT로 작성하며 추후 번역 수집 대상이다.
7. **확장 경계:** 거래·소모·재정렬은 장착 RPC에 섞지 않는다. 각 서버 작업의 별도 검증/요청 계약을 추가한다.

## UI 에셋을 임포트한 뒤 교체하는 방법

1. `UProject_JPlayerHUDWidget`, `UProject_JItemWidget`, `UProject_JSkillButton`을 부모로 Widget Blueprint를 만든다.
2. 화면의 선택적 바인딩 이름: `AttributeLabel`, `StatusLabel`, `InventoryList`, `EquipmentList`,
   `MenuPanel`, `MenuTitle`, `CloseButton`, `ScreenCanvas`, `ActionBar`.
   목록은 `UProject_JInventoryListView` 또는 `UProject_JInventoryTileView`를 사용한다. 행은 `ItemLabel`, `ItemIcon`, `ItemFrame`,
   스킬은 `SkillLabel`, `SkillIcon`을 선택적으로 제공한다. 추가 자원/전용 모듈 이름은 캐릭터 UI 프로필 문서를 따른다.
3. Controller의 PlayerUI 컴포넌트 `ScreenClass`를 새 화면으로 지정한다.
   화면의 `ItemWidgetClass`, `InventoryTileWidgetClass`, `SkillWidgetClass`를 새 행/타일/스킬 스킨으로 지정한다.
4. 임포트한 텍스처/폰트/테두리는 Blueprint 레이아웃에 적용한다. 장착 RPC와 데이터 소유 코드는 교체하지 않는다.
5. 스킨 교체 후에는 I/Esc, 드래그, 빈 가방 드롭, 긴 이름, 목록 휠,
   720p/1080p/울트라와이드 DPI, 네트워크 지연, 재접속, Pawn 교체를 다시 확인한다.

기본 행은 `/Game/UI/WBP_ProjectJItemRow`의 최소 Widget Blueprint다. UE5.8 에디터의
목록 행 클래스 제약을 만족하고 native 기본 표현을 사용한다. `/Game/UI`는 cook 포함 디렉터리로
설정했다. 전체 패키징/cook 완료를 의미하지 않는다.

목록 스킨은 Drag and Drop 허용을 끄지 않는다. UE5.8 `SObjectTableRow`는 이 플래그가 꺼지면
행의 `NativeOnDrop`도 실행하지 않는다. 더블클릭은 행의 Native 이벤트보다 목록 이벤트로
전달되므로 `UProject_JInventoryListView`에서 같은 서버 요청에 연결한다.

## 직접 조작하기

1. `Lvl_ThirdPerson`에서 PIE를 시작하고 I를 누른다.
2. 실제 소유 인벤토리 아이템을 무기 등 호환 슬롯에 끌어 놓는다. 초기 프로토타입 무기는
   가방 소유 아이템과 다를 수 있으므로, 가방이 비어 있다고 오류로 판단하지 않는다.
3. 개발 검증 시에만 기존 게임 콘솔의 `EquipmentClientTest prepare`를 사용하면 서버가
   테스트용 Greatsword 인스턴스 하나를 지급한다. 콘솔 equip 명령 없이 UI로 장착/해제를 검증한다.
   이 fixture는 PIE 전용이며 stop/PIE 종료/300초 만료 시 정리된다. 영구 아이템 지급 기능은 아니다.

## 초기 최소 UI 단계 검증

Editor 및 Game Development 직접 UBT 빌드 통과(2026-10-10).
첫 빌드에서 발견한 기존 unity 빌드의 `FoleySubsystem.Enabled` 이름 충돌은
`FoleyEnabled`로만 변경했다(동작 변경 없음).

| 검증 | 결과/증거 |
|---|---|
| 최종 Editor / Game 빌드 | `Saved/Validation/MMOUI_20261010/BuildEditorInteractionFinal.log`, `BuildGameInteractionFinal.log`: Succeeded |
| 회귀 자동화 | `TestsFinal/index.json`: 성공 24, 경고/실패 0. UI·EarlyTransition·RuntimeOwnership·LocomotionContinuity·MovingTurn·Equipment |
| 최종 UI 이벤트 연결 후 자동화 | `TestsUIInteractionFinal/index.json`: 성공 2, 경고/실패 0, 프로세스 exit 0 |
| 실제 드래그 장착/해제 | 전용 서버 + 클라이언트 PIE에서 Greatsword 장착/가방 빈 영역 해제와 서버 완료 메시지 확인 |
| 잘못된 슬롯 드롭 | Greatsword를 Head에 놓아도 장비/가방 상태가 바뀌지 않음 |
| 우클릭 / 더블클릭 | 장착, 장비 행 더블클릭 해제 확인. 최종 더블클릭은 목록 이벤트 경로로 검증 |
| 휠 / 창 이동 | Mount까지 스크롤, 제목 이동 및 이전 창 영역 밖 이동 확인 |
| 메뉴 수명 | I/Esc/닫기 버튼, 다시 열었을 때 현재 장착 상태 확인 |
| 플레이어 분리 | 클라이언트 1에 지급한 아이템이 클라이언트 2 가방에 표시되지 않음 |
| HUD | 클라이언트 HP 100/100, MP 100/100, Lv.1 및 5개 스킬 슬롯 표시 확인 |
| 에디터 수 / 정리 | 하나의 UE5.8 Editor에서 서버+클라이언트 실행. 최종 StopPIE, 설정 원복 후 MCP 재조회 |

실제 조작은 computer-use `@oai/sky`로 수행했다. UE5.8 내장 ModelContextProtocol의
StartPIE/StopPIE 및 관련 설정 조회에 MCP를 사용했다. 별도 GASP 5.7 에디터는 닫은 후
Project J 5.8 에디터를 실행했으며 두 에디터를 동시에 실행하지 않았다.

스크린샷은 `UI_Client1_Equipped.png`, `UI_Client2_EmptyBag.png`이며 위 Saved 검증 디렉터리에 있다.
Saved 로그/백업은 로컬 증거이며 Git의 저장 대상이 아니다.

이번 결과는 전체 cook/패키징, 대규모 아이템 부하, 패킷 지연/재접속,
실제 스킬 쿨다운 발동과 만료의 시각 검증, 모든 해상도/게임패드 테스트를 포함하지 않는다.
현재는 캐릭터/프로필별 메뉴 위치 저장과 화면 크기 변경 시 배치 보정까지 확장했다.
사용자 창 크기 편집, 거래/판매, 가방 순서 재배치는 구현 범위에 포함하지 않았다.

다음 고도화는 (1) 임포트할 스킨의 실제 해상도/긴 이름 검증,
(2) 직업 데이터·UI 프로필 및 전체 HUD 설정, (3) 아이템 정렬/필터/툴팁 스탯 비교,
(4) 대상/파티/버프 표시 순서로 기존 ViewModel·서버 작업 계약을 확장한다.
