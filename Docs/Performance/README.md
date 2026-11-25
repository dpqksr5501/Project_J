# 성능 설계·수집·결과

Iris 필드 40인·던전 24인 완주와 별도 24인 Iris/Generic 대조는 [전용 서버 애니메이션·부하 검증](../Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md#전용-서버-병렬-평가montagetiming-및-24인-irisgeneric-ab-2026-11-24-후속)에 있다. [야간 재검증](../Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md#문서-기준-로컬-재검증--2026-11-24-야간)은 필드 16·24·32인 완주, 40인 타격 후 RAM 보호 중단, 던전 16인 D3D12 실행을 기록한다. [수용량 추정](../Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md#수용량-추정과-분리-호스트-검증)의 74~120인은 Burst p95가 33.33ms에 닿는 외삽 범위다. 그래픽·의상은 관측 클라이언트의 프레임과 작업 대기를 보고, 서버는 처리·복제·틱 간격을 따로 읽는다.

200인 필드 보스의 목표와 현재 판정, 다음 서버 측정은 [독립 기술 감사 후속 현황](../Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md#200인에서-확인한-사실)을 기준으로 읽는다. 아래 결과는 화면 표시, 서버 실접속, 타격과 틱을 각 실행 조건에 따라 기록한다.

## 스펠블레이드 원격 경량 망토 적용 · 2026-11-24

`BP_Spellblade.Cloak`에 `SK_Spellblade_cloak_remote`를 연결했다. 소유 캐릭터는 기존 망토와 몸체 충돌을 사용한다. 다른 플레이어의 망토는 클라이언트에서 기존 비동기 외형 로더로 읽고 준비 전에는 원본을 표시한다. 엔진 기본 Cloth Tick 설정을 사용한다. 세 Cloth 섹션, 렌더 LOD 0의 정점 2,798개와 인덱스, 스켈레톤과 충돌 에셋을 유지했다. 물리 메시 정점은 2,534→678개다. 적용 전에 보관한 원본 망토·머리카락·치마·망토 충돌 에셋의 SHA-256을 다시 확인했다.

필드 샌드박스 `Sandbox_FieldBoss_01`과 `Test_MMO_Dungeon`의 `Dungeon_Starter_Boss`에서 Generic 전용 서버와 D3D12 1280×720 관측 클라이언트를 같은 PC로 실행했다. 각 행은 참가자 전원이 화면에 잡힌 공격 후 프레임의 중앙 FPS와 p95다. 이전 원본 값은 같은 장소와 인원의 별도 실행이다.

| 전투 | 실접속 | 원본 중앙 FPS | 경량 중앙 FPS | 경량 프레임 p95 | 경량 표본 | 서버 월드 틱 p95 | 실제 틱 간격 p95 | 타격 확인 | 서버 종합 기준 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 필드 보스 | 16 | 23.92 | 90.67 | 14.44ms | 58 | 21.02ms | 34.03ms | 16/16 | 통과 |
| 던전 보스 | 16 | 24.76 | 96.00 | 14.37ms | 237 | 19.93ms | 34.18ms | 16/16 | 통과 |
| 필드 보스 | 20 | 24.33 | 99.45 | 16.53ms | 230 | 26.30ms | 34.13ms | 20/20 | 통과 |
| 던전 보스 | 20 | — | 99.46 | 17.79ms | 222 | 25.81ms | 34.21ms | 20/20 | 통과 |
| 필드 보스 | 24 | — | 95.86 | 18.97ms | 213 | 28.94ms | 34.58ms | 24/24 | 통과 |
| 던전 보스 | 24 | — | 89.69 | 19.23ms | 205 | 31.58ms | 34.53ms | 24/24 | 미달 |

모든 실행에서 소유 1명의 원본과 원격 전원의 경량 망토에 활성 Cloth 인스턴스와 기본 Tick 설정을 확인했다. 전원 타격, 체력 복제, 정상 접속 종료를 확인했다. 공격 단계 월드 처리 시간 p95는 30Hz 예산 33.33ms 이내였고, 같은 PC의 원본 실행 대비 서버 처리 시간이 줄어든 근거는 없었다. 24인 던전은 공격 p95 기준을 충족했으나 대기 단계 p95가 25.25ms로 기준 25ms를 넘어 서버 종합 기준이 미달이었다. 종합 기준은 러너의 `worldTickBudgetMet`이며 실제 틱 간격과 기능 통과는 각각 따로 기록한다. 24인 던전의 최소 가용 물리 메모리는 5,065MiB로 보호선 4,608MiB에 가까워 이 실행에서는 인원 증가를 멈췄다. [종합 기준과 결과 저장](../../Scripts/Validation/Run-LoopbackCohort.ps1#L281) · 24인 던전 원자료 (로컬 `Saved/Validation/Continuation_20261014/RemoteProxyDungeon24_20261124/verification.json`)

재현은 `Scripts/Validation/Run-LoopbackCohort.ps1`의 `-MeasureCombat -BossCombat -RenderedClient -LegacyReplication -VerifyRemoteCloak`와 `-FieldArena` 또는 `-DungeonCombat`을 사용한다. 원자료는 `Saved/Validation/Continuation_20261014/RemoteProxy{Field,Dungeon}{16,20,24}_20261124`에 있다. Editor 증분 빌드와 D3D12 의상 배정·인터랙터 수명·모듈 틱 회귀 3건을 통과했다. 새 경량 메시와 Blueprint의 Data Validation은 유효했으며, 메시의 기존 Cloth LOD 수동 검수 안내 1건을 확인했다. 측정 원자료와 변경 전 Blueprint는 중앙 보관소의 `INDEX.csv`에서도 찾을 수 있다.

## 스펠블레이드 망토 계산 주기와 물리 메시 비교 · 2026-11-24

Editor D3D12 1280×720, 같은 이동·착지·기본 공격과 카메라를 사용했다. 단독 프로세스의 실제 렌더링 군중으로 각 인원에 로컬 1명이 있다. `-CloakHz`는 시험에서 나머지 망토의 네이티브 Cloth Tick 간격만 조정한다. 원래 포즈 갱신과 네이티브 완료 경로를 사용하며, 중간 프레임용 별도 천 표면 보간은 없다. Hz 실측은 공격 단계 전체 약 8초의 겉단 Tick 호출 수다. 솔버 내부 서브스텝 수는 이 수치에 포함하지 않는다. 영상은 별도 고정 시간 간격 실행으로 수집했다.

| 인원 | 원격 역할 목표 / 실측 | 이동 중앙 FPS | 동시 공격 중앙 FPS | 동시 공격 p95 | 공격 표본 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 100 | 기본 | 34.63 | 25.51 | 43.00ms | 28 |
| 100 | 10 / 10.02Hz | 75.21 | 38.59 | 38.25ms | 41 |
| 100 | 15 / 14.90Hz | 63.76 | 29.81 | 40.00ms | 35 |
| 100 | 20 / 20.06Hz | 61.48 | 27.08 | 40.82ms | 31 |
| 100 | 30 / 29.92Hz | 34.85 | 26.10 | 45.12ms | 28 |
| 150 | 20 / 20.06Hz | 25.45 | 18.90 | 72.03ms | 19 |
| 200 | 20 / 20.06Hz | 19.42 | 14.35 | 86.43ms | 14 |
| 200 · 경량 물리 메시 | 20 / 20.06Hz | 53.86 | 21.78 | 58.30ms | 23 |

동시 공격 표본은 전원 몽타주 구간이며, 스크린샷 요청 및 다음 두 표본을 제외했다. 200체는 한 렌더 프로세스이며 최소 물리 RAM / 커밋 여유는 12,596 / 14,996MiB였다. 100체 최초 간격 적용에서는 99개 원격 역할 망토가 한 프레임에 같이 계산한 표본이 있었다. 평균 주기와 별개로 프레임별 예약 분포도 확인한다.

100체 20Hz에서 준비 구간의 첫 예약을 분산하자 공격 단계 프레임당 예약 개수 p95는 99→50으로 감소했다. 최대는 여전히 99였고, 동시 공격 프레임 p95는 40.82→43.38ms였다. 이 후보는 예약 분포 완화까지만 확인했다. 200체 경량 메시의 최소 물리 RAM / 커밋 여유는 12,471 / 15,007MiB였다.

물리 메시 사본 `SK_Spellblade_Cloak_PhysicsProxy`는 세 섹션의 물리 정점을 **2,534→678**로 줄였다. 화면용 LOD0 정점 2,798개와 인덱스 수를 유지하고 물리 가중치를 재매핑했다. 세 새 물리 섹션에서 고정 정점과 움직이는 정점을 확인했다. 기존 몸 충돌 캡슐 10개를 재사용한다. 100체에서 기본 계산 주기로 비교한 동시 공격 중앙값은 25.51→42.49FPS, p95는 43.00→28.01ms였다. 원본 BP, 망토와 몸 충돌 에셋의 SHA-256은 이전 기본 적용 검증과 일치한다. 성능과 외관 비교는 원본과 사본을 각각 실행했다.

영상은 `Saved/Validation/SpellbladeClothCadenceReview_20261124`에 있다. 각 영상은 같은 회전·점프와 착지·공격 구간 순서로 약 6초, 800×450, 30FPS 재생이다. 군중 원자료는 `Saved/Validation/WholeProjectAudit_20261013/ClothCadence*_20261124` 아래 `frames.csv`, `cloth-cadence.csv`, `verification.json`에서 확인할 수 있다.

Generic 실접속 시험도 필드 샌드박스와 던전 각각 16인에서 수행했다. 각 장소에서 원격 역할 15명의 망토에 20Hz를 적용했고, 5초 관측 구간에서 네이티브 Tick을 각각 1,500회와 1,470회 기록했다. 필드는 공격 중앙 25.23FPS, 프레임 p95 46.02ms, 서버 월드 틱 p95 18.09ms였다. 던전은 23.96FPS, 50.26ms, 19.65ms였다. 두 장소 모두 16/16 타격, 체력 복제, 정상 종료를 확인했다. 원자료는 `Saved/Validation/Continuation_20261014/ClothCadence{FieldArena,DungeonCombat}16Hz20_20261124`에 있다.

Editor 증분 빌드를 통과했다. 경량 사본은 `Saved/Validation/WholeProjectAudit_20261013/ClothPhysicsProxy{Native100,200Hz20}_20261124`에서 다시 로드해 렌더링과 전투 몽타주를 확인했다. 영상 7개의 AVI 인덱스와 시작·중간·마지막 JPEG 프레임을 확인했다. 스키닝 영상은 기존 본 포즈를 사용하는 대조군이다.

## 실접속 전투의 의상 작업 대기 · 2026-11-24

앞선 20인 필드에서 공격 전 프레임 중앙값은 40.38ms, 공격 중은 41.09ms였다. 공격 중 GPU 중앙값은 2.86ms였다. 동시 접속과 가용 메모리를 확인해 CPU 추적은 같은 부하가 재현된 16인에서 수집했다. 시야·캐릭터·공격 입력을 고정했고, 진단 변경은 지정한 프로세스에만 적용했다.

| 필드 16인 조건 | 공격 중앙 FPS | 공격 프레임 p95 | 검증 범위 |
| --- | ---: | ---: | --- |
| 기본 | 23.92 | 49.33ms | 16/16 타격과 종료 |
| CPU 추적 | 23.32 | 49.89ms | 기준 재현 |
| 헤드리스 서버·부하 생성기만 코어 예산 4 | 25.99 | 43.96ms | 16/16, 렌더 프로세스는 기존 8코어 |
| 렌더러의 Chaos Cloth 끔 | 236.80 | 6.32ms | 16/16, 원인 격리 |
| 기존 예산으로 원격 의상 15개 일시 중단 | 177.12 | 9.86ms | 16/16, 로컬 의상 1개 활성 |
| Chaos 내부 병렬 호출 끔 | 13.22 | 92.53ms | 16/16, CVar의 true 확인 |
| Cloth 내부 병렬 배치 기준 8,192 | 24.96 | 47.85ms | 16/16 |
| 프레임 시간에 따른 Cloth solver 반복 수 조정 끔 | 40.68 | 30.11ms | 16/16, 의상 16개 활성 |

16인 CPU 추적에서 공격 직전 44프레임의 게임 스레드 `STAT_EndParallelClothTask` 구간 중앙값은 34.82ms였다. 실제 병렬 Cloth 작업 구간과의 교집합 중앙값은 34.72ms였다. 화면 최초 이펙트 직후 4프레임에서 애니메이션 갱신과 완료는 각각 0.85ms와 0.32ms, Niagara 게임 스레드 틱은 0.10ms였다. 첫 이펙트 생성 스코프 한 건은 12.82ms였고 Editor Niagara 컴파일 요청을 포함했다. 서로 중첩하는 범위의 시간은 합산하지 않는다. 병목은 지속적인 의상 완료 대기이며, 같은 PC의 부하 경합은 영향을 주는 조건 중 하나다. 프레임 시간 비례 반복 수가 비용을 키울 수 있는 것은 엔진 코드와 진단 조합에서 확인했다. 전역 반복 수 변경은 로컬 의상 해석에도 영향을 준다.

`ProjectJ.ClothBudget.PauseOptInRemote`는 기본 0인 공통 클라이언트 정책이다. 제작자가 허용한 비로컬 의상만 기존 실행 관리의 페이드·native tick 완료·드레인으로 일시 중단한다. 로컬·관전·미확정 소유권, 미동의 의상과 검증할 수 없는 바인딩은 기존 보호 계약을 따른다. 근거리 전체 품질 우선권은 이 모드에서 제작자 동의보다 앞서지 않는다. 모드를 해제하면 기존 vertex 예산 정책이 다시 적용되며 예산이 0이면 원래 blend/suspension 상태를 복구한다.

| 공통 모드 검증 | 공격 중앙 FPS | 공격 프레임 p95 | 서버 월드 틱 p95 | 타격 / 재개 확인 |
| --- | ---: | ---: | ---: | --- |
| 필드 16 | 160.49 | 8.95ms | 21.43ms | 16/16, 원격 의상 15개 재개 |
| 던전 16 | 172.00 | 8.20ms | 21.61ms | 16/16, 원격 의상 15개 재개 |
| 필드 20 | 170.27 | 10.04ms | 26.43ms | 20/20, 원격 의상 19개 재개 |

검증 중 로컬 의상이 계속 활성인 상태로 공격했다. 원격 의상은 테스트 프로세스에서만 동의시켰다. 모드를 끈 뒤 원래 blend·suspension 값과 각 의상의 native 완료 프레임을 확인했다. 캐릭터 전원 표시와 체력 복제, 정상 종료도 유지됐다. 같은 PC의 20인 서버 p95는 앞선 기본 실행의 21.39ms에서 26.43ms로 증가했고 송신량은 약 1,935→1,939KiB/s였다. 20인의 최소 물리/커밋 여유는 5,210/5,559MiB였다. 개별 제작 의상의 최종 실루엣과 전환 시각 승인은 별도로 확인한다.

CPU 타이밍 원자료, export 설정과 재현 요약은 `Saved/Validation/Continuation_20261014/ConnectedFieldClientCPU16_20261124`의 `Rendered.utrace`, `ClientExport.rsp`, `CriticalPath.json`, `SummarizeTrace.py`에 있다. 렌더 타이밍을 반복할 때 `Run-LoopbackCohort.ps1 -RenderedTrace`를 추가한다. 모드 검증은 `-RenderedClothProbe ParkOptInRemote`로 실행하며 공격 구간 뒤 모드 해제와 native 재개를 확인한다.

공통 모드 원자료는 같은 검증 폴더의 `ConnectedFieldOptionalCloth16_20261124`, `ConnectedDungeonOptionalCloth16_20261124`, `ConnectedFieldOptionalCloth20_20261124`다. Editor 증분 빌드와 실제 D3D12에서 의상 배정·인터랙터 수명·모듈 의상 틱 회귀 3건을 통과했다. 회귀 결과는 `OptionalClothModeRegression_20261124/index.json`에 있다.

## 실접속 군중과 월드 장면 · 2026-11-24

`Run-LoopbackCohort.ps1 -MeasureCombat -BossCombat -RenderedClient`는 전용 NullRHI 서버, 독립 D3D12 플레이어 한 명, 나머지 NullRHI 소켓 클라이언트를 같은 전투에 연결한다. `-FieldArena`는 `Test_MMO_Sandbox`에 배치된 `Sandbox_FieldBoss_01`을, `-DungeonCombat`은 `Test_MMO_Dungeon`의 `Dungeon_Starter_Boss`를 기준으로 클래스와 위치를 재사용한다. 테스트 서버가 보스 체력을 설정하고 스펠블레이드의 기본 장비·조준·공격 입력·GAS 피해를 참가자별로 확인한다. 참가자 전원의 최종 보스 체력 복제와 종료까지 확인해야 통과한다. MainWorld B01·B02의 배치된 보스 소유자에는 NPC 클래스가 아직 바인딩되지 않았다.

렌더 클라이언트는 1280×720 D3D12의 고정 전투 카메라를 사용한다. 공격 RPC 수신 전 2초, 수신 후 3초 동안 프레임·Game/Draw/GPU 시간과 화면 투영 및 최근 렌더 조건을 만족한 캐릭터 수를 기록한다. 스크린샷 요청 직후 두 프레임은 통계에서 제외하며 원자료에 표시한다. 서버 공격 단계는 5초다. 두 프로세스의 시간 구간과 집계는 독립이다. FPS는 클라이언트 프레임 중앙값의 역수다. 네트워크는 서버 NetDriver 집계이며, 모든 역할이 한 PC의 CPU·메모리를 공유했다.

| 장면·정책 | 화면 인원 중앙값 / 서버 확정 타격 | 공격 중앙 FPS | 클라이언트 프레임 p95 | 서버 월드 틱 p95 | 서버 수신 / 송신 (KiB/s) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 필드 16 · Generic | 16 / 16 | 23.92 | 49.33ms | 19.39ms | 78.45 / 1,504.08 |
| 던전 16 · Generic | 16 / 16 | 24.76 | 47.73ms | 18.43ms | 80.57 / 1,556.80 |
| 필드 16 · Iris | 16 / 16 | 25.36 | 48.41ms | 18.12ms | 87.68 / 1,523.13 |
| 필드 16 · Generic, 원격 의상 예산 12,000 | 16 / 16 | 25.76 | 46.74ms | 19.36ms | 80.08 / 1,534.12 |
| 필드 20 · Generic | 20 / 20 | 24.34 | 51.54ms | 21.39ms | 76.03 / 1,935.30 |

Iris는 같은 필드에서 16개 연결, 공격 16회, 피해 160, 최종 체력 확인과 정상 종료를 모두 통과했다. 이번 표본에서 서버 월드 틱 p95는 1.28ms 짧았고, 송신량은 1.3%, 수신량은 11.8% 늘었다. 비교에는 각 조건을 한 번 사용했다. Iris 드라이버가 보고한 현재 정원은 127이므로 결과는 그 한도 안에서 해석한다. 현재 프로젝트의 Iris 기본값과 대규모 로컬 실험의 Generic 선택을 유지한다.

의상 비교는 `-ClothVertexCap 0`과 `-ClothVertexCap 12000`에서 기존 실행 예산을 재사용한다. 격리된 렌더 클라이언트의 원격 의상만 두 조건에서 같은 권한으로 허용한다. 수치는 물리 LOD 버텍스 예산이다. 공격 중 의상 상태 중앙값은 미중단 16→5, 중단 0→11, 클라이언트 Game Thread 중앙값은 6.89→3.80ms였다. 이 값은 전체 게임 스레드이며 Chaos 작업 자체의 CPU 시간은 별도 계측 대상이다. 캡처의 발광 이펙트와 어두운 시험 조명으로 의상 복귀와 실루엣을 판정하기 어려워 운영 기본값은 0으로 유지한다.

전투 원자료는 `Saved/Validation/Continuation_20261014/Connected{FieldGenericFixed16,DungeonGeneric16,FieldIris16,FieldCloth12000_16,FieldGeneric20}_20261124`에 있다. `verification.json`에 판정과 백분위, `combat-ticks.csv`에 서버 원자료, `Rendered/frames.csv`와 `Rendered/Strike.png`에 클라이언트 원자료가 있다. 20인에서 최소 물리/커밋 여유는 5,562/5,984MiB였다. 24인 시험은 실행 전 가용량 17,590MiB에서 시작 기준 18,000MiB에 미달해 보류했다. 서버 네트워크와 플레이어 그래픽의 병목은 각 프로세스에서 판정한다.

### MainWorld 수면·실내 프리셋 비교

`L_MainWorld`의 H06 북쪽 습지 수면과 H08 광산 내부 고정 시점을 확인했다. 독립 D3D12, 1920×1080, 렌더 비율 100%, VSync·FPS 제한·동적 해상도 해제 상태에서 HUD로 낮음→매우 높음→매우 높음→낮음을 적용했다. 각 패스는 스트리밍 완료 후 180프레임이다. 범위는 같은 품질의 두 패스 최솟값과 최댓값이다.

| 장면 | 품질 | 프레임 중앙값 | 프레임 p95 | GPU 중앙값 |
| --- | --- | ---: | ---: | ---: |
| H06 습지 수면 | 낮음 | 3.44–3.47ms | 3.79–3.98ms | 2.20–2.24ms |
| H06 습지 수면 | 매우 높음 | 9.32–9.43ms | 9.76–10.08ms | 7.67–7.72ms |
| H08 광산 내부 | 낮음 | 2.60–2.63ms | 3.21–3.46ms | 1.42ms |
| H08 광산 내부 | 매우 높음 | 5.75–5.80ms | 6.43–6.48ms | 4.18–4.22ms |

두 장면에서 그림자, VSM 페이지, Lumen 간접광과 반사 전환 및 복원을 확인했다. 습지 캡처에는 기존 검토용 수면 메시가 보이며, 비용에는 주변 지형·초목·그림자가 같이 들어간다. 실내 캡처의 매우 높음에서는 광산 통로가 거의 어둡고 낮음에서는 통로가 밝게 드러난다. 현재 광산 초안의 프리셋별 가독성 차이이며, 실내 광원을 확인하고 프리셋 간 시야가 유지되는지 비교할 근거다. 군중, 수영, 실내 동시 전투는 이 환경 측정 범위 밖이다.

원자료는 `Saved/Validation/MainWorldSceneABVerified_20261124/{H06Wetland,H08MineInterior}`에 있다. 재현 시 기존 HUD 검증 플래그에 `-ProjectJGraphicsWorldView=X,Y,Z,Pitch,Yaw`를 추가한다. H06은 `88500,-212400,2425,-19,0`, H08은 `-173200,191800,63889,-4,140`이다.

최종 Editor 증분 빌드를 통과했다. 복제 관련성·정책 정규화·PlayerState 대상 범위·의상 배정·모듈 의상 틱 회귀 5건과 실제 D3D12 의상 인터랙터 수명 회귀 1건을 통과했다. 회귀 리포트는 같은 검증 폴더의 `Regression/index.json`, `ClothRHIRegression/index.json`에 있다.

## 스펠블레이드 200체 표현 기준 · 2026-11-24

현재 스펠블레이드의 망토·머리카락은 기존 기본 Physics Asset 경로에서 충돌을 줄인 구성을 사용한다. 로컬 720p 200체 기본 경로의 동시 공격 프레임 p95는 적용 전 94.00ms, 적용 후 86.09ms였다. 머리카락 시뮬레이션 호출당 평균은 56.95→39.64µs였다. 실행 조건과 표본 수, CPU 작업량, 메모리, 기본 경로 적용 결과는 [보조 움직임 성능 검증](Reports/ProjectJ_Spellblade_Secondary_Motion_2026-11-24.md)에 모았다. 신규 파츠는 [보조 움직임 제작 기준](../Animation/Authoring/Secondary_Motion_Authoring.md)을 따른다.

### 그래픽 품질 프리셋 실측 · 2026-11-24

Ryzen 7 9800X3D와 RX 9070 XT에서 로컬 플레이어 1명을 포함한 스펠블레이드 100체로 같은 장면을 실행했다. D3D12 출력은 1280×720, 렌더 비율은 100%로 고정했다. VSync와 FPS 제한을 끄고 설정 UI와 같은 11개 품질 그룹만 바꿨다. 각 조합을 한 번 실행했고 모든 집계 프레임에서 100체의 렌더 표본이 확인됐다.

| 품질 | 이동 중앙 FPS | 동시 공격 중앙 FPS | 공격 프레임 p95 | 공격 표본 |
| --- | ---: | ---: | ---: | ---: |
| 낮음 | 35.39 | 26.47 | 42.14ms | 30 |
| 중간 | 36.40 | 26.46 | 42.79ms | 29 |
| 높음 | 36.99 | 26.13 | 45.45ms | 29 |
| 매우 높음 | 37.44 | 25.67 | 43.08ms | 28 |
| 시네마틱 | 31.54 | 24.05 | 44.55ms | 27 |

중앙 FPS는 프레임 시간 중앙값의 역수다. 이동은 첫 1초를 제외했다. 공격은 전체 캐릭터의 지상 기본 공격 몽타주가 활성인 짧은 구간을 사용했다. 양쪽에서 스크린샷 요청과 직후 두 프레임을 제외했다. 공격 구간 게임 스레드 중앙값은 약 29~32ms로 GPU 중앙값 2~8ms보다 길었다. 이 단순 장면에서는 낮음~매우 높음의 공격 FPS 차이가 작았다. 시네마틱의 최소 커밋 여유는 4.28GiB로, 4GiB 보호선에 가까웠다.

검증 폴더는 `Saved/Validation/WholeProjectAudit_20261013/GraphicsPreset{Low,Medium,High,Epic,Cinematic}100_20261124`다. 각 `manifest.json`에 실행 명령과 적용한 품질 값, `frames.csv`에 원자료가 있다. 네트워크나 복잡한 월드 환경에서의 비용은 별도로 측정한다.

### MainWorld HUD 실시간 전환 · 2026-11-24

기존 `L_MainWorld` 마을 시작 지점에서 독립 실행 D3D12를 사용했다. 고정 카메라, 로컬 스펠블레이드 1명, 1920×1080, 내부 렌더 비율 100%, VSync·동적 해상도·FPS 제한 해제로 같은 시점을 유지했다. 콘솔로 품질을 설정하지 않고 HUD의 프리셋 선택·적용 경로로 낮음→매우 높음→매우 높음→낮음을 실행했다. 각 구간에서 최소 4초와 World Partition 로딩 완료를 기다린 뒤 180프레임을 측정했고, 각 캡처는 측정 이후에 찍었다.

| 순서 | 품질 | 프레임 중앙값 | 프레임 p95 | GPU 중앙값 |
| ---: | --- | ---: | ---: | ---: |
| 1 | 낮음 | 7.64ms | 15.89ms | 2.84ms |
| 2 | 매우 높음 | 10.77ms | 11.32ms | 9.07ms |
| 3 | 매우 높음 | 10.75ms | 11.24ms | 9.09ms |
| 4 | 낮음 | 4.36ms | 4.73ms | 3.05ms |

각 적용 후 그림자 `r.ShadowQuality`는 0↔5, VSM 페이지 예산은 256↔4096, Lumen GI와 반사 허용 값은 각각 0↔1이었다. 낮음 복귀까지 확인했고 화면에서도 캐릭터·나무 그림자와 초목 차이가 확인됐다. 첫 낮음의 전체 프레임 시간 편차와 같은 품질로 복귀한 구간의 차이를 보존했다. 월드·렌더 설정을 처음 통과하는 비용을 안정 구간 비교로 합치지 않는다. 이번 시점에 물이나 실내 장면, 네트워크 군중은 포함되지 않았다. 최소 물리 RAM 여유는 9.94GiB, 최소 커밋 여유는 8.51GiB였다.

원자료는 `Saved/Validation/GraphicsWorldAB_20261124`의 `World.log`, `frames.csv`, `Pass1.png`~`Pass4.png`다. `-ProjectJGraphicsSettingsSmoke -ProjectJGraphicsWorldProbe=<새 출력 폴더> -UserDir=<격리 폴더>`로 같은 HUD 경로를 실행한다.

### 군중 클라이언트와 전투 서버 CPU 추적 · 2026-11-24

별도 D3D12 평지 100체에서 생산 스펠블레이드·망토·머리카락으로 동시 기본 공격을 재현했다. 근접 스윕이 지나간 8개 틱에서 게임 스레드의 Cloth 완료 대기 합계는 틱당 중앙값 8.50ms, 애니메이션 갱신은 3.68ms, 스윕은 0.10ms, 추적용 관찰기는 0.45ms였다. 애니메이션 평가 완료 스코프는 중앙값 1.31ms, 최대 14.13ms였다. 스코프에는 중첩이 있으므로 값을 합산해 전체 프레임으로 해석하지 않는다. 망토 실행비용을 줄이는 [기존 선택형 실행 예산](Architecture/PerformanceOptimizationFoundation.md)의 보호·복귀 계약을 재사용할 근거이며, 기본 활성화나 옷별 시각 승인까지 완료한 상태는 아니다.

Generic 루프백 36인은 36/36 타격·정상 종료를 확인했고 공격 구간 서버 월드 틱 p95는 32.80ms였다. 타격 이후 60.32ms 틱 안에서 `ServerReplicateActors` 25.31ms, 틱 그룹 차단 31.52ms가 있었다. 같은 틱의 근접 스윕은 0건이다. 타격 이후의 복제와 틱 그룹을 분석 우선순위로 잡을 수 있는 관측이며, 틱 그룹 내부 대기 원인이나 40인 재현까지 확정하지 않는다. 사전 물리 RAM 여유 17.70GiB가 트레이스와 함께 실행할 40인 기준 18GiB에 못 미쳐 36인을 사용했다.

타이밍 요약과 분석 절차는 `Saved/Validation/GraphicsWorldAB_20261124/CpuTimingSummary.json`과 `SummarizeCpuWindows.py`에 있다. 원본 트레이스는 `WholeProjectAudit_20261013/GraphicsWorldCrowdCPU100_20261124/Render.utrace`와 `Continuation_20261014/GraphicsWorldServerCPU36_20261124/Server.utrace`에 있고 중앙 보관 색인에 연결했다. Insights 메모리 태그 오류가 있어 이 분석에서 메모리는 테스트 프로세스가 기록한 OS 가용량을 사용했다.

## 던전·필드보스 전투 접속 · 2026-11-24

동일 PC의 전용 Editor 서버와 NullRHI 클라이언트 호스트에서 Generic 복제를 사용했다. 참가자 간 복제를 유지하고, 동시 입력에서 스펠블레이드의 실제 공격과 서버 확정 GAS 피해를 관측했다. 던전은 `Test_MMO_Dungeon`에 배치된 최종 보스 클래스와 위치를 재사용한 전투 시험이다. 이동·클리어·보상 경로는 별도 시험 범위다.

| 장면 | 접속 / 확정 타격 | 공격 단계 서버 월드 틱 p95 | 실제 타격이 처리된 틱 최대 | 서버 공격 요청부터 피해 확인까지 p95 |
| --- | ---: | ---: | ---: | ---: |
| 던전 32인 | 32 / 32 | 34.92ms | 22.77ms | 970.26ms |
| 필드보스 32인 | 32 / 32 | 35.86ms | 19.18ms | 933.66ms |
| 필드보스 32인, ABP 정리 후 서버 추적 | 32 / 32 | 36.65ms | 21.75ms | 932.66ms |
| 필드보스 36인, 최초 / 추적 재시험 | 36 / 0씩 | 38.84 / 39.60ms | 해당 없음 | 해당 없음 |

32인 실행은 명중 위반 0, 연결 종료까지 통과했다. 공격 단계 p95는 30Hz의 월드 틱 처리 예산 33.33ms를 조금 넘었다. 지연에는 로컬 왕복과 공격 재생이 포함된다. 각 참가자가 한 번 공격하는 시험이며, 화면 프레임은 위의 720p 시험을 참조한다.

초기 36인 두 실행에서는 36명이 준비했으나 5초 공격 구간에서 확정 피해가 0건이었다. 재시험의 서버 트레이스에서는 플레이어당 근접 히트 구간 시작 1회와 궤적 스윕 41회가 실행됐다. 32인 비교에서는 Gameplay Effect 적용이 32회 기록됐고, 36인에서는 적용 타이머가 관측되지 않았다. 당시 36인의 최소 물리 RAM 여유는 약 6.4GiB였으며 초기 40인 실행은 시작 전 가용량 때문에 보류했다. 원인과 수정 후 40인 실접속 결과는 [스윕·이벤트 분리와 테스트 장비 수정](#스윕이벤트-분리와-테스트-장비-수정)에 있다.

원자료는 `Saved/Validation/Continuation_20261014` 아래 `CurrentDungeonCombat32_20261124`, `CurrentFieldCombat32_20261124`, `CurrentFieldCombat32PostCleanup_20261124`, `CurrentFieldCombat36_20261124`, `CurrentFieldCombat36TraceRetry_20261124`에 있다. 메모리 판정, ABP 정리 전후와 서버 타이머 CSV는 `Saved/Validation/CombatScaleAndGraphAudit_20261124`에 있다.

### 고정 배치 후속 진단 · 2026-11-24

`Run-LoopbackCohort.ps1`의 `-StableBossLayout`은 두 실행에서 첫 32명의 시작 위치·방향을 동일하게 유지한다. 36인 실행은 기존 위치 사이에 4명만 더한다. `-BossHitDiagnostics`는 타격 후보의 권한 검사와 시야선 차단을 기록한다. 참가자 간 Generic 복제와 원래 판정 규칙을 그대로 사용했다.

| 참가자 | 준비 / 확정 타격 | 공격 구간 월드 틱 p95 | 물리 메모리 최소 여유 |
| ---: | ---: | ---: | ---: |
| 32 | 32 / 32 | 37.34ms | 6.92GiB |
| 36 | 36 / 0 | 38.03ms | 6.15GiB |

36인에서는 아군 대상 제외 200건이 기록됐고, 해당 로그에 보스 대상 요청이나 시야선 차단은 없었다. 32인에서는 확정 피해 320과 이미 처리된 보스 대상의 중복 요청이 기록됐다. 배치가 고정되어도 실패가 재현됐으며 메모리 보호선 4GiB는 유지됐다. 위 p95는 진단 로그를 활성화하고 서버·클라이언트 호스트를 같은 PC에서 실행한 수치이며, 양쪽 모두 30Hz 예산을 넘었다. 스윕 결과와 이벤트 전달을 분리한 결과는 다음 절에 있다.

원자료는 `Saved/Validation/Continuation_20261014`의 `FieldCombatStable32_20261124`, `FieldCombatStable36_20261124`에 있다. [플레이어 그래픽 설정](../UI/README.md#그래픽-설정)은 같은 날 D3D12 환경에서 별도로 검증했다. 그래픽 프리셋별 군중 프레임과 서버 틱은 서로 다른 측정이다.

#### 스윕·이벤트 분리와 테스트 장비 수정

두 비교에서 대상 캡슐은 (3850, 0, 222)에 정지했고 Pawn 충돌 쿼리가 활성 상태였다. 기존 36인 실행의 1,476개 스윕 중 캡슐을 포함한 결과와 보스 대상 이벤트는 0건이었다. 각 공격자의 가장 가까운 구간도 보스 캡슐에서 4.83~5.89cm 떨어져 있었다. 32인에서는 1,312개 스윕 중 480개에 보스가 포함됐고 32명이 서버 확정 피해를 냈다.

러너가 기본 장비를 `DA_Spellblade_CanonicalEquip`으로 교체하고 있었다. 이 구형 테스트 공격은 34개 키를 메시 공간의 동일한 점에 저장해 캐릭터의 이동으로만 타격 범위에 진입했다. 러너에서 장비 교체를 제거해 필드와 던전 모두 캐릭터가 정상 소유 시 받은 기본 장비와 실제 근접 공격을 사용한다. 공유 에디터 Data Validation은 궤적 키가 한 점에 모이면 제작 의도를 확인하도록 경고한다. 기존 선택형 참조 데이터는 별도 근접 경로 시험에서 그대로 사용한다.

실제 장비로 바꾼 첫 공격에서는 36명 모두 보스 대상 이벤트를 만들었지만, 클라이언트 시점이 동일 방향을 가리켜 19명이 공격 각도에서 거절됐다. 공통 러너는 소유 클라이언트의 정상 카메라 회전 입력으로 대상을 조준시킨다. 서버가 Mover 입력으로 모든 참가자의 조준을 확인한 뒤에만 공격 단계로 진행한다. 실제 공격은 기존 입력 라우터와 명령 해석을 사용하며 피해 권위, 반경과 공격 각도 계약을 보존한다.

수정 전 원자료는 `Saved/Validation/Continuation_20261014/FieldCombatMeleeWitness{32,36}_20261124`와 `FieldCombatProductionInput36_20261124`에 있다. 저장 키와 실제 장착 그래프 확인 결과는 `Saved/Validation/GraphicsAndCombatSettings_20261124/MeleeProductionGeometry.json`에 있다.

| 장면 | 조준 승인 / 확정 타격 | 공격 구간 월드 틱 p95 | 최소 물리 RAM 여유 |
| --- | ---: | ---: | ---: |
| 던전 32인 | 32 / 32 | 31.62ms | 7.17GiB |
| 필드보스 36인 | 36 / 36 | 32.31ms | 6.27GiB |
| 필드보스 38인 | 38 / 38 | 36.61ms | 6.16GiB |
| 필드보스 40인 | 40 / 40 | 36.44ms | 5.14GiB |

모든 실행에서 참가자 전원이 서버 확정 피해를 내고 정상 종료했다. 던전 32인은 30Hz 종합 기준을 통과했다. 필드보스 36인은 스윕 진단을 활성화했고 실제 피해 처리 틱 최대가 35.81ms였다. 필드보스 세 실행의 30Hz 종합 기준은 미달이었다. 42인은 바로 전 실행의 메모리 사용량과 보호 여유를 적용한 사전 시작 기준 19GiB에 대해 물리 RAM 여유가 18.20GiB여서 시작을 보류했다. 이번 결과는 동일 PC의 전용 Editor 서버와 클라이언트 호스트로 단일 공격을 실행한 범위다.

수정 후 원자료는 `Saved/Validation/Continuation_20261014`의 `DungeonCombatProductionAim32_20261124`, `FieldCombatProductionAim36_20261124`, `FieldCombatProductionScale38_20261124`, `FieldCombatProductionScale40_20261124`에 있다.

Editor 직접 증분 빌드와 근접 타격·수명·SSR 집중 회귀 7개가 통과했다. 최종 빌드와 회귀 근거는 `Saved/Validation/GraphicsAndCombatSettings_20261124/ProductionMeleeAimEditorBuild.log` 및 `MeleeAutomation/index.json`에 있다.

이전 근접 경로 시험의 참조 에셋 두 개는 현재 원본과 소스 지문이 달라 다시 제작했다. 기존 키와 새 후보의 최대 좌표 차이는 0cm, 타격 시간 차이는 0초였다. 새 Editor에서 두 에셋 모두 유효했고, 구형 스펠블레이드 참조의 정지 지점 경고 1건이 확인됐다. `CanonicalStampRefresh.json`에 에셋별 백업 위치와 전후 SHA-256이 있고, `CanonicalPathDataRecheck.json`에 재검증 결과가 있다. 이전 버전이 필요하면 Editor를 닫고 현재 패키지를 보존한 뒤 중앙 백업의 SHA-256을 대조해 대상 파일만 복원한다.

## 2026-11-18 기반 작업 기준선

Development Editor에서 클라이언트 표시 밀도와 서버 실접속 수를 따로 측정한다. 각 축에서 같은 실행 바이너리를 사용하고 기존 러너를 재사용한다. Cook과 배포 실험은 별도다.

렌더 비교는 Ryzen 7 9800X3D와 RX 9070 XT에서 1280×720 출력이 확인된 D3D12 offscreen, 단순 Entry 장면, Spellblade Avatar 100/200개와 로컬 소유자 1명씩이다. 네트워크 연결은 0개다. `Summarize-MoverCrowd.py`는 시작 1초와 캡처 요청/후속 2프레임을 제외한다. 공격은 전체 Avatar에서 몽타주가 활성인 표본을 사용한다.

| 표시 Avatar | 이동 프레임 P95 / P99 | 동시 공격 프레임 P95 / P99 | 프로세스 Private peak |
| ---: | ---: | ---: | ---: |
| 100 | 41.40 / 47.44ms | 55.26 / 90.86ms (66표본) | 18.36GiB |
| 200 | 71.56 / 73.72ms | 102.93 / 119.76ms (34표본) | 21.46GiB |

공격 GT P95는 39.12→85.30ms, GPU P95는 10.16→16.81ms였다. 스레드별 시간은 별개 분포다. 먼저 200개 공격 구간의 GT 실행과 wait를 분석할 근거가 생겼다. 메모리는 준비를 포함한 전체 Editor 게임 프로세스의 500ms 샘플 peak이며, 개체당 비용이나 VRAM의 증가분으로 해석할 수 없다. 렌더 요약은 `Saved/StorageCleanup_20261104/ArchiveStore/02_Measurements/NPCFoundation_20261118_Render_Density.json`에 있다.

```powershell
foreach ($count in 100,200) {
    ./Scripts/Validation/Measure-RenderedCrowd.ps1 -RunName "Density_$count" -Count $count -Movement -LocalPlayer -DisableEditorPython -ForceResolution
}
```

서버 비교는 같은 복제 프로브 100개를 사용하는 전용 Editor 서버의 NullRHI·로컬 소켓 테스트다. 30Hz 제한에서 각 조건은 5개 AOI/FastArray 단계와 정상 종료를 통과했다. 송신량에는 테스트 제어 트래픽이 포함된다.

| 실제 접속 | 서버 송신량 | 프레임 시작 간격 P95 / P99 | 서버 Private peak |
| ---: | ---: | ---: | ---: |
| 2 | 9.52KiB/s | 33.64 / 33.95ms | 1.70GiB |
| 4 | 19.42KiB/s | 33.88 / 34.10ms | 1.71GiB |

첫 캐릭터 애니메이션 실행은 2접속을 통과했다. 4접속은 네 소켓이 모두 입장했으나 초기 준비 단계에서 시간 초과돼 측정 구간에 도달하지 못했다. 위 표는 같은 복제 프로브로 다시 실행한 결과다. 실제 캐릭터의 수정 후 2·4접속은 아래에 기록한다. 요약·실행명·trace/CSV SHA는 `Saved/StorageCleanup_20261104/ArchiveStore/02_Measurements/NPCFoundation_20261118_Network_Baseline.json`에 있다.

```powershell
foreach ($count in 2,4) {
    ./Scripts/Validation/Measure-Network.ps1 -RunName "Socket_$count" -ClientCount $count -NPCCount 100 -NoTrace -ServerBaseline
}
```

`-ServerBaseline`은 클라이언트 추적을 끈 상태에서 서버 frame/region trace와 프로세스 메모리만 수집한다. `Server/network-phases.csv`에서 송신량·단계 시간을 읽고, `Server.utrace`에서 Insights의 `Game Frames` 트랙을 `ProjectJ.Network.Stage*` 구간별로 추출한다. 단계 첫·마지막 프레임을 제외한 연속 `StartTime`의 차이를 틱 간격으로 집계한다.

### 실제 캐릭터 2·4접속 후속

기존 4접속은 `Stage 0` 시간 초과였다. 후속 실행에서 구 경로가 사용하던 임시 Pawn 좌표가 네 슬롯 모두 `(54.656, 79.013, 208.000)`임을 확인했다. 충돌이 없는 임시 Pawn의 좌표에 충돌이 있는 캐릭터 캡슐들을 그대로 생성하던 시험 코드가 초기 착지 조건을 깨뜨릴 수 있었다. `Entry`의 시험 바닥에서 실제 캡슐 치수에 따라 슬롯별 위치를 나누고 기존 네이티브 배치 검사로 충돌과 바닥 지지를 확인한다. 이후 AOI 이동도 슬롯별 Y 간격을 유지한다. 기존 착지 확인과 AOI 그룹의 X 거리·단계 조건을 사용한다. 실패 자료는 보존했다.

수정 후 기존 Spellblade BP를 사용하는 로컬 실소켓 2·4접속이 각각 AOI·FastArray 5단계와 정상 접속 종료를 통과했다. 4접속의 슬롯별 요청 위치와 실제 생성 위치가 정확히 일치했다. 두 실행은 같은 소스·바이너리 지문, 전용 Editor 서버, NullRHI, 복제 프로브 100개, 30Hz 제한, 추가 지연·손실 0 조건이다. 송신량은 단계 1–5의 서버 합계로 테스트 제어 트래픽을 포함한다.

| 실제 캐릭터 접속 | 서버 송신량 | 프레임 시작 간격 P95 / P99 | 서버 Private peak |
| ---: | ---: | ---: | ---: |
| 2 | 37.54KiB/s | 34.63 / 34.89ms (444표본) | 2.83GiB |
| 4 | 133.06KiB/s | 34.63 / 35.00ms (440표본) | 2.85GiB |

장시간 실행, 실제 필드 NPC AI, RHI 클라이언트, 손실 조건 및 출시 서버 패키지는 별도 검증이다. 원본 실패와 최종 빌드·실행 지문, 메모리, trace/CSV SHA는 `Saved/StorageCleanup_20261104/ArchiveStore/02_Measurements/NPCSocketSlots_20261118_Authored_Baseline.json`에 있다.

```powershell
foreach ($count in 2,4) {
    ./Scripts/Validation/Measure-Network.ps1 -RunName "AuthoredSocket_$count" -ClientCount $count -NPCCount 100 -Animation -NoTrace -ServerBaseline
}
```

### 저장된 실행 재집계와 전후 비교

렌더 스크린샷과 프레임 CSV, 서버 연결 검증·단계별 송신량·메모리·서버 프레임을 `Compare-FoundationBaselines.py`로 한 번에 집계한다. 렌더 100·200은 같은 RHI 바이너리와 1280×720 설정을 사용했다. 두 실행의 소스 지문 차이는 당시 함께 작업 중이던 `Measure-Network.ps1`이었다. 실제 2·4접속은 같은 전용 서버 바이너리와 설정에서 측정했다. 비교기는 같은 축 안에서 설정과 실행 바이너리 지문을 확인한다. 이전 기준선과 비교할 때는 축별 부하와 조건을 맞추고, 변경된 실행 바이너리의 지문과 원자료 SHA를 결과에 함께 남긴다.

새 서버 실행은 `Server.utrace`에서 Stage1–5의 `Game Frames`를 CSV로 내보낸다. 아래는 해당 서버 실행 폴더 하나를 처리하는 예다. 보존된 실행에는 이 파일이 이미 있다.

```powershell
$run = (Resolve-Path 'Saved/Validation/GroupD_20260910/AuthoredSocket_2').Path
$insights = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealInsights.exe'
$frames = (New-Item -ItemType Directory -Path "$run/ServerFrames").FullName.Replace('\','/')
$response = "$frames/Export.rsp"
$export = 'TimingInsights.ExportTimingEvents "{0}/Server-{{region}}.csv" -columns=ThreadId,ThreadName,TimerName,StartTime,EndTime,Duration -threads="Game Frames" -region=ProjectJ.Network.Stage*' -f $frames
$export | Set-Content -LiteralPath $response -Encoding utf8NoBOM
Start-Process $insights -ArgumentList "`"$run/Server.utrace`" -AutoQuit -NoUI `"-ExecOnAnalysisCompleteCmd=@=$response`"" -WindowStyle Hidden -Wait
```

재집계는 에디터 없이 실행한다. 비교 기준은 [저장소에 포함된 기준선 JSON](Reports/Data/ProjectJ_Foundation_Baseline_2026-11-18.json)이다. 새 측정을 비교할 때는 `$render`, `$server`를 새 실행 폴더들로 바꾸고 다른 출력 파일명을 사용한다. 새 서버 측정에는 `Server.utrace`와 단계별 CSV가 필요하다. 과거 `Server.utrace`가 퇴역한 뒤에는 이 기준선과 보존된 CSV·명세의 SHA-256이 모두 일치할 때 기존 실행을 재집계할 수 있다. 이때 결과의 `trace_evidence`는 `recorded_sha256`이다.

```powershell
$render = 100,200 | ForEach-Object { "Saved/Validation/WholeProjectAudit_20261013/NPCFoundation_20261118_Render$_" }
$server = 2,4 | ForEach-Object { "Saved/Validation/GroupD_20260910/NPCSocketSlots_20261118_Auth$_" }
$measurements = 'Saved/StorageCleanup_20261104/ArchiveStore/02_Measurements'
$reference = 'Docs/Performance/Reports/Data/ProjectJ_Foundation_Baseline_2026-11-18.json'
python ./Scripts/Validation/Compare-FoundationBaselines.py --render @render --server @server `
    --compare $reference `
    --output "$measurements/NPCFoundationTable_Repeat.json"
```

서버 송신량은 Stage1–5의 전송 바이트를 전체 초로 나눈 프로세스 전체 송신율이다. 프레임 시작 간격과 메모리는 각 실행의 조건에서 해석한다. 렌더 밀도와 소켓 수는 별도의 축이다.

### 트레이스에서 확인한 검증 결과

[성능 트레이스 검증 결과](Reports/ProjectJ_Trace_Findings_2026-11-18.md)에 실행 조건·측정치·실패 원인과 원본 없이 다시 읽을 수 있는 CSV·로그를 정리했다.

**2026-10-28 서버 실행·표현 자원:** [전용 서버 병렬 평가와 headless 의상 생성](Architecture/PerformanceOptimizationFoundation.md#2026-10-28-서버-병렬-평가와-headless-의상-생성). 같은 바이너리에서 지원 startup 정책과 실제 task 실행을 확인했고, 서버 Frame82→39ms와 RHI 품질을 보존한 headless cloth 생성 방지를 검증했다. client FPS·운영 CCU 보장과 구분한다.

**직접 ABP 후속 분석:** [200명 Cloth 병목과 품질 예산](Reports/ProjectJ_Cloth_Budget_200_2026-10-25.md). GT 대기 구간·Cloth OFF/내부 직렬화 진단을 분리하고, 로컬 품질을 유지하는 변경의 실제 검증 상태를 기록한다.

**2026-10-25 전환 직후 비용:** [200명 직접 ABP와 이전 리타깃 비교](Reports/ProjectJ_DirectRig_200_2026-10-25.md). 당시 새 몸체·머리는 LOD0만 있었고 리타깃 제거 뒤에도 Cloth 대기가 남았다. 현재 피부 body LOD와 원격 Cloth 예산의 비교는 위 후속 보고서를 따른다. 아래 10월 22일 이전의 리타깃·LOD0–2 결과는 당시 에셋의 기록이며 worker CPU 합계와 전체 프레임을 구분한다.

[README의 대표 결과](../../README.md#성능)는 프로젝트 소개를 위한 요약이다. Mover 5개 구간은 [해당 보고서](Reports/ProjectJ_Mover_200_2026-10-22.md), 기존 LOD의 GPU 수치는 [200개 캐릭터 비교 자료](Reports/ProjectJ_200_Player_Optimization_2026-10-15.json), Mass·Niagara·네트워크·PSO는 [시스템별 벤치마크](../Benchmarks/SystemsModernization.md)에서 조건·원자료와 함께 확인한다. 아래는 적용 결정과 상세 근거를 찾는 목차다.

**발전 순서:** 기존 예산·LOD 최적화 → Mover·Manny/native 전환 → 이번 정책 소비 복구·추가 최적화다. 이번 비교도 기존 최적화가 적용된 기반에서 수행했다. 전환 자체의 동일 조건 성능 증감은 미측정이며, 과거 GPU 감소와 이번 frame 감소를 연결한 누적 수치를 만들지 않는다. [단계별 구조와 비교 범위](Reports/ProjectJ_Mover_200_2026-10-22.md#이전-최적화--mover-전환--이번-최적화)를 먼저 읽는다.

**동시 공격 결정:** 이번 저빈도 시각 갱신·IK 생략 최적화를 적용하지 않고 기존 매 프레임 평가와 제작 LOD·물리 품질을 유지한다. 활성 montage는 blend-out까지 보호한다. 공격·복귀 측정은 회귀 관찰용이며 성능 개선의 채택 근거가 아니다.

원자료의 작은 요약·CSV는 문서에 유지하고 상세 로그·캡처·대표 trace는 [Git LFS 압축 보관소](../../Archives/Validation/README.md)에서 공유한다. 바이트 동일 사본을 통합하고 원본 해시·복원 방법·퇴역 범위를 함께 기록한다.

**2026-10-22 Mover 재검증 기록:** source 품질 정책 호출 누락·회전 좌표계 불일치를 수정하고 200명 실제 이동/반전·점프/착지·공격/복귀를 재측정했다. 동일 바이너리 ABBA의 유휴 평균 **18.19→16.12ms**, 이동 **21.05→19.42ms**이며, 동시 공격 **30.10→30.08ms**와 tail 편차도 함께 공개한다. [현재 → 결과·실행 검증·원자료](Reports/ProjectJ_Mover_200_2026-10-22.md)를 시작점으로 읽고 아래 LOD 및 과거 CMC 결과와 합산하지 않는다. 내부 궤적 예측은 기본 60Hz를 유지한다.

**2026-10-16 기존 몸체의 결정:** 검토한 GreatSword_Woman의 LOD0–2를 채택하고 LOD3–4 추가는 보류한다. 로컬LOD0·200명 전원 표시·원본 물리와 서버 판정을 유지한다. 공통 제작 설정은 다른 에셋에도 재사용할 수 있지만 생성·예외 검수·품질 승인은 에셋별로 필요하다. [현재 결정표와 단계별 근거](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md)를 기준으로 아래 과거 측정을 읽는다.

최신 [7차 기록](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-7차--새-캐릭터의상장비의-공통-lod-제작)은 새 에셋의 기존 LOD를 보존하고, 누락된 일반 표면 메시를 공통 native 설정으로 생성·검증하는 Editor 제작 경로다. [사용법과 Cloth·헤어·morph 예외](../Animation/Authoring/Mesh_LOD_Authoring.md)를 따른다. 최종 RHI13개·200명 표시/로컬LOD0·실제2연결 네트워크 회귀를 통과했으며 새 성능 개선율을 측정한 작업은 아니다.

[6차 기록](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-6차--다른-에셋-호환성과-실제-몸체-lod)은 에셋 교체·Cloth/외부 제어·몸체 역할 충돌의 공통 경계를 보완하고 실제 원격 body LOD를 적용했다. 동일 바이너리 교대 시험의 GPU는 유휴10.526→6.188ms, 동시200 montage 행11.922→7.421ms였지만 전체 frame/CPU 이득은 확정하지 않는다. 로컬LOD0·전원 표시·원본 물리를 보존하며 다른 에셋의 LOD/Cloth 제작·품질 검수까지 자동 완료로 주장하지 않는다.

[5차 기록](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-5차--물리-복제의-초기화-동등성과-비용-판정)은 물리 복제 비교의 비대칭 초기화 순서를 고쳤다. 단순 복제·축소본의900표본 동등성은 통과했지만, 실제91→44 actor의64행 비계측 변화는 대부분1% 안팎이었다. trace의 대조 scope 변동도 남아 제작 적용은 하지 않았다. Editor 동등성 회귀만 추가하고 기존 runtime/에셋을 보존했으며 JSON `fifth_pass`에 근거를 통합했다.

2차는 같은 [200명 계획](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-2차--공통-비용과-실제-소비-확인)에 통합했다. 유휴·이동·회전/착지까지 공통 조회를 정리하고 64행 비계측 A→B→B→A를 비교했으나 전체 CPU 개선은 미확정이다. 실제 표시 몸체 LOD1개, Far 정책과 retarget IK 노드 소비 불일치, 물리3개의 서로 다른 담당 뼈를 확인했다. 최종 Editor/Game·RHI34개·200명 표시/local1011 samples가 통과했으며, 상세 근거는 요약 JSON의 `second_pass`를 따른다.

현재 목표는 **총 200명 전원 표시·로컬 품질 유지**다. [2026-10-15 계획과 첫 적용](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md)은 기존 예산 subsystem의 거리 관측 활성화, 로컬 중복 계산 제거, 200명 실제 렌더·로컬 보호·원격/전투 검증을 기록한다. 단일 렌더 유휴 표본은 15.978→12.712ms였지만 전원 공격은 30.499→30.607ms였고 50명 trace의 CPU 증가도 남아 있다. 전체 개선·회귀 해소로 판정하지 않는다. [실행 조건과 source/DLL/asset·원자료 지문](Reports/ProjectJ_200_Player_Optimization_2026-10-15.json)을 함께 읽고, 아래 과거 실행과 합산하지 않는다.

동일 계획의 3차는 원격 retarget IK 생략 후보를 검증한 뒤 **미채택**했다. 같은 바이너리 off/on/on/off 64행에서 Far 비용 절감이 입증되지 않아 후보 코드·옵션은 제거하고 2차 runtime을 보존했다. 기능/같은 입력 포즈 통과와 전체 성능 이득을 구분하며, JSON의 `third_pass`는 현재 적용 결과와 후보 측정의 해시를 별도로 보존한다.

4차는 기존 trace의 의상별 비용과 새 Editor 진단의 retarget op·실제 physics actor를 확인했다. physics 데이터 축소는 단순 복제도 포즈 동등성을 통과하지 못해 **적용 보류**다. 읽기 진단만 기존 시험에 남겼고 runtime·에셋·서버/네트워크 경계를 유지했다. JSON의 `fourth_pass`와 계획의 4차 절에 새 실행·재사용 근거를 구분했다.

2026-10-14 추가 구조 변경의 기준은 [재점검](../Review/Audits/Project_J_Reaudit_2026-10-14.md)이다. RA01–13 전후의 전체 8개 Editor 모듈+modules를 맞춘 A1/B1/B2/A2 교대 CPU 비교 64행을 완료했다. 조건별 240틱 준비/600틱 측정·2회 반복이며 평균 증가 +2.40–8.59%가 남아 있다. 별도 단계 trace와 원인 조사를 계속하며 성능 완료로 판정하지 않는다. 실제 D3D12의 동시 200 montage 구간은 38.14FPS, GPU10.31ms였고 지속 공격이나 A/B 개선율은 아니다. 아래 기존 측정을 새 RA 변경본의 수치로 사용하지 않는다. UI·원격·CPU wrapper의 종료 근거도 보완했다.

현재 후속 구조·수명·실제 데이터 조사는 [심화 감사](../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](../Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md)에 기록한다. DS01–18의 실제 변경·계약 검토·미검증을 구분한다. 최종 전체 회귀 332개·Game/Cook과 기록한 통합 검증을 수행했으며, CPU 일부 증가와 packaged RHI trace 종료 실패는 남아 있다.

[2026-10-13 전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)는 같은 조건의 200명 교대 실행과 단계별 trace, 실제 RHI의 200개 authored 몸체 검증을 추가한다. NullRHI world tick, 렌더 프레임/GPU, 실제 원격 접속 수는 서로 다른 측정이다. 이전 리팩토링/리타깃 CPU 증가를 이번 입력 결함 수정의 통과로 해소 판정하지 않는다.

2026-10-13 후속 [200명 애니메이션 런타임 최적화](../Animation/Planning/Animation_Runtime_Optimization_2026-10-13.md)는 MM 데이터베이스 적용의 전체 그래프 탐색을 기존 캐시로 대체하고, 공격 시작·관측 시간·tick 원자료를 보완했다. 해당 timer의 국소 CPU 감소와 전체 World tick 비교의 편차·최대값 한계를 구분한다.

2026-10-13 파이프라인 구조 변경의 200명 CPU 전후 비교와 실행 경계는 [애니메이션 리팩토링](../Animation/Architecture/Animation_Pipeline_Refactor_2026-10-13.md)에 기록한다. 아래 10월 12일 시각 예산 off/on 결과와 다른 비교다.

다른 채팅에서 후속 작업을 시작할 때는 [200명 애니메이션 최적화 인계](../Animation/Planning/Animation_Optimization_Handoff_2026-10-13.md)를 읽는다. 현재 main 기준·품질 보호·네트워크/병렬 소유권·CPU 과제·원자료·실행 명령을 모았다.

## 최근 애니메이션 구현과 비교 — 2026-10-12

[200명 시각 애니메이션 예산](../Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md)에 실제 BP·장비·리타깃 몸체·의상 물리 구성의 기능 off/on 비교를 기록했다. 같은 바이너리, NullRHI, 조건별 120틱 준비 + 120틱 측정, 2회 반복이며 ABA on 평균 World tick은 근거리 13.61→11.40ms, 원거리 13.78→10.69ms다. 동시 공격은 19.76→19.58ms로 거의 같다.

기존 AnimInstanceProxy·엔진 병렬 평가를 유지하면서 평가 작업량과 주기를 줄인다. NPC/Mass 작업 큐에 애니메이션을 새로 제출하지 않는다. 로컬·전투·단발·급회전·화면 복귀 보호, 실제 보간 검증과 복구 CVar를 함께 기록했다. 200명 렌더 FPS·GPU 개선이나 200개의 소켓 접속 검증을 뜻하지 않으며, 원자료는 문서가 가리키는 로컬 `Saved/Validation`에 있다.

## 측정 순서

1. [최적화 기반](Architecture/PerformanceOptimizationFoundation.md)에서 기존 예산·실행 정책과 후속 범위를 읽는다.
2. [프로파일링 수집](CaptureGuides/ProjectJ_Baseline_Capture_Guide_2026-09-03.md)으로 조건과 자료를 남긴다.
3. [통합 기준선](Reports/ProjectJ_Profiling_Consolidated_Summary_2026-09-06.md)과 [CPU 기준선](Reports/ProjectJ_Profiling_Baseline_Results_2026-09-03.md)을 비교한다.
4. [벤치마크와 원본](../Benchmarks/README.md)에서 workload·표본·재계산 방법을 확인한다.

## 계획과 변경 기록

- [오픈 월드 레벨 설계와 확장 계획](../Gameplay/Planning/Open_World_Level_Design.md): 고속 비행·팝인·원경·식생 검증 계획과 16절의 에디터 숲 고정 시점 CSV 비교. 패키지·전체 월드·원격 MMO 성능 합격이나 기존 200명 최적화 완료를 뜻하지 않는다.
- [확장성 검증 계획](Planning/ProjectJ_Scalability_Profiling_Validation_Plan_2026-09-03.md): 인원 단계와 CPU/GPU/메모리/네트워크 회귀 게이트.
- [보류된 최적화](Planning/DeferredOptimizationArchitecture.md): 도입 조건과 후속 계약.
- [시스템 현대화 작업 기록](Reports/ProjectJ_Systems_Modernization_Refactor_2026-09-08.md): 초기 진단부터 적용·측정·병합까지의 원본 이력.
- [애니메이션 실행 감사](../Animation/Planning/ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md) · [네트워크 수집](../Networking/README.md).

로컬 복제 캐릭터 수, 실제 원격 플레이어 수, 실험 fixture 수를 구분한다. 서로 다른 조건의 개선율을 합쳐 전체 게임 성능으로 해석하지 않는다.

## 재감사 현재 측정과 한계 — 2026-10-14

최신 production RA25 package3은 실제200 표시 body·동시 montage167프레임에서27.1149ms(36.88FPS),GPU10.6211ms였고 복귀219프레임은15.8822ms(62.96FPS)였다. Render2보다 낮은 값이다. 단순 light/camera·D3D12 offscreen·실제 socket0이며 교대 비교/열·스케줄 통제가 없어 개선·코드 회귀를 확정하지 않는다. NullRHI full-binary A/B/B/A64행의 일부 CPU 증가와 별도16 traced region의 반대 방향은 미해결이다. [현재 검증과 원자료 범위](../Review/Audits/Project_J_Reaudit_Verification_2026-10-14.md)를 읽는다. 원격은 실제2socket+NPC200 반복이며200socket으로 계산하지 않는다.
