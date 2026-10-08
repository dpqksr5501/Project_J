# 정면 달리기 Foley 누락 복구 — 2026-10-08

## 증상과 원인

회전·기울어진 달리기에서는 소리가 나지만 정면에서는 나지 않았다. 실제 BP_GreatSword의 Motion Matching에서 `PSD_Run_Cycle`과 `M_Neutral_Run_Loop_F`가 선택됐으며, 수정 전 Foley Submitted/Played는 모두 0이었다.

정면 루프에는 `FoleyEvent: Run` 이름·타이밍·GUID가 남아 있지만 Notify 객체가 None이었다. 회전 루프에는 native 객체가 있었다. 프로젝트 정면 루프 파일에는 원래 BP 클래스 참조 자체도 이미 없었지만, GASP 원본에는 좌우 발 BP 노티파이가 있었다. 이름만 남은 이벤트는 native Notify::Notify를 호출하지 않는다.

기존 이관은 AssetRegistry의 BP referencer 목록으로 후보를 찾았다. 객체 참조가 이미 제거된 애니메이션은 목록에 들어오지 않았다. 기존 985개/9,003개 이관 검증과 single-node 회전 시험은 실제 정면 이동의 콘텐츠 완전성을 보장하지 못했다. 이번 문제는 런타임 최적화나 MM 네트워크 정책의 결함으로 확인된 것이 아니다.

## 복구와 보존

UEFN 애니메이션 루트 감사에서 154개 애니메이션/895개 객체 없는 Foley 이벤트를 찾았다. 원본에도 객체가 없는 roll 마커 2개는 유지하고, **152개 애니메이션의 893개 이벤트**를 native 객체로 복구했다. 후보는 Run 58개, Sprint 20개, Jump 76개이며 roll 예외 2개 패키지는 저장하지 않았다.

[편집 전용 C++ 도구](../../../Source/Project_JCharacterEditor/Private/Animation/Project_JFoleyMigrationLibrary.cpp)의 `RepairMissingGaspFoley`는 원본을 `/FoleySource/`에 별도로 읽고 GUID로 이벤트를 찾는다. 객체 참조를 제외한 직렬화 메타데이터가 일치할 때만 원본 payload를 읽는다. 기존 native 객체와 원본의 genuine named-only 이벤트는 수정하지 않는다. 원본 파일을 저장하거나 대상 애니메이션 전체를 원본으로 덮어쓰지 않는다.

Notify 포인터와 dedicated-server 실행 플래그만 변경한다. 타이밍·offset·GUID·track·weight·filter·chance·좌우 발·tag·볼륨·pitch를 보존했다. 전체 이벤트 메타데이터와 복구 payload를 적용 직후와 새 에디터의 디스크 재로드 후 검증했다. 포즈·곡선, PSD, Blueprint, 맵, 음원, 런타임 네트워크 경로는 이번 수정 대상이 아니다.

[복구 스크립트](../../../Scripts/Editor/Repair-MissingGaspFoley.py)는 명시적 후보 목록과 dry-run/apply/verify를 사용한다. 전체 dry-run 오류 0 이후에만 적용하고, 먼저 대상 파일을 해시 검증한 백업으로 보관한다. 미저장 대상·GUID 불일치·원본과 다른 메타데이터는 중단 조건이다. GASP 일회성 콘텐츠 복구 도구이며 앞으로 추가될 음원·애니메이션의 런타임 규격을 제한하지 않는다.

## 검증

- 직접 UnrealBuildTool.exe: Editor/Game Win64 Development 성공.
- 새 프로세스 자동화: **16개 성공, 테스트 경고/실패 0개**. 새 `ProjectJ.Foley.LocomotionAssetContacts`는 실제 정면 Walk/Run/Sprint 루프의 실행 가능한 native 객체와 양쪽 발 접촉을 확인한다.
- 새 에디터: 154개 후보/893개 복구 이벤트의 메타데이터·payload 재검증 성공.
- 클라이언트 PIE: 실제 BP_GreatSword, source mesh `SKM_Quinn_Simple`, 기존 AnimBP/MM 그래프를 유지했다. AddMovementInput으로 실제 이동했으며 single-node override를 쓰지 않았다.
- 정면 4초: Submitted/Played **0→13**, Traces 13. 회전 4초: **13→24**, Traces 24. NotReady/Duplicate/Capacity/Expired/Budget/Unmapped/Loops는 모두 0.

Played는 PlaySoundAtLocation 제출 횟수다. 사람이 들은 음질·실제 살아남은 voice 수나 군중 성능을 측정한 결과가 아니다. 모든 입력·전환·질주 의미 상태 검증을 대신하지 않는다.

기존 ThirdPerson 기본 spawn 충돌은 맵 수정 없이 PIE 서버 RestartPlayerAtTransform으로 우회했다. 에디터 백그라운드 3fps 제한은 시험 동안만 해제하고 복원했다. 임시 PIE와 Python remote execution을 종료했다. 이후 에디터의 정상 종료 로그도 확인했다.

최초 헤드리스 원본 대조는 오류 0이었지만 종료가 지연되어 해당 임시 읽기 프로세스를 정리했다. 이를 깨끗한 프로세스 검증으로 계산하지 않는다. 이후 실제 에디터 대조·복구·재검증과 별도 자동화 프로세스 종료 결과로 검증했다. 기존 GASP BP 버전 차이·누락 dependency 진단은 native 런타임 테스트 결과와 구분한다.

전체 감사·백업·apply·verify·MM 샘플은 `Saved/Validation/FoleyForwardFix_20261008/`에 있다. [기계 판독 기록](Foley_Forward_Run_Validation_2026-10-08.json)에 요약과 해시를 보관한다.
