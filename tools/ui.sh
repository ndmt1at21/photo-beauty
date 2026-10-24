#!/bin/bash
# Runs a command with Qt 6.11 for the UI's builds and tests (docs/ui-tasks.md, Môi trường): QT_ROOT_DIR as set, else
# aqt's ~/Qt/6.11.3/gcc_64; in the current directory, arguments as given, with the command's exit code. Until 22/10/2026
# it ran the command in Ubuntu-24.04 from 20.04; the dev machine is 24.04 now. Environment variables: `env X=1 command`.
# Usage: tools/ui.sh <command> [arguments], e.g. tools/ui.sh ctest --test-dir build/ui -R '^ui$'
set -e
[ $# -gt 0 ] || { echo 'usage: tools/ui.sh <command> [arguments]' >&2; exit 2; }
export QT_ROOT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.11.3/gcc_64}"
exec "$@"
