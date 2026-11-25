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

이 Fleet는 로컬 오케스트레이션용입니다. 실제 온라인 다중 채널 운영에는 인스턴스별 채널 ID·광고 주소, Nakama 연결, SQLite Outbox의 영속 볼륨과 교체 후 복구 정책이 필요합니다. Linux 전용 서버 타깃은 엔진의 `bCompileCustomSQLitePlatform=false` 옵션으로 SQLiteCore를 빌드합니다. 이 타깃에는 소스 엔진의 독립 빌드 환경이 필요합니다. 프로젝트는 연결마다 SQLite의 표준 `unix` VFS를 지정합니다. 해당 VFS가 빌드에서 빠졌거나 Outbox가 열리지 않으면 온라인 입장을 거부합니다. 채널의 첫 heartbeat는 저장소 복구 뒤에 전송하고, 채널 임대 응답 후에 입장을 허용합니다. Pod 교체 시 동일 채널의 저장소를 한 프로세스만 소유하도록 구성하고, 고유한 광고 주소와 채널 ID를 공급해야 합니다. [SQLite Unix VFS](https://www.sqlite.org/vfs.html)

Linux 이미지, Pod 준비 상태, SIGTERM 종료는 Kubernetes에서 별도로 실측합니다.

## 입장과 보상 복구 확인

일반 월드 접속은 Nakama 플레이어 세션에서 선호 채널을 요청합니다. 대기 중에는 서버가 반환한 순번과 재조회 주기를 사용합니다. 같은 캐릭터의 같은 대기표를 갱신하고, 취소·만료된 대기표로 재조회하면 `EXPIRED`를 반환합니다. 던전 입장·복귀는 지정된 목적지와 기존 서명 티켓을 사용합니다. 종료를 시작한 인스턴스에서는 늦게 도착한 기존 heartbeat로 입장을 재개할 수 없습니다.

온라인 필드 보스의 쿨다운과 수령자 명단은 같은 SQLite 커밋으로 확정됩니다. 프로세스 교체 시험에는 같은 Outbox 파일을 다시 열어 미전송 지급과 퀘스트 진척이 원래 키로 재처리되는지 확인하는 과정이 포함됩니다. 다른 저장 볼륨으로 교체하면 이전 파일의 미전송 행을 복구할 수 없습니다. 개별 백엔드 명령이 재시도 한도를 넘으면 기존 dead-letter 조회와 redrive 경로를 사용합니다.

## 4. 100~200인 호스트 분리 실측

- 서버 호스트는 UE 전용 서버와 로컬 백엔드를 실행합니다. 생성기 호스트는 설치형 Iris 한도 안에서 연결·반복 교전을 단계적으로 늘립니다.
- 같은 Windows PC에서는 `Run-LoopbackCohort.ps1`을 사용합니다. 사설망의 Windows PC 두 대에서는 `Scripts/Validation/Run-SplitHostCohort.ps1`로 동일한 Editor 개발 빌드, 실행 ID, 포트, 인원 수를 지정해 `Server`와 `Generator` 역할을 실행합니다. 각 역할은 자기 IP와 단일 상대 IP를 명시합니다. 생성기 결과를 서버 쪽에 복사한 뒤 `Verify` 역할로 확인합니다.
- 군중 제한의 실제 발동을 확인할 때는 서버에 `-VerifyCrowdCap`을 지정합니다. 이때 2인 파티들을 구성해 파티원 보호와 다른 파티의 제한을 함께 시험합니다. 분리 호스트의 피해 확정→클라이언트 수신 단방향 지연은 미측정으로 기록합니다.
- 검증에는 30Hz 서버 예산, 타격·복제·정상 종료, Outbox 상태와 3구간 전투 지연을 포함합니다.
