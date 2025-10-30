#!/bin/sh

BRANCH=${CI_COMMIT_BRANCH:-$CI_MERGE_REQUEST_TARGET_BRANCH_NAME}


if [ -n "${BRANCH}" ]; then
    case "$BRANCH" in
        develop|master) zb version _/_ ;;
        *) zbuild version $BRANCH ;;
    esac
else
    zb version _/_
fi

zbuild build zbuild -f || true

# 删除一天前的遗留目录
find $CI_BUILDS_DIR -maxdepth 1 -type d -name "[0-9]*" -mmin +1440 -exec rm -rf {} \; || true
