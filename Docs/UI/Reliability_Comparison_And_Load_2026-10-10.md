# UI 저장 안정성·장비 비교·부하 검증

작업 기준점은 `cf557c50`(MMORPG UI 구현과 애니메이션 복구 작업 기준점 저장)이다. 이후 변경은 UI 저장, 장비 보너스 표시, 인벤토리 요청 수명, 퀵슬롯 집계와 검증에 한정한다. 기존 애니메이션·콤보 작업은 기준점에 보존했다.

## 로컬 설정 저장 계약

`UProject_JUILayoutSettings`는 LocalPlayer의 기기 설정만 저장한다. 캐릭터의 인벤토리·장비·GAS 상태·경험치 영구 저장은 별도다.

- 변경은 즉시 메모리에 반영하고 첫 변경 후 0.75초 동안 모아 저장한다. World와 관계없는 CoreTicker를 사용한다.
- 작은 파일을 동기 저장한다. 정상 종료의 OnPreExit와 Subsystem Deinitialize에서 남은 변경을 FlushNow로 저장한다.
- 정상 primary를 `_Backup` 슬롯에 먼저 복사한다. 백업 저장 실패 시 primary 덮어쓰기를 진행하지 않는다. primary를 읽을 수 없으면 정상 backup 또는 기본값으로 복구하고 상태를 표시한다.
- 실패한 변경은 메모리에 유지한다. 자동 재시도 간격은 2/4/8초이며 이후 UI 설정 창에서 ‘저장 다시 시도’를 사용할 수 있다. 메모리 적용과 디스크 저장 완료 문구를 구분한다.
- v1을 v2로 이관한다. 빈 키·비유한 좌표·잘못된 HUD 항목만 제외하고 좌표는 0~1로 제한한다. 잘못된 키 설정은 기본값으로 복구한다.
- 지원 버전보다 최신 파일을 발견하면 덮어쓰지 않는다. 현재 변경은 이번 실행에만 적용한다.
- 같은 프로세스의 PIE 화면은 최신 파일에 자신의 변경 필드만 합친다. 같은 필드의 충돌은 마지막 저장값이 우선한다. 별도 OS 프로세스의 동시 저장은 파일 잠금/트랜잭션으로 보장하지 않는다.

강제 종료·전원 차단에서 마지막 0.75초 변경의 저장을 보장하지 않는다. 백업은 이전 정상 설정으로 돌아가기 위한 장치이며 시스템 수준의 원자적 파일 교체는 제공하지 않는다.

## 장비 비교와 아트 교체

`FProject_JEquipmentComparison`과 행을 BlueprintReadOnly로 노출한다. 텍스트 툴팁, 별도 비교 패널, 향후 UI 에셋이 같은 결과를 사용한다. 시험 장착하거나 GAS 효과를 임시 적용하여 계산하지 않는다.

| 런타임 정책 | 표시 계약 |
|---|---|
| StatModifiersOnly | 고정 가산 보너스 합계와 현재 장비의 고정 보너스 차이 |
| GameplayEffectsThenStatModifiers, 지속 효과 없음 | 런타임 고정 대체 보너스와 동일한 비교 |
| GameplayEffectsThenStatModifiers, 지속 효과 존재 | 고정값은 ‘효과 미적용 시 대체’로 표시, 확정 차이는 숨김 |
| GameplayEffectsOnly | 고정 대체 값을 광고하지 않음. 지속 효과가 있으면 확정 차이 숨김 |
| null/Instant 장비 효과 | 런타임과 동일하게 제외 |

현재 장비 쪽이 조건부여도 확정 차이를 표시하지 않는다. 교체로 잃는 보너스는 음수 차이에 포함한다. 비유한 modifier와 거의 0인 modifier를 제외하고 같은 능력치의 modifier는 합산한다.

최종 캐릭터 능력치 예측은 아니다. 다른 GAS 효과, attribute clamp, 스킬·패시브, 면역·스택·조건부 계산은 수치 비교에 포함하지 않는다. 현재 장비 이름/아이템 레벨, 잠금·장착·서버 처리 상태를 함께 표시한다. 정의에 없는 직업·레벨 제한을 만들지 않는다.

## 요청 수명과 갱신 비용

ViewModel은 요청 종류(장비/사용/가방), GUID, 소스 세대를 구분한다. 잘못된 종류나 과거 GUID 응답은 pending을 풀지 않는다. 타임아웃은 5초이며 늦은 응답은 상태 문구를 덮어쓰지 않는다. 이후 복제 데이터는 정상 갱신한다. 재시도는 새 요청이며 타임아웃은 서버 취소를 의미하지 않는다.

Unbind 시 등록한 World에서 요청·갱신 타이머를 제거한다. 컴포넌트가 먼저 사라져도 World 약한 참조와 세대 검사로 오래된 콜백을 막는다. PlayerState가 유지되는 Pawn 교체와 실제 인벤토리 소유자 교체를 구분한다.

퀵슬롯은 변경 묶음마다 `ItemId → 정의/총수량/사용 가능한 GUID`를 한 번 집계한다. 슬롯마다 전체 가방을 반복 순회하지 않는다. 검색·분류는 총수량을 바꾸지 않는다. 잠긴 스택도 보유량에 포함하되 사용 GUID는 잠기지 않은 소모품에서 선택한다. 실제 사용은 기존 서버 소유권·잠금·쿨다운 검사를 거친다.

기존 UObject 행의 정체성과 가상화 TileView를 유지한다. 지속 프레임 Tick을 새로 추가하지 않는다. CSV `ProjectJUI` category에 InventoryProjection, InventoryFilter, QuickSlotPresentation을 추가했다.

## 재현과 결과

직접 UnrealBuildTool.exe로 Editor/Game Development를 빌드하고 엔진 관련 프로세스 종료 후 다음 단계로 진행한다. 로컬 증거는 Git 제외된 `Saved/Validation/UIReliability_20261010`에 둔다.

자동화 `TestsVerified/index.json`: 38개 중 성공 37, 기존 탈것 경고 성공 1, 실패 0. 새 테스트는 저장 실패/병합/백업 복구/이관/최신 파일 보호, 장비 정책, 타임아웃/늦은 응답/소스 전환, 집계와 최종 delta를 검증한다. 실제 패킷 전송·재접속 검사는 별도다.

패키지 측정은 `Scripts/Validation/Validate-CookedUI.ps1 -Profile`을 사용한다. `-HeavyWorkload`를 추가하면 standalone authority에서 아이템 1,000개, UI 버프 24개, 창 4개, 아이템 퀵슬롯 10개를 준비한다. Shipping에서는 fixture를 실행하지 않는다. 새 UserDir를 사용하고 준비 후 CSV 240프레임을 기록한다. 두 조건에서 같은 실행 파일·해상도·60fps 제한을 사용한다.

실행·측정 결과와 네트워크 UI 관측은 다음과 같다.
### 패키지·측정 결과

`BuildEditorFinal.log`, `BuildGame.log`: 직접 UBT 성공. `Package.log`: UAT 코드 빌드 생략 후 cook/stage/package 성공. 처음 Zen oplog 연결 재시도가 있었으나 UAT가 복구하여 종료 코드 0으로 완료했다.

`UIReliability_ProfileIdle_20261010/Results.json`, `UIReliability_ProfileHeavy_20261010/Results.json`: 각 720p/1080p, 총 4회 종료 코드 0·로그 오류 0. Smoke는 키 변경을 FlushNow한 뒤 실제 디스크 파일을 다시 읽어 저장을 확인한다. 고부하 marker는 실제 rows=1000, buffs=24, windows=4, slots=10을 요구한다.

`UIReliability_20261010/PerfSummary.json`은 Results와 CSV 원본·실행 파일 SHA256을 연결한다. 같은 실행 파일 SHA256은 `8F861D4F47212DEB55B394BB2C1270A7142FACEB182568DEA3C281BBE14FA3BC`다. `Scripts/Validation/Summarize-UIProfile.py`로 계산한다. 각 240프레임 중 첫 120프레임을 제외했다.

| 조건 | 해상도 | Engine UI 평균 / p95 |
|---|---|---:|
| 기본 HUD | 1280×720 | 0.209 / 0.316 ms |
| 기본 HUD | 1920×1080 | 0.205 / 0.303 ms |
| 아이템 1,000 + 버프 24 + 창 4 + 퀵슬롯 10 | 1280×720 | 0.605 / 0.752 ms |
| 아이템 1,000 + 버프 24 + 창 4 + 퀵슬롯 10 | 1920×1080 | 0.626 / 0.737 ms |

Development/D3D12/RenderOffscreen/60fps 제한의 짧은 안정 상태 관측이다. Engine UI는 HUD만의 비용이 아니다. 대량 목록 준비와 첫 구성의 비용은 CSV 시작 전이며, 지속적인 복제·필터 입력·전투·군중·장시간 GC 부하는 측정하지 않았다. 호출되지 않은 ProjectJUI 계측은 값이 없는 것이므로 0ms라고 해석하지 않는다. 변경 전후 개선율이나 최대 FPS를 주장하지 않는다.

### 네트워크와 실제 화면

MCP floating PIE: 단일 에디터 안의 서버와 Client 1/2, 모두 송신 지연 150~250ms 및 패킷 손실 10%, 수신 지연/손실 0. `PIENetworkConditions.json`과 `EditorNetwork.log`에 접속 URL의 PktLagMin/PktLagMax/PktLoss, 두 접속 완료를 기록했다.

Computer Use 키보드로 Client 1에서 `UIPrototypeTest prepare`를 실행했다. 서버 marker의 아이템 2개와 Client 1 가방의 체력/마나 포션 각각 10개, HP=50/MP=30이 일치했다. 다른 Client 2의 HP/MP는 100/100으로 유지됐다. Client 1의 UI 설정 창에서 저장 안내 문구가 표시되는 것을 확인했다. `SettingsReady.png`, `OtherOwnerResources.png`가 해당 관측이다.

Windows 네트워크 접근 권한 팝업(PickerHost.exe)이 클릭 지점을 가려 마우스 클릭이 거부됐다. 권한 팝업은 조작하지 않았다. 다른 클라이언트의 빈 가방, 소모품 UI pending→완료, 분할·드래그를 이번 실제 네트워크 실행에서 확인했다고 주장하지 않는다. 늦은 응답·타임아웃·소스 전환은 앞의 자동화 검증이다. 장비 비교는 정책 자동화로 검증했으며 이번 화면의 hover 검증은 못 했다.

PIE는 정상 종료했고 원래 설정으로 복원한 뒤 `PIESettingsBaseline.json`과 `PIESettingsRestored.json`의 응답 동일성을 확인했다. 원래 값은 1280×720, Client 1개, PIE_Client, 단일 프로세스, 네트워크 에뮬레이션 꺼짐이다. 에디터 정상 종료 확인은 Windows 권한 팝업 때문에 별도 확인이 필요하다.
