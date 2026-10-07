# 일반 이동 회전 후보와 수명

체크포인트 커밋 `38f2268`(애니메이션 회전 연속성과 GASP 분석 기반 보정·진단 추가) 이후의 구현이다. 기존 GASP 조사 자료 [02_GASP](../Planning/GASP_ProjectJ_Naturalness_2026-10-06/02_GASP.md)의 기본 CMC MM 경로, 조건부 PSD 배열과 continuing pose, 각 Blend Stack 플레이어의 Steering 입력 계약을 사용했다. 비활성 실험적 State Machine의 실행 방식을 기본 경로로 간주하지 않는다.

## 문제와 적용 범위

사용자 로그에서는 느린 회전 중 몸이 목표를 빠르게 따라가면 몸/목표 150도와 velocity/새 입력 135도라는 큰 반전 진입 조건이 동시에 충족되지 않았다. 이 조건은 전진 180도 반전 클립에 필요하므로 낮추지 않는다. 일반 Run 회전은 별도 후보 수명으로 처리한다.

후속 사용자 로그(이번 검증 폴더의 `UserBefore.log`)에서는 General 창의 기록된 진입 12회가 모두 45도 클립을 먼저 선택했다. 진입 시 최근 변화는 주로 20~22도, 몸 목표 오차는 0~1.5도, 경로 오차는 약 9~14도였다. 몸이 이미 따라가는 작은 입력에도 속도 조건 때문에 Turn이 열렸고, Strafe에서는 Turn → Arc → Turn 재선택도 관측했다. 10Hz와 edge로 수집한 260개 선택 표본이므로 클립의 시간 점유율을 의미하지 않는다. 이 때문에 전역 PSS 가중치보다 후보 진입의 의도와 실제 Cycle 복귀를 먼저 보정했다.

| 상황 | 검색 후보 |
| --- | --- |
| 완만한 OTM Run 곡선 | 기존 Cycle + 플레이어별 Steering |
| 빠른 OTM Run 방향 전환 또는 실제 큰 정렬 오차 | Dynamic Cycle + GeneralTurn |
| 완만한 Combat Strafe 회전, 카메라만 회전, 옆/뒤 이동 | Combat Dynamic Cycle, 전진 GeneralTurn 없음 |
| 이동 방향과 카메라가 함께 빠르게 꺾이는 전진 Strafe | Combat Dynamic Cycle + Combat GeneralTurn |
| 기존 자격 있는 180도 반전 | 기존 TurnRedirect + Dynamic Cycle |
| Combat 옆/뒤 이동, 일반 창 비활성, 미연결 데이터 | 기존 Cycle 선택 |
| Start/Stop/Pivot/TIP/공중/착지/공격 등 | 기존 State Controller/몽타주 소유권 |
| Sprint, 원격, NPC, 탑승 | 일반 후보 창 없음, 기존 정책 |

동시에 검색하는 PSD는 최대 두 개다. 창을 열기 전에 실제 가족의 후보 호환성을 검사하므로 미연결/불일치 데이터는 수명과 SettledCycle 선택 자체를 바꾸지 않는다. Cycle의 Arc/Box/Diamond/Hourglass 등 기존 데이터는 삭제하지 않는다. GeneralTurn은 기존 broad Turn PSD의 45/90/135도 좌/우 및 양발 12개 entry만 복사한다. 기존 급반전 PSD를 수정하거나 새 일반 창에 180도 클립을 넣지 않는다. 복사 시 sampling range, mirror, exclusion 설정을 보존한다.

## 수명과 데이터 일관성

`FProject_JGeneralTurnPolicy`는 객체 참조 없는 값 정책이다. 기존 게임 스레드 애니메이션 업데이트에서 호출한다. 추가 Tick/RPC/게임플레이 이동 상태가 없다. 요청 이동 방향과 Facing을 고정 32개 링 버퍼로 기록한다. 0.35초의 방향 변화와 0.12초의 회전 속도를 시간 기준 보간으로 계산하고 yaw wrap을 푼다. 샘플링은 최대 약 50Hz다. 작은 좌우 흔들림을 절대 변화량으로 누적하지 않는다.

- 공통 진입 속도 기본 120 이상. 누적 방향 변화만으로는 Turn 후보를 열지 않는다.
- OTM: 최근 변화 기본 60도 이상이면서 짧은 구간 이동 회전 속도 120도/초 이상, 또는 실제 경로/몸 목표 오차 45도 이상. 몸과 경로가 따라가는 작은 20~45도 입력은 Cycle에 남긴다. 카메라 yaw 자체는 OTM 판정에 넣지 않는다.
- Strafe: 전진 범위에서 이동과 카메라가 같은 방향으로 빠르게 회전해야 한다. 카메라 회전 속도 120도/초 이상과 이동 방향 전환 조건을 함께 요구한다. 카메라만 회전하거나 이동 입력 방향만 바뀌면 방향별 Cycle이 담당한다.
- 최소 후보 시간 0.20초. 속도는 진입 조건이며 같은 회전의 일시 감속을 해제 조건으로 쓰지 않는다.
- 짧은 구간 회전 속도가 진입 기준의 절반 이하이고 경로 오차 15도 이하, 몸 목표 오차 12도 이하가 0.18초 유지되면 정상 종료한다. Strafe는 카메라 속도도 확인한다.
- 정상 종료 후 continuing 허가는 기본 최대 0.25초다. 새 전환 요구 또는 종료 방향/카메라 방향에서 20도 이상 변경되면 먼저 철회한다. 오래된 Turn의 유지 우위가 직진/완만한 회전까지 무기한 남지 않게 한다.
- 같은 가족의 실제 GeneralTurn 선택 이후 최신 Cycle 선택이 확인되면 즉시 후보 창과 completion 허가를 닫는다. 최초 Cycle 경쟁 결과나 이미 소비한 프레임은 창을 닫지 않는다. 선택 결과는 현재 프레임에서 최대 2프레임 전, 유효한 그래프 기여와 비-interaction 노드만 사용한다.
- Cycle로 복귀한 뒤에는 같은 전환의 잔여 0.35초 이력이 Turn 창을 다시 열지 못하도록 유지한다. 조용한 상태 0.18초, 반대 방향의 새 60도 전환, 또는 복귀 시점부터 20도 이상 달라진 목표에서 실제 정렬 오차가 확인되면 재진입할 수 있다. 시각 root의 보정 결과를 입력으로 되먹이지 않는다.
- Dynamic Cycle 수명은 Turn 후보 수명과 별개다. 이동 또는 Strafe 카메라가 8도/초 이상 회전하거나 정렬 오차가 남으면 rich Cycle을 유지하고, 조용해진 뒤 0.18초를 두고 기존 Settled 최적화로 복귀한다. 옆/뒤 이동의 곡선도 이 판정에 포함한다.
- 최대 시간 1.5초, 다음 진입까지 0.10초. 방향 반전·75도 초과 단일 입력 변화·시간 역전·0.1초 초과 샘플 단절은 일반 회전 창을 취소한다.
- Strafe는 요청 이동/카메라 방향 45도 이내, 속도/몸 방향 75도 이내의 전진 범위를 요구한다. 135도 이상 경로 반전은 기존 급반전/Pivot이 담당한다.
- 현재 Steering 소유권/유효 trajectory guard를 통과해야 한다. 입력 해제, 물리적 공중, one-shot, 공격/회피/피격, montage/root motion, 오래된 trajectory는 즉시 해제한다.

Strafe의 기존 `DesiredFacingYaw`는 one-shot 이동 목표이므로 카메라 방향으로 재사용하지 않는다. 같은 게임 스레드 업데이트의 현재 controller yaw를 사용한다. 시간은 기존 animation clock을 사용하며 time dilation을 다시 곱하지 않는다. 샘플은 진입/수명 판정에만 쓰고 MM과 Steering에는 최신 trajectory/현재 포즈를 계속 공급한다.

새 후보가 열리면 한 번 검색을 요청하되 기존 continuing pose는 유지해 Cycle이 계속 이길 수 있다. 정상 종료만 general continuation을 허용한다. 취소 또는 허가 철회는 primary PSD가 같아도 한 번 재검색하고 이전 continuing query pose를 무효화한다. 다만 새 primary Cycle을 이미 최신 MM 결과가 선택했다면 그 Cycle을 무효화하지 않는다. 기존 급반전의 취소 계약은 유지한다. 이미 혼합 중인 Blend Stack 플레이어를 직접 지우거나 시간을 0으로 되돌리지 않는다.

Combat의 완만한 회전 중이나 GeneralTurn이 실제 continuing 결과인 동안에는 Loop-only SettledCycle로 넘기지 않는다. 회전 요구와 completion 허가가 끝나면 기존 최적화로 복귀한다. 후보·수명·trajectory 데이터는 같은 proxy publication에서 소비하며, 중첩 traversal은 소비한 snapshot을 유지한다. 후보 edge만 선택 context를 갱신하고 기존 검색 throttle/예산을 유지한다. 유지 허가 만료 시 기존 재검색 계약이 continuing query를 한 번 무효화하지만 혼합 중인 플레이어는 지우지 않는다.

이는 GASP에서 확인한 조건부 후보·Loop 경쟁·Steering 계약을 Project_J의 OTM/Strafe에 적용한 정책이다. 회전 속도 120도/초, quiet/완료 시간 등은 Project_J의 조정값이며 GASP의 실제 상수라고 주장하지 않는다. 45/90/135 이름에 맞춰 클립을 강제로 고르거나 전역 PSS/bias를 변경하지 않는다.

## 정규화와 에셋 저장

두 후보는 정확히 같은 PSS와 공유 NormalizationSet을 요구한다. 새 GeneralTurn을 기존 가족별 NormalizationSet에 추가한다. 따라서 cooked index의 공통 특징 통계도 바뀐다. `p.ProjectJ.GeneralTurnCandidates 0`은 새 후보 수명만 끄며 정규화 변경을 되돌리는 기능은 아니다.

UE 5.8의 `NormalizationSet` 포인터는 editor-only다. 에디터에서 실체·포함 관계를 검증하고 Asset Set의 각 gait/후보 쌍에 cooked compatibility certificate를 저장한다. 저장·cook 시 실제 schema/normalization 관계로 다시 계산하므로 수동 데이터 변경도 오래된 certificate를 재사용하지 않는다. 비에디터는 같은 schema와 이 certificate를 확인하며, certificate가 없는 데이터는 단일 PSD로 안전하게 돌아간다. 데이터 구성을 수동 변경할 때는 자동화 검증 및 재-cook가 필요하다.

`ProjectJ.GeneralTurning.ProductionData`는 기본 읽기 전용 audit다. `-ProjectJApplyGeneralTurning`에서만 두 새 PSD와 기존 두 normalization, 두 Asset Set을 저장한다. 모든 가족의 source/schema/entry/root rotation과 dirty 여부를 먼저 검사하고 기존 소유 에셋을 `Saved/Validation/GeneralTurning_20261006/Before`에 보존한다. ABP·PSS·레벨·기존 Turn/Cycle PSD는 저장하지 않는다.

## 진단과 재현

콘솔 `p.ProjectJ.LocomotionContinuityTrace 2`로 기존 진단을 켠다. `Stage=Selection`에 `GeneralTurn`, `GeneralContinue`, `GeneralReason`, `RecentHeading`, `WindowElapsed`, `MoveYawRate`, `FacingYawRate`, `PathError`, `FacingError`, `DynamicCycle`, `SettledCycle`와 실제 candidates/PSD/clip/time을 함께 남긴다. 기본은 0이며 끌 때 같은 명령에 0을 넣는다. `p.ProjectJ.GeneralTurnCandidates 0/1`로 정책을 비교할 수 있다. 비용은 선택된 결과의 총비용이며 경쟁에서 진 Cycle/Turn의 비용이나 bias 기여량을 나타내지 않는다.

`Stage=NodeSettings`는 그래프 업데이트 후 대표 MM 노드의 실제 BlendTime, PlayRate, MaxActiveBlends, pose history/jump 값과 Profile 값을 함께 기록한다. `SearchThrottleRestored`는 일회성 강제 검색 뒤 복구한 throttle이며 검색 순간의 임시 0을 의미하지 않는다. `Demand`는 CommittedHeading/Alignment/None, `CycleHandoff`는 같은 전환 재진입 억제 상태다. `Stage=MMPlayer`는 MM 내부 최대 5개 플레이어의 실제 시간과 상대 블렌드 가중치를 남긴다. 이 가중치는 전체 그래프/최종 출력 기여량이 아니다. 진단은 기존 표본 주기를 사용하며 검색을 추가 실행하지 않는다.

실제 화면에서는 Run 전진 중 90도를 약 1초에 걸쳐 좌/우로 돌리는 완만한 곡선과, 약 0.5초에 90도를 꺾는 빠른 전환을 비교한다. Combat 전진, 옆/뒤 이동하며 카메라 회전, 회전 도중 감속·입력 해제·공격·착지도 확인한다. 특정 Arc/Diamond 이름의 승리를 강제하지 않으며 실제 미래 경로와 자세에 맞는 Cycle이 선택되어야 한다. 후보 수나 GeneralTurn 승리가 많다는 사실만으로 자연스러움이 입증되지는 않는다. visible follower의 발 접지, 방향 변경 지연과 최종 영상은 별도 시각 평가 대상이다.

## 작은 회전 억제와 Cycle 복귀의 검증

이번 변경의 근거는 `Saved/Validation/TurnIntent_20261006/verification.json`, 재검사 스크립트는 같은 폴더의 `analyze.py`다. PSS, PSD, normalization과 Asset Set은 이번 변경에서 저장하지 않았다.

- 회귀 자동화 39/39 성공. 30/60/144Hz, 양 모드와 좌/우, yaw wrap, 작은 반복 입력, 실제 정렬 요구, fresh/stale 선택 결과, 최초 Cycle 경쟁, Cycle 복귀 이후 같은 전환 재진입 억제와 새로운 반대 전환을 포함한다.
- 운영 Pawn/AnimBP/visible follower의 22개 NullRHI 재생 성공. 20도 빠른 입력, 30도 반복 입력과 정렬된 45도 빠른 입력은 양 모드 모두 General 후보·선택 0프레임이다. 느린 90도 회전과 Strafe의 카메라만 회전/옆/뒤 이동도 General 선택은 0이다.
- 빠른 90도/135도 전환에서 OTM은 각각 General 선택 41/49프레임, Combat은 12/28프레임이다. 각 전환에서 General 사용 후 Cycle 복귀 뒤 General 재선택은 0회다. Combat은 실제 Diamond/Box 등 Cycle로 복귀한다. 90/135도 이름의 클립 선택을 강제하지 않는다.
- 완만한 Combat 곡선에서 Cycle의 곡선 클립 92프레임, 반복 30도 입력에서 115프레임을 선택했다. OTM은 전진 Loop와 Steering을 선택했다. OTM에서도 반드시 Diamond가 이겨야 한다는 테스트를 두지 않는다.
- 일반 시나리오의 최대 시각 root 오차는 36.887도다. 기존 180도 급반전은 후보 30프레임, continuing 허가 50프레임과 시각 root 45도 한도를 유지했다.
- 실제 대표 노드의 774개 설정 표본에서 MaxActiveBlends=4, PlayRate=0.85~1.15로 Profile 값이 적용됐다. 스크린샷의 3과 0.8~1.1은 실행 결과의 값과 달랐다. BlendTime은 지상 0.2, 착지 등에서 0.5이며 pose history=0.3, local pose jump threshold=0~0이었다. 모든 값은 업데이트 후 관측이며, throttle은 기존 예산과 요청 상태에 따라 달라진다.
- 직접 UBT의 최종 Editor/Game 빌드 성공. 전체 39개 검사 이후 proxy의 논리적으로 중복된 조건을 제거하고 재빌드했으며, 최종 proxy 후보/수명 검사와 기능 OFF의 같은 22개 재생은 2/2 성공했다. 기능 OFF에서는 General 후보·선택이 모두 0이며 기존 급반전은 동일하다. 기본 진단 OFF 로그의 continuity 진단 행도 0이다.
- 최종 코드의 비로컬 기본 6개 운영 재생은 1/1 성공했다. 기록된 84개 Steering 표본 모두 alpha=0, 후보 1, General 후보·유지 허가 0이다. 네트워크 세션 결과로 일반화하지 않는다.
- 이전 구현의 신규 PSD/normalization/Asset Set 6개 SHA256과 체크포인트 `38f2268`의 Master/PSS/Cycle/Turn 8개 SHA256을 확인했다. Git LFS 파일은 체크포인트 pointer의 oid와 비교했다. 사용자 원본 로그의 SHA256도 보존됐다.

NullRHI는 실제 그래프의 선택·플레이어 시간·본 출력을 검사하지만 지정 velocity를 사용한다. 렌더링된 발 접지와 실제 마우스 조작 체감은 PIE에서 확인해야 한다. 이번 변경으로 전체 cook/패키징, 네트워크 세션이나 군중 CPU 측정을 반복하지 않았다.

## 완만한 곡선과 방향 전환 구분의 검증

이 절은 작은 회전 억제와 Cycle handoff를 추가하기 전 정책의 검증 기록이다. 근거는 `Saved/Validation/ContinuousCurves_20261006/verification.json`, 재검사 스크립트는 같은 폴더의 `analyze.py`다. 빠른 45도 진입 결과는 현재 정책의 기대 동작과 다르다.

- 직접 UBT Editor 빌드 38.26초, fixture 입력 보정 후 5.47초, Game 빌드 34.93초 성공.
- 최종 회귀 자동화 37/37 성공. 30/60/144Hz, 좌/우 20/60/90도/초의 연속 곡선, 양쪽 모드의 빠른 45/90/135도, yaw wrap, 노이즈, 감속, 모드 변경, 입력/소유권 해제, 미연결 데이터와 완료 허가 만료를 포함한다.
- 운영 Pawn/AnimBP/visible follower로 18개 NullRHI 재생 성공. 느린 OTM/Combat 전진 90도/1초에서 추가 후보와 GeneralTurn 선택은 모두 0프레임이다. OTM은 Cycle의 `Run_Loop_F`와 Steering, Combat은 Dynamic Cycle의 `Arc_Wide_R`를 실제 선택했다. Combat 곡선 클립 선택은 92프레임이다. 특정 Diamond/Arc 클립 승리를 강제하지 않는다.
- 빠른 90도/0.5초, 135도/0.75초, 45도/0.25초에서 양쪽 모드 모두 GeneralTurn을 실제 선택했다. 추가 후보 프레임은 각각 39/54/24이며 완료 허가 15프레임 이후 직진 꼬리에서는 철회된다.
- Strafe의 카메라만 회전/옆 이동은 후보·GeneralTurn 선택 모두 0프레임이며 Arc/Hourglass 등의 Cycle을 선택했다. 카메라 상대 입력과 지정 world velocity가 같은 방향을 나타내도록 fixture를 구성했다.
- 180도 급반전은 기존과 같은 후보 30프레임, continuation 허가 50프레임을 유지했다. 신규 11개 시나리오의 시각 root 오차 최대는 32.270도이며 급반전은 기존 45도 한도 이내다.
- 후보 기능 OFF의 같은 18개 재생 성공: GeneralTurn 후보/선택은 0, 기존 급반전은 동일하다. 진단 기본 OFF 로그의 continuity 진단 행은 0이다.
- 비로컬 기본 6개 운영 재생 성공: 샘플 84행 모두 Steering 0, 후보 1, GeneralTurn/허가 0이다. 이는 네트워크 세션 검증을 대신하지 않는다.
- 기존 일반 회전 PSD/Normalization/Asset Set 6개 SHA256이 초기 구현의 최종 증거와 동일하다. 이번 조정은 C++ 정책·설정·진단·테스트·문서에 한정했다.

이 조정에서 전체 cook/패키징, 실제 멀티플레이 세션과 군중 CPU 측정은 다시 실행하지 않았다. 지정 velocity의 NullRHI 재생은 실제 포즈와 선택을 확인하지만 렌더링된 발 접지, 입력 지연, 사용자의 마우스 조작 체감을 인증하지 않는다. 기존 로그는 보존하고 별도 `-abslog` 경로로 검증했다. 실제 화면에서 위의 완만한/빠른 회전 비교를 이어서 확인한다.

## 초기 후보 풀 구현의 검증

실행 근거와 최종 소스·에셋 SHA256은 `Saved/Validation/GeneralTurning_20261006/verification.json`에 기록했다. `analyze.py`로 결과와 제한 조건을 다시 검사할 수 있다.

- 직접 UBT 최종 Editor 빌드 19.16초, Game 빌드 24.55초 성공. Game 검증에서 확인한 `DataPreprocessor`와 `NormalizationSet`의 editor-only 접근도 분리했다.
- 실제 데이터 읽기 전용 audit 3/3 및 제한적 데이터 저장 1/1 성공. 두 가족의 12개 entry는 실제 루트 약 ±45/±90/±135도를 확인했다.
- 진단 ON, 기본 OFF, 최종 미연결 보호 조건 추가 후 각각 전체 34/34 자동화 성공. 기본 OFF 로그에는 continuity 진단 행이 없다.
- 실제 운영 Pawn/ABP/visible follower를 생성한 authored 재생 10개 시나리오 성공. 90도를 1초에 걸쳐 돌리는 OTM/Combat 전진은 몸/목표 150도 없이 각각 후보 75프레임, 정상 종료 후 허가 30프레임을 관측했다. 실제 GeneralTurn PSD 선택은 OTM 104프레임, Combat 32프레임이다. 뒷걸음은 후보·GeneralTurn 선택 모두 0이다.
- 기존 180도 반전은 후보 30프레임, continuation 허가 50프레임을 유지했다. 일반 회전 source root 최대 시각 오차는 OTM 13.831도, Combat 14.668도이며 급반전도 기존 45도 한도 이내다.
- 후보 기능 OFF의 같은 10개 재생 성공. 일반 회전 후보·선택은 모두 0이며 기존 급반전은 유지했다. 비로컬 6개 재생도 성공했고 모든 기록에서 Steering 0, 일반 후보 0, PSD 후보 1이다.
- 최종 로컬 전체 world tick 평균은 OTM 0.7369ms, Combat 0.7617ms, 후보 OFF 비교는 0.7800/0.8673ms였다. 60프레임 warmup 이후 120프레임의 단일 측정이며 실행 순서·캐시·클립 선택이 다르므로 감소량을 성능 향상으로 주장하지 않는다.
- 실제 서버/2클라이언트/100 NPC, 80ms 지연·2% 손실 검증 성공, 종료 코드 3개 모두 0, timeout 0. 근거: `Saved/Validation/GroupD_20260910/GeneralTurningNetwork_20261006/verification.json`.
- 실제 authored 캐릭터 1/200, 각각 ABA OFF/ON × Near/Far/Hidden/AttackBurst의 8행 CPU fixture 완료. 200개 ABA ON world tick 평균은 13.95/13.43/6.74/21.03ms였다. 이 fixture는 unpossessed authority 캐릭터로 군중 예산·일반화된 실행만 검증하며 로컬 Steering 비용 또는 변경 전/후 개선을 증명하지 않는다. 근거: `Saved/Validation/AuthoredAnimation_20261005/GeneralTurningCPU_20261006`.
- 네트워크와 군중 CPU는 마지막 미연결/비호환 보호 조건 전에 실행했다. 그 조건 추가 후 두 타깃을 다시 빌드하고 미연결 테스트를 포함한 전체 34개 및 같은 운영 재생을 재검증했다.
- 기존 Master, 두 PSS, 두 Cycle, 기존 두 Turn 및 broad Combat source 총 8개 파일의 SHA256이 체크포인트 상태와 같음을 확인했다.

NullRHI 재생은 CMC를 비활성화하고 속도·이동 입력을 지정한다. 실제 그래프의 포즈·선택·시간·본 출력은 확인했지만 렌더링된 발 접지·사용자 입력 지연·최종 체감 자연스러움을 인증하지 않는다. 전체 cook/패키징이나 패키지 실행은 수행하지 않았다. 변경된 공통 정규화와 visible follower 품질은 실제 에디터 플레이에서 확인한다.
## Strafe 방향 선택 재현과 진단 (21:37)

사용자가 지적한 문제는 A/S/D를 한 개씩 누른 채 카메라를 회전할 때 이동 방향에 맞는 Diamond/Box/Hourglass 대신 전진 Arc가 선택되는 현상이다. GeneralTurn이 나오지 않는 문제가 아니다. 사용자는 Loop 8개가 문제 발생 전부터 비활성화되어 있었다고 확인했다. Loop 복구를 원인 수정으로 제안했던 추론은 철회했으며 이번 조사에서 에셋을 변경하거나 저장하지 않았다.

새 `ProjectJ.Animation.Naturalness.StrafeCameraCMC` 재현은 실제 CMC, 충돌 바닥, 카메라 기준 A/S/D 입력을 사용한다. 속도·액터 회전은 직접 지정하지 않는다. 90/240/480도/초 × A/S/D 9개 장면에서 측정 구간 각 240프레임 모두 지상에서 실제 이동했다. 90도/초의 A와 D는 각각 219/240프레임 Arc를 선택하여 쏠림을 재현했고 S는 0프레임이었다. Steering 노드만 끄거나 새 continuity 정책 전체를 꺼도 같은 느린 좌우 쏠림이 남았다. 따라서 Steering만을 원인으로 확정할 수 없다.

진단 모드 2에 입력/속도의 상대 방향, 예측 이동과 몸 방향, 선택 시점의 authored root delta, 생성된 Pose History 노드의 실제 미래 루트/이동 방향을 추가했다. authored delta는 미러 적용 전이며 최대 0.1초 구간이다. 이 값 자체를 검색 인덱스의 방향이나 화면에 보이는 방향으로 해석하지 않는다. 로그는 기존 post-evaluation 경로에서 읽기만 하며 기본값은 OFF이다.

Editor/Game 빌드 및 비교 재현 근거는 `Saved/Validation/StrafeCamera_20261006`에 보존한다. 자동화 성공은 CMC 이동·진단 재현을 검증하며 방향 선택 문제가 해결됐다는 뜻은 아니다. 480도/초 카메라는 CMC 360도/초 회전을 초과하므로 카메라 기준 S 입력을 항상 몸 기준 후진으로 취급하지 않는다. PSS 가중치나 후보 제한을 바꿀 원인은 이 초기 조사에서는 확정하지 않았다. 아래 추가 조사에서 검색 비용을 분리해 비교했다.


## Strafe 검색 비용 분리 검증과 적용 제안 (추가 로그)

21:39:54 사용자 로그에서 이동 중 Strafe Cycle 표본 153개를 확보했다. 방향별로 A 36개 중 31개, S 58개 중 30개, D 58개 중 40개가 Arc였다. 모든 Arc 표본의 GeneralTurn 창은 닫혀 있었고 실제 선택 PSD는 Combat Run Cycle이었다. S 입력과 후진 예측이 유지되는 순간에도 전진 Arc가 선택되었으며, 카메라를 멈추면 Box/Diamond 등이 다시 선택되었다. 이 수치는 10Hz/상태 edge 표본이며 클립의 시간 점유율로 해석하지 않는다.

같은 실제 CMC 9개 장면을 전체 후보 BruteForce 검색으로 바꿔도 원래 PCAKDTree 결과와 Arc 프레임 수가 같았다. 이 재현에서는 근사 검색의 후보 누락을 원인으로 보지 않는다. 기존 인덱스에서 trajectory의 Velocity 항목 비용만 일시적으로 4/8/16배 높였다. 4배에서 느린 A/D의 Arc는 219/240에서 0으로 줄었지만 A 240도/초는 175프레임이었다. 8배에서는 A 240도/초에 21프레임 남았고, 16배에서는 A/S/D 90/240도/초의 6개 장면 모두 0이었다. 후보를 삭제하거나 클립을 강제로 선택하지 않았다. 이는 해당 선택에서 이동 속도·방향 비용의 상대 우선순위가 중요하다는 근거이며, 모든 원인이 하나라고 일반화하지 않는다.

실제 적용 구조도 임시 객체로 검증했다. Combat Run 전용 PSS 복사본에서 Position/Velocity/Facing이 섞인 샘플을 같은 시각의 별도 항목으로 나누고 Velocity 가중치만 16배 높였다. 기존 feature 순서, 35차원, 정규화 그룹, Facing/Position/포즈 가중치와 sample rate는 보존했다. Cycle/TurnRedirect/GeneralTurn PSD 3개와 공유 normalization을 임시 복제하고 인덱스를 새로 만들었다. 원래 PCAKDTree/PC4/KNN200 검색을 유지하여 A/S/D 9개와 W 3개를 재생했다. 90/240도/초 A/S/D 6개 장면은 Arc 0프레임 회귀 검사를 통과했다. W 90도/초에서는 Arc 219/240프레임을 유지했고, 다른 W 구간에서는 GeneralTurn도 실제 선택했다. 전진 회전 후보를 전역 금지하지 않는다. 전체 자동화 1/1 성공, 12개 장면 모두 실제 지상 이동 240/240프레임이었다.

원본 적용 제안은 `/Game/Animation_Logic/PSS/PSS_Combat_Run_Continuity`를 만들고 기존 Combat Run Cycle/TurnRedirect/GeneralTurn의 Schema 연결만 바꾸는 것이다. 기존 공유 normalization의 데이터셋 3개를 유지한다. 공용 PSS_Combat과 OTM PSS_Player, 기존 Cycle 애니메이션 목록·활성화 상태, Loop-only SettledCycle, State Controller one-shot과 몽타주 데이터는 보존한다. 이 단계에서는 임시 검증만 했으며, 이후 사용자의 “적용해보자” 요청으로 실제 저장을 승인받아 아래 적용을 진행했다.

근거는 `Saved/Validation/StrafeCamera_20261006`의 `UserLatest`, `BruteForce`, `Velocity4`, `Velocity8`, `Velocity16`, `SchemaTrial16`과 각 `Playback*.txt`, `verification.json`이다. 원본 Cycle 2개 해시와 사용자 최신 로그 보존을 확인했다. Editor/Game 빌드 성공. NullRHI는 화면상의 자연스러움이나 발 접지를 인증하지 않으며, 적용 후 실제 에디터 재생 확인이 필요하다.

## Strafe 전용 PSS 실제 저장과 재검증 (22:24)

사용자의 “적용해보자” 요청을 받아 새 `PSS_Combat_Run_Continuity`와 실제 Combat Run Cycle/TurnRedirect/GeneralTurn PSD의 Schema 연결을 저장했다. 실제 TurnRedirect는 `PSD_Player_Locomotion/PSD_Combat_Run_Turn`이며, 읽기 전용 broad 소스인 `PSD_Player_Combat_Locomotion/PSD_Combat_Run_Turn`과 구분한다. 새 PSS는 검증했던 Velocity 16배 분리를 적용한다. Position/Facing/포즈 채널, 35차원, 30Hz, PCAKDTree/PC4/KNN200, 기존 normalization 데이터셋 세 개는 유지했다. 세 PSD 원본은 `Saved/Validation/StrafeDirectionApplied_20261006/Before`에 보존했다.

공유 normalization의 한 PSD를 변경하면 나머지 인덱스도 무효화되므로 초기 로딩 완료 후 세 Schema 연결을 모두 바꾸고 새 인덱스를 요청한다. 첫 적용 시도는 이 순서 문제로 저장 전에 실패했고, 순서를 보정한 재시도는 1/1 성공했다. 애니메이션 entry 전체의 reflected 텍스트를 변경 전후 비교하여 목록·활성화·미러·sampling range·metadata가 같음을 검사했다. 공용 PSS 두 개, OTM Cycle/GeneralTurn/normalization, Combat normalization/Loop-only PSD, 두 Asset Set 총 9개 파일의 SHA256은 적용 전과 같다. 현재 사용자 로그도 보존했다.

새 에디터 프로세스에서 실제 저장 파일을 읽어 18개 검사가 모두 성공했다. 임시 스키마 복제, BruteForce 전환, 인덱스 가중치 오버라이드 없이 원래 검색 모드를 사용했다. 실제 CMC 12개 장면은 각각 240/240프레임 지상 이동을 유지했다. A/S/D 90·240도/초의 여섯 장면은 전진 Arc 0프레임이며 표본에서 Box·Diamond·Hourglass가 선택됐다. W 90도/초는 Arc 219/240프레임을 유지했고 Combat GeneralTurn의 실제 선택도 로그에서 확인했다. OTM/Combat authored 그래프 재생, GeneralTurning 수명, 후보 호환성, StrafeFacingRedirect 검사도 통과했다. Editor/Game 직접 UBT 빌드 성공.

근거: `Saved/Validation/StrafeDirectionApplied_20261006/verification.json`, `ProductionSchema.txt`, `FreshRead/index.json`, `StrafeCMCPlayback.txt`, `AuthoredPlayback.txt`. 480도/초는 몸 회전 한도보다 빠르므로 해당 구간의 Arc 횟수 자체를 실패 조건으로 삼지 않는다. NullRHI 검증은 화면상의 자연스러움·발 접지를 인증하지 않는다. 에디터를 새로 열어 Combat Strafe에서 A/S/D 하나씩 누르고 마우스를 천천히/중간 속도로 회전해 확인한다. 추가 노드 작업은 필요 없으며, 진단은 `p.ProjectJ.LocomotionContinuityTrace 2`로 켤 수 있다. 전체 cook/패키징·네트워크·군중 성능은 이번 데이터 보정에서 재실행하지 않았다.

## Strafe 전진 곡선의 GeneralTurn 과선택 보정

다음 사용자 로그(22:30:29 종료)는 A/S/D 개선 후 W와 카메라 회전에서 Turn이 과하게 선택되는 문제다. Combat Cycle 선택 표본 125개에서 GeneralTurn 실제 선택은 51개였다. Combat 진입 4회 중 일반적인 세 진입의 짧은 이동/카메라 회전 속도는 약 170~190도/초, 경로 오차는 약 14도, 몸/카메라 오차는 0도였다. 나머지 진입은 415도/초였다. 일반적인 진입도 누적 60도 조건을 만족해 후보가 열렸고 45도/90도 및 양발 클립 간 재선택이 있었다. 10Hz/edge 로그 표본이며 화면 버벅임의 시간 점유율이나 렌더링 원인을 직접 증명하지 않는다.

Strafe에만 별도의 정렬된 곡선 범위를 추가했다. 이동과 카메라의 짧은 구간 회전 속도가 모두 `GeneralTurnStrafeContinuousCurveYawRate` 기본 210도/초 이하이고, 경로 오차가 기존 alignment 진입각의 절반(기본 22.5도) 이하, 몸/카메라 오차가 12도 이하이면 누적 각도만으로 GeneralTurn 후보를 열지 않는다. 빠른 전환과 기존 큰 경로 오차는 전진 범위/동방향 coupled 조건을 계속 요구하며 Turn 자격을 유지한다. OTM의 진입 조건은 그대로다. 이미 열린 Strafe Turn도 같은 곡선으로 돌아오면 기존 최소 후보 시간과 0.18초 grace를 거쳐 끝나므로 마우스를 멈출 필요가 없다. DynamicCycle·Cycle 승자 handoff·bounded continuation은 유지한다. 새 Profile 속성은 C++ 기본값을 사용하며 데이터 에셋을 다시 저장하지 않았다.

30/60/144Hz의 양방향 150/180/200도/초 및 14도 경로 오차, yaw wrap, 빠른 전환 후 연속 곡선, 큰 실제 경로 오차를 검증했다. 실제 CMC 재생은 기존 12개에 W 180도/초, W 180±25도/초 흔들림, W 360→180도/초의 3개를 추가했다. 총 15개 장면 모두 240/240프레임 지상 이동했다. 새 완만한 두 장면은 GeneralTurn 후보/선택 0프레임이며 Arc는 각각 173/157프레임이었다. 빠른 회전 후 감속 장면은 GeneralTurn 113프레임을 실제 선택했고 마지막 60프레임의 후보/선택은 0이었다. 기존 A/S/D 90·240도/초 여섯 장면은 Arc 0프레임을 유지했다. 19개 자동화 검사와 Editor/Game 직접 UBT 빌드 모두 성공했다. AuthoredPlayback의 빠른 Combat 90/135도 장면은 새 곡선 범위를 넘는 240도/초로 명시했고 OTM/완만한 장면은 보존했다.

PSS/PSD/normalization/Asset Set 총 13개 관련 파일과 사용자 로그는 적용 전과 byte/hash가 같다. 근거는 `Saved/Validation/StrafeForwardTurn_20261006/verification.json`, `UserAnalysis.json`, `StrafeCMCPlayback.txt`, `AuthoredPlayback.txt`, `Tests/index.json`이다. 로그 명령은 그대로이며 `GeneralReason=ContinuousCurve`와 `SelectedPSD`에서 후보와 실제 선택을 구분한다. 에디터를 새로 열어 실제 화면 자연스러움을 확인해야 한다. 전체 cook/패키징·네트워크·군중 성능은 재실행하지 않았다.

## 실제 보정 요구를 기준으로 한 OTM/Strafe 후보 구조 (2026-10-07)

사용자와 토의한 뒤 속도/누적 각도가 독립적으로 GeneralTurn을 여는 계약을 폐기했다. 최신 로그에서는 Strafe가 210도/초 경계를 211도/초 정도로 넘어서 후보를 열었으며 OTM도 경로 오차 약 14~15도, 몸 오차 0도의 180~200도/초 곡선을 Turn으로 취급했다. 기존 속도 경계 보정은 특정 재현 조건을 통과했지만 사용자의 입력 범위를 충분히 다루지 못했다.

기본은 Dynamic Cycle과 기존 Steering이다. 실제 경로 오차 45도 이상, 또는 OTM의 실제 actor/이동 목표 오차 45도 이상이 동일한 보정 방향으로 기본 0.04초 유지돼야 Turn을 추가한다. Strafe는 기존 전진 범위와 이동/카메라 동방향 회전 조건도 요구한다. 카메라만 회전하거나 A/S/D 방향 이동은 기존 directional Cycle이 담당한다. 시각 root offset을 진입 조건에 되먹이지 않으며 CMC 이동·물리 회전·몽타주·State Controller 소유권은 그대로다.

진입과 종료의 오차 기준을 분리했다. 경로 15도 이하/몸 12도 이하로 회복하면 기존 최소 후보 시간과 quiet grace를 거쳐 끝나며 마우스를 멈추지 않아도 된다. 실제 Cycle 승자 handoff와 bounded continuation은 유지한다. 회복하지 않은 같은 방향 전환은 목표가 계속 흘러가도 재진입하지 않는다. 새로운 큰 반대 방향 보정 또는 안정적인 회복 후에 다시 자격을 평가한다. 초기 Cycle 경쟁은 실제 Turn 사용 전에는 창을 바로 닫지 않는다. 클립의 전 구간 강제 재생·Blend Stack 삭제·전역 검색 bias 변경은 도입하지 않았다.

새 Profile 속성 `GeneralTurnAlignmentConfirmation`은 기본 0.04초다. 이전 heading-only admission, OTM rate admission, Strafe curve-rate ceiling 속성은 직렬화 호환성을 위해 남기고 deprecated 처리했다. 각도 이력은 방향 반전과 handoff 판정에 사용하며 검색 비용의 후보 선택을 강제하지 않는다. 로그는 기존 명령에서 `AlignmentPending`, `Enter`, `ContinuousCurve`, `CycleHandoffHold`와 `Demand=PathAlignment/FacingAlignment`를 기록한다.

30/60/144Hz에서 단일 프레임 오차, 좌우로 번갈아 바뀌는 보정 방향, 진입 확인, 오차 hysteresis, 회복 중 계속 회전, timeout 후 동일 이벤트 재진입 억제를 검사했다. 실제 CMC 23개 장면 모두 지상 이동 240/240프레임을 유지했다. OTM 180/211/320도/초 및 Strafe 211/268/320도/초 곡선은 GeneralTurn 후보/선택 0프레임이었다. 급한 실제 방향 변화 장면에서는 Strafe GeneralTurn 28프레임, OTM 67프레임을 선택했고 마지막 60프레임은 모두 후보/선택 0이었다. 기존 A/S/D 90·240도/초의 여섯 장면은 Arc 0프레임을 유지했다. AuthoredPlayback의 90/135도 보정 장면에는 실제 body/velocity lag를 명시해, 단순히 빠른 각도 회전을 성공 기준으로 삼지 않았다. Editor/Game 직접 UBT 빌드 성공.

전체 관련 검사 결과는 16/20 성공이다. 나머지 네 실패는 지난 검증 이후 사용자가 수정한 Combat Asset Set에서 Run TurnRedirect 연결이 비어 있기 때문이다. 기본 후보·스키마·기존 180도 재생 audit가 이 누락을 검출했다. 읽기 전용 `ProjectJ.StrafeDirection.CombatTurnLink`는 1/1 성공했고 현재 슬롯과 GeneralTurn entry 전체를 기록했다. 기존 연결 `/Game/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Combat_Run_Turn` 복구 도구는 명시적 `-ProjectJRestoreCombatTurnLink`에서만 한 Combat Asset Set을 백업/저장한다. 사용자에게 의도와 복구 저장 허가를 요청했으며 승인 전 실행하지 않았다. 이번 작업에서 관련 에셋 13개와 사용자 로그는 보존했다.

근거: `Saved/Validation/TurnFrequency_20261007/verification.json`, `Tests/index.json`, `CombatTurnLink.txt`, `StrafeCMCPlayback.txt`, `AuthoredPlayback.txt`, `UserBefore.log`. NullRHI는 화면상의 자연스러움·발 접지를 인증하지 않으며 전체 cook/패키징·네트워크·군중 검증은 재실행하지 않았다.


## 사용자 연결 복구 후 커밋 전 재검증 (2026-10-07)

사용자가 Combat Run TurnRedirect를 원래 `PSD_Player_Locomotion/PSD_Combat_Run_Turn`으로 직접 연결하고 플레이 확인 후 한글 커밋을 요청했다. 앞서 실패한 AuthoredPlayback/ProductionData/ProductionCandidates/ProductionSchema와 읽기 전용 CombatTurnLink를 같은 저장 데이터로 재실행해 **5/5 성공**했다. 기존 180도 후보와 continuing 재생, 두 후보의 schema/normalization 호환성, 실제 Combat Run 전용 PSS 연결을 확인했다. 이 재검증에서는 코드·에셋을 수정하거나 저장하지 않았다. 이전 누락 상태의 보고서는 `CombatTurnLinkBeforeUserRestore.txt`에 보존했다.

근거: `Saved/Validation/CommitCheckpoint_20261007/Tests/index.json`, `AuthoredPlayback.txt`, `CombatTurnLink.txt`, `ProductionSchema.txt`. 새 사용자 로그는 `UserBefore.log`에 보존했다. Strafe 빠른 180도 근처 회전에서 Diamond 등의 선택이 남는 사용자 관측은 이 커밋 이후 별도 구조 토의 대상으로 남긴다.

## 2026-10-07: 큰 회전으로의 연결과 감속 수명

후속 구현과 검증은 [일반 회전과 180도 회전의 전환](Turn_Handoff_2026-10-07.md)에 기록했다. 두 회전 판정은 같은 물리 표본을 사용한다. 최종 구조는 OTM/Strafe별 프로필과 Tracking/Preparing/Active 상태로 큰 회전의 진입·유지·종료를 판단하며, 이미 활성화된 일반 회전의 연결도 상태로 제한한다. 준비 중에는 Cycle을 유지하고 승인된 큰 회전의 감속 중에는 이동 의도를 보존한다. 실제 입력 해제와 중단 조건은 계속 우선한다. Editor/Game 빌드, 자동화 36개, 실제 CMC 57개 장면 검증이 통과했으며 화면 테스트의 범위와 남은 확인 사항은 연결 문서에 구분한다.
