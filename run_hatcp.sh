#!/usr/bin/env bash
set -euo pipefail

# ===== user config =====
HATCP_DIR="${HATCP_DIR:-$HOME/kwon/hatcp}"
APP_DIR="${APP_DIR:-apps/wan_acc}"      # apps/socks, apps/wan_acc, test_apps/iperf_lite 등
ROLE="${ROLE:-primary}"                # primary or replica
CORE_MASK="${CORE_MASK:-0x3}"
MEM_MB="${MEM_MB:-4096}"
NIC_PCI="${NIC_PCI:-}"                 # 예: 03:00.0, 비우면 bind 안 함
LOG_DIR="${LOG_DIR:-$HATCP_DIR/logs}"

# app args는 실행 파일마다 다를 수 있음
APP_ARGS="${APP_ARGS:-}"

echo "[1] checking repo"
if [ ! -d "$HATCP_DIR/.git" ]; then
    git clone https://github.com/rcslab/hatcp.git "$HATCP_DIR"
fi

cd "$HATCP_DIR"

echo "[2] setup hugepages"
sudo mkdir -p /mnt/huge
if ! mount | grep -q /mnt/huge; then
    sudo mount -t hugetlbfs nodev /mnt/huge
fi
echo "$MEM_MB" | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages >/dev/null

echo "[3] optional NIC bind"
if [ -n "$NIC_PCI" ]; then
    sudo modprobe vfio-pci || true

    if command -v dpdk-devbind.py >/dev/null 2>&1; then
        DPDK_DEVBIND=dpdk-devbind.py
    elif [ -x /opt/mellanox/dpdk/bin/dpdk-devbind.py ]; then
        DPDK_DEVBIND=/opt/mellanox/dpdk/bin/dpdk-devbind.py
    else
        echo "dpdk-devbind.py not found"
        exit 1
    fi

    sudo "$DPDK_DEVBIND" --bind=vfio-pci "$NIC_PCI"
fi

echo "[4] build HA/TCP app: $APP_DIR"

cd "$HATCP_DIR"

MAKE_DIR="$(dirname "$APP_DIR")"

if [ -f "$APP_DIR/Makefile" ]; then
    cd "$HATCP_DIR/$APP_DIR"
elif [ -f "$MAKE_DIR/Makefile" ]; then
    cd "$HATCP_DIR/$MAKE_DIR"
elif [ -f "$HATCP_DIR/Makefile" ]; then
    cd "$HATCP_DIR"
else
    echo "No Makefile found for $APP_DIR"
    echo "Run: find $HATCP_DIR -name Makefile"
    exit 1
fi

make clean || true
make -j"$(nproc)"

echo "[5] find binary"
cd "$HATCP_DIR"

BIN="$(find "$HATCP_DIR/$APP_DIR" -type f -executable \
    ! -name '*.sh' \
    ! -name '*.py' \
    | head -n 1)"

if [ -z "$BIN" ]; then
    echo "No executable binary found under $APP_DIR"
    echo "Check build output:"
    find "$HATCP_DIR/$APP_DIR" -maxdepth 3 -type f
    exit 1
fi

mkdir -p "$LOG_DIR"

echo "[6] run"
echo "binary: $BIN"
echo "role:   $ROLE"
echo "args:   $APP_ARGS"

sudo "$BIN" \
    --proc-type=primary \
    -c "$CORE_MASK" \
    --huge-dir=/mnt/huge \
    -- \
    --role "$ROLE" \
    $APP_ARGS \
    2>&1 | tee "$LOG_DIR/${APP_DIR//\//_}_${ROLE}.log"
