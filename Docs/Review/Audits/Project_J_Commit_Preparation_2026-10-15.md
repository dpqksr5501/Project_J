# 2026-10-15 커밋 전 정리·검증 경계

기준 HEAD는 `855a1c8e111c94a5dffdd08ad05c68bdea2accec`, 브랜치는 `codex/runtime-retarget-profiles`다. 사용자의 최신 요청에 따라 계속 작업의 실제 수정·검증·기록을 한글 커밋으로 보존한다. 모든 소스/에셋에 불필요한 로직이 전혀 없다는 완료 선언은 아니다.

## 실제 제거한 임시 실행 도구

`Scripts/Audit/`의 다음 7개는 완료된 세션의 MCP 주소·세션 폴더 또는 이미 제거된 특정 graph를 전제로 하는 일회 도구다. 제품·영구 회귀 caller와 문서 직접 링크가 없음을 확인했다. 당시 요청/응답과 전후 해시는 보존하며, 도구 원본은 `Saved/Validation/CommitPreparation_20261015/RetiredSessionTools/`에 SHA 대조 후 보관했다. [정리 영수증](AssetArchitectureEvidence_2026-10-15/CommitPreparation.json)에 원래 경로·SHA·바이트·보관 경로를 기록한다.

- `AssetCleanupMCPHost.py`: 이번 전용 idle editor 종료 호스트.
- `Invoke-AssetCleanupMCP.py`: 종료된 로컬 MCP 세션 호출기.
- `Prepare-AssetCleanupQueries.py`, `Prepare-AssetGraphNodeQuery.py`: 이번 Query/Nodes 요청 생성기.
- `MCP-ReadCombatInterface.py`: 특정 BP 일회 조회.
- `MCP-RetireCombatPlaceholders.py`, `MCP-RetireDuplicateMMWriter.py`: 이미 제거한 상수 override/중복 MM writer 전용 수정기.

## 유지한 이유와 남은 구조 과제

영구 회귀 시험과 실제 caller가 있는 fixture, Editor 제작 validator, 테스트·샘플 BP/DA를 보존했다. native 호출이 사용하는 ReturnedChooserTraversal·MoveIntentState·EquipmentStats GE는 제품 코드다. 일괄 삭제하지 않는다. 미사용 여부·제작 의도가 불명확한 에셋도 유지한다.

`HandoverContractActor`, `InputLeaseCallbackFixture`, `NPCCallbackFixture`, `AnimationContinuationFixture`의 UCLASS는 runtime 모듈의 Private/Tests에 있다. 시험 본문 guard와 별개로 reflection class/CDO가 제품에 포함될 수 있다. 실제 회귀 caller가 있어 삭제하지 않았다. 제품에서 reflection까지 제외하려면 별도 시험 모듈 배치·UHT/Game 빌드 대조가 필요하며 이번 임시 파일 정리에서 완료 처리하지 않는다.

재검증/분석 도구와 역사적 증거 생성기를 구분한 [도구 안내](../../../Scripts/Audit/README.md)를 추가했다. 기존 실패·성공 원자료와 당시 SourceHashes를 수정하지 않는다. `Saved/`, `Intermediate/`, `Binaries/`, DDC·개인 UserDir·로그·MCP 세션 헤더는 제품 커밋에 넣지 않는다. 정해진 portable 증거 폴더만 해시를 대조해 포함한다. 두 증거 폴더는 Git 줄바꿈 변환을 제외해 기록 바이트를 보존한다.

## 검증의 시점과 범위

- 최신 Editor 빌드 `R16_AssetMMCallbackTestBuild2`: 7 actions / 13.52초 / 정상 종료 0. 첫 시험 코드의 private 엔진 getter 컴파일 실패는 보존하고 public FPoseLink 사용으로 교정했다.
- 최신 영향 범위 `R16_AssetArchitectureFinalFocused1`: 65 clean, warning/failure/notrun/inprocess 0. 실제 재생·MM database 수명·전투 인터페이스·성장/UI/인계 등 선택 범위이며 전체 시스템 전수 검증 수가 아니다.
- 에셋 fresh reload/compile `Reload2`와 `Cook2`: 변경 BP/ABP를 포함해 1363 cooked, 플랫폼 제외 7, 정상 종료 0. 기존 GameplayCuePath 경고 1개는 남아 있다. 새 Game 실행 파일·패키지·원격 전체 검증을 의미하지 않는다.
- 이전 통합389·Game/Cook/패키지·전투·원격 기록은 CA17–18 당시 근거다. 후속 수정의 최신 검증으로 승격하지 않는다.
- 커밋 직전 정리는 도구·문서·증거의 Git 포함 경계만 바꾼다. 검증된 native 소스·에셋 바이트를 대조하고, 스크립트 구문·증거 SHA·staged diff를 확인한다. 제품 코드 변경이 없으므로 긴 빌드를 반복하지 않는다.

커밋 전 바이트 대조는 [3391개 소스·에셋·설정/플러그인 파일과 Editor binary 9개](AssetArchitectureEvidence_2026-10-15/CommitNativeAssetHashCheck.json)의 추가·누락·SHA 불일치가 모두 0이었다. Python 31개 AST와 PowerShell 30개 구문 검사도 오류 0이었다. 기존 index가 가리키는 portable 사본 1264개의 SHA를 확인했고, 제품 실행 파일·개인 UserDir·세션 헤더를 증거 커밋에 포함하지 않는다. Git 무시 규칙의 `Saved/` 이름 때문에 증거 내부 사본이 빠지지 않도록 두 증거 폴더의 정확한 파일 경로를 명시해 포함한다.

기존에 들어 있던 인계문 `Project_J_Complete_Audit_Continuation_2026-10-14.md`의 요청문 삭제 diff는 이번 구현과 별개다. 원상복구하지 않고 이번 커밋에서 제외한다. 커밋 이후에도 이 사용자 변경은 working tree에 남긴다.

CPU 증가·렌더 차이, 전체 함수/에셋 속성·환경 정밀 대조의 미완료, 테스트 fixture의 제품 reflection 포함 가능성은 남은 문제다. 성능 반복 비교·대규모 부하/원격·전수 시각 품질·운영 문제 해결은 최신 범위 변경에 따라 보류한다. 추가 push·병합·컴퓨터 종료는 수행하지 않는다.
