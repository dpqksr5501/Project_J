# Unreal Engine 5.8 — MMORPG Lv.1~30 시작 권역 8×8km 구축 마스터 프롬프트 (v3)

> **문서 목적:** 이 문서를 새로운 Codex 대화에 첨부하여, *기존 Unreal 프로젝트와 임포트된 에셋/C++ 코드를 먼저 감사하고*, Unreal Engine 5.8의 공식 기능을 검증한 다음, **MMORPG 전체 월드의 첫 번째 Lv.1~30 지역으로서 실제 8×8km 단일 월드를 구성**하도록 지시한다. 월드 전체 구축과 권역별 품질 고도화는 별개 단계로 진행한다.
>
> **디자인 기준:** 검은사막에서 참고할 점은 사실적인 지형 스케일, 먼 거리의 지형 가시성, 자연스러운 도로·마을의 연결, 밀도 높은 자연환경, 빠른 탈것 이동의 연속성이다. **게임의 지형·건물·에셋·고유 표현을 복제하지 말 것.** 직접 정의한 세계관 및 중세 판타지 건축 양식으로 구현한다.
>
> **품질 표기:** 'AAA급'은 *제작 파이프라인·구조·지향 품질*을 뜻한다. 단일 개발자와 자동화만으로 AAA 완성도를 자동 보장한다는 의미가 아니다. 구현 결과와 성능 측정을 기반으로 단계별로 품질을 확인한다.

---

## 함께 전달되는 자료와 우선순위 (v3)

**구성 원칙: 이미지 1장 = 하나의 목적. 다수의 그림과 문장을 하나의 인포그래픽에 합치지 않는다.**

1. `References/01_Starter_World_TopDown_NoLabels.png`: **수직에 가까운 탑뷰 콘셉트 이미지 1장.** 처음부터 8×8km 전체를 만드는 거시 지형·수계·정착지 밀도 참고. 이는 **정확한 축척/좌표/높이맵이 아닌 생성형 시각 참고**다.
2. `ZONE_LAYOUT_LV1_30.json`: **Lv.1~30 시작 권역**의 사냥터 9개, 보스 2개, 정착지 7개, 외곽 확장 후보의 임시 좌표와 레벨 계획. 위치는 Landscape를 평가한 뒤 변경 가능하다.
3. `REAL_GAME_REFERENCES.md`: **검은사막·킹덤 컴: 딜리버런스 2·위쳐 3의 실제 스크린샷**을 한 장씩 열어볼 수 있는 **원본 페이지 및 이미지 링크**, 관찰할 상세 요소. 링크 파일과 저장된 실제 이미지 파일은 구분한다.
4. `References/02_Boss_Arena_Schematic.png`, `References/03_Hunting_Ground_Schematic.png`: **개별 전투 레이아웃 설명용 도식 2장**. 완성된 게임 아트/실측 좌표가 아니다.
5. `References/04_Previous_Levels_Before.jpg`: 사용자가 제공한 기존 레벨 스크린샷. 개선 전 상태를 비교하는 기준.

**자료 우선순위:** 현재 프로젝트의 실제 구조와 성능 > 본 문서의 명시적 요구사항 > JSON 수치 계획 > 실제 게임 사진에서 분석한 설계 원칙 > 생성형 콘셉트 이미지의 분위기. **생성 이미지의 특정 건축물·하천·색감을 그대로 복제하지 않는다.**

**반드시 구별:** `References/`에는 직접 제작/사용자 제공 이미지가 있으며, **외부 실제 게임 스크린샷 이미지는 저작권을 고려하여 ZIP에 넣지 않고 링크만 제공**한다. Codex가 외부 링크를 열 수 없으면 사용자에게 해당 이미지를 개별 저장한 경로를 요청한다.

---

## A. 프로젝트의 정확한 범위 — 후속 모든 섹션보다 우선

이 프로젝트에서 **8×8km는 게임 전체 대륙도, Lv.1~100 전체 맵도 아니다.** 최대 레벨을 임시로 100이라고 가정했을 때, 이 8×8km는 **플레이어가 Lv.1~30에 주로 활동하는 첫 번째 대형 시작 권역**이다. 이것을 ‘튜토리얼용 2×2km’로 축소하거나, Lv.60까지의 지역을 한꺼번에 넣지 말 것.

- **월드 범위:** 처음부터 시작 권역 지형 면적 약 8×8km 전체를 제작한다. 중앙 성곽 도시 1곳, 스타트 외곽 마을, 농촌·호반·숲·산악·항구 정착지 등 기능이 다른 장소를 연결한다.
- **레벨 진행:** Lv.1~5 교외·초원 → Lv.5~12 농촌·숲 가장자리 → Lv.10~20 호숫가·숲·하류 → Lv.18~25 해안/폐허 → Lv.22~30 산악·고원·고대 유적. 이 경로는 **주력 권장 동선**일 뿐 대륙을 레벨별로 물리적으로 격리하지 않는다. 플레이어는 일찍 높은 지역을 탐험할 수 있어야 하며 위험 지역에는 자연스러운 위험 신호를 제공한다.
- **9개 사냥터, 보스 1~2종:** 주요 필드 보스는 Lv.25~30 부근의 선택적 도전 콘텐츠로 기획한다. **플레이어 레벨 1~30 밸런스가 아직 없다면 데이터 스키마와 배치 우선**, 실제 스탯·드롭률은 승인 전 결정하지 않는다.
- **큰 초기 권역의 이유:** 낮은 레벨의 플레이어도 장시간 탐험할 수 있도록 주도로·우회로·월드 이벤트·풍경 구역·강·고도 차를 포함하고, 플레이어가 8km를 횡단하느라 초반 퀘스트가 지나치게 늘어지지 않도록 퀘스트 허브와 복귀 동선을 고려한다.
- **향후 연장:** 월드의 북·동·서 경계에 Lv.31+ 지역으로 이어질 도로·고개·수계·바이옴 연결 후보를 보존한다. 남쪽은 해안 및 장래 해상·섬 확장 가능성을 남긴다. 현재 단계에서 후속 지역을 구현하지는 않는다.
- **이미지 정책:** 탑뷰 한 장은 공간적 참조, 실제 게임 사진은 개별 환경/건축 참고, 계획 도식은 전투 동선 참고. 자료를 합성/압축한 하나의 포스터를 구현의 정답으로 사용하지 않는다.

### 성공 기준

1. 월드 전체 8×8km의 거시 지형·연결된 주요 수계·대표 교통망·정착지 후보·전투 지역 후보가 실제 에디터에서 존재한다.
2. 8×8km의 각 지역이 Lv.1~30 단계 중 어느 경험을 제공하는지 월드 데이터와 지도로 추적할 수 있다.
3. 중앙 도시의 규모와 건물 구성을 기존처럼 단순 복제하지 않고, 농촌·항구·호반·산악의 지형별 도로/필지를 각각 검증한다.
4. 빠른 탈것/이동기, 높은 산의 전망대 등 **최악의 팝인 시점**에서 실제 화면·프레임타임·메모리·스트리밍이 검증된다.
5. 별도 외부 월드로 갈 수 있는 경계 연결 계약을 실제 데이터로 남긴다.

---

## 0. Codex에 대한 최우선 지시

> **최신 변경:** 지금 만들어야 할 것은 **최대 Lv.100 MMORPG의 첫 번째 Lv.1~30 지역(8×8km)**이다. `ZONE_LAYOUT_LV1_30.json`이 지역 및 레벨의 기준이며, 이전 버전의 T1~T5 또는 31+ 보스/사냥터는 이 시작 권역에 넣지 않는다. 이어지는 31+ 구역은 향후 확장을 위해 경계 연결점만 확보한다.


당신은 Unreal Engine 5.8 오픈월드 기술 디렉터, 테크니컬 아티스트, 월드 빌딩 엔지니어, 성능 엔지니어 역할을 수행한다. **기존 프로젝트를 무시하거나 파괴하지 않고**, 검증 가능한 근거를 기준으로 설계하고 구현한다.

### 비타협 요구사항

1. **8×8km급 단일 오픈월드를 최초 구현 범위로 삼는다.** 2×2km 레벨 하나를 완성해 최종 목표인 것처럼 보고하지 않는다. 초기부터 최소 8km × 8km 지형 외곽, 전체 고도 분포, 주요 산맥·수계, 도로 위계, 도시·마을 입지, 월드 경계와 스트리밍 구조를 설계·구축한다. 단, 전 영역의 AAA급 환경 디테일을 한 번에 완성한 것으로 포장하지 않고 *전체 월드 골격 구축 → 대표 권역 정밀 구현 → 전체 권역 확장* 순으로 관리한다.
2. **단일 Persistent Level + World Partition**을 기준으로 한다. 별도의 맵 로딩 없이 지역 사이 이동이 가능해야 한다. 내부의 지역별 스트리밍, Data Layer, HLOD 등의 사용은 허용한다.
3. **X·Y뿐 아니라 Z축의 플레이 가능 공간을 설계**한다. 큰 산맥과 고원, 깊은 계곡, 실제 올라갈 수 있는 산길, 전망대, 산악 마을, 고도차를 포함한다. 멀리 보이는 산이 단순 배경이 되지 않도록 설계한다.
4. **이동 속도가 높아도 건물과 식생이 뒤늦게 튀어나오는 현상을 억제**한다. 단순한 로딩 거리 증가만으로 해결했다고 보고하지 말고 HLOD·식생 원경 표현·선행 스트리밍·텍스처/메시 스트리밍·프레임타임을 종합 테스트한다.
5. **수문학적으로 설득력 있는 하천·호수·해안**을 구성한다. 물은 대체로 높은 고도에서 낮은 고도로 흐르고, 지형에 따라 강폭·강바닥·곡률·지류·합류점·계곡·평야·하구가 달라진다. 강이 갑자기 생성·종료되거나 언덕을 거슬러 흐르지 않아야 한다.
6. **향후 지형 확장 가능한 외곽 경계**를 준비한다. 외곽을 임의의 수직 절벽이나 무한 바다로 고정하지 않고 확장 가능한 지형·수계·도로 데이터와 경계 이음 규칙을 마련한다.
7. **기존 C++ 코드, 에셋, 플러그인, 마스터 머티리얼, PCG, 도로·마을 생성 로직은 사전 조사 후 재사용 여부를 결정**한다. 무차별 재구현·중복·삭제를 금지한다.
8. **실제 Unreal Editor 결과 검증**이 필수다. 소스 코드가 컴파일되었다는 것과 에디터에서 레벨이 정상 생성되었다는 것, 패키징 플레이에서 스트리밍·팝인·성능이 정상이라는 것은 각각 별도로 입증한다.
9. **디자이너가 수정 가능한 결과**를 만들어야 한다. 중요한 도시·산길·강·성벽·랜드마크와 PCG 배치 결과는 지정 범위에서 수동 편집할 수 있어야 하며, 재생성 시 보호 정책이 필요하다.
10. 실험적 기능은 안정성을 검증하고 회귀 가능한 기본 구현을 준비한다.
11. **사냥터 9개와 필드 보스 1~2개를 8×8km 월드의 실제 콘텐츠 레이어로 설계**한다. 단순 장식용 몬스터 모델 배치가 아니라 사냥터 출입 동선, 위험도, 스폰/디스폰, 몬스터 AI, 보스 집결 공간, 멀티플레이 상태와 성능을 별도 요구사항으로 관리한다. 몬스터 스폰 데이터와 정적 PCG를 혼동하지 않는다.
12. **첨부 이미지 및 `ZONE_LAYOUT_LV1_30.json`을 읽고 활용**한다. 단, 좌표와 묘사는 기획용이며 실제 고도·경사·수계에 따라 유효성을 검증해 조정한다. 의심스러운 문서/툴/API는 실제 프로젝트에서 확인할 때까지 '검증 필요'로 표시한다.

### 절대 금지

- 기존 레벨/에셋/코드를 사전 승인 없이 삭제·이동·대량 리네임하거나 기존 맵을 덮어쓰기.
- 프로젝트에 이미 있는 건물·자연환경 에셋을 조사하지 않은 채 퀄리티가 낮은 임시 메시만 대량 배치.
- 광범위한 랜덤 노이즈 언덕 반복, 균일한 수목 확률, 획일적인 직교 도시 그리드.
- 외관 사진 한두 장만으로 전체 레벨이 완료되었다고 주장.
- 포괄적인 `r.ViewDistanceScale` 증가로 성능 문제를 숨기기.
- 불필요한 플러그인 설치·외부 유료 에셋 구매·라이선스 수락을 사용자 승인 없이 수행.
- 메모리 초과·에디터 크래시가 발생하는 대규모 생성 그래프를 검증 없이 한 번에 실행.

---

## 1. 사용자 제공 환경 정보 — 여기에 채워 넣을 것

아래 항목은 **현재 알 수 없는 값**이다. 추측해서 채우지 말고 사용자 입력 또는 로컬 프로젝트 검사 결과로 작성한다.

| 항목 | 입력/조사 값 |
|---|---|
| 프로젝트 루트 경로 | `TODO: D:\\...\\Project` |
| `.uproject` 경로 | `TODO` |
| 정확한 엔진 버전/빌드 | `TODO — 5.8.x 확인` |
| Git 저장소/현재 브랜치 | `TODO` |
| 기존 메인 레벨(.umap) | `TODO` |
| 기존 C++ 월드/PCG/도로 클래스 | `TODO — 코드 탐색 후 채우기` |
| 기존 Unreal 에셋 Content 경로 | `TODO: /Game/...` 복수 허용 |
| 로컬 추가 에셋 원본 폴더(FBX/GLB/Textures 등) | `TODO: D:\\...` 복수 허용 |
| 기존 Fab/Quixel/구매 에셋 목록 | `TODO — 라이선스/엔진 호환 확인` |
| Codex 실행 환경 (로컬/원격/컨테이너) | `TODO` |
| Unreal MCP 연결 상태 및 사용 가능 도구 | `TODO — 실측 도구 목록` |
| 타깃 GPU/CPU/RAM/VRAM/저장장치 | `TODO` |
| 타깃 해상도/그래픽 옵션/목표 프레임 | `TODO — 초기 기준은 60FPS 검토` |
| 도보/스킬/탈것의 실측 최고 속도 | `TODO (m/s)` |
| 멀티플레이 동시 인원/서버 요구 | `TODO — 월드 디자인과 서버 성능 별도 관리` |
| 캐릭터·NPC·몬스터 기존 C++/AI/Ability 시스템 | `TODO — 전투 기능과 재사용/수정 후보 조사` |
| 기존 필드 보스/몬스터 스켈레탈 메시 및 애니메이션 | `TODO — 실제 에셋 경로와 라이선스 확인` |
| 기존 몬스터 Spawn Manager / Encounter Manager | `TODO — 구현 유무 및 서버 권한 검토` |
| 기존 퀘스트·경험치·전리품·레벨 시스템 | `TODO — 현재 기획은 최대 Lv.100 가정, 시작 월드는 Lv.1~30만; 기존 실제 성장 시스템 조사 후 검증` |
| 첨부 이미지/기획 데이터 기준 경로 | `References/`, `ZONE_LAYOUT_LV1_30.json`, `REAL_GAME_REFERENCES.md` |

**경로 작성 권장:** 실제 폴더 경로를 그대로 제공하되, 우선 Unreal의 **Content Browser 패키지 경로 `/Game/...`**를 제공하고, 별도로 외부 원본 에셋이 존재할 때에만 Windows 절대경로를 제공한다. Content의 실제 하위 폴더를 지정해 두면 Codex가 사용 가능한 에셋을 분류하는 데 도움이 된다. 중요한 개인 계정·라이선스 비밀값·로그인 토큰은 제공하지 않는다.

---

## 2. 최초 작업: 기존 프로젝트 읽기 전용 감사

### 2.1 보호 및 백업

- Git 상태(`git status`), 브랜치, 대형 바이너리·Git LFS 사용, `.gitignore`, `Content/`, `Plugins/`, `Source/`, `Config/` 구성 확인.
- 현재 작업 변경분 보존. Git 커밋/백업 완료 여부를 확인하고, 변경은 **신규 브랜치**에서 진행할 것을 제안. 기존 레벨의 `.umap`만이 아니라 `__ExternalActors__`, `__ExternalObjects__` 및 참조된 에셋까지 보존할 것.
- `Saved`, `Intermediate`, `DerivedDataCache`는 작업 환경 캐시이므로 필요한 것이 무엇인지 구분하되, 프로젝트 구조 조사 과정에서 임의 삭제하지 말 것.
- 기존 World Partition을 신규 레벨로 전환할 때 유효한 콘텐츠 종속성과 Redirector를 점검할 것.

### 2.2 실제 사용 가능한 코드/에셋 인벤토리

출력: `Docs/WorldGen/01_AUDIT.md`, `Docs/WorldGen/ASSET_INVENTORY.csv` (가능하다면).

- `Source/**`: `UWorldSubsystem`, `AActor`, `UActorComponent`, `UDataAsset`, Editor Utility, PCG Element/Settings, Landscape 수정 코드, spline, road/pathfinding 관련 클래스를 실제 파일명과 함께 기록.
- `Content/**`: 맵, Static Mesh, Material, Material Instance, PCG Graph, Blueprint, Data Asset, Landscape Layer, FoliageType, Niagara, HLOD Layer, Water, Level Instance의 실제 `/Game/` 경로 기록.
- 에셋별 `지역/유형/크기/폴리곤/나나이트/LOD/충돌/텍스처 크기/마스터 머티리얼/라이선스/중복`을 필요한 범위부터 수집. 읽을 수 없는 메타데이터는 '미확인'으로 둔다.
- 재사용 / 수정 후 재사용 / 신규 개발 / 보류 / 폐기 후보로 나누고 근거를 작성. **폐기는 추천만 하고 실제 삭제는 승인받을 것.**
- 기존 임포트된 중세 건축, 성벽, 농경, 숲, 암석, 수변, 도로, 장식 에셋은 가능하면 우선 활용.
- **게임플레이 재사용 조사:** 기존 몬스터·NPC·보스 AI, Behavior Tree/StateTree, Blackboard, EQS, Gameplay Ability System, AnimBP, 시체/루팅, Spawn Point, Quest/Experience 코드도 같은 수준으로 감사. 새 Spawn Manager/Combat Component를 무조건 만들지 말고 기존의 인터페이스·데이터 계약을 우선 확인.
- **몬스터 에셋이 부족한 경우:** 필요한 크리처 모델/리깅/애니메이션/이펙트를 `ASSET_GAPS`에 명확히 기록. 검증 전까지 임시 프리미티브/대체 모델로 동작 테스트는 가능하지만 이를 완성된 AAA 보스라고 보고하지 말 것.

### 2.3 Unreal MCP 상태 검사

공식 Unreal MCP는 UE 5.8의 **Experimental** 기능이다. Editor 플러그인 `Unreal MCP`, `All Toolsets`를 확인하고, 지원 도구를 실제로 나열한다. 동작하지 않거나 연결되지 않은 기능은 수행했다고 주장하지 않는다.

공식 초기 설정 참고:

1. `Edit > Plugins`에서 `Unreal MCP`, `All Toolsets`를 켠 다음 에디터를 재시작.
2. `Editor Preferences > Model Context Protocol`에서 로컬 서버 시작 설정 확인. 기본 접속 주소는 `http://127.0.0.1:8000/mcp`.
3. 에디터 콘솔에서 `ModelContextProtocol.GenerateClientConfig Codex`로 **Codex용 설정** 생성 시도.
4. 생성된 설정 위치 확인 후, **프로젝트 루트에서 Codex를 실행**. 기존 Codex TOML 설정이 존재하면 공식 명령이 덮어쓰지 않을 수 있으므로 내용 점검.
5. MCP 연결 후 `list_toolsets` → `describe_toolset` → `call_tool` 계열의 제공 도구를 검토. 실제 지원하는 Asset/Actor/PCG/Landscape/Blueprint/Material/Test 도구를 목록화.
6. MCP는 기본적으로 로컬 루프백 서버이며 인증 기능이 없다. **공개 인터넷에 포트를 노출하지 않는다.** 원격 Codex 실행 환경에서는 단순히 `localhost`를 입력하는 것으로 사용자 PC 에디터에 연결되지 않을 수 있으므로 네트워크 실행 위치를 먼저 검증한다.
7. MCP 툴 호출은 게임 스레드에서 직렬 실행되므로, 여러 구조 변경 호출을 무분별하게 병렬로 실행하지 말 것.
8. 필요한 기능이 도구 목록에 없으면 **Editor Python, Editor Utility, C++ Editor Module/Commandlet** 등 검증된 대체 경로를 설계한다. 에이전트가 편집기 GUI를 조작할 수 있다고 가정하지 않는다.

공식 가이드: https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor

---

## 3. 초기부터 8×8km: 월드 공간 규격과 확장 전략

### 3.1 고정 목표와 검토 항목

- 기본 지리적 월드 규모: **최소 8km × 8km급(X/Y), 단일 Persistent Level**.
- 건너갈 수 있는 산길과 분지, 높은 능선, 고원, 협곡을 포함하여 **지형의 고도 범위**를 적극 활용.
- 사용자 승인을 받기 전 임의로 `2×2km`로 축소 금지. 성능·도구 제한이 있다면 8×8km **거시 지형/전체 레이아웃을 먼저 구축**한 후 부분 구역을 세밀하게 완성.
- 해수면/강원/고산 정상 기준의 상대 고도를 정하고, 실제 높이맵 `Z Scale` 범위를 계산해 데이터 손실 없이 관리한다.
- 예시 설계 목표: 평야 ~0–250m, 중산지 ~250–700m, 고산지 ~700–1,200m 상대 고도. **절대 확정값이 아니며** 실제 렌더링, 가시성, 이동 동선, 수계 분석에 맞춰 재조정할 것.

### 3.2 Landscape 크기 검증 (중요)

Epic의 공식 권장 예시 `8129×8129` 버텍스, 1m XY 쿼드 스케일일 때 한 변은 **8128m(약 8.128km)**. `8129`가 정확히 8,129m를 뜻하는 것은 아니다. 공식 구성은 254×254 쿼드/컴포넌트, 32×32 컴포넌트, 총 1024 컴포넌트다. 이 구성은 상당히 무거울 수 있으므로 에디터/패키지/메모리에서 테스트한다.

- 단일 8129 Heightmap 구성과 타일형 Heightmap / Landscape Streaming Proxy 워크플로 중 **향후 외곽 확장**과 빌드 비용을 고려해 비교한다.
- 시작 시 8.128km를 전부 사용하더라도 월드 **최외곽 300~500m 정도는 향후 확장을 고려한 전이 지역**으로 기획할 수 있다. 이것은 지도 크기를 줄이자는 뜻이 아니라, 경계 디테일과 연결부를 보존한다는 뜻이다.
- 만약 요구사항이 '8×8km의 핵심 콘텐츠 공간 **바깥에** 300~500m의 추가 버퍼까지 포함'이라면 8129 단일 지형 구성만으로는 부족할 수 있다. **더 큰 타일형 영역을 별도 설계**해야 한다. 두 요구사항을 혼동하지 않는다.
- 경계 확장이 `Landscape Add` 또는 별도 인접 Landscape만으로 항상 매끄럽게 가능한 것으로 가정하지 않는다. **이음/고도/재질/충돌/PCG/Water/도로 검증용 샘플을 먼저 만들어 테스트**한다.
- `Landscape Z Scale`은 높이 범위와 정밀도의 균형을 갖춘다. 참고: Epic은 높이맵 원시 높이 범위를 대략 `-256`~`+256`에 Z Scale을 적용하는 방식으로 설명한다. 1,000m가 넘는 산맥을 충분히 표현하려면 실제 사용 고도 차이에 맞춰 Z Scale을 계산한다.

공식 가이드: https://dev.epicgames.com/documentation/unreal-engine/landscape-technical-guide-in-unreal-engine

### 3.3 경계 확장 계약 (World Edge Contract)

월드 가장자리마다 다음 데이터를 저장:

- Height / Slope / Curvature / Material Layer Weight 등 **지형 데이터 및 경계 접선/기울기**.
- 강 상류/하류가 향하는 방향, 유량 계층, 강폭·강바닥 높이, 호수 유입·유출, 향후 하천 연결 노드.
- 도로 연결 노드, 도로 등급, 예상 진행 방향 및 새 지역 도입 시 연결할 위치.
- 바이옴 가중치, 고도·강수·식생 분포 연속성.
- `Seed`, 버전, 생성 파라미터, 타일/지역 ID, 경계 좌표, 데이터 정밀도.
- 월드 외곽은 갑자기 같은 높이로 평탄해지거나 인공적인 수직 절단면으로 끝나지 않도록 한다.

**Lv.30 이후의 다음 권역과 이어질 수 있도록 초기 월드에 적어도 두 곳의 추후 확장 가능 도로 출구와 두 개 이상의 지형/수계 연결 후보를 남길 것.** 외곽과 접하는 하천은 미래 확장 방향과 상류/하류 관계를 기록한다.

### 3.4 큰 Z축을 실제 플레이에 사용

- 낮은 해안과 높은 산맥 사이의 **적극적인 고도 차이**를 허용하되, 도보 및 탈것의 등반 가능한 경로를 제공.
- 지형 경사 한계, 산길 폭, 회전반경, 헤어핀 커브, 교량/터널 접근부를 **캐릭터·탈것 이동 모델**과 연계.
- 예시: 주요 보행/탈것 경로에서 목표 종단경사 범위를 정의하고, 급한 구간은 지그재그 길·계단·교량·터널·대체 경로로 해결. **임의의 단일 경사값을 확정하지 말고 실제 CharacterMovement/탑승체 이동을 테스트한다.**
- 정상까지의 길이 있으면서, 별도로 사람이 접근하기 어려운 절벽·암봉도 둔다.
- 위에서 아래를 내려다볼 때 멀리 도시·숲·호수가 보이므로 가시거리와 HLOD 비용이 커진다는 점을 설계 초기에 고려한다.
- Landscape 높이필드로 표현할 수 없는 동굴/오버행/수직 암벽은 정적 메시/Nanite 클리프 키트 또는 **Experimental Mesh Terrain**을 제한적으로 시험. Mesh Terrain을 전체 월드의 유일한 기반으로 선정하려면 충돌·NavMesh·Water·PCG·WP·빌드시간까지 검증.

---

## 4. 독창적인 현실형 중세 판타지 시작 권역 레이아웃

### 4.1 월드의 큰 인과 구조

**산맥/분수계 → 강·하천 계통 → 충적 평야·호수·해안 → 도로·교역 경로 → 도시·마을 → 농업/채굴/어업 등의 생활 공간 → 수목과 지형 디테일** 순서로 설계한다.

각 요소를 서로 독립 랜덤 위치에 생성하지 않는다. 플레이어가 이동할 때 지리적 연결을 이해할 수 있어야 한다.

### 4.2 첫 번째 8×8km 시작 권역 초안 (확정 전 검토)

| 권역 | 지형 | 주요 정착지/장소 | 공간적 특징 |
|---|---|---|---|
| 북서 산맥 | 고산 암봉, 침식 계곡, 고개 | 광산·산악 요새 | 산길로 실제 등반 가능, 망루/전망대 |
| 북부 구릉·관문 | 긴 능선·침엽수림 | 국경 요새·관문 | 자연스러운 고도 변화, 굴곡진 진입로 |
| 북동 고원 | 고원·절벽·폐허 | 유적·소규모 취락 | 단차가 큰 지형과 위험 구역 |
| 중앙 분지·평야 | 완만한 언덕, 하천, 경작지 | 중심 성곽 도시 | 광장·시장·주거/상업 밀도 차이 |
| 서부 호수 | 산지 하류의 호수·주변 습지 | 호반 마을 | 호숫가 산책로·부두·농가 |
| 동부 삼림 | 혼합림·계곡·작은 개울 | 벌목촌·숲속 마을 | 울창한 캐노피·개간지·사냥길 |
| 남서 농경지 | 낮은 언덕·평야·지류 | 농촌 마을 | 다각형 농지, 목책, 풍차/방앗간 |
| 남부 하류 | 범람원·퇴적지·습지 | 강변 정착지 | 하천 합류·교량·습지 바이옴 |
| 남동 해안 | 곶·만·해안 절벽·해변 | 항구 도시 | 실제 해안 동선·부두·등대·진입 도로 |

**주의:** 표의 3×3 형태는 구역 목록일 뿐 **절대로 지형을 3×3 정사각형으로 자르지 않는다.** 권역 사이 경계는 고도·수분·노출·토양·인간의 이동로에 의해 점진적으로 바뀐다.

### 4.3 도시·마을 구체적 목표

- 메인 성곽 도시 1개(주요 성채, 광장, 시장, 성문, 주거/상업 구역, 외곽 작업장, 주변 농지).
- 중소 마을 5~8개를 기능과 지형 조건에 따라 배치(숲·농촌·광산·항구·호수·산악).
- 중형 랜드마크(요새, 고원 신전, 오래된 다리, 망루, 폐허 등)는 원경 실루엣에서 식별되도록 배치.
- 도시 규모·건물 수·에셋 다양성은 **보유한 에셋과 실제 성능 평가에 맞게** 확정한다. 임의로 수만 채를 복제하여 규모감을 해결하지 않는다.

---

## 5. 지형 생성: 반복 노이즈 언덕 금지

### 5.1 거시/중간/미세 스케일 분리

1. **Macro terrain (수백 m~수 km):** 산맥 축, 대규모 능선, 고원, 분지, 주 하천 유역, 해안.
2. **Meso terrain (수십~수백 m):** 큰 언덕, 하천 사행, 계곡 지류, 자연스러운 둔덕, 사면, 산악 도로.
3. **Micro terrain (수 cm~수십 m):** 암반의 경계, 물길 주변 자갈, 흙, 표면 재질, 뿌리·잔디·잔석. 큰 기복은 메시/스컬프/높이 데이터, 작은 디테일은 적합한 기법에 분담.

무작위 1~2종 노이즈 합으로 끝내지 말고 **산맥 방향성, 융기 지대, 분수계, 침식 계곡, 지역별 융기 패턴, 수계 연결성** 등을 반영한다. 실제 지형 형태를 사용하려면 Heightmap과 GIS/지형 제작 도구도 비교하되, 외부 데이터 권리·라이선스를 확인한다.

### 5.2 데이터 레이어

- `Height` 및 `Slope`, `Curvature`, `Aspect`(사면 방향), `FlowDirection`, `FlowAccumulation`.
- `Moisture`, `WaterDistance`, `Soil`, `BiomeWeights`, `ErosionMask`, `RockExposure`.
- `RoadReserve`, `SettlementReserve`, `NoSpawn`, `LandmarkPreserve`, `ExpansionBoundary`.
- 유효 범위, 단위, 좌표계, 해상도, 버전, 시드, 소유 모듈 및 캐시 방식 정의.
- 바이옴/도로/건물/물의 계산 결과가 동일한 기준 지형 데이터를 참조하도록 한다.

### 5.3 Landscape / Mesh Terrain 비교 결과를 반드시 기록

**안정적인 기본안:** Landscape + Landscape Edit Layers + Heightmap + 스플라인/패치 + Static Mesh Cliffs + PCG.

**실험 비교안:** UE 5.8 Mesh Terrain (MeshPartition 등), PCG Mesh Partition Interop.

평가 항목: 8km급 편집/메모리, 수직 암벽/오버행, 재질, 충돌, Navigation, Water, WP/HLOD, 생성 속도, 재생성, 자동화 방법, 저장/패키징, 엔진 업데이트 위험.

새로운 Mesh Terrain 기능이 실험 단계라는 사실을 문서에 명시하고, 초기 승인 없이 전체 프로젝트를 해당 기능에 종속시키지 않는다.

---

## 6. 하천·호수·바다: 자연스럽게 연결된 수계

### 6.1 수계 계획 선행

- `watershed`(유역)·`watershed divide`(분수계)·유출 방향·유량 누적량 추정.
- 상류→중류→하류의 수로 연속성, 지류 합류 및 배수 흐름.
- 산지의 좁고 급한 하천 → 분지의 완만한 굴곡 → 평야에서 넓어진 강 → 하구로 연결되는 **지형 변화에 따른 하천 계층**.
- 강바닥 높이와 수면 높이의 일관성 확인. 호수는 유입/유출 및 지형의 낮은 부분과 연결. 일부 내륙 호수는 다른 배수 구조도 가능하지만 특별한 설정 없이 무근거로 만들지 않는다.
- 강과 도로 교차점은 교량·여울·도하 지점을 명시적으로 설계.
- 경사/유량에 맞게 하천 폭, 깊이, 주변 자갈·둑·사면·식생 밀도 조정.

### 6.2 엔진 구현

- `Water Body River/Lake/Ocean` 및 Spline, Landscape Edit Layers 연동 가능성 검증.
- Water Body River의 스플라인 포인트별 폭·깊이·흐름 속도 속성을 확인하고 실제 방향이 전반적인 지형 하강과 일치하도록 구성.
- **Water System은 수면 표현/특성 제어 도구이며, 지형 형성의 전체 수문학을 자동으로 보장한다고 가정하지 않는다.** 유역·침식·강바닥은 자체 생성/분석 로직으로 먼저 설계한다.
- 물리/시각적 움직임(Flow Map, 노멀, 포말, 급류, 폭포 메시/효과), 물 경계, 수중 이동/부력 요구를 구분.
- 수면 셰이더/반사/투명 재질의 GPU 비용과 먼 거리 Water 렌더링 품질을 측정.
- 스트리밍 경계에서 하천 세그먼트/충돌/물 재질의 이음새가 보이지 않아야 한다.

**Water 공식 문서:** https://dev.epicgames.com/documentation/unreal-engine/water-body-actors-in-unreal-engine

---

## 7. 중세 도시·마을·농촌 자동생성 (도로/필지/랜드마크)

### 7.1 전역 입지 분석

정착지 후보 점수는 경사·하천 접근성·침수 위험·이동로 연결성·경작지/자원·방어에 유리한 지형 등을 반영. 유형마다 다른 가중치를 쓴다.

- **성곽 도시:** 교역로 교차, 하천과 평야 연결, 방어 가능한 고도와 도시 외곽 확장.
- **농촌:** 평탄한 토양, 수자원, 경작 면적, 큰 길과의 연결.
- **산악 광산촌:** 채굴지/곡지/사면의 안정적인 공간, 광석 운반 경로.
- **숲속 마을:** 수목 개간, 도로·벌목/가공 시설, 화재 완충지.
- **호숫가·어촌·항구:** 호안/해안 경사, 선착 가능한 지형, 내륙 연결로, 조류/파랑의 개연성.

### 7.2 도로 위계와 경로 비용

- 전역 교역로 → 주요 도로 → 마을 진입로 → 골목·농로·산길.
- 산악 도로는 구불구불한 스위치백, 고개, 완경사 통행구간을 포함. 급경사/절벽에서는 교량·터널·절토·우회안 비교.
- 예시 비용 함수:

  `C = α · Distance + β · SlopePenalty + γ · RiverCrossing + δ · TerrainModification + ε · Hazard + ζ · DesignConstraint`

- 이 함수는 수학적인 지표의 예시로 가중치가 확정된 것은 아니다. 경로를 만든 뒤 Spline 기반 월드 좌표 정합·충돌·도로폭·흙길 텍스처·절토 성토를 후처리한다.
- 도로는 산과 강에 맞추되, 산악 마을의 진입로가 무조건 최단 직선이 되면 안 된다.

### 7.3 정착지 내부 구조

- 전역 `SettlementPlan` → 유형별 `District` → 도로·블록·필지 → `BuildingArchetype` → 메시·소품.
- 성벽/성문/성채/대성당/시장/광장/주요 창고 등 **랜드마크는 선행 위치 제어 및 수동 편집**을 지원.
- 필지 크기와 형상은 도로폭/구역/경사에 따라 변동. 한 종류 주택의 위치만 랜덤화하지 않는다.
- 건축물은 용도·높이·폭·지붕·벽면 재질·지면과의 접합을 다양화. 기존 건축 팩의 모듈 호환성을 먼저 확인.
- 도로·건물·담장·전등/장식·농장/밭과 자연환경이 충돌하지 않도록 금지·완충 마스크 사용.
- 마을 기능을 드러내는 시설(광산 수레·선착장·경작지·풍차·목재 가공)이 물리적 공간과 연결되어야 한다.

### 7.4 PCG 구현 참조

UE 5.8 **City Sample PCG**는 현대 도시 미학을 가져오라는 뜻이 아니다. **18개 상호 의존 그래프와 실행 순서, 구역 분할, 곡선 도로, 도로 등급, 랜드마크 수동 배치** 등을 분석하여 중세 도시 생성에 필요한 개념만 적용한다. 공식 사례 자체는 교통 AI가 포함되지 않으므로 **도시 PCG와 MMO NPC/교통을 구별**할 것.

공식 문서: https://dev.epicgames.com/documentation/unreal-engine/city-sample-pcg-for-unreal-engine

---

## 8. 바이옴·식생·암석·표면 디테일

### 8.1 자연환경이 '텅 비어' 보이지 않게 할 것

기존 화면에서 확인된 문제: 드문드문 나무가 보이는 넓은 초지, 유사한 곡면 언덕, 마을과 환경의 경계가 무관한 배치.

개선 사항:

- 산맥: 노출 암반, 암설, 바람에 노출된 능선, 수목 한계의 변화.
- 울창한 숲: 높은 수목 밀도, 수관 다양성, 관목·낙엽·고사목·바닥 식생. 단, 플레이 경로와 성능에 맞춰 제어.
- 습한 계곡: 이끼·양치식물·강변 수목·젖은 토양.
- 중앙 평야: 자연 구릉, 밀밭·초지·토지 이용 패턴, 하천 주변 수목.
- 높은 고원: 개활지, 바위 군집, 경사별 초본 차이.
- 바닷가: 모래·자갈·풍식 지형·절벽·해안 식생.

### 8.2 배치 규칙

- `BiomeWeights`, 경사, 고도, 수분, 사면 방향, 계절·자연 환경과의 상호작용으로 식생 밀도 결정.
- 산림 군집은 한 종류 나무를 균일 산포하지 않고 패치 형태/군집 중심/수관 높이 차이로 설계.
- 나무·바위·풀 각각 밀도, 충돌, 이동 가능 통로, 먼 거리 표현, 성능 등급을 다르게 적용.
- 근접 디테일에 치중해 원거리 숲이 완전히 사라지는 문제 방지.
- 필수 인스턴싱/HISM/ISM/PCG 레벨 저장과 Nanite Foliage/PVE의 성능/시각적 이점을 A/B 비교.

### 8.3 UE5.8 Experimental 기능 평가

- **Nanite Foliage** — 고밀도 원경 식생·마이크로 인스턴싱/voxelized distant representation 등 원리 검토. 실제 에셋의 머티리얼/바람/원경 실루엣/VRAM/빌드 비용 검증.
- **Procedural Vegetation Editor (PVE)** — 직접 나무 종류를 제작하거나 기존 에셋을 변환하는 워크플로에 적합한지 평가. **Experimental 및 버전 호환성 문제** 존재.
- 기존 Foliage/HISM/LODs와 비교한 **GPU/CPU/메모리/팝인 벤치마크** 후 채택.

공식 가이드:
- https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage
- https://dev.epicgames.com/documentation/unreal-engine/procedural-vegetation-editor-pve-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/electric-dreams-environment-in-unreal-engine

---

## 9. 최중요: 고속 이동 시 건물·식생 팝인/스터터 방지

> '팝인이 0인 결과'를 미검증 상태로 보장하지 않는다. 플레이어에게 **확연한 팝인, 급작스러운 형태 변화, 빈 공간, 흐릿한 텍스처가 나타나는 상황을 정량·시각적으로 억제**하는 것을 목표로 한다.

### 9.1 문제를 구분해서 진단

1. **World Partition 셀 지연 로드:** 액터 자체가 늦게 생김.
2. **HLOD/LOD 전환:** 멀리 있는 실루엣이 갑자기 변화하거나 튀어나옴.
3. **식생 Cull 거리:** 나무/풀/바위가 특정 거리에서 갑자기 나타남.
4. **메시/텍스처 스트리밍:** 오브젝트는 존재하지만 해상도가 늦게 좋아지거나 메시가 늦게 나타남.
5. **CPU/GPU/Disk 히칭:** 빠른 이동 시 프레임 정지 또는 로딩 지연.
6. **셰이더/PSO 초기화:** 새로운 머티리얼/효과 진입 시 일시 정지.
7. **Nav/Collision 활성화 지연:** 건물/지형이 보이더라도 충돌이 준비되지 않음.

각 유형별 검증 방법/도구/원인/조치를 분리해서 보고.

### 9.2 스트리밍 범위를 속도와 로드 지연 기준으로 예측

최소 선행 거리의 설계 지표:

`D_prefetch >= V_max × (T_IO + T_create + T_safety) + D_visibility_margin`

- `V_max`: 스킬/탈것/낙하/고속이동 등 실제 최악의 속도(m/s).
- `T_IO`: 저장장치/월드/에셋 로딩 지연.
- `T_create`: 레벨/컴포넌트 활성화 및 GPU 데이터 준비.
- `T_safety`: 여유 시간(초), 로딩 지연 최악값을 반영.
- `D_visibility_margin`: 카메라 높이·시야·원경 랜드마크에 따라 필요한 추가 시야 거리.

**예시:** 40m/s에서 측정된 총 준비 지연이 3초라면 이동 방향으로 적어도 120m + 가시거리 여유를 고려해야 한다. 실제로는 높은 산 정상이나 넓은 평야에서 원거리 도시/숲이 보이므로 **120m만 로딩한다는 뜻이 전혀 아니다.** 원경 HLOD/식생/지형 표현이 따로 유지되어야 한다.

### 9.3 UE 실제 구현 비교

- 기본 World Partition Streaming Source(플레이어 중심)와 **이동 방향의 보조 Streaming Source / Shapes 기반 선행 로딩**을 비교. `Priority`, `Target State`, `Is Streaming Completed`와 런타임 로딩 범위를 활용하되 동적 생성/제거에 따른 비용도 측정.
- City/성곽/대형 건물은 **HLOD 계층과 항상 보이는 원경 실루엣**을 사용. 식생은 HLOD Instancing/Impostor/Billboard/LOD/Nanite Foliage 등 실제 지원되는 원경 방식을 비교.
- 멀리 있는 건물·숲·산이 가까워질 때 부드럽게 원거리 표현에서 근거리 디테일로 넘어가도록 HLOD layer, 전환 거리, 소스 메시 설정을 맞출 것.
- UE5.8 **Experimental Fast Geometry Streaming Plugin**을 비교 시험. 특히 정적이며 게임플레이와 무관한 대규모 지오메트리를 더 빠르게 읽는 목적이다. **동적 MMORPG Actor/Replication/Collision/Navigation을 자동 해결하는 기술로 오해하지 말 것.**
- 텍스처 스트리밍 풀/Virtual Texturing/RVT/Nanite data streaming/재질 컴파일/PSO precaching 등과 함께 테스트.
- 스킬 이펙트·탈것 속도 자체와 스트리밍 성능의 충돌을 검사.
- 모든 액터를 Always Loaded로 만드는 방식은 사용하지 않는다.

공식 가이드:
- World Partition: https://dev.epicgames.com/documentation/unreal-engine/world-partition-in-unreal-engine
- World Partition HLOD: https://dev.epicgames.com/documentation/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine
- UE 5.8 공식 발표(새 스트리밍 기능): https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available

### 9.4 재현 가능한 이동 테스트

**최초 제안용 시나리오(실제 속도 확인 후 수정):** 10, 25, 40, 60, 80m/s, 카메라 높이 2m/5m/산악 전망대, 도심 진입·숲 진입·산 정상 하강·강 횡단·항구 도착·180도 회전·순간 가속·빠른 이동 후 급정지.

- **속도별 실제 시간 축의 프레임타임 그래프 및 스트리밍 로그** 확보.
- 셀 로딩 상태, HLOD 교체 전후, 수목 시각화, 텍스처 미립한 구간, 도시 실루엣 변화를 녹화/연속 캡처.
- *사용자 눈에 분명히 보이는* 주요 건물·나무 군집 팝인과 무너진 원경은 불합격. 허용 가능한 미세 LOD 변화/풀의 가까운 범위 처리는 가시성 평가로 결정.
- 숫자 합격 기준(허용 hitch 횟수, 전환 시 눈에 띄는 변화, VRAM 상한, FPS)은 실측 후 사용자 동의하에 확정.

---

## 10. 성능·메모리·대형 빌드 계획

### 10.1 기본 성능 예산

- **초기 후보 목표:** 대표 타깃 PC에서 60FPS (프레임당 약 16.67ms). 이는 품질 목표이며 **현 하드웨어에서 달성했다고 주장하지 않는다**.
- GPU 프레임시간, Game Thread, Render Thread, RHI, Nanite/Lumen/VSM/Foliage, HLOD 생성 및 streaming 비용 따로 측정.
- VRAM/RAM/SSD IO, 에디터 부팅/레벨 부분 로드/PCG 재생성/World Partition HLOD 생성/쿠킹 시간도 측정.
- 최고 그래픽 옵션만이 아니라 scalable Low/Medium/High/Ultra 프로필의 권장 범위를 설계.
- 월드 경계와 높은 전망대에서의 최악 가시거리, 밀집 도시, 고밀도 숲, 빠른 탈것 이동을 **worst-case**에 포함.
- AI/NPC·스킬·멀티플레이 오브젝트가 추가되면 성능 예산이 줄어드므로 레벨 렌더링만으로 예산 전체를 소모하지 않도록 여유 확보.

### 10.2 필수 최적화 후보

- WP Streaming / HLOD / Data Layers / OFPA / Level Instances.
- 대량 식생 인스턴싱(HISM/ISM)과 과도한 컴포넌트/Draw Call 회피.
- Nanite의 실제 비용 및 재질/마스킹/바람 부하 분석.
- 거리별 머티리얼/지형 셰이더 복잡도, 텍스처 스트리밍 및 RVT 적용 검증.
- `Virtual Shadow Maps`, `Lumen`, AO/Volumetric Cloud/Fog, Water의 원경 가시성과 GPU 비용 균형.
- Editor에서 저장/생성하는 **Baked PCG 정적 콘텐츠** 우선; 매 프레임 런타임 전체 월드 생성 금지.
- 필요시 커스텀 Editor Builder/Commandlet를 이용해 청크별 생성·HLOD/맵 검사 자동화.
- 타깃 PC의 CPU/RAM/SSD/VRAM 상황에 맞춰 병렬 작업과 로딩/메모리 예산 관리.

### 10.3 프로파일링 출력

- 대표 구역별 `Unreal Insights` 트레이스, `stat unit`, `stat gpu`, WP 스트리밍 디버그, 메모리/VRAM, HLOD/PCG 로그를 활용.
- 에디터 PIE 결과 **외에** 패키징된 Development 빌드에서 스트리밍 테스트.
- 60FPS 달성 여부, 1% low 또는 프레임타임 분포, 히치 발생 지점, 로딩 실패, 메모리 상한 및 개선 내역 기록.

---

## 11. C++ + PCG + Editor 도구 설계

### 11.1 제안 아키텍처 (기존 시스템과 충돌 방지)

예시 논리 단위 — 아래 이름을 그대로 새 클래스로 대량 생성하지 말고 기존 코드와 비교한 뒤 이름/모듈을 확정:

```text
WorldAuthoring (Editor-oriented)
  WorldRecipe / GenerationSeed / RegionRegistry
  TerrainAuthoring / TerrainAnalysis / HydrologyPlanner
  BiomePlanner / RoadNetworkPlanner / SettlementPlanner
  LandmarkRegistry / ExpansionBoundaryContract
  PCGExecutionCoordinator / BuildValidation / AssetCatalog

WorldContent (Data Assets)
  DA_WorldProfile / DA_RegionProfile / DA_BiomeProfile
  DA_RoadStyle / DA_SettlementArchetype / DA_BuildingPalette
  DA_StreamingProfile / DA_AssetPalette

Generated Outputs (versioned)
  Heightmaps / Masks / RiverGraph / RoadGraph
  SettlementLayouts / RegionBoundaries / BiomeWeights
  PCG Local Outputs / HLOD / ValidationArtifacts

Runtime
  World Partition + Static World Content
  Gameplay Actor Layer (NPC, monsters, players, interactables)
  Network/Replication and navigation handled separately
```

### 11.2 데이터/종속성 계약

- 월드 전체 변형 `Seed`와 권역 단위 `Subseed`를 분리하고, 지역 하나의 변경으로 다른 지역이 무작위로 바뀌지 않게 한다.
- `Inputs→Outputs`(Height→Hydrology→Road→Settlement→Biome→PCG) 데이터 종속성을 문서화.
- 정착지/도로/수계는 전역 계획을 공유하며, PCG는 이를 읽어 **지역 단위 에셋 배치**를 담당.
- 각 생성 결과에 소스 버전/타임스탬프/Seed/권역 ID/환경 설정 및 hash를 기록.
- 에셋 삭제·리네임 시 참조 무결성 검증, 수동 Override 보존.
- 주 도로·랜드마크는 디자이너 Override가 우선하도록 데이터와 그래프 설계.

### 11.3 PCG 그래프는 역할별 분리

예시:

```text
PCG_Forest_Canopy / PCG_Forest_Understory / PCG_Grass
PCG_Rock_Cliff / PCG_Rock_Talus / PCG_Riverbank
PCG_Roadside / PCG_Farm / PCG_Field
PCG_SettlementResidential / PCG_SettlementCommercial
PCG_Harbor / PCG_MiningVillage / PCG_WorldDetails
```

- Hierarchical/Partitioned Generation 설정을 가시 거리, 업데이트 비용, 콘텐츠 등급에 맞춰 조정.
- 런타임 생성이 필요한 개별 요소와 에디터에서 베이크해야 할 대규모 정적 요소 구분.
- PCG Graph 컴파일/실행 성공뿐 아니라 결과 메시 경로, 배치 수, 충돌, 재생성 결과, 에디터 재시작 후 저장 상태 검증.

---

## 12. MMORPG용 별도 아키텍처 체크리스트

- World Partition은 **월드 콘텐츠 로딩 시스템**이지 MMORPG 전용 네트워크 서버 구현이 아님.
- Dedicated Server에서 필요한 충돌·게임플레이 상태·AI 활성화와 클라이언트의 원거리 시각 데이터를 분리.
- 동적 몬스터·NPC·채집/상호작용 오브젝트는 서버 권한과 Relevancy/Replication 전략을 별도로 계획.
- 멀티플레이 규모 확장은 Replication Graph/Iris/Net relevancy 등 프로젝트 채택 기술에 따라 개별 검증.
- 큰 산악 지형의 Navigation Mesh, 스트리밍 경계의 경로 유지, AI spawn/Despawn, 세이브/지속 상태를 시험. 실험적 WP Navigation의 안정성을 먼저 검증.
- 순간이동, 탈것 탑승, 위치 동기화, 낙하, 경사 이동, 이동 속도 높은 스킬, 지면 충돌 지연을 포함하여 검증.
- 월드의 8km 크기가 곧 대규모 동시 접속 인원을 보장하는 것은 아님.

---

## 13. 실제 공개 사례 분석 — 무엇을 재사용할 것인가

### 사례 A. The Witcher 4 UE5 기술 데모 (CDPR × Epic, 2025)

**확인된 사실:** UE5 기술 데모가 PlayStation 5에서 60FPS로 시연되었으며, Fast Geometry Streaming과 나중에 UE5.8 문서로 이어지는 Nanite Foliage 관련 기술이 소개되었다. 이는 **정식 게임 전체의 성능 수치가 아니고 기술 데모 결과**이다.

**이번 프로젝트로 가져올 원칙:**
- 탈것처럼 연속 고속 이동이 있는 월드에서는 리소스 스트리밍이 디자인 단계의 1급 요구사항.
- 높은 전망대·산악 원경·도시·숲을 동시에 보이는 상황을 기준으로 원거리 표현 전략 설계.
- 정적 지오메트리의 선행 로딩/계층적 표현, 가시성 유지, 프레임타임 측정을 같이 검증.

자료: https://www.youtube.com/watch?v=aorRfK478RE
공식 UE 5.8 설명: https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available

### 사례 B. City Sample PCG (Epic)

**확인된 사실:** 메인 데모에서 18개의 상호 의존적인 PCG 그래프를 특정 순서로 실행하고, 곡선 도로와 3개 도로 계층, 다양한 구역, 수동 랜드마크를 결합. 본 사례는 현대 도시 디자인이므로 건물·미학을 복제하면 안 된다.

**이번 프로젝트로 가져올 원칙:**
- 전역 데이터/그래프 의존성을 명시.
- 도로 위계 → 필지/구역 → 건축물 생성 순서.
- 중심 랜드마크 수동 아트 디렉션.

자료: https://dev.epicgames.com/documentation/unreal-engine/city-sample-pcg-for-unreal-engine

### 사례 C. Electric Dreams Environment (Epic)

**확인된 사실:** UE의 PCG, 전통적인 수동 레벨 아트, 조명/식생을 조합한 시연 환경. 문서에서는 4×4km World Partition 샘플 레벨을 소개하되 **해당 샘플 레벨은 스트리밍 비활성화 상태**라고 설명한다. 따라서 이 사례를 최적화된 MMORPG 스트리밍 성공 사례로 포장하면 안 된다.

**이번 프로젝트로 가져올 원칙:**
- 밀도 높은 자연환경의 다층 구조(수관·관목·낙엽·암석·물가).
- PCG에서 모든 구도를 생성하지 않고 아트 디렉션과 결합.
- 지표면·바위·나무·물의 접합 디테일.

자료: https://dev.epicgames.com/documentation/en-us/unreal-engine/electric-dreams-environment-in-unreal-engine

### 사례 D. Unreal 5.8 Mesh Terrain / PVE / Nanite Foliage / MCP

**확인된 사실:** 5.8은 메시 기반 지형 편집, 고급 식생 제작, PCG 수동 Override, Experimental MCP를 소개. 개발상 **실험 단계 기술**이 포함된다.

**이번 프로젝트로 가져올 원칙:**
- Landscape와 Mesh Terrain 중 안정성·성능·미래 버전 호환성을 비교.
- 특정 절벽/암반의 고난도 지오메트리는 국소적으로 3D 메시/실험 도구를 사용할지 판단.
- MCP 연결은 도구 노출/실제 호출/결과 검증이 확인된 경우에만 인정.

자료:
- https://dev.epicgames.com/documentation/unreal-engine/mesh-terrain-in-unreal-engine
- https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage
- https://dev.epicgames.com/documentation/unreal-engine/procedural-vegetation-editor-pve-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor

**연구적 한계:** 공개된 AAA급 오픈월드 제작 사례와 UE 5.8 MCP 기능 설명은 존재하지만, 'Codex+기본 Unreal MCP만으로 검은사막 수준의 8×8km 게임을 자동 완성했다'는 검증된 사례는 확인되지 않는다. **MCP는 제작 자동화 수단이지 아트 품질의 증명 수단이 아니다.**

---

## 14. 디테일 레퍼런스(이미지/공식 가이드) — 아트 방향성

아래 링크는 **참고/분석용**이다. 원본 에셋·이미지의 재사용 권리가 자동 부여되는 것은 아니다.

| 참고 주제 | 확인해야 할 세부 요소 | 원본 링크 |
|---|---|---|
| 산맥 원경·고속 승마 | 먼 산, 협곡, 숲의 실루엣, 높은 위치에서 조망 | [The Witcher 4 UE 기술 데모](https://www.youtube.com/watch?v=aorRfK478RE) |
| 깊고 울창한 자연환경 | 수목 캐노피, 암석·침식된 지면, 숲 바닥 디테일 | [Electric Dreams 환경](https://www.unrealengine.com/en-US/electric-dreams-environment) |
| PCG 숲·계곡 노드 | 수동-절차적 배치 결합, 군집 디테일 | [Electric Dreams PCG 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/electric-dreams-environment-in-unreal-engine) |
| 불규칙 도시·도로·지구 | 도로 계층, 특수 랜드마크, 구역 변형 | [City Sample PCG](https://dev.epicgames.com/documentation/unreal-engine/city-sample-pcg-for-unreal-engine) |
| 산악 절벽/오버행 | 일반 지형과 메시 지형의 차이 | [Mesh Terrain](https://dev.epicgames.com/documentation/unreal-engine/mesh-terrain-in-unreal-engine) |
| 멀리 보이는 숲 | 원경 식생 실루엣, dense canopy, 비용 | [Nanite Foliage](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage) |
| 하천 형태·폭·수심·흐름 | 강바닥, 굴곡, 합류점 및 주변 지형 | [Water Body Actors](https://dev.epicgames.com/documentation/unreal-engine/water-body-actors-in-unreal-engine) |
| 팝인 개선 전후 | HLOD off/on 비교, 먼 거리 도시/절벽 | [World Partition HLOD](https://dev.epicgames.com/documentation/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine) |

### 14.1 바로 열어볼 수 있는 시각 레퍼런스

아래 이미지들은 공식 샘플/창작자/게임 관련 공개 페이지의 레퍼런스다. **제작용 에셋 제공이 아니며**, 이미지 파일의 복사·재배포·모델 학습이나 상업적 재사용 권리를 뜻하지 않는다. 이미지 직링크는 변경될 수 있으므로 **원본 페이지도 함께 제공한다.**

1. **고산에서 먼 성곽과 숲 조망:** [Witcher 4 기술 데모 영상](https://www.youtube.com/watch?v=aorRfK478RE), [관련 이미지 및 해설](https://www.pcgamer.com/games/the-witcher/we-just-got-a-look-at-someone-actually-playing-the-witcher-4-as-cdpr-promises-the-most-immersive-and-ambitious-open-world-witcher-game-ever/). 확인: 나무/암벽/먼 도시가 가시성 계층을 어떻게 형성하는가.
2. **고도 높은 숲속 계곡:** [Epic Electric Dreams 공식 페이지](https://www.unrealengine.com/en-US/electric-dreams-environment), [이미지 보기](https://cdn2.unrealengine.com/electric-dreams-environment-header-1920x1080-4ac94506c2bf.png?resize=1&w=1920). 확인: 자갈, 암석, 수목과 계곡 바닥의 접촉.
3. **산·호수·원경 수목 밀도:** [Northern Valley UE5 환경 사례](https://80.lv/articles/take-a-look-at-this-peaceful-northern-valley-ue5-scene). 확인: 호수 연안의 식생, 고산 시야, 안개에 의한 깊이.
4. **가파른 산길과 지그재그 접근:** [Mountain Pass UE5 레벨디자인 사례](https://dillonben.net/projects/level-design-1-mountain-pass/), [Unreal 산길 아트 제작 사례](https://80.lv/articles/how-to-build-an-atmospheric-environment-using-zbrush-unreal-engine-substance-3d). 확인: 경사 한계와 실제 이동 동선 및 디자이너 수작업의 필요성.
5. **중세 해안 마을 건축 밀도:** [Fab Medieval Seaside Village](https://www.fab.com/listings/3dda7842-9e6e-4d6c-98f3-723156071545). 확인: 수변 주거·부두·소품의 기능 연결. Fab 페이지의 가격/라이선스/엔진 버전은 사용 시 재검증.
6. **실제 게임형 항구 실루엣:** [Avowed 항구 이미지 사례](https://www.pcgamer.com/hardware/graphics-cards/stutters-and-hitches-in-unreal-engine-5-games-should-become-rarer-over-time-due-to-epics-continuous-updates-but-well-still-see-them-for-a-while-because-of-the-way-games-are-made/). 확인: 원경 배·선착장·지붕군의 실루엣. 게임/기사 이미지의 에셋 복제는 금지.
7. **실제 고해상도 산악 월드빌딩 연구:** [Arid Mountain Pass — Unreal Environment](https://sipp_studio.artstation.com/projects/x3nr3O). 작가 설명에 8K 하이트맵·실제 DTM·메시/머티리얼 최적화 작업이 기재되어 있으며, 이를 AAA 상용 게임 품질 인증으로 혼동하지 않는다.

**권장 스크린샷 세트 (Codex 검증 출력):**
1. 월드 전체 Orthographic / 고도·바이옴·하천 지도.
2. 산 정상에서 중앙 도시를 내려다본 3인칭 시점.
3. 큰 산길과 급커브, 고개 및 전망대.
4. 강 상류 → 중류 → 하구 3장 비교.
5. 중앙 성곽 도시 상공/성문 진입/골목/광장 4장.
6. 숲의 원경/중거리/근거리 3장.
7. 탈것 이동 중 동일 프레임 위치에서 HLOD 전환 전후.
8. 월드 외곽 확장 지점의 경계 고도/길/하천 연결 상태.
9. 성능 오버레이/Unreal Insights/메모리 측정 결과.

아트 스타일은 '실사에 가까운 자연환경과 중세 판타지 건축'으로 통일하고, 사용자에게 건물 팩·숲 팩·지형 팩의 아트 스타일 충돌 사례도 보고한다.

---

## 14A. 사냥터 9곳 — 지형·전투·진행 동선이 연결된 오픈월드 설계

> **본 섹션은 신규 요구사항이다.** 최초 8×8km 시작 권역의 지형 골격부터 모든 사냥터의 실제 위치 후보를 기획하고, 각 권역의 전투 공간/입구/시야·고도·AI 구동 조건을 정의한다. 단조롭게 원형 평지에 몬스터 수십 마리를 뿌리지 말 것.
>
> 아래 T1~T5와 일부 과거 지역명은 이전 문서의 **상대 난이도 참고용**이다. **실제 콘텐츠 구간과 지역 명칭은 바로 아래 v3 레벨 표 및 `ZONE_LAYOUT_LV1_30.json`을 우선한다.** 사용자의 기존 레벨/능력치 시스템을 먼저 조사하고 확정한다.

### v3 레벨 구간 배치 우선표 — **아래 T1~T5보다 우선**

다음 레벨은 **첫 8×8km 시작 권역 Lv.1~30의 콘텐츠 분포 예시**다. 기존 최대 레벨·적 능력치가 정의돼 있다면 사용자 승인 후 실제 시스템에 맞게 데이터로 조정한다. 모든 사냥터를 '한 번 방문하고 끝나는 일회성 지역'으로 만들지 말고, 재방문·퀘스트·탐험 경로를 확보한다.

| 사냥터 | 권장 레벨 | 지형/목표 |
|---|---|---|
| H01 초원·완만한 구릉 | 1~5 | 초급 전투, 도시 외곽 접근 용이 |
| H02 농지 외곽 소굴 | 4~9 | 농장/목책/야수·도적 |
| H03 동부 숲 가장자리 | 7~12 | 벌목길·부분 숲·유적 전조 |
| H04 서부 호안 | 10~15 | 호수변 지형·수면과 평지 충돌 검사 |
| H05 깊은 벌목림·숲 유적 | 12~18 | 울창한 숲 속 개활지와 다양한 엄폐 |
| H06 남부 하류·습지 | 15~21 | 수로와 얕은 물, 통행로 분리 |
| H07 해안 절벽·밀수꾼 | 18~24 | 낙하 위험·길목·원거리 시야 |
| H08 북서 폐광·산길 | 22~27 | 광산 입구·암반·Z축 층위 |
| H09 북동 유적·고원 | 25~30 | 정예 위협, 고지대와 유적 |

**필드 보스:** B01 암릉의 수호자(Lv.25~30 도전), B02 고목의 파수자(Lv.28~30 도전). 둘 다 스토리 필수 통행로를 막지 않고, 안전한 우회 경로를 유지한다. 한 번에 둘 다 전투까지 완성했다고 주장하지 말고, 에셋·전투·멀티플레이 검증 순서에 따라 기능별 완성도를 보고한다.

### 14A.1 실제 배치 자료

`ZONE_LAYOUT_LV1_30.json`을 읽어 사냥터 9곳·필드보스 2곳·정착지와 확장 출구 후보를 배치 계획으로 도출한다. 좌표는 남서쪽이 `(0,0)`, 동쪽이 +X, 북쪽이 +Y인 **기획용 km 단위 상대 좌표**이며 실제 Unreal 좌표와 다르다. 지형 적합도·수계·동선·기존 에셋 및 성능 제약을 평가하여 최종 위치를 확정한다.

- 사냥터 주변에는 일반적인 마을 길·완충지·비전투 풍경 공간을 남긴다. Lv.25~30 권역이 Lv.1 스타트 마을 바로 옆에 배치되지 않도록 접근 경로를 검증한다.
- `Lv.1~30`은 권장 성장 콘텐츠이며 **탐험 가능 공간의 전면적인 하드 레벨 제한**을 의미하지 않는다.
- 북부 산지와 동부 숲의 보스·사냥터는 선택적 고난도 지역으로 연결하지만, 후속 Lv.31+ 월드 접근 동선을 영구적으로 봉쇄하지 않는다.

### 14A.2 사냥터 공통 레벨 디자인 계약

사냥터마다 다음 정보를 **Data Asset 및 시각 디버그 오버레이**로 제공할 것:

1. `RegionID`, `HuntingGroundID`, 기획용 Tier, 실제 Level/Difficulty Curve, 생태/진영, 주간/야간 변화(선택), 지역별 원래 에셋 목록.
2. `EncounterPolygon`(비정형 다각형, Landscape/Z 고려), 선호 고도·경사·Nav Area·수변 거리, 지형 위험(Hazard) 마스크, `NoSpawn` 안전구역.
3. `EntrancePaths`, 주요 도로와 연결된 입구 1~3개, 집결/후퇴 위치, 탈것 접근 위치, 고난도 구역으로의 자연스러운 경계.
4. `SpawnClusters`, 무리 규모/반경/패트롤 영역, 스폰 유효성 Raycast·NavProject·시야·플레이어 안전 거리 검사. **스폰 좌표를 고정 시드로 사전 계산할 수 있지만 살아있는 몬스터 Actor는 PCG 인스턴스가 아니다.**
5. 근접/원거리/엘리트 몬스터의 역할 조합, 지형과 연계된 행동(EQS/Behavior Tree·StateTree 중 기존 프로젝트에 맞는 것), 공격 범위와 장애물 예외처리.
6. `SpawnBudget`: 월드 전체 총량, 권역별 시뮬레이션 예산, 관심 플레이어 수, 동적 밀도 상한. 값은 서버 벤치마크 이후 확정.
7. `Loot/XP/Respawn`은 기존 게임 시스템과 호환해 데이터로 구성. 수치·경제·드롭률은 독단적으로 결정하지 않을 것.
8. `VisualIdentity`: 땅 재질, 숲 밀도, 캠프 구조물, 조명/날씨 분위기, 원경 랜드마크, 소음/사운드. 사용자 제공 이미지와 기존 에셋 스타일 우선.
9. `Debug`: 스폰 위치/NoSpawn/AI Nav/Combat Space/Tier Boundary 가시화; 편집기 스냅샷과 패키징 플레이 검증.

### 14A.3 MMORPG 사냥터에서 빠지기 쉬운 문제

- **리젠 캠핑/혼잡:** 다수 플레이어가 동일 지점에 몰릴 때 몬스터 중복·공간 충돌·킬 크레딧·스폰 대기 병목 발생 여부를 확인. 실제 정책은 사용자 승인.
- **광역기와 몰이:** AOE 반경, 탱킹/어그로, 길막, 과도한 몬스터 끌기, Nav 경계 위로 몬스터가 따라오는 현상, 리셋/귀환(Leash)을 설계.
- **지형 치트/AI 고립:** 벽 너머 공격, 절벽에 걸림, 다리/계단/좁은 길의 충돌, 높은 위치에서의 몬스터 경로 상실을 테스트.
- **스폰과 스트리밍 분리:** 플레이어가 멀어지면 클라이언트 시각 Actor는 내려도, 서버에서 유지할 상태/전투/처치 기록과 월드 지속 스폰 정책은 독립 관리. 필요 시 근처 플레이어가 없는 사냥터의 AI Full Tick을 감축하되 서버 정합성 유지.
- **멀티플레이 렌더링:** 사냥터 식생 + 많은 몬스터 + 광역 스킬 이펙트 + 탈것 이동을 합친 프레임타임/네트워크/애니메이션 예산 실측.
- **주변 마을 안전:** 플레이어 사망 복귀/안전지대/대도시 정문이 몬스터 스폰/보스 공격 범위에 들어가지 않도록 별도 `SafetyMask` 확보.
- **세계의 납득 가능성:** 늑대·산악 야수는 고지대, 도적은 도로와 폐허, 수생 생물은 습지·호안, 고대 수호자는 유적 주변이라는 식의 생성 조건을 데이터화. 모든 적을 같은 평지에 배치하지 않는다.
- **전투 접근성:** 시야 차단/광원/색약 배려/텔레그래프 대비, 카메라 충돌, 근접·원거리 직업의 회피 공간 등을 고려.

---

## 14B. 필드 보스 1–2종 — 넓은 오픈월드 지형과 네트워크 시스템의 결합

### 14B.1 필드 보스 배치

| ID | 기획상 명칭·위치 | 전투 배경 | 주요 전투 연출 | 접근성·원경 |
|---|---|---|---|---|
| **B01** | **암릉의 수호자**, 북서 산악지대 고개 위 오래된 망루터 | 능선에 둘러싸인 암반 테라스, 무너진 성벽, 주변 큰 절벽, 산길 | 거대한 암석 거인의 내려찍기, 지면 균열, 선행 경고가 있는 낙석, 위치 이동 유도 | 산 중턱의 굽은 등반로 → 집결 전망대 → 전투 테라스. 멀리서도 암봉 실루엣으로 위치 파악. 근처 H07과 분리 |
| **B02** | **고목의 파수자**, 동부 고대 숲 유적의 침수되지 않은 광장 | 거목·석조 제단·나무뿌리·얕은 하천이 인접한 유적, 명확한 근접 전투 바닥 | 오래된 숲 수호자의 뿌리 공격, 지면 장판, 덩굴의 영역 분할, 시야 차단 구간 | 숲속 도로 → 유적 입구 → 안전 집결 공간 → 보스 광장. 인접 H08 정예 사냥터와 `NoSpawn` 분리 |

**구현 우선순위:** B01 또는 B02 중 기존 프로젝트의 모델·애니메이션·스킬·AI 기반이 더 풍부한 **1종을 실제 동작하는 MVP**로 먼저 구현하고, 재사용 가능한 보스 프레임워크가 검증되면 둘째 보스를 추가한다. 사용자가 희망하는 최종 수는 1~2종이며, 둘째가 미완성이면 완료했다고 보고하지 말 것.

### 14B.2 전투 공간 요구사항

- 거대한 보스 주변에 **실제로 이동·회피가 가능한 유효 면적**이 필요하다. 초기 후보로 약 120~220m 지름을 비교하되, 적중 판정 크기·시야·파티 인원·카메라 줌·스킬 이동 거리·평균 지형 경사에 따라 실제 값을 조정한다. **이 수치는 공식 규격이나 확정 요구가 아니다.**
- 완전한 원형 평지로 만들지 말 것. 전투 가능한 완만한 기복·낮은 엄폐물·외곽 단차·동선의 변화를 주되, 전투 중심부에서는 카메라 시야와 장애물/타격 판정이 불리해지지 않게 설계.
- 접근로 최소 2개 후보, 집결지와 부활/회복 동선, 비전투 관람/원경 구역, 낙하 위험 경고, 탈것 주차/해제 정책, 퀘스트·서버 이벤트 진입 연계.
- 사냥터 몬스터가 보스 전투 공간에 과도하게 유입되지 않도록 경계/AI Leash 설정. PvP/PK 가능 여부는 사용자의 게임 규칙 확인 후 결정.
- 서버 권한으로 Boss State Machine(`Dormant → Announcement → Spawned → Engaged → Phases → Defeated/Reset → Cooldown`)을 관리. **스폰 시간·보상·부활 정책은 데이터로 분리하고 사용자 승인 전 확정하지 않는다.**
- 보스 HP/페이즈/타깃/CC면역/어그로/전투 기록/기여도/루팅 권한은 신뢰 가능한 서버 상태에 둔다. 클라이언트는 VFX, 경고 텔레그래프, 애니메이션·오디오 위주로 재생.
- 다수 플레이어 참여 시 Replication relevancy, 능력/이펙트 전송량, 주변 원경 HLOD, SkeletalMesh/AnimBP Tick, Gameplay Cue 비용을 측정. 대형 월드가 자동으로 대규모 보스 전투를 처리해주지 않음.
- 보스 주변 월드가 멀리서도 보이는 건축/숲/암석은 정적 원경 콘텐츠로 미리 준비하되 **동적 보스 자체는 플레이어 거리/이벤트 상태와 서버 로직에 따라 적절히 생성 및 동기화**.
- 안전 종료/리셋/월드 재시작/로그아웃/참여자 전원 이탈/동시 처치/이중 스폰/장거리 어그로 등 예외 사례에 대한 테스트 시나리오 작성.

### 14B.3 전투 시스템 기술 결정 원칙

- 기존 프로젝트에 **Gameplay Ability System(GAS)**, 공격/피격·스탯·죽음 이벤트가 있다면 먼저 해당 구조를 사용하고, 없을 때만 새 보스 컴포넌트/AbilitySystem 도입의 비용과 이득을 비교.
- 상태 관리에 Behavior Tree + Blackboard, StateTree, C++ 상태 머신 등을 실제 기존 코드/플러그인 사정에 맞춰 선택하고, 복잡한 의사결정이 필요한 부분만 EQS·Smart Object 검토.
- Area of Effect는 '보스가 크니까 수많은 Tick Actor를 스폰'하는 형태를 피하고, 단순 VFX와 실제 서버 히트 판정을 분리.
- 보스 다형성: `BossProfile DataAsset`에 ArenaID, Ability Set, Spawn/Respawn Rule, Difficulty, Loot Table 참조, VFX/SFX/Animation 참조를 넣는 구조를 검토. 가공의 에셋 경로를 생성했다고 주장하지 않는다.
- 오브젝트/보스가 별도로 레벨 단위로 존재하더라도 **하나의 플레이 가능 Persistent World** 내의 콘텐츠로 취급한다. 월드 전체 별도 Loading Screen 전환을 기본안으로 설계하지 않는다.

### 14B.4 필드 보스 시각 검증 제출물

1. 월드 전체 지도에 B01/B02의 실제 UE 좌표와 주변 도로, 사냥터 경계, 마을 안전거리를 표시한 이미지.
2. 보스 등장 시점의 **3인칭 플레이어 시점**, 머리 위/측면을 포함한 보스 실루엣과 텔레그래프 시인성 캡처.
3. 최소 2개 진입로와 대형 전투공간을 보여주는 탑뷰·측면 캡처.
4. 기본 페이즈, 이동·공격·피격·리셋 동작의 영상 또는 구체적 자동화 로그.
5. 1인→복수 클라이언트→상정 동시 인원 증가 실험별 FPS/프레임타임/서버 틱/네트워크 대역폭/AI Tick 측정표.
6. 에셋이 없는 동안 회색박스만 구현했다면 명확히 `GREYBOX / GAMEPLAY UNVERIFIED` 표기.

---

## 14C. 게임플레이 데이터·C++ 및 AI 생성 파이프라인

### 14C.1 '생성되는 정적 세계'와 '살아 있는 월드 게임플레이' 분리

```text
월드 아서링 / 에디터 단계
  Landscape / Hydrology / Biome / Road / Settlements
    → HuntingGround Polygon + BossArena Polygon + SafeZone Mask + Nav Areas
    → PCG로 식생·바위·폐허·캠프 등 정적 환경 배치
    → SpawnCandidates/PatrolRoutes/EncounterProfile 데이터 검증 및 저장
    → WP/HLOD, 충돌, NavData 빌드 및 시각 검증

런타임 / 서버 권한
  RegionActivity / PlayerInterest
    → Encounter / Spawn Director (몬스터 수·군집·리젠·동적 난이도 정책)
    → AI Controllers, Navigation, Ability/Combat/Threat/Death/Loot
    → Boss Event Director (스폰 조건·페이즈·리셋·결과)
    → Replication / Relevancy / Client VFX
```

- 에디터 생성된 정적 `SpawnCandidate` / `PatrolRoute` 데이터와 서버에서 존재하는 실제 `Monster` Actor를 별도로 관리한다.
- 기존 C++ 클래스가 있다면 리네임/중복을 피하고, 확장 가능한 인터페이스/데이터 테이블을 우선 정의한다.
- `UWorldSubsystem`과 `GameState`/서버 서비스의 책임을 혼동하지 않는다. 싱글 에디터 편집 로직과 Dedicated Server 런타임을 구분한다.
- Spatial Hash/RegionID/Partition Cell ID를 공유하고, 서버 지역 상태와 클라이언트 시각 Streaming Source가 서로 다름을 명시.
- 재시작 후 상태 보존이 필요한 보스/퀘스트/필드 상태 범위는 사용자 게임 기획이 없으므로 제안으로만 보고한다.

### 14C.2 후보 데이터 스키마 (실제 기존 코드 조사 후 결정)

```text
DA_HuntingGroundProfile
  RegionId / TerrainAffinity / Tier / EncounterBounds / SafeZoneExclusion
  SpawnerGroupRefs / MobArchetypes / PatrolRouteRefs / SpawnBudget
  SpawnDelayPolicy / AggroAndLeashProfile / BiomeVisualIdentity

DA_FieldBossProfile
  BossId / ArenaBounds / EntranceRoutes / GatheringPoints
  BossArchetype / AbilitySet / SpawnPolicy / PhaseProfile
  DamageAndParticipationPolicy / RewardRefs / VisualBudget

WorldEncounterRegistry
  StableIds / WorldCoordinates / AssetRefs / Version / GenerationSeed
  ReferenceHeight / NavValid / WaterSafe / LastValidationHash
```

위 항목은 **예상 인터페이스**다. 실제 구현에서 GAS, GameState, AI 기본 구조가 이미 있다면 맞춰 조정하고, 과도한 범용 프레임워크를 처음부터 생성하는 일은 피한다.

### 14C.3 필수 검증 장면

- 아홉 사냥터의 **진입/전투/퇴각**을 플레이어·탈것으로 검증. 몬스터가 바위·나무·다리·산비탈에 갇히지 않아야 함.
- 이동속도별 8×8km 횡단 시 몬스터/보스의 **동적 Actor 로딩**과 정적 지형 HLOD/식생의 팝인을 따로 측정.
- 보스 시작/종료 시 근처 HLOD·식생·하천·스킬 이펙트·사냥 몬스터가 겹치는 최악 부하를 검증.
- 주요 마을과 연결된 도로에 몬스터가 무분별하게 증식하지 않고, 몬스터가 장거리 추격 후 원래 구역으로 안전하게 복귀하는지 확인.
- 멀티플레이 테스트가 없다면 실제 MMORPG용 완성품이라고 주장하지 않는다.

---

## 14D. 개별 이미지 레퍼런스 사용법 — v3

**이미지는 합치지 않는다. 자료마다 다음처럼 역할을 분리한다.**

| 파일 | 종류 | Codex 활용 |
|---|---|---|
| `References/01_Starter_World_TopDown_NoLabels.png` | 생성형 수직 조감 **탑뷰 한 장** | 거시 산맥·중앙 도시·호수·삼림·농촌·해안의 대략적 상대 구성. 실제 지형/축척 아님 |
| `REAL_GAME_REFERENCES.md`의 R01~R09 | **실제 게임 스크린샷 원본 링크** | 각각 건축, 재질, 도로, 수변, 원경, 농촌 생활 공간을 독립적으로 관찰. 링크가 열리지 않으면 사용자에게 로컬 PNG/JPG 경로 요청 |
| `References/02_Boss_Arena_Schematic.png` | 보스 전투 구역 독립 도면 | 안전지대·집결지·회피 공간·다중 진입로 |
| `References/03_Hunting_Ground_Schematic.png` | 사냥터 구조 독립 도면 | 몬스터 스폰 군집, 안전거리, AI 순찰, 경사·엄폐 |
| `References/04_Previous_Levels_Before.jpg` | 사용자 기존 Unreal 제작 현황 | 고른 언덕, 반복 도시, 빈 환경과 개선 결과의 Before/After 비교 |
| `ZONE_LAYOUT_LV1_30.json` | **좌표 및 레벨 배치 명세** | 탑뷰 생성 이미지보다 우선하는 구조화된 기획 참고 데이터 |

**스키매틱의 적정성은 사용자 승인 이후에 결정하며, 생성 이미지나 실제 다른 게임의 스크린샷을 그대로 게임 에셋으로 사용하지 말 것.**

시각 QA는 1) 실제 UE 탑뷰 2) 3인칭 도보 3) 탈것 최고속도 주행 4) 고산 원경 5) 도시 골목 6) 보스/사냥터 접근로의 각 **독립 스크린샷**을 기준으로 수행한다. 하나의 인포그래픽 합성 이미지로 제출하지 않는다.

---

## 14E. 스타팅 월드의 플레이 밀도와 실제 크기 체감 관리

- **8×8km를 걷거나 탈것으로 이동하는 데 걸리는 시간은 캐릭터 이동 실측값으로 산출한다.** 도시·마을·퀘스트 허브가 지나치게 떨어져 Lv.1~5 플레이어가 빈 평야만 오래 걷는 현상을 방지한다.
- 위험이 없는 길도 표지판·소교량·시야가 열린 언덕·농지·수레·초소·야영지·하천 소리 등으로 변화를 만든다. 소품은 실제 보유 에셋과 성능에 맞게 조정한다.
- **관심 지점 POI 밀도 목표:** 처음에는 레벨 퀘스트 거점, 지역의 실루엣, 전투 권역을 지도로 배열하고 빈 지역이 어느 이유로 비어 있는지 설명한다. 획일적인 250m 그리드 간격 배치 금지.
- **고도 분포:** Lv.1~30 시작 권역이라도 높은 산·절벽·계곡·실제 등반 가능한 산길이 포함돼야 한다. 높은 산은 Lv.30+ 다음 세계로 확장하는 장벽이 아니라 일부는 Lv.20~30에도 등반 가능한 콘텐츠다.
- **팝인 최악 사례:** 능선 위 전망대에서 중심 도시가 보이는 장면, 능선을 뛰어넘는 이동기, 탈것이 도시 외곽으로 고속 진입하는 장면, 촘촘한 숲 가장자리, 교량 건너 항구 도착을 각각 테스트한다. 최고 이동 속도를 모르면 측정값을 확인하고 기준 속도를 가정했다면 가정값을 분리 보고한다.
- **해안과 후속 월드:** 남쪽 바다가 미래 외부 영역으로 이어질 수 있도록 항구/섬·해로 후보를 남기고, 나머지 세 방향은 인접 높이맵 이음 및 하천/도로 출구를 남긴다.
- **AAA는 품질 목표이지 현재 상태 인증이 아니다.** 전체 8×8 거시 지형 완성, 주요 콘텐츠 위치 완성, 대표 마을 아트 패스 완료, 사냥터 상호작용 완료, 서버 멀티플레이 검증을 각기 다른 상태로 관리한다.

---

## 15. 실행 순서: **8×8km 전체를 먼저 구축**, 지역별 정교화

### Phase 0 — 감사 및 설계 승인 (프로젝트 변경 최소화)

- 기존 프로젝트와 Content 폴더 검사, 재사용 목록, 기술 선택 매트릭스, 8×8km 높이맵/타일 배치, 산악 Z Scale, 확장 경계 설계, 예상 저장공간/빌드비용, 장단점 보고.
- **실제 월드 생성 전** 파괴적 변경과 실험 기능 도입에 대해 사용자 승인 요청.

**산출물:** `01_AUDIT.md`, `02_ARCHITECTURE.md`, `03_ASSET_GAPS.md`, `04_BUILD_PLAN.md`.

### Phase 1 — 8×8km 월드 골격 전체 구축

- 신규 Persistent Level, World Partition, 전체 지형(높이), 전체 월드 권역 및 바이옴 데이터, 산맥·고원·계곡·강·호수·해안의 큰 구조, 전역 도로 연결, 도시/마을 주요 입지 및 랜드마크 위치 생성.
- **아홉 사냥터 H01~H09와 두 보스 구역 B01~B02의 입지 후보/진입로/격리 구역/안전 마스크를 8×8km 전체 골격에 배치**하고, 실제 지형 및 월드 스케일을 기준으로 보정한다.
- 월드 경계 4방향 확장 정보까지 구축.
- 이 단계에서 자연환경 디테일은 일부 비어 있어도 된다. **단, '8×8km 월드 전체의 지형 골격' 자체는 실제 UE 레벨로 존재해야 한다.**
- 경계 이음새 및 WP 스트리밍 기본 동작 테스트.

**산출물:** `.umap`, 데이터 에셋, 생성 결과, 전체 지형 지도 및 보고서.

### Phase 2 — 핵심 이동/원경 시각 체계

- 전역 보행/탈것 도로, 고산 접근로, 주요 교량, 하천과 지면 접합, 핵심 원경용 HLOD/LOD, 스트리밍 로딩 파라미터 구축.
- **8×8km 가로지르기와 산악 등반이 가능한지** 먼저 검증.
- 고속 이동에서 주요 산/도시/숲의 실루엣 가시성 검증.

### Phase 3 — 대표 권역의 정밀 품질 구현

- 왕국 성곽 도시 + 근교 + 숲 + 강 + 산길 등 연결된 대표 구역을 완성도 있게 구현.
- PCG 식생·암석·마을별 구조·지표면 머티리얼 정교화.
- 대표 사냥터 H01 한 곳의 실제 SpawnCandidate→Runtime Spawn→AI 추격/전투/리셋과 B01/B02 후보 중 1종의 아레나 Greybox→전투 루프를 검증한다. 보유 애니메이션/AI 구조와 멀티플레이 검증 상태에 따라 완료 범위를 명시한다.
- 대표 구역에서 여러 에셋의 아트 스타일/밀도/재생성/수동 수정/성능을 합격시킨 뒤 다른 권역으로 확장.

### Phase 4 — 8×8km 전체 지역별 환경 확장

- 중앙, 산악, 호반, 농촌, 삼림, 항구를 서로 다른 생성 파라미터/건축/지형 디테일로 완성.
- H01~H09 모든 사냥터의 Encounter Polygon/진입·탈출로/대표 몬스터 군집/비주얼 아이덴티티를 실제 에디터에 구현한다. 1종 필드 보스 전투 시스템을 실측 검증하고, 두 번째 보스의 완전 구현은 사용자와 조율한다.
- 비어 있는 지역을 단순 랜덤 오브젝트로 채우지 말고 실제 탐험 경로/랜드마크/시야를 기준으로 작업.
- 전체 고속 이동/도시 진입/긴 시야/강 연결 테스트 반복.

### Phase 5 — 최적화와 확장성 검증

- HLOD 빌드, 데이터 스트리밍, Nanite/Foliage/Shadow/Water/Texture/PCG 비용 분석.
- 사냥터 9곳 + 필드 보스 다수 인원 전투에 대한 서버/AI/애니메이션/파티클/네트워크 병목을 별도 측정한다. 월드 렌더링 60FPS 목표와 실제 전투 FPS를 혼동하지 않는다.
- 프로젝트를 재시작한 뒤 맵 로드, C++ 빌드, 맵 Validation, 패키징 플레이, 끊김/팝인, 메모리 및 프레임타임 테스트.
- 실제 추가 외곽 지형 타일 하나를 임시 연결하여 **World Edge Contract가 유효한지** 검증하고 필요하면 되돌리기.

---

## 16. 검증 기준과 최종 결과 보고 형식

### 반드시 확인해야 할 테스트

- **정확한 크기:** 월드/지형 범위가 숫자로 최소 8×8km급인지.
- **Z축:** 산 정상, 능선, 협곡 및 산길을 실제 캐릭터/탈것으로 통과할 수 있는지.
- **수계:** 강이 고도/지형에 맞게 이어지는지, 중간에 끊어지거나 역류하지 않는지.
- **정착지:** 성곽 도시/산악/광산/농촌/호반/항구가 실제로 서로 다른 구성인지.
- **자연환경:** 수목 군집/원경 숲/물가 암석/들판 밀도가 일관되고, 비어 있는 대평원이 무의미하게 반복되지 않는지.
- **원경:** 산 위에서 먼 도시·강·숲이 보이는지, 실루엣이 연속적인지.
- **고속 이동:** 여러 속도/방향/카메라 높이에서 팝인·히치·텍스처 지연이 어느 정도인지.
- **WP/HLOD:** 셀 로딩/언로딩 정상, 원경 HLOD 실제 빌드·전환 정상.
- **재생성:** 동일 Seed로 재생성 시 결정적이고 수동 Override를 보호하는지.
- **확장:** 4방향 경계 정보와 연결 지점 유효.
- **성능:** 대표 타깃 PC의 패키지 빌드에서 FPS/프레임타임/메모리 데이터 기록.
- **사냥터:** H01~H09의 유효 좌표, 마을 안전 경계, 스폰·리스폰, AI Nav·Leash, 이동 스킬과 탈것의 접근 경로, 위험도별 분포가 실제로 검증되었는지.
- **필드 보스:** 1종 이상이 실제 움직이며 서버 권한/전투·페이즈·리셋 상태를 검증했는지, 둘째 보스는 `DONE / GREYBOX / NOT IMPLEMENTED` 중 정확히 어느 상태인지.
- **네트워크:** AI·보스와 월드 셀 스트리밍을 서로 다른 시스템으로 관리하고, 다중 클라이언트 및 서버 상태 재현 실험을 했는지.

### 매 단계 보고서 템플릿

```markdown
# World Build Report — Phase N
- Branch/commit: ...
- Engine version and plugins: ...
- Modified/created assets: 실제 /Game/... 경로
- Modified C++ sources: 실제 파일 경로
- Generated world bounds XY/Z: ... / ...
- Verified MCP tools actually used: ...
- Build status: PASS/FAIL/SKIPPED (사유)
- Unreal Editor level open/save: PASS/FAIL/SKIPPED
- PCG generation and deterministic re-run: PASS/FAIL/SKIPPED
- WP/HLOD generated and tested: PASS/FAIL/SKIPPED
- Packaged/standalone test: PASS/FAIL/SKIPPED
- Fast traversal pop-in test: 속도 / 구간 / 결과
- Hunting grounds H01–H09: terrain/AI/spawn/safe boundary/evidence by ID
- Field bosses B01/B02: arena/AI/phases/network/evidence and explicit completion state
- Performance: FPS, frame times, VRAM/RAM, trace paths
- Visual evidence: 스크린샷/영상의 실제 경로
- Known limitations: ...
- Next dependencies/required assets: ...
- Explicit user decisions required: ...
```

**증거가 없으면 `SKIPPED` 또는 `UNVERIFIED`로 표시하고 절대로 PASS라고 보고하지 말 것.**

---

## 17. 최종 사용자 승인 질문 (처음 분석 후 필수)

첫 응답에서 이미 프로젝트 안에 존재하는 코드를 충분히 읽은 후 아래 사항을 제안하라.

1. 기존 레벨을 보존한 상태로 신규 맵을 추가할지 (기본 Yes).
2. 8×8km 내부의 경계 전이 지역을 얼마나 둘지, 실제 별도 외곽 버퍼까지 요구하는지.
3. Landscape 기본안 대 실험 Mesh Terrain 여부 및 위험.
4. 확보한 에셋으로 가능한 스타일·밀도와 추가 Fab 다운로드가 필요한 부분.
5. 최고 스킬/탈것 속도 및 타깃 하드웨어 예산.
6. 하천·해안·산악 높이와 도시 규모 등 가장 중요한 비주얼 방향.
7. 성능과 품질 우선순위를 포함한 Phase 1 시작 승인.
8. **기존 전투/AI 시스템을 실제 게임플레이에 재사용할 것인지, 몬스터/보스 에셋과 애니메이션은 충분한지.**
9. **필드 보스 초기 1종 우선 구현 및 2종째 착수 조건**, 사냥터 9곳 상대 난이도를 실제 레벨 시스템에 언제 연결할지.
10. **이 문서의 이미지가 참조용임을 확인하고, `References/` PNG와 JSON을 열어 계획을 실제로 검토했는지.**

**승인 전에는 분석/조사 산출물을 먼저 제시하고, 승인 이후에는 8×8km 월드 골격을 실제로 구축하는 실행으로 진행하라.** 사용자가 별도 요청하지 않았다면 기존 파일 삭제·대규모 리임포트·유료 결제·보호되지 않은 브랜치 푸시를 하지 마라.

---

## 참고 공식 문서 목록 (검토일: 2026-10-17)

1. [Unreal MCP in Unreal Editor — UE 5.8](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)
2. [Unreal Engine 5.8 공식 출시 발표](https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available)
3. [World Partition](https://dev.epicgames.com/documentation/unreal-engine/world-partition-in-unreal-engine)
4. [World Partition HLOD](https://dev.epicgames.com/documentation/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine)
5. [Landscape Technical Guide](https://dev.epicgames.com/documentation/unreal-engine/landscape-technical-guide-in-unreal-engine)
6. [Landscape Edit Layers](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-edit-layers-in-unreal-engine)
7. [Mesh Terrain](https://dev.epicgames.com/documentation/unreal-engine/mesh-terrain-in-unreal-engine)
8. [PCG and Mesh Terrain](https://dev.epicgames.com/documentation/unreal-engine/pcg-and-mesh-terrain-in-unreal-engine)
9. [PCG Generation Modes](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine)
10. [City Sample PCG](https://dev.epicgames.com/documentation/unreal-engine/city-sample-pcg-for-unreal-engine)
11. [Electric Dreams Environment](https://dev.epicgames.com/documentation/en-us/unreal-engine/electric-dreams-environment-in-unreal-engine)
12. [Water Body Actors](https://dev.epicgames.com/documentation/unreal-engine/water-body-actors-in-unreal-engine)
13. [Nanite Foliage](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage)
14. [Procedural Vegetation Editor](https://dev.epicgames.com/documentation/unreal-engine/procedural-vegetation-editor-pve-in-unreal-engine)
15. [Large World Coordinates](https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-unreal-engine-5)
16. [The Witcher 4 UE5 Tech Demo — Epic 공식 영상](https://www.youtube.com/watch?v=aorRfK478RE)
17. [World Partitioned Navigation Mesh — UE5.8, Experimental](https://dev.epicgames.com/documentation/unreal-engine/world-partitioned-navigation-mesh)
18. [Environment Query System (EQS)](https://dev.epicgames.com/documentation/en-us/unreal-engine/environment-query-system-in-unreal-engine)
19. [Gameplay Ability System Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system)
20. [Replication Graph](https://dev.epicgames.com/documentation/unreal-engine/replication-graph-in-unreal-engine)
21. [World Partition Streaming Sources](https://dev.epicgames.com/documentation/unreal-engine/world-partition-in-unreal-engine)
22. [PCG Development Guides](https://dev.epicgames.com/documentation/en-us/unreal-engine/pcg-development-guides)

**결론:** 사냥터 9곳과 필드 보스 1~2종의 설계/실제 구현 및 성능 검증을 포함해 이번 작업의 성공 여부는 파일을 얼마나 많이 만들었는지가 아니라, **하나의 실제 8×8km급 지형에서 고산 등반, 하천과 지형의 자연스러운 연결, 개성 있는 중세 정착지, 고속 이동 시 원경 연속성, 재사용 가능한 툴 체계, 검증된 최적화**가 구현되었는지로 판단한다.
