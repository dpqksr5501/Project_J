# 검증 자료의 공유·로컬 압축 보관소

요약·표·작은 CSV는 `Docs`에 바로 읽을 수 있게 유지하고, 실행별 상세 원자료는 주제별 ZIP 하나와 원본 경로/크기/SHA-256 명세로 보관한다. 공유 ZIP은 Git LFS로 추적하며 로컬 보관은 아래에 별도로 표시한다. 새 PC에서 공유본은 일반 clone 후 `git lfs pull`로 실제 파일을 받는다. LFS pointer만 받은 상태는 자료 수신 완료가 아니다.

새 ZIP은 Git에서 제외된 `Saved` 임시 경로에서 완성·검증한 뒤 최종 보관 경로로 이동한다. LFS 대상 경로에 큰 ZIP을 직접 계속 쓰면 앱의 Git 검사와 겹쳐 중간 사본·임시 파일이 늘 수 있다. 같은 자료의 새 사본을 추가하기 전에 기존 보관 명세의 해시와 목적을 확인한다.

| 보관본 | 내용 | 복원·정리 기록 |
| --- | --- | --- |
| [액션 복귀 2026-10-26 명세](ActionRecovery_2026-10-26/ArchiveManifest.json) | **로컬 ZIP**: 실패/최종 PIE·빌드/회귀·임시 드라이버 65개. [작은 공유 결과](ActionRecovery_2026-10-26/Summary.json)는 Git에서 바로 조회 | [원인·검증](../../Docs/Animation/Authoring/Player_Animation_Library.md#액션-복귀의-idle-break와-잔여-예측-보강--2026-10-26) · [정리 수치](ActionRecovery_2026-10-26/CleanupResult.json) |
| [리깅·직접 ABP 200명 명세](Rigging_2026-10-25/ArchiveManifest.json) | **로컬 ZIP**: 실패/최종 리깅·200명2회+trace. Git으로는 작은 수치·timer CSV·대표 화면·복원 명세 공유 | [성능 결과](../../Docs/Performance/Reports/ProjectJ_DirectRig_200_2026-10-25.md) · [안전 정리·물리 회수 한계](../../Docs/Maintenance/Storage_Cleanup_2026-10-25.md) |
| [Mover 200명](Mover200_2026-10-22/Evidence.zip) | 최종 ABBA·큰 편차·미채택 30Hz 후보·중간 실패 로그, 회귀/네트워크/월드 검사, 최종 100 NPC 기준 trace | [명세](Mover200_2026-10-22/ArchiveManifest.json) · [공간 정리 결과](Mover200_2026-10-22/CleanupResult.json) · [성능 보고서](../../Docs/Performance/Reports/ProjectJ_Mover_200_2026-10-22.md) |

## 2026-10-25 로컬 보관 범위

리깅·직접 ABP의 큰 원문은 `Saved/Maintenance/RigCleanup_20261025/Evidence.zip` 한 개에 보존한다. ZIP793,891,789bytes, 원래745경로/676payload의 모든 SHA-256을 검증했다. 이 ZIP은 Git LFS로 추가하지 않아 **새 clone에 자동 제공되지 않는다**. 공유하는 [수치·타이머 CSV/JSON·대표 화면](../../Docs/Performance/Reports/Data/DirectRig200_20261025)은 원문 ZIP 없이 읽을 수 있다. 명세의 로컬 ZIP 위치와 SHA가 일치해야 전체 trace/중간 상태 복원이 가능하다.

```powershell
python Archives/Validation/verify_archive.py Archives/Validation/Rigging_2026-10-25/ArchiveManifest.json
```

아래 Mover ZIP은 기존과 같이 Git LFS 공유 보관본이다.

## 검증과 복원

액션 복귀 원문은 `Saved/Maintenance/ActionRecovery_20261026/Evidence.zip`에 로컬 보존하며 새 clone에 자동 제공되지 않는다. `ActionRecovery_2026-10-26/ArchiveManifest.json`도 아래 동일 검증기와 복원 옵션을 사용한다. 공유 요약에는 native59개 결과·실제 PIE 조건·관측 오류를 구분한 판정과 지문을 남겼다.

프로젝트 루트에서 Python 표준 라이브러리만으로 전체 ZIP 지문과 모든 해제 payload를 확인한다.

```powershell
python Archives/Validation/verify_archive.py Archives/Validation/Mover200_2026-10-22/ArchiveManifest.json
```

폴더 단위로 다시 분석하려면 **빈 별도 폴더**에 복원한다. 동일 바이트의 사본은 ZIP 안에 한 번만 저장했으므로, 단순 ZIP 전체 해제보다 명세를 해석하는 도구를 사용한다. 기존 파일 덮어쓰기는 거부한다.

```powershell
python Archives/Validation/verify_archive.py Archives/Validation/Mover200_2026-10-22/ArchiveManifest.json --restore-prefix Saved/Validation/WholeProjectAudit_20261013/Mover200_ReleaseA1 --destination Saved/RestoredEvidence
```

단일 파일은 `--restore <명세의 path>`를 사용한다. 이 보관본은 측정 당시의 소스 지문·조건·결과이며, 실행 가능한 엔진/패키지 전체 백업이 아니다. 원자료의 과거 절대 환경 경로는 측정 환경 기록으로만 해석한다.

## 2026-10-22 선별·중복 정리

- 원본 경로 1,226개를 검토했다. 1,213개 경로의 보존 파일은 바이트 동일 사본을 합쳐 **611개 payload**, 원문 **354,901,017 bytes → ZIP 215,908,297 bytes(약 205.9MiB)**로 보관했다. 모든 payload의 해제 SHA-256이 원문과 일치한다. 실패 로그와 36.88ms 공격 편차를 포함하며 좋은 결과만 선별하지 않았다.
- 매 실험에 자동 생성된 전체 trace 13개, **1,042,436,636 bytes**는 지문·경로·퇴역 이유를 명세의 `retired`에 남기고 폐기했다. 최종 100 NPC 추적의 전체 `Run.utrace` 한 개는 보존했다. 퇴역 trace의 모든 timer/event를 재분석할 수는 없지만, 보고서 표는 보존한 CSV로 다시 계산할 수 있다.
- 로컬의 archive-backed 로그·캡처·일회성 실행 파일과 퇴역 trace 총 **959개 / 1,374,298,581 bytes**를 정리했다. JSON receipt·CSV·동결 지문은 남겨 과거 실행 label 재사용 방지와 작은 결과 조회를 유지한다. 원본 삭제량에서 ZIP 추가량을 뺀 논리 순감소는 **1,158,390,284 bytes(약 1.08GiB)**다. Git LFS의 로컬 object 사본 약 205.9MiB는 별도로 필요하다.
- 삭제 직전/직후 C 드라이브 여유 증가 관측은 **77,135,872 bytes(약 73.6MiB)**였다. 논리 감소를 실제 회수량으로 바꾸어 보고하지 않는다. 동시 디스크 활동·파일 할당과 이후 LFS 저장은 이 관측과 구분한다. 정리 결과 JSON에 전후 원값을 보존했다.
- 이번 ZIP 생성 중 Git 검사와 겹쳐 남은 `.git/lfs/tmp`도 별도로 정리했다. 2분 이상 변경이 없고 배타적 열기로 사용하지 않음을 확인한 **88개 / 16,012,272,234 bytes(약 14.91GiB)**다. 활성/최근 파일은 건너뛰고 Git 필터 종료 후 재확인했다. 삭제 구간별 여유 증가 합계는 16,086,134,784 bytes이며, **이번 작업 중 생긴 임시 사본 정리를 기존 프로젝트의 순절감량에 더하지 않는다.** [1차 기록](Mover200_2026-10-22/LfsTempCleanup.json) · [후속 기록](Mover200_2026-10-22/LfsTempCleanup-Final.json).
- 이번 작업의 중간 ZIP이 LFS object로 남은 사본 2개도 **1,093,553,805 bytes** 정리했다. 모든 Git 참조와 스테이징의 LFS OID 23,300개를 대조해 미참조임을 확인하고 해당 두 파일의 SHA-256을 재검증했다. 최종 ZIP과 기존 이력의 object는 유지하며 일반적인 `lfs prune`이나 이력 재작성은 하지 않았다. [정확한 두 OID와 전후 여유](Mover200_2026-10-22/IntermediateArchiveCleanup.json)에 별도 기록했다.
- 기존 `Archives/AnimationSources` 두 ZIP은 제작 원본, `Docs/Archive/Navigation`은 이전 문서, 로컬 `PreRetargetSource8485562c.zip`은 과거 소스 백업으로 내용을 확인했다. 이번 측정과 동일 자료가 아니므로 합치지 않았다. 과거 별도 작업의 `.gz` trace/로그도 이번 보관본에 넣었다고 주장하지 않는다. 현재 빌드 캐시·월드 제작 자료·콘텐츠·개인 SaveGame·Git/LFS 이력은 정리 대상에서 제외했다.

문서/보관 도구 추가는 측정 이후다. 당시 동결한 Source/Scripts/Config/Plugins/uproject 1,115개와 검증 바이너리는 보고서의 실행 기준으로 유지하며, 새 커밋 ID와 과거 측정 HEAD가 같다는 의미가 아니다.
