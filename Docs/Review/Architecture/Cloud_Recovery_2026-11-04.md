# Cloud 소스 복구와 검증 상태

기준: `d4077428fac7e6f1d7d90039dfdd496f0e50bc31`.
2026-11-04 환경 재시작 후 사용자가 다시 첨부한 소스 패치를 복구했다. SHA-256은 `9000b1d14b608a9af9e02454d0ad4cf7dcf017164f6092a9d76032ffbe74d08a`이며, 깨끗한 기준 checkout에서 `git apply --check` 후 적용했다.

복구 대상은 Source 54개다. 이전 패치의 문서 6개와 도구 2개는 소스 전용 파일에 없었다. 필요한 W8 근거·현재 제한과 W10 sparse 문서 검사/테스트는 이후 기준 소스에서 재작성했다. 이전 전체 패치와 byte-for-byte 동일하다고 주장하지 않는다. 에셋·사용자 원본 변경은 없다.

## 기능 범위

- W1: 현재 생존/Avatar/ASC와 파티 관계를 권위 피해 제출까지 검사. 재진입 중 실행 교체와 중복 적용 방지.
- W2/W3: 사망 래치·명시적 부활·NPC 타깃 등록·입력/탑승 정리. 연결 종료 시 정확한 세션/Pawn/ASC 소유권을 확인하며 replacement Pawn과 공유 탈것 보존.
- W4: 기본 Character의 rewind history 생성과 종료 정리. 순서가 뒤바뀐 요청 수락 정책은 바꾸지 않음.
- W5: 기본 비활성 기여 원장과 확정 GE 피해 관측. 관측 중 재진입 종료는 불완전 회차로 처리. 복구 후 개발 전용 설정형 XP 소비자와 영속 claimed 원장을 추가했다. 실제 운영 지급/재시작 후 미지급 자격 복원은 미완료.
- W6: 기존 GAS·스타일 경로에 서버 직선 투사체. 벽·NPC·파티원 차폐, 초기 겹침, 취소·수명 테스트 작성. 실제 DA·입력·시각 BP 연결 미완료.
- W7: 명시적 개발 플래그의 trusted session 프로필 저장과 idle epoch 갱신. 운영 다중 서버 영속 서비스가 아님.
- W8: 새 성능 측정 없음. 과거 Cloth 200개 시험의 실제 원격 연결은 0개이므로 200CCU/60FPS 근거로 사용하지 않음.
- W9: 현재 그룹·리더·멤버·세션 revision을 확인하는 파티 권한 fence. 던전·길드 실제 소비자는 별도.
- W10: 운영 배포/Cook/Shipping 검증은 미완료. sparse 생략 경로를 정상 삭제와 구분하는 문서 검사와5개 회귀 시나리오를 복원했다.

## 검증 경계

현재 소스는 UE UHT/UBT 및 native automation 미검증 상태다. 실제 기본 BP의 rewind 중복 여부, 2-client party/death/latency, 연결 종료 및 동일 PlayerState 갱신을 별도 Windows 복사본에서 확인해야 한다. 기존 실행 중 빌드/에디터를 종료하거나 빌드를 겹치지 않는다. 사용자 작업 폴더 전체 덮어쓰기나 에셋 재작성은 하지 않는다.

집중 회귀 등록명:
- ProjectJ.Combat.PlayerMeleeEligibility
- ProjectJ.Combat.ProductionRewindLifecycle
- ProjectJ.PlayerLife.DeathExplicitReviveAndRetirement
- ProjectJ.Session.AvatarRetirementPreservesSharedMount
- ProjectJ.Combat.ProjectileLifecycle
- ProjectJ.MMO.WorldEventContributions
- ProjectJ.MMO.Social.PartyAuthorityFence
- ProjectJ.MMO.ServerSession.NativeOwnership
- ProjectJ.MMO.Persistence.TrustedDevelopmentQuestReconnect

GitHub main 직접 push·병합·배포는 수행하지 않는다. Cloud의 정적 성공을 Windows 실행 성공으로 보고하지 않는다.

## 복구 후 cloud 검사

아키텍처205 features/20 domains 통과. Python77개 중72개 통과·PowerShell 필요5개 제외. sparse 문서 검사5개 시나리오 통과. 문서293개·로컬 링크3513개 중 깨진 링크0, 기존 로컬 보관 부재11개 구분. git diff 검사 통과. 이 수치는 네이티브 UE 테스트 통과를 뜻하지 않는다. GitHub 연결의 브랜치 생성은403(Resource not accessible by integration)으로 거절되어 원격 쓰기를 중단했다.

## 후속 W5/W7 보강

실제 GE 피해→회차 기여→처치 종료→자동 XP/영속 표시 연결 시험 `ProjectJ.MMO.Persistence.WorldEventGEAutomaticReward`를 추가했다. 기존 연결의 저장 작업이 끝나기 전에 재접속한 경우, 정확한 세션에 묶인 제한된 대기 요청을 writer 종료 시 재개한다. 대기 중인 컴포넌트 변경은 차단하고, 두 번 교체된 오래된 세션을 다시 열지 않는다. 독립 정적 검토에서 발견한 재접속 정지·대기 상태 변경 허용 문제를 수정했다. 네이티브 시험은 작성 상태이며 실행 성공으로 보고하지 않는다.

## Windows 첫 실행 후 수정 묶음

Windows 별도 작업본은 레지스트리 own-header 순서, 잘못된 중첩 PlayerState 선언, 테스트의 protected 함수 접근을 수정한 뒤 UBT가 성공했다. 최초 네이티브20개는14통과·경고 포함1통과·5실패였다. 이 실패 이력은 보존한다.

Cloud에서 다음 수정 묶음을 준비했다: 투사체 fixture 초기화/속성/BeginPlay 순서와 진단, 무기 수명 fixture의 실제 rewind 초기화, trusted 프로필 복구의 읽기 전용 검증과 mutation admission 분리, busy reconnect 진단, 서로 다른 연결의 최종 종료와 같은 연결 갱신의 grant 보존 조건 구분. 컴파일 수정3건도 포함한다. 이 수정 후 UE 빌드·네이티브 재실행은 수행하지 않았으므로5개 실패가 해결됐다고 확정하지 않는다. 기존 Mover 경고도 무경고 통과로 바꾸지 않는다. 삭제된 Windows 작업본을 재생성하지 않았다.
