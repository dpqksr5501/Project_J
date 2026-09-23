# Project_J 최종 후속 감사 결과

대상: `Character_Test`의 `d6f5467` 이후 코드. 에셋 내부 연결은 조회하거나 수정하지 않았다.

## 수정 사항

- **근접 공격 판정 궤적:** `Melee Hit Trace` Notify의 Leader `SocketName`에 활성 `AttackDefinition.HitSpec.CanonicalBladeTipOffset`을 적용해 칼날 끝을 계산한다. `bUseCanonicalBladeTipOffset`을 켜면 손잡이와 떨어진 끝점을 공격별로 지정할 수 있다. 시각 무기, Follower, Hand IK 변환은 계산에 참여하지 않는다. Leader 소켓이 없으면 히트 윈도를 열지 않는다.
- **서버 판정:** 같은 칼날 끝의 이전·현재 위치가 로컬 구형 스윕과 서버 `RecordAuthoritativeTrace`에 전달된다. SSR 기록의 공격 노드·예측 키·히트 윈도 계약은 유지했다. `TraceDistance`는 무기 길이가 아니라 클라이언트 제출 선분에 허용하는 프레임 간 Leader root 이동 거리다. 긴 칼날의 회전 이동 여유는 오프셋 지름으로 더한다.
- **중복 정리:** PlayerController의 중복 Shipping 전처리 조건과 MeleeHit의 중복 friend 선언을 제거했다.
- **프로파일링 경계:** PlayerController의 디버그·프로파일링 Exec, 군중 RPC, PIE 테스트 명령은 `WITH_EDITOR`에만 존재한다. 군중·테스트 컴포넌트 포인터는 `WITH_EDITORONLY_DATA`에만 존재한다. Shipping에서 군중 객체 생성·Tick·RPC 경로가 없다.
- **장비 표현:** 전투 테스트가 발견한 프로필 교체 오류를 함께 수정했다. 플레이어에게는 현재 장비 프로필을 우선 사용하고, 플레이어가 아닌 소유자의 명시적 적용 프로필은 유지한다.

## 판정 구조

변경 전: `Leader WeaponSocket_R 위치 → 프레임 간 스윕 → 서버 기록 → SSR`. 긴 무기의 칼날 끝을 별도로 지정할 수 없었다.

변경 후: `Leader gameplay pose의 SocketName 변환 + 공격 데이터의 local 칼날 끝 오프셋 → 프레임 간 스윕 → 동일한 서버 기록 → SSR`. Presentation Weapon과 독립 모션은 시각 표현에만 사용한다. 현재는 **칼날 끝의 프레임 간 구형 스윕**을 사용하며, 칼날 뿌리부터 끝까지의 면적 스윕은 추가하지 않았다.

## 검증

- `Project_JEditor Win64 Development` 직접 UBT 빌드: 성공.
- `Project_J Win64 Shipping` 직접 UBT 빌드: 변경 중 한 차례 성공했고, 최종 증분 컴파일도 성공했다. 최종 재링크는 Google Drive File Stream이 출력 `.exe`·`.exp`를 잠가 `LNK1104`로 실패했다. Shipping UHT 생성 코드에서 프로파일링 함수·RPC는 `WITH_EDITOR`, 포인터는 `WITH_EDITORONLY_DATA`로 보호된 것을 확인했다.
- 전투·canonical melee·SSR·WeaponPresentationIdentity: 8개 통과. 시각 무기를 옮겨도 판정 끝점이 그대로이고, Leader를 회전하면 손잡이에서 200 cm 떨어진 끝점이 이동하며, 동일한 끝점이 SSR 기록에 저장되는 신규 테스트를 포함한다.
- 애니메이션: 6개 중 모의 프로필 조회 수정 전 5개 통과·1개 실패. 실패한 `TwoHandIKTransitionAndCurve`를 수정 후 재실행해 통과했다.
- 그립 표현: `StableGripTargetsAndAuthoredAlpha` 수정 후 통과.
- 탈것: 4개 통과.

## 에디터에서 확인할 작업

1. Greatsword 공격 Montage의 각 `Melee Hit Trace` Notify에서 `SocketName`이 실제 **Leader 스켈레톤의 gameplay root 소켓**인지 확인한다. 기본값 `WeaponSocket_R`가 손잡이 위치라면 그대로 쓰되, 해당 소켓이 실제로 존재해야 한다. 직렬화된 Notify 값을 코드만으로 확정하지 않았다.
2. 각 Greatsword `AttackDefinition`의 `HitSpec.bUseCanonicalBladeTipOffset`을 켜고, `CanonicalBladeTipOffset`을 해당 Leader 소켓의 로컬 좌표계에서 칼날 끝으로 설정한다. Data Validation은 사용 설정 시 0 또는 비유한 값을 오류로 처리한다. `TraceDistance`는 무기 길이가 아닌 프레임 간 root 이동 상한으로 검토한다.
3. AnimBP의 Grip Rotation·Elbow 연결과 최소 두 체형의 손목·팔꿈치·어깨 및 실제 칼날 궤적을 화면에서 검증한다.

## 남은 코드 작업

No material code follow-up is required for this audit scope.
