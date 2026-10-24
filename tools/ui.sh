#!/bin/bash
# Runs a command in WSL's Ubuntu-24.04, where Qt 6.11 runs (docs/ui-tasks.md, Môi trường): in the current
# directory, after ~/.profile (QT_ROOT_DIR, VCPKG_ROOT), arguments as given, with the command's exit code.
# The main checkout, worktrees and all, is bind-mounted there at the same path through /mnt/wsl, again
# whenever a mount is gone (WSL restarted, 24.04 stopped when idle). Environment variables: `env X=1 command`.
# Usage: tools/ui.sh <command> [arguments], e.g. tools/ui.sh ctest --test-dir build/ui -R '^ui$'
set -e
[ $# -gt 0 ] || { echo 'usage: tools/ui.sh <command> [arguments]' >&2; exit 2; }
# Already on 24.04 (the dev machine since 22/10/2026, Qt from aqt in ~/Qt): run it here, no mount.
if [ "$WSL_DISTRO_NAME" = Ubuntu-24.04 ]; then
  export QT_ROOT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.11.3/gcc_64}"
  exec "$@"
fi
main=$(cd "$(dirname "$0")" && cd "$(git rev-parse --git-common-dir)/.." && pwd)
share=/mnt/wsl/${main##*/}
mountpoint -q "$share" ||
  wsl.exe -d "$WSL_DISTRO_NAME" -u root --cd / -e sh -c "mkdir -p $share && (mountpoint -q $share || mount --bind $main $share)" </dev/null
# The script goes on stdin, as wsl.exe's command line would mangle quotes and expand $ in a login shell;
# a wsl.exe in it reads stdin too, so not that one.
exec wsl.exe -d Ubuntu-24.04 --cd / -e bash -s <<EOF
source ~/.profile
mountpoint -q $main || wsl.exe -d Ubuntu-24.04 -u root --cd / -e sh -c "mkdir -p $main && (mountpoint -q $main || mount --bind $share $main)" </dev/null || exit
cd $(printf %q "$PWD") || exit
exec $(printf '%q ' "$@")
EOF
