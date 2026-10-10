# 캐릭터 UI 프로필과 검은사막 참고 — 2026-10-10

최신 구현/검증은 [Compact HUD와 gameplay 연결](Compact_HUD_And_Gameplay_2026-10-10.md)을 따른다.
이 문서의 검증 표는 프로필·격자 가방 확장 당시의 기록이다. 이후 독립 창, 10개 혼합 퀵슬롯,
HUD 배율, 검색/정렬/필터/장비 비교, 지도/의뢰/소모품/경험치를 추가했다.

## 참고 자료와 적용 판단

사용자가 제공한 `13372.jpg`, `13373.jpg`에서는 전투 중 핵심 HUD와 상세 캐릭터 정보·장비·격자 가방 창이 구분된다.
이번 구현은 화면의 역할 분리, 자원 게이지, 아이콘 격자, 창 이동을 참고한다. 검은사막 이미지/아이콘을 게임 에셋으로 복제하지 않았다.
상용 게임의 내부 소스나 서버 구조를 확인한 것은 아니다. 아래는 공식 문서에 공개된 기능과 Project J 설계 판단이다.

| 공식 자료 | 확인한 기능 | Project J 적용 |
|---|---|---|
| [검은사막 인터페이스](https://www.kr.playblackdesert.com/ko-KR/Wiki?wikiNo=15) | 정보 표시/숨김, 창 위치 편집, 격자 배치, 프리셋 저장 | 전투 HUD와 상세 메뉴를 분리하고 제목 이동 및 개인 창 위치 저장을 구현. 전체 HUD 편집기는 후속 범위 |
| [검은사막 기술 창·프리셋](https://www.kr.playblackdesert.com/ko-KR/Adventure/History?_groupMasterNo=3179) | 전승/각성별 기술 구분, 쿨다운 표시 설정, 기술 프리셋 | 성장 상태와 GAS 태그로 UI 프로필 선택. 스킬 습득/프리셋 서버 기능과 표시 프로필을 구분 |
| [FFXIV 직업 게이지](https://na.finalfantasyxiv.com/uiguide/know/know-hud/hud_jobhud_settings.html) | 직업별 상태·자원·게이지 디자인, 직업마다 간소화 설정 | 자원 정의와 전용 패널을 프로필 데이터로 분리. 필요한 실제 속성이 없으면 게이지를 표시하지 않음 |
| [FFXIV HUD Layout](https://na.finalfantasyxiv.com/uiguide/know/know-hud/hud-layout.html) | 위치·크기 등 표시 설정 편집 | 로컬 UI 환경설정을 서버 캐릭터/인벤토리 데이터와 분리 |
| [Lyra Inventory/Equipment](https://dev.epicgames.com/documentation/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine) | 보유 인벤토리와 장비 인스턴스 역할 구분 | 기존 owner-only Inventory/EquipmentManager와 서버 검증 경로 유지 |

검은사막 기술 자료는 업데이트 히스토리의 해당 기능 설명이다. 현재 모든 클래스의 세부 기술/과금 조건을 검증한 자료로 사용하지 않는다.

## 캐릭터마다 달라질 수 있는 UI

| 차이 | 현재 연결/확장 경계 |
|---|---|
| 직업·전직·무기/전투 모드별 스킬 슬롯, 아이콘, 안내 | ClassId/AdvancementId/ASC 태그로 프로필 선택. 실제 보유 능력과 CanActivate/cooldown은 GAS에서 조회 |
| 마나·기력·분노 등 전용 자원 | Current/Maximum FGameplayAttribute로 정의. 현재 기본 화면은 실제 Health/Mana만 사용. 새로운 자원 규칙/복제는 gameplay 구현이 먼저 필요 |
| 콤보/명령 가이드, 직업 전용 게이지 | CharacterHUDModule Widget Blueprint를 프로필에 지정. 전달받은 상태/자원으로 표현하며 해당 기능의 실제 데이터 어댑터는 별도 추가 |
| 소환수·펫·탑승물·변신 패널 | 상태 태그로 전용 프로필/모듈을 고를 수 있음. 대상 actor와 소유권·수명·전용 상태 연결은 해당 시스템의 ViewModel로 구현해야 함 |
| 장비 종류·착용 제한·세트 효과 | 현재 EquipmentSlot 서버 계약을 유지. 종이 인형식 배치는 화면 스킨으로 변경 가능. 새 슬롯/착용 규칙은 UI 프로필로 만들지 않음 |
| 레벨별 해금, 직업 퀘스트, 제작/채집 정보 | 실제 진행 상태의 표시 어댑터를 별도 추가. UI가 해금 권한을 판단/저장하지 않음 |
| 개인별 배치와 접근성 | Character GUID + ProfileId로 독립 창 위치·퀵슬롯·HUD 배율·수치 표시 분리. 전체 HUD 자유 배치·게임패드·입력 remap 편집기는 후속 기능 |

프로필은 **표시 구성**이다. 직업 변경, 각성, 스킬 습득, 장비 프리셋 적용을 승인하는 서버 기능이 아니다.

## 구현 계약

`Project_JCharacterUIProfile`과 `UIProfileCatalog`는 Project_J의 UI 모듈에 둔다.
Project_JCharacter의 성장/장비 코드가 UI 에셋을 참조하지 않아 의존성 순환을 만들지 않는다.

1. 로컬 PlayerUIComponent가 PlayerState의 Progression 및 ASC 소유 태그에서 Context를 만든다.
2. Catalog는 ClassId, AdvancementId, RequiredTags, BlockedTags를 검사한다. 높은 Priority,
   높은 조건 수(Specificity), 사전순 ProfileId로 결정해 배열 재정렬에도 동일한 결과를 얻는다.
3. ProfileId 누락/중복, 잘못된 자원 속성, 중복/무효 입력 태그는 후보에서 제외한다.
   일치 프로필이 없으면 native 5개 입력과 HP/MP로 안전하게 돌아간다.
4. bOverrideSkills/bOverrideResources=false는 기본 구성 상속, true + 빈 배열은 명시적으로 숨김이다.
5. 프로필 교체/Pawn 교체/메뉴 열기·닫기/EndPlay에서 눌린 UI 입력을 원래 Pawn과 원래 태그로 해제한다.
   이전 슬롯 인덱스를 새로운 프로필 태그로 해석하지 않는다. 교체 시 스킬 버튼과 전용 모듈을 정리한다.
6. 속성 델리게이트는 현재 자원과 공격력/방어력에만 연결하고 교체/종료 시 제거한다.
   GAS/성장/identity 이벤트는 다음 tick에 갱신을 합친다. 쿨다운 중에만 0.2초 타이머가 실행된다.
7. 자원은 실제 AttributeSet 존재 여부, 유한한 값, 양수 Maximum을 검사한다. 비율은 0..1로 제한한다.
   스킬 아이콘은 비동기로 로드하며 프로필 변경/위젯 종료 시 이전 요청을 취소한다.

InputHint는 표시 문구다. 키 재바인딩 기능이 아니므로 입력 설정을 변경할 때 함께 갱신하거나,
추후 Enhanced Input 기반 키 표시 어댑터를 연결해야 한다. 클래스 ID 대신 번역 가능한 DisplayName을 화면에 사용한다.

## 에셋 제작/교체 방법

- 기본 `/Game/UI/DA_ProjectJUIProfiles`는 `/Game/UI/DA_ProjectJUIDefault`를 참조한다.
  기본 프로필은 gameplay의 실제 입력/HP/MP를 상속하며 허구의 직업/분노/펫 시스템을 만들지 않는다.
- 새 Data Asset의 클래스는 `Project_JCharacterUIProfile`. 고유 ProfileId, DisplayName, 조건, 우선순위를 지정하고
  Catalog.Profiles에 추가한다. 프로필 자체를 PlayerState나 캐릭터 클래스 데이터에 넣지 않는다.
- Skills는 실제 InputTag, Label, 선택적 InputHint와 Icon을 지정한다. 프로필 표시만으로 능력이 지급되지 않는다.
- Resources는 실제 AttributeSet의 Current/Maximum을 선택한다. AttributeSet이 없는 캐릭터에는 표시되지 않는다.
- 전용 게이지/가이드 Widget Blueprint의 부모는 `Project_JCharacterHUDModule`.
  `PresentCharacter(Context, Resources)` 이벤트에서 전달받은 상태를 그린다. 구독을 추가하는 모듈은 NativeDestruct에 정리해야 한다.
- 공통 화면은 PlayerUIComponent.ScreenClass, 행은 ItemWidgetClass/InventoryTileWidgetClass,
  스킬 버튼은 SkillWidgetClass로 교체한다. 선택적 이름은 기존 문서와 아래 추가 목록을 따른다.
- 추가 바인딩: ResourcesBox(UVerticalBox), CharacterDetailsLabel(UTextBlock), CharacterModuleHost(UVerticalBox), SkillIcon(UImage).
  InventoryList/EquipmentList는 UListView 호환이며 native `Project_JInventoryListView` 또는 `Project_JInventoryTileView`를 사용한다.
- 기본 TileView는 가상화된 격자, 장비는 가상화된 목록이다. 드래그/우클릭/더블클릭은 동일한 ViewModel 요청을 사용한다.
  기본 타일 BP는 `WBP_ProjectJItemTile`, 기존 행 BP는 `WBP_ProjectJItemRow`다. 사용자 스킨은 ItemLabel/Icon/Frame을 제공할 수 있다.
- `/Game/UI`는 cook 포함 디렉터리. Catalog는 프로필과 모듈 클래스를 hard reference한다.
  외부 디렉터리에 옮긴 아이콘/스킨 및 soft reference는 실제 cook 검증이 필요하다. 전체 패키징 완료를 뜻하지 않는다.

신규 기본 에셋 생성은 `Scripts/Editor/Create-MinimalInventoryRow.py`로 재현한다.
이미 존재하는 에셋은 덮어쓰지 않는다. 기본 에셋은 이 작업에서 생성한 최소 표현이며 직업별 완성 아트 스킨은 추후 임포트한다.

## 캐릭터별 창 위치

LocalPlayerSubsystem은 `ProjectJ_UI_Player_<ControllerId>` 로컬 SaveGame에 작은 설정만 저장한다.
키는 Character GUID/ProfileId이며 AccountId, 캐릭터 이름, 아이템이나 GAS 핸들은 저장하지 않는다.
CharacterId owner-only 복제에 OnRep 이벤트를 추가해 늦게 도착한 identity도 갱신한다.
ID가 없으면 저장 키를 만들지 않고 다른 캐릭터 설정을 공유/덮어쓰지 않는다.

제목 드롭 때 이동 가능 영역 대비 정규화된 좌표를 저장한다. 뷰포트/DPI가 바뀌면 창 크기를
기본 크기와 화면 크기 중 작은 값으로 맞추고 위치를 다시 계산한다. 매 프레임 계산은 화면 크기 변경 감지만 하며,
아이템·스탯·스킬 데이터를 매 프레임 읽지 않는다.

저장 시 현재 변경 키만 디스크의 최신 설정과 합친다(동일 프로세스 PIE 클라이언트 분리).
실패한 저장은 메모리에 남기고 종료 때 재시도하며 로그를 남긴다. 여러 독립 프로세스의 동시 저장 잠금,
클라우드 동기화, 전체 HUD 레이아웃·크기 편집은 아직 구현하지 않았다. 임시 PIE Character GUID는 매 접속 변경될 수 있어,
그 경우 PIE 재시작 간 위치 복원은 실제 영구 캐릭터 저장/로그인 ID를 사용하는 경우와 다르다.

## 검증 기록

로컬 증거 위치는 `Saved/Validation/CharacterUI_20261010`이다. Saved 증거는 Git 저장 대상이 아니다.

| 검사 | 결과/증거 |
|---|---|
| 직접 UBT Editor/Game Development | `BuildEditorFinal.log`, `BuildGameFinal.log`: Succeeded. 실행 중인 에디터/빌드가 없을 때 순차 실행 |
| 최종 자동화 | `TestsFinal/index.json`: 성공 16, 경고/실패 0, exit 0. UI 6개 + RuntimeOwnership + Equipment + LocomotionContinuity + EarlyTransition |
| 프로필 선택 | 직업/전직/태그·blocked·우선순위·배열 순서 독립·중복 ID/입력·빈 override·빈 Catalog 검사 |
| 실제 속성 자원 | 실제 GAS 값, 최대값 0, AttributeSet 누락, ASC 누락, 초과 비율 제한 검사 |
| 저장 | 캐릭터·프로필 키 분리, 무효 identity, 직렬화/역직렬화, 범위 제한·비유한 좌표·버전 거부 검사 |
| 실제 파일 재로드 | 에디터 종료 후 새 commandlet 프로세스에서 GUI가 저장한 `.sav` 로드 및 native Save 클래스 역직렬화 확인. `LayoutDiskReload.log`: PROJECT_J_LAYOUT_DISK_LOAD_OK, exit 0 |
| 기본 에셋 | 새 프로세스에서 Tile BP/CDO의 compact 설정, Catalog/Default 프로필 연결 검사 |
| 실제 GUI 조작 | UE5.8 MCP로 한 클라이언트 PIE 시작. Computer Use로 격자→Weapon 장착, 장비→빈 가방 해제, Head 오드롭 무변경 확인 |
| 클릭/휠 | 타일 우클릭·타일 더블클릭 장착, 장비 목록 더블클릭 해제, 장비 Mount까지 휠 확인 |
| 창 이동·재오픈 | 제목 이동 후 Esc/I로 재오픈해 위치·장비 상태 유지 확인. `Layout_Reopen.png`, 실제 `.sav` 파일 생성 확인 |
| 화면 크기 | 창 캡처 1920×939 → 3440×1400 크기 변경 후 메뉴가 화면 안에 있고 HUD/스킬바 위치 보정 확인. `Layout_Resized.png` |
| 정리 | StopPIE, 시작 전 PIE 설정 원복 후 MCP 재조회(`PIESettingsBefore/After.json`). 미저장 에셋 없음 확인 후 Editor 정상 종료 |

장착 검증 화면은 `Grid_Equipped.png`. 게임 콘솔은 PIE 전용 테스트 인스턴스 **prepare**에만 사용했고,
장착/해제 자체는 마우스로 실행했다. 별도 GASP 에디터와 동시에 실행하지 않았다.

현재 실제 PIE 로그는 **Character data registry is empty**를 보고한다. 따라서 실제 화면에서는 기본 프로필을 검증했고,
다중 직업 선택은 자동화의 context fixture로 검증했다. 클래스/전직 정의를 레지스트리에 등록하고 직업별 Profile/Widget Blueprint를
작성한 뒤 실제 전직·무기/모드 변경의 화면 검증을 추가해야 한다. 직업별 완성 스킨을 이미 연결했다고 해석하지 않는다.

직렬화/필드 검사는 C++ 자동화로, 실제 디스크 저장은 GUI 이동과 파일 생성·새 프로세스 로드로 확인했다.
Python에서 비공개 설정 필드와 C++ 전용 메모리 API를 읽으려던 보조 검사는 접근/API 제한으로 실패해,
지원되는 디스크 로드 API만 사용하도록 검사 스크립트를 수정했다. 제품 설정 필드의 접근 권한은 확대하지 않았다.
영구 Character ID를 이용한 실제 재로그인 UI 복원, 많은 가방 아이템의 휠/부하,
모든 해상도, 패킷 지연/재접속, 전용 모듈 BP의 게임 데이터 구독, 실제 스킬 쿨다운 발동·만료, 전체 cook/패키징은 제외했다.

## 후속 우선순위

1. 직업 기획/실제 UI 에셋을 프로필에 연결하고 긴 이름·색각·720p/1080p/울트라와이드·입력 설정 변경을 검증.
2. 툴팁 스탯 비교와 정렬/검색/필터는 Compact HUD 단계에서 구현했다. 추가 스탯/번역/입력 사례를 검증하고 서버 가방 재정렬은 별도 계약으로 추가.
3. 대상/파티/버프 ViewModel. 소유자 교체, 거리/가시성, 만료 시간, 서버 복제 범위를 먼저 정의.
4. 거래·판매·분할·창고 기능은 작업별 서버 계약을 추가. 현재 장비 RPC를 재사용해 권한 경계를 넓히지 않음.
5. 게임패드/다중 메뉴 단계에서 CommonUI 입력 라우터를 검토. UIOnly 메뉴와 여러 모달의 입력 차단 소유권을 통합.

EarlyTransition의 6개 누락 Notify 복구 및 GASP 비교는
`../Animation/Diagnostics/EarlyTransition_Recovery_2026-10-10.md`를 따른다.
이는 작성된 구간의 조건부 전환 복구이며 FPS 향상이나 모든 애니메이션 버벅임 제거를 보장하지 않는다.
