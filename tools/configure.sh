#!/bin/sh
# Configures build/<preset> of the worktree this script is in, on the dev machine (WSL2, Ubuntu 20.04, GCC 9),
# under tools/heavy.sh:
# - one vcpkg_installed per triplet for every worktree (build/vcpkg_installed of the main checkout, -mingw for
#   Windows), linked from the build dir so include paths read alike in each worktree and ccache hits; installed only
#   when a package is missing, as manifest mode removes what the manifest lacks (another worktree's packages);
# - ccache with photo-beauty's cache, a slangc that runs on glibc 2.31, samples/ linked to the main checkout's;
# - TSan: GCC 9 lacks libtsan_preinit.o, made here.
# The ui presets (Qt 6.11) run on Ubuntu-24.04 instead, through tools/ui.sh, with their own vcpkg_installed-ui
# (GCC 13): one distro's packages would be rebuilt by the other's compiler and break its builds.
# Usage: tools/configure.sh <preset> [more cmake arguments]
set -e
preset=${1:?usage: tools/configure.sh <preset> [cmake arguments]}
case $preset in
  ui*) distro=24.04 hint="; from 20.04: tools/ui.sh tools/configure.sh $preset" ;;
  *) distro=20.04 hint= ;;
esac
if [ "$(. /etc/os-release && echo "$VERSION_ID")" != $distro ]; then
  echo "tools/configure.sh: preset $preset is for Ubuntu $distro$hint" >&2
  exit 2
fi
[ -n "$PB_HEAVY_LOCKED" ] || exec "$(dirname "$0")/heavy.sh" "$0" "$@"
shift
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}" VCPKG_MAX_CONCURRENCY="${VCPKG_MAX_CONCURRENCY:-4}"
case $PATH in  # once: the mingw triplet's ABI hashes PATH, so binary cache hits need it alike
  "$HOME/.local/bin:"*) ;;
  *) export PATH="$HOME/.local/bin:$HOME/.local/opt/llvm-mingw-20250402-ucrt-ubuntu-20.04-x86_64/bin:$HOME/.local/opt/nasm-2.14.02/usr/bin:$PATH" ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
main=$(cd "$(git rev-parse --git-common-dir)/.." && pwd)
build=$root/build/$preset
case $preset in
  mingw*) triplet=x64-mingw-static installed=$main/build/vcpkg_installed-mingw ;;
  ui*) triplet=x64-linux installed=$main/build/vcpkg_installed-ui ;;
  *) triplet=x64-linux installed=$main/build/vcpkg_installed ;;
esac
mkdir -p "$build" "$installed"
[ -e "$build/vcpkg_installed" ] || ln -s "$installed" "$build/vcpkg_installed"
[ -e samples ] || ln -s "$main/samples" samples

# Every feature's packages (tracy: the profile preset), so presets never take each other's away.
install="install --triplet=$triplet --host-triplet=x64-linux --x-install-root=$installed --x-feature=tracy --x-wait-for-lock"
if "$VCPKG_ROOT/vcpkg" $install --dry-run | grep -q -e 'will be built' -e 'will be rebuilt'; then
  "$VCPKG_ROOT/vcpkg" $install --clean-after-build
fi

ccache=$(command -v ccache || true)
if [ -n "$ccache" ]; then
  launcher="env;CCACHE_DIR=$HOME/.cache/ccache-pb;$ccache"  # also when the build runs outside heavy.sh
  set -- -DCMAKE_C_COMPILER_LAUNCHER="$launcher" -DCMAKE_CXX_COMPILER_LAUNCHER="$launcher" "$@"
fi
slangc=$HOME/.local/opt/slang-2026.19/bin/slangc
[ ! -x "$slangc" ] || set -- -DSLANGC="$slangc" "$@"
case $preset in
  tsan*)
    if [ "$(gcc -print-file-name=libtsan_preinit.o)" = libtsan_preinit.o ]; then
      printf '%s\n' 'void __tsan_init(void);' \
        '__attribute__((section(".preinit_array"), used)) static void (*preinit)(void) = __tsan_init;' >"$build/tsan_preinit.c"
      gcc -c -o "$build/libtsan_preinit.o" "$build/tsan_preinit.c"
      set -- -DCMAKE_C_FLAGS="-B$build/" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread -B$build/" "$@"  # the preset's, plus -B
    fi
    ;;
esac
exec cmake --preset "$preset" -DVCPKG_MANIFEST_INSTALL=OFF "$@"
