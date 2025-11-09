#!/bin/bash
# arg -> docker container name (e.g. sknf-worker)
PID=$(docker inspect -f '{{.State.Pid}}' "$1")
sudo nsenter -t "$PID" -n wireshark
