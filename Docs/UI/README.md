# 플레이어 UI

현재 기본 조작(2026-10-21)은 **왼쪽 Ctrl 단독 탭 = 걷기 전환**, **왼쪽 Alt 단독 탭 = 커서 표시/숨김**이다. 두 입력 모두 키를 놓을 때 한 번 확정하며 키 반복·다른 키/마우스와의 조합은 토글로 처리하지 않는다. Alt+Tab·Alt+Enter·Alt+F4 및 포커스 소실은 커서 전환을 취소한다. 오른쪽 Alt/AltGr와 기존 Ctrl UI 단축키는 유지한다.

걷기는 기존 `/Game/Input/Actions/IA_Walk`와 플레이어/대검 BP의 기존 참조를 재사용한다. 현재 `IMC_Default`에 누락돼 있던 걷기 매핑을 왼쪽 Ctrl로 연결했고, action의 native `UProject_JInputTriggerModifierTap`이 단독 해제를 판단한다. 입력 컴포넌트는 `Triggered`만 소비하며 메뉴 이동 차단·탑승 중에는 지상 걷기 의도를 바꾸지 않는다. 기존 Ctrl 탑승 하강 매핑은 유지한다. 커서는 UI에 키보드 포커스가 있어도 동작해야 하므로 기존 Slate 전처리 경로를 유지하고 Alt로 바꿨다. 별도 커서 IA나 중복 입력 writer를 추가하지 않는다.

이 변경은 direct UBT 빌드와 집중 회귀 7개가 통과했다. 실제 Enhanced Input에 생산 `IMC_Default`를 적용한 Ctrl 해제·Ctrl+F·flush 검증, modifier 탭/커서 조합 검증, 기존 입력 재진입·로코모션 경계 검증을 포함한다. `Saved/Validation/InputRemap_20261021`에 에셋 백업·다른 매핑 보존 비교·빌드/실패 이력과 최종 Automation 보고서가 있다. Windows에서 실제 Alt+Tab으로 다른 앱을 전환하는 수동 검수는 이번 자동화 결과에 포함하지 않는다.

2026-10-15 PC 입력 계약: 키와 마우스 위젯은 자신이 취득한 원래 owner/index/receipt만 해제한다. 재바인딩 전 delegate와 이전 위젯의 늦은 mouse-up/destruct는 callback이 만든 새 press를 소비하지 않는다. 공개 BP Press/Release 경로는 유지한다. [실제 GAS callback·키 delegate·마우스 회귀](../Review/Audits/Project_J_PC_Structure_Cleanup_2026-10-15.md)를 확인한다.

현재 후속 구조·수명·실제 데이터 조사는 [심화 감사](../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](../Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md)에 기록한다. DS01–18의 실제 변경·계약 검토·미검증을 구분한다. 최종 전체 회귀 332개·Game/Cook과 기록한 통합 검증을 수행했으며, CPU 일부 증가와 packaged RHI trace 종료 실패는 남아 있다.

[구조 개선](../Review/Audits/Project_J_Structural_Improvement_2026-10-13.md)은 HUD 조립과 재사용 skill/inventory widget 구현을 분리하고 두 widget의 아이콘 요청·취소·GC 수명을 공유한다. BP 이름/상속/BindWidget 계약과 PlayerUIComponent/ViewModel의 상태·권한 소유는 유지한다.

[전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)는 화면 생성 실패·재시도의 구독/입력 처리기 수명 경계도 확인한다. 정상 화면 자동화 통과만으로 실패 경로의 수명까지 보장하지 않는다.

이전 좌측 Ctrl 커서 조작과 CMC 달리기 예측 수정의 이력은 [커서와 달리기](Cursor_And_Sprint_Prediction_2026-10-10.md)를 읽는다. 현재 커서/걷기 키는 위의 기본 조작, 현재 이동/Sprint 예측은 [Mover 로코모션](../Animation/Architecture/Player_Locomotion_Architecture.md)이 기준이다.

최신 전환·키보드·연속 갱신 검증은 [UI 수명과 키보드](Continuity_Keyboard_And_Dynamic_Load_2026-10-10.md), 저장·장비 정책은 [UI 저장과 장비 비교](Reliability_Comparison_And_Load_2026-10-10.md), 기본 화면·아트 교체 계약은 [Compact MMORPG HUD](Compact_HUD_And_Gameplay_2026-10-10.md)를 읽는다.

| 문서 | 역할 |
|---|---|
| [UI 수명과 키보드](Continuity_Keyboard_And_Dynamic_Load_2026-10-10.md) | 소스 세대·전환 정리, 키보드 대체 조작·분할 모달, 실제 드래그/네트워크 검증, 연속 delta 부하 |
| [UI 저장과 장비 비교](Reliability_Comparison_And_Load_2026-10-10.md) | 저장 병합·재시도·백업·버전 보호, 장비 정책에 맞는 비교, 요청 수명과 부하 측정 |
| [UI 조작 확장](Extended_Interaction_And_Validation_2026-10-10.md) | 스택 분할/합치기/순서, UI 키 설정, 상태 효과/대상 연결, 입력 수명과 실행 검증 |
| [Compact HUD](Compact_HUD_And_Gameplay_2026-10-10.md) | 현재 화면·조작, 퀵슬롯/지도/소모품/의뢰/경험치, 소유권·최적화·스킨 계약과 검증 한계 |
| [캐릭터 UI 프로필](Character_UI_Profiles_2026-10-10.md) | 직업·전직·상태별 표시 선택, 실제 자원, 전용 모듈, 프로필 단계 검증 |
| [초기 최소 조작 UI](MMORPG_UI_Architecture_2026-10-10.md) | 기존 인벤토리/장비 RPC 연결과 첫 드래그·클릭 검증 기록 |

UI 설정 저장과 캐릭터 gameplay 영구 저장은 별도 시스템이다. 완료 범위는 각 문서의 검증 조건을 따른다.
