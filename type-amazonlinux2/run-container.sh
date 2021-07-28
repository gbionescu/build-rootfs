#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

# Launch the container
cont_id=$(docker run -td --rm  --privileged -v $1:/rootfs.ext4 amazonlinux:2)

# Install systemd
docker exec -it "$cont_id" /bin/bash -c "yum install -y systemd"

# Restart the container
docker commit "$cont_id" "temp-build-amazonlinux"
docker stop "$cont_id"
new_cont_id=$(docker run -td --rm  --privileged -v $1:/rootfs.ext4 temp-build-amazonlinux /sbin/init)

# Run the payload
set +e
docker exec -it "$new_cont_id" /bin/sh -c "`cat $SCRIPT_DIR/inside-container.sh`"

rval=$?
set -e

# Stop the container
docker stop "$new_cont_id"
docker rmi -f "temp-build-amazonlinux"

if [[ "$rval" != 0 ]]; then
    echo "Error running the payload"
    exit 1
fi