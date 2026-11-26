# 오픈월드 맵을 새로 만들거나 교체할 때

새 맵의 이름은 디자이너가 정할 수 있다. UE는 그 맵에 저장한 World Partition(WP) 셀과 HLOD를 스트리밍하고, Project J는 플레이어 컨트롤러와 지역 정의에서 이동 예측과 도착 절차를 연결한다. 예를 들어 숲을 통째로 다시 만들면 새 나무의 원경 프록시는 다시 생성해야 한다. 기존 탈것의 이동 코드는 그대로 사용할 수 있다.

## 어떤 작업을 하는가

| 작업 | 재사용할 것 | 다시 정할 것 |
|---|---|---|
| 같은 맵에서 지형·숲만 수정 | 맵 경로, 기존 WorldState와 지역 정의, 유효한 장소·조우 ID | 바뀐 경계·노면·Nav·스폰·저장 위치, 식생 포함 범위와 프록시. 빈 맵에 가깝게 지웠으면 상시 액터와 입구도 다시 확인한다. |
| 다른 맵으로 같은 메인 지역을 대체 | 같은 지역·장소·배치를 가리키는 ID와 연결 의미. 기존 지역 정의를 유지할 경우 경계와 위치를 수정한다. | 새 패키지 경로를 사용하는 포털·서버 목적지·Cook 목록, 기존 캐릭터 복귀, 맵 경로가 들어가는 영속 키. 전환을 확인할 때까지 기존 맵으로 돌아갈 경로를 확보한다. |
| 독립된 야외 지역을 추가 | 기본 게임 모드·컨트롤러, 공통 제작·검사 도구 | 새 지역 정의와 고유 ID, 진입·복귀 지점, 이 지역의 가시 거리·예측 범위, 배포할 지도 목록 |
| 마을을 Level Instance로 만들거나 별도 실내 던전을 추가 | 반복 배치는 호스트의 원경 정책을 사용할 수 있다. 별도 던전은 기존 던전 정의·입장 절차를 사용한다. | 하위 레벨에 호스트 WorldState를 복제하지 않는다. 독립 목적지에는 포털과 귀환을 연결한다. 소형 non-WP 실내에는 아래의 야외 WP 검사를 적용하지 않는다. |

## 제작자가 에디터에서 할 일

1. **빈 야외 맵을 연다.** World Partition의 스트리밍을 켜고 World Settings의 GameMode와 실제 PlayerController를 확인한다. 기본 프로젝트 컨트롤러가 현재 위치와 이동 방향의 스트리밍 소스를 제공한다. 별도 컨트롤러나 차량이 이동을 소유하면 실제 플레이어 Pawn에서 속도와 공중 이동 상태가 전달되는지 확인한다. 새 지상 탈것은 다리와 급선회에서, 비행은 급강하와 착지에서 시험한다. [이동 소스 코드](../../Source/Project_J/World/Project_JWorldStreamingComponent.cpp)
2. **월드가 사용하는 데이터를 연결한다.** 야외 호스트에 런타임 `Project_JWorldState` 하나를 두고 `Is Spatially Loaded`를 끈다. 런타임 Data Layer 밖에 둔다. `Definition`에 해당 지역의 WorldDefinition DA를 연결하고 지역 경계, 장소, 연결, 도착 좌표와 예측 거리를 확인한다. 현재 Asset Manager는 `/Game/World` 아래에서 이 타입을 찾는다. 원래 MainWorld의 8×8km 좌표나 생성용 manifest를 새 지역에 그대로 적용할 필요는 없다. [지역 정의](../../Source/Project_J/World/Project_JWorldDefinition.h) · [검색·Cook 설정](../../Config/DefaultGame.ini)
3. **먼 곳에서 남길 모습을 정한다.** 예를 들어 산등성이 수관은 원본 셀이 내려가도 형태가 보여야 하고, 길가의 작은 풀은 가까이서만 보여도 된다. 수관에는 인스턴싱에 맞는 유효 레이어, 성채 같은 랜드마크에는 실루엣에 맞는 빌더를 사용한다. 맵 기본 레이어를 상속해도 된다. Runtime Hash Set의 해당 파티션에 레이어가 등록됐는지 확인한다. 근경 전용 메시 역할 또는 컴포넌트 태그 `ProjectJ.World.NearOnly`는 검사에 의도를 알린다. 실제 표시 거리와 HLOD 참여는 엔진 설정으로 편집한다. [원경 역할](../../Source/Project_JCore/Public/World/Project_JDistantMeshPolicy.h) · [배치 검사](../../Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetEditorValidator.cpp)
4. **저장하고 다시 연다.** 지형·식생·Level Instance 또는 셀 구성이 달라졌으면 해당 맵의 HLOD 설정과 빌드를 갱신한다. 큰 숲은 대표 구역으로 빌드 메모리와 결과를 먼저 살핀다. WP 맵 복제·이름 변경은 에디터의 WP 지원 절차를 사용한다. 맵, 외부 액터·오브젝트, 레이어, 생성 프록시가 함께 저장되고 다시 열리는지 확인한다. 레벨 파일 하나만 복사하면 외부에 저장된 배치가 빠질 수 있다.
5. **플레이 입구에서 왕복한다.** 일반 시작점이나 실제 포털로 입장해 스폰 발밑의 충돌, NPC 이동 경로, 문·입구·조우·귀환을 바뀐 장소에서 확인한다. 전투 장소는 보스 클래스, 공격과 보상 배치까지 연결한다. 원경 대상은 가까이→중간→멀리→복귀하는 같은 카메라 경로에서 확인한다. 화면, 원본 셀 언로드, 프록시 전환을 함께 보면 ‘배치 누락’과 ‘대체물이 안 보임’을 가를 수 있다. [공간 연결](../WorldGen/07_GAMEPLAY_SPATIAL_INTEGRATION.md) · [필드 이벤트](World_Event_Runtime.md)

## 작업 규모에 맞는 검사

소품 하나를 바꿨으면 열린 맵에서 해당 액터를 선택해 **Validate**를 실행한다. 새 야외 맵이나 WorldState·레이어 구성을 바꿨으면 저장한 맵도 검사한다. 아래는 기존 MainWorld를 대상으로 한 예다. 저장소 루트의 새 PowerShell 창에서 `PROJECT_J_STREAMING_MAP` 값을 검사할 맵의 패키지명으로 바꾼다.

```powershell
$editor = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project = (Resolve-Path './Project_J.uproject').Path
$check = (Resolve-Path './Scripts/World/Validate-StreamingWorld.py').Path
$env:PROJECT_J_STREAMING_MAP = '/Game/World/MainWorld/Maps/L_MainWorld'
$env:PROJECT_J_STREAMING_MIN_HLODS = '1'
& $editor $project /Engine/Maps/Entry "-ExecutePythonScript=$check" -unattended -nop4
```

저장 프록시가 필요한 맵에는 기대 최소 개수를 지정한다. 기본값 0은 개수 검사를 생략한다. `PROJECT_J_MAP_INVENTORY_ROOT`는 별도 목록 조회용이므로 단일 맵 검사와 동시에 지정하지 않는다. 성공 로그 `Project J streaming map validated`와 대상 DA 경로를 확인한다. 검사는 저장된 WorldState 수·상시 로드 조건·DA 유효성과 등록, 지정한 최소 프록시 수를 확인한다. 새 숲 한 곳이 통째로 빠졌는데 다른 곳에 프록시가 남은 경우도 개수 검사를 통과할 수 있다. 그 숲의 실루엣과 셀 경계 주행은 실제 플레이로 확인한다. [저장 맵 검사](../../Scripts/World/Validate-StreamingWorld.py)

## 기본 메인 월드를 교체할 때 기술 담당과 함께 볼 것

| 연결 지점 | 교체 확인 |
|---|---|
| 시작 맵과 실제 배포 | 현재 게임·에디터 기본 시작은 ThirdPerson이며 명시적 MapsToCook에는 L_MainWorld가 있다. 제작 맵 변경, 프로젝트 첫 화면 변경, 서버 실행 시 여는 맵은 서로 다른 선택이다. 포털 소프트 참조와 서버 실행 인자를 확인한다. 패키징 실행기의 기본 목록은 ThirdPerson과 Entry이므로 `-Maps`에 실제 입구와 목적지를 지정하고 일반 패키지에서 그 입구로 직접 들어간다. [시작 설정](../../Config/DefaultEngine.ini) · [패키징 실행기](../../Scripts/Audit/Run-ContinuationPackaging.ps1) |
| 맵 경로에 묶인 코드 | UE 5.8.4 Win64 Game/Client의 지형 PSO 재요청 보완은 기존 MainWorld 패키지의 특정 D3D12 재활성화 경로에 한정된다. 패키지명을 바꾸면 이 조건에서 벗어난다. 새 맵의 패키지에서 지형 셀 첫 방문·재방문을 비교해 보완의 필요성을 판단한다. [적용 조건](../../Source/Project_J/Project_J.cpp) · [빌드 조건](../../Source/Project_J/Project_J.Build.cs) |
| 온라인 지역과 저장된 위치 | 맵 안의 바이옴 이동에는 WP를 사용한다. 공개 지역 간 이동에는 채널 등록·티켓·세션 인계가 관여한다. 교체 전후 입장, 중단 뒤 재접속, 귀환, 옛 캐릭터의 지면·안전 도착점을 확인한다. 지역 ID를 유지해도 맵·채널·조우로 구성된 보스 정산 키는 달라질 수 있다. |
| 기존 데이터와 되돌리기 | 교체 전 구·신 목적지와 배치 키 대응, 재현 가능한 맵·외부 액터 버전을 남긴다. 영속 데이터가 있는 경우에만 대상 스냅샷으로 이관을 연습한다. 기존 이관 도구의 범위는 보스 정산이다. 전환 후 새 보상이 지급됐다면 구 DB를 단순 복원하면 그 기록을 잃을 수 있다. [SQLite 이관 도구](../../Scripts/Maintenance/Migrate-BossSettlementLayout.py) · [지역 이동·이관 결과](../Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#로컬-재검증과-제작-경로) |

인계할 때는 맵 경로·지역 DA, 실제 입구와 귀환점, 원경에서 남겨야 할 대상, 확인한 이동 경로·프리셋, 실패하거나 확인할 수 없었던 조건을 적는다. 기존 MainWorld를 편집할 때 필요한 좌표·제작 원본·환경 자산 목록은 [기존 맵 제작 인계](LEVEL_DESIGNER_GUIDE.md)에 있다. 시스템 전체의 상태는 [오픈월드·MMORPG 통합 인계](../Architecture/Planning/Project_J_OpenWorld_Project_Handoff.md)에서 이어진다.
