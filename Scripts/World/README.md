# 월드 제작 도구

현재 `Content/World/Starter/Source`와 저장된 `L_StarterRegion`의 제작·확인 도구다. 시작점과 미완료 상태는 [인수인계](../../Docs/World/WORLD_HANDOFF_2026-10-18.md)를 따른다. Python 파일은 게임 런타임의 시작 훅이 아니다.

- `Generate-*` / `Plan-*`: 높이·도로·장소·배치 입력 작성. 기존 Source를 덮지 않는지 각 파일의 조건을 먼저 확인한다.
- `Build-*` / `Apply-StarterWorldBiomes.py` / `Save-StarterForests.py`: Source를 에디터의 실제 에셋·액터로 작성한다. 이미 배치된 월드를 일괄 재생성하는 용도로 실행하지 않는다.
- `Starter-*`: 지형 좌표, 건축·다리·습지 기하, 공간 계약 등 공통 제작 함수다.
- `Verify-*`: 저장본과 Source의 연결·기존 공간 계약을 확인한다. 수치 통과는 시각 품질 합격이 아니다.
- `WorldVisualHarness.py <새 label> --cameras panorama montane user_far`: 고정 Editor 화면과 설정 기록. `Capture-StarterPerformance.py`는 PIE 성능 캡처이며 별도 실행 조건이 있다.
- MCP 연결과 콘솔 입력은 `Scripts/Editor/World-MCP.py`, `WorldEditorConsole.py`를 사용한다. 연결 상태와 실행 완료는 별개다.

제작기 일부는 로컬 `Saved/Validation/StarterWorld` 체크포인트를 입력·중복 실행 방지에 사용한다. Saved는 Git 제외이므로 새 PC에서 초기 저작을 그대로 재실행할 수 있다고 보장하지 않는다. 현재 Source·Native 상태를 먼저 조사한다. 이미 적용한 일회성 이행·보정, 반복 진단/캡처, 철회한 물 시험, 퇴역 Region A 제작기는 커밋 전 정리에서 제거했다. HLOD 등록 수정 후 프록시 빌드·PIE 전환은 아직 미검증이며 기존 실패 경로를 재실행하지 않는다.
