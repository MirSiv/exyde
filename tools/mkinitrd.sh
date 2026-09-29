#!/bin/sh
# Build build/initrd -- a cpio newc archive of the userspace programs
# under bin/ (Phase 11.5.8).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
STAGE="$BUILD/initrd-stage"

[ -d "$BUILD" ] || { echo "mkinitrd: $BUILD does not exist" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/bin"

for prog in test echo exy-vfs console exshell; do
    src="$BUILD/$prog.elf"
    [ -f "$src" ] || { echo "mkinitrd: missing $src" >&2; exit 1; }
    cp "$src" "$STAGE/bin/$prog"
done

( cd "$STAGE" \
    && find . -type f -printf '%P\0' \
       | LC_ALL=C sort -z \
       | cpio --null -o -H newc --owner=0:0 2>/dev/null ) \
    > "$BUILD/initrd"

rm -rf "$STAGE"
