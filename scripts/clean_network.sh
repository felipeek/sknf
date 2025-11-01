#!/bin/bash

set -u

sudo ip link del brsknf
sudo ip netns del testing
sudo ip netns add testing

# Remove veth interfaces corresponding to vethsknf-*
PREFIX="sknf"

echo "Deleting interfaces starting with '${PREFIX}'..."

# List matching interfaces (may include @peer suffix)
mapfile -t ifs < <(ip -o link show | awk -F': ' '{print $2}' | grep -E "^${PREFIX}" || true)

if [ ${#ifs[@]} -eq 0 ]; then
    echo "No interfaces found."
    exit 0
fi

for ifname in "${ifs[@]}"; do
    # Remove @peer if present
    base="${ifname%%@*}"

    printf "Deleting %s ... " "$base"
    if sudo ip link del "$base" >/dev/null 2>&1; then
        echo "ok"
    else
        echo "failed"
    fi
done

PREFIX="tmp"

echo "Deleting interfaces starting with '${PREFIX}'..."

# List matching interfaces (may include @peer suffix)
mapfile -t ifs < <(ip -o link show | awk -F': ' '{print $2}' | grep -E "^${PREFIX}" || true)

if [ ${#ifs[@]} -eq 0 ]; then
    echo "No interfaces found."
    exit 0
fi

for ifname in "${ifs[@]}"; do
    # Remove @peer if present
    base="${ifname%%@*}"

    printf "Deleting %s ... " "$base"
    if sudo ip link del "$base" >/dev/null 2>&1; then
        echo "ok"
    else
        echo "failed"
    fi
done

echo "Done."

