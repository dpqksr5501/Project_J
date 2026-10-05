# 아키텍처 검토 기록

| 문서 | 읽는 범위 |
| --- | --- |
| [2026-10-05 이동 중 하차 정리](Project_J_Mount_Dismount_Motion_2026-10-05.md) / [검증 결과](Project_J_Mount_Dismount_Motion_Validation_2026-10-05.json) | 성공한 하차의 탈것 입력·속도 정리, 거부 시 이동 유지, Editor/Game 빌드·회귀 112개 통과 |
| [2026-10-05 재탑승 입력 복구](Project_J_Mount_Remount_Input_2026-10-05.md) / [검증 결과](Project_J_Mount_Remount_Input_Validation_2026-10-05.json) | 클라이언트 InputComponent 재사용 시 F·비행 이동 입력 복원, 중복/타 바인딩 보존, Editor/Game 빌드·회귀 111개 통과 |
| [2026-10-05 Pivot 입력·F 하차 후속 수정](Project_J_Pivot_Mount_Followup_2026-10-05.md) / [검증 결과](Project_J_Pivot_Mount_Followup_Validation_2026-10-05.json) | Pivot 입력 중단·재진입 억제·하차 캡슐/바닥 높이·이벤트 진단, Editor/Game 빌드·회귀 110개 통과 |
| [2026-10-05 이동 중 Strafe 회전 모션 연결](Project_J_Strafe_Facing_Redirect_2026-10-05.md) / [검증 결과](Project_J_Strafe_Facing_Redirect_Validation_2026-10-05.json) | 고정 카메라 facing catch-up 예측·history 보존·전환 phase/재검색·선택적 Strafe PSD fallback, Editor/Game 빌드·전체 회귀 108개 통과 |
| [2026-10-04 F 하차·TAB Strafe 수정](Project_J_Mount_Strafe_Fix_2026-10-04.md) / [검증 결과](Project_J_Mount_Strafe_Fix_Validation_2026-10-04.json) | 공통 하차 입력 hand-off·CMC 점진 회전·facing 정렬 후 SettledCycle, Editor/Game 빌드·전체 회귀 105개 통과, 2026-10-05 검증 완료 |
| [2026-10-04 플레이어·궤적·애니메이션 적용](Project_J_Player_Animation_Implementation_2026-10-04.md) / [검증 결과](Project_J_Player_Animation_Implementation_Validation_2026-10-04.json) | P01–P13 수명·유효성·Notify·Layer·시간·DA 계약 보강, Editor/Game 빌드·회귀 101개 통과, 측정 후 적용할 성능 항목 구분 |
| [2026-10-04 플레이어·궤적·애니메이션 재점검](Project_J_Player_Animation_Audit_2026-10-04.md) | 현재 구현을 반영한 플레이어 경계 감사, 궤적·OTM/Start·Notify·Layer·DA 개선 13항목, 관련 자동화 87개 통과 |
| [2026-10-04 재점검 범위표](Project_J_Player_Animation_Coverage_2026-10-04.csv) / [검증 결과](Project_J_Player_Animation_Validation_2026-10-04.json) | 이전 감사 hash 대조와 현재 65파일 경계 재검토 구분, 테스트·경고·대표 ABP inventory |
| [2026-10-03 잔여 소스 고도화 적용](Project_J_Architecture_Maturity_Implementation_2026-10-03.md) | R01–R14 구현, 기존 구조를 유지한 소유권·복구·검증 보강, 회귀 89개 통과와 남은 실제 환경 확인 조건 |
| [2026-10-03 고도화 적용 검증](Project_J_Architecture_Maturity_Implementation_Validation_2026-10-03.json) | 최종 빌드·테스트 결과, 경고 원문, 변경 파일 hash와 감사 기록 연결 |
| [2026-10-03 잔여 전체 소스 고도화 감사](Project_J_Remaining_Source_Maturity_Audit_2026-10-03.md) | 기존 감사 영역을 제외한 NPC·Mass·GAS·비행·Notify·백엔드·소셜·에디터·실험/검증 코드, 개선 14항목과 추가 자동화 46개 결과 |
| [2026-10-03 소스 감사 범위표](Project_J_Remaining_Source_Coverage_2026-10-03.csv) | 소스·shader·검증 스크립트 431개: 이전 영역/신규 검토 구분, 검토 깊이와 파일 hash |
| [2026-10-03 캐릭터 구조 고도화 점검](Project_J_Character_Architecture_Maturity_Audit_2026-10-03.md) | 플레이어 컴포넌트 23개·DA 타입 23개, 수명·궤적·OTM/Start·스킬·통신 개선안과 검증 근거 |
| [2026-09-03 전체 감사](ProjectJ_Architecture_Audit_2026-09-03.md) | 당시 코드·설정·에디터 증거와 기반/구현/계획 구분 |
| [전체 구조 감사](ProjectJ_Architecture_Audit.md) | 구조·책임 검토의 원본 |
| [MMORPG 구조 검토](MMORPGArchitectureReview.md) | 모듈·GAS·서비스·네트워크·UI·MM 확장 우선순위 |
| [2026-06-07 초기 감사](Architecture_Audit_20260607.md) | 초기 구조 진단 |
| [2026-09-13 콘텐츠 확장 검토](Extensions/Extension_Architecture_Review_2026-09-13.md) | 직업·전직·DA 작성 구조의 선행 제안 |

후속 구현은 [런타임 보고서](../../Architecture/Runtime/README.md), [확장 기반](../../Architecture/Extensions/README.md), [캐릭터 런타임 후속](../CharacterRuntime/README.md)에서 확인한다.
