#!/bin/sh
# Trowel CLI — open files as new tabs in the running Trowel GUI.
#
# Homebrew symlinks this (from inside Trowel.app) onto your PATH as `trowel`.
# It launches via `open` (LaunchServices) rather than exec'ing the app binary,
# so a second invocation is routed to the already-running Trowel instance and
# adds tabs, instead of spawning a second copy of the app. See
# TrowelApplication (QEvent::FileOpen) for the receiving end.
#
#   trowel                 # bring Trowel to the front (launch if needed)
#   trowel a.tur b.tur     # open both as tabs in the existing window

set -eu

BUNDLE_ID="com.turmeric-lang.TrowelEditor"

# No arguments: just activate Trowel.
if [ "$#" -eq 0 ]; then
    exec open -b "$BUNDLE_ID"
fi

# Resolve every argument to an absolute path so `open` doesn't interpret it
# relative to the app bundle. Positional parameters are rebuilt in place, which
# keeps paths containing spaces intact.
count=$#
while [ "$count" -gt 0 ]; do
    arg=$1
    shift
    count=$((count - 1))
    case "$arg" in
        /*)
            set -- "$@" "$arg"
            ;;
        *)
            dir=$(dirname -- "$arg")
            base=$(basename -- "$arg")
            if abs_dir=$(cd "$dir" 2>/dev/null && pwd); then
                set -- "$@" "$abs_dir/$base"
            else
                printf 'trowel: no such directory: %s\n' "$dir" >&2
                set -- "$@" "$arg"
            fi
            ;;
    esac
done

exec open -b "$BUNDLE_ID" "$@"
