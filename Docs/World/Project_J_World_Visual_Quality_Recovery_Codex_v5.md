# Project_J — 8×8km 시작 권역 비주얼 품질·원경·수계·메모리 통합 개선 실행 지시서 (Codex v5)

> **이 문서는 기존 월드 제작 프롬프트의 대체가 아니라 후속 실행 지시서다.**
>
> **목표:** `Project_J`에 이미 배치된 8×8km Lv.1~30 시작 권역의 20개 주요 공간(정착지 9, 사냥터 9, 필드 보스 2)을 유지하면서, 에디터와 실제 플레이에서 모두 자연스럽고 밀도 높은 MMORPG 환경을 구현한다. 원거리 숲, 민둥산, 낮은 풀 밀도, 단조로운 도시·마을, 부자연스러운 물과 허전한 연결 공간, 텍스처 스트리밍 경고를 우선 해결한다.
>
> **하드웨어 기준:** 개발자 GPU `AMD Radeon RX 9070 XT 16GB`. 엔진은 UE 5.8 계열을 가정하되 **설치된 정확한 버전과 실제 설정을 먼저 확인**한다. 16GB는 전체 GPU 메모리이고 텍스처 풀 전용 용량이 아니다.
>
> **우선 적용 규칙:** 사용자 최신 지시 > 현재 프로젝트의 작동 중인 게임플레이 계약/원본 보존 > 프로젝트 내 최신 `인수인계.md`와 실행 현황 > 이전 `WorldGen_Prompt_v4.md` 및 `v3` 참고 파일 > 이 문서의 구현 예시. 단, 본 문서가 요청한 **비주얼 문제 해결의 작업 범위 및 검증 기준**은 이번 후속 작업의 기준이다. 서로 충돌하면 사용자에게 차이를 설명하고 파괴적 변경을 피한다.

---

## 0. 새 Codex 채팅의 첫 행동: 실제 인수인계부터 읽기

1. 프로젝트 루트 또는 `Docs/`의 최신 `인수인계.md`(파일 이름이 다르면 관련 handoff 문서), 이전 월드 프롬프트, 현황 보고서와 Git 상태를 확인한다. **존재하지 않는 파일명/경로를 지어내지 말 것.** 문서의 최신 완료/미완료 항목을 이 실행 지시서와 대조한다.
2. 현재 월드 `.umap`, World Partition 상태, Landscape / Heightmap 원본과 Edit Layers, PCG 컴포넌트, Foliage Type, Grass Type, Materials, Post Process, Light, Water/도로 Spline, HLOD Layer 및 이미 임포트된 건축·자연환경 에셋을 조회한다.
3. 사용 가능한 Unreal MCP 동작을 확인한다. 도구가 제공하지 않는 조작은 적법하고 검증 가능한 Editor Utility, Editor Python, C++ Editor 모듈 또는 UE 명령행으로 보완한다. **에디터에서 실행하지 않은 변경을 실행했다고 보고하지 않는다.**
4. 기존 20개 장소의 Stable ID·좌표·참조 액터를 보존한다. `__ExternalActors__` 등을 포함한 World Partition 데이터, 공급자 원본, 기존 게임플레이·이동·퀘스트·전투 계약을 무단 삭제/덮어쓰기하지 않는다.
5. 수정 전 브랜치/변경사항을 안전하게 보존한다. 미커밋 작업을 임의로 리셋하지 않는다. 지형 변경은 원본 높이맵을 보존한 **비파괴 Edit Layer/파생 데이터/배치 메시**를 우선한다.
6. **이번 요청은 계획 문서만 쓰고 끝내는 요청이 아니다.** 읽기 전용 현황 조사 후, 안전하게 가능한 수정 작업을 실제로 구현하고 단계별 실측·스크린샷으로 검증한다. 도구·에셋·사용자 승인이 필요한 지점은 명확히 구분하여 막히지 않는 다른 작업을 진행한다.
7. 날씨 제작, Lv.31+ 전체 제작, 신규 핵심 게임플레이 시스템 재작성은 이번 범위에서 제외한다. 이후 권역 확장에 사용할 공통 PCG/Material/Biome/Streaming 설정은 유지한다.

### 현재 사용자 스크린샷에서 관찰된 문제 (가설이며 프로젝트 분석으로 확인)

- 에디터에서 특정 숲만 높은 밀도로 보이고 주변 산과 구릉은 나무가 거의 없거나 멀어지면 사라진다.
- 실제 플레이에서 가까운 잔디는 있지만 먼 거리와 산비탈의 자연환경이 텅 비어 보인다.
- 에디터 품질을 **Low**로 설정한 적이 있어, 품질 설정이 식생/라이팅 차이를 일으켰을 가능성이 있다.
- 산악은 높낮이만 있고 암반·자갈·수종·수관·토양 변화가 없어 매끈한 표면처럼 보인다.
- 기존 도시에는 대표적인 큰 건물과 뚜렷한 스카이라인이 부족하고 마을은 소수 주택이 흩어진 형태다.
- 강·호수·바다의 물, 하상·호안·교량 접합과 전체 도로 주변 환경이 미완성이다.
- 플레이 화면에서 **Texture Streaming Pool Over Budget** 경고가 나타난 적 있다. 과거 캡처의 약 142.793MiB는 **당시 초과량**이며 현재 수치·풀 용량을 뜻하지 않는다.
- 에디터와 PIE가 다른 위치/시간/카메라일 수 있으므로 단순 육안 비교만으로 원인을 확정하지 않는다.

---

## 1. 가장 먼저 수행할 원인 격리: Low 품질 vs 실제 생성 부족 vs 스트리밍

### 1.1 통제된 비교 시나리오

대표 장소 5~6곳을 선택하고 **같은 좌표·카메라 Transform·FOV·시간대·해상도·화면 비율·Screen Percentage·Data Layer 로드 상태**에서 촬영한다.

- `V01` 시작 마을 바닥/풀: 캐릭터 눈높이, 약간 떨어진 시점.
- `V02` 숲 가장자리: 가까운 수목→중거리 산림 전환.
- `V03` 민둥산 정면: 산기슭부터 능선까지 관찰.
- `V04` 산 정상/고지대 전망: 중·원경 숲과 수도/하천 관찰.
- `V05` 수도 외곽 접근로: 먼 거리 스카이라인→성문→내부.
- `V06` 강·호수·다리: 수변 접합, 흐름·물높이·반사.

에디터 일반 뷰포트, Game View(`G`), PIE, 가능하면 Development 또는 Shipping 패키지 환경을 구별한다. 동일 비교 시 **뷰포트의 실시간 조명·노출 설정, 게임 카메라 후처리, PIE GameUserSettings를 기록**한다. 에디터 저품질 뷰포트와 게임 Epic을 그대로 비교하지 않는다.

### 1.2 품질 프리셋 별로 차이 분석

UE 5.8 기본 Scalability Reference에서는 `FoliageQuality@0`의 `foliage.DensityScale=0`, `grass.DensityScale=0`이며 `@3`에서는 각각 1.0이다. **프로젝트별 커스텀 설정이 있을 수 있으므로 실제 값부터 확인**한다.

테스트 콘솔 명령(현 설정 로그 저장 후 실험, 영구 적용 아님):

```ini
sg.FoliageQuality 3
sg.ViewDistanceQuality 3
grass.DensityScale 1
foliage.DensityScale 1
```

- `sg.*`는 다른 관련 값을 덮을 수 있으므로 **적용 전후 각 CVar를 다시 읽는다.** 품질 비교의 조건으로만 사용한다.
- Low/Epic 설정에서 나무·풀이 바뀐다면 **Scalability** 영향 및 Foliage Type `Enable Density Scaling` 여부를 확인한다.
- 밀도가 변하지 않으면 `Landscape Grass Output`, PCG Mask/Bounds, 생성 결과·컴포넌트, 월드 셀, `Cull Distance`, HLOD를 확인한다.
- 멀리서는 없고 가까이서만 보일 때, **생성되지 않음 / 렌더링 컬링 / WP 셀 언로드 / 런타임 PCG 생성 반경** 네 원인을 분리하여 증거를 제시한다.
- `stat streaming`의 풀 부족은 텍스처 해상도/밉의 문제다. **수목/잔디 인스턴스 개수 감소나 라이팅 차이의 직접 원인이라고 단정하지 않는다.**
- PCG 액터 바운드나 보라색 볼륨은 생성 범위일 수도, 편집기 디버그 표시일 수도 있다. **에디터에서 액터 타입과 Graph 연결을 실제 조회**한다.

**산출물:** `Docs/WorldQuality/01_BASELINE_DIAGNOSIS.md` — 장소별 스크린샷, Low/Epic/PIE의 CVar와 그래픽 조건, 원인 확정/미확정, 변경 전 수치.

---

## 2. 멀리서 사라지는 나무와 빈 산림 — 생성/가시성 이중 해결

1. 기존 `PCG Graph → Biome Mask → Bounds → Generated Actors/ISM/HISM → Runtime Grid → HLOD` 경로를 역추적한다. 일부 구역에만 생성돼 있으면 **품질 설정을 높이는 것으로 해결되지 않는다.**
2. 월드의 고도·경사·바이옴·수분·도로·정착지·보스 제외 영역에 따라 **산림 분포와 수종 군집**을 구성한다. 숲 가장자리/산기슭/골짜기/고산/암벽이 똑같은 수목 밀도가 되지 않도록 한다.
3. 경사면 수목 생성의 `Slope Filter`가 너무 엄격한지, 표본이 Landscape를 올바르게 샘플링하는지, 낮은 산의 수목이 원천적으로 누락됐는지 확인한다.
4. 정적 자연환경은 가능하면 **에디터 생성 결과를 저장하고 WP 셀 스트리밍**으로 관리한다. 런타임 PCG를 실제 사용하는 경우 스폰 반경, 생성 Source, Cleanup, 편향된 로딩 방향과 고속 이동을 별도로 검증한다.
5. 식생별 Static Mesh LOD/Nanite, ISM/HISM/Foliage Type, Fade 및 컬링, Scalability, Draw Distance, WP Loading Range를 확인한다. 원거리 HLOD Layer의 `Instancing` 등 적합한 정책을 시험하고 **실제 HLOD 빌드와 언로드된 셀의 가시성을 검증**한다. HLOD에 미포함인 컴포넌트는 별도 원거리 전략을 마련한다.
6. 작은 풀을 몇 km까지 풀 메시로 유지하지 않는다. **큰 수목 실루엣은 중·원경에 유지**, 작은 풀은 지면 재질의 원거리 색상·Macro Variation/적합한 저비용 표현으로 연결한다.
7. HLOD 및 원거리 대체 메시를 만들 때 수관 형태와 색상 변화, 전환 순간의 팝인/색 차이, 바람 애니메이션 불연속, 그림자 튐을 확인한다.
8. `foliage.CullDistanceScale` 등 전역 수치를 극단적으로 올리거나 WP 셀을 모두 로드해서 문제를 숨기지 않는다. 변경 이유와 프레임·메모리 비용을 측정한다.
9. 산 정상에서 360도 회전, 도보·탈것·이동 스킬의 실제/목표 최고 속도 구간을 테스트하고 정지/급회전 시 식생 생성 지연을 측정한다.

**완료 기준:** `V02/V03/V04`에서 **생성이 필요한 산림대가 실제로 생성되어 있고**, 일정 거리에서 지형만 남도록 갑자기 지워지지 않으며, WP 셀 언로드 뒤에도 적절한 원거리 숲 실루엣이 보일 것. 실제 시야거리·FPS·메모리는 측정값으로 결정할 것.

---

## 3. 산악 지형과 Landscape Material — 높이 데이터 보존

- 현재 Heightmap/Slope/Curvature/Water Mask/Region Mask를 분석하고 **원본 산체나 길을 무단 재생성하지 않는다.** 필요 시 비파괴적 보완을 별도 데이터/레이어에 저장한다.
- `Landscape Material`은 단색 풀에서 최소 **Grass / Dry Grass / Dirt / Mud / Gravel / Rock / Forest Floor / Sand 또는 Wet Ground**의 기능적 레이어로 확장한다. 각 레이어의 실제 보유 텍스처·Normal/Roughness 상태를 조사하고 부족한 것만 요청한다.
- 단순 고도·경사 기반 자동 분포만으로는 인공적인 띠가 생길 수 있으므로 **바이옴 마스크·곡률·수분·노출 암반·수동 페인팅**을 결합한다.
- 가까운 거리의 타일링과 먼 거리의 균일한 색을 함께 잡기 위해 **micro/detail + macro color/noise variation + 적합한 거리별 텍스처 스케일**을 제공한다. 과도한 Landscape Layer 수로 셰이더가 비대해지지 않게 머티리얼 비용도 검증한다.
- 산 정상·노출 능선·급경사에는 바위/암반 Material 및 검증된 `Cliff Mesh`를, 절벽 하부에는 바위 군집/낙석/자갈을 배치한다. 기존 지형의 충돌/등반로/교량을 유지한다.
- 산기슭과 계곡에 밀집된 나무·관목, 중턱에 점진적 감소, 높은 능선에는 저식생/암석 등 **물리적 지형의 차이**가 실제 플레이와 원경 모두에서 구분되어야 한다.
- Landscape와 Rock Mesh, Road Spline, 물 주변의 지면 색·Normal·Roughness가 과도하게 분리되지 않도록 **RVT 또는 머티리얼 블렌딩 등 실제 호환되는 방식**을 비교한다.
- 8×8km 전역에 같은 노이즈·동일한 나무 간격을 반복하는 것을 금지한다. 대륙 수준의 지형 실루엣과 국지적 지면 표현을 분리한다.

**완료 기준:** `V03/V04` 원경에서 산기슭 숲, 암반면, 능선·자갈·초지 구획이 구분되고, `V01/V03` 근거리에서 반복적인 풀 텍스처만 보이지 않을 것.

---

## 4. 지표면 3D Grass, Ground Cover, 자연환경 밀도

- 기존 `Landscape Grass Type`/`Landscape Grass Output`·Foliage·PCG와 임포트한 메시를 먼저 확인한다. **주기능이 겹치는 시스템을 무작정 중복해서 GPU/메모리 비용을 증가시키지 않는다.**
- 작은 풀·잡초·들꽃은 성능이 적절할 때 Landscape Grass로, 관목·고사리·어린 나무·고목·바위는 PCG/수동 배치로 구성한다.
- `Grass`는 한 모델만 무작위 회전시키는 대신 **짧은 잔디/긴 잔디/마른 풀/잡초/들꽃** 등의 변화를 지역별로 설정한다. 기존 에셋 품질이 낮으면 신규 무료 후보를 목록화한다.
- **마을/길/농지/보스 아레나/물/암반 제외 마스크**와 주변 밀도 페이드를 적용한다. 사람의 이동량이 많을수록 식생이 듬성한 지면, 숲 바깥이 초원으로 넘어가는 전이대를 구현한다.
- 숲속 바닥에 낙엽·작은 가지·이끼·고사리, 큰 바위 주변에 돌과 관목, 수변에 갈대와 습윤 식물을 배치한다. 모든 빈 땅을 풀로 덮지 않는다.
- 각 바이옴별 최소 2~3가지 크기와 밀도 패턴을 비교하고, PCG 가시 디버그 및 실제 사진에서 규칙적인 격자·동일 크기 반복이 보이지 않는지 확인한다.
- 멀리서 풀 메시가 사라질 때 **Landscape Material이 원거리 Grass Cover 색/질감 역할**을 자연스럽게 이어받게 만든다.
- 고밀도 식생으로 인해 Overdraw, 그림자, WPO/바람, 충돌 비용이 증가할 수 있으므로 개별 메시·인스턴싱·Nanite 적용 여부를 실제 GPU 프로파일링으로 결정한다.

**완료 기준:** 가까운 초원/숲/산길/강변의 지면이 텍스처 한 장처럼 보이지 않으며, PIE Epic에서 동일 위치의 식생 수가 의도한 설정과 일치할 것.

---

## 5. 수도·마을·20개 장소의 환경 아트 — '큰 건물 없는 도시' 해결

1. 기존 20개 장소의 Blockout과 실제 배치 Actor, 사용 가능한 건축 에셋을 먼저 매핑한다. **20개 모두 다시 만들지 않는다.**
2. **S01 메인 수도는 별도 생성 규칙**을 사용한다. 일반 주택 PCG만 반복 생성하지 말고 **대표 랜드마크 1개 이상**(성채/궁전/대성당/대형 길드홀 중 기존 콘셉트와 에셋에 적합한 것), 큰 공공시설(시장/병영/공방/대형 여관 등), 성문·성벽, 대로·광장·상가·주거지·뒷골목으로 도시를 구분한다.
3. 대표 건물은 멀리서 알아볼 수 있는 **스카이라인과 크기 계층**을 가지며, 광장·접근로·성곽과 공간적으로 연결한다. 최소한 기존 소형 주택 복제만으로 ‘수도 완성’이라 판단하지 않는다.
4. 중형 마을은 농업/숲/호반/항구/산악 등 각 기능에 맞춘 **고유 중심시설과 도시 형태**(농장/목공소/부두/산길 계단식 집터)를 사용한다. 소형 거점은 규모를 불필요하게 키우지 말되 길목 기능이 분명해야 한다.
5. 건축 메시 부족을 감추기 위해 큰 건물을 임의 확대하지 않는다. **모듈형 에셋 조합·기존 대형 건물의 재사용·정상적인 랜드마크 제작 요구**를 검토하고, 추가 팩이 필요하면 무료 Fab 후보/라이선스·포함 내용·엔진 버전 확인 후 사용자에게 요청한다. 외부 구매/로그인/승인 없이 수행됐다고 말하지 않는다.
6. 건물 하단 기초·지붕·창·문·담장·도로 접합, 길 폭과 회전 반경, NPC/탈것 통행, 작은 생활 소품을 점검한다.
7. 도시 외부 `주요 도로→농로/숲길→야생지`의 **연속적인 중간 경관**을 만든다. 작은 POI(과수원/울타리/목책/마차 정류장/작은 폐탑/작은 폭포 등)는 지역 맥락에 따라 선택적으로 생성한다. 억지로 모든 공간을 채우지 않는다.
8. 큰 랜드마크의 HLOD/스트리밍과 Lumen/그림자 비용을 확인해 원경에서도 수도가 식별되도록 한다.

**완료 기준:** `V05` 원경에서 수도의 중심 건축과 지붕 높이 분포가 뚜렷하고, 캐릭터가 도시 진입→광장→기능 구역으로 실제 이동 가능할 것. S02~S09는 기존 정체성과 공간 규모에 맞춰 현황별 개선 결과를 기록한다.

---

## 6. 강·호수·해안 Water 구현 (무료 내장 기능 우선)

1. 기존 강/호수/해안의 Blue Spline, Water Actor, 수면 메시, Heightmap·Edit Layer, Bridge·Road Spline, 이미 활성화된 플러그인을 먼저 감사한다.
2. **UE Water 플러그인 기본 콘텐츠부터 검토**한다. `Water Body River / Lake / Ocean`과 `Water Zone`, 수면 머티리얼, 부가 Water Content를 실험적으로 시험한다. UE 5.8에서 Water는 **Experimental**이므로 필수 프로젝트 경로에 바로 적용하기보다 별도 안전한 테스트 맵/지역에서 평가한다.
3. `Water Body`는 Landscape Brush로 지형을 깎을 수 있다. **기존 Heightmap/산체를 보호**해야 하므로 전용 Edit Layer 및 롤백 가능한 시험 영역에서 지형 변형을 검증하고, 기존 다리/해안/월드 경계를 무단 훼손하지 않는다. 기존 Landscape가 Edit Layers를 쓰지 않는 경우 안전한 전환과 영향 범위를 검토한다.
4. 내장 Water 플러그인이 현재 월드와 호환되지 않거나 부작용이 과도하면 **Spline 기반 커스텀 수면 메시 + 흐름 머티리얼 + 수동/PCG 호안**도 후보로 비교한다. 기존 수계를 이중으로 겹쳐 그리지 않는다.
5. 하천의 `상류→지류→본류→호수/하구/해안`을 실제 고도에 따라 검토한다. 강폭·곡률·변화, 합류부·교량·하상, 물 표면의 높이 경계/끊김을 개선한다.
6. 호수는 원형 수면을 올려둔 것처럼 보이지 않도록 **불규칙한 호안·얕은 물·갈대·바위·모래/진흙 구간**과 연결한다. 강변 PCG에 물 접근 마스크·습윤 재질을 전달한다.
7. 바다와 항구는 해안 절벽·모래 해변·부두·방파제·원경 수면을 연결한다. Water Zone 범위와 수면 타일, 원경 공백·반사/거품/깊이 표현을 점검한다.
8. 물리적으로 완전한 유체 시뮬레이션을 요구하지 않는다. 대부분의 하천 흐름은 셰이더/Flow Map으로 표현해도 된다. 캐릭터의 수면/수중 이동 및 충돌 등 게임플레이 계약과 관련된 기능은 별도로 검증한다.
9. Water가 Lumen/RT 반사·투명도·VRAM에 미치는 비용을 실제로 측정한다. **무료 Water로 충분한 결과를 얻을 수 있으면 외부 유료 물 에셋은 필요 없다.**

**완료 기준:** `V06`에서 수면/강둑/교량 높이가 맞고 강의 흐름이 눈으로 식별되며 호수·바다와의 연결이 부자연스럽게 끊기지 않을 것. PIE와 패키지에서 결과를 확인한다.

---

## 7. Texture Streaming Pool / RX 9070 XT 16GB / 향후 Lumen·RT 예산

### 7.1 실제 원인 확인 및 초기 설정

- 게임에서 발생했던 `TEXTURE STREAMING POOL OVER BUDGET`을 **문자 그대로 현재 과다 사용량으로 해석하지 말 것**. 어느 시점의 풀 크기·Required Pool·NonStreaming Mips/임시 메모리·해상도·씬 상태에서 발생했는지부터 기록한다.
- `stat streaming`, `stat RHI`, `stat gpu`, `stat unit`, 가능하면 Unreal Insights 및 GPU 프로파일링으로 전체 VRAM/프레임 예산을 수집한다.
- **개발 PC용 초기 테스트값**은 `r.Streaming.PoolSize 6144`(MB), 여유가 충분하고 필요한 텍스처를 유지할 경우 `8192`와 비교. `4096`도 비교 기준으로 사용. **6GB·8GB는 측정 전의 테스트 기준이지 고정 정답이 아니다.**
- 콘솔에서 실행할 때 엔진/기존 설정에 따라 필요하면 `r.Streaming.UseFixedPoolSize 1`을 적용하고 런타임 변경 반영 여부를 확인한다.
- **`r.Streaming.PoolSize 0`으로 무제한 설정하거나 `r.TextureStreaming 0`으로 스트리밍을 꺼서 문제를 숨기지 말 것.**
- 실제 16GB VRAM은 텍스처 외에도 Nanite 메모리, Lumen GI/Reflection 캐시, Shadow Maps / Virtual Shadow Maps, Render Targets, Water, GPU Buffers, RT 가속 구조, UI 및 에디터 오버헤드가 공유한다. 전부를 스트리밍 풀에 할당하지 않는다.

실험 콘솔 명령 (값은 변경 전후 로그와 함께 저장):

```ini
stat streaming
stat RHI
stat GPU
stat Unit
r.Streaming.UseFixedPoolSize 1
r.Streaming.PoolSize 6144
```

6GB 테스트 통과 후 8GB 비교:

```ini
r.Streaming.PoolSize 8192
stat streaming
stat RHI
```

### 7.2 영구 반영 정책

1. **현재 CVar의 소스 우선순위**(프로젝트 Config, Device Profile, Scalability, GameUserSettings, 콘솔/런치 옵션)를 확인한다. 동일 변수가 여러 파일에서 덮이는 상태를 정리한다.
2. 개발기에서 안정적인 수치를 측정한 뒤 **고사양 GPU 전용 Device Profile 또는 적절한 Texture Quality 프리셋**으로 배치한다. 모든 PC/저사양 사용자에게 6~8GB를 강제하지 않는다.
3. 영구 프리셋 예시는 실제 Config 구조를 확인한 경우에 한해 `Config/DefaultScalability.ini`의 `[TextureQuality@3] r.Streaming.PoolSize=6144`를 검토한다. 기존에 이 항목이 있으면 중복 선언을 피한다. **이것이 에디터 Low → Epic 자동 전환을 의미하지는 않는다.**
4. Texture Streaming build 데이터가 오래됐으면 관련 데이터를 적절하게 재빌드하고, 고정 임포트 에셋의 NeverStream/LOD Bias/텍스처 해상도를 조사한다.
5. 프로젝트에서 실제로 낭비되는 고해상도·중복 텍스처가 있으면 정리하되 **공급자 에셋 원본은 보호**하고 파생/인스턴스 수정으로 처리한다.

### 7.3 미래 하드웨어 레이트레이싱(HW RT)·Lumen 고려

- 기존 렌더링 경로와 RHI(DX12 등), Lumen의 Software/Hardware RT 적용 상태를 먼저 조사한다. **RTX/RT 수준을 올리기 전에 기본 월드/식생/텍스처/스트리밍을 정상화**한다.
- AMD RX 9070 XT는 DX12 하드웨어 RT의 기술적 지원 대상이지만, **HW RT가 언제나 빠르거나 좋은 기본 선택이라는 뜻은 아니다.** 산림의 많은 인스턴스·겹치는 잎/잔디·WPO·그림자 비용 때문에 성능이 크게 달라질 수 있다.
- Lumen Software RT vs HW RT, Reflection 품질, Virtual Shadow Maps/그림자 거리, Screen Percentage/업스케일러를 동일 씬에서 비교한다. RT 활성화 시 **TLAS/BLAS 등 가속 구조, Lumen 관련 메모리와 GPU 시간을 별도 기록**한다.
- RT를 위해 기존 식생·Nanite를 무차별적으로 변환하지 않는다. RT Scene에 포함할 메시 범위, Cull, 저비용 Fallback, 반사/그림자 품질을 정리한다.
- **예산상 RT까지 동시에 감당하지 못하면** 기본 프리셋에 Software RT/Lumen 구성을 선택하고 고품질 RT는 별도 프리셋으로 제공한다. 기본 이동 품질과 VRAM 안정성을 우선한다.
- 메모리 부족 시 먼저 문제를 측정하고 Texture Pool 및 렌더링 프리셋을 조정한다. 시스템 메모리로 페이지아웃되거나 GPU OOM 직전까지 몰아넣지 않는다.

**완료 기준:** 설정과 게임 프리셋이 실제 PIE/패키지에 적용되며 반복 이동 시 스트리밍 경고와 저해상도 Mip 불안정성이 사라진다. VRAM 사용·GPU/CPU 프레임시간·RT 사용 시 증가한 예산을 재현 가능하게 기록한다.

---

## 8. 장면 일치 및 시각 품질 보정 — 에디터와 게임을 구분하되 같은 목표 화면 유지

- `sg.FoliageQuality`, `sg.ViewDistanceQuality`, `sg.GlobalIlluminationQuality`, `sg.ShadowQuality`, `sg.PostProcessQuality`, `sg.TextureQuality`, `r.ScreenPercentage` 등 실제 값을 **Low/Epic 뷰포트·PIE·패키지 각각 확인**한다.
- PostProcessVolume의 `Infinite Extent`, 적용 우선순위와 카메라 PostProcess 오버라이드, Auto Exposure(셔터/히스토그램 설정), DirectionalLight/SkyLight, Lumen, 안개, 톤매퍼 등을 점검한다.
- 라이팅이 다르다는 이유로 무작정 텍스처 색을 보정하지 않는다. 조명·노출/카메라 설정을 정렬한 후 아트 리밸런싱을 한다.
- 작업 중 에디터 UI의 `Low` 설정은 빠른 조작을 위한 **미리보기 옵션**으로 유지할 수 있다. 하지만 **최종 품질 판단은 Epic 등 목표 플레이 프리셋에서** 한다.
- 현재 엔진 버전·프로젝트 커스텀 `Scalability.ini`에 따라 Low에서 Grass가 0이 아닐 수도 있다. **실제 CVar 읽기 결과를 우선**한다.
- 고품질 프리셋과 성능 프리셋은 각각 검증한다. 저품질이라도 게임플레이 필수 오브젝트·몬스터·충돌·길이 사라지지 않게 한다.

---

## 9. 작업 순서 — 유의미한 결과를 실제로 만드는 단계

| 단계 | 실제 구현 내용 | 완료 판정 및 선행 의존성 |
|---|---|---|
| **P0 감사/비교** | 인수인계 읽기, 실제 설정/파일 확인, Low ↔ Epic ↔ PIE 동일 씬 비교, 16GB VRAM baseline | 원인 분류 및 스크린샷·통계 확보. **작업을 조사만 하고 멈추지 말 것** |
| **P1 식생 가시성 복구** | PCG 생성 범위, Scalability, Culling, WP Streaming/HLOD 연결, 원거리 산림 테스트 | 생성 문제와 표시 문제 모두 근거 있게 해결 |
| **P2 전역 지면/산악 아트** | Landscape Material/Grass, 암반·바위·산림대·Biome 규칙, 마을 간 지형 | V01/V03/V04의 실제 품질 변화, 원본 보존 |
| **P3 수도·마을 환경 아트** | 수도 대형 랜드마크·내부 구획, 기능별 마을, 소품/도로 연결, POI | 수도 스카이라인 및 지역 정체성 검증 |
| **P4 수계·호안 구현** | 내장 Water 시험, 강·호수·해안·교량 연결, 수변 PCG | 편집 안전성·호안·흐름·게임 충돌 확인 |
| **P5 전체 권역 확장** | 검증된 공통 규칙으로 8×8km 전체 20개 장소 + 연결지대에 적용 | 대표 구간만 좋은 상태로 종료하지 않음 |
| **P6 통합 성능/회귀** | 고속 이동, 산악 전망, 수목·도시 원경, Lumen/RT, PIE/패키지, 목표 GPU/저사양 프리셋 | 실제 FPS·프레임타임·메모리 및 품질 리포트 |

**P4의 기반 Water 테스트는 필요 시 P1~P3와 병행할 수 있지만, 본격적인 수변 아트는 도로·지형·강높이 안정화 후 적용**한다. P1~P5 전 과정에서 메모리·프레임시간을 중간 측정한다.

### 반드시 피할 실패 패턴

- **P0 진단만 반복하며 실제 수정 없이 종료**하는 것.
- 작업 속도만 최적화하여 프리팹·나무를 대량 복제하는 것.
- HLOD 빌드·PCG 생성 확인 없이 `Cull Distance`를 극단적으로 올리는 것.
- 지형 높이 원본을 삭제하거나 Water Brush로 주요 마을·도로·산길을 망가뜨리는 것.
- 텍스처 풀을 무제한으로 설정하고 경고가 사라졌다는 이유로 완료하는 것.
- 테스트한 적 없는 하드웨어 RT를 고품질이라는 이유만으로 상시 활성화하는 것.
- 수도에 대형 랜드마크 없이 주택만 늘려 완료하는 것.
- 단순 위치 후보나 빈 공간만 존재하는 사냥터/보스 구역을 아트 완료로 간주하는 것.
- 8×8km 중 시작 마을 근처만 개선하고 **월드 전체**라고 보고하는 것.

---

## 10. 검증 매트릭스와 수치 기록

성능 검증은 최소 아래 조합을 포함한다. 목표 FPS/해상도는 사용자 확인 전 **60FPS/16.67ms를 비교용 가설**로만 둘 수 있으며 최종 합격값으로 독단 확정하지 않는다.

| 장면 | 에디터 Low | 에디터 Epic | PIE Epic | 패키지 High/Epic (가능 시) |
|---|---|---|---|---|
| V01 시작 마을 Grass/Material | O | O | O | O |
| V02 숲 가장자리/수관 원경 | O | O | O | O |
| V03 민둥산/암반/산림대 | O | O | O | O |
| V04 산악 전망대/고속 이동 | O | O | O | O |
| V05 수도 랜드마크/밀도 | O | O | O | O |
| V06 강·호수·다리 | O | O | O | O |

각 셀에 카메라 Transform, 그래픽 프리셋/해상도/Screen Percentage, WP/Data Layer 로딩 상태, 스크린샷, 생성 수량 또는 가시성 근거, CPU/GPU 프레임타임, Texture Pool/Required/NonStreaming, GPU Memory, 발견된 팝인/부작용을 기록한다. 전부를 한 번에 측정 못하면 **검증 불가** 표시와 이유를 남긴다.

품질 평가에는 단순 숫자뿐 아니라 아래 관찰 기준도 적용한다.

- 산은 중·원거리에서 암반·초지·수관 경계가 구분되는가?
- 플레이 시 저품질 에디터 화면보다 풀이 부족했던 원인을 입증·수정했는가?
- 숲과 도시의 원경 실루엣이 WP 셀 언로드 시에도 유지되는가?
- 수도에 도로 계층·중심 시설·큰 건물·실제 통행로가 존재하는가?
- 마을과 사냥터 사이가 기능적 경관으로 연결돼 있는가?
- 하천 물높이·강둑·다리·하구가 자연스럽게 이어지는가?
- 비파괴 수정이 기존 20개 장소, 퀘스트/스폰·NPC, 충돌·등산로를 깨뜨리지 않았는가?
- 사용한 그래픽 프리셋에서 프레임·GPU 메모리가 안정적으로 유지되는가?

---

## 11. 보고·진행 기록·인수인계 갱신

프로젝트의 기존 `인수인계.md`가 가리키는 기록 위치를 우선 사용한다. 아래 파일명은 **새 프로젝트를 만들어야 한다는 의미가 아닌 권장 문서 형태**다.

- `Docs/WorldQuality/01_BASELINE_DIAGNOSIS.md`: 환경/성능/품질 Baseline, CVar, 스크린샷, 실제 원인.
- `Docs/WorldQuality/02_ASSET_AND_SYSTEM_AUDIT.md`: 기존 에셋·C++·PCG·Landscape·Water·HLOD 재사용/부족 사항.
- `Docs/WorldQuality/03_IMPLEMENTATION_LEDGER.md`: 단계별 변경 파일·액터·그래프·설정, 원인·해결, 롤백 방법.
- `Docs/WorldQuality/04_VISUAL_REGRESSION.md`: 정해진 카메라 V01~V06의 Before/After 이미지 경로와 검증 조건.
- `Docs/WorldQuality/05_PERFORMANCE_AND_VRAM.md`: VRAM·Texture Pool 4096/6144/8192 비교, Lumen/HW RT 변형 테스트, 프레임타임.
- `Docs/WorldQuality/06_REMAINING_AND_HANDOFF.md`: 미해결 항목, 실패 원인, 다음 Codex 채팅에서 재개할 작업.

각 단계 결과를 **완료 / 부분 완료 / 미구현 / 검증 불가**로 표시한다. 실제 코드 컴파일 성공과 **실제 게임에서 예쁘고 안정적으로 보이는 것**은 다른 완료 기준이다. 안전하게 적용한 변경은 실제 빌드/에디터/PIE에서 확인한다. 완료 보고에는 스크린샷, 측정값, 파일 경로, 임시 설정과 영구 설정의 차이를 반드시 명시한다.

---

## 12. 최초 실행 지시 (새 채팅에서 이 문서를 전달한 직후)

1. 최신 `인수인계.md`와 현재 브랜치, 실제 에셋·월드 상태를 읽고 기존 작업과 충돌 여부를 확인한다.
2. **현재 Editor Low가 Grass 0으로 만들었는지**와 PIE에서 실제 풀/나무·광원·후처리 설정이 어떻게 달라지는지 같은 좌표에서 비교하고, 원거리 숲의 생성/스트리밍/컬링/HLOD 문제도 함께 분류한다.
3. texture over-budget의 실제 현재 수치와 GPU 메모리를 읽고, 6144MB 테스트를 통해 경고 해결과 다른 렌더링 자원의 메모리 여유를 확인한다. 임시 값은 사용자 고사양 프리셋으로 정식 적용하기 전 성능 검증을 거친다.
4. 가장 영향이 큰 **원거리 식생·산악 지면 문제부터 실제 수정**한다. 6개 검증 카메라 중 관련 장면에서 눈에 보이는 결과를 제출한다.
5. 그다음 수도 스카이라인/큰 건축물, 수변 Water, 마을 간 연결 환경을 기존 v4의 20개 장소 범위에 맞게 실제 보완한다.
6. 세션이 종료될 때 변경 사항과 다음 실행 작업을 프로젝트 내 기존 `인수인계.md` 또는 그 문서가 정한 링크된 파일에 남긴다. 기존 문서의 다른 작업 기록은 훼손하지 않는다.

**최종 목표는 에디터에서 잠깐 잘 보이는 한 장면이 아니라, 8×8km Lv.1~30 시작 권역 전체가 실제 플레이에서도 일관된 환경 밀도·원경 품질·도시/마을 정체성·수계·스트리밍 안정성을 갖는 것이다.**

---

## 13. 참고할 공식 UE 5.8 문서 (구현 전 API/버전 확인)

1. [Scalability Reference](https://dev.epicgames.com/documentation/ko-kr/unreal-engine/scalability-reference-for-unreal-engine) — Low/Epic에 따른 Grass/Foliage Density 차이.
2. [PCG Generation Modes](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine) — Partitioned/Runtime Generation.
3. [World Partition HLOD](https://dev.epicgames.com/documentation/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine) — 원거리 숲/메시 HLOD.
4. [Foliage Mode](https://dev.epicgames.com/documentation/unreal-engine/foliage-mode-in-unreal-engine) — Foliage Scaling/인스턴싱.
5. [Landscape Materials](https://dev.epicgames.com/documentation/unreal-engine/landscape-materials-in-unreal-engine) — Landscape Layer Blend.
6. [Grass Quick Start](https://dev.epicgames.com/documentation/unreal-engine/grass-quick-start-in-unreal-engine) — Landscape Grass Type/Grass Output.
7. [Water Bodies](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-body-actors-in-unreal-engine) — River/Lake/Ocean/Edit Layer 주의.
8. [Landscape Edit Layers](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-edit-layers-in-unreal-engine) — 비파괴 지형 변경.
9. [Texture Streaming Configuration](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-configuration-in-unreal-engine) — PoolSize/UseFixedPoolSize.
10. [Texture Streaming Metrics](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-metrics-in-unreal-engine) — `stat streaming` 해석.
11. [Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine) — HW/SW RT, DX12, 인스턴싱 메모리 비용.
12. [Ray Tracing Performance Guide](https://dev.epicgames.com/documentation/unreal-engine/ray-tracing-performance-guide-in-unreal-engine) — HW RT 최적화.
