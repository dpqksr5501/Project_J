# 스펠블레이드 망토·머리카락 충돌 최적화

기존 `BP_Spellblade`의 기본 에셋 경로에서 아래 구성을 사용한다. 경로와 참조를 유지해 직접 재생과 리타깃을 모두 지원한다.

이 보고서의 비교는 망토·머리카락 충돌 에셋 채택 단계다. 이후 소유 망토를 유지하면서 원격에만 경량 물리 메시를 적용했다. [원격 망토 적용과 필드·던전 실접속 검증](../README.md#스펠블레이드-원격-경량-망토-적용--2026-11-24)

| 파츠 | 기본 에셋 | 채택한 변경 |
| --- | --- | --- |
| 망토 | `SK_Spellblade_cloak`의 세 Cloth 섹션 → `PA_Spellblade_ClothCollision` | 이미 제작된 망토용 캡슐만 남겨 16→10. 소유 메시, Cloth 섹션과 기존 품질 정책 유지 |
| 머리카락 | `ABP_Spellblade_Body`와 `ABP_Spellblade_Retarget` → `PA_Spellblade_Hair` | 동적 머리카락 4개와 머리 앵커 1개, 시뮬레이션 관절 4개 유지. 동적 바디와 충돌이 꺼진 캡슐 44개 제거 |
| 치마 | 동일 그래프 → `PA_Spellblade_Skirt` | 기존 구성 유지 |

머리카락은 이전 48체 비교에서 시뮬레이션 호출당 평균 44.06→35.38µs였다. 동시 공격 전체 프레임 p95는 25.54→25.50ms였다. 1체 근접 비교에서 공격·회전·실제 착지의 새 관통이나 실루엣 변화가 보이지 않았고, 짝지은 900개 포즈에서 최대 위치 차이 0cm, 회전 차이 약 0.000004도를 기록했다.

## 200체 비교

Ryzen 7 9800X3D·RX 9070 XT의 Development Editor D3D12 로컬 뷰포트, 1280×720 출력, 동일 Spellblade 200체, 로컬 소유자 1명, 네트워크 연결 0개다. 96체 예비 시험 이후 적용 전 기본, 두 사본 동시 사용, 적용 후 기본을 각각 한 번 실행했다. 모든 선택 구간에서 직접 재생, 최근 렌더, 활성 망토 시뮬레이션이 각각 200체로 확인됐다.

| 지표 | 적용 전 기본 | 최적화 사본 두 파츠 | 적용 후 기본 |
| --- | ---: | ---: | ---: |
| 이동 프레임 p95 | 60.81ms | 54.98ms | 54.64ms |
| 점프·착지 프레임 p95 | 62.88ms | 52.91ms | 56.07ms |
| 전체 공격 단계 프레임 p95 | 79.05ms | 70.97ms | 69.18ms |
| 200체 동시 공격 프레임 p95 | 94.00ms | 86.32ms | 86.09ms |
| 동시 공격 머리카락 호출당 평균 | 56.95µs | 39.97µs | 39.64µs |
| 동시 공격 망토 작업당 평균 | 1.84ms | 1.64ms | 1.62ms |
| 동시 공격 GT의 Cloth 완료 대기 p95 | 12.35ms | 8.42ms | 8.31ms |
| 전체 Editor 프로세스 Private peak | 9.66GiB | 9.77GiB | 9.59GiB |

캡처와 후속 두 프레임을 제외한 동시 공격 표본은 순서대로 12/14/14개였다. 적용 후 기본 경로의 동시 공격 프레임 p95는 적용 전보다 약 8% 줄었으며 GT p95는 79.00ms였다. 병렬 작업 시간과 프레임 시간은 별개다. 적용 후 실행의 최소 표본 여유는 RAM 11.34GiB, 커밋 12.17GiB였다. 이 기준은 로컬 화면의 200체 부하를 나타낸다.

## 기본 경로 검증

- 두 기존 Physics Asset만 저장했다. 3개 Cloth 섹션의 PA 연결, 메시·두 애니메이션 블루프린트·치마·캐릭터 BP와 시험 사본의 파일 지문을 확인했다.
- Editor 증분 빌드가 통과했다. 새 Editor 프로세스에서 저장된 세 에셋을 다시 읽어 Data Validation 3개가 유효했다. 스펠블레이드 망토의 기존 Mesh LOD 수동 검수 안내가 1건 있었다.
- `ProjectJ.Retarget.HairCollisionTrial` 및 `ProjectJ.Retarget.SecondaryPhysicsEquivalence`가 통과했다. 적용 후 900개 짝 비교에서도 위치 차이는 측정 정밀도 내에서 0cm, 최대 회전 차이는 약 0.000004도였다. PoseSearch 인덱스 준비 경고 2건을 기록했다.

## 재현과 이력

아래 명령은 기본 에셋 채택 이후의 실제 경로를 측정한다. 호스트 메모리 여유를 먼저 확인하고, 동일한 뷰포트와 품질 조건에서 실행한다.

```powershell
./Scripts/Validation/Measure-RenderedCrowd.ps1 -RunName SpellbladeSecondaryMotionCurrent -Count 200 `
    -LocalPlayer -Movement -CloakTrial Original -CloakGroundAttack -HairTrial Original `
    -MemoryReserveMiB 4096 -DisableEditorPython -Trace
```

- 현재 `Original` 옵션은 BP가 사용하는 저장된 기본 PA로 연결한다. 채택 이전 200체 기준선은 당시 manifest와 trace로 재현한다. 두 테스트 사본은 독립 품질 비교와 회귀에 사용한다.
- 200체 원자료는 `Saved/Validation/WholeProjectAudit_20261013/SpellbladeAccessoryOriginal200_20261124` 및 `SpellbladeAccessoryCombined200_20261124`다. 집계, 96체 시작 조건, trace 추출 자료는 `Saved/Validation/SpellbladeAccessoryCrowd200_20261124`에 있다.
- 적용 후 기본 경로 원자료는 `Saved/Validation/WholeProjectAudit_20261013/SpellbladeAccessoryProduction200_20261124`다. 검증 결과·요약·복구 대상과 SHA-256은 `Saved/Validation/SpellbladeSecondaryMotionPromotion_20261124`에 있다. 교체 전 두 PA의 원본은 기존 중앙 보관소의 에셋 백업에 보존했다.
- 이전 24체 망토 단독과 48체 머리카락 단독 비교는 파츠별 원인 분석 이력이다. 중앙 보관본과 동일한 작업 폴더의 트레이스 링크 4개를 정리했다. 원본 트레이스와 SHA-256은 보존했고, 제거한 이전 경로는 `TraceAliasesRetired.json`에서 확인할 수 있다. 원본 패키지와 트레이스의 위치·복원은 [중앙 보관 규칙](../../Maintenance/Local_Archive_Policy.md)과 로컬 명세를 따른다.
- 더 많은 캐릭터와 의상 제작에 적용할 기준은 [보조 움직임 제작 가이드](../../Animation/Authoring/Secondary_Motion_Authoring.md)에 있다.
