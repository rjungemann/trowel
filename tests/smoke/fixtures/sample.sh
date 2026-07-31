#!/usr/bin/env bash
# A shell script.
set -euo pipefail

greeting="hello"
count=3

say_hi() {
    echo "${greeting} world"
}

if [ "$count" -gt 2 ]; then
    say_hi
fi
