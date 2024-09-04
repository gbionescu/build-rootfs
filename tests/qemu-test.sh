#!/bin/bash
set -e

TIMEOUT=60
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

echo "Testing $1"
qemu-system-x86_64 \
    -kernel "$SCRIPT_DIR/../kernel/bzImage-6.1.22" \
    -append "console=ttyS0 root=/dev/sda earlyprintk=serial" \
    -nographic -m 512M \
    -hda "$1" > serial.log 2>&1 &

PID="$!"

# Wait for "login:" to appear in the serial log
timeout="$TIMEOUT"
result="PASS"
while ! grep -q "login:" serial.log; do
    echo "[$((TIMEOUT - timeout))/$TIMEOUT] Waiting... last line of serial.log: $(tail -n 1 serial.log)"

    sleep 1
    timeout=$((timeout - 1))
    if [[ "$timeout" -eq 0 ]]; then
        echo "Timeout waiting for QEMU to start"
        result="FAIL"
        break
    fi
done

echo "Found login prompt."

kill -9 "$PID"
if [[ "$result" == "FAIL" ]]; then
    echo "Test failed"
    exit 1
fi

rm serial.log
echo "Test passed in $((TIMEOUT - timeout)) seconds"