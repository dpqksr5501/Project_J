# 리깅·200명 측정 자료 안전 정리 — 2026-10-25

작업 중 생긴 캡처·실험 스크립트·중간 Blender/UE 백업·상세 로그·대표 trace를 검증 가능한 **로컬 압축본1개**로 모으고 임시 원본을 정리했다. 작은 수치·timer CSV/JSON·대표 화면은 저장소에 유지한다. 현재 제작 소스와 빌드 캐시는 삭제하지 않았다.

| 항목 | 결과 |
| --- | ---: |
| 원자료 경로 / 중복 통합 후 payload | 745 / 676 |
| 전체 원자료 논리 합계 | 1,424,687,524 bytes |
| 검증된 로컬 ZIP | 793,891,789 bytes (757.11 MiB) |
| 압축 보존 후 임시 원본 삭제 | 455개 / 1,352,253,214 bytes |
| 위 삭제 중 재생성 캐시 | 10개 / 172,020 bytes |
| 별도 LFS tmp 삭제 | 29개 / 169,152,238 bytes |
| 총 논리 삭제 / ZIP 추가 후 순감소 | 1,521,405,452 / **727,513,663 bytes (693.81 MiB)** |
| 잠금·변경으로 미삭제 | 0개 |
| 보호 파일 확인 | **74개 SHA-256 전후 동일** |

## 범위와 보존 이유

- `Saved/Validation/GreatSword*_20261025`, `RigPostReview_20261025`, `RigDocumentationReview_20261025`, `RigCost_20261025`와 이번 `RigDirect*` 실행만 대상으로 골랐다. 실패·미채택·이전 발/손 문제의 증거도 압축본에 포함한다. 작은 JSON/CSV/결과 receipt는 로컬에서 바로 읽을 수 있게 유지한다.
- 대표 전체 trace1개는 향후 비용 재분석에 필요해 보존했다. 큰 ZIP은 `Saved/Maintenance/RigCleanup_20261025/Evidence.zip`에만 두어 LFS object/작업 사본 중복을 추가하지 않는다. 새 clone에서 이 ZIP이 자동 제공되지는 않는다. 공유하는 수치·타이머·실행별 CSV와 대표 화면은 [성능 Data](../Performance/Reports/Data/DirectRig200_20261025)에 있다.
- 제작용 `.blend`, Rig UI·export helper, 재임포트 FBX19개와 UE 기준 비교 FBX, 현재 Content/Source/Config/영구 검증 도구·빌드는 유지한다. 다른 월드 제작 백업·사용자 원본·과거 독립 성능 기준선·Git 이력/정상 LFS object는 이번 작업의 잔여 파일로 판단하지 않는다.
- 초기 생성 파츠8개라는 이전 측정 가정과 새10개 구성의 차이를 고친 측정기·집계 도구는 다음 검사에 재사용하므로 유지한다. 물리·IK를 낮춰 수치를 만들지 않았다.

## 실제 디스크 여유와 논리 크기

원본 정리 직후 C: 여유 증가는 **53,248 bytes**, 별도 LFS tmp 정리 직후는 **131,072 bytes**로 작았다. 따라서 위 693.81 MiB를 실제 디스크 회수량이라고 주장하지 않는다. 파일 할당·공유/열린 핸들·다른 디스크 활동의 원인을 분리하지 않았으며, 압축본 생성 전 여유도 별도 원값으로 기록했다. 압축본 생성 전부터 두 정리 직후까지의 C: 여유 관측은 **118,913,404,928→118,119,636,992 bytes**로, 오히려757.00 MiB 감소했다. 보존 ZIP 추가를 포함한 관측이며, 논리 파일 삭제가 이 시간대의 물리 할당 감소로 이어졌다고 확인하지 못했다. 현재 빌드 캐시나 다른 프로세스를 강제 종료해 숫자를 늘리지 않았다.

## 검증과 복원

[원래 경로·크기·SHA-256·정리 여부](../../Archives/Validation/Rigging_2026-10-25/ArchiveManifest.json), [정리 수치](../../Archives/Validation/Rigging_2026-10-25/CleanupResult.json), [LFS tmp 명세](../../Archives/Validation/Rigging_2026-10-25/LfsTempCleanup.json), [보호 파일 해시](../../Archives/Validation/Rigging_2026-10-25/ProtectedPreservation.json)를 보존한다. ZIP 전체와676개 payload를 검증한 후 각 원본 해시를 다시 비교해 삭제했다. LFS tmp는2분 이상 미변경·배타적 열기·해시·재검사를 통과한 파일만 대상으로 했고 Git/LFS 프로세스를 강제 종료하지 않았다.

```powershell
python Archives/Validation/verify_archive.py Archives/Validation/Rigging_2026-10-25/ArchiveManifest.json
# 특정 실행 복원: 현재 파일과 겹치지 않는 빈 폴더 사용
python Archives/Validation/verify_archive.py Archives/Validation/Rigging_2026-10-25/ArchiveManifest.json `
  --restore-prefix Saved/Validation/WholeProjectAudit_20261013/RigDirect200_Trace_20261025 `
  --destination Saved/RestoredRigEvidence
```

복원은 기록된 SHA-256의 로컬 ZIP이 필요하며 기존 파일 덮어쓰기를 거부한다. 재생성 캐시10개는 payload에 넣지 않아 원문 복원 대상이 아니고, 실행 인자·구성·결과·수치·전체 trace는 보존했다. 모든 에디터·측정·분석 프로그램은 정상 종료 후 정리했다.

## 10월 26일 Cloth 후속 작업

원래 ZIP·trace member·임시 복원본의 SHA를 모두 다시 확인한 뒤 중복 907,223,845bytes trace를 제거했다. 후속 시험의 격리 UserDir·중복/미채택 캡처·일회성 드라이버를 포함해 276개 파일·논리 1,105,016,978bytes를 정리했다. 대표 이미지와 interval/측정 CSV·manifest는 [후속 보고서](../Performance/Reports/ProjectJ_Cloth_Budget_200_2026-10-25.md)에 보존한다. 원본 ZIP과 SourceArt·에셋·현재 바이너리/빌드 캐시는 유지했다.

즉시 관측 C 여유 공간은 108,812,017,664→108,811,784,192bytes로 233,472bytes 감소했다. 논리 삭제 합계를 물리 확보량으로 주장하지 않는다. [정확한 경로·해시·보존 증명](../Performance/Reports/Data/ClothBudget200_20261025/Cleanup.json)을 따른다. 이전 10월 25일 정리 수치와 별도 작업이다.
