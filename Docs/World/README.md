# 메인 오픈 월드 제작

새 맵에서 시작하거나 기존 월드를 갈아엎을 때는 [맵 신설·교체 절차](World_Map_Authoring_and_Replacement.md)를 따른다. 원경 설정과 검사, 플레이 입구, 패키징, 옛 캐릭터 복귀까지 다룬다. 전투·군중·온라인을 포함한 전체 작업의 상태는 [오픈월드·MMORPG 통합 인계](../Architecture/Planning/Project_J_OpenWorld_Project_Handoff.md)에 있다.

기존 약 8×8km 제작 맵은 `/Game/World/MainWorld/Maps/L_MainWorld`이며 명시적 MapsToCook에 포함된다. 지역 정의의 Asset Manager 검색 범위는 `/Game/World`다. 이 맵의 제작 원본은 `MainWorld/Source`, 환경 라이브러리와 월드 전용 메시·재질·텍스처·지형 풀은 `Map_Resource`에 있다. 10월 제작 인계는 20곳의 초기 배치와 환경 아트·도시·마을·물의 남은 작업을 기록한다.

기존 지역의 ID·좌표·자산은 [MainWorld 제작 인계](LEVEL_DESIGNER_GUIDE.md)를 참고한다. 서버의 필드 이벤트와 준비·스폰 예산은 [월드 이벤트 실행](World_Event_Runtime.md)에 있다. 11월에는 B01의 제작 맵 독립 2클라이언트 전투와 원경 프록시 재생성을 확인했다. [독립 검증 결과](../Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#독립-검증-결과)

2026-10-19 환경 라이브러리: 세 원본의 제작 자산7,556개를 분류해 신규5,282개를 반입하고2,274개는 재사용했다. `/Game/Map_Resource`와 공유 컬렉션 `Env_All`에서 찾는다.543개 변형,54개 제작용 프리팹 원본 레벨,12개 구형 PCG의 호환성 보류와49개 범위 밖 보존 자료는 가이드·인수인계를 따른다. GameAnimationSample/GASP는 제외했고 이번 작업에서 월드 아트·높이·StableID·C++를 바꾸지 않았다. 재점검에서 환경 조립·구름 자산 8개를 보완하고 빈 충돌체와 Nanite 식생 설정 경고를 해결했다. 최종 자산/참조·대상 재로드 검사는 오류 0·경고 0이며, 전체 셰이더·PIE·성능 합격을 뜻하지 않는다.

2026-10-19 후속 정리: 기존 환경 자산 2,195개의 이동 후 추가 상호작용 자원·공유 노이즈·마을 원본 맵과 MainWorld 전용 환경 자원 41개도 통합했다. 최초 충돌 59개 중 속성·픽셀·의존 연결이 같은 48개는 재사용하고 서로 다른 11개는 `Variants/ProjectExisting`에 유지한다. 추가 Door Blueprint도 실제 동작이 다른 변형으로 보존했다. 실제 레벨·DA·HLOD 설정·PCG 그래프·제작 원본은 `World/MainWorld`를 따른다. 액터 2,717개와 인스턴스 414,311개의 배치 대조가 일치했으며 전체 PIE·Cook·성능 합격을 뜻하지 않는다.

10월 제작 작업을 재개할 때는 [당시 월드 인수인계](WORLD_HANDOFF_2026-10-18.md)를 확인한다. 이전 WorldQuality 진단·제작 기록과 WorldGen/WORK_CONTINUATION의 정리 내역이 포함돼 있다.

- 목표: [v5 비주얼 복구 지시서](Project_J_World_Visual_Quality_Recovery_Codex_v5.md), [v4 공간·확장 기획](Project_J_8x8km_WorldGen_Completion_Expansion_v4.md), [목표 월드맵](초반%20지역%208×8km%20판타지%20월드맵.png). 기존 [v3 기획](MMORPG_8x8km_Starter_Lv1-30_Codex_Prompt_v3.md)과 레퍼런스도 보존한다.
- 제작 계약: [StableID 대응](../WorldGen/01_EXISTING_TO_TARGET_MAPPING.csv), [월드 구조](../WorldGen/05_WORLD_ARCHITECTURE.md), [월드 경계](../WorldGen/06_WORLD_EDGE_CONTRACT.md), [게임플레이 공간 연결](../WorldGen/07_GAMEPLAY_SPATIAL_INTEGRATION.md).
- 실제 입력: `Content/World/MainWorld/Source`의 높이·manifest·장소/배치·바이옴·식재 데이터와 Native 저장본. 예전 작업 보고서의 수량보다 최신 Source와 에디터 상태를 우선한다.
- 도구: [현재 월드 제작 도구](../../Scripts/World/README.md). 적용 완료한 일회성 보정·반복 조회·철회한 시험·퇴역 Region A 제작 스크립트는 삭제했다. Unreal 런타임은 해당 Python 파일로 생성되는 구조가 아니며 이미 저장된 월드/에셋을 사용한다.

현재 [구현 상태표](../WorldGen/03_IMPLEMENTATION_STATUS.md)는 Source 레지스트리 기준이다. 이전 [기반 감사](Build/01_AUDIT.md)와 [제작 구조](Build/02_ARCHITECTURE.md)는 당시 계약을 설명한다. 장문 초기 제작 일지는 최신 인수인계·상태표로 통합했다. 실제 Water 플러그인 구현은 보류 상태다.

2026-10-18 소스 감사는 당시 상태 기록이다. 이후 사용자 요청으로 Source 파일명과 환경 경로를 MainWorld로 정리하고 Legacy와 옛 redirector를 퇴역했다. 안정 지역 ID `Starter.Lv1_30`와 기존 제작 소유 태그·자동 생성 HLOD 내부 이름/GUID는 보존한다. 기본 시작 맵 ThirdPerson은 변경하지 않았다. 과거 Cook/패키지 결과를 이번 정리 후의 신규 검증 결과로 사용하지 않는다.

## C++ 기반 Nav 통합 검증

`/Game/Tests/Combat/L_CanonicalNetwork`는 기존 메인/ThirdPerson 맵을 보존한 작은 non-WP 검증 맵이다. 기존 C++ NetworkLoadGameMode가 초기 캡슐 지면·Nav 슬롯, 실제 NavMover 경로, autonomous 입력·GAS/몽타주·정확한 종료를 소비한다. 바닥은 AOI0/20000 동선을 덮고 Nav XY halfextent5000은 기존 R/RMB의 실제 이동 범위를 수용한다. 환경 아트·고속 스트리밍·수용량 인증 맵은 아니다.

`UProject_JWorldAuthoringLibrary::BuildEditorNavigation()`은 현재 Editor world에서 native 동기 빌드를 호출한다. 반환값은 전체 Nav freshness/physics/route 보증이 아니므로 실제 투영·경로·collision 검증과 저장은 호출자의 책임이다. 신규 world의 AsyncLoadLock은 정상 에디터 프레임에서 해제될 때까지 기다려야 하며 잠금 강제해제나 수동 tick으로 대체하지 않는다. 생성 도구는 해당 준비 후 실제 투영을 확인하고 새 대상만 저장했다.

실행은 기존 `Scripts/Validation/Measure-Network.ps1`의 `-Animation -CombatInput -CanonicalMelee -CanonicalMeleeMovingRecipient`를 사용한다. 고유 RunName과2client가 필요하며 map은 opt-in에서만 선택된다.151의 strict선택/Cook·실제패키지 listen+원격2 검증은 [현재 아키텍처 작업](../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md)을 따른다. 기본 시작 맵과 NPC 비모션매칭 정책은 유지한다.
