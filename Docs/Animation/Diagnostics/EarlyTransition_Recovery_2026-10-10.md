# EarlyTransition 누락 복원 — 2026-10-10

## 확인한 원본

Computer Use로 로컬 UE5.7 GASP의 `BP_NotifyState_EarlyTransition`을 열어
`Received Notify Tick` 그래프를 확인했다. Begin/End에서 단순 bool을 여닫는 구현이 아니다.
원본은 AnimInstance를 `SandboxCharacter_CMC_ABP`로 연결하고 블렌드아웃 중에는 요청하지 않는다.
`Always` 또는 `Gait Not Equal` 조건을 검사한 뒤 `Re-Transition` 또는 `Transition to Loop` 요청을 설정한다.
클래스 기본값의 `Always/Walk`를 각 애니메이션의 저작 값으로 해석하면 안 된다.

아래 6개 원본 인스턴스는 모두 **Re-Transition / Gait Not Equal Run**이다.
이번 작업의 복구 대상은 이 6개뿐이다.

| Run 클립 접미사 | 시작(초) | 길이(초) |
|---|---:|---:|
| Box_LR_F_Lfoot | 1.166783 | 2.033217 |
| Box_LR_F_Rfoot | 1.146522 | 2.086811 |
| Box_RL_F_Lfoot | 1.159815 | 1.873518 |
| Box_RL_F_Rfoot | 1.051964 | 1.914703 |
| Pivot_B_F_Lfoot | 2.506900 | 1.893100 |
| Pivot_B_F_Rfoot | 2.821284 | 1.545383 |

전체 경로는 `/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_`에 접미사를 붙인다.
기존 [2026-10-08 소유권 감사](../../Review/Audits/Project_J_Runtime_Ownership_Audit_2026-10-08.md)의
CR-01은 당시 누락 상태를 기록한 것이며 이번 복구 이전의 역사적 기록이다.

## Project J 적용

기존 native 구현은 구간 진입/종료의 전역 depth만 사용했다. 이를 실제 저작 조건을 가진 Tick 기반 bridge로 변경했다.

- `bRequireGaitChange=true`, `ExcludedGait=Run`을 저장한다.
- 블렌드아웃 이벤트를 무시한다.
- 현재 held one-shot과 같은 애니메이션만 조기 재평가를 요청할 수 있다.
  outgoing Blend Stack 또는 일반 Motion Matching 클립의 Notify가 다른 one-shot을 종료시키지 않는다.
- 게임 스레드에서 요청을 기록하고 immutable animation snapshot에 조건 결과를 전달한다.
  새 Notify Tick이 없으면 다음 animation update 이후 기한이 만료되어 오래된 창이 남지 않는다.
  엔진 전역 프레임 번호 대신 AnimInstance 갱신 revision을 사용해 NPC의 Animation Budget 간격도 허용한다.
- 현재 Project J의 locomotion gait intent가 Run일 때는 원본 6개의 창으로 조기 종료되지 않는다.
  Walk/Sprint로 바뀌면 기존 StateController의 다음 상태 재평가를 허용한다.
- GASP의 `Transition to Loop` 목적지를 일반적으로 구현했다고 주장하지 않는다.
  다른 저작 조건/목적지는 migration 도구가 거부한다.

이것은 포즈 보간·프레임률·네트워크 이동 스무딩을 개선하는 기능이 아니다.
GASP 에셋을 그대로 가져와도 Notify가 의존하는 Blueprint/AnimInstance가 없으면 원본 정책은 실행되지 않는다.
이번 작업은 누락 의존성과 원본의 조건부 전환 허용을 복원한 것이다.
모든 버벅임 해소 또는 시각 자연스러움 향상을 계량한 A/B 결과로 표현하지 않는다.
일반 MM 경로에서는 held one-shot bridge가 적용되지 않는다.

## 콘텐츠 변경과 검증

Editor 전용 `Project_JEarlyTransitionMigrationLibrary`는 원본 5.7 콘텐츠를 임시 mount로 읽고,
이벤트 GUID·시각·길이·전체 직렬화 메타데이터와 위 조건을 비교한다.
`Repair-GaspEarlyTransition.py`는 dry-run, SHA-256 원본 백업, apply, 새 프로세스 verify를 제공한다.
원본 GASP 프로젝트는 저장하지 않는다. Project J의 6개 NotifyState 객체 포인터만 교체했다.
Foley, PoseSearch Notify, 애니메이션 데이터, 트랙, GUID, 필터, 서버 플래그는 보존한다.

- dry-run: 6개 성공, 오류 0.
- apply: 6개 저장, 오류 0.
- 원본 mount 없는 새 UE5.8 프로세스 verify: 6개 native Notify, 메타데이터 변경 0, exit 0.
- byte 백업과 보고서: `Saved/Validation/MMOUI_20261010/EarlyTransition/`.
- 원본 인스턴스 조사: `Saved/Validation/MMOUI_20261010/EarlyTransitionSource.json`.
- `ProjectJ.Animation.EarlyTransition` 자동화 통과: 같은/다른 애니메이션,
  Run/Walk/Sprint 조건, 다음 AnimInstance 갱신 허용, 이후 만료, 비활성 one-shot 검사.
  관련 회귀 포함 24개 성공(실패/경고 0): `TestsFinal/index.json`.
- 최종 Editor 및 Game 직접 UBT 빌드 성공. 시각적 자연스러움의 A/B 계량 테스트는 수행하지 않았다.

원본 Blueprint를 5.8에서 읽는 authoring 문맥은 GASP 5.7의 VisualLogger/함수 타입 의존성 오류를 기록해
commandlet exit 1이었다. 이는 무오류 실행으로 분류하지 않는다.
별도 새 프로세스의 native 재로드 검증은 해당 원본을 로드하지 않고 exit 0으로 완료했다.
이 원본 의존성 문제를 이유로 GASP 함수/BP를 수정하거나 Project J에 복사하지 않았다.
