# Project J 오픈월드 기반 구현 인계

이 기반에서 이어진 구현·독립 검증과 현재 남은 작업은 [오픈월드·MMORPG 통합 인계](Project_J_OpenWorld_Project_Handoff.md)에 있다.

새 월드·직업·스킬의 연결 확인 절차와 월드 종료·병렬 처리 회귀는 [콘텐츠 추가 시 가장 작은 검사](Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#콘텐츠-추가-시-가장-작은-검사)에 정리했다.

**최신 재검증.** 메인 월드의 오래된 원경 프록시를 엔진 빌더로 정리했다. 새 직업의 슬롯, 선택 효과음, 원형 경고의 지면 표시와 지역 이관 시험에는 기존 HUD·GAS·음향 예산·온라인 접속 경로를 사용한다. [로컬 재검증과 제작 경로](Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#로컬-재검증과-제작-경로)

**후속 검증.** 제작 클래스의 실제 원형 스킬, 저옵션 원격 경고, 세 바이옴의 원경, 서로 다른 지도 간 온라인 왕복과 재접속, 영속 보상 키 이관을 실행했다. 제작 MainWorld의 16·40인 실측은 메모리 사전 검사에서 보류됐다. 각 결과와 원자료는 [Astra 독립 검증 결과](Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#독립-검증-결과)에서 확인할 수 있다.

2026-11-26 · UE 5.8.4 Win64 · 1~2인 개발, 로컬 PC 한 대 · 기준 소스 `f6eb7bc` 위의 작업 트리 변경

[설계안 2](Project_J_OpenWorld_MMO_System_Design_2026-11-26.md)의 첫 구현이다. 기존 UE Data Validation, Asset Manager, WP 소스, GAS 공격 실행과 Iris 필터를 확장했다. 추가 구독·호스팅·유료 플러그인이나 별도 관측 서버 설치가 필요 없다.

설계와 설계 변경 판단은 GPT-6.2 Astra, 구현·디버깅·회귀 검증은 GPT-6.2 Sol이 이어간다. 아래 제작 규칙은 현재 코드의 계약이고, 출시 사양·허용 팝인·전조 선행 시간은 아직 제품 결정 사항이다.

저장 맵의 중복 검사와 런타임의 첫 WorldState 선택, WPO 정책 필드와 검사의 실제 상한, 플레이어 비용 커밋 시점, 군중 필터 등록 범위를 소스와 대조했다. 후속 Sol 작업에서 검증용 포털·새 지상 탈것·같은 패키지의 비행 및 원경 전환을 확인했고, 서버 원형 경고와 첫 유효 콤보 노드의 비용 확정을 연결했다. 입력·실패 사례·완료 증거는 [Sol 세부 구현과 시험](Project_J_OpenWorld_Sol_Implementation.md)에 모았다.

후속 적용에서 작은 WP 검증 맵을 추가해 Runtime Hash Set의 HLOD 레이어 등록 누락을 검사하도록 보강했다. 프록시 2개, PIE 근경·원경 전환과 고속 지상 다리 왕복을 확인했다. 최신 Win64 Development 패키지에서는 일반 포털 상호작용에서 같은 경로를 재현했다. CanonicalNetwork 16·40명 및 제작 조우 B01 독립 경량 2명을 통과했다. 반복 원경 관측과 단일 PC의 제작 맵 메모리 한계는 세부 기록에 있다. [두 번째 지역 적용 결과](Project_J_OpenWorld_Sol_Implementation.md#2026-11-26-두-번째-지역-적용-결과)

재감사에서 누락·비지도 포털 검증과 ASC 연결 변경 직후의 경고 철거를 수정했다. 두 맵의 저장 상태, 최종 패키지와 전환 화면, 36개 집중 회귀를 확인했다. B01에서 발견한 단일 PC 시험 지연은 무제한·60FPS 경량 비교로 다시 관측했다. 각 인계 항목의 적용 증거와 아직 제작 데이터·환경이 필요한 범위를 [두 인계 문서 대조 결과](Project_J_OpenWorld_Sol_Implementation.md#2026-11-26-두-인계-문서-대조-결과)에 정리했다.

## 새 콘텐츠를 연결하는 방법

| 제작 대상 | 어디에 입력하는가 | 적용 결과와 범위 |
|---|---|---|
| 새 WP 야외 지역 | `/Game/World` 아래에 기존 WorldDefinition 타입을 만들고, 상시 로드되는 WorldState 하나에 연결한다. 배포 맵은 기존 Cook/여행 경로에 등록한다. | 정의 스캔이 MainWorld 고정 경로에서 프로젝트의 월드 디렉터리로 확장됐다. AlwaysCook 규칙은 해당 타입에 적용된다. 아래 명령은 실제 맵의 저장된 WP descriptor·WorldState·로드한 DA·데이터 검증과 Asset Manager 등록을 검사한다. |
| 별도 로컬 지도 포털 | `StandaloneDestination`에 저장된 다른 지도 에셋을 지정한다. | 제작 검사와 로컬 상호작용이 같은 Asset Registry 및 패키지 확인을 사용한다. 누락·비지도·현재 지도·장소/던전과 중복 지정은 거절된다. 온라인 도착·던전 티켓은 기존 경로를 사용한다. |
| 수관·랜드마크·근경 소품 | Static Mesh의 **Asset User Data**에 **Project J Distant Mesh Policy**를 한 번 추가한다. 원거리 반복 수관은 `InstancedCanopy`, 다른 HLOD 빌더도 가능한 랜드마크는 `DistantLandmark`, 근경 전용은 `NearOnly`다. 같은 메시를 그 배치에서만 제외하려면 해당 메시 **컴포넌트**에 `ProjectJ.World.NearOnly` 태그를 준다. | 에디터 저장/검증 시 Actor·컴포넌트의 공간 로드 및 HLOD 참여, 레이어를 확인한다. 수관은 Instancing을 요구하고 실제 WPO를 쓰면 기존 바람 종료 거리 경고를 적용한다. 루트 배치는 월드 기본 레이어를 상속할 수 있다. 기존 숲의 두 소유 태그는 호환된다. 분류되지 않은 UE foliage 컴포넌트는 제작자에게 경고한다. |
| 지상·공중 이동 | 기존 지상 Pawn은 속도를 제공하면 XY 예측이 유지된다. 새 공중 이동 Pawn은 `Project_JStreamingMovementSource`의 `UsesAirborneStreamingPrediction`에서 **현재** 공중 주행 여부를 반환한다. 조회는 가볍게 유지하고 월드/이동 상태를 변경하지 않는다. 두 기존 공중 탈것 부모에는 이미 연결했다. | 동일한 Loaded/Activated 제공자가 주행 상태를 이용한다. 선택적 감속과 명목 속도 보정은 해당 기능을 가진 기존 Mover 비행 경로에 남는다. 이 인터페이스를 구현해도 속도 제한이나 서버 이동 권한이 자동 부여되지는 않는다. |
| 몽타주 시전자 경고 | AttackDefinition의 `WarningPolicy = MontageAttention`, `RequiredWarningCue`에 의미 태그를 지정하고, 진입 섹션에 해당 태그의 Gameplay Warning Notify State 하나를 배치한다. 무경고 의도는 `NoWarningRequired`, 이전 데이터는 `Legacy`다. | DA와 Notify 검사가 같은 생산 조건을 사용한다. 섹션 내 생산자 누락·중복, 잘못된 필터/확률/전용 서버 조건, 첫 근접 타격보다 늦은 시작 등을 검출한다. Native NPC는 선택된 override를, 플레이어 콤보는 실행할 노드를 확인한다. Legacy는 저장·제출 검사 때 분류 안내가 나오고 기존 실행은 유지한다. B01이 사용하는 기존 클리브 DA는 실제로 이행했다. |
| 밀집 지역 서버 진단 | 기존 서버의 `/metrics`에 `projectj_iris_crowd_exempt_pairs`와 `projectj_iris_crowd_over_cap_pairs`가 추가됐다. | 마지막 필터 패스의 연결–Actor 쌍이다. 전체 제외 수에서 over-cap을 빼면 거리로 제외된 쌍을 얻는다. 허용 수에서 면제 수를 빼면 일반 허용 쌍이다. effective cap은 연결별 한도이며 쌍 수는 연결 합계다. 개인 ID 라벨이나 프레임당 별도 로깅을 추가하지 않았다. |

기준 구현은 [원경 메시 정책](../../../Source/Project_JCore/Public/World/Project_JDistantMeshPolicy.h), [에디터 검사](../../../Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetEditorValidator.cpp), [이동 인터페이스](../../../Source/Project_JCore/Public/World/Project_JStreamingMovementSource.h), [공격 정의 검사](../../../Source/Project_JCharacter/Private/Combat/Project_JAttackDefinition.cpp), [Iris 선정과 집계](../../../Source/Project_JCharacter/Private/Network/Project_JCrowdDensityNetObjectFilter.cpp)에 있다.

### 저장된 야외 지역 검사

프로젝트 루트의 PowerShell에서 아래 명령으로 대상 맵 하나를 검사한다. 프로젝트 지역 스트리밍을 사용하려는 맵을 지정한다. 샘플이나 일반 실내 던전까지 이 검사를 요구하지 않는다.

```powershell
$env:PROJECT_J_MAP_INVENTORY_ROOT = $null
$env:PROJECT_J_STREAMING_MAP = '/Game/World/MainWorld/Maps/L_MainWorld'
$projectRoot = (Get-Location).Path
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
    "$projectRoot/Project_J.uproject" -run=pythonscript `
    "-script=$projectRoot/Scripts/World/Validate-StreamingWorld.py" -unattended -nop4 -NullRHI
```

[검사 스크립트](../../../Scripts/World/Validate-StreamingWorld.py)는 별도 에디터 프로세스에서 실행한다. 전체 구역 액터를 강제로 로드하지 않고 월드 상태 배치만 로드한다. 성공 로그에는 맵과 실제 정의의 경로가 나온다. 맵 미등록·패키지 누락을 최종 판정하려면 실제 배포 경로에서 Cook 후 입장하는 후속 검사가 필요하다.

새 지도를 먼저 발견하려면 위 명령에서 환경 변수 두 줄을 아래로 바꾼다. Asset Registry가 지정한 루트 아래의 저장된 World 패키지를 열거하고 현재 패키징 설정의 명시적 `MapsToCook` 포함 여부를 각 행에 출력한다. 다른 제작 루트도 지정할 수 있다. 0건은 오류다. 명시 목록 밖에서 포털·소프트 참조나 별도 Cook 명령으로 포함되는 지도는 패키지 입장 시험에서 판정한다.

```powershell
$env:PROJECT_J_STREAMING_MAP = $null
$env:PROJECT_J_MAP_INVENTORY_ROOT = '/Game/World'
```

## 설계안 대조 후 수정한 기반

| 발견한 누락 | 현재 계약과 검증 |
|---|---|
| WP가 있는 것과 실제 스트리밍 활성 여부를 구분하지 못했다. 로드된 모든 WorldState를 다시 세면서 에디터 전용 배치가 결과를 바꿀 수도 있었다. | 야외로 지정한 저장 맵은 엔진의 `IsStreamingEnabled()`를 통과해야 한다. 저장 descriptor로 런타임 배치가 정확히 하나인지 확인하고 그 경로의 액터를 읽는다. 공간 로드인 WorldState, descriptor에서 읽지 못하거나 런타임 전환 대상인 Data Layer, 연결 DA와 등록 ID의 실제 대상 불일치는 오류다. 배치 HLOD 검사는 스트리밍을 사용하는 에디터 월드에서 동작한다. 꺼진 새 맵을 찾는 경로는 위 지도 목록과 의도한 야외 맵 검사다. |
| 수관 레이어의 시험 전용 판정이 실제 에디터 판정과 따로 있었다. | 중복 함수를 제거했다. 실제 배치 검사에서 상속 레이어, 누락, 잘못된 빌더, 직접 지정, HLOD 참여 해제, 근경 예외, 스트리밍을 끈 WP를 시험한다. `NearOnly`는 Project J 검사에서 해당 배치를 제외한다. 엔진의 HLOD 생성 설정은 제작자가 소유한다. |
| 기본 컨트롤러의 두 제공자가 월드 정의 누락·비활성 상태를 서로 다르게 처리했다. | 짝지어진 Loaded 제공자도 Activated 제공자의 등록·활성·고속 설정과 같은 월드 정의를 확인한다. 지상 전용 인터페이스가 없는 일반 Character로 정의 미연결→연결→소스 비활성 전이를 시험했고, 빠른 지상 예측은 높이 성분을 제외했다. 관찰 Pawn·이동 모드 교체와 제공자 재등록에서 비행 준비도 캐시를 무효화한다. 별도 제작자가 Loaded 컴포넌트만 배치한 기존 독립 사용은 자체 프로필을 유지한다. |
| 몽타주에서 마지막 경고를 지우면 생산자 자체가 사라져 몽타주 검사가 이를 놓쳤다. 또 같은 몽타주를 재사용하는 플레이어 콤보는 섹션이 비어 있으면 이미 지나간 경고 뒤에서 계속 재생했다. | 저장 몽타주의 직접 참조 패키지에서 AttackDefinition 타입만 좁혀 실제 몽타주 연결과 기존 경고 계약을 대조한다. 필수 경고는 검사·플레이어·NPC가 같은 진입 섹션을 사용하고 정방향 재생 속도가 유효해야 한다. 다른 플레이어 공격 표본으로 기존 GAS 태스크가 첫 경고 앞으로 되돌아감을 확인했다. B01은 저장 참조를 따라 생산자 전체 삭제를 검출했다. |

[월드·이동 제공자](../../../Source/Project_J/World/Project_JWorldStreamingComponent.cpp) · [실제 저장 의존성 검사](../../../Source/Project_JCharacterEditor/Private/Validation/Project_JCombatWarningAssetValidator.cpp) · [콤보 재사용 회귀](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp)

**이번 재점검 결과.** UE 5.8.4에서 Editor와 Game의 Win64 Development 직접 빌드가 성공했다. D3D12 집중 회귀에서는 월드 도착 permit·만료, 비행 고도·강하, GAS 콤보와 경고 수명, Iris 군중·서버 지표까지 확인했다. `/Game/World`에서 저장 지도 5개를 발견했고 명시적 MapsToCook에는 MainWorld 한 개가 들어 있었다. MainWorld→DA_MainWorld는 통과했고, `/Engine/Maps/Entry`를 야외로 지정한 명령은 예상 진단과 함께 실패했다. MainWorld의 독립 D3D12 PIE에서 B01 경고 표식 생성·해제, 공격 입력 10회, HP 100→0, 개인 우편 **1,881골드**를 확인했다. 맵과 제작 에셋 저장은 발생하지 않았다. 재점검 원자료는 `Saved/Validation/OpenWorldFoundation_Reaudit_20261126`의 `Verified/Automation/index.json`, `Maps4`와 `B01PIE/verification.json`이다.

기반 재감사 당시 집중 회귀는 고유 **14/14 성공**, 실패·경고 동반 성공·미실행 각 0건이다. 같은 프레임에 비행 탈것을 다른 지상 Pawn으로 바꾸는 시험에서도 이전 고도·진행 방향·후방 앵커·준비도 캐시가 새 계획에 섞이지 않았다. 최종 시험·지도 검사·PIE의 원자료 7개는 중앙 보관소의 `INDEX.csv`와 기존 `Deduplicated.jsonl`에 원래 경로·크기·SHA-256·중앙 위치로 등록했다. 복원에는 [기존 복원 도구](../../../Scripts/Maintenance/Restore-LocalArchive.ps1)를 사용한다.

## 최초 기반 구현 시 확인한 결과

첫 회차의 집중 회귀는 **고유 12개 성공**이다. NullRHI에서 11개, 렌더 장치가 필요한 GameplayWarningLifetime은 별도 D3D12에서 1개를 실행했다. 두 리포트 모두 실패 및 경고 동반 성공 0건이다.

- **빌드** — 설치 엔진의 직접 UnrealBuildTool로 `Project_JEditor Win64 Development`, `Project_J Win64 Development`가 정상 종료했다. 최초 에디터 컴파일에서 `GetStaticMesh()`의 `TObjectPtr` 형식 추론 오류를 확인했고, 기존 UE 반환형에 맞게 고친 뒤 다시 빌드했다.
- **월드** — 이름이 다른 임시 WP 월드에 태그 없는 수관 메시를 배치해 월드 기본 Instancing 레이어 상속, HLOD 참여 해제 시 실패, 명시적 근경 예외 통과를 확인했다. 저장 MainWorld에서는 `L_MainWorld → DA_MainWorld` 연결과 등록을 확인했다.
- **전투** — 실제 B01 공격을 복제한 격리 시험에서 정상 경고를 확인하고 생산자 전체를 제거했을 때 실패하는 것을 확인했다. 저장 DA의 정책·태그를 새 에디터 프로세스에서 재확인했다. 제작 MainWorld의 B01을 별도 D3D12 단독 PIE로 실행해 경고 생성·해제, 공격 입력 10회, HP 100→0, 재생성 대기 진입, 개인 우편 **1,625골드**를 관측했다. 시험은 기존 검증기로 위치·체력을 준비하며, 실제 300초 후 재생성까지 기다리는 시험은 아니다.
- **군중** — 기존 500개 일반 후보와 5개 면제 후보 시험에서 K=60일 때 면제 5, 한도 제외 440, 거리 제외 0을 확인했다. 연결별 동작을 검증하는 계산 표본이며 동접 인원이나 서버 수용량 실측은 기존 인계의 16/40인 근거를 따른다.

원자료는 같은 PC의 `Saved/Validation/OpenWorldFoundation_20261126`에 있다. `Final/Automation/index.json`은 NullRHI 집중 회귀, `WarningRHI/Automation/index.json`은 D3D12 경고 수명 시험, `B01PIE/verification.json`은 제작 보스 통합 결과, `StreamingWorld.log`는 실제 맵 검사, `boss-reopen.json`은 저장 후 재검증이다. 이전 회차의 비행·HLOD 패키지·40인 지연 수치는 이번 변경으로 얻은 결과와 구분한다.

## 새 콘텐츠에서 다시 확인할 경계

검증 로그·결과 13개는 기존 ArchiveStore `INDEX.csv`에서 원래 경로의 `OpenWorldFoundation_20261126`로 찾을 수 있다. 원래 위치와 중앙 하드링크의 SHA-256을 대조했다. 재실행할 때는 새 출력 경로를 사용한다.

1. **제작 배포 맵에서 원경을 인수한다.** RidgeRoute03의 포털·지상·비행·HLOD 재현은 Sol 문서에 기록했다. MainWorld 세 바이옴과 별도 중첩 Level Instance의 프록시 생성·실제 전환도 확인했다. 이후 추가되는 바이옴의 미분류 수동 배치와 실루엣은 해당 맵에서 검사한다. 로드된 배치와 저장에 연관된 외부 객체가 현행 검사의 입력이다. 메시 분류나 UE foliage 유형이 빠진 수동 배치는 제작자의 명시적인 분류가 필요하다.
2. **공격별 경고·비용 계약을 연결한다.** 몽타주 주의 표식과 서버 고정 원형 경고가 제공된다. 플레이어 콤보는 첫 유효 노드에서 서버 GAS 비용을 한 번 확정한다. 원형 경고는 시전자와 별개로 복제하며 사망·연결 변경·Avatar 종료 때 즉시 철거한다. 제작 Spellblade의 Ground Pulse는 비용·쿨다운과 저옵션 늦은 진입·재진입·만료까지 확인했다. 팀 전용·다단 공격·독립 장판·직접 호출 투사체는 각 제작 공격의 소유자와 표시 수명에 맞춰 검증한다. 피해는 서버의 예약·취소·대상 검사를 따른다.
3. **다인 비용은 재현 후 한 항목씩 조정한다.** 같은 인원으로 밀집/분산, 파티·길드의 유무를 바꾸고 over-cap·면제 수를 비교한다. 필터 선정 시간·송신·HP 수신·GT 적용 시간을 맞춰 40인 꼬리 지연을 좁힌다. 반복 효과의 병목이 생성/활성 수일 때만 Niagara Data Channels 표본을 추가한다. 최저 사양·프리셋·화질 합격선이 정해지기 전에는 현재 단일 PC 결과를 출시 예산으로 승격하지 않는다.
4. **되돌릴 때 데이터와 바이너리를 함께 본다.** 메시 정책이 실제 제작 메시에서 쓰이기 시작하면 참조를 이행한 뒤 타입을 제거한다. B01 계약 변경은 해당 공격 DA와 제작 스크립트의 같은 두 필드를 함께 관리한다. 새 지도 정의가 확장된 스캔 루트에 의존하면 예전 설정으로 돌아가기 전에 등록을 보존한다. 후속 원형 경고의 wire 버전 16은 대응 Game·서버 바이너리를 함께 적용한다. 저장소와 보상은 기존 마이그레이션·권위 계약을 따른다.

### 다음 검증에서 놓치기 쉬운 경계

- **발견과 배포.** 지도 목록은 지정한 루트의 저장 지도만 본다. 변경 지도·OFPA 소유 맵, 다른 마운트 루트와 실제 포털 목적지까지 합쳐 출시·실내·검토 용도를 판정하는 작업이 남았다. 미초기화 WP에서 배치 검사가 소유 맵 열기를 안내하면 그 맵을 연 뒤 재검사한다. 중첩 컨테이너의 유효 Data Layer, 공유 메시 변경·삭제 시 이전 셀의 프록시 무효화와 Cook 포함 여부는 대표 셀에서 먼저 확인한다. 메시 역할 기록, 생성 작업 소유 태그, 엔진 HLOD 레이어는 각각 다른 정보를 담는다.
- **이동과 도착.** 기본 컨트롤러의 짝지어진 두 제공자에서 고속 지상 경로는 XY, 비행은 3차원으로 계산한다. 일반 준비 경로는 실제 속도 벡터를 사용한다. 전용 Mover의 선택적 감속은 기존 기본값인 꺼짐을 유지한다. 새 지역의 실제 상세 그리드 이름과 프로필을 맞춘 뒤, 주행 중 교체·급선회·다리 진입·강하·포털 직후를 촬영한다. 프록시 가시 프레임과 필수 지면 충돌의 준비 시각을 따로 기록한다.
- **공격과 연출.** 새 경고 제작 경로가 GAS 플레이어 콤보, Native NPC 공격, 원형 공간 공격 중 어느 실행자를 통하는지 먼저 확인한다. 역참조 검사는 레지스트리에 기록된 직접 참조를 읽는다. 신규 미저장 공격이나 자체 GA·투사체·소환물이 만든 피해는 그 생산자의 실행·취소 수명을 시험한다. 실제 비용을 가진 제작 콤보는 첫 유효 노드의 서버 단일 비용 확정과 소유 클라이언트의 자원 수렴을 함께 확인한다. 독립 장판·다단 공격은 최소 반응 시간, 늦은 관찰자, 피해 종료 후 표식 잔류까지 포함한다.
- **캐릭터·군중·표시 예산.** 새 직업과 장비는 기존 전투 스타일·애니메이션 소유자·PresentationSet을 통해 연결한다. 원격 캐릭터의 포즈 갱신·장비 메시 LOD, 필수 경고와 선택 Niagara, UI 이름표·오디오 음성 수가 같은 혼잡 구간에서 어떻게 바뀌는지 확인한다. Iris의 owner-only 제한을 지난 후보가 집계 대상이며 K는 연결별 일반 후보 한도다. 500개 합성 후보와 한 대에서의 40 경량 클라이언트 기록은 각 시험 조건으로 보존한다. 다음 측정은 같은 인원의 밀집/분산에서 서버 선정·송신·클라이언트 적용과 화면·GPU 시간을 구분해 가장 비싼 구간을 고른다.
