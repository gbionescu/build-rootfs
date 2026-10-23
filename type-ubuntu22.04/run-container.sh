#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

# Launch the target-architecture container
cont_id=$(docker run -td --rm --privileged --platform "$DOCKER_PLATFORM" -v "$1:/rootfs.ext4" ubuntu:22.04)

# Install systemd
docker exec -t "$cont_id" /bin/bash -c "apt-get update && DEBIAN_FRONTEND='noninteractive' apt-get install -y systemd"

# Run the payload in the already-running container. Services are enabled offline
# so this also works under qemu-user emulation.
set +e
docker exec -t -e SERIAL_CONSOLE="$SERIAL_CONSOLE" "$cont_id" /bin/sh -c "`cat "$SCRIPT_DIR/inside-container.sh"`"

rval=$?
set -e

docker stop "$cont_id"

if [[ "$rval" != 0 ]]; then
    echo "Error running the payload"
    exit 1
fi
