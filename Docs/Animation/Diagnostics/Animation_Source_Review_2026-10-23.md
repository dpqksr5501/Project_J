# 애니메이션 소스 수명과 Strafe 출발 점검 — 2026-10-23

## 범위와 구분

애니메이션 runtime/editor 소스·헤더 133개, 13,856줄의 목록을 기준으로 상태/선택·proxy·리타깃/IK·notify·Foley·예산·탑승 표현과 제작/점검 도구를 검토했다. 실제 입력·무기 표현·전투 판정·복제 이벤트 등 직접 소비 경계도 함께 확인했다. 파일 수는 검토 범위이며 모든 에셋의 외형·모든 네트워크 조합에 대한 인증 수치가 아니다. 기존 사용자 변경은 보존했다.

이전 snapshot 소비 순서·강제 Backward 이력 쓰기 제거·미소비 7필드 정리는 [앞선 기록](../Authoring/Player_Animation_Library.md)의 변경이다. 아래는 그 이후 확인한 수명 문제와 사용자의 실제 Strafe 재현에 대한 추가 변경이다.

## 확인하고 수정한 경계

| 경계 | 확인한 문제와 최종 처리 |
| --- | --- |
| retained Player AnimInstance의 Mover 교체 | 현재 값은 새 owner인데 predictor는 이전 Mover를 가리켰다. `BindLocomotionSource`가 based-movement 구독·predictor·궤적/방향 이력·공중 episode를 같은 source 수명으로 관리한다. 첫 궤적의 turn-angle 계산 전에도 새 속도를 게시해 이전 속도로 가짜 회전을 만들지 않는다. |
| 리타깃 품질 producer 소실 | 이전 Hidden 정책이 남았다. 매 GT update에서 완전한 정책을 캡처하며 producer가 없으면 기존 standalone 정책을 사용한다. |
| 리타깃 표현 구독 | Uninitialize 이후 GC 전까지 구독이 남고, retained owner 교체에서도 유효한 옛 컴포넌트를 계속 읽었다. 해제 시 구독/포즈 freshness를 종료하고, owner 변경·늦은 컴포넌트 발견도 초기화와 같은 등록 계약을 사용한다. |
| 근접 notify의 동기 이벤트 재진입 | 첫 hit 이벤트가 공격을 취소하고 새 hit window를 열어도 이전 sweep의 나머지 hit가 전달됐다. 실제 재현은 1회 대신 3회였다. 각 전달 전에 실행 identity·window token·owner를 재확인하고 콜백 동안 실행 객체를 pin한다. 늦은 옛 NotifyEnd가 새 window를 닫지 않는 것도 검사한다. |
| 이동 표현 EndPlay·재시작 | 컴포넌트가 살아 있는 동안 jump/fall-off/landing timer가 남을 수 있었다. EndPlay/Destroy에서 기존 소유 작업을 취소하고, tag 해제 콜백의 재진입·늦은 원격 이벤트가 새 작업을 등록하지 않도록 수명 admission을 닫는다. 새 BeginPlay는 새 표현 episode로 시작한다. 정상 실행의 기존 timeout 역할은 유지한다. |
| capsule 없는 APawn의 Foley | 공개 contact 계산에서 capsule을 무조건 역참조했다. capsule이 없으면 기존 pawn 위치 계약으로 계산한다. |
| 사용되지 않는 의미 입력 복사 | 방향별 Boolean action callback 12개와 pending copy를 거쳐도 실제 Mover 소비는 `IA_Move`였다. 미소비 callback·전달 필드·setter를 제거하고 실제 Move Completed/Canceled 취소 경계를 검사한다. 기존 BP 직렬화의 action reference와 독립 테스트용 MoveIntent 자료형은 유지한다. |

처음 추가한 source/품질 수명 테스트는 이전 구현에서 모두 실패했다. 이후 실제 회귀가 드러낸 무기 테스트의 mock attachment 초기화도 새 구독 계약에 맞게 고쳤으며 production 경고를 억제하지 않았다.

## 사용자가 확인한 뒤대각선 Start의 두 표현

사용자는 카메라를 고정한 Strafe에서 S+D/S+A에 두 표현이 나타난다고 확인했다. 등이 보이는 후진 출발은 자연스럽고, 하체/몸을 이동 방향으로 크게 돌리는 출발은 원하지 않았다. 처음에는 Start가 누락되는 문제로 조사했지만 실제 입력 기록에서 방향 계열이 달라지는 원인을 구분했다.

같은 카메라 기준 +135도 입력이 물리 속도 약 8cm/s에서는 `Forward` + `M_Neutral_Run_Reface_Start_F_R_180`, 약 2cm/s에서는 `Backward` + `M_Neutral_Run_Start_B_Rfoot`로 관측됐다. 원본에서 가져온 `StrafeStyle=1`은 이전 Forward일 때 140도, 이전 Backward일 때 120도를 경계로 사용했다. 뒤대각선 135도가 이력 유지 범위 안에 있어 첫 키 순서로 다른 출발을 선택할 수 있었다. 에셋이 없어서 생긴 누락은 아니었다.

유효한 로코모션 프로필의 `StrafeDirectionPolicy`를 native 입력 producer가 사용하도록 바꿨다. 기본 구간·각도 hysteresis와 상태 소유권은 [아키텍처 기준](../Architecture/Player_Locomotion_Architecture.md)을 따른다. 실제 회전 오프셋 Curve Chooser, 기존 Backward 모션, Pose Match 진입 시각, Steering·Warping·발 보정은 재사용한다. 전역 샘플 스타일 CVar의 실행 경로/Config 등록은 제거했고 관련 BP/ABP 38개 그래프 계약에도 해당 문자열 소비는 없었다. 소스·이동 벡터 기반으로 처리하므로 S 키/특정 클립 예외나 두 키를 모으는 timer는 없다.

후진 몸체를 카메라 yaw에 강제 고정한 변경은 아니다. 뒤대각선에서 기존 후진 커브의 약 45도 이내 비스듬한 몸 방향을 허용하며, 이동 방향을 바라보는 전방 Reface 계열로 뒤집히지 않게 한다. 감속 중 약 400cm/s에서 재입력한 경우 Loop로 이어지는 것은 정지 출발과 구분해 유지한다.

## 유지한 구조와 자원

Chooser/PSD의 후보·검색과 ABP 노드 실행 시점, 순서 있는 공중 이벤트, 공중 재선택 cooldown/진행률, gameplay tag/Foley timeout은 서로 다른 책임이다. 단순히 초·임계값이 있다는 이유로 제거하지 않았다. montage/포즈 이력·요청 시 playback 관측, 몸체별 RTG/접촉·추가 뼈와 공식 제작 kit, 샘플/미래 제작 자원도 실제 역할을 기준으로 유지했다.

GASP 에셋 추가 수입·복제, 생산 Content 저장, 키별 강제 방향 전환·root reset·IK 해제는 이번 수정에 없다. 기존 575개 시퀀스/11개 BlendSpace와 현재 제작 연결을 사용한다.

## 검증과 남은 범위

- 새 Editor/Game Development direct UBT 모두 성공. Strafe 정책 Editor 빌드는 42 actions/47.01초, 최종 Game 빌드는 48 actions/51.13초였다.
- 정책 수정 후 native/관련 엔진 회귀 **134개**, 실패·경고·미실행 0개. 방향 정책은 6개 이전 방향·8방향·각도 잡음과 analog 경계 이력, 실제 input producer의 양 뒤대각선·선행 입력·세 카메라 yaw·기존 Curve Chooser 결과를 확인한다.
- 같은 바이너리에서 관련 BP/ABP **38개** 재컴파일 성공, 파일 해시 동일. 새 프로필 필드의 기존 에셋 로드도 해당 회귀에서 확인했다.
- 새 `Validate-PlayerLocomotionStrafeStarts.py`: Walk/Run × 양 뒤대각선 × 동시/가로 먼저/세로 먼저, 정지·재정지·감속 재출발 **36회·5,039표본** 통과. 정지 출발 24회는 실제 Start 재생을 확인했고, 모든 최종 뒤대각선은 Backward였다. 관측 구간 몸체 yaw 최대 약 42.72도, 표시 source root 최대 약 44.75도였다.

- 같은 정책의 기존 8방향·키 시차 Pivot **36조건·19,430표본**, 지상 **2,394표본**, OTM/Strafe 공중·착지 **8조건·2,399표본**이 통과했다.
- 새 PIE에서 Strafe 질주 점프 **8조건·1,920표본**, 루프 완만 회전 **5조건·1,477표본**, 전신 몽타주 **6개·281표본**이 통과했다. 실제 게임 뷰포트 캡처 2장은 양 뒤대각선의 후진 Start/후진 Reface Start와 평가된 BlendStack을 함께 기록한다.
- 최종 회귀 선택 집합과 수명·방향 정책의 필수 테스트 12개를 영구 실행기에 반영했다. 증거 보고서에서 모두 Success/오류·경고 0임을 확인했다. 검증 에디터는 정상 종료했고 최종 Game 빌드 후에도 관련 에디터/빌드 프로세스가 남지 않았다.

정책 변경 전 사용자의 수동 입력, 이전 fresh PIE/72회 재출발 비교와 정책 변경 후 검증은 별개 실행이다. 초기 임시 캡처 API 오류와 연속 실행 중 전투 모드 복원 입력이 겹친 질주 점프 setup 실패도 보존했다. 후자는 실제 이동 표본 0개인 전제조건 실패였고 새 PIE의 8조건 성공으로 재검증했으며 production 로직으로 가리지 않았다.

local 상세는 `Saved/Validation/AnimationSourceReview_20261023`의 `FinalLiveSummary.json`, `StrafePolicyLive.json`, `RemainingFreshPIE.json`, `CaptureBackStarts.json`과 `Saved/Validation/PlayerLocomotion/AnimationStrafePolicy_20261023`에 남긴다. Cook/Shipping·모든 지형/체형/클립의 주관적 외형·새 군중 성능은 이번 점검 범위에 포함하지 않는다.
