#!/bin/bash

set -e

echo "---------------------------------------- runtest-p1.sh board: $BOARD_NAME, username: $ZB_BOARD_USERNAME"

zflash -b $BOARD_NAME -w emmc emmc_boot.loader --tar=../build/Release/$P1_TARGZ

zb test cicd-testcase
if [ $? -ne 0 ]; then
    echo "Tests failed, please check the logs."
    exit 1
fi
