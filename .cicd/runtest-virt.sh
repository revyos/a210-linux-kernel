#!/bin/bash

set -e

SOCAT_FILE=$(mktemp /tmp/socat_XXXXXX)
export SOCAT_FILE

zb diskimage
zb run --socat=$SOCAT_FILE &
pid=$!

# 定义清理函数
cleanup() {
    set +e
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
    rm -rf $SOCAT_FILE
    exit 0
}

# 设置陷阱，在接收到信号时调用清理函数
trap 'cleanup' EXIT

# 不支持并行测试
pytest $(dirname "$0")/tests -sv --durations=0 -x
