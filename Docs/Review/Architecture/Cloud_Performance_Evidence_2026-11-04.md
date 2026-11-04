# W8 기존 성능 근거와 다음 검증

현재 cloud 작업에서는 새 성능·부하 시험을 실행하지 않았다. 아래는 기존 자료의 조건을 다시 확인한 결과이며 이번 코드 변경의 개선률이 아니다.

## 기존 근거

- [Cloth 200개 비교](../../Performance/Reports/ProjectJ_Cloth_Budget_200_2026-10-25.md): D3D12 1280×720 Epic, 로컬 1명 포함 캐릭터 200개, 실제 원격 연결 0개. 동일 바이너리 교대 비교의 이동 Frame 평균 1,827.665→63.092ms, 공격 2,123.513→79.754ms. GPU는 이동 6.398→9.970ms, 공격 9.609→10.160ms였다. Cloth 경로 개선 근거이지 GPU 개선·60FPS·200CCU 근거가 아니다. 후속 스모크의 편차를 같은 평균에 합치지 않는다.
- [아키텍처 감사의167차](../Audits/Project_J_MMO_Architecture_Readiness_2026-11-04.md): 전용 서버와 NullRHI 클라이언트 2개, 75ms/loss0에서 이동 표적 SSR 1회와 HP1000→990을 확인했다. 기본 Pawn 전체·재정렬·렌더링·대규모 연결 성능은 별도다.166차 실패 원자료를 지우지 않는다.
- [기존 작업 기록](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md): 초기 소켓 실험의 static AOI probe100개는 실제 AI100개가 아니다. 별도 NavigationLoad100은 실제 NPC100이나 고정0.05초 시뮬레이션·접속0·미렌더링이다. 합성 Tick P95 2.611ms를 실제 서버 FPS로 환산하지 않는다.
- 포털 도착 준비에는 기존 `Project_JWorldStreamingComponent::ArrivalTimeoutSeconds=30`이 있다. 영구 데드락이라는 전제로 timeout/셀 수를 임의 변경하지 않는다.

## 기존 측정 경로를 재사용할 것

`Scripts/Validation/Measure-Network.ps1`의 ServerPerformanceThreads, IrisCSV, RenderedClientCSV, RenderedClothExecutionObservation과 기존 latency/loss 옵션을 사용한다. 새 병렬 측정 프레임워크나 새 부하 시험을 이 작업에서 도입하지 않는다.

1. 서버 tick wall time, 게임 스레드, worker wait를 분리한다. 대기 구간과 OS CPU 사용률을 혼동하지 않는다.
2. 실제 socket 연결 수, actor 수, negotiated bytes/s, 관측 전송량·queue·saturation을 별도 기록한다. 송신 설정을 실제 전달량으로 보지 않는다.
3. Client1 RHI CSV와 서버 Iris CSV를 분리하고 최종 저장·해시·바이너리·맵·장비·측정 구간을 남긴다.
4. 장식 업데이트와 필수 전투 pose/notify/경고를 분리한다. 성능 수치를 위해 판정이나 경고를 생략하지 않는다.
5. Windows 빌드와 기능 회귀 이후 동일 조건으로 비교한다. Strict 실패·startup diagnostics·timeout을 숨기지 않는다.

현재 engine/runtime와 원본 Saved 측정 자료가 없는 cloud에서는 문서 조건만 확인했다. 과거 결과를 이번 변경 후 검증으로 재표기하지 않는다.
