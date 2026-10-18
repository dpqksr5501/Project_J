# 게임플레이와 콘텐츠 제작

## 월드와 레벨 설계

- [새 Lv.1~30 시작 권역 제작](../World/README.md): v3의 8×8km 전체 골격, MCP 자산 조사·공통 코드 재사용·기존 A 결과물 퇴역과 현재 검증 상태.
- [오픈 월드 레벨 설계와 확장 계획](Planning/Open_World_Level_Design.md): A 배치와 유기적 확장, 월드 C++ 책임, 지형·정착지·탐험 제작 이력. 16절은 밀도·숲 성능, 17절은 지하·상층 탐험, 18절은 부분 산지·도시 가로, 19절은 주변 여섯 마을·새 건축·토지 이용과 검증 범위를 기록한다.
- [레벨 디자인과 최적화 조사 자료](Planning/Open_World_Research_References.md): 엔진 문서·개발 사례·샘플·설계 지식·로컬 에셋 확인과 초기 개념도. 구현·실측과 제안을 구분한다.

## 에디터 작성

| 문서 | 작업 |
| --- | --- |
| [콘텐츠 데이터 제작 통합 가이드](Authoring/Content_Authoring_System.md) | DA 역할 → 스탯·직업·스킬·장비 확장 → 묶음 제작·저장·등록 |
| [모듈러 캐릭터·의상](Authoring/ModularCharacterEquipmentGuide.md) | 메시·머티리얼·추가 뼈와 물리 작성 |

## 구조와 소유권

- [스킬·GAS 구조](Architecture/SkillSystemArchitecture.md).
- [탈것 구조](Architecture/MountSystemArchitecture.md).
- [직업·전직 확장 계약](../Architecture/Extensions/README.md).

공격·콤보는 [전투 제작](../Combat/Authoring/GreatswordCombatAuthoringGuide.md), 몸체별 손 접촉은 [애니메이션 제작](../Animation/Authoring/README.md)을 따른다. 저장과 런타임 등록은 각 가이드의 별도 단계를 유지한다.
