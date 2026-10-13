# 애니메이션 계획과 제안

## 구현 결과와 현재 기준

아래 문서는 Planning 폴더에 있지만 구현·검증 결과를 포함한다. 실제 플레이 품질과 전체 렌더 성능의 남은 확인은 각 문서에서 구분한다.

- [실시간 리타깃 프로필](../Architecture/Runtime_Retarget_Profiles_2026-10-13.md): 골격·몸체 설정 공유, MCP 실자산 연결, GT/worker·GC 경계와 효과 없는 메타데이터 캐시 제거.
- [200명 최적화 인계](Animation_Optimization_Handoff_2026-10-13.md): main 병합 기준, 현재 구조·품질/실행 계약, 동시 공격 CPU 과제, 원자료와 재현 명령, 다음 채팅 요청 예시.
- [200명 런타임 최적화](Animation_Runtime_Optimization_2026-10-13.md): 기존 MM 인덱스 캐시 재사용, v2 측정 범위·원자료, 국소 timer 감소와 전체 성능의 한계, 빌드·움직임·네트워크 검증.
- [통합 Locomotion 구현](Unified_Locomotion_Implementation_2026-10-12.md): 단발/MM 재생 스택 통합, State Controller 유지, ABP 노드별 역할과 당시 검증.
- [지속 곡선 판정과 Master 정리](Sustained_Curve_And_Master_Cleanup_2026-10-12.md): 느린 회전의 단발 복귀, Strafe 미세 입력 보호, 사용하지 않는 함수 그래프 제거.
- [200명 시각 예산](Crowd_Visual_Presentation_Budget_2026-10-12.md): 리타깃·의상 물리의 평가·보간, 보호·복구 조건, 병렬 실행 경계와 동일 바이너리 비교.

## 조사와 제안 이력

- [통합 재생 재설계 조사](Locomotion_Unified_Playback_Redesign_Research_2026-10-12.md): 공식·사용자 구현 자료와 대안 검토. 실제 채택 범위는 위 구현 결과를 따른다.
- [GASP / Project_J 자연스러움 조사와 고도화 인계](GASP_ProjectJ_Naturalness_2026-10-06/README.md): 실제 그래프·설정·소스 근거, 비교·우선순위·검증·후속 PROMPT와 통합 PDF. 구현 완료 문서가 아니다.

- [파이프라인 구조 개선 제안](Humanoid_Animation_Pipeline_Redesign_Proposal.md): 대안 비교·마이그레이션·당시 요청을 보존한 제안서.
- [실행·스레딩 감사 계획](ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md): Insights, 병렬 평가, Fast Path와 MM/Chooser 비용의 확인 절차.

현재 손 접촉 구현은 [Guided](../Authoring/Guided_Hand_Contact.md)·[보조 손](../Authoring/Secondary_Hand_Contact.md)·[무기 정책](../Architecture/Weapon_Grip_Drive_Policy.md)을 먼저 읽는다. 제안의 전 항목을 구현 완료로 해석하지 않는다.
