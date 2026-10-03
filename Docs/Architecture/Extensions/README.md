# 콘텐츠와 MMORPG 확장 기반

| 문서 | 용도 |
| --- | --- |
| [직업·전직·전투 확장 기반](Extension_Foundation_2026-09-19.md) · [검증](Extension_Foundation_Validation_2026-09-19.json) | PlayerState 진행 상태, 공유 능력 수명, 구성 선택과 작성 계약 |
| [MMO 기반 구현 범위](MMO_Foundation_2026-09-12.md) · [검증](MMO_Foundation_Validation_2026-09-12.json) | Core-only 모듈, 요청·저장·동시성 계약과 당시 적용 범위 |
| [콘텐츠 카탈로그](MMO_Content_Catalog.md) · [원본 JSON](MMO_Content_Catalog.json) | 콘텐츠·운영 확장 항목. 콘텐츠 구현 완료 목록과 구분 |
| [직업·전직 제작 도구](../../Gameplay/Authoring/Content_Bundle_Authoring_2026-09-19.md) | 에디터 작성·검증·저장·등록 절차 |
| [선행 구조 검토](../../Review/Architecture/Extensions/Extension_Architecture_Review_2026-09-13.md) | 9월 13일의 제안과 진단 |

카탈로그 JSON/Markdown과 기반 보고서는 생성 스크립트 및 코드의 참조 경로를 유지한다. 카탈로그 재생성과 검증은 `Scripts/Validation/Validate-MMOArchitecture.py`를 따른다.
