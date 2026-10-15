# main 통합과 Mirror Data 결정 반영

기준일: 2026-10-15. `codex/runtime-retarget-profiles`의 기존 구현·검증 이력과 [Mirror Data 도입 보류 결정](../../Animation/Planning/Mirror_Data_Adoption_Decision_2026-10-15.md)을 로컬 `main`에 fast-forward로 통합한다. 새 runtime 구현·에셋 변경 없이 기존 커밋과 문서 결정을 보존하는 통합이다. 원격 push는 이번 범위에 포함하지 않는다.

## 통합 범위

기존 로컬 main은 `8485562cdb8cc1da8ba460465045972db9349fda`이며 다음 5개 커밋의 선행이다. 원격 main을 fetch해 로컬 main의 선행임을 확인했다. 충돌 해결이나 source 재작성을 요구하는 분기 병합이 아니다.

| 커밋 | 내용 |
| --- | --- |
| `f1bbd48a` | 실시간 리타깃 프로필 구조와 전체 감사 인계 정리 |
| `855a1c8e` | 전체 프로젝트 감사 개선·검증 기록과 후속 마무리 인계 정리 |
| `f8ebd8a5` | 소스·상태 소유권과 BP 중복 실행 경로 정리 및 회귀 검증 |
| `e5e534e8` | PC 소스·BP·DA의 중복 로직과 상태 소유권 정리 |
| `a28b6657` | Strafe 대각 착지 선택과 점프·착지 보정 초기화 수정 및 소스·데이터 구조 대조 |

이 이력에 Mirror 판단 문서, 현재 문서 진입점, R19·R20의 후속 판단 링크와 생성 카탈로그 갱신을 추가한다. 과거 조사·빌드·시험 수치를 이번 병합에서 새로 실행한 결과로 바꾸지 않는다.

## 유지 결정과 남은 범위

Mirror Data는 새로 도입하지 않는다. 현재 움직임을 유지하며, 구체적인 연결 문제가 확인됐을 때만 해당 후보와 실제 재생·보정 소비를 재검토한다. 기존 MDT와 미러 지원 재생 기반을 삭제하지 않는다.

R19의 대각 Stand 착지 선택, R20의 sample 보정 초기화, TIP·OTM/Strafe·Run/Sprint·Pivot·단발/MM 복귀·Land·전투·장비·리타깃과 지속 곡선 0.35초·100cm·3도·85%를 보존한다. 테스트·제작 자산과 기존 감사 증거를 삭제하거나 정리하지 않는다.

Mirror 보류는 모든 실행 품질의 검증 완료를 뜻하지 않는다. 공중 방향 재선택 pending 순서 후보, preview 설정·시험 CSV 의미, Traversal 설치 및 독립 원격·전수 시각 경계는 기존 기록을 따른다. CPU/GPU·반복 성능 측정·대규모 부하는 계속 보류한다.

## 검증과 사용자 변경 보존

이번 추가 변경은 문서뿐이므로 Unreal 빌드·PIE·Cook을 반복하지 않는다. 문서 링크와 `git diff --check`, 기존 runtime·Config·콘텐츠·시험 파일의 보존, main이 통합 대상 HEAD와 같은 커밋/트리를 가리키는지를 확인한다. 기존 runtime 검증의 조건과 한계는 [R19](Project_J_PC_Source_Data_Reconciliation_2026-10-15.md)·[R20](Project_J_Strafe_Diagonal_Warp_Repair_2026-10-15.md) 및 각 감사 기록을 따른다.

문서 카탈로그 갱신 후 231개 Markdown의 로컬 링크 2,304개를 검사해 깨진 링크는 0개였다. 부재한 로컬 archive 링크 12개는 기존 기록으로 유지한다. 이번 문서 변경의 공백 검사는 통과했으며, 기존 사용자 인계 변경에 있는 EOF 빈 줄 경고는 수정하지 않는다.

시작 시 사용자 수정은 `Docs/Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md` 1개이며 SHA256는 `9465b0717e3a5152e06831deffaa20fb70d62f7251ae45f9c9ae3b3253b0c5f2`다. 이 변경은 그대로 유지하고 이번 문서 커밋·main 통합의 커밋 내용에서 제외한다. 같은 트리로 main을 fast-forward한 뒤 전환해 해당 작업 사본을 보존한다.
