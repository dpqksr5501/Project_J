# 메인 오픈 월드 제작

약8×8km의 기존 제작 월드를 앞으로 확장할 메인 월드로 전환 중이다. 목표 맵 `/Game/World/MainWorld/Maps/L_MainWorld` 복제와 관련 비맵 에셋45개 이름 변경은 저장됐고, 현행 manifest·검증 도구·AssetManager 스캔·Cook 설정도 MainWorld를 따른다. 물리적 Source 폴더는 Starter에 보존하며 구 맵과 redirector 정리는 수행하지 않았다. **전체20곳의 초기 공간 배치는 있지만 최종 환경 아트·도시·마을·물·성능은 미완이다.** 전환의 기술 검증과 제한은 아래 감사 기록과 실행 영수증을 따른다.

레벨 디자이너는 [제작 인계 가이드](LEVEL_DESIGNER_GUIDE.md)를 먼저 읽는다. 맵 선택, C++와 저작의 경계, 보존 ID·좌표, 편집 순서, 알려진 문제와 다음 제작 방향을 설명한다.

2026-10-19 환경 라이브러리: 세 원본의 제작 자산7,556개를 분류해 신규5,282개를 반입하고2,274개는 재사용했다. `/Game/Map_Resource`와 공유 컬렉션 `Env_All`에서 찾는다.543개 변형,54개 제작용 프리팹 원본 레벨,12개 구형 PCG의 호환성 보류와49개 범위 밖 보존 자료는 가이드·인수인계를 따른다. GameAnimationSample/GASP는 제외했고 이번 작업에서 월드 아트·높이·StableID·C++를 바꾸지 않았다. 재점검에서 환경 조립·구름 자산 8개를 보완하고 빈 충돌체와 Nanite 식생 설정 경고를 해결했다. 최종 자산/참조·대상 재로드 검사는 오류 0·경고 0이며, 전체 셰이더·PIE·성능 합격을 뜻하지 않는다.

재개 전 [최신 인수인계](WORLD_HANDOFF_2026-10-18.md)를 먼저 읽는다. 이전 WorldQuality 진단·제작 기록과 WorldGen/WORK_CONTINUATION은 커밋 전 정리하면서 이 문서에 통합했다. 과거 실행 예시를 새 제작 지시로 사용하지 않는다.

- 목표: [v5 비주얼 복구 지시서](Project_J_World_Visual_Quality_Recovery_Codex_v5.md), [v4 공간·확장 기획](Project_J_8x8km_WorldGen_Completion_Expansion_v4.md), [목표 월드맵](초반%20지역%208×8km%20판타지%20월드맵.png). 기존 [v3 기획](MMORPG_8x8km_Starter_Lv1-30_Codex_Prompt_v3.md)과 레퍼런스도 보존한다.
- 제작 계약: [StableID 대응](../WorldGen/01_EXISTING_TO_TARGET_MAPPING.csv), [월드 구조](../WorldGen/05_WORLD_ARCHITECTURE.md), [월드 경계](../WorldGen/06_WORLD_EDGE_CONTRACT.md), [게임플레이 공간 연결](../WorldGen/07_GAMEPLAY_SPATIAL_INTEGRATION.md).
- 실제 입력: `Content/World/Starter/Source`의 높이·manifest·장소/배치·바이옴·식재 데이터와 Native 저장본. 예전 작업 보고서의 수량보다 최신 Source와 에디터 상태를 우선한다.
- 도구: [현재 월드 제작 도구](../../Scripts/World/README.md). 적용 완료한 일회성 보정·반복 조회·철회한 시험·퇴역 Region A 제작 스크립트는 삭제했다. Unreal 런타임은 해당 Python 파일로 생성되는 구조가 아니며 이미 저장된 월드/에셋을 사용한다.

현재 [구현 상태표](../WorldGen/03_IMPLEMENTATION_STATUS.md)는 Source 레지스트리 기준이다. 이전 [기반 감사](Build/01_AUDIT.md)와 [제작 구조](Build/02_ARCHITECTURE.md)는 당시 계약을 설명한다. 장문 초기 제작 일지는 최신 인수인계·상태표로 통합했다. 실제 Water 플러그인 구현은 보류 상태다.

2026-10-18 소스 감사: manifest의 현행 월드/데이터/외부 패키지 루트와 AssetManager 스캔·MapsToCook을 실제 저장된 MainWorld에 맞췄다. 안정 지역 ID `Starter.Lv1_30`와 물리적 Source 폴더는 유지한다. 기본 시작 맵 ThirdPerson은 변경하지 않았다. 기존 Starter 맵은 별개 사본이며 삭제하지 않았다. 이전 HLOD 실물과 redirector를 통한 참조는 남아 있어 기존 에셋 재저장·이동·redirector 정리는 수행하지 않는다. Cook/패키지 결과는 감사 영수증을 따른다.
