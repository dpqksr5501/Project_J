# MMORPG UI 조작 확장과 실행 검증 — 2026-10-10

직접적인 화면 피드백 없이 진행하도록 승인받은 기능 확장이다. [Compact HUD](Compact_HUD_And_Gameplay_2026-10-10.md)의 화면·아트 교체 계약을 유지하며, 가방 조작·UI 키 설정·상태 효과 표시·입력 수명을 보강했다.
이 문서는 해당 확장의 기록이며, 이전 문서의 빌드/테스트 수치는 당시 기록으로 유지한다. 이후 저장 재시도·백업·버전 이관, 장비 정책에 맞는 비교와 부하 검증은 [UI 저장과 장비 비교](Reliability_Comparison_And_Load_2026-10-10.md)를 따른다.

## 새 조작

| 기능 | 조작과 동작 |
|---|---|
| 스택 분할 | 항목 선택 → 가방의 ‘분할’, 또는 Shift+왼쪽 클릭. 수량 입력 후 확인. 원본에 최소 1개를 남긴다 |
| 스택 합치기 | 원본 선택 → ‘합치기’ → 대상 선택 → ‘합치기’. 또는 Ctrl+드래그. 같은 정의·아이템 레벨만 허용하고 최대 스택까지 이동, 나머지는 원본에 유지 |
| 가방 위치 교환 | 가방 항목을 다른 항목으로 드래그. 검색·분류·이름 정렬을 해제한 전체 보기에서 사용 |
| 작업 취소 | 합치기는 ‘선택 취소’, 분할은 취소/닫기. 가방 닫기·캐릭터 변경 시 미완료 선택을 폐기. 드래그 중 Escape는 드래그 취소 |
| UI 키 설정 | 설정 → 단축키 설정 → 항목 클릭 → 새 키. Escape 취소. 겹치는 UI 키는 서로 교환 |
| 실패/진행 표시 | 가방 상태 문구와 4초 HUD 알림. 메뉴가 닫혀 있어도 물약 실패·서버 응답 지연 등을 확인 가능 |
| 강화/약화 | 좌측 자원 아래 작은 아이콘, +/− 구분과 중첩, 남은 시간. 설명은 툴팁. 최대 12개 표시, 초과 수 별도 표시 |

가방 이동은 밀집 격자의 항목 순서 교환이다. 빈 슬롯 위치·고정 용량·무게·창고·거래 계약을 새로 정의한 것은 아니다.
화면의 이름 정렬은 표시만 바꾸며 서버 순서나 퀵슬롯 ItemId를 수정하지 않는다. 이름 정렬을 끄면 서버 가방 순서로 돌아간다.

## 서버 가방 계약

- `FProject_JItemInstanceData.BagOrder`는 서버가 관리하는 순서다. InstanceId와 아이템 정의를 변경하지 않으며 FastArray owner-only 복제에 포함된다.
- 분할·합치기·순서 교환은 `FProject_JBagRequest` 하나로 제출한다. 관측한 원본/대상의 수량과 순서를 서버 현재 상태와 비교한다.
  드래그/입력창을 열어 둔 동안 상태가 변경되면 `Changed`로 거부하며 클라이언트에서 먼저 수량을 변경하지 않는다.
- 소유 인벤토리 안의 GUID만 허용한다. 잠금·장착·동일 항목·잘못된 수량/연산·서로 다른 정의/레벨·스택 상한을 검사한다.
- 두 항목을 모두 변경한 뒤 값 snapshot으로 이벤트를 통지한다. 변경 이벤트 중 재진입한 수량/추가/제거/잠금/사용 변경을 막는다.
- 원본을 전부 합치면 GUID를 제거한다. 일부 합치기는 원본 GUID를 보존한다. 분할은 새로운 GUID를 만들고 정의와 레벨을 복사한다.
- 요청은 초당 20개, 최근 receipt 64개를 제한한다. 중복 RequestId는 다시 변경하지 않는다. 영구 보상/거래 receipt가 아니다.
- 분할로 항목을 무제한 생성하지 않도록 인벤토리 4096개 이상에서는 분할을 거부한다. 이는 상품 기획의 가방 용량과 별개인 방어 한도다.
- ViewModel은 공통 pending/5초 timeout을 유지한다. 서버 응답과 복제는 서로 다른 도착 순서일 수 있으며, FastArray delta를 다음 tick에 모아서 현재 상태를 읽는다.

## 키 설정과 입력 수명

`FProject_JUIKeys`는 퀵슬롯 10개 + 가방/장비/의뢰/설정의 14개 키를 갖는다.
`ProjectJ_UI_Player_<ControllerId>` 설정 파일에 로컬 플레이어 기준으로 저장하며 캐릭터 GUID/프로필별 슬롯 구성과 분리한다.
기존 v1 설정은 새 필드가 없으면 기본 키로 읽는다. 중복/허용하지 않는 키 배열은 거부한다.

현재 변경 범위는 **UI/퀵슬롯 키**다. 숫자, I/K/O/U, F1~F12 단일 키를 허용하며 이동·전투·마우스·Escape·콘솔·조합키는 제외한다.
활성 Enhanced Input mapping과 타 Controller/Pawn 키 바인딩을 추가로 확인해 충돌한 키를 거부한다.
mapping 재구축/Pawn 변경 시 자기 바인딩만 제거·재생성하며 충돌한 UI 바인딩은 등록하지 않는다.
현재 화면 버튼으로 창을 여는 경로는 유지한다. 이동/전투의 전체 Enhanced Input remapper와 게임패드 편집기는 후속 기능이다.

키 변경 전 눌린 스킬을 원래 Pawn/입력 태그에 release하고 키 상태를 비운다. 퀵슬롯은 변경된 키를 표시한다.
메뉴에서는 같은 설정으로 창을 열고 닫는다. 검색/수량 입력은 자기 키 입력을 우선하며 키 캡처는 preview 단계에서 처리한다.
앱 비활성화 때 스킬 release/키 초기화를 수행하고 종료 때 Slate/ASC/Input delegate와 timer를 해제한다.

## 상태 효과와 대상 표시

`UProject_JStatusEffectUIData`를 GameplayEffect의 Components 목록에 추가한다.
DisplayName/Description/soft Icon/bDebuff/Priority를 작성한다. UE 기본 TextOnly UIData도 표시할 수 있다.
설명 없는 내부 장비 효과·쿨다운 효과를 전부 버프 아이콘으로 노출하지 않는다.

`UProject_JStatusEffectModel`은 로컬 소유자의 ASC만 읽는다. 실제 효과 handle, GAS stack, 활성/억제 상태와 남은 시간을 사용한다.
추가/제거 이벤트로 갱신하고 UI 메타데이터가 있는 효과가 존재할 때만 0.25초 관측 timer를 유지한다.
무기/캐릭터 교체 시 예전 ASC delegate를 제거한다. 아이콘 위젯은 표시 handle 집합이 바뀔 때 재구성하고 timer마다 새로 만들지 않는다.
유한 효과의 남은 시간은 GAS의 로컬 world time 기준이며 임의 UI countdown을 gameplay 상태로 저장하지 않는다.
무한 효과는 지속 표시, instant 효과는 버프 목록에 남지 않는다. UI에서 효과를 적용/해제할 권한을 제공하지 않는다.

`PlayerUIComponent.SetObservedTarget`은 선택 시스템이 알려 준 actor의 체력 표시 연결 지점이다.
같은 world의 유효 actor/ASC만 읽고 30m·시야·유효 체력 조건을 확인한다. 소멸/사망/시야 이탈/Pawn 교체 시 정리한다.
actor 전체 검색이나 공격 RPC를 추가하지 않았다. 기존 TargetScoring은 advisory 실험용이므로 플레이어 타깃 선택으로 자동 채택하지 않는다.
클릭/Tab 선택, 진영 규칙과 타깃별 상세 버프의 복제는 후속 계약이다. 원격 actor의 미복제 속성을 UI가 새로 얻을 수 있는 것은 아니다.

## 표시와 제작 계약

작은 화면에서는 우측 하단 메뉴를 퀵슬롯 위쪽으로 이동하고 독립 창을 화면 안에 맞춘다.
키 목록은 스크롤하며 긴 상태/분할 설명은 줄바꿈한다. 강화/약화는 색과 +/−를 같이 사용한다.
아이콘은 비동기 로드 중/실패에도 짧은 이름을 표시하고 성공한 뒤 대체한다. 재활용 행의 오래된 완료 콜백은 무시한다.
최종 아트·색상·전체 HUD 자유 배치와 접근성 검수는 별도다. 전체 720p 배치나 성능 향상 수치를 자동으로 보장하지 않는다.

## 검증

로컬 증거는 Git 제외된 `Saved/Validation/ExtendedUI_20261010`에 보관한다. 작업 트리의 기존 콤보/애니메이션 변경은 별도로 유지한다.

| 확인 | 결과/증거 |
|---|---|
| 서버/설정/상태 효과 자동 테스트 | 최종 `TestsCookStripFinal/index.json`: 33개 중 성공 32 + 기존 탈것 경고 성공 1, 실패/미실행 0, commandlet 종료 코드 0 |
| 직접 UBT | `BuildEditorCookStrip.log`, `BuildGameCookStrip.log`: 최종 Editor/Game Development 성공, 종료 코드 0 |
| 실제 UI | MCP floating PIE Client 1 + Computer Use, 1280×720: 체력 포션 10→5+5→10, 가방 드래그 순서 교환, 첫 퀵슬롯 1→F5, Escape 키 변경 취소/기본값 복원, 서버 30초 버프 3중첩/남은 시간 감소/만료 제거 확인 |
| 패키징 | 직접 UBT Game 빌드 후 UAT는 코드 빌드를 생략하고 cook/stage/package/archive 수행. 최종 `PackageFinal.log`: 성공, 종료 코드 0 |
| cooked 실행 | `Saved/Validation/ExtendedUI_CookedFinal_20261010/Results.json`: 1280×720 / 1920×1080 모두 success=1, cooked=1, 종료 코드 0, 실행 로그 오류 0 |

PIE에서만 `UIPrototypeTest prepare/fill/buff`를 사용한다. buff는 게임 속성을 바꾸지 않는 native 정의의 30초/최대 3중첩 표시 효과를 적용한다.
서버에서 동적으로 만든 임의 효과 정의를 네트워크 자산처럼 복제하지 않는다. fixture 외 정상 gameplay에서 이 효과를 부여하지 않는다.

Development 실행의 `-ProjectJUIRuntimeSmoke`는 명시적 opt-in 검증이다. standalone local authority에서만 동작하며 Shipping에서는 실행하지 않는다.
독립 UserDir로 실행해 사용자 설정과 분리한다. cooked UI 에셋, 창 bounds, 스택 보존, 키 저장, 메뉴 입력 복원, 상태 효과 제거를 확인하고 결과 marker와 종료 코드로 완료한다.
운영용 gameplay 명령/RPC로 제공하지 않는다.

Computer Use 검증 중 드래그 준비가 클릭을 소비해 ListView 선택이 갱신되지 않는 문제를 발견했다.
아이템의 마우스 입력에서 소유 ListView 선택을 먼저 갱신하도록 수정하고 직접 UBT 및 실제 분할·합치기를 다시 검증했다.
증거: `Split720.png`, `Merge720.png`, `Keys720.png`, `Buff720.png`, `BuffExpired720.png`, `EditorUIFinal.log`.
PIE 창 크기는 검증 후 원래 3434×1352로 복원했고 키는 검증 전 기본값으로 복원했다. 에디터는 정상 종료했다.

패키지 실행 재현 스크립트는 `Scripts/Validation/Validate-CookedUI.ps1`이다. 새 EvidenceName과 실제 패키지의 내부 Game 실행 파일을 전달한다.
자동 실행은 D3D 렌더링을 사용하는 RenderOffscreen이며 1280×720/1920×1080 두 조건을 순차 실행한다.
이 검사는 화면 경계와 기능 smoke 검증이며 전체 시각 배치·가독성·성능 측정을 대체하지 않는다.

첫 UAT는 기존 staging 폴더의 삭제 접근 권한 오류(102)로 중단됐다. 새 경로에서도 삭제 단계가 거부되어 최종 실행은
비어 있는 `StageFinal` 경로에 공식 `-nocleanstage` 옵션을 적용했다. 기존 staging 폴더의 권한이나 내용을 수동 변경하지 않았다.
최종 산출물은 `Saved/Validation/ExtendedUI_20261010/PackageFinal/Windows`다.

첫 cooked 실행은 UI 기능 marker가 성공했지만 PlayerController Blueprint에 `EquipmentClientTest`, `ProfilingCrowdComponent`
템플릿 참조 오류가 있었다. `WITH_EDITOR` 생성 분기만으로는 cook 제외를 보장하지 않아
`CreateEditorOnlyDefaultSubobject(..., true)`로 변경했다. 에셋을 수동 저장하지 않고 다시 cook한 최종 패키지는 두 해상도 모두 로그 오류 0이다.
검증 스크립트도 marker와 종료 코드뿐 아니라 로그 오류가 없어야 성공하도록 강화했다.

### 짧은 UI 비용 관측

Development / D3D12 / RenderOffscreen / 기본 HUD 대기 상태 / `t.MaxFPS 60`에서 `csvprofile FRAMES=240`과 Slate category를 사용했다.
각 CSV의 초기 120프레임을 제외한 다음 120프레임만 계산했다. 원본 CSV·SHA256·요약은
`Saved/Validation/ExtendedUI_CookedFinal_20261010/PerfSummary.json`에 연결한다.

| 해상도 | Engine UI 게임 스레드 평균 / p95 | UMG SObjectWidget Tick 평균 | SlateUI DrawCall |
|---|---:|---:|---:|
| 1280×720 | 0.211 / 0.310 ms | 0.00368 ms | 19 |
| 1920×1080 | 0.212 / 0.254 ms | 0.00388 ms | 19 |

평균 FrameTime은 두 조건 모두 약 16.667ms지만 60fps 제한을 걸었으므로 최대 성능 수치가 아니다.
Engine UI 범위는 HUD만의 비용으로 분리한 값이 아니며, 짧은 대기 상태 관측이다. 창을 많이 열었을 때,
대량 아이템·많은 버프·군중 전투·네트워크 장애 상황이나 변경 전후 개선율을 검증한 결과로 사용하지 않는다.

영구 DB·거래/우편/파티·전체 gameplay 키 변경·실제 직업 기획은 이번에 임의로 추가하지 않았다.
재접속 저장/복원과 실 네트워크 장애/장시간 성능 검증의 완료 여부는 실제 증거에 따라 구분한다.
