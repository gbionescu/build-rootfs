#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
ROOT_DIR="$( cd "$SCRIPT_DIR/.." >/dev/null 2>&1 ; pwd -P )"
CC="${CC:-gcc}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/elf-symbol-test.XXXXXX")"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cat > "$TMP_DIR/target.c" <<'EOF'
__attribute__((noinline)) void target_symbol(int value)
{
    volatile int *sink = &value;
    (void)sink;
}

int main(void)
{
    target_symbol(1);
    return 0;
}
EOF

"$CC" -O2 -Wall -Wextra -D_GNU_SOURCE \
    "$ROOT_DIR/linux-app/common/elf_symbol_test.c" \
    -o "$TMP_DIR/elf_symbol_test"

for kind in pie nopie; do
    flags=()
    if [[ "$kind" == "nopie" ]]; then
        flags=(-no-pie)
    fi

    "$CC" -O0 -g "${flags[@]}" "$TMP_DIR/target.c" -o "$TMP_DIR/target-$kind"

    symbol_addr="$(nm -n "$TMP_DIR/target-$kind" |
        awk '$3 == "target_symbol" { print $1; exit }')"
    if [[ -z "$symbol_addr" ]]; then
        echo "FAIL: target_symbol missing from $kind binary" >&2
        exit 1
    fi

    read -r load_offset load_vaddr <<<"$(readelf -lW "$TMP_DIR/target-$kind" |
        awk '$1 == "LOAD" { print $2, $3; exit }')"
    load_offset="${load_offset#0x}"
    load_vaddr="${load_vaddr#0x}"
    expected=$((16#$symbol_addr + 16#$load_offset - 16#$load_vaddr))

    "$TMP_DIR/elf_symbol_test" "$TMP_DIR/target-$kind" target_symbol \
        "$expected"
done
