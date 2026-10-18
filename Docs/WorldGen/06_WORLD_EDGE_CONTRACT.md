# 월드 경계 계약 — Starter 작성 중

상태: 기존 7개 포트의 Source/Native 계약 유지. 인접 Realm 생성·연속 지형/원경·서버 이동 증명은 미완료다. 연결 ID는 장소 ID·WP cell ID·서버 zone ID와 별개다.

원천은 `Content/World/Starter/Source/StarterWorld.plan.json`의 connections이며 `StarterWorld.edges.json`의 connections와 Native `DA_StarterRegion.Connections`에 반영한다. Source는 남서 원점의 m, Native는 Manifest의 origin/units를 적용한 cm다. 높이 경계 배열은 보존한 R16의 샘플 계약이며 포트를 옮겼다고 배열을 재생성하지 않는다.

| 안정 연결 ID | 경계 / 종류 | Source XY(m) | 폭(m) | 현재 상태 |
|---|---|---|---:|---|
| E_N_Road | 북 / 도로 | 3352,8064 | 6 | S09 캠프 관문과 연결; 남쪽 대도로·인접 Realm 미검증 |
| E_E_Road | 동 / 도로 | 8064,7240 | 6 | 후보 |
| E_W_Road | 서 / 도로 | 0,5670 | 6 | 후보 |
| E_S_Sea | 남 / 해상 | 6860,0 | 240 | 후보 |
| E_R_Main | 북 / 강 | 4020,8064 | 42 | 수계 후보 |
| E_R_LakeIn | 북 / 강 | 1900,8064 | 17 | 수계 후보 |
| E_R_East | 동 / 강 | 8064,6490 | 21 | 수계 후보 |

## 북쪽 육상 포트의 조정

기존 (3700,8064,약711.06)m은 폭 6m 양끝의 높이 차/폭이 약143%, 경계 안쪽 12m 평균 종단 경사가 약57%였다. 원본 산을 깎지 않고 통행 후보를 확보하기 위해 **동일 E_N_Road ID**를 (3352,8064,891.3125)m으로 옮겼다. 새 포트의 폭 방향 평균 경사는 약3.65%, 안쪽 12m 평균 경사는 약7.81%다. 이 수치는 전체 연결로의 폭·급회전·탈것 합격이 아니다.

기존 19개 Place 구조체와 다른 6개 Connection 구조체 전체 값, 원래 높이/경계 배열은 보존한다. `Road_NorthExit`의 끝을 캠프 접근 후보에 연결하고 이 도로의 Native Review 리본도 같은 Source로 교체했다. 남쪽의 기존 급경사 구간은 여전히 `Candidate.SlopeUnverified`이며 새 포트와의 전 구간 통행을 보증하지 않는다. Native 데이터와 숨은 옛 끝점이 서로 다른 활성 경로로 남지 않도록 함께 정리했다.

이전 Source 문서 3개는 `Saved/StarterWorld/BeforeNorthCamp`, Native 전체 값과 이전 Review 경로는 `north-camp-registration.json`에 기록했다. 등록 시 재질 이름 확인 오류는 실제 변경 상태를 확인한 제한 재개로 복구했으며 `north-camp-registration-before-material-fix.json`에 남겼다. 완료된 등록 스크립트는 다시 실행하지 않는다.

## 다음 지역에서 검증할 사항

- 북쪽 이웃은 현재 Starter 북쪽 높이 배열·도로 폭/방향·수계 높이에 맞춰 경계를 생성해야 한다. 다음 산맥을 현재 포트 앞의 허공을 가리는 장식만으로 대체하지 않는다.
- S09 관문 통행·확장 마커는 저작 연결을 증명한다. 인접 레벨 저장/로드, HLOD 원경, 경계 횡단 이동, 서버 handover, Nav/탈것, 패키지 경로는 별도 검증한다.
- Source plan / edge connection / Native connection / Expansion_E_N_Road의 좌표 일치와 기존 값 보존은 `Verify-StarterNorthCamp.py`로 확인한다. 변경된 장소와 연결의 소유권은 기존 WorldState/Definition에 유지한다.
