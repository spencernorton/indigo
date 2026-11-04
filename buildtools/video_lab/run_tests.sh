#!/bin/sh
set -eu
cd "$(dirname "$0")"
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror core.c test_core.c -o "$test_dir/core"
"$test_dir/core"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror scene.c test_scene.c -o "$test_dir/scene"
"$test_dir/scene"
if command -v clang >/dev/null 2>&1; then
    case "$(uname -s)" in Linux) set -- -no-pie ;; *) set -- ;; esac
    clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined "$@" core.c test_core.c -o "$test_dir/sanitized"
    "$test_dir/sanitized"
    clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined "$@" scene.c test_scene.c -o "$test_dir/sanitized-scene" # -no-pie on Linux via "$@"
    "$test_dir/sanitized-scene"
fi
python3 -m unittest discover -s . -p 'test_*.py'
