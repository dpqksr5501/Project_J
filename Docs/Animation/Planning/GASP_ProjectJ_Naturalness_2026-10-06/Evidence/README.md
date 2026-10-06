# 원본 근거 추출물

이 폴더는 조사에서 확보한 데이터의 읽기 전용 복사본을 보존한다. 원본 `.uasset`를 수정/저장/컴파일한 것이 아니다.

## 자료

- `ABP_Humanoid_Master.AnimGraph.clipboard.txt`: 분석 중 clipboard로 확보한 운영 Master 그래프 텍스트. 노드 선언/설정/핀/중첩 그래프를 포함한다. 대형 원문이며 통합PDF에 그대로 나열하지 않는다. G/A 결론은 원문의 활성연결과 에디터 Details를 함께 읽어 도출했다.
- `mcp_properties.json`: 분석 중 functions 메모리에 남아 있는 관련Chooser ColumnsStructs 및 4PSD search settings의 read-only MCP 결과. 성공/실패와 refPath를 보존한다. 에셋전체dump가 아니다.
- `file_snapshot.json`: 문서 포장 시점의 관련로컬파일 size/mtime/SHA256, git dirty 상태. 이것은 원래 그래프열람 순간의 hash라고 주장하지 않는다.
- `MovingTurnTrace_PIE1053.Analysis.json`: 기존 Saved/Validation의 8개 반전 episode 분석 복사본. 당시 Strafe schema는 PSS_Player이며 현재 PSS_Combat 연결과 다르다. 새 실행 결과가 아니다.
- `pdf_validation.json`: 통합 PDF의 지문·페이지 수·전체 렌더링·텍스트 영역 검사·수동 시각 검수 결과. 게임플레이 검증과 무관하다.

GASP의 모든 raw graph, 에디터 screenshot, 당시 full tool transcript를 이 폴더가 모두 보유한 것은 아니다. 없는원문을 새로창작하지 않았다. GASP 상세값은 02와 01의 관찰장부를 기준으로 후속에디터에서 필요항목만 재확인한다.

## 읽는 주의

graph text의 NodePos/NodeGuid는 시각/식별 메타데이터다. node존재와실제output기여를 혼동하지 않는다. declaration과property initialization이 분리되어 있을 수 있다. parent/nested path, LinkedTo, function binding을 함께 확인한다.

MCP의 Column bDisabled는 Row Disable과다르다. 이자료의UseMM=true두FallOff행은 에디터에서행Disable을확인했다. ResultsStructs/DisabledRows 등 조회되지않은부분을 이JSON이복원한다고생각하지않는다.

Raw자료에 현재프로젝트의이름/경로/graph내용이 포함되므로 외부공유전 내용을 검토한다. 보고서에는 machine-specific경로를 명시하고 source/assets와timestamps가현재와같은지확인한다.
