# Project_J 재감사 — 현재 변경·검증 결과

현재 RA01–27 변경본은 **64개 텍스트 변경(생산57·시험/fixture4·검증 script3)**과 관련 빈 장비 placeholder DA2개 정리다. 기능별25개 소스 리뷰 단위에 수정파일62개를 모두 연결했고, 추가 회귀파일2개와 에셋 변경은 별도로 기록했다. 기준선은 이전 사용자 미커밋 작업을 포함한 RA 이전 상태다. 이전 DS/SI/리타깃 변경을 이번64개로 중복 계산하지 않는다. 커밋·push·병합·컴퓨터 종료는 하지 않았다.

[변경별 원인과 재현](Project_J_Reaudit_2026-10-14.md), [실행·상태·모듈 지도](Project_J_Reaudit_Map_2026-10-14.md), [상세 결과와 남은 문제](Project_J_Reaudit_Remaining_2026-10-14.md)를 함께 읽는다. 전체 소스607개 목록·추가 구현 본문141개·명명된 헤더/호출/계약 검토는 모든 함수·분기·에셋의 전수 정밀 완료가 아니다. 검토한 것과 실제 개선한 것을 구분한다.

## 이번 재개에서 실제 정리한 구조

- 공유 GT callback 계약: Mass active step, TargetScoring/VisualAsset/NPCPath delivery, NPCDecision action/result/stop, PresentationBudget apply에서 실행 참가자 UObject를 동기 범위에 보유한다. worker는 값 snapshot만 사용하고 epoch/token/FIFO/취소 완료/module shutdown join은 유지한다.
- MM의 미사용 경로: 호출 없는 Hidden/Mid/Far cadence getter3개, 과거 early-transition Begin/End·depth·초기화·snapshot OR 분기를 제거했다. 현재 held clip·gait·snapshot revision이 전이 권한을 소유한다. Blueprint getter와 실제 notify는 유지한다.
- GAS의 미사용 분기: 선언·정의만 남은 비반영 direct input-tag activation loop를 제거했다. 실제 pressed/released·GAS 활성화·서버 권위/소유자 예측 경로는 유지한다.
- 검증의 부작용: “읽기” 비교가 UPoseSearchDatabase 복제·entry 수정을 통해 derived index 작업에 참여했다. 비교용 값 배열로 바꾸고 canonical index의 readiness·entry range·animation 연결을 확인했다. 캐시 삭제·강제 reset·warning 억제를 추가하지 않았다.
- 검증 host의 범위: game/server 실행은 공식 DisablePython으로 editor 전용 Python startup을 제외한다. 일반 Editor/MCP 설정은 유지한다. 기존 오류45행→부분 제외15행→최신0행의 기록을 각각 보존했다.

기존 RA01–19는 delegate/tag/input/task 수명, UI projection/reconstruction/persistence, movement/ground-contact numeric contract, follower 역할 resolver, Mass 종료 저장소, 새 play admission과 빈 중복 DA 정리를 포함한다. 이름·파일 규모·토큰 참조 횟수만으로 공개 확장 계약이나 반영/BP 함수를 삭제하지 않았다.

## 변경본에 맞춘 실행 결과

| 검증 | 결과 | 범위 |
| --- | --- | --- |
| 직접 Editor UBT | 최종 ValidationIsolationBuild1 266.99초·정상 성공 | 최신 RA27 소스650·project Editor DLL8+modules1 해시 고정 |
| 최신 전체 native | **352개(332 clean·20 warning), 실패/미실행/inProcess0·665.34초·exit0** | 재감사20개 모두 clean. 원래 경고20을 무해함의 증명으로 승격하지 않음 |
| Index 새 프로세스3회 | 각각1 clean·exit0, raw corrupted-index 로그0 | canonical Cycle70 index asset·5,288 pose, Forward14 entry. 수정 전 단독 시험은 clean 집계 뒤 raw 경고를 남겼음 |
| 실제 원격 반복3회 | 실제 localhost2 socket·NPC200, lag50ms/loss20%; 각3 process exit0·timeout0·Python startup 오류0 | 각 실행의 client2×actor2×trial3인12 묶음 모두 R/target/end 관찰. WAN·200socket 검증 아님 |
| Game/Cook/패키지 | GameBuild4 203.57초 성공, Cook3 0error/기존1warning·35.94초 commandlet, Packaged3 stage 정상 | production RA25. RA27과의 차이는 Editor 시험1·network wrapper1뿐임을 전체 해시 대조 |
| cooked 피해·전투·UI | CookedDamage3 통과, 실제 입력/GAS/montage/notify/해제3회 통과, UI1280×720/1920×1080 동적 부하 통과 | 다른 개발자의 저장/UI 설정을 덮어쓰지 않도록 실행별 UserDir 사용 |
| 실제 렌더 | body200·동시 montage200·D3D12 offscreen 정상 종료 | 단순 camera/light, 실제 원격 접속0. 가까운 관절/의상·모든 장비/클립 품질 보증 아님 |
| 일반 Editor MCP | 공개 Cycle/Forward schema·normalization, Master/Body native 부모·Body profile 연결 정상 | 에셋 저장0. private entry 배열 읽기 거절 및 CDO property 대상 불일치도 원응답 보존; 우회하지 않음 |
| 독립 Server UBT | target 존재, 정상 종료 exit6 | 첫 오류: 현재 엔진 배포본이 Server target을 지원하지 않음. 엔진 파일/경로 변경 없음. Editor-Cmd server-mode 검증과 구분 |

## 성능과 보존

200명 RA13 full binary 교대 A/B/B/A 비교64행에는 일부 CPU 평균 증가가 남았다. 별도16 traced region은 반대 방향이며 실제 ABA/MM 수행량 차이도 있어 원인을 확정하지 못했다. **CPU 증가 해결 완료가 아니다.** 최신 Render3의 실제200 montage167프레임은27.1149ms(36.88FPS),GPU10.6211ms이고 복귀219프레임은15.8822ms(62.96FPS)다. 이전 Render2보다 낮은 FPS다. 교대 렌더 비교·열/스케줄링 통제 없이 개선이나 코드 회귀로 단정하지 않는다. UI GT 평균/p95는0.5737/0.8108ms와0.5900/0.8532ms이다. 상세 CSV·phase·trace·argument·binary/source hash로 결과를 구분한다.

기존 Body ABP·RTG·현재 인계 문서와 앞선 공격 DA 교정10개, 합13개 해시가 일치한다. 소스650·Editor9도 최신 고정본과 일치한다. 지속 곡선0.35초·100cm·3도·85%를 유지한다. RTG/전투/장비/로코모션·단발/MM 복귀 등 이전 정상 계약의 회귀를 포함했다. 모든 시각 조합을 검증한 것으로 표시하지 않는다.

## 남아 있는 검토·검증

CPU 증가 원인과 notify 동시 시작 비용, LowerBody review installer의 실패 원자성, WeaponMotion/OneShot tuning 등 추가 malformed numeric 계약, 동일 정책에서 follower 교체, 실제 GameplayCue asset closure, 모든 source 함수/헤더/시험 분기, 모든 clip/cloth/audio/접근성/장비 조합, 실제 backend·지속 저장·분산 handover·WAN·독립 Server 운영·200socket은 완료하지 않았다. 잠재 위험과 실제 측정/확인 결함, 운영 확장 제안을 [상세 장부](Project_J_Reaudit_Remaining_2026-10-14.md)에 분리했다.

원자료는 `Saved/Validation/Reaudit_20261014` 및 실행별 명시된 경로에 보존한다. 이전 실패 fixture·첫 compiler 오류·unsupported MCP 응답·warning·포함되지 않은 범위도 삭제하지 않는다. 배포 가능한 원자료 묶음은 [ReauditEvidence_2026-10-14](ReauditEvidence_2026-10-14/Manifest.json)의 hash 목록으로 확인한다. 전체 trace와 binary는 용량 때문에 Saved에 별도 보관하며 경로·해시를 남긴다.
