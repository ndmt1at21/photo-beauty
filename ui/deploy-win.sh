#!/bin/bash
# Gathers the Windows build of the UI (preset ui-mingw) into a folder Windows runs as is (docs/ui-tasks.md, U22):
# the stripped .exe files, the Qt DLLs, plugins and QML modules they load, llvm-mingw's C++ runtime, a fallback font
# for offscreen, and ui/tests/*.qml in tests\ (ui/qml/*.js beside, as in the sources) for `pb-ui-tests.exe -input tests`.
# Run through tools/ui.sh (QT_ROOT_DIR's qmlimportscanner):
#   tools/ui.sh ui/deploy-win.sh /mnt/c/Users/<you>/AppData/Local/Temp/pb-<task>
# copied from snapone 6bdb5c8 (ui/deploy-win.sh), without its TLS plugin, FTP tests and crash handler.
set -euo pipefail
dest=${1:?usage: ui/deploy-win.sh <folder>}
root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build/ui-mingw
qt=${QT_WIN_DIR:-$HOME/Qt-win/6.11.3/llvm-mingw_64}
mingw=$HOME/.local/opt/llvm-mingw-20250402-ucrt-ubuntu-20.04-x86_64
mkdir -p "$dest/plugins/platforms" "$dest/plugins/imageformats" "$dest/tests"

for exe in "$build/ui/pb-ui.exe" "$build/ui/pb-ui-tests.exe"; do
  "$mingw/bin/llvm-strip" -o "$dest/$(basename "$exe")" "$exe"
done
cp "$qt"/plugins/platforms/{qwindows,qoffscreen}.dll "$dest/plugins/platforms/"  # offscreen: tests, --screenshot
cp "$qt"/plugins/imageformats/{qjpeg,qsvg}.dll "$dest/plugins/imageformats/"     # mock photos, ui/brand/*.svg
printf '[Paths]\nPrefix = .\nPlugins = plugins\nQmlImports = qml\n' >"$dest/qt.conf"
# Offscreen on Windows has no system fonts, only <Prefix>/lib/fonts: Linux's fallback (fontconfig's on 24.04) for
# what the app's fonts lack (⇧, ✓, ▸), so tests and shots draw those as on Linux.
mkdir -p "$dest/lib/fonts"
cp /usr/share/fonts/truetype/dejavu/DejaVuSans{,-Bold}.ttf "$dest/lib/fonts/"
cp "$root"/ui/tests/*.qml "$dest/tests/"
mkdir -p "$dest/qml"
cp "$root"/ui/qml/*.js "$dest/qml/"  # tests import "../qml/<name>.js" from the source tree

# The QML modules ui/ imports, as the build tools see them, and QtQuick.Controls, which attached
# tooltips import; each one's own files only, since a module's folder also holds other modules.
# No style but Basic (look.h sets it): QtQuick.Dialogs' quickimpl brings the others in with its
# +Fusion, +Material… files, which load only under those styles (~17 MB, U23).
styles='/QtQuick/(Controls/(Fusion|Imagine|Material|Universal|FluentWinUI3|Windows)|NativeStyle)(/|$)'
{
  "$QT_ROOT_DIR/libexec/qmlimportscanner" -rootPath "$root/ui/qml" -rootPath "$root/ui/tests" -importPath "$qt/qml" |
    python3 -c 'import json, sys; [print(m["path"]) for m in json.load(sys.stdin) if m.get("path", "").startswith(sys.argv[1])]' "$qt/qml/"
  echo "$qt/qml/QtQuick/Controls"
} | sort -u | grep -Ev "$styles" | while read -r dir; do
  mkdir -p "$dest/qml/${dir#"$qt/qml/"}"
  find "$dir" -maxdepth 1 -type f -exec cp {} "$dest/qml/${dir#"$qt/qml/"}/" \;
done

# Every DLL those load, from Qt or the toolchain; the rest come with Windows.
todo=$(find "$dest" -name '*.exe' -o -name '*.dll')
while [ -n "$todo" ]; do
  next=
  for f in $todo; do
    for dll in $("$mingw/bin/llvm-readobj" --coff-imports "$f" | sed -n 's/^ *Name: \(.*\.dll\)$/\1/p'); do
      [ -f "$dest/$dll" ] && continue
      for dir in "$qt/bin" "$mingw/x86_64-w64-mingw32/bin"; do
        if [ -f "$dir/$dll" ]; then cp "$dir/$dll" "$dest/"; next="$next $dest/$dll"; break; fi
      done
    done
  done
  todo=$next
done
du -sh "$dest"
