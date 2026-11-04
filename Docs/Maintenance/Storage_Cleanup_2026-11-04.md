# 필수 아키텍처 마감 후 저장 공간 정리 — 2026-11-04

사용자의 로컬 용량 정리 요청에 따라 Project_J의 생성 출력과 이 checkout 전용 OS Temp만 처리했다. 다른 프로젝트·개인 파일·설치 엔진에는 적용하지 않았다. 외부 저장소 생성·업로드·커밋/push는 수행하지 않았다.

## 현재 중앙 보관 위치

`Saved/StorageCleanup_20261104/ArchiveStore`에서 실제 보관 자료750개를 성능 trace73·측정126·로그429·과거 디버그12·에셋/복구 ZIP55·기타50·package payload3·shader payload2로 분류했다. README와 INDEX.csv가 관리 진입점이다. Saved 아래 기존 압축 보관 파일까지 포함하며 SourceArt/Content/Docs의 원본·배포 자산이나 읽지 못한 긴 MockHarness 경로는 이동하지 않았다.

이번 정리의 gzip75개·중복 보존본5개는 같은 볼륨 안에서 이동했다. 기존 압축자료670개는 중앙에 NTFS 하드링크로 연결해 옛 문서/도구의 경로를 유지했다. 실제 파일 데이터를 추가 복사하지 않았으며 전체750항목의 SHA와 링크 동일성을 검증했다. 호환 링크가 있으므로 폴더 크기의 단순 합계는 실제 디스크 사용량과 다를 수 있다. 보관 파일은 불변 자료로 취급하고 변경 작업은 별도 복원 사본에서 수행한다.

`ArchiveLocations.json`을 이동 전에 게시하고 Restore-File.ps1이 새 중앙 위치를 해석하도록 연결했다. 기존 Compressed/Deduplicated 명세는 과거 기록으로 보존했다. 이동 중에는 이미 이동한 중앙 파일과 아직 남은 원래 파일을 모두 해석할 수 있다. 동일 이름/내용의 별도 원본에서 경로가 충돌한 경우 경로 식별자를 추가했고 중간 명세도 보존했다. 최종 새 위치에서 gzip·payload 실제 복원2개와 전후 Source/runtime 동일성을 확인했다. `CentralizationResult.json`·`CentralizationVerification.json`이 근거다.

아래1차의 원래 옆 `.gz`/PreservedPayloads 설명은 당시 배치 기록이다. 현재 실제 저장 경로는 중앙 위치 명세를 따르며 기존 복원 명령은 그대로 사용한다. 외부 보관 시에는 중앙 자료뿐 아니라 부모 폴더의 위치/해시 명세와 복원 도구도 함께 보관한다. 게임 프로젝트 전체 백업이나 폐기한2차 생성물 전체의 복원을 보장하는 묶음은 아니다.

## 추가 빌드·패키징 정리 — 2차

종료한 과거 성공 검증의 Package/Cooked60곳에서 생성 payload516개·12,728,411,531bytes(약11.85GiB)를 추가 폐기했다. 완료 기록과 실제 절대 출력 경로가 일치하는 경우만 선택했으며, 파일별 크기/수정시각·reparse 부재·미사용과 SHA-256을 확인해 삭제 명세를 남겼다. 외부 프로세스 감지로99개 이후 안전 지점에서 중단한 실행도 보존했고, 프로세스 부재·동일 계획/소스를 확인한 명시적 resume으로 완료했다. 최초 부분 결과는 `Phase2/PartialResult1.json`에 유지했다.

`Batch147CanonicalStage1`, `Batch151CanonicalStage1`, `MMOFoundation_20261028_ShippingStage1`과 이들이 사용하는 Cook을 남겼다. 남아 있던 대표 자료249파일의 크기/수정시각을 대조했다. 이는 과거 대표 검증 자료의 보존이며, 앞선1차 보관 자료를 복원하지 않고 모든 과거 package가 바로 실행 가능하다는 보장은 아니다. 소스/설정/현재 runtime provenance, 보호28asset SHA, Git index는 전후 일치했다. 현재 Binaries·Intermediate·DDC와 고유 제작/복구·에셋 원본은 계속 보존한다.

로그·설정·명세 및 내부 Saved/SaveGames/Autosaves/Crashes/Backup은 삭제 목록에서 제외했다. Cook의 내용 폐기 뒤 공식 readiness marker13개는 내용/SHA를 유지한 채 `.retired-20261104` 이름으로 보존해, 불완전한 Cook을 정상 Stage 입력으로 잘못 승인하지 않게 했다. 최근 보존 Cook은 이 처리 대상이 아니다.

**2차에서 폐기한 생성 payload는 바이트 단위 복원을 약속하지 않는다.** 과거 실행의 로그/명세는 남지만 그 실행 이미지를 다시 만들려면 새 이름의 검증 run에서 현재 소스로 Cook·Stage해야 한다.1차의 gzip/중복 보존본은 여전히 복원할 수 있으나, 그것만으로2차 폐기한 package 전체가 복원되지는 않는다. `Saved/StorageCleanup_20261104/Phase2`의 Plan/Deleted/Result·전후 provenance·RetiredReadinessMarkers가 상세 근거다.

2차 삭제 전후 관측은 C:136.50→148.36GiB였다. 이후 별도 관측은 약149.76GiB이며 볼륨의 다른 변동을 이 정리의 회수량으로 합산하지 않는다. 이번 추가 삭제의 논리 합계와 관측 여유 공간을 구분한다.1차와 동일하게 생성물 정리이며 향후 용량 증가의 root-cause fix가 아니다.

## 결과와 보존 — 1차

| 항목 | 결과 |
| --- | --- |
| 관측 C: 여유 공간 | 약105.09→136.73GiB, 관측 증가 약31.63GiB |
| 논리 용량 감소 합계 | 36,563,201,793bytes, 약34.05GiB |
| 종료한 검증/commandlet의 생성 registry cache | 181개·14,484,404,363bytes 제거 |
| 대형 trace/CSV/log·과거 PDB | 75개를 gzip으로 보존, 복원 SHA-256 전부 일치, 8,986,942,559bytes 감소 |
| 동일한 과거 package/shader 출력 | 원래64경로를 해시별 보존본5개로 통합, 13,091,854,871bytes 감소 |
| 실제 복원 확인 | gzip 원자료1개와 중복 payload1개 복원·SHA 일치 후 시험 복원본만 제거 |
| 소스·설정·현재 실행물·Git index | 정리 전후 provenance/index SHA 일치 |

논리 파일 크기 합계와 실제 드라이브 여유 공간 관측은 다른 값이다. 차이의 원인을 이번 정리에서 특정하지 않았으며 논리 합계를 실제 회수량으로 표현하지 않는다. 재생성 파일 정리와 가역 보관이며 향후 저장 공간 증가의 root-cause fix는 아니다.

Source/Config/Content/SourceArt, Git refs/objects/LFS objects, 현재 Binaries/Intermediate/DDC, 고유 월드 제작·staging/백업 자료, 고유 package와 진단 내용은 보존했다. 정상 Git fsmonitor 대기 데몬을 확인하고 유지했으며 filter나 Git 설정은 변경하지 않았다. 엔진/빌드/Insights가 실행 중이면 정리를 거절하고 프로세스를 임의 종료하지 않았다.

최초 파일 집계의78개 오류는 긴 중첩 MockHarness 경로의 WinError3였다. 이 경로는 미확인으로 보존했다. registry 후보 검색에 잡혔던 현재 프로젝트 Intermediate 파일도 소유 범위 검사로 보존했다. 추가 정리 첫 pass가 경계 검사로 중단되기 전 제거한 commandlet cache24개는 고정 inventory와 실제 부재를 대조해 ExtraCaches.json에 기록했다. package 대조 중 SHA가 다른 기존 counterpart1개를 보존했고 해당 두 후보 자체의 일치하는 보존본으로 통합했다. 크기만 같은 파일을 동일 내용으로 취급하지 않았다.

## 근거와 복원

상세 원자료는 `Saved/StorageCleanup_20261104`에 있다. `Result.json`, `DeletedCaches.json`, `ExtraCaches.json`, `Compressed.jsonl`, `Deduplicated.jsonl`, `PreservedDifferentPayloads.json`, `RecoveryChecks.json`과 전후 Source provenance를 연결한다. 보존 payload는 같은 폴더의 `PreservedPayloads`에 있으며 `.gz`는 각 원래 파일 바로 옆에 있다. 이 폴더와 압축본/보존 payload를 삭제하면 복원 계약을 잃으므로 함께 보관한다.

기존 측정 보고서의 원래 path/hash는 과거 기록으로 유지했다. 보관한 trace/CSV/log/PDB를 직접 읽거나 과거 package/cook을 재사용하려면 해당 경로를 먼저 복원해야 한다. 최신 전체 실행물과 당시 전체 package의 동일성을 새로 인증한 것은 아니다.

프로젝트 루트에서 `Restore-File.ps1 -OriginalPath <명세의 원래 절대 경로>`를 사용한다. 이 도구는 명세의 압축본/보존본 SHA와 복원 결과 SHA를 확인하며 기존 원래 파일을 덮어쓰지 않는다. 압축본/보존본은 복원 후에도 남는다. 예:

```powershell
$entry = Get-Content './Saved/StorageCleanup_20261104/Compressed.jsonl' | Select-Object -First 1 | ConvertFrom-Json
& './Saved/StorageCleanup_20261104/Restore-File.ps1' -OriginalPath $entry.original
```

## 외부 보관 판단

이번 정리로 공간을 확보했으므로 별도 GitHub 저장소를 즉시 만들 필요는 없다. 소스/에셋 버전 관리와 대형 진단/복구 아카이브를 구분해 보관한다. GitHub는 Git을 백업 도구로 권장하지 않고 일반 파일100MiB 제한을 둔다([공식 안내](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)). 장기 보관이 필요하면 별도 드라이브나 백업 저장소에 원본 에셋/저장소와 보관 명세·압축본·보존 payload를 함께 복사하고 복원 검증 뒤 로컬 보관량을 결정한다. 현재 원격 백업 여부와 다른 디스크 전체의 정리 가능 여부는 검증하지 않았다.
