// copied from snapone 6bdb5c8 (ui/qml/Theme.qml); values from the Tokens artboard of photo-beauty
pragma Singleton
import QtQuick

// Design tokens of the prototype (Tokens artboard). Names follow its CSS variables.
QtObject {
    property bool dark: true
    // Màu nhấn of Thiết lập in the order of the prototype's .a0–.a3; the first is the default.
    readonly property var accents: ["#2FB5B0", "#4C8DF6", "#E0A53A", "#E8664A"]
    property color accent: accents[0]

    readonly property color bg: dark ? "#161617" : "#e7e7e8"  // window, top bar
    readonly property color p1: dark ? "#1c1c1e" : "#f3f3f3"  // panels, toolbar, open tab
    readonly property color p2: dark ? "#252527" : "#e3e3e4"  // fields, popups, menus
    readonly property color p3: dark ? "#323235" : "#d3d3d5"  // button pressed, tool in use
    readonly property color ln: dark ? "#2b2b2e" : "#d2d2d4"  // rules between areas
    readonly property color t1: dark ? "#ececed" : "#19191a"  // main text
    readonly property color t2: dark ? "#b4b4b7" : "#3c3c3f"  // secondary text, icons
    readonly property color t3: dark ? "#86868b" : "#636367"  // labels, shortcuts, notes
    readonly property color t4: dark ? "#5d5d62" : "#98989c"  // not usable right now
    readonly property color view: dark ? "#282828" : "#8e8e8e"  // around the image
    readonly property color trk: dark ? "#3a3a3e" : "#c8c8cb"  // slider track
    readonly property color thumb: dark ? "#dededf" : "#242426"  // slider knob
    readonly property color sel: dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.06)  // hover, selection
    // Nền quanh ảnh of Thiết lập: Đen, Xám đậm, Xám vừa, Xám nhạt; `view` is the theme's own.
    readonly property var views: ["#000000", "#282828", "#535353", "#8e8e8e"]
    readonly property color acc2: Qt.rgba(accent.r, accent.g, accent.b, 0.16)  // selected layer row, button on
    readonly property color acct: "#1d0f0b"  // text on accent
    readonly property color danger: "#ef5350"  // errors, delete, out of gamut
    readonly property color warn: "#e5a33a"  // notice bar, recovery, memory running low
    readonly property color closeFill: "#c42b1c"  // the window's close button under the mouse, as Windows 11
    readonly property color closeInk: "#ffffff"
    readonly property color knob: "#f5f5f5"  // switch knob
    // Chrome's native checkbox and radio (the prototype's inputs with accent-color): the box, its edge, the tick on accent.
    readonly property color checkFill: dark ? "#3b3b3b" : "#ffffff"
    readonly property color checkLine: dark ? "#858585" : "#767676"
    readonly property color checkTick: "#3b3b3b"
    readonly property color scrim: Qt.rgba(0, 0, 0, 0.5)  // behind dialogs
    readonly property color shadow: Qt.rgba(0, 0, 0, 0.45)
    readonly property color onPhoto: Qt.rgba(0, 0, 0, 0.55)  // labels over the image
    // Data colours: histogram channels R, G, B; Curves grid and backdrop.
    readonly property color h1: dark ? Qt.rgba(214 / 255, 71 / 255, 61 / 255, 0.8) : Qt.rgba(240 / 255, 120 / 255, 110 / 255, 0.9)
    readonly property color h2: dark ? Qt.rgba(71 / 255, 179 / 255, 92 / 255, 0.8) : Qt.rgba(120 / 255, 199 / 255, 130 / 255, 0.9)
    readonly property color h3: dark ? Qt.rgba(66 / 255, 110 / 255, 224 / 255, 0.85) : Qt.rgba(120 / 255, 150 / 255, 240 / 255, 0.9)
    readonly property var hist: [h1, h2, h3]
    // How overlapping channels mix (--hmix: screen on dark, multiply on light) as a Canvas
    // globalCompositeOperation: fill the canvas with the colour under it, then draw h1–h3 with it.
    readonly property string hmix: dark ? "qt-screen" : "qt-multiply"
    readonly property color grid: dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.11)
    readonly property color faint: dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.07)

    // Cỡ chữ in Thiết lập (Nhỏ, Vừa, Lớn: 11, 12, 13.5 px). Every text size goes through pt(), so
    // all text follows; icons do not.
    property string fontSize: "m"
    readonly property var fontScales: ({ s: 11 / 12, m: 1, l: 13.5 / 12 })
    readonly property real fontScale: fontScales[fontSize] ?? 1

    // font.pointSize for a CSS pixel size: font.pixelSize is an int and the prototype uses half
    // pixels (12.5, 11.5, 10.5). Qt's logical DPI is 96 on Windows/Linux and 72 on macOS.
    function pt(px) { return px * fontScale * (Qt.platform.os === "osx" ? 1 : 0.75) }

    readonly property string sans: "Space Grotesk"
    readonly property string mono: "JetBrains Mono"
    readonly property string icons: "Material Symbols Outlined"
    readonly property string logo: "Manrope"  // the wordmark only, weight 800 (Font.ExtraBold)
}
