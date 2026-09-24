# 감사·리팩터링·후속 보완 기록

이 폴더는 **요청/진단 → 구현 결과 → 부족한 부분의 후속 보완**을 구분한다. `Prompt`는 요청 문서이며 완료 증거가 아니다. `Result`도 해당 날짜의 결과이므로 뒤의 후속 문서를 함께 확인한다. 기존 미추적 요청 초안 4개는 로컬 `Docs/Review` 원래 위치에 두며, 저장소에 포함된 결과와는 구분한다.

## 전체 코드 감사

1. 전체 감사·정리 요청(로컬 미추적 초안)과 [코드 감사 결과](Audits/Project_J_Code_Audit_Result_2026-09-23.md).
2. 후속 감사 요청(로컬 미추적 초안)과 [후속 감사 결과](Audits/Followups/Project_J_Final_Audit_Followup_Result_2026-09-23.md).

## 캐릭터 런타임 책임

1. 책임 분리 요청(로컬 미추적 초안) → [책임 분리 결과](CharacterRuntime/Refactor/Project_J_Character_Runtime_Responsibility_Refactor_Result_2026-09-24.md).
2. 분리 후 남은 State Controller 책임: 마무리 요청(로컬 미추적 초안) → [마무리 결과](CharacterRuntime/Followups/Project_J_StateController_Runtime_Finalization_Result_2026-09-24.md).
3. 별도 시각 회귀: [TIP 순간 튐 로그 분석과 수정](Animation/TIP/Project_J_TIP_Visual_Pop_Trace_2026-09-24.md).

## Runtime Retarget·Hand IK·SSR

- [구조 검토](RuntimeRetarget/Assessment/ProjectJ_Runtime_Retarget_Architecture_Review.md) 후 [후속 개선](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_SSR_Followup_Prompt.md), [최종 보완](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_SSR_Final_Fix_Prompt.md), [최종 통합 수정](RuntimeRetarget/Followups/ProjectJ_Runtime_Retarget_Final_Integration_Fix_Prompt.md) 요청을 보관한다. 이 묶음은 요청·검토 기록이므로 실제 완료 여부는 소스와 별도 결과에서 확인한다.
