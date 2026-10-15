# 검토와 감사 기록

현재 결정과 마지막 수정은 아래 표에서 찾고, 실제 구현 계약은 [주제별 문서](../README.md)에서 읽는다. 날짜별 감사는 당시 변경·재현·검증 조건을 보존한 기록이다. 서로 다른 단계의 통과 수를 합산하거나 이전 성공을 후속 수정의 새 검증으로 해석하지 않는다.

## 현재 통합 기준과 결정

| 확인할 내용 | 기준 기록 |
| --- | --- |
| main 통합 범위와 사용자 변경 보존 | [2026-10-15 main 통합](Audits/Project_J_Main_Integration_2026-10-15.md). 리타깃·구조 정리·R19/R20과 Mirror 판단을 문서 커밋 `6d6713f4`까지 통합했다. |
| Mirror 도입 여부 | [도입 보류 결정](../Animation/Planning/Mirror_Data_Adoption_Decision_2026-10-15.md). 현재 locomotion을 유지하고 특정 연결 문제가 확인될 때만 국소 재검토한다. |
| 마지막 점프·착지 runtime 수정 | [R20 sample 보정 초기화](Audits/Project_J_Strafe_Diagonal_Warp_Repair_2026-10-15.md). 보정 분기 초기화 순환 의존성을 해결했다. R19의 선택 존재 검증과 실제 보정 검증을 구분한다. |
| 착지 선택·DT·Chooser·입력·소스 범위 대조 | [R19 소스·데이터 대조](Audits/Project_J_PC_Source_Data_Reconciliation_2026-10-15.md). 대각 Stand 착지 조건과 명시 범위 장부·Cook 근거를 보존한다. |
| 마지막 BP·DA 검토와 Door 정리 | [PC 에셋 검토 마감](Audits/Project_J_PC_Asset_Review_Closure_2026-10-15.md). 유지 이유·실제 수정·회귀·Cook과 남은 실행 경계를 구분한다. |
| PC 소스·BP·DA 소유권 정리 | [PC 구조 정리](Audits/Project_J_PC_Structure_Cleanup_2026-10-15.md). 입력 receipt·상호작용/identity 퇴장·편집 취소·중복 스킬 부여 등 해당 단계의 변경을 확인한다. |
| 앞선 native 책임·공통 로직 정리 | [소스·아키텍처 정리](Audits/Project_J_Source_Architecture_Cleanup_2026-10-15.md) · [BP·DA 후속 정리](Audits/Project_J_Asset_Architecture_Cleanup_2026-10-15.md). 완료 범위와 유지한 fallback·테스트/제작 자산을 확인한다. |
| 전체 시스템 검토 범위와 source 지도 | [상세 시스템 대조](Audits/Project_J_Continuation_System_Review_2026-10-15.md) · [계속 작업 구조 지도](Audits/Project_J_Continuation_Structure_Map_2026-10-14.md). 파일 수·장부 연결을 전수 의미 검토 완료율로 사용하지 않는다. |

현재 전문 기준은 [애니메이션](../Animation/README.md), [콘텐츠 제작](../Gameplay/Authoring/Content_Authoring_System.md), [전투](../Combat/README.md), [아키텍처](../Architecture/README.md)에서 찾는다. 감사 보고서는 이 문서들을 대신하는 별도 운영 가이드가 아니다.

## 유지와 보류 범위

| 상태 | 내용 |
| --- | --- |
| 유지 | TIP·OTM/Strafe·Run/Sprint·Pivot·단발/MM 복귀·Land·전투·장비·리타깃, 지속 곡선 0.35초·100cm·3도·85%, R19/R20 수정과 기존 시험·제작 자산. |
| 도입 보류 | Mirror 후보 일괄 확대. 구체적인 발·자세 연결 문제가 확인됐을 때만 해당 구간을 검토한다. |
| 기존 미확인 경계 | 공중 방향 재선택 pending 순서 후보, preview 설정·시험 CSV 의미, Traversal 설치, 독립 원격 및 전수 시각·오디오 품질. 자세한 범위와 담당 경계는 R19/R20 및 PC 에셋 검토 기록을 따른다. |
| 조사 보류 | CPU/GPU 원인 분석·반복 성능 비교·대규모 부하·운영 검증. 기존 CPU 증가와 렌더 차이가 해결됐다고 하지 않는다. |

이 표는 모든 미확인 항목을 즉시 다시 수행하라는 작업 지시가 아니다. 이전 인계·Prompt의 전체 감사 요청은 작성 당시 범위로 읽고, 재개 범위는 현재 사용자의 요청으로 정한다. 전체 함수·에셋 무결성 보증과 명시 범위의 구조 정리 마감을 구분한다.

## 단계별 변경과 검증 이력

| 단계 | 보존 기록과 역할 |
| --- | --- |
| 커밋 준비 | [통합 감사 준비](Audits/Project_J_Commit_Preparation_2026-10-15.md) · [PC 정리 준비](Audits/Project_J_PC_Commit_Preparation_2026-10-15.md). 당시 stage·제외·보존 범위이며 현재 main 병합 상태는 위 통합 기록을 따른다. |
| 계속 작업 | [10월 14일](Audits/Project_J_Continuation_Audit_2026-10-14.md) · [10월 15일](Audits/Project_J_Continuation_Audit_2026-10-15.md). CA 단계별 수정·검증이며 후속 R19/R20의 재실행 결과로 계산하지 않는다. |
| RA 재점검 | [변경](Audits/Project_J_Reaudit_2026-10-14.md) · [소유권 지도](Audits/Project_J_Reaudit_Map_2026-10-14.md) · [검증](Audits/Project_J_Reaudit_Verification_2026-10-14.md) · [상세와 남은 문제](Audits/Project_J_Reaudit_Remaining_2026-10-14.md). RA01–27의 당시 구현과 측정 조건을 보존한다. |
| DS 심화 감사 | [DS01–18 결과](Audits/Project_J_Deep_Structure_Audit_2026-10-13.md) · [지도](Audits/Project_J_Deep_Structure_Map_2026-10-13.md). 상태·자원·GAS·실제 공격 데이터의 변경과 당시 통합 검증 근거다. |
| 초기 전체·구조 감사 | [전체 감사](Audits/Project_J_Whole_Project_Audit_2026-10-13.md) · [구조 개선](Audits/Project_J_Structural_Improvement_2026-10-13.md). 후속 단계 이전의 문제·유지 판단·원자료다. |
| 당시 작업 인계 | [10월 14일](../Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md) · [10월 13일](../Handoffs/WholeProjectAudit_2026-10-13/Project_J_Whole_Project_Audit_Handoff_2026-10-13.md). 당시 브랜치·요청·미완료 목록을 보존하며 최신 실행 지시로 재사용하지 않는다. |

## 다른 검토와 진단

| 주제 | 시작점 |
| --- | --- |
| 전체 구조·콘텐츠 확장 | [아키텍처 검토](Architecture/README.md) |
| 이전 코드 감사 | [요청](Audits/Project_J_Codex_Full_Audit_Refinement_Prompt.md) → [결과](Audits/Project_J_Code_Audit_Result_2026-09-23.md) → [후속 요청](Audits/Followups/Project_J_Final_Audit_Followup_Prompt.md) → [후속 결과](Audits/Followups/Project_J_Final_Audit_Followup_Result_2026-09-23.md) |
| 캐릭터 런타임 | [책임 분리와 StateController 후속 흐름](CharacterRuntime/README.md) |
| 객체 수명 | [2026-10-08 실제 재현·수정 감사](Audits/Project_J_Runtime_Ownership_Audit_2026-10-08.md) |
| 런타임 리타깃·Hand IK·SSR의 이전 검토 | [구조 검토](RuntimeRetarget/Assessment/ProjectJ_Runtime_Retarget_Architecture_Review.md) → [후속 요청](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_SSR_Followup_Prompt.md) → [최종 보완](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_SSR_Final_Fix_Prompt.md) → [통합 수정](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_Final_Integration_Fix_Prompt.md) |

애니메이션 재현·원인은 [Diagnostics](../Animation/Diagnostics/README.md), 현재 손 접촉 제작은 [Authoring](../Animation/Authoring/README.md), 성능 조건·원자료는 [성능](../Performance/README.md)에서 읽는다. 통합 전 누적 안내는 [보관 자료](../Archive/README.md)에 남겼다.
