# Compact MMORPG HUD와 gameplay 연결 — 2026-10-10

후속 스택 조작·UI 키 설정·상태 효과와 실행 검증은 [조작 확장 문서](Extended_Interaction_And_Validation_2026-10-10.md)를 따른다. 아래 검증 수치는 Compact HUD 단계의 기록이다.

사용자가 제공한 검은사막 상세 캡처 5장을 기준으로, 큰 안내 문구 중심의 초기 화면을 작은 전투 HUD와 독립 창으로 확장했다.
검은사막 이미지/아이콘은 참고 자료이며 프로젝트 아트로 복제하지 않았다. 아이콘이 없는 데이터는 짧은 이름으로 표시한다.
이 문서가 현재 구현 기준이다. [최초 구현](MMORPG_UI_Architecture_2026-10-10.md)과
[캐릭터 프로필](Character_UI_Profiles_2026-10-10.md)의 이전 검증 기록은 해당 단계의 기록으로 유지한다.

## 화면과 조작

| 위치/창 | 구현 | 조작 |
|---|---|---|
| 왼쪽 위 | 레벨, 실제 자원 게이지, 경험치/다음 레벨 | 설정에서 자원 수치 표시 변경 |
| 아래 중앙 | 스킬/아이템 혼합 퀵슬롯 10개, 수량, 키, 쿨다운 | 1..9, 0. 잠금 해제 후 드래그 등록/교환, 우클릭 비우기 |
| 오른쪽 아래 | 실제 기능으로 연결된 가방·장비·의뢰·설정 버튼 | 클릭 또는 I/K/O/U |
| 오른쪽 위 | 북쪽 고정 미니맵, 플레이어 방향, 추적 목표, 의뢰 진행 | 지도 위 휠 확대/축소, 의뢰 헤더 접기 |
| 가방 | 가상 격자, 검색, 표시 정렬, 전체/장비/소모품 필터, 툴팁 | 휠, 우클릭/더블클릭 사용·장착, 장비창으로 드래그 |
| 장비 | 실제 장착 슬롯, 공격력·방어력, 스탯 비교 | 가방으로 드래그 해제, 우클릭/더블클릭 해제 |
| 의뢰 | 진행, 추적 켜기/끄기, 완료 후 경험치 보상 | 보상은 서버 검증 후 한 번만 지급 |
| 설정 | 스킬 등록 창, 퀵슬롯 잠금, 수치 표시, HUD 크기, 기본값 복원, 가방/장비 동시 열기 | 제목 드래그로 각 창 이동 |

메뉴는 게임을 일시 정지하지 않는다. 메뉴가 열린 동안 UIOnly 입력으로 커서와 이동/시점 차단을 관리하고,
마지막 창을 닫으면 UI가 획득한 차단을 해제한다. 클릭한 창을 앞으로 올린다. Escape는 위쪽 창부터 닫는다.
검색 중에는 I/K/O/U가 글자 입력이며 첫 Escape가 검색 포커스를 나가고 다음 Escape가 창을 닫는다.
숫자키 퀵슬롯은 메뉴가 닫힌 게임 상태에서 사용한다. 스킬 버튼의 눌림은 메뉴/프로필/Pawn 교체와 종료 시 원래 Pawn/태그에 해제한다.

HUD 기본 논리 크기는 퀵슬롯/가방 셀 46, 본문 12, 지도 168이다. DPI 설정이 적용되므로 물리 픽셀과 같지 않다.
사용자가 주요 전투 HUD(레벨·자원·퀵슬롯) 크기를 0.75..1.5로 조절할 수 있다. 지도/창/경험치 자체 크기는 이 배율의 대상이 아니다.
큰 디버그 설명을 전투 HUD에 상시 표시하지 않는다.
채팅/메신저/길드/우편은 실제 데이터와 통신 기능이 없으므로 동작하지 않는 버튼을 추가하지 않았다.

## 공개 자료와 적용 판단

상용 게임의 공개된 사용 방식과 Epic의 공개 기술 문서를 참고했다. 상용 MMORPG의 내부 소스 구조를 확인한 것은 아니다.

| 공식 자료 | 적용 |
|---|---|
| [검은사막 인터페이스](https://www.kr.playblackdesert.com/ko-KR/Wiki?wikiNo=15) | 전투 정보와 상세 창을 나누고 작은 격자·개별 창 이동을 제공 |
| [FFXIV HUD Layout](https://na.finalfantasyxiv.com/uiguide/know/know-hud/hud-layout.html) | 위치/크기 환경설정을 gameplay 저장과 분리 |
| [FFXIV 직업 HUD](https://na.finalfantasyxiv.com/uiguide/know/know-hud/hud_jobhud_settings.html) | 직업별 자원과 전용 표현을 프로필/모듈로 분리 |
| [FFXIV 아이템 핫바 명령](https://na.finalfantasyxiv.com/lodestone/playguide/db/text_command/0992946f68f/) | 스킬과 아이템을 같은 단축 슬롯에서 다루되 서로 다른 사용 경로로 연결 |
| [Lyra Inventory/Equipment](https://dev.epicgames.com/documentation/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine) | 보유 아이템과 장착 상태를 구분하고 서버 소유권 유지 |
| [Epic UMG 최적화](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine?lang=en-US) | 매 프레임 속성 바인딩 대신 이벤트 갱신, 가상 목록, 비동기 아이콘 |

## 코드 경계와 상태 소유권

```text
Local PlayerController
  PlayerUIComponent → 프로필 선택 / 로컬 설정 / 입력 / 표시 상태
    PlayerHUDWidget → HUD / 독립 HUDWindow / MinimapWidget
    InventoryViewModel → 가상 목록 / 검색·정렬·필터 / 장비 비교
                     ↓ 인스턴스 ID 또는 정의된 QuestId 요청
PlayerState
  Inventory + EquipmentManager + ASC + Progression + QuestComponent
                     ↓ 서버 검증, owner-only 상태 복제, 이벤트 갱신
```

- UI는 아이템 지급, 보상액, 능력 습득, 장비 스탯을 결정하지 않는다. dedicated server에는 화면을 생성하지 않는다.
- `Project_JUIModels`는 typed 퀵슬롯 바인딩과 표시 snapshot, 작은 로컬 설정, 스타일 Data Asset을 정의한다.
  Skill은 InputTag, Item은 ItemId로 저장한다. 수명 짧은 GAS handle이나 복제된 아이템 stack을 저장하지 않는다.
- 인벤토리 ViewModel은 메뉴가 닫혀도 연결을 유지해 아이템 퀵슬롯 수량/사용 상태를 갱신한다.
  Pawn/PlayerState 교체와 종료에서 델리게이트·요청·아이콘을 정리한다. 초기 문서의 ‘메뉴 닫힘 시 전체 구독 해제’는 현재 동작이 아니다.
- 스킬/아이템 사용 가능 상태는 실제 소유 능력/아이템과 서버 쿨다운에서 만든다. 품절 시 바인딩은 유지되고 수량 0/비활성이다.
- 검색은 최대 128자, 0.15초 지연 적용, 이름/ItemId 대소문자 무시 검색이다. 정렬은 표시 순서이며 서버 가방 순서를 바꾸지 않는다.
  동일 표시 이름은 GUID로 안정 정렬한다. 필터링으로 실제 아이템이나 기존 행 객체를 삭제하지 않는다.
- 툴팁은 설명·상태와 정적으로 정의된 주요 4개 능력치의 현재 장비 대비 차이를 표시한다.
  모든 GameplayEffect의 최종 전투 수치를 계산하는 시스템으로 해석하지 않는다.

### 실제 gameplay 확장

**소모품:** `Project_JConsumableDefinition`의 회복량/공유 쿨다운을 서버에서 사용한다.
요청 GUID와 소유 인스턴스 ID를 검증하고 잠김·장착·사망·이미 가득 찬 자원·효과 없음·쿨다운을 검사한다.
유한한 양수 회복량만 허용하고 부족한 자원만큼 제한한 즉시 GAS 효과를 적용한다.
1개 소모와 쿨다운 예약을 콜백 전에 처리하며 재진입을 막는다. RPC는 초당 20개 제한,
최근 64개 응답 ID로 중복 요청을 처리한다. UI 대기는 5초 후 해제하며 자동 재전송하지 않는다.
이 receipt cache는 메모리 내 보호이며 재접속 이후의 영구 중복 처리 저장소가 아니다.

**경험치:** Progression의 owner-only int64 경험치와 서버 `GrantExperience`를 추가했다.
음수/overflow를 거부하고 여러 레벨 상승을 처리한다. 기본 필요 경험치는 100×현재 레벨이며
설정 배열로 교체할 수 있다. 기본 최대 레벨은 100이다. Snapshot v2는 이전 v1을 수용한다.
클라이언트가 보상 수치를 임의로 보내는 경험치 RPC는 없다.

**의뢰:** PlayerState의 `Project_JQuestComponent`는 정의된 QuestId와 owner-only 진행/추적/보상 상태를 관리한다.
서버 `Accept`/`RecordObjective`만 진행을 바꾸고 클라이언트는 기존 의뢰의 추적/보상 요청만 보낸다.
최대 128개 상태, 초당 20개 요청 제한, 중복 ID 검증과 한 번 보상/재진입 보호를 적용했다.
복원 Snapshot v1은 이미 존재하는 진행을 덮어쓰거나 보상을 다시 지급하지 않도록 검증한다.
기본 ‘첫걸음’은 서버가 자동 수락하며 수평 이동 20m 완료 후 경험치 100을 지급한다.
0.5초 샘플의 이동 중 수평 거리를 누적하고 1000cm 이상 점프와 Pawn 변경을 제외한다.
이는 연속 경로 길이 측정이나 모든 소규모 텔레포트 판별이 아니다. 전투/상호작용 의뢰는 신뢰된 서버 gameplay 이벤트를 연결해야 한다.

Snapshot은 저장 어댑터용 API이다. **XP/의뢰/소모품 쿨다운의 영구 DB·로그인 복원 연결은 아직 없다.**
실제 MMORPG 운영에서는 저장 transaction, 보상 receipt 영속성, 서버 시간 정책, 재접속 복원 순서를 추가해야 한다.

## 지도와 최적화

- `Project_JMapDefinition`은 LevelName, center/half extent, 선택적 soft texture, 정적 영역, 표식을 정의한다.
  북쪽은 world X+, 동쪽은 Y+이며 UV 변환을 자동 테스트한다. 다른 레벨에는 지정 레벨의 맵을 사용하지 않는다.
- `Bake-PrototypeMapBounds.py`는 `Lvl_ThirdPerson`의 정적 mesh bounds 48개를 한 번 읽어 지도 데이터에 저장했다.
  half extent는 약 8367.5cm다. 실제 지형/통행 가능 지도나 정밀 지도 아트가 아니라 프로토타입 평면도다. 레벨을 저장하지 않는다.
- 플레이어 위치/방향은 0.2초 샘플, 확대 1..8, 지도 영역 clipping을 적용한다.
  선택한 텍스처는 비동기 로드 후 UV 영역을 잘라 사용한다. SceneCapture/매 프레임 월드 액터 검색을 사용하지 않는다.
- 인벤토리/장비는 ListView/TileView 가상화와 안정 행 객체를 사용한다. 재활용 행은 아이콘 load를 취소하고 오래된 완료 콜백을 무시한다.
  퀘스트 창은 변경 이벤트와 사용자 동작 때 갱신한다. 쿨다운 샘플은 활성 쿨다운 중에만 실행한다.
- HUD Tick은 화면 크기 변화/창 배치 검사에 사용한다. 매 프레임 전체 인벤토리나 gameplay 속성을 다시 읽지 않는다.
  Slate 지도 paint와 geometry 작업 자체는 남는다. 실측 FPS 향상, Unreal Insights/Slate 성능 측정 완료를 주장하지 않는다.

## 캐릭터별 구성과 아트 교체

기존 CharacterUIProfile/Catalog의 직업·전직·ASC 태그 선택을 유지했다.
스킬, 실제 자원 속성, 소환/변신 등의 전용 HUD 모듈을 프로필에서 고를 수 있다.
존재하지 않는 분노/펫 시스템을 UI만으로 만들지 않는다. 현 레지스트리가 비어 있어 실제 PIE는 기본 프로필이며,
다중 직업 조건은 자동 테스트로 검증했다. 새 직업을 등록한 뒤 실제 전환 검증을 추가해야 한다.

로컬 설정은 Character GUID/ProfileId별 창 위치, 10개 퀵슬롯, 잠금, 자원 수치, HUD 배율을 저장한다.
`ProjectJ_UI_Player_<ControllerId>` SaveGame에서 좌표/배율/슬롯 유효성을 검사하고 dirty 항목을 최신 파일에 병합한다.
저장 실패 시 dirty 상태를 유지한다. 작은 사용자 편집 때 동기 저장하며 매 프레임 쓰지 않는다.
identity 미도착 시 다른 캐릭터 설정을 공유하지 않는다. PIE의 임시 Character GUID는 실행 간 영구 로그인 ID가 아니다.

| 교체 대상 | 제작 계약 |
|---|---|
| 공통 스타일 | `/Game/UI/DA_ProjectJHUDStyle`: 슬롯/아이템 크기, 본문 크기, 패널 색. 실제 texture/font 아트는 BP 스킨에 적용 |
| 가방/장비 행 | `Project_JItemWidget` 자식 BP. 선택적 ItemLabel, ItemIcon, ItemFrame, ItemQuantityLabel, ItemLockLabel. 짧은 이름과 수량은 별도 레이어 |
| 퀵슬롯 | `Project_JSkillButton` 자식 BP. SkillLabel, SkillIcon, KeyLabel, QuantityLabel, CooldownBar. 화면 SkillWidgetClass 지정 |
| 독립 창 | `Project_JHUDWindow` 자식 BP와 화면 WindowClass. BodyBox(UVerticalBox)에 native 본문이 들어간다. TitleLabel(UTextBlock)로 제목 이동, 닫기 버튼은 RequestClose 호출. BodyBox가 없으면 native 창으로 fallback |
| 전용 캐릭터 패널 | `Project_JCharacterHUDModule` 자식 BP, Profile.CharacterModuleClass의 PresentCharacter 이벤트 |
| 지도 | `/Game/UI/DA_ProjectJMap`의 Texture/좌표/마커를 실제 맵 아트와 맞춤 |
| 전체 화면 | PlayerUIComponent.ScreenClass의 PlayerHUDWidget 자식 BP. 기존 BindWidgetOptional/공개 업데이트·명령 계약을 연결하고 입력/창 동작을 재검증 |

현재 기본 창 본문 배치는 native 코드에서 생성한다. 전체 화면 BP에 임의 레이아웃을 넣으면 모든 창/지도 기능이 자동 연결되는 것은 아니다.
전체 화면 스킨을 새로 구성할 때 추가 연결이 필요하며, 개별 창/행/스킬 스킨 또는 스타일 교체는 gameplay RPC 변경 없이 가능하다.
외부 폴더로 옮긴 soft reference 아트는 cook 포함과 패키징을 검증해야 한다.

`Create-MinimalInventoryRow.py`는 기본 행/타일/프로필을, `Create-CompactHUDData.py`는 스타일/지도/첫걸음/HP·MP 물약을 생성한다.
기존 동일 이름의 에셋은 덮어쓰지 않는다. gameplay 기본 물약은 회복량 25, 공유 쿨다운 10초, 최대 stack 99다.

## 검증과 재현

증거는 Git 제외된 `Saved/Validation/CompactHUD_20261010`에 있다.

| 확인 | 결과 |
|---|---|
| 직접 UBT Editor/Game Development | 최종 지도 입력 수정 후 `BuildEditorMapInput.log`, `BuildGameMapInput.log`: Succeeded. 실행 중인 UE/빌드 프로세스가 없는 상태에서 순차 실행 |
| 기능 자동 테스트 | `TestsPolish/index.json`: 31개, 성공 30 + 경고 성공 1, 실패/미실행 0. 기존 Mount.PossessionAndBlockedExit의 animation profile 누락 경고이며 새 UI 테스트는 경고 없음 |
| 신규 UI 자동 테스트 | 경험치 overflow/다중 level/snapshot, 소모품 서버 조건/쿨다운, 의뢰 중복 보상/복원, 설정 직렬화/무효값, 지도 좌표, 검색/필터, 1000개 인벤토리의 안정 projection |
| 마지막 스킨 계약 이후 UI 회귀 | `TestsCompleteUI/index.json`: UI 13개 모두 성공, 경고/실패/미실행 0 |
| 실제 PIE 조작 | MCP 시작/종료 + Computer Use. 물약 등록→슬롯 교환→잠금→숫자키 사용: HP 50→75, 수량 10→9, 쿨다운 표시 |
| 실제 의뢰/성장 | 서버 fixture로 목표 완료 후 UI 보상 클릭: 레벨 1→2, 보상 버튼 완료/비활성, tracker 정리. `Quest_Claimed.png` |
| 가방/입력 실제 조작 | fixture 80개 항목의 휠 스크롤·수량 표시, ItemID 검색, 검색 중 첫 Escape로 포커스 해제/두 번째로 창 닫기 확인. 설정 창의 7개 버튼이 패널 안에 표시됨 |
| 장비/독립 창 실제 조작 | Greatsword를 가방→Weapon 슬롯으로 드래그하여 장착 완료, 장비→가방으로 드래그하여 슬롯 해제 확인. 가방 제목 이동 후 닫기/재열기 위치 유지 확인 |
| 지도 최종 입력 회귀 | native 지도 SizeBox/UserWidget의 hit test visibility를 수정한 뒤 휠 확대 및 지도 tooltip 확인. `EditorMapInputUI.log` |
| 초기 화면/종료 | `HUD_Overview_Final.png`. PIE 종료 후 기존 1-client/PIE_Client/단일 프로세스 설정 유지 확인, 에디터를 정상 종료하고 관련 프로세스 잔류 없음 확인 |

검증용 콘솔 `UIPrototypeTest prepare`는 PIE에서만 HP/MP를 낮추고 물약 2종을 지급한다.
`fill`은 해당 fixture 항목을 최대 80개까지 채워 스크롤을 확인하고, `finish`는 첫걸음 목표를 완료한다.
`stop`은 이 fixture가 만든 GUID만 제거한다. Editor build의 PIE world 검사로 실제 서버/게임 지급을 막는다.
PIE 종료 후 해당 임시 상태는 사라진다. 장비 검증은 기존 `EquipmentClientTest prepare`의 일시 Greatsword를 UI로 장착/해제한다.

전체 cook/패키징, 720p에서 모든 창을 동시에 연 배치, 장시간 성능 측정, packet loss/지연/재접속,
전용 직업 아트, 게임패드/리바인딩 편집기, 영구 캐릭터 로그인 복원은 완료 범위가 아니다.
실제 서비스 확장 우선순위는 영구 저장/보상 transaction, 입력 remap·접근성·좁은 화면 배치,
대상/파티/버프 ViewModel, 그리고 거래·분할·창고의 개별 서버 계약이다.

EarlyTransition은 [별도 복구 보고서](../Animation/Diagnostics/EarlyTransition_Recovery_2026-10-10.md)를 따른다.
이번 UI 변경은 애니메이션 타이밍을 추가 변경하지 않았으며 모든 버벅임 제거/FPS 증가를 보장하지 않는다.
