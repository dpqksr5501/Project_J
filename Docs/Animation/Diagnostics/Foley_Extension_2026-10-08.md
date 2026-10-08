# 마네킹 Foley 확장과 초기 오디오 연결

## 목표와 범위

MMORPG의 클라이언트 표현 서비스를 유지하면서 새 음원·애니메이션을 GASP 제작 규격에 종속시키지 않는다. 현재 대상은 기본 인간형 메시와 일반 바닥이다. 지형 에셋, Physical Surface 슬롯, water volume, 신규 테스트 맵을 작성하지 않는다. 실제 프로젝트 레벨과 환경 데이터가 생기면 동일 프로필에 표면별 음원을 추가한다.

런타임은 음원 경로·파일명·GASP BP·특정 MetaSound 입력 파라미터를 요구하지 않는다. 이벤트 태그와 좌우/배율은 노티파이의 작은 공통 계약이다. 기존 GASP 태그는 9,003개 노티파이의 의미를 보존하는 호환 데이터다. 새 태그도 Gameplay Tags에 등록하여 사용할 수 있다.

## 결정과 비용

| 결정 | 이유와 비용 |
| --- | --- |
| 정확한 정의 우선, 명시적 EventFallbacks | RunStrafe 전용음이 없을 때 Run을 지정할 수 있다. 암묵적인 태그 부모/문자열 추론을 하지 않는다. 전체 정책을 대체하며 group/접촉 위치도 함께 바뀐다. |
| 대체 탐색 최대 16개 정의 | 순환·오작성으로 런타임 비용이 무한히 증가하지 않는다. inline 저장소이며 정상 exact hit는 한 번의 정의 조회로 끝난다. |
| Foot/Hand/Capsule/Socket 접촉 정책 | 손 짚기와 몸 접촉을 발 본으로 처리하지 않는다. 최근 렌더링된 포즈만 사용하고, 없는/오래된 소켓은 캡슐 하단으로 돌아간다. 군중의 전체 본 평가를 강제로 깨우지 않는다. |
| 프로필 데이터 검증 | 잘못된 규칙·정책·필수 소켓·수치 범위와 이미 로딩된 루프 음원을 작성 단계에서 찾는다. 검증 목적으로 전체 음원을 동기 로딩하지 않는다. 실행 시 loop guard도 유지한다. |
| 기존 중앙 선별·비동기 로딩·예산 유지 | 추가 RPC, 캐릭터별 Tick, 동기 로딩, 여러 표면 트레이스를 만들지 않는다. 접촉 위치와 물리 표면은 선별된 요청에만 계산한다. |
| 미설정 상태의 무음과 스트리밍 중 drop 유지 | 아직 할당되지 않은 음원은 정상 제작 상태다. 할당되었으나 미로딩인 exact 음원은 fallback 대상으로 오해하지 않는다. 오래된 이벤트를 로딩 후 재생하지 않는다. |
| 로컬 소유 시점 비동기 준비 | 첫 PIE 시험에서 cold stream 중 10개 요청이 drop된 관찰을 반영한다. PossessedBy/PawnClientRestart에서 내 플레이어만 기존 shared cache에 준비 요청한다. 원격/NPC/전용 서버는 소유 hook으로 리소스를 추가하지 않는다. Tick/RPC/trace/contact replay를 만들지 않는다. |
| Unmapped/Fallbacks/Loops 계측 추가 | 실행 명령 ProjectJ.Foley.Status로 누락 데이터·대체 사용·루프 거부를 구분한다. 매 프레임 로그를 쓰지 않는다. |

## 초기 에셋 설정

새 에셋 `/Game/DataAssetSets/Audio/DA_Mannequin_Foley`에 이주한 GASP MetaSound 12개를 연결한다. `/Game/DataAssetSets/Animation_Profiles/AnimProfiles/DA_GreatSword_AnimProfile`의 FoleyAudioProfile만 새 에셋으로 지정한다. 다른 animation/combat/hand calibration 설정은 수정하지 않는다. 기존 캐릭터 프로필의 변경 전 파일은 `Saved/Validation/FoleyExtension_20261008/DA_GreatSword_AnimProfile.before.uasset`에 보관한다.

| 이벤트 | preset 접미사 | Group | Contact | 바닥 trace |
| --- | --- | --- | --- | --- |
| Walk | Walk | Footstep | Foot | O |
| Run | Run_Soft | Footstep | Foot | O |
| Jump | Jump | Jump | Capsule | X |
| Land | Land | Land | Capsule | O |
| Scuff | Scuff | Scuff | Foot | O |
| Handplant | Handplant | Other | Hand | O |
| RunBackwds | RunBackwards | Footstep | Foot | O |
| ScuffPivot | ScuffPivot | Scuff | Foot | O |
| ScuffWall | ScuffWall | Other | Foot | O |
| RunStrafe | RunStrafe | Footstep | Foot | O |
| Tumble | Tumble | Other | Capsule | O |
| WalkBackwds | WalkBackwards | Footstep | Foot | O |

모든 preset은 `/Game/Audio/Foley/MetaSounds/Presets/MSS_FoleySound_` 접두사다. Land Importance는 1.5, 나머지는 1.0이다. 런타임 코드에는 이 경로를 하드코딩하지 않는다. Slide.Loop는 종료 수명이 필요하므로 연결하지 않는다. ScuffWall은 지면 trace의 재질을 사용하며 벽 재질을 판정하는 기능으로 주장하지 않는다. Hand Side=None은 특정 손을 추정하지 않고 캡슐 contact로 돌아간다.

5개 명시적 대체 규칙은 RunStrafe/RunBackwds→Run, WalkBackwds→Walk, ScuffPivot/ScuffWall→Scuff다. 전용 정의가 채워져 있으면 그 음원이 우선한다. Handplant나 Tumble를 보행음으로 대체하지 않는다.

[초기 작성 스크립트](../../../Scripts/Editor/Configure-MannequinFoley.py)는 기본적으로 조회만 한다. `FOLEY_SETUP_APPLY=True`인 명시적 실행만 에셋을 작성한다. 타깃에 미저장 변경이 있거나 캐릭터에 다른 Foley 프로필이 지정되어 있으면 중단한다. 기존 작성된 프로필은 덮어쓰지 않으며, map/Blueprint/animation/이주한 audio는 저장하지 않는다.

## 단계별 환경 확장

1. 눈·진흙·목재 등의 바닥: 프로젝트 Physical Surface를 정하고 충돌/landscape에 Physical Material을 지정한다. 각 이벤트의 SurfaceSounds만 추가한다. trace miss나 미정의 표면은 기본음으로 돌아간다.
2. 얕은 물·젖은 신발: 바닥 재질과 환경 상태를 분리한다. 현재 단일 blocking hit만으로 물 깊이/젖음을 판정하지 않는다. 선정된 접촉에 환경 정보를 제공하고, 추가 splash 비용도 재생 예산에 포함한다.
3. 신발·장비: 애니메이션 태그에 지형/장비 조합을 넣지 않는다. 초기에는 ProfileOverride를 통해 별도 소리 세트를 선택한다. 실제 장비 데이터 구조가 준비되면 제한된 layered resolver를 도입한다.
4. 강도·체형: 현재 jump/landing 속도→배율은 초기 고정 범위다. 실제 필요가 생기면 프로필 곡선으로 이동한다. 점프 직전의 마지막 support surface, 착지의 실제 contact surface를 함께 제공하는 구조를 검토한다.
5. 지속음: sliding/swimming은 시작·갱신·종료와 EndPlay 종료를 가진 별도 emitter를 사용한다. one-shot 큐를 무한 loop 수명 관리에 사용하지 않는다.

## 검증과 증거

직접 UnrealBuildTool.exe로 Editor/Game Development 타깃을 빌드한다. 추가 자동화는 명시적 대체 우선순위·빈 정의·미로딩 exact 참조·순환·깊이 제한·실제 마네킹 발/손/소켓·capsule/오프스크린 fallback·데이터 검증을 검사한다. 기존 100-emitter 선별·local 보호·ground gate·dedicated server·수명과 animation/component 회귀도 함께 실행한다.

최초 ContactPolicies 실패는 `SK_UEFN_Mannequin` 스켈레톤을 USkeletalMesh로 불러온 테스트 경로 오류였다. 실제 메시 `SKM_UEFN_Mannequin`으로 수정했다. 런타임은 이 테스트 경로에 의존하지 않는다. 에디터 시작 로그에는 기존 콘텐츠 관련 경고/누락 dependency가 있을 수 있으며, 테스트의 성공/경고 수와 구분한다.

첫 실제 PIE는 기존 ThirdPerson 레벨의 기본 spawn 위치 충돌로 플레이어가 생성되지 않았다. 맵을 수정하지 않고 GameMode.RestartPlayerAtTransform으로 PIE 서버에만 임시 캐릭터를 생성했다. 클라이언트의 실제 BP_GreatSword 기본 프로필 연결과 source `SKM_Quinn_Simple`을 확인했다. Single-node로 이주된 Run 애니메이션을 재생해 native notify→서비스→실제 WASAPI audio device 재생 제출 경로를 검사했다. 첫 계측은 Played=41, Traces=41, NotReady=10, Budget=0, Loops=0이었다. 반복 애니메이션을 정지한 후 로컬 jump/landing에서 Played 303→305, Traces 302→303을 확인했다. Played는 actual voice/사람의 청취 품질 지표가 아니다. single-node 시험은 기존 Motion Matching 전체 연속 이동/전환 검증을 대체하지 않는다.

로컬 소유 준비 보완을 반영한 최종 직접 빌드는 Editor 101.51초, Game 61.02초로 성공했다. 자동화 15개 성공, 테스트 warning/error 0개였다. 새 PIE에서 소유 경계의 준비 이후 첫 명시적 접촉 요청이 바로 admission되었고, 이후 native notify 반복 시험에서 Played=138, Traces=138, NotReady=0, Budget=0, Loops=0이었다. Duplicate=1은 같은 발의 짧은 중복 admission 억제 결과다. 마지막 단일 pass 계측 0.068ms는 한 캐릭터의 그 순간 값이며 평균/최악 지연이나 군중 benchmark로 해석하지 않는다. 느린 장치에서도 첫 접촉의 로딩 완료를 보장한다는 의미는 아니다.

에디터를 다시 열어 12개 이벤트·5개 대체 경로·실제 캐릭터 프로필 연결을 재검증했으며 데이터 검증 error/warning은 0개였다. 초기 작성 스크립트 재실행은 기존 프로필을 유지하고 asset write=0이었다. 최종 PIE를 종료해 미저장 content/map package가 없음을 확인했다. MCP로 발/손 본과 캐릭터 프로필 연결을 확인하고 새 Foley Data Asset 편집기를 열어 화면을 캡처했다. 임시 Python remote execution은 false로 복원했으며 bind address는 127.0.0.1이다. Config, map, animation, 이주한 MetaSound/SoundWave/Mix의 저장은 이 확장 단계에 포함하지 않았다.

기계 판독 결과와 검증의 실제 한계는 [검증 결과](Foley_Extension_Validation_2026-10-08.json)에 기록한다. 실제 수백 명/저사양 CPU·GPU·voice 실측, 2인 PIE 지연, landscape/물 깊이, 사람이 평가한 음질·볼륨은 별도 검증 대상이다. 높은 품질을 지향한다는 이유로 구현되지 않은 acoustic propagation이나 전체 AAA 오디오 시스템을 완료했다고 주장하지 않는다.

후속 실제 정면 이동에서 referencer 목록에 없던 객체 없는 Foley 이벤트가 발견됐다. [정면 달리기 복구 기록](Foley_Forward_Run_Fix_2026-10-08.md)에 152개 에셋/893개 이벤트 복구와 실제 Motion Matching 정면·회전 검증, 16개 자동화 결과를 별도로 남긴다. 이 문서의 single-node 시험은 해당 후속 검증 이전의 기록이다.
