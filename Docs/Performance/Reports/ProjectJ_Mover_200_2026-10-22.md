# Mover 구조 결함 수정과 200명 재검증 — 2026-10-22

기준 HEAD는 `83edf6057ed1f614bce47156ae1b56b8eeb8ae95`다. 감사 기준 `25a4fb98` 이후 변경은 애니메이션 그래프 주석 정리다. 시작 시 작업 트리는 깨끗했다. 아래 내용은 이 기준점 이후 변경의 검증 기록이며 과거 CMC·MM 측정과 합산하지 않는다.

## 이전 최적화 → Mover 전환 → 이번 최적화

**기존 최적화가 적용된 기반 위에서, Mover 전환 후 끊긴 소비 경로를 복구하고 추가 결함을 수정했다.** 이번 대조군은 최적화를 모두 제거한 초기 프로젝트가 아니다. 따라서 이번 감소율은 기존 최적화 위의 추가 효과이며, 과거 효과를 다시 더하거나 누적 FPS 개선율로 표시하지 않는다.

| 단계 | 이어받거나 변경한 구조 | 수치와 비교 가능 범위 |
| --- | --- | --- |
| ① 이전 최적화 | ABA·visual 예산, 리타깃 Family/Body, 몸체 LOD0–2, 이펙트 풀·관심 영역·로딩/PSO 기반을 구축했다. | 10/15 몸체 LOD off→on의 GPU 유휴 **10.526→6.188ms**, 공격 **11.922→7.421ms**. 해당 조건의 렌더 비용 감소이며 전체 frame/CPU 효과는 미확정이다. |
| ② Mover·Manny/native 전환 | `7ecadbde`와 후속 안정화에서 입력·이동 시뮬레이션·native 상태/Chooser·포즈 연결을 재구성했다. 기존 예산/LOD 기반을 이어받았으나 source 정책 호출 누락과 회전 공간 불일치로 일부 절감 경로가 작동하지 않았다. | ①→②를 같은 workload/바이너리 조건으로 잰 비교는 없다. 전환 자체의 성능 증감 수치는 **미측정**이다. |
| ③ 이번 수정·재최적화 | 실제 정책 소비를 복구하고 동일 공간으로 회전을 판정한다. NPC Velocity와 플레이어 DirectionalIntent solver 계약을 구분하고 수명·복제·착지 등 결함을 수정했다. 기존 LOD·로컬·공격 품질은 유지한다. | 이번 같은 바이너리 정책 A→B: 평균 frame 유휴 **18.19→16.12ms(11.4% 감소)**, 이동 **21.05→19.42ms(7.7% 감소)**. 정확성 수정은 A/B 양쪽에 포함된다. |

읽는 순서는 **기존 예산·LOD 기반 → Mover 구조 전환(증감 미측정) → 정책 복구의 추가 이득**이다. 하나의 동일 지표로 측정한 `a→b→c` 수치열은 없으므로 만들지 않는다. 과거 GPU와 이번 frame은 다른 지표이며, 41.2%와 11.4%를 합산하거나 곱하지 않는다. 이전 결과의 근거는 [200명 계획·단계별 기록](../Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md), 현행 적용 범위는 아래 표를 따른다.

## 동시 공격은 기존 품질 유지

**동시 공격에는 이번 저빈도 시각 갱신·IK 생략 최적화를 적용하지 않는다.** 공격/활성 montage 동안 기존 매 프레임 평가와 hand/foot/retarget/follower 정책 허용을 보호하며, montage의 독립 갱신 요구는 blend-out까지 유지한다. 원래 제작된 메시 LOD·IK 노드의 LOD 조건과 물리는 유지하고, 원격 캐릭터를 로컬 LOD0으로 강제 변경하지 않는다. 내부 궤적 예측 기본값도 60Hz다.

이는 기존 코드의 보호 계약을 유지한다는 결정이다. 공격 수치는 품질 보호와 회귀 관찰용으로 남기며, **공격 성능 개선을 채택 근거로 사용하지 않는다.** 새로운 동시 공격 최적화 기능은 탑재하지 않는다.

## 변경한 책임과 계약

- 기본 속성의 초기화 여부는 Avatar가 아니라 지속되는 AttributeSet이 소유한다. 재Possess는 마나·체력·공격력·방어력 0을 미초기화로 해석하지 않는다. 직업 설치·레벨 변경의 명시적 초기화 경로는 유지한다.
- 플레이어는 DirectionalIntent만 승인한다. Velocity·알 수 없는 enum은 무입력 DirectionalIntent로 거부해 공중 관성 solver를 우회하지 못하게 한다. NPC/NavMover의 Velocity 계약은 유지한다.
- NPC의 승인된 Velocity 명령은 엔진 SimpleWalking의 직접 속도 solver로 전달한다. 플레이어의 DirectionalIntent만 기존 SmoothWalking 가속 스프링을 사용한다. NavMover가 이미 소유한 도착/정지 명령을 다시 플레이어 스프링에 넣어 경로 중단 뒤 재이동하던 원인을 제거하며, root motion·경사 변환과 기존 충돌 시뮬레이션은 공용 경로에 남긴다.
- Walking 속도 제한은 desired velocity를 만들기 전, 시뮬레이션 시작의 승인된 gait에서 다시 정한다. 이전 Walk 제한을 Run에 끌고 오지 않는다.
- 비행 speed/braking/friction은 복제된 비행 상태와 활공 상태에서 함께 계산한다. 서버·rep-notify·클라이언트 재시작이 같은 적용 경로를 사용하며, 제작 기본 braking은 최초 복제 전 보관한다.
- AnimInstance는 새 소스의 첫 정상 프레임에서 이전/최근 상태·속도·미분·aim 이력을 함께 시작한다. admission만 닫는 것으로 이전 수명의 착지를 지우지 않는다.
- 로컬 착지 강도와 event gate는 Mover가 확정한 충격 속도를 사용한다. 기존 원격 착지 복제 이벤트와 native master의 포즈 경로는 각각 유지한다.
- 장기 저장 명령을 기다리는 동안에도 travel 표본을 수집한다. 같은 Pawn의 유한한 거리 누적만 유지하고, committed snapshot을 설치한 뒤 게시한다. Pawn 교체·종료에서는 누적을 폐기한다.
- 탑승 admission은 capsule과 실제 movement backend를 요구한다. CMC custom submode를 보관하고 복원하며, detach도 capsule 소실에 안전하게 대응한다.
- 공용 애니메이션 품질 정책을 실제 primary AnimInstance의 GT 갱신에 연결한다. linked layer가 follower tick의 두 번째 writer가 되지 않으며, 기존 coordinator/외부 tick 소유권을 존중한다.
- 미래 회전 긴급도는 현재·미래 궤적의 같은 visual-root 공간에서 비교한다. Manny의 메시 축 오프셋을 capsule yaw와 비교해 직진/유휴까지 급회전으로 처리하던 문제를 수정했다. Strafe 방향 반전은 속도와 입력 가속도의 불일치로 별도 보호한다.
- 로컬 플레이어 판별을 공용 함수로 모은다. AI의 PlayerState 유무에 의존하지 않고 PlayerController 소유권을 사용하며, 컨트롤러가 탈것으로 이동한 로컬 탑승자도 source·ABA·visual·예측 품질 보호를 유지한다.
- 원거리 예측 밀도 후보는 안정된 접지 유휴/직진에만 30Hz를 허용한다. 가속·회전·점프·전투·montage와 로컬은 60Hz다. 실제 Mover 시뮬레이션, 입력/복제 주기와 궤적 출력 표본 간격은 바꾸지 않는다. 최종 채택 여부는 아래 실측으로 구분한다.

## J 항목과 조건부 검토

| 항목 | 처리와 검증 범위 |
| --- | --- |
| J-01 | AttributeSet 수명으로 초기화 소유권 이전. 마나 0/1/45와 체력·공격·방어 0, 명시적 reset 회귀 추가. |
| J-02 | 플레이어의 Velocity 0/역방향/과대값·invalid enum 거부. 정상 DirectionalIntent와 내비게이션은 별도 유지. |
| J-03 | 양쪽 rep-notify와 초기 수신/재시작의 solver 설정 일원화. 활공/비활공·착륙과 수신 순서 회귀. |
| J-04 | admission 이후 첫 프레임까지 재초기화 회귀 확대. fake landing·미분 spike 제거와 같은 수명의 진짜 착지를 구분. |
| J-05 | 확정 LandingVelocity를 강도·초기 착지 gate에 전달. 서버 sampling skip에 의존하지 않는다. |
| J-06 | 군중 입력을 실제 held input producer에 연결. 개체별 변위·방향 반전·착지를 검증하는 실제 Mover/RHI 측정 추가. |
| J-07 | Smoke의 서버·클라이언트·실패 진단을 get_player_pawn으로 변경하고 생산 타입을 검사. Verify 스크립트에는 해당 Character 조회가 없었다. MainWorld 실행 결과는 아래에 구분한다. |
| J-08 | 초기 warmup만 제외. 정상 진입 후 reset/invalid를 실패시키고 정지한 수신도 기본값에서 거부. 전체 행·기간과 제외 행을 보존. |
| J-09 | 현행 MassValues roster만 요구. 이전 migration은 Historical 이름으로 분리하고 현행 runner는 실제 PlayerLocomotion 회귀를 실행. 현재 Master는 native 입력·Chooser·포즈 검사를 재사용한다. |
| J-10 | continuation도 Source/Scripts/Config/Plugins 전체를 수집. plugin descriptor/source/binary와 파일 집합 변경·삭제, Cook의 실제 Editor binary까지 비교. |

| 조건부 항목 | 결정 |
| --- | --- |
| Walking gait 순서/rollback | 승인된 속도를 먼저 적용하도록 수정. 이전 override가 달라도 replay의 제한은 동일하다. |
| 추가 NPC 추적 진동 | 확장 회귀의 100명 중 89–92명만 InRange인 상태가 반복됐다. 개체별 위치/속도/상태에서 Pawn 충돌은 100명 모두 Ignore였고, 목표 부근에서 재가속/반전하는 것을 확인했다. Velocity를 플레이어 스프링에 재투입하던 경로를 엔진 직접 속도 계약으로 수정했다. |
| capsule 없는 Pawn | 실제 지원 API 입구에서 InvalidRider로 거부하며 Attach/Detach 경계도 보호. |
| legacy CMC custom submode | 공용 movement observation에 custom mode를 포함해 기존 restore 계약 유지. |
| checks-off 전직 복원 | ValidateRestoreSnapshot의 output 계산을 check 바깥에서 실행. 기존 LocalProfile의 지원 범위는 확대하지 않음. |
| Sprint jump entry | 실제 Air Chooser에서 Strafe Sprint의 비전방 Loop entry 10조건이 비어 있었다. 방향/발/공중 상태를 보존하고 gait만 Run으로 정규화하는 경로를 entry/reselection이 공유하도록 수정했다. |
| common combat aim | linked weapon layer의 GraphAim 입력을 유지하고, 공통 snapshot도 완료된 Mover ControlRotation을 사용하도록 수정. |
| pending travel | snapshot을 덮어쓸 수 없도록 거리만 누적하고 설치 후 게시. 생명주기와 유한 한도를 유지. |
| 비행 재탑승·복제 순서 | 기존 재탑승 회귀와 새 solver rep-notify 순서 회귀를 구분한다. 실제 네트워크 범위는 아래 실행 결과를 따른다. |

## 기존 최적화의 현재 적용 범위

| 영역 | 현재 계약과 판단 |
| --- | --- |
| Mass | Mover NPC demotion은 CanDemote/Demote 양쪽에서 거부한다. 순수 Mass 값 계산은 실행 가능하지만 현재 Mover 캐릭터가 Mass로 전환된다는 성능 주장은 하지 않는다. |
| 애니메이션 예산 | ABA/visual coordinator는 활성이다. 그러나 source 품질 정책은 실제 갱신 호출이 누락됐고 visual cadence는 잘못된 급회전 판정으로 막혔다. 두 소비 경로를 복구하고 로컬·전투·montage·반전·복귀 보호를 유지한다. |
| retarget/body LOD | Manny source와 Family/Body retarget, 화면 크기 LOD0–2를 유지한다. 로컬 LOD0·IK·매 프레임 평가를 검사한다. 10/15 GPU 감소율은 별도 역사적 결과다. |
| cloth/secondary physics | 원본 물리·visual 평가/보간 정책을 보존한다. 의상 solver 비용을 이번 frame 개선율과 동일시하지 않는다. |
| Niagara | 기존 풀·culling과 authored trail 설정을 유지한다. 과거 100개 생성 실험은 이번 200명 Mover 측정의 개선율이 아니다. |
| 네트워크 | 서버 권위·Iris 관심 영역·owner prediction을 유지한다. 화면의 200명과 실제 socket 연결 수를 따로 기록한다. |
| 비동기 내비게이션/판단 | 기존 중앙 스케줄러·값 계산·완료 시 수명/버전 검사 계약을 유지한다. Mover NavMovement 통합 회귀를 사용한다. |
| 로딩/PSO | 기존 VisualAssetSubsystem/장비 lease와 PSO 사전 요청을 유지한다. 과거 packaged miss 감소를 이번 새 Cook 성공으로 재표기하지 않는다. |

## 이번 실행과 현재 → 결과

실행별 상세 로그·캡처·대표 trace는 [Git LFS 압축 보관소](../../../Archives/Validation/README.md)로 옮겼다. 보고서에서 참조하는 작은 CSV·요약·원본 해시는 아래 Data에 그대로 유지한다. 과거 `Saved/Validation` 경로의 큰 파일은 보관 명세를 통해 빈 별도 폴더에 복원한다. 압축 보존과 불필요한 전체 trace 퇴역 범위는 보관소에 구분했다.

성능 원자료는 [Summary.json](Data/Mover200_20261022/Summary.json), [파일 지문](Data/Mover200_20261022/SHA256.json), [소스·플러그인·바이너리 지문](Data/Mover200_20261022/SourceProvenance.json)에 보존한다. 아래 수치는 실제 실행이 끝난 결과다.

| 구조/결함 | 현재 → 결과 |
| --- | --- |
| 지속 속성과 Avatar 교체 | 0을 미초기화로 재해석 → AttributeSet 초기화 수명으로 구분, 소진 상태 보존 |
| 플레이어 명령 승인 | Velocity 명령으로 공중 solver 계약 우회 가능 → DirectionalIntent만 승인, NPC Velocity는 유지 |
| Walk → Run/replay | 이전 override가 첫 desired velocity에 영향 → 승인된 gait 제한부터 설정 |
| NPC 도착·정지 | Nav 속도 명령에 플레이어 스프링의 잔여 속도 적용 → 명령 의미에 맞는 기존 직접 속도 solver 사용 |
| 비행 solver | 활공/비행 속성과 로컬 solver 설정 적용 경로 분산 → 동일 파생 설정 함수와 양쪽 rep-notify |
| 애니메이션 재시작 | 이전 낙하/속도 이력 잔존 → 첫 유효 source에서 같은 수명의 이력 시작 |
| source 품질/visual 예산 | 정책 호출 누락·축 오프셋을 급회전으로 판정 → 실제 소비 연결·동일 공간 회전 비교 |
| 로컬 탑승자 | Controller 이전 후 일반 원격으로 분류 가능 → rider/mount 소유권으로 로컬 보호 지속 |
| 예측 계산량 | 안정된 원거리도 내부 예측 60Hz → 제한된 안정 상태에만 30Hz 후보, 그 외 60Hz |
| travel 저장 대기 | 표본을 건너뛰어 거리 소실/긴 구간 teleport 오분류 → 같은 Pawn의 bounded 거리만 보관, commit 설치 후 게시 |
| 검사/실행 도구 | reset 행 제거·정지 수신 false-pass·퇴역 roster/캐릭터 조회 → 부정 fixture 실패·현행 등록/생산 Pawn 계약 검사 |

최신 direct Editor UBT 전체 빌드와 새 프로세스의 `MoverAudit07` native 회귀 **100개**가 통과했다. 테스트 오류·경고·누락은 0개다. Python 도구 회귀는 **48개** 통과했다. Native에는 지속 속성 재Possess, 실제 명령 유형/Walking replay, 제작 BP의 원거리/가속/회전/로컬/탑승 품질 admission, 비행 rep-notify 순서, 확정 착지, 실제 Chooser entry/reselection과 pending travel을 포함한다.

초기 검증에서 추상 비행 클래스를 직접 생성한 테스트 fixture 오류와 명시적 타입 include 누락은 검사 코드에서 수정했다. Chooser의 비전방 Sprint Loop 10조건 실패는 실제 entry 계약 수정으로 해결했다. 원거리 품질과 observer 회귀 실패는 assertion을 완화하지 않고 정책의 실제 소비 경로·PlayerController 판별·회전 좌표계를 수정한 뒤 새 프로세스에서 재검증했다. 테스트 경고 0개는 엔진 시작 시 MCP 플러그인이 출력하는 안내 경고까지 없다는 의미는 아니다.

재현 도구는 `Scripts/Validation/Measure-RenderedCrowd.ps1 -Movement -LocalPlayer -Count 200`과 `Summarize-MoverCrowd.py`다. warmup 이후 유휴·실제 이동/방향 반전·점프/착지·동시 공격·공격 복귀를 각각 관측한다. 각 phase의 첫 1초와 screenshot 요청/다음 2프레임을 명시적으로 제외하고 p50/p95/p99·표본 수·run 편차를 기록한다. frame과 GPU counter는 별도이며 worker scope 합을 frame으로 쓰지 않는다.

성능 대조군 A는 같은 바이너리에서 `ProjectJ.AnimationQuality.SourcePolicy.Enabled=0`과 `ProjectJ.PlayerTrajectory.RemotePredictionHz=60`을 사용한다. 이전의 constant Local source 정책과 actor-yaw 회전 판정을 재현하는 대조군이며, 버그가 남은 옛 HEAD 전체를 실행한 결과는 아니다. **채택한 B는 source 정책/좌표계 수정을 켜고 내부 예측 60Hz를 유지한다.** 두 조건 모두 정확성 수정·동일 제작 에셋·동일 200명 workload를 사용한다.

조건은 UE **5.8.3**, Windows 11, Ryzen 7 9800X3D, Radeon RX 9070 XT, D3D12/SM6, scalability 3, **1280×720 offscreen**, VSync/FPS cap 해제, 소켓 연결 0개다. Entry 맵에 동일 바닥·조명·고정 카메라와 생산 대검 BP/PlayerState/장비·의상 200개를 구성한다. warmup 8초 후 phase별 8초이며, 공격 통계는 유효 측정 중 **200명이 동시에 montage를 재생하는 행만** 포함한다. 이 비교의 지표는 실제 RHI 프레임이며 서버 처리량이 아니다.

최종 순서는 `Mover200_ReleaseA1 → Mover200_ReleaseB1 → Mover200_ReleaseB2 → Mover200_ReleaseA2`다. 소스·설정·플러그인·에셋·바이너리 지문은 네 실행에서 동일하다. 각 셀의 평균은 두 run 평균의 산술 평균이며, 분위수는 두 run의 범위를 그대로 표시한다.

| 200명 workload | 평균 frame 현재 → 결과 (ms) | 변화 | 현재 / 결과 측정 프레임 수 |
| --- | --- | --- | --- |
| 유휴 | **18.19 → 16.12** | 11.4% 감소 | 358+360 / 406+406 |
| 실제 이동·반전 | **21.05 → 19.42** | 7.7% 감소 | 325+328 / 355+353 |
| 점프·착지 phase | **21.06 → 19.50** | 7.4% 감소 | 325+327 / 355+350 |
| 200명 동시 공격 | **30.10 → 30.08** | 0.1% 감소 · 차이 미확정 | 113+111 / 112+111 |
| 공격 종료·복귀 | **18.31 → 18.33** | 0.1% 증가 · 차이 미확정 | 374+377 / 376+375 |

| workload | p50 현재 → 결과 범위 (ms) | p95 현재 → 결과 범위 (ms) | p99 현재 → 결과 범위 (ms) |
| --- | --- | --- | --- |
| 유휴 | 17.92–18.03 → 15.98–15.98 | 19.56–19.67 → 17.07–17.47 | 20.60–24.90 → 17.96–18.13 |
| 실제 이동·반전 | 20.89–21.03 → 19.26–19.33 | 21.90–22.25 → 20.37–20.58 | 22.58–23.30 → 22.20–22.97 |
| 점프·착지 phase | 20.93–20.98 → 19.18–19.31 | 21.90–22.32 → 20.60–21.88 | 22.85–23.15 → 22.22–23.64 |
| 200명 동시 공격 | 29.37–29.48 → 29.45–29.57 | 32.50–33.69 → 32.69–32.81 | 32.98–36.02 → 33.39–34.15 |
| 공격 종료·복귀 | 18.09–18.25 → 18.11–18.24 | 19.41–19.47 → 19.53–19.80 | 20.15–20.44 → 20.48–21.24 |

유휴·이동 평균 개선은 최신 두 반복에서 확인했다. **공격·복귀 개선과 장시간 tail 안정성은 입증하지 않았다.** 공격 run 평균은 A 29.85–30.36ms, B 30.07–30.10ms다. run당 공격 표본은 111–113개이므로 p99를 지속 전투 성능으로 일반화하지 않는다. 모든 phase를 합친 단일 개선율은 사용하지 않는다.

| workload | GT 현재 → 결과 | render 현재 → 결과 | RHI thread 현재 → 결과 | GPU 현재 → 결과 |
| --- | --- | --- | --- | --- |
| 유휴 | 18.18 → 16.11 | 5.96 → 5.49 | 5.99 → 5.62 | 6.81 → 6.80 |
| 실제 이동·반전 | 21.04 → 19.42 | 5.96 → 5.62 | 5.74 → 5.54 | 6.58 → 6.61 |
| 점프·착지 phase | 21.05 → 19.49 | 5.99 → 5.63 | 5.73 → 5.57 | 6.59 → 6.61 |
| 200명 동시 공격 | 30.11 → 30.09 | 6.53 → 6.35 | 5.95 → 5.88 | 7.46 → 7.48 |
| 공격 종료·복귀 | 18.31 → 18.33 | 5.95 → 5.78 | 5.89 → 5.77 | 6.83 → 6.80 |

위 값은 각각 평균 ms다. GT가 프레임 시간을 지배하며 GPU 감소는 확인되지 않았다. GT 시간에는 worker 대기도 포함될 수 있어 이것만으로 개별 solver의 exclusive CPU 비용을 단정하지 않는다. 상호 겹치는 thread/GPU 구간을 더하지 않는다.

최종 네 실행의 모든 유효 측정 행에서 200명 전원 렌더를 관측했다. 캐릭터별 누적 수평 이동은 **5,432–5,440cm**, 반전 **17회**, 착지 **3회**였으며, 200명 동시 공격과 복귀 후 montage 0개를 검사했다. 로컬 LOD0·source Local·hand/foot IK·visual interval 0·내부 예측 60Hz를 매 측정 프레임 확인했다. 50/100명 단계도 같은 workload 검증을 통과했다.

최신 fixture는 모든 활성 montage의 source hand/foot/retarget/follower 허용 상태도 검사한다. 이는 실제 각 LOD에서 모든 IK 노드를 강제로 켰다는 뜻이 아니라, 기존 제작 LOD 계약을 유지하며 montage의 정책 보호가 사라지지 않았음을 확인하는 검사다.

메모리의 현재 → 결과 범위는 process peak working set **4.666–4.696 → 4.665–4.666GiB**, private bytes **14.572–14.594 → 14.514–14.567GiB**다. 500ms 간격의 전체 Editor game 프로세스 표본으로 시작·에셋 로드를 포함한다. 인원당 추가 메모리·메모리 최적화 이득이나 VRAM 수치가 아니다.

[초기 별도 후보 시리즈](Data/Mover200_20261022/PredictionCandidate.json)의 30Hz 유휴 평균은 **16.05/16.59ms**, 그 시리즈의 같은 바이너리 60Hz B는 **16.13/16.03ms**였다. 이번 배치에서 안정·거리 조건을 만족한 대상은 유휴 평균 5명뿐이므로, 30Hz의 독립 이득을 입증하지 못해 **기본값 60Hz를 유지**한다. 후보는 명시적 CVar opt-in으로만 남기고 평균 개선율에 기여했다고 주장하지 않는다. 새 lease/타이머나 별도 시뮬레이션 권위는 추가하지 않았다.

앞선 [다른 ABBA 시리즈](Data/Mover200_20261022/VariationSeries.json)는 유휴 **17.91→16.74ms**, 이동 **21.30→20.58ms**, 공격 **29.79→33.40ms**, 복귀 **18.25→19.39ms**였다. B 한 실행의 공격 평균 **36.88ms**와 GPU/GT 증가를 포함해 그대로 보존한다. 이후 변경은 네트워크 캡처의 준비 완료 경계와 RHI montage 품질 검사이며 생산 solver·품질 정책을 추가 튜닝하지 않았다. 최신 시리즈에서 공격이 비슷했다고 앞선 편차가 해소됐다고 판정하지 않는다. **공격·복귀의 이득과 일관된 tail 개선은 채택 근거에서 제외한다.** 두 시리즈를 합쳐 같은 바이너리의 단일 개선율로 만들지 않는다.

빌드와 다른 엔진 프로세스는 직렬로 실행했다. OS·데스크톱 전체를 독점한 시험은 아니며 다른 배경 부하의 기여는 분리 측정하지 않았다. 큰 편차의 원인을 특정 worker/solver나 외부 프로세스로 단정하지 않는다.

![최종 정책의 200명 실제 이동 장면](Data/Mover200_20261022/Result-Movement.png)

고정 원경 화면은 전원 표시를 확인하는 자료다. 모든 클립·체형·접촉의 근접 외형 승인이나 실제 플레이 카메라 전체 검수로 해석하지 않는다. 몽타주의 수명은 기존 BudgetedMesh의 독립 요구가 blend-out까지 소유하고 source 정책이 이를 소비하므로, 같은 상태를 관리하는 두 번째 보호 장치를 추가하지 않았다.

## 실행 검증과 실패 후 재검증

개별 원자료와 테스트 목록은 [검증 요약](Data/Mover200_20261022/Validation/Validation.json)에 보존한다. 아래 실행은 서로 겹치는 검사를 포함하므로 개수를 합산하지 않는다.

| 실행 | 실제 결과와 범위 |
| --- | --- |
| Editor direct UBT | `EditorBuild14` 전체 target 성공. 최종 fixture·현재 Experiments DLL 포함. |
| Game direct UBT | `GameBuild` Win64 Development 성공. 실제 exe/pdb/target과 plugin artifact 12개를 수집했고, frozen 소스 1,115개 및 Editor/plugin/선택 에셋 지문이 일치했다. Cook 명령을 실행한 기록은 아니다. |
| 집중 native | `MoverAudit07` **100개 성공**, 테스트 오류·경고 0. |
| Mass/내비게이션 확장 | `MoverAuditMass03` **13개 성공**, 오류·경고 0. MassValues, 실제 100명 추적·동적 장애물·재경로·취소/종료, NPC root motion 포함. |
| 표현 수명 집중 | `MoverAuditVisual02` **2개 성공**, 오류·경고 0. controlled visual fixture의 720개 경고는 transform 게시 순서를 고쳐 제거했고, montage blend-out lease도 유지했다. |
| RHI Experiments 확장 기록 | `MoverAuditExperiments01` **48개 성공**, 테스트 경고 0. 이후 생산 NPC solver 변경은 집중 native/내비게이션으로 재검증했다. |
| Maturity 확장 기록 | `MoverAuditMaturity01` **139개 완료**, 135개 경고 없음·4개 경고 있음·실패 0. 실제 저장 8,192회와 bounded stream 16,384회 포함. 전체 suite를 최신 빌드에서 반복한 기록은 아니다. |
| Python 도구 | **48개 성공**. 정지 수신·중간/후반 reset·plugin 소스/descriptor/binary 변경과 캡처 이후 경고의 부정 사례 포함. |
| MainWorld | 저장된 `L_MainWorld` 재로드와 PIE smoke 성공. landscape 1,024개·충돌 표본 20개, 원본 대비 최대 오차 0.473cm 미만. 생산 Pawn 생성, 맑음 60,000lux → 비 15,000lux → 복귀 확인. 수리·저장은 실행하지 않았다. |

첫 확장 내비게이션은 100명 목표 도착이 89–92명 사이에서 반복되어 timeout됐다. 승인된 Velocity의 스프링 경로를 수정한 뒤 100명 추적·혼합 장애물·종료까지 완료했고, 이후 초기화 fixture도 명시적 Recast 계약으로 맞춰 경고 0개를 확인했다. 도착 인원·취소/종료 assertion이나 제한 시간은 완화하지 않았다.

최종 RHI의 소스 집합과 Game 빌드 직전 동결한 Source/Scripts/Config/Plugins/uproject 집합 1,115개가 일치했다. `Capture-ContinuationGame.py`와 `Check-ContinuationProvenance.py ... Cook` 대조도 통과했다. 후자는 배포 전 identity 검사이며 Cook 실행이나 모든 에셋 byte의 일치를 대신하지 않는다. 선택 에셋 15개와 Content 작업 트리 변경 1개는 별도 해시로 보존한다.

Maturity 경고 4개 중 visual fixture 경고는 위 집중 재검증에서 해결했다. 다른 3개는 socket `None`을 넣는 무기/그립 fixture와 과대 HTTP 응답을 거부하는 localhost 부정 사례다. 이 기록을 테스트/엔진 로그 전체가 깨끗한 최신 suite로 재표기하지 않는다. 현재 Master 주석 노드의 누락 GUID는 엔진이 생성한 값을 저장했으며, 새 프로세스 제작 감사에서 Master 394개·무기 layer 8개 노드를 읽어 dirty 전후 모두 false, 유효 GUID를 확인했다. 포즈 연결을 다시 편집한 변경은 아니다.

## 실제 소켓과 버퍼

이 검증은 **실제 소켓 2개, 정적 AOI 카운터 프로브 200개와 제작 Mover 플레이어 2개**다. 200개 이동 캐릭터의 복제 부하나 200개 실접속 성능을 측정한 결과로 사용하지 않는다. 200명 렌더/이동은 앞 절, 100명 실제 NPC 추적은 별도 native fixture다.

| 실행 | 조건 | 결과 | 두 client의 수신/버퍼 |
| --- | --- | --- | --- |
| `MoverAuditNetwork02` | lag 0 / loss 0 | 서버·client 전부 정상 종료, AOI/FastArray·소유권·원격 애니메이션·R/다음 공격·disconnect 통과 | 926/924프레임, tail 수신 14.82/15.15Hz, tail 평균 buffer 62.32/66.49ms |
| `MoverAuditNetwork03` | 설정 `PktLag=80`, `PktLoss=2` | 같은 실제 소켓 검사 통과 | 980/975프레임, tail 수신 14.48/14.49Hz, tail 평균 buffer 72.28/72.31ms |

두 실행의 캡처 구간에는 reset·미래 소비·되감기·수신 정지·NP 진단 경고가 없었다. 최소 buffer는 일반 6.54ms, 지연/손실 1ms였고 이후 tail이 회복했다. 설정 지연과 실제 RTT를 동일시하지 않는다. 전송 byte 원자료는 각 실행의 `Server/network-phases.csv`에 별도로 둔다.

첫 `MoverAuditNetwork01`은 기능 assertion은 성공했지만, 연결 전 임시 월드 5행도 초기화된 stream으로 요구하고 초기 에셋 로드 경고까지 섞어 전체 검사에 실패했다. 검사기를 느슨하게 만들지 않고 기존 Stage 1의 **모든 client 배우·포즈·소유권 준비 확인 완료 후 Stage 2**에서 캡처를 시작하도록 runner/fixture를 연결했다. 초기 로드 경고는 일반 실행에 client당 1개가 남아 공개 결과에 보존한다. 캡처 marker 이후의 모든 NP 경고는 여전히 실패하며, authority 경고에도 예외를 주지 않는다. CSV의 정상 진입 이후 invalid/reset 행을 제거하는 동작은 추가하지 않았다.

## 유지한 것과 증거 경계

제작/테스트 에셋, 옛 migration commandlet과 당시 원자료는 보존한다. Historical migration은 현재 Master의 제작·회귀 계약으로 실행하지 않는다. BP 포즈 그래프·Chooser·골격·의상·무기 원본을 이름이나 텍스트 미참조만으로 삭제하지 않았다. 이 작업은 실제 200개 연결·Shipping/운영 서비스·전체 에셋 품질 승인이 아니다.

비행 solver는 rep-notify 수신 순서와 native 탑승/하차 회귀를 확인했다. owning client의 실제 비행 속도·correction·서버 자동 상승/착륙을 묶은 소켓 E2E는 이번 두-player 소켓 fixture가 다루지 않으므로 미검증으로 남긴다. 새 Cook/package·Shipping, MainWorld 전체 내비게이션과 장시간 밀집 전투 tail, 모든 체형/클립의 근접 외형 승인은 별도 범위다.
