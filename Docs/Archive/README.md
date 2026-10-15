# 보관 자료

## 이전 작업 프롬프트 분류 2026-10-15

과거 작업용 프롬프트 9개는 [상태 목록](../Handoffs/README.md#이전-작업-프롬프트의-상태)에서 결과 연결·현재 기준 대체·당시 인계를 구분했다. 기존 파일에는 안내만 추가하고, 변경 전 원문은 바이트 그대로 `.txt`로 보존했다. 각 경로·SHA256·추가한 안내문은 [프롬프트 보존 명세](Requests/2026-10-15/Preservation.json)에 있다. 상대 링크는 원래 경로를 기준으로 해석한다.

후속 사용자 요청에 따라 [10월 14일 전체 감사 인계 원문](Requests/2026-10-15/WholeProjectAudit_Continuation_2026-10-14.md.txt)도 보관했다. 사용자가 붙여넣기 요청문을 제거한 사본을 그대로 남겼으며, 기존 인계 경로는 [현재 기준과 보관 위치 안내](../Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md)로 통합했다. SHA256와 처리 상태는 같은 명세의 `retired_handoffs` 항목에 기록했다.

## 문서 안내 통합 2026-10-15

main 통합 이후 여러 README에 중복 누적된 감사 안내는 [검토 목차](../Review/README.md)로 모았다. 현재 구현·제작 계약과 날짜별 원인·시험·측정 기록은 원래 경로에서 유지한다. 통합 전 안내 7개는 바이트 그대로 `.txt`로 보존하며, 원래 위치·SHA256와 브랜치 Markdown 68개 목록은 [보존 명세](Navigation/2026-10-15/Preservation.json)에 기록했다. 원문 안의 상대 링크는 명세의 원래 경로를 기준으로 해석한다.

| 원래 안내 | 보존 원문 |
| --- | --- |
| 루트 README | [원문](Navigation/2026-10-15/README.md.txt) |
| 문서 시작점 | [원문](Navigation/2026-10-15/Docs/README.md.txt) |
| 애니메이션 | [원문](Navigation/2026-10-15/Docs/Animation/README.md.txt) |
| 애니메이션 계획 | [원문](Navigation/2026-10-15/Docs/Animation/Planning/README.md.txt) |
| 검토·감사 | [원문](Navigation/2026-10-15/Docs/Review/README.md.txt) |
| 인계 | [원문](Navigation/2026-10-15/Docs/Handoffs/README.md.txt) |
| 프로젝트 맥락 | [원문](Navigation/2026-10-15/Docs/Overview/ProjectContext.md.txt) |

## 정리 전 안내문 — 2026-10-03

기존 목차와 안내의 모든 내용을 아래에 보존했다. 현재 시작점은 [Docs 목차](../README.md)이며, 아래 문서의 현재/최근 표현은 보관 전 시점의 안내다. 이동 후에도 자료를 찾을 수 있도록 링크·경로만 갱신했다.

- [기존 전체 안내](Navigation/2026-10-03/README.md).
- [기존 아키텍처 안내](Navigation/2026-10-03/Architecture/README.md).
- [기존 애니메이션 안내](Navigation/2026-10-03/Animation/README.md).
- [기존 전투 안내](Navigation/2026-10-03/Combat/README.md).
- [기존 게임플레이 안내](Navigation/2026-10-03/Gameplay/README.md).
- [기존 성능 안내](Navigation/2026-10-03/Performance/README.md).
- [기존 검토 안내](Navigation/2026-10-03/Review/README.md).

원본 보고서·제안·검증 기록은 각각의 주제 폴더에서 유지한다. 보관은 폐기나 구현 완료를 의미하지 않는다.

## 통합 전 상세 원문

같은 주제를 다루는 상세 문서 11개를 세 통합 가이드로 묶었다. 전체 원문은 `SourceDocuments/2026-10-03` 아래에서 보존하며 각 통합 가이드에 원문 링크가 있다.

- [무기 파지·손 접촉 통합](../Animation/Authoring/Weapon_Hand_Contact_System.md): Guided, Palm 부착, 무기 구동, 보조 손, 접촉 복귀 원문 다섯 개.
- [전투 애니메이션 통합](../Combat/Architecture/Combat_Animation_System.md): 합성 모드, 정책 경계, 무기 레이어 원문 세 개.
- [콘텐츠 데이터 제작 통합](../Gameplay/Authoring/Content_Authoring_System.md): DA 참조, 콘텐츠 확장, 제작 도구 원문 세 개.

이전 문서 경로에는 절별 통합 위치와 원문을 찾는 안내를 남겼다. 보고서·검증 기록은 통합 문서에서도 시점과 조건을 유지한다.
