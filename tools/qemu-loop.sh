#!/usr/bin/env bash
# tools/qemu-loop.sh — run QEMU N times, stop on the first panic.
#
# Usage:  tools/qemu-loop.sh [N]        (default N = 100)
#
# The kernel runs its full test suite and then enters an idle hlt
# loop; it never exits on its own, so each run is bounded by a
# timeout.  A "green" run is one where the timeout fired and no panic
# marker appeared in the output.  On the first panic the script stops,
# prints the full log, and exits non-zero.
#
# Assumes the ISO has already been built (`make iso`).

set -u

N="${1:-100}"
PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$PROJ_ROOT/build/exyde.iso"
LOG_DIR="$PROJ_ROOT/build/qemu-loop"
TIMEOUT_SEC=5

if [ ! -f "$ISO" ]; then
    echo "ISO not found: $ISO"
    echo "Run 'make iso' first."
    exit 2
fi

mkdir -p "$LOG_DIR"

QEMU_CMD=(qemu-system-x86_64
    -accel kvm
    -display none
    -cdrom "$ISO"
    -serial stdio
    -m 512
    -no-reboot
)

green=0
fail_run=0
fail_log=""

echo "qemu-loop: up to $N runs, ${TIMEOUT_SEC}s timeout each"
echo "qemu-loop: logs in $LOG_DIR"
echo

for i in $(seq 1 "$N"); do
    log="$LOG_DIR/run-$(printf '%04d' "$i").log"
    timeout -k 2 "$TIMEOUT_SEC" "${QEMU_CMD[@]}" </dev/null >"$log" 2>&1
    rc=$?

    # 124 = timeout killed QEMU (expected: kernel reached idle loop).
    # 0   = QEMU exited by itself (no-reboot path, still fine).
    # other = QEMU error (kvm missing, iso corrupt, ...).
    if [ "$rc" -ne 124 ] && [ "$rc" -ne 0 ]; then
        echo
        echo "run $i: qemu exited with rc=$rc"
        echo "  log: $log"
        cat "$log"
        echo
        echo "qemu-loop: stopping on QEMU error"
        exit 3
    fi

    if grep -qE "KERNEL PANIC|iretq-frame check FAILED" "$log"; then
        fail_run=$i
        fail_log="$log"
        echo
        echo "run $i: FAIL (panic detected)"
        break
    fi

    green=$((green + 1))
    printf '\rgreen: %d / %d' "$green" "$i"
done

echo
echo

if [ "$fail_run" -ne 0 ]; then
    echo "qemu-loop: STOPPED at run $fail_run"
    echo "  failing log: $fail_log"
    echo "  ---------- begin failing log ----------"
    cat "$fail_log"
    echo "  ---------- end failing log ----------"
    exit 1
fi

echo "qemu-loop: $green / $N green, no panics detected"
exit 0
