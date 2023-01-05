#!/usr/bin/env bash
set -ex
# find src -name '*.c' -exec clang-format -i {} +
# find src -name '*.h' -exec clang-format -i {} +

find src -name '*.c' -exec indent -kr --no-tabs {} +
find src -name '*.h' -exec indent -kr --no-tabs {} +


