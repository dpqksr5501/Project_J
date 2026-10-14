# 구조 개선 근거 — 2026-10-13

[변경 이유·소유권 지도·검토만 한 범위](../Project_J_Structural_Improvement_2026-10-13.md)와 함께 읽는다. 앞선 미커밋 개선 위의 구조 변경 SI-01~08이며, 기존 CPU 회귀 해결이나 전체 에셋 품질 인증을 뜻하지 않는다.

| 자료 | 범위·해석 |
| --- | --- |
| [기준선 대비 Source 변경](SourceChangesFromAuditBaseline.json) · [현재 Source/Config 해시](SourceHashes.json) | 593파일 SHA256, 기준선 이후 변경34파일. 이전 감사 변경 전체를 이번 구조 정리의 변경량으로 다시 계산하지 않는다 |
| [현재 바이너리 해시](FinalBinaryHashes.json) · [보존](Preservation.json) | 같은 최종 Editor8모듈·Game 실행 파일, 인계 문서 사용자 변경과 BodyABP·Content 보존 |
| [빌드·Cook·stage](BuildCookStage.json) | 성공과 최초 compiler 오류를 함께 보존. UBT 직접 실행, 실패도 정상 종료. source 구현 오류·obj 접근 오류를 구분 |
| [자동화 판정](AutomationResults.json) · [검사 이름](AutomationCoverage.json) · [경고 예시](AutomationWarnings.json) | WholeFinal317: 성공297+경고 포함 성공20, 실패·미실행0. 자동화가 실제 원격·전체 콘텐츠/품질을 대신하지 않음 |
| [MCP 조회](McpEvidence.json) | 실제 layer/profile 및 row/tile CDO 연결. 개별 원본 JSON의 tool 지원/속성명 실패를 성공으로 재분류하지 않음 |
| [원격 판정](NetworkResults.json) · [소유권·입력 로그 발췌](NetworkOwnershipExcerpts.json) | 최초 실패와 같은50ms·20%loss 수정 후 성공. 서버+원격2client·200nativeNPC,200실접속이 아님. 원본 manifest와 client 관찰 CSV도 각 실행 폴더에 복사 |
| [UI 교대 및 최종 패키지](UIComparison.json) | A1/B1/A2/B2 각600개 수치 표본, cap60·VSync0·10초·1000행/24buff/4windows/10slots·5Hz delta. baseline/current 빌드 방식과 시간 차이로 개선 인과성 미확정. 해상도별 CSV를 별도 보존 |
| [CPU 전체 원자료](CPU200.json) · [16행](CPU200Rows.csv) · [요약](CPU200Summary.json) | NullRHI world tick wall time,60Hz·warm120/measured120·ABA2ms·각2회. 렌더 FPS/GPU·remote capacity 아님 |
| [구조 개선 전후64행](AlternatingCPU.csv) · [판정](AlternatingCPUResults.json) · [요약](AlternatingCPUSummary.json) · [최종 모듈 복원](RestoredEditorSet.json) | A1/B1/A2/B2·같은 전체 non-unity8모듈, asset/toolchain·200명·각2회. 큰 B2 Hidden R0도 포함. compiled source와 baseline 실행 당시 working source가 다름을 각 phase의 provenance에 명시 |
| [렌더 수치](RenderedStatistics.json) · [최종 idle](StructuralFinalCrowd200/phase2.png) · [최종 R](StructuralFinalCrowd200/phase3.png) | 실제 BP construction/body200, D3D12 offscreen1280×720·cap0/VSync0·간단한 Entry/광원/카메라·원격0. 요청 capture 행 제외; 후속 캡처 영향과 GPU 지연 counter 한계 남음. 근접 손/발·연속 영상·오디오 품질 인증 아님 |
| [패키지 전투](CookedCombatEvidence.json) | native character에 authored visuals를 연결한3회 실제 input/GAS/montage/notify/unequip. 실제 BP/body construction은 별도200표시 fixture가 담당 |
| [중복 후보 전](BaselineExactCloneCandidates.json) · [후](CurrentExactCloneCandidates.json) | 의미 줄12개·300문자 이상 exact match2→0. 모든 semantic/짧은 중복 제거를 의미하지 않음 |
| [미사용 후보](PrivateMethodUsageCandidates.json) · [파일 규모 전](BaselineProductionSizeInventory.json) · [후](CurrentProductionSizeInventory.json) | 비반영·비virtual private 함수2개 삭제 근거. public/BP/serialized extension을 grep만으로 제거하지 않음 |

UI CSV는 원본 UE CSV의 numeric FrameTime 행만 추렸다. 반복 header/footer는 제외하고 해당 범위의 UI/GT 열을 그대로 보존했다. CPU 원본은 tick/observer/공격 actor 배열까지 포함한다. p95는 정렬 표본의 `(n-1)*0.95` 선형 보간이다. worker 누적 timer나 nested timer를 wall time에 더하지 않는다. 이전 같은 컴파일 방식의 CPU 교대와 단계 trace는 [앞선 근거](../Evidence_2026-10-13/README.md)에 그대로 있다.

원본 로그·trace·패키지·DLL 전체는 로컬 `Saved/Validation/StructuralAudit_20261013` 및 실행별 Saved 폴더에 있다. Saved는 Git ignore이므로 새 clone에 자동 전달되지 않는다. 이 폴더의 복사한 JSON/CSV/PNG와 원본 SHA256·arguments·manifest를 같이 사용한다. 최종 Editor8모듈과 modules 파일은 로컬 `FinalEditorBinaries`에 한 세트로 보존했다. ABI가 다른 일부 DLL만 교체하지 않는다.

자료는 미커밋 상태다. 이번 작업에서 commit·push·merge·컴퓨터 종료는 수행하지 않았다.
