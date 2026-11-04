# MMORPG 검증 산출물 안전 정리 — 2026-10-29

사용자가 용량 증가로 안전 정리 후 작업을 이어가도록 요청했다. 소스·에셋·Git/LFS·직접 빌드 결과와 현재 증분 빌드 캐시는 보존하고, 종료된 검증의 이전 패키지19개·Cook2–13과 해당 격리 캐시만 정리했다.

| 관측 | 결과 |
| --- | ---: |
| 삭제 파일 / 잠금·변경으로 보류 | 1,169 / 0 |
| 삭제 논리 크기 | 63,979,023,868 bytes / 59.585GiB |
| C: 여유 공간 전 → 후 | 87,723,581,440 → 151,861,768,192 bytes |
| 실제 여유 증가 관측 | 64,138,186,752 bytes / 약59.733GiB |
| 보존 비교 | 소스1,197개·runtime38개·Git index 동일 |

실제 여유 증가는 같은 시간대의 다른 디스크 활동도 포함하며 논리 삭제 합계와 동일한 수치로 취급하지 않는다. 파일마다 정확한 대상 경로·reparse 부재·크기/수정 시각·배타적 열기·SHA256·삭제 직전 불변을 확인했다. 엔진/빌드/검증 프로세스가 없는 상태에서 수행했고 프로세스를 종료하지 않았다. [삭제 경로·SHA·보존 사유](Storage_Cleanup_2026-10-29.json)를 따른다.

## 보존한 실행과 자료

- Stage3는 미해결 계측 종료 문제의 원래 실행 파일/패키지이므로 유지했다.
- Stage21은 마지막 전체 MainWorld 콘텐츠 패키지, Stage22는 의상/CSV 실패와 두 후속 성공 실행의 공통 패키지, Stage24는 최신 Development Game32 기준점으로 유지했다.
- ShippingStage1과 재사용할 Cook14/15 전체를 유지했다. 실패 Cook1·전체 실행 인자/결과/로그·CSV·trace·화면·소스 지문도 유지했다.
- 프로젝트 루트 Intermediate/Binaries/DDC와 월드 제작·원본·백업, SaveGame·Autosaves, Git index/refs/objects/LFS objects는 정리 대상이 아니다.

퇴역한 Package/Cooked 디렉터리는 text manifest·utoc·일부 메타데이터만 남아 있다. 실행 루트에 `Retired_*.json`을 남겼으며, 그 디렉터리의 예전 정확한 바이너리/콘텐츠를 재실행하거나 완전한 Cook으로 재사용할 수는 없다. 기존 수치·CSV·실패 로그 재분석과 보존한 대표 실행은 구분한다. 전체 원자료를 새 ZIP으로 복제해 용량을 다시 늘리지 않았다.

## 증가 원인과 처리의 한계

연속 검증의 고유 Stage 디렉터리에 같은 Cook 콘텐츠를 반복 보관하면서 패키지가 약1.2–3.1GiB씩 누적됐다. 정리 전 Saved는 약134.5GiB, 검증 자료는 약109.6GiB, 연속 검증 폴더는 약73.7GiB였다. 필요한 증거를 보존한다는 이유로 이전 대형 배포물까지 모두 유지한 것이 주된 저장소 내부 증가 경로다.

표본 Stage20의2.224GiB 컨테이너는 저장소와 Documents의 `.tmp.driveupload` 두 경로로 하드링크돼 있었다. GoogleDriveFS도 실행 중이었고, 저장소 사본을 지운 직후에는 공간이 거의 반환되지 않았다. 외부 임시 파일이나 동기화 설정·프로세스는 건드리지 않았으며 이후 관측한 외부 링크가 사라지고 실제 여유가 반환됐다. 외부 동기화의 영구 원인 수정으로 주장하지 않는다. 지원되는 Drive 설정은 [Google 공식 안내](https://support.google.com/drive/answer/13470231)를 참고하되 이번 작업에서는 변경하지 않았다.

이번 결과는 **종료된 산출물의 보존 범위를 줄인 정리**다. 이후 큰 Package/Cooked/격리 UserDir을 Documents 밖 실행별 임시 저장소로 분리하고 작은 실행 근거는 기존 Saved에 두는 구조를 적용한다. 임시 저장소도 무제한 보존하지 않으며, 새 배포 검증 때 현재 기준점과 미해결 재현에 필요한 실행을 먼저 골라 이전 대형 산출물을 퇴역한다. 무조건 전체 Cook/Stage를 반복하거나 현재 빌드 캐시를 삭제하지 않는다.

## 이후 실행 저장 경로와 재검증

기존 Tests/Packaging runner와 작은 공통 `Continuation-ScratchPaths.ps1`을 변경했다. 큰 UserDir/Cooked/Package만 OS에 영속 설정된 User TEMP(없으면 Machine TEMP)의 `ProjectJContinuation/<checkout명-경로SHA16>/<실행명>`에 둔다. 도구 호출마다 달라질 수 있는 process TEMP를 다음 실행에 저장해 재사용하지 않는다. Saved의 ABSLOG·인자·process·automation report와 provenance는 유지한다. process.json의 실제 `userDirectory`/Cook `output`/Stage `packageDirectory`를 소비하며, 예전 `$out/Package`를 추측하거나 복사 fallback으로 숨기지 않는다.

같은 checkout/실행 이름의 기존 데이터를 덮지 않고, Documents·저장소 내부/이를 포함하는 Temp·reparse 경로·경로 이탈을 거절한다. Stage는 현재 보존한 정확한 Cook14/15 또는 이 checkout의 Temp `<실행명>/Cooked`와 공식 Windows package-store marker만 허용한다. 퇴역한 Cook13 경로는 거절했다. 기존 기록 경로·OS 환경변수·엔진·Drive 설정은 바꾸지 않았다.

`MMOFoundation_20261029_Batch49StorageNative1`은 기존 Editor125의18개 회귀·경고0·7.492초/actual exit0이었다. 소스/runtime은 실행 중 동일했고 실제 UserDir은 Temp, 작은 실행 근거는 Saved에 생성됐다. 새 격리 데이터는15개 파일/166,450,696bytes였다. 가장 큰 파일의 관측 하드링크는 Temp의 자기 경로 하나였으며, 별도 호출에서도 checkout root가 같고 중복 이름·상위 경로 요청은 거절됐다. 이는 새 저장 경로의 한 실행 검증이며 반복적인 전체 Cook/Stage·장기 동기화 안정성을 인증한 것은 아니다.

새 전체 Cook/Stage와 C++ 빌드는 하지 않았다. 다음 필요한 실제 배포에서 Temp의 실제 packageDirectory와 exe/콘텐츠 소비까지 확인한다. 이 변경은 **생성 산출물의 위치를 분리한 구조적 회피·억제**이며 Temp도 자동으로 무한 보존해도 되는 곳은 아니다. 현재 기준점·미해결 재현은 의도적으로 보존하고 이전 대형 산출물은 결과/원자료 지문을 남긴 뒤 퇴역한다.

종료 후 별도 두 시점의 새 격리 데이터는15파일/166,450,696bytes로 동일했고, 마지막 빌드/검증 프로세스는0이었다. 후속 검증까지 마친 C: 여유는140.523GiB였다. 정리 직후141.432GiB와 구분하며 전체 디스크 변화가 이 격리 데이터 크기와 같다고 가정하지 않는다. 짧은 종료 후 관측을 장기 누수·동기화 안정 보장으로 확대하지 않는다.
