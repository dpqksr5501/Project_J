# 검토 요청·감사·후속 결과

`Prompt`는 작업 요청이고 `Result`는 해당 단계의 결과다. 요청 → 결과 → 후속 보완 순서로 읽고, 현재 적용 여부는 주제별 기준 문서와 소스에서 확인한다.

| 주제 | 기록 |
| --- | --- |
| 남은 전체 감사·누락 점검을 다른 채팅에서 마무리 | [2026-10-14 계속 작업 인계](../Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md). 최신 커밋 기준점·반복 방지·알려진 후보 외 전체 범위 대조·검토 장부·완료 기준을 연결한다. |
| 전수 완료 주장 재점검·추가 수명·공통 판정 정리 | [2026-10-14 재점검](Audits/Project_J_Reaudit_2026-10-14.md) · [소유권 지도](Audits/Project_J_Reaudit_Map_2026-10-14.md) · [현재 검증](Audits/Project_J_Reaudit_Verification_2026-10-14.md) · [상세/남은 문제](Audits/Project_J_Reaudit_Remaining_2026-10-14.md). RA01–27 실제64개 변경·352개 native 회귀, 141개 추가 구현 본문과 header/inline 계약, 실제 수정·검토만 한 것·미검증을 구분한다. 전체 607개 정밀 수동 완료를 주장하지 않는다. |
| MM 이중 상태·공통 수명·GAS 세대·실제 공격 데이터 심화 | [2026-10-13 시작 심화 감사](Audits/Project_J_Deep_Structure_Audit_2026-10-13.md). DS01–18 구현·최종 332개 회귀·Game/Cook·통합 결과, 실제 재현과 MCP/native 대조를 기록한다. CPU 일부 증가·RHI trace 종료 실패와 정밀 미검증은 남아 있다. |
| 실제 중복·책임·규모·private 미사용 정리 | [2026-10-13 구조 개선](Audits/Project_J_Structural_Improvement_2026-10-13.md). 기능별 실제 변경과 검토 후 유지한 시스템을 구분한다. |
| 전체 구조·실제 R/마우스·Avatar·수요 수명·리타깃 제작·성능 | [2026-10-13 조사와 개선](Audits/Project_J_Whole_Project_Audit_2026-10-13.md). 범위별 근거와 진행/미검증 항목을 구분한다. |
| 전체 구조·버그·병렬·성능 후속 감사 | [2026-10-13 작업 인계와 새 채팅 요청](../Handoffs/WholeProjectAudit_2026-10-13/Project_J_Whole_Project_Audit_Handoff_2026-10-13.md). 새 감사의 완료 결과가 아니라 출발 상태·보존 사항·재현·검증 범위다. |
| 전체 구조·콘텐츠 확장 | [아키텍처 검토](Architecture/README.md) |
| 코드 감사 | [요청](Audits/Project_J_Codex_Full_Audit_Refinement_Prompt.md) → [결과](Audits/Project_J_Code_Audit_Result_2026-09-23.md) → [후속 요청](Audits/Followups/Project_J_Final_Audit_Followup_Prompt.md) → [후속 결과](Audits/Followups/Project_J_Final_Audit_Followup_Result_2026-09-23.md) |
| 캐릭터 런타임 | [책임 분리와 StateController 후속 흐름](CharacterRuntime/README.md) |
| 런타임 소유권·객체 수명 | [2026-10-08 실제 재현·수정 감사](Audits/Project_J_Runtime_Ownership_Audit_2026-10-08.md) |
| 런타임 리타깃·Hand IK·SSR | [구조 검토](RuntimeRetarget/Assessment/ProjectJ_Runtime_Retarget_Architecture_Review.md) → [후속 요청](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_SSR_Followup_Prompt.md) → [최종 보완 요청](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_SSR_Final_Fix_Prompt.md) → [통합 수정 요청](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_Final_Integration_Fix_Prompt.md) |

실제 애니메이션 진단은 [Diagnostics](../Animation/Diagnostics/README.md)에 모았다. 최신 손 접촉 구성은 [제작 가이드](../Animation/Authoring/README.md)에서 읽는다. 과거 요청의 지시문은 당시 작업 범위를 보존한 자료다.
