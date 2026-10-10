# 캐릭터 UI 전환·키보드 조작·연속 갱신 검증

기준 커밋은 `20f785e2`다. [이전 저장·비교 계약](Reliability_Comparison_And_Load_2026-10-10.md)을 유지하며 소스 전환, 키보드 대체 조작과 연속 갱신 측정을 보강했다.

## 소스 수명과 구조

PlayerUIComponent는 PlayerState, ASC, CharacterId를 함께 비교한다. 같은 PlayerState에서 CharacterId만 바뀌어도 메뉴·누른 스킬·대상·검색·분할·합치기를 정리하고 소스를 다시 연결한다. PlayerState가 없으면 이전 자원·경험치·상태 효과를 남기지 않는다. Pawn 교체에서는 임시 조작을 정리하되 같은 PlayerState의 인벤토리 소유권은 유지한다.

InventoryEntry의 SourceRevision은 분할·합치기·드래그 스냅샷에도 복사된다. Unbind 후 같은 모델과 같은 아이템 GUID로 돌아와도 과거 항목은 장착·사용·분할·합치기·퀵슬롯 등록에 사용할 수 없다. 마우스와 Enter는 같은 ViewModel Activate 검증을 거친다. 서버의 최종 소유권·수량·잠금·쿨다운 검사 계약을 유지한다.

HUD ResetTransientInteraction은 이 화면에 속한 아이템/퀵슬롯 드래그만 취소한다. 스냅샷, 검색 지연 타이머, 알림, 선택과 스크롤을 비운다. 다른 LocalPlayer의 드래그를 임의로 취소하지 않는다. 메뉴 닫기는 기존 입력 차단을 반환하고 종료 경로는 타이머·delegate를 해제한다.

UI 소스 수명 보강이며 서버 세션 재인증, 소켓 복구, DB 로딩 완료 신호를 구현한 것은 아니다. CharacterId와 인벤토리 복제가 분리되는 서비스에서는 서버의 캐릭터 로딩 상태/세대 계약을 추가해야 한다.

## 키보드 조작

| 입력 | 동작 |
|---|---|
| 가방/장비 창 열기 | 기존 선택 또는 첫 항목에 포커스 |
| 방향키 | TileView 가상화 목록 탐색·선택 |
| Enter | 선택 항목 사용/장착/해제, 반복 키 재요청 방지 |
| Shift+Enter | 가방 스택 분할 |
| Ctrl+M | 소스 선택 → 대상 선택 후 다시 눌러 합치기 |
| Ctrl+F | 가방 검색 포커스 |
| Tab / Shift+Tab | 포커스 가능한 버튼·입력 사이 이동 |
| Escape | 드래그 취소, 검색 포커스 반환, 최상위 창 닫기, 게임 입력 복귀 |

분할 창은 수량 입력에 포커스를 주고 다른 창과 메뉴 버튼을 비활성화한다. 입력의 Enter로 확정, Escape로 취소한다. 검색·수량 입력 중 아이템 단축 동작을 처리하지 않는다. 버튼은 focusable이며 목록 선택은 배경색으로 표시한다. UIOnly 모드에서 동작하여 이동·전투를 함께 발동시키지 않는다.

데이터/요청은 ViewModel에 남고 스킨은 기존 optional named widget 계약을 따른다. 새 에셋이 InventoryList/EquipmentList, ItemFrame, BodyBox 계약을 유지하면 같은 검증·탐색을 사용한다. 완전한 게임패드 매핑·스크린리더와 임의 스킨의 Tab 순서는 별도 QA가 필요하다.

## 빌드와 자동화

증거는 `Saved/Validation/UIContinuity_20261010`에 보관한다. 에디터를 한 개만 실행했고 직접 UnrealBuildTool.exe를 사용했다. `BuildEditorGC.log`, `BuildGame.log`는 성공이다.

`TestsFinal/index.json`: UI/RuntimeOwnership/PlayerMaturity 39개, 성공 38 + 기존 탈것 경고 성공 1, 실패/미실행 0. DetachedEntryGeneration은 소스 해제/복원, 과거 분할·활성화·합치기 거부, 100회 재연결의 행 개수, 살아 있는 모델에서 분리된 행의 GC 회수를 검사한다. 엔진 초기 자체 UnifiedError 테스트의 Condition failed 로그는 이전 실행에도 있으며 프로젝트 테스트 실패와 구분한다.

## 실제 네트워크 화면

MCP floating PIE Client 1/2, 단일 프로세스, 송신 150~250ms/손실 10%, 수신 0이다. `PIEBaseline.json`과 종료 후 `PIEAfter.json` 응답이 같다. 재시작한 에디터의 현재 설정을 기준으로 기록했다.

Computer Use로 확인한 동작:

- Enter 포션 사용: ‘사용 확인 중’→‘완료’, 체력 50→75, 수량 10→9 (`UseCompleted.png`).
- Shift+Enter 분할: 수량 포커스와 배경 비활성화 (`SplitModal.png`). 확정 후 9→5+4.
- 방향키와 Ctrl+M 합치기: 5+4→9, 항목 3→2.
- 마우스 아이템 드래그 순서 교환, 응답 후 완료 (`BagSwap.png`).
- 가방 헤더 드래그와 원위치 복원, 80개 가방 휠 스크롤 (`WheelScroll.png`).
- 다른 Client 2 가방 0개, 체력·마나 100/100 유지 (`PeerEmptyBag.png`).

PIE 종료 후 Alt+F4로 에디터를 정상 종료했다. `Editor.log`의 LogExit와 엔진/Live Coding 프로세스 부재를 확인했다. 이번 실행은 보안 팝업 없이 진행됐고 에셋·레벨을 저장하지 않았다. 실제 장비 후보 hover 비교, 게임패드 체감, 검색/Tab의 모든 조합은 이번 관측 범위에 포함하지 않는다.

## 패키지 연속 갱신

`Validate-CookedUI.ps1 -Profile -HeavyWorkload -DynamicWorkload`는 fresh UserDir의 standalone authority에서 실행한다. Shipping에서는 fixture를 실행하지 않는다. 1,000항목·UI 버프 24개·창 4개·아이템 퀵슬롯 10개를 준비한 뒤 CSV 600프레임을 기록한다.

5Hz로 50회 진행하며 매번 8항목 수량과 검색/정렬을 바꾼다. 총 10회 항목 제거/추가, 5회 장비 창 토글을 수행한다. 실제 50회/1,000행 marker와 smoke 성공을 요구한다. 마지막 smoke는 PlayerState 연결을 잠시 끊고 복원해 메뉴/목록/pending/효과가 비워지고 오래된 항목이 되살아나지 않는 것을 검사한다.

이 부하는 로컬 authority delta다. 네트워크 복제량, 전투, 군중, 장시간 메모리 검증으로 해석하지 않는다. 100회 연결/GC 테스트도 장시간 메모리 프로파일을 대체하지 않는다. 같은 새 실행 파일의 기본 HUD와 동적 부하를 비교하며 변경 전후 개선율을 주장하지 않는다.

`Package.log`: cook/stage/package 성공, 종료 코드 0. `UIContinuity_Idle_20261010/Results.json`과 `UIContinuity_Dynamic_20261010/Results.json`: 720p/1080p 총 4회 성공, 종료 코드 0, 게임 로그 오류 0. 동적 부하 두 실행 모두 실제 50회 marker와 소스 복원 smoke가 성공했다. 복원 행 수 1002는 1,000개 부하 항목에 smoke가 추가/분할한 두 행을 더한 값이다.

같은 실행 파일 SHA256은 `B5A92D1357CAEE074920D8B5FD5C9641786954E26711D293BE95487E164AABB4`다. `PerfSummary.json`은 실행 파일/CSV hash와 원본 경로를 연결한다. `Summarize-UIProfile.py`는 기본 240프레임/동적 600프레임을 요구하고 첫 120프레임을 제외한다.

| 조건 | 해상도 | Engine UI 평균 / p95 |
|---|---|---:|
| 기본 HUD | 1280×720 | 0.213 / 0.293 ms |
| 기본 HUD | 1920×1080 | 0.213 / 0.327 ms |
| 1,000행·24효과·연속 변경/필터/창 토글 | 1280×720 | 0.607 / 0.915 ms |
| 1,000행·24효과·연속 변경/필터/창 토글 | 1920×1080 | 0.613 / 0.957 ms |

Development/D3D12/RenderOffscreen/t.MaxFPS=60의 짧은 관측이다. Engine UI는 HUD만의 비용이 아니다. 기본 120프레임과 동적 480프레임의 표본 길이가 다르며 대량 최초 구성은 캡처 전이다. 실제 네트워크 수신 burst와 장시간 GC를 포함하지 않는다. 전투 최적화 완료 또는 최대 FPS를 주장하지 않는다.

## 후속 확인과 근거

직접 리뷰는 글자 크기·배치·투명도·조작감과 실제 아트 가독성에 집중하면 된다. 서비스 접속 흐름이 준비되면 실제 단절/재로그인/맵 이동, 캐릭터 로딩 완료와 복제 순서, 장시간 전투/GC 측정을 추가한다. 거래·창고·파티·채팅·영구 저장은 별도 gameplay 범위다.

Epic의 [UMG 최적화 지침](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine?lang=en-US)과 [이벤트 기반 갱신](https://dev.epicgames.com/documentation/unreal-engine/driving-ui-updates-with-events-in-unreal-engine?lang=en-US)을 참고했다. 기존 이벤트 묶음 갱신과 가상화 목록을 유지하며 상시 UI Tick을 새로 추가하지 않았다.
