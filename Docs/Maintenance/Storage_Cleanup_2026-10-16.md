# 로컬 검증 산출물 정리와 최적화 근거 보존 — 2026-10-16

사용자가 과도하게 늘어난 로컬 산출물 정리를 요청했다. 처음에는 재생성 캐시·정확한 중복만 정리하고 전체 trace를 보존했으나 충분하지 않아, 추가 요청에 따라 대표 원자료를 선별하고 오래된 trace·실행 산출물까지 정리했다. 소스·Config·Content·테스트 자산·현재 Development 빌드와 다른 작업 변경은 보존했다. 새 빌드·성능 시험은 실행하지 않았다.

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
