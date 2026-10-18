# MMORPG Lv.1~30 첫 번째 8×8km 오픈월드: Codex 패키지 v3

이 패키지는 검은사막처럼 현실적인 지형·마을 밀도를 목표로 하는 **첫 번째 8×8km 스타팅 월드(Lv.1~30)** 제작을 위한 설계 자료다. 이는 이후 Lv.31~100 권역으로 확장되는 전체 MMORPG의 일부분이다.

## 새 Codex 대화 전달 방법

1. 이 ZIP을 언리얼 프로젝트의 읽기 가능한 문서 경로에 압축 해제한다 (예: `Docs/WorldGen/Starter_8x8_v3/`).
2. `WorldGen_Prompt_v3.md`를 읽고 **첫 요구 단계는 읽기 전용 감사/아키텍처 보고**로 시작하게 한다.
3. `ZONE_LAYOUT_LV1_30.json`과 **개별 이미지** `References/01_Starter_World_TopDown_NoLabels.png`를 함께 열어보게 한다.
4. `REAL_GAME_REFERENCES.md`에 있는 실제 게임 이미지 URL을 **하나씩** 열고 무엇을 관찰했는지 기록하게 한다. 인터넷 접근이 안 되면 이미지 파일을 사용자에게 요청한다.
5. 기존 사용자 에셋 경로, `.uproject`, 기존 C++/AI/GAS/PCG 경로, 최대 이동속도, 타깃 하드웨어를 조사한다.

## 파일 설명
- `WorldGen_Prompt_v3.md` — v2의 기능/성능/엔진 설계를 보존하면서 Lv.1~30 스타팅 지역을 반영한 통합 프롬프트.
- `ZONE_LAYOUT_LV1_30.json` — 도시·마을·9개 사냥터·보스 2종·월드 경계 연결점의 기획 좌표 및 난이도 데이터.
- `REAL_GAME_REFERENCES.md` — 실제 출시 게임의 스크린샷 원본/직접 이미지 링크. 패키지에 해당 이미지 파일은 넣지 않았다.
- `References/01_Starter_World_TopDown_NoLabels.png` — 하나의 탑뷰 이미지 (AI 콘셉트).
- `References/02_Boss_Arena_Schematic.png` — 보스 아레나 도식.
- `References/03_Hunting_Ground_Schematic.png` — 사냥터 배치 도식.
- `References/04_Previous_Levels_Before.jpg` — 기존 작업 전/후 비교 원본.

**주의:** 탑뷰 AI 그림은 정확한 지도/고도맵이 아니다. JSON도 기획용 임시 좌표다. Landscape·수계·이동 경로와 성능을 실측하여 변경할 수 있다.
