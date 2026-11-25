# Project J · 무료/초저비용 UE 5.8 소스 빌드(256+ 슬롯) · Linux DS · 로컬 K3s/Agones 실행 가이드

이 가이드는 대학생·1인 개발 환경에서 **상용 클라우드 고정비 없이** 대기업급 다중 서버 운영과 100~200인 호스트 분리 실측을 수행하는 절차를 정리합니다.

## 1. 로컬 백엔드 + Grafana 관측성 스택 실행 (0원)

```powershell
$env:PROJECT_J_NAKAMA_HTTP_KEY = "local-dev-runtime-key-change-me"
docker compose -f Infra/docker-compose.yml up -d --wait postgres
powershell -ExecutionPolicy Bypass -File Infra/scripts/apply-project-schema.ps1
docker compose -f Infra/docker-compose.yml --profile observability up -d
```

- **Nakama Admin**: `http://127.0.0.1:7351`
- **PgBouncer**: `127.0.0.1:6432` (Transaction Pooling, `MAX_CLIENT_CONN=1000`)
- **Valkey 8**: `127.0.0.1:6379`
- **Prometheus**: `http://127.0.0.1:9090`
- **Grafana**: `http://127.0.0.1:3000` (`admin` / `project_j_dev_grafana`, `Project J · AAA MMO Server & Backend Overview` 대시보드 자동 로드)
- **UE Dedicated Server 메트릭**은 기본 루프백입니다. 호스트의 Prometheus에는 `-useperfthreads -ProjectJMetricsPort=9100`을 사용합니다. Docker의 `host.docker.internal`에서 수집하려면 `-ProjectJMetricsBindAddress=0.0.0.0`을 명시하고 호스트 방화벽으로 스크래퍼 접근을 제한합니다.
- 기본 Nakama 컨테이너는 JS 모듈을 사용합니다. Go/Valkey 플러그인은 Nakama 3.25.0의 동일 Linux 빌드 환경으로 별도 빌드·로드해야 하는 선택 항목입니다. PostgreSQL이 로그인 티켓의 권위입니다.

## 2. UE 5.8 소스 빌드 전환 (256~512 슬롯 Iris + Linux Dedicated Server, 무료)

설치형(Launcher) UE 5.8은 기본 Iris 커넥션 슬롯 상한과 `Server` 타깃 빌드가 제한됩니다. Epic Games GitHub 연동(무료)으로 소스를 받아 아래 3단계를 수행합니다:

1. `git clone --branch 5.8 --depth 1 https://github.com/EpicGames/UnrealEngine.git`
2. Iris 최대 커넥션 슬롯 확장은 별도 엔진 변경으로 다룹니다. 목표 인원에 맞춰 NetDriver 정원, Iris 연결 ID 및 할당 배열, 필터·우선순위 메모리, 프로토콜 비트 폭을 대조한 뒤 단계적으로 실측합니다. 이 배포 팩의 정적 검증만으로 256~512슬롯을 승인하지 않습니다.
3. Windows에서 Linux Cross-Compile Toolchain 설치 (Epic 공식 `v23/v25 clang` 무료 설치) 후:
   ```powershell
   RunUAT.bat BuildCookRun -project="c:\Users\I\Documents\GitHub\Project_J\Project_J.uproject" `
     -platform=Linux -target=Project_JServer -server -noclient -serverconfig=Development `
     -cook -stage -pak -archive -archivedirectory="Saved/StagedBuilds"
   docker build -f Infra/deploy/Dockerfile.ue-server -t project-j-server:latest ./Saved/StagedBuilds/LinuxServer
   ```

## 3. 로컬 K3s + Agones 오케스트레이션 검증 (0원)

```bash
# WSL2 Ubuntu 또는 Linux VM
curl -sfL https://get.k3s.io | sh -
kubectl create namespace agones-system
kubectl apply --server-side -f https://raw.githubusercontent.com/googleforgames/agones/release-1.47.0/install/yaml/install.yaml
kubectl create secret generic project-j-secrets \
  --from-literal=nakama-http-key="local-dev-runtime-key" \
  --from-literal=handover-shared-key="local-dev-handover-hmac-secret-key"
kubectl apply -f Infra/deploy/agones/fleet-zone-server.yaml
```

스테이징 컨텍스트에 실행 권한이 있는 `Project_J/Binaries/Linux/Project_JServer`가 있어야 합니다. 이 바이너리가 컨테이너의 PID 1로 실행됩니다. Docker의 SIGTERM은 UE 게임 스레드의 드레인 상태 전이로 이어집니다. Agones 컨테이너는 SDK HTTP 포트를 환경변수로 받아 월드 준비 뒤 `/ready`를, 정상 동작 중 `/health`를 호출합니다. 메트릭 포트는 Pod 내부에 두고 게임 UDP 포트만 호스트에 할당합니다. [Agones REST SDK](https://agones.dev/site/docs/guides/client-sdks/rest/)

이 Fleet는 로컬 오케스트레이션용입니다. 실제 온라인 다중 채널 운영에는 인스턴스별 채널 ID·광고 주소, Nakama 연결, SQLite Outbox의 영속 볼륨과 교체 후 복구 정책이 필요합니다. 또한 현재 `Source/Project_J/Backend/Project_JProfileDatabase.cpp`는 안전한 Win32 롤백 VFS만 지원하며 비-Windows 플랫폼에서 안전하지 않은 기본 HAL VFS로의 암묵적 폴백을 막기 위해 `Open()`이 `false`를 반환합니다. 따라서 비-Mock Linux Dedicated Server 컨테이너를 서비스 상태로 기동하려면 먼저 POSIX 파일 잠금·동기화·롤백을 지원하는 Linux SQLite VFS 구현 및 컨테이너 크래시 복구 검증이 선행되어야 합니다. Linux 이미지, Pod 준비 상태, SIGTERM 종료는 Kubernetes에서 별도로 실측합니다.

## 4. 100~200인 호스트 분리 실측

- 서버 호스트는 UE 전용 서버와 로컬 백엔드를 실행합니다. 생성기 호스트는 설치형 Iris 한도 안에서 연결·반복 교전을 단계적으로 늘립니다.
- `Run-LoopbackCohort.ps1`은 동일 Windows 호스트용입니다. 원격 생성기 실행과 시간 동기화는 별도 하네스 계약이 필요합니다.
- 검증에는 30Hz 서버 예산, 타격·복제·정상 종료, Outbox 상태와 3구간 전투 지연을 포함합니다.
