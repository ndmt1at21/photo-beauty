#!/bin/bash
# Renders icon.svg (the app icon of build/proto/Splash.dc.html; icon-16.svg, the same on whole pixels, for 16 px) into
# the files the packages take: photo-beauty.ico (Windows: 16, 32, 48, 256 px), photo-beauty.icns (macOS: 32 to 1.024
# px, the tile 824 of 1.024 px as Apple's icon grid has it) and icon.png (Linux: 512 px). Qt's SVG renderer draws,
# Pillow packs. Rerun after changing an SVG, in Ubuntu-24.04 (Qt in $QT_ROOT_DIR, Pillow in ~/.venv-tools); from
# 20.04: tools/ui.sh ui/brand/render.sh
set -euo pipefail
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/render.qml" <<'QML'
import QtQuick
// The icon at each size, full or on Apple's grid (mac-<size>.png), grabbed once the window has shown.
Window {
    id: w
    readonly property var args: Qt.application.arguments  // …, the SVGs' folder, the PNGs'
    readonly property var jobs: [16, 32, 48, 256, 512].map((s) => ({ s: s, mac: false })).concat([32, 64, 128, 256, 512, 1024].map((s) => ({ s: s, mac: true })))
    property bool started
    property int saved
    width: 1024
    height: 1024
    visible: true
    color: "transparent"
    onFrameSwapped: {
        if (started)
            return
        started = true
        for (let i = 0; i < shots.count; i++)
            shots.itemAt(i).save()
    }
    Repeater {
        id: shots
        model: w.jobs
        Item {
            id: shot
            required property var modelData
            width: modelData.s
            height: modelData.s
            Image {
                readonly property int s: shot.modelData.mac ? Math.round(shot.modelData.s * 824 / 1024) : shot.modelData.s
                anchors.centerIn: parent
                source: "file://" + w.args[w.args.length - 2] + (shot.modelData.s === 16 && !shot.modelData.mac ? "/icon-16.svg" : "/icon.svg")
                sourceSize: Qt.size(s, s)
            }
            function save() {
                grabToImage((r) => {
                    r.saveToFile(w.args[w.args.length - 1] + "/" + (shot.modelData.mac ? "mac-" : "") + shot.modelData.s + ".png")
                    if (++w.saved === w.jobs.length)
                        Qt.quit()
                })
            }
        }
    }
}
QML
QT_QPA_PLATFORM=offscreen "$QT_ROOT_DIR/bin/qml" "$tmp/render.qml" -- "$PWD" "$tmp"
~/.venv-tools/bin/python - "$tmp" <<'PY'
import sys
from PIL import Image
png = lambda name: Image.open(f"{sys.argv[1]}/{name}.png").convert("RGBA")
png(512).save("icon.png")
win = [png(s) for s in (16, 32, 48, 256)]
win[-1].save("photo-beauty.ico", sizes=[im.size for im in win], append_images=win[:-1])
png("mac-1024").save("photo-beauty.icns", append_images=[png(f"mac-{s}") for s in (32, 64, 128, 256, 512)])
PY
ls -l icon.png photo-beauty.ico photo-beauty.icns
