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
find $CI_BUILDS_DIR -name "[0-9]*" -type d -maxdepth 1 -mtime +1 -exec rm -r {} \; || true
