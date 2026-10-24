# 로컬 검증 산출물 정리와 최적화 근거 보존 — 2026-10-16

사용자가 과도하게 늘어난 로컬 산출물 정리를 요청했다. 처음에는 재생성 캐시·정확한 중복만 정리하고 전체 trace를 보존했으나 충분하지 않아, 추가 요청에 따라 대표 원자료를 선별하고 오래된 trace·실행 산출물까지 정리했다. 소스·Config·Content·테스트 자산·현재 Development 빌드와 다른 작업 변경은 보존했다. 새 빌드·성능 시험은 실행하지 않았다.

본문의 초기 수치는 10월 16일 당시 기록이다. 이후 원자료의 현재 보존 형태와 패키지 퇴역은 아래 [10월 19일 추가 정리](#2026-10-19-추가-정리)를 함께 읽는다.

## 실제 공간과 범위

| 구분 | 결과 |
| --- | --- |
| 프로젝트 전체 논리 파일 크기 | **163.34 → 46.43GiB**; 경로별 합계이며 실제 회수 공간과 구분 |
| 두 차례 제거한 논리 파일 크기 순합계 | 116.91GiB |
| 드라이브 실제 여유 공간 증가 | **52.55GiB** |
| 정리 후 C 드라이브 여유 공간 | 142.73GiB |
| Saved | 122.41 → 15.84GiB |
| Intermediate | 21.54 → 14.08GiB; Development 유지 |
| 남긴 trace | 4.18GiB / 7개 |

1차의 논리 제거량36.86GiB와 실제 여유 증가3.42GiB를 혼동하지 않는다. 현재 보존 PDB/PCH의 하드링크도 확인했다. 여러 경로가 같은 데이터를 공유하면 경로 크기를 합산하거나 사본 하나를 지운 양이 실제 회수량과 같지 않다. 위 실제 여유량은 같은 드라이브의 전후 값이며 외부 작업의 디스크 활동 영향을 받을 수 있다.

## 제거한 내용과 이유

- 실행별 Asset Registry 캐시240곳과 오래된 AuditPreRetarget 백업의 Intermediate. 현재 빌드 캐시를 무효화하지 않으며 백업의 기준 DLL은 보존했다.
- 이전 패키지의 바이트 동일 payload104개는 보존 사본을 SHA-256으로 확인한 뒤 1차에서 정리했다. 이후 2차에서 구 패키지21개분의 실행·Cook payload와 오래된 검증용 exe/PDB/셰이더를 더 퇴역했다. 구 패키지는 현재 실행 가능한 완전 아카이브가 아니며, 1차 retained_copy도 2차 퇴역 목록과 함께 해석한다.
- 대표 비교 외 오래된 raw trace39.90GiB. 삭제 전 SHA-256·크기·경로를 남겼다. 이는 압축 보관이 아닌 퇴역이므로 과거 모든 시점의 timer/event를 다시 분석할 수 있다는 보증은 하지 않는다. 관련 CSV·집계 JSON·실행 인자·로그는 유지했다.
- 사용하지 않는 DebugGame/Shipping 중간 파일과 IDE IntelliSense/Browse 캐시. 해당 구성 재사용이나 IDE 재열기 시 해당 캐시를 재생성해야 한다. 현재 Development 빌드와 사용자 IDE 설정/레이아웃은 보존했다.
- 반복 UBA 접근 오류로 커진 FollowerReproBuild1의 UBT.json/log는 삭제 전 **원문 전체를 gzip 보관하고 해제 SHA-256 일치**를 검증했다. 압축본은 같은 폴더의 UBT.json.gz/UBT.log.gz다.

과거 hash 색인·성공/실패·측정 결과는 당시의 기록으로 수정하지 않았다. 원자료가 퇴역된 경로는 아래 명세를 따른다. 과거 실행 스크립트나 패키지의 모든 경로가 그대로 실행 가능하다는 뜻은 아니다.

## 남긴 최적화 근거

현재 판단의 시작점은 [성능 목차](../Performance/README.md), [200명 계획/결과](../Performance/Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md), [source·DLL·asset와 측정 조건/지문 JSON](../Performance/Reports/ProjectJ_200_Player_Optimization_2026-10-15.json)이다. 아래는 기존 결과를 재사용한 보존 요약이며 새 측정이 아니다.

| 기존 비교 | 보존한 관찰과 한계 |
| --- | --- |
| 6차 동일 바이너리 B1→A1→A2→B2, 로컬1+원격표현199, 1280×720 offscreen D3D12 | GPU 유휴 평균 10.526→6.188ms, 동시200 montage 행 11.922→7.421ms. 로컬LOD0·전원 표시 조건이며 실제199개 소켓 접속이 아님. 전체 frame/CPU 개선으로 확대하지 않음. |
| 초기 단일 실제 렌더 비교 | 유휴15.978→12.712ms였지만 전원 공격30.499→30.607ms. 캡처·표본/실행 차이로 전체 개선율을 확정하지 않음. |
| 2차 NullRHI 50명 A→B→B→A | CSV·집계·조건 보존. 실제200표시 렌더FPS/GPU나 원격200접속 결과가 아니며 전체 CPU 개선 미확정. |
| 미채택 후보 | 3차 retarget IK 생략은 비용 절감 미입증으로 미채택. 5차 physics 축소는 동등성 보강 뒤에도 비계측 변화가 대부분1% 안팎이라 제작 적용하지 않았다는 판단과 원자료 요약을 보존. |
| 최신 LOD 제작 | 7차 RHI13개·실제2연결 회귀는 기능/보존 검증이며 새 성능 개선율 측정이 아님. |

실제 원격 접속 수, 로컬 생성 캐릭터 수, 표시200과 NullRHI CPU는 구분한다. 기존 CPU 증가·렌더 차이가 해결됐다고 기록하지 않는다. 과거 JSON의 source/DLL 기준점은 당시 값이며 현재 작업 트리와 자동 동일시하지 않는다.

대표 raw trace는 다음을 보존했다. 전체 CSV·결과 JSON·캡처도 남겼고 10425개 파일의 크기/mtime 보존을 확인했다.

- Saved/Validation/CrowdOptimization_20261015_Execution1의 최신 Baseline1/Baseline50/After50 trace와 단계별 CSV·결과·로그·진단.
- Saved/Validation/AuthoredAnimation_20261005/Reaudit_Trace_A_trace 및 Reaudit_Trace_B_trace의 비교 trace.
- 같은 폴더 R15_CA16_CPU_Trace1_A_trace 및 R15_CA16_CPU_Trace1_B_trace의 CPU 회귀 비교 trace.
- Saved/Validation/Continuation_20261014/R15_Package5/Package/Windows의 완전 패키지 한 벌과 실행/패키지 identity 기록.
- 기존 source/config 및 실제/test Content, 현재 Binaries, baseline DLL·source/asset hash, 최신 최적화/LOD 제작 근거. 사전부터 수정돼 있던 56개 파일은 SHA-256 동일성을 확인했다.

로컬 raw 자료는 GitHub에 포함됐다는 뜻이 아니다. 새 PC에서 재분석하려면 보존한 raw trace/CSV를 별도로 옮겨야 하며, Git에는 문서·요약·지문과 재현 가능한 기준을 남긴다.

## 명세와 재발 방지

[1차 정리·압축/중복 사본 명세](Storage_Cleanup_2026-10-16.json)와 [2차 추가 퇴역·보존 trace·실제 공간 명세](Storage_Cleanup_2026-10-16_Phase2.json)에 경로·크기·이유·지문을 기록했다. 세부 실행 목록·삭제 journal은 로컬 Saved/StorageCleanup_20261016에 있다. 원문 압축 로그와 trace는 민감한 로컬 환경값을 포함할 수 있어 문서에 복사하지 않는다.

앞으로 실행별 격리 캐시는 작업 종료 후 사용 중 여부를 확인하고 정리한다. 패키지·PDB·전체 trace를 매 실행마다 영구 보관하지 않고, 유효한 기준점/비교·미해결 분석에 필요한 대표 원자료와 재현 조건을 선별한다. 자료를 퇴역할 때는 요약·지문·실행 불가/분석 불가 경계를 남긴다. 용량 보고는 논리 합계와 실제 드라이브 여유 증가를 구분한다.

## 2026-10-19 추가 정리

사용자가 이전 검증·Cook 산출물 정리를 요청하면서 필요한 로그·결과, 월드 백업·에셋 원본과 현재 빌드 파일의 보존을 지정했다. Region A도 현재 사용 여부를 확인한 뒤 정리하도록 요청했다. 실행 중인 Unreal·게임·빌드 프로세스가 없음을 확인하고, 경로·파일 크기·SHA-256을 검증한 개별 파일만 제거했다. 이 절은 [6850ed45 감사 마감](../Review/Audits/Project_J_Source_Audit_6850ed45_Closure_2026-10-19.md)의 감사 전용 정리 이후 수행한 별도 작업이다.

| 항목 | 결과 |
| --- | --- |
| 오래된 Cook·패키지·실행별 캐시 | 417개 / 1.82GiB 제거 |
| Region A 임시 프로젝트의 중복 에셋 | 현재 `Content`와 SHA-256이 같은 561개 / 4.70GiB 사본만 제거. 현재 에셋의 정리 후 해시도 일치 |
| 큰 로그·결과·대표 trace | 111개 원문 6.65GiB → 인접 `.gz` 3.15GiB. 무손실 압축으로 3.50GiB 절약 |
| 논리 파일 크기의 순감소 | 10.02GiB. 압축본 추가 크기를 차감한 값 |
| 실제 드라이브 여유 공간 관측 | 137,014,460,416 → 147,766,947,840 bytes, **10.01GiB 증가** |
| 정리 직후 여유 공간 | 137.62GiB. 이후 현재 여유 공간을 보장하는 수치가 아님 |
| 보존 검증 | 대상 밖 원본 58,028개 크기·mtime 변화 없음. 현재 `Binaries`·`Intermediate` 5,367개 metadata와 `Binaries` SHA-256 유지 |

1,089개 원문·불필요 파일을 제거했다. 이 중 111개 검증 원문은 압축본으로 보존했으며, 삭제 후 압축본을 다시 읽어 해제 크기·SHA-256이 원문과 일치함을 확인했다. 앞서 보존한 대표 trace 7개도 압축 보존에 포함된다. 같은 드라이브의 외부 작업과 하드링크·공유 데이터 영향 때문에 실제 여유 증가와 논리 감소를 구분한다. 10월 16일이나 감사 전용 정리의 확보 용량을 이번 확보량에 다시 합산하지 않는다.

### Region A 판단과 보존 범위

`Saved/WorldRegionA`는 MainWorld 실행 맵이 아니라 과거 월드 제작의 변경 전 백업과 에셋 이행용 scratch 프로젝트다. 정리 당시 `Scripts/Editor/Migrate-WorldEnvironment.py`는 이 경로의 `AssetStaging` 프로젝트를 명시적으로 입력으로 사용했다. 현재 게임 실행에 직접 필요하지 않다는 사실만으로 이 안의 고유 에셋·복구본까지 삭제하지 않았다.

`AssetStaging/Content`와 `VillageAssetStaging/Content`에서 정리 당시 프로젝트 `Content`에 같은 바이트의 생존 사본이 확인된 항목만 정리했다. 월드 제작 백업·작업 메타데이터 **1.92GiB**, 당시 별도 원본이나 동일 사본을 확인하지 못한 staging 콘텐츠 **13.74GiB**를 보존했다. 정리 후 Region A 논리 크기는 약 **15.65GiB**다. `MainWorld` 및 기존 `Content`, `AssetIntake`, 에셋 원본·월드 백업, root `Binaries`·`Intermediate`·`DerivedDataCache`, `Source`·`Config`는 삭제 범위 밖이다. 제작 도구의 체크포인트 입력인 `Saved/Validation/StarterWorld`도 유지했다.

사용자가 이후 작업을 알린 뒤 **2026-10-19 22:08 KST**에 실제 경로를 재조회했다. `Saved/WorldRegionA`는 존재하며 재집계한 크기도 16,808,103,063 bytes / **15.65GiB**다. `AssetStaging` 10.74GiB, `VillageAssetStaging` 3.00GiB와 백업·기타 자료가 남는다. 이 재조회는 존재·용량 확인이다. 이후 MainWorld·에셋 이행을 반영한 새 중복 해시 대조는 수행하지 않았으므로, 앞선 미확인 판정을 현재에도 동일 사본이 없다는 근거로 사용하지 않는다.

### 재사용 시 제한과 근거

- 과거 완전 패키지 `Saved/Validation/Continuation_20261014/R15_Package5/Package/Windows`의 실행 payload도 이번에 퇴역했다. 위 10월 16일 보존 목록은 당시 상태이며, 현재 이 패키지를 그대로 실행할 수 없다. 로그·결과·identity는 남지만 실행하려면 Cook·Stage를 다시 수행해야 한다.
- 압축한 과거 CSV·JSON·로그·trace는 원래 경로를 요구하는 분석 도구에 사용하기 전에 인접 `.gz`를 해제해 원래 파일명으로 복원한다. 원문 SHA-256과 크기는 `Archives.json`에서 대조한다. 압축본은 원문 폐기와 구분하며, raw trace가 더 이상 분석 불가능해졌다고 처리하지 않는다.
- Region A scratch 프로젝트 일부 사본을 정리했으므로 과거 migration 도구를 그대로 재실행하지 않는다. 의존 자료를 복구하고 현행 경로·대상을 작업 사본에서 검토해야 한다.
- 로컬 근거는 `Saved/StorageCleanup_20261019/Report.txt`, `Result.json`, `Plan.json`, `Inventory.json`, `Archives.json`, `VerifiedActions.json`, `Deleted.jsonl`, `CurrentBuildProtected.json`이다. 삭제 대상·SHA-256·생존 사본·압축 복원 검증·실제 여유 공간을 보존한다. 해당 로그·원자료가 Git에 포함됐다는 뜻은 아니며, 이 문서에는 환경 비밀값과 사용자 절대 경로를 복사하지 않았다.

이번 추가 정리는 파일 보존·공간 정리 검증이며 새 게임 동작·Cook·성능 검증이 아니다. 소스와 현재 빌드는 변경하지 않았다.

## 2026-10-20 GASP 작업 산출물 추가 정리

Manny 이관 후 늘어난 용량과 불필요 스크립트의 안전한 정리를 요청받아 GASP 실험·작업 폴더를 확인했다. 현재 프로젝트의 에셋·Source·Config·빌드와 영구 테스트는 유지했다. 스크립트 전체 크기는 약 1MiB로 작으며, 주요 증가분은 과거 실험용 복제 프로젝트의 생성된 빌드 파일과 반복 실행 원자료였다.

| 정리 대상 | 처리 |
| --- | --- |
| `Saved/GASP58Migration_20261019/Experiments`의 두 실험 프로젝트 | 종료 상태 확인 후 `Project_J`의 Binaries/Intermediate/DDC와 `GameAnimationSample`의 Intermediate/DDC만 제거. 2,446개, 7.84GiB. Content·Source·설정·실행 결과·실험 자산은 보존. |
| 완료된 GASP/Manny 세션 스크립트 | 일회성 이행·점검·PIE 준비 스크립트 265개를 ZIP 보관하고 원래 실행 위치에서 제거. 각 원문의 SHA-256과 ZIP 해제 결과 일치 확인. 현행 `Scripts` 도구와 공용 콘솔 제출 helper는 유지. |
| 큰 로그·Blueprint 텍스트 export·포즈/입력 원자료 | 105개, 원문 1.66GiB → 인접 `.gz` 약 91.1MiB. 삭제 후 압축본을 다시 해제 스트리밍해 모든 원문의 SHA-256 일치 확인. 성공·실패 근거는 폐기하지 않음. |
| 이번 작업/도구의 Python bytecode | 재생성 가능한 캐시 19개 제거. |

압축본을 포함한 논리 파일 크기 순감소는 **9.41GiB**다. 최초 용량 확인 시 C 드라이브 실제 여유 164,491,563,008 bytes에서 정리 후 174,587,039,744 bytes로 **9.40GiB 증가**를 관측했다. 다른 디스크 활동과 파일 할당 영향이 있으므로 논리 합계와 실제 여유 변화를 구분한다. 정리 직후 여유는 약 162.60GiB이며 이후 용량을 보장하지 않는다.

파일 정리 직후 보존 대상으로 기록한 **33,810개 파일의 크기·mtime이 그대로**임을 확인했다. 현재 Content/Source/Config/Scripts/Binaries/Intermediate/Plugins와 해당 작업의 원본·백업 에셋을 포함한다. 이후 아래의 감사기 receipt 보호 조건 1개는 의도적으로 보완했으며 정리로 인한 변조와 구분한다. 별도로 UEFN AnimSequence 448개의 최종 SHA-256도 이전 값과 모두 일치한다. 이번 작업으로 애니메이션·기타 Content 에셋을 삭제하거나 변경하지 않았다. 현재 프로젝트의 PCH·DLL·EXE·PDB·플러그인 빌드 캐시, DDC·IDE 캐시와 WorldRegionA 제작/복구 자료도 보존했다. Git/LFS 이력은 정리하지 않았다.

현행 `Scripts/Validation/Run-GASP58Regression.ps1`과 실제 PIE 검사기, native 테스트, `Scripts/Audit/Audit-GASPAnimationContracts.py` 등은 계속 필요해 유지했다. 감사기의 기존 receipt 보호 조건은 `.json.gz`도 검사하도록 보완했다. 압축된 과거 결과가 있다는 이유로 같은 label을 새 결과에 재사용하지 않는다. Python 문법과 원문/압축 receipt 충돌 조건을 확인했으며, runtime 변경이 없어 C++ 빌드나 에디터 실행은 반복하지 않았다. 에디터는 종료 상태를 유지한다.

로컬 명세는 `Saved/StorageCleanup_20261020/Inventory.json`, `Plan.json`, `Deleted.jsonl`, `Result.json`, `Verification.json`에 있다. 일회성 스크립트는 `RetiredSessionScripts.zip`, 이번 정리 실행 원형은 `CleanupImplementation.zip`에 보관했다. 입력 원문과 압축 위치·해시가 Plan에 기록돼 있다. 원자료를 다시 분석하려면 인접 `.gz`를 해제하고, 퇴역 스크립트를 조사하려면 ZIP에서 필요한 파일만 꺼낸다. 실험용 과거 Binaries/Intermediate는 폐기했으므로 과거 실험 프로젝트가 즉시 실행 가능한 완전 아카이브는 아니며 실행하려면 해당 당시 소스/환경으로 다시 빌드해야 한다.

파일 정리 후 빈 폴더 마감에서 Windows PowerShell의 `Path.GetRelativePath` 미지원이 확인돼, 삭제 명세의 완료를 먼저 대조하고 호환 방식으로 빈 폴더만 마감했다. 파일 삭제 작업을 다시 실행하지 않았다. 상세는 Result에 남겼으며 최종 보존·압축 복원 검증은 통과했다.

## 2026-10-23 반복 검증 기록 압축

최근 용량 증가에 대한 안전한 정리 요청으로, 종료된 세션의 `Saved/Validation`·`Saved/Logs`에서 2MiB 이상인 JSON/JSONL/CSV/log/utrace 원자료 358개를 인접 `.gz`로 무손실 압축했다. 원문 3,022,388,202 bytes → 압축본 342,440,219 bytes로 논리 크기가 **2.50GiB 감소**했다. 성공·실패·사용자 재현 기록을 모두 보존했으며, 원문 제거 전과 제거 후 압축 해제 스트리밍으로 SHA-256 일치를 확인했다. 기존 분석기가 원문 경로를 요구하면 해당 `.gz`를 원래 이름으로 해제해야 한다. 요약·Automation index·이미지·검증 스크립트와 작은 기록은 그대로 유지했다.

에디터·빌드·IDE 프로세스가 없는 상태에서 처리했다. Content·원본/백업 에셋·Source·Config·Scripts·현재 Binaries/Intermediate·DDC·IDE 캐시·Git/LFS 이력은 정리 대상에서 제외했다. 기존 수정/미추적 파일 45개의 SHA-256도 정리 전후 일치했다. 실행 로직 변경이 없어 빌드나 PIE를 반복하지 않았다.

C 드라이브 여유는 158,205,583,360 → 163,498,909,696 bytes로 **4.93GiB 증가를 관측**했다. 이는 파일 논리 감소량과 다르므로 전부 이번 정리의 회수량으로 귀속하지 않는다. 정리 후 관측 여유는 약 152.27GiB다. 원문/압축 경로·크기·해시와 측정값은 로컬 `Saved/Validation/StorageCleanup_20261023/VerifiedCompression.json`, `Result.json`에 남겼다.

## 2026-10-25 무기·액션 복귀 작업 후 정리

최근 작업의 스크립트와 용량 증가를 확인하고, 종료된 에디터/빌드 프로세스가 없는 상태에서 다음 범위를 정리했다. 원본/제작용·테스트 에셋과 현재 빌드 캐시는 보존한다.

| 대상 | 처리와 유지 이유 |
| --- | --- |
| Git에 있던 고정 작업 2개 | `Scripts/Editor/Bake-PrototypeMapBounds.py`, `Scripts/Audit/Analyze-ContinuationCPUCallTrees.py` 퇴역. 각각 완료된 최초 지도 굽기와 과거 CA 캡처/출력에 고정된 분석이다. 완료 데이터·결과와 실제 소비자는 유지했고, 현재 코드/설정/CI 호출이 없는 것도 확인했다. 원본은 Git `b328b948`과 로컬 ZIP에 남긴다. |
| 최근 완료 세션 스크립트 42개 | `Saved/AttackStop_20261024`, `Saved/WeaponBasic_20261024`에서만 선별해 같은 ZIP에 보관하고 실행 위치에서 제거. 공용 `run_mcp.py`와 `Scripts`의 영구 액션/네트워크 회귀 도구는 유지. |
| 큰 원자료 23개 | JSON·에디터 로그를 인접 `.gz`로 무손실 보관. 실패한 재현/중간 검수와 최종 성공 자료를 모두 남겼으며 원문 제거 전·후 압축 해제 스트리밍 SHA-256을 대조. 원문 경로를 요구하는 분석은 먼저 해제해야 한다. |
| UI 생성·Foley 설정·Region A 이행 및 제작/성능 검증 도구 | 재사용 계약이나 복구/제작 역할이 있어 유지. 특히 UI 생성기는 기존 에셋 보존 검증기의 실제 입력이며, Region A 고유 staging/복구 자료는 미사용으로 판정하지 않았다. 과거 이행 도구의 즉시 재실행 가능성을 보장하지 않는다. |

ZIP을 포함한 논리 파일 크기 순감소는 **54,095,727 bytes(약 51.6MiB)**다. 정리 직전/직후 C 드라이브 여유는 160,266,162,176 → 160,263,135,232 bytes로 약 2.9MiB 감소를 관측했다. 디스크 전체의 동시 변화와 파일 할당이 포함되므로 논리 감소량을 실제 여유 증가로 주장하지 않는다. 직후 여유는 약 **149.3GiB**다. 무기 원본 FBX/텍스처·실제 Content/SourceArt·현행 Binaries/Intermediate/DDC·Git/LFS 이력은 삭제하지 않았으며, 이번 작업으로 전체 디스크 용량이 과거 시점과 같아졌다고 판정하지 않는다.

로컬 `Saved/StorageCleanup_20261025/Plan.json`에 각 원문 경로·크기·SHA-256, `RetiredScripts.zip`에 실행 원형, `Result.json`에 재검증과 공간 측정값을 남겼다. 기존 수정/미추적 11개 파일은 정리 전후 SHA-256이 일치했다. 같은 커밋에 포함되는 로코모션 수정과 그 최종 native/PIE 검증은 [애니메이션 라이브러리](../Animation/Authoring/Player_Animation_Library.md)의 10월 24일 기록이며, 파일 정리를 이유로 엔진 빌드를 반복하지 않는다.

정리 후 Python 회귀 **48개**, 자산 검증 래퍼 모의 **11조건**, provenance 비교 모의 **6조건**과 현행 Python **165개 문법 검사**가 통과했다. 문서 링크 3,006개 중 오류 1개는 기존 `Naturalness_20261006/FinalLocal/Automation/index.json` 로컬 기록 링크이며, 이번 변경으로 새로 생긴 오류는 없다. 역사적 링크를 삭제해 통과로 처리하지 않았다.
