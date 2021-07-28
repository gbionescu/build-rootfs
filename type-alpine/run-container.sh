#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

# Launch the container
cont_id=$(docker run -td --rm  --privileged -v $1:/rootfs.ext4 alpine)

# Run the payload
set +e
docker exec -it "$cont_id" /bin/sh -c "`cat $SCRIPT_DIR/inside-container.sh`"

rval=$?
set -e

# Stop the container
docker stop "$cont_id"

if [[ "$rval" != 0 ]]; then
    echo "Error running the payload"
    exit 1
fi