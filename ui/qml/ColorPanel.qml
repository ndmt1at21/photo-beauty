pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Color.js" as Color
import "Fmt.js" as Fmt

// The Color panel (docs/ui-tasks.md, U11; PanelsB's Color, the col part of Main.dc.html), a tab of the dock's group
// Color, Swatches, Gradients: the foreground and background colours (a click opens the Color Picker); the foreground's
// H S B, R G B or L a b on sliders whose tracks follow it, each with its number, or a colour wheel and Brightness
// (Tools.colorMode: HSB, RGB, Lab, Wheel); its hex; + adds it to Swatches (Color.addSwatch).
Item {
    id: panel
    readonly property var rgb: Color.parseHex(Tools.fg) || [0, 0, 0]
    readonly property var hsb: Color.rgb2hsv(rgb, Tools.fgHue)
    // the prototype's Hv: the hue kept while the colour is all but grey or black
    readonly property real hue: hsb.s < 0.005 || hsb.v < 0.005 ? Tools.fgHue : hsb.h
    readonly property var lab: Color.rgb2lab(rgb)
    readonly property bool wheel: Tools.colorMode === "Wheel"
    readonly property var modes: [["HSB", "HSB"], ["RGB", "RGB"], ["Lab", "Lab"], ["Wheel", qsTr("Color Wheel")]]
    property var menu: modes.map(m => ({ l: m[1], check: Tools.colorMode === m[0], act: () => { Tools.colorMode = m[0] } }))
                            .concat([{ t: "sep" }, { l: qsTr("Thêm màu trước vào Swatches"), act: () => panel.addSwatch() }])
    implicitWidth: 308
    implicitHeight: wheel ? 204 : 124

    function setHsb(h, s, v) { Tools.setFg(Color.css(Color.hsv2rgb(h, s, v)), h) }
    function setRgb(i, x) {
        const c = rgb.slice()
        c[i] = x
        Tools.setFg(Color.css(c))
    }
    function setLab(i, x) {
        const l = lab.slice()
        l[i] = x
        Tools.setFg(Color.css(Color.lab2rgb(l)))
    }
    function addSwatch() { Prefs.set("swatches", Color.addSwatch(Prefs.values.swatches, Tools.fg)) }
    function pick(front) {  // the Color Picker on the foreground (it sets Tools.fg) or the background colour
        (panel.Window.window as Main)?.openDialog("ColorPicker", front ? { color: Tools.fg, title: qsTr("màu trước") }
                                                                       : { color: Tools.bg, title: qsTr("màu sau"), picked: c => { Tools.bg = c } })
    }
    // The three sliders of the mode: [letter, min, max, value, unit, track colours, name, set(x)], as the prototype's csl
    readonly property var bars: {
        const c = rgb, l = lab, H = hue, S = hsb.s * 100, B = hsb.v * 100, hx = Color.css
        if (Tools.colorMode === "RGB")
            return [["R", 0, 255, c[0], "", [hx([0, c[1], c[2]]), hx([255, c[1], c[2]])], qsTr("Red"), x => setRgb(0, x)],
                    ["G", 0, 255, c[1], "", [hx([c[0], 0, c[2]]), hx([c[0], 255, c[2]])], qsTr("Green"), x => setRgb(1, x)],
                    ["B", 0, 255, c[2], "", [hx([c[0], c[1], 0]), hx([c[0], c[1], 255])], qsTr("Blue"), x => setRgb(2, x)]]
        if (Tools.colorMode === "Lab")
            return [["L", 0, 100, l[0], "", ["#000000", "#ffffff"], qsTr("Lightness"), x => setLab(0, x)],
                    ["a", -128, 127, l[1], "", [hx(Color.lab2rgb([l[0], -100, l[2]])), hx(Color.lab2rgb([l[0], 100, l[2]]))], "a", x => setLab(1, x)],
                    ["b", -128, 127, l[2], "", [hx(Color.lab2rgb([l[0], l[1], -100])), hx(Color.lab2rgb([l[0], l[1], 100]))], "b", x => setLab(2, x)]]
        return [["H", 0, 360, H, "°", ["#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000"], qsTr("Hue"), x => setHsb(x, S / 100, B / 100)],
                ["S", 0, 100, S, "%", [hx(Color.hsv2rgb(H, 0, B / 100)), hx(Color.hsv2rgb(H, 1, B / 100))], qsTr("Saturation"), x => setHsb(H, x / 100, B / 100)],
                ["B", 0, 100, B, "%", ["#000000", hx(Color.hsv2rgb(H, S / 100, 1))], qsTr("Brightness"), x => setHsb(H, S / 100, x / 100)]]
    }

    // A 50 × 20 number box (the prototype's .fld of the sliders): Enter or leaving it applies what was typed, Esc drops it, ↑ ↓.
    component Val: TextField {
        id: val
        required property var bar
        readonly property string shown: Fmt.vn(Math.round(bar[3])) + bar[4]
        width: 50
        height: 20
        padding: 0
        leftPadding: 6
        rightPadding: 6
        verticalAlignment: TextInput.AlignVCenter
        font.family: Theme.mono
        font.pointSize: Theme.pt(11.5)
        color: Theme.t1
        selectionColor: Theme.accent
        selectedTextColor: Theme.acct
        selectByMouse: true
        Accessible.name: qsTr("%1, số").arg(bar[6])
        background: Rectangle {
            radius: 4
            color: Theme.p2
            border.width: val.activeFocus ? 1 : 0
            border.color: Theme.accent
        }
        function apply(x) {
            x = Math.round(Color.clamp(x, bar[1], bar[2]))
            if (!isNaN(x) && x !== Math.round(bar[3]))
                bar[7](x)
        }
        Component.onCompleted: text = shown
        onShownChanged: if (!activeFocus) text = shown
        onEditingFinished: {
            const x = Fmt.pnum(text)
            if (x !== null)
                apply(x)
            text = shown
        }
        onAccepted: focus = false  // so it follows the colour again
        Keys.onEscapePressed: e => {  // nothing typed: on to the window
            e.accepted = text !== shown
            text = shown
            focus = !e.accepted
        }
        Keys.onUpPressed: e => apply(Math.round(bar[3]) + (e.modifiers & Qt.ShiftModifier ? 10 : 1))
        Keys.onDownPressed: e => apply(Math.round(bar[3]) - (e.modifiers & Qt.ShiftModifier ? 10 : 1))
    }
    // "#" and the hex of the foreground: Enter or leaving it applies six hex digits, Esc drops them.
    component Hex: TextBox {
        id: hexBox
        property string value
        signal picked(string c)
        implicitHeight: 24
        leftPadding: 20
        font.family: Theme.mono
        font.pointSize: Theme.pt(11.5)
        Component.onCompleted: text = value
        onValueChanged: if (!activeFocus) text = value
        onEditingFinished: {
            if (/^#?[0-9a-fA-F]{6}$/.test(text.trim()))
                picked(Color.css(Color.parseHex(text)))
            text = value
        }
        onAccepted: focus = false
        Keys.onEscapePressed: e => {  // nothing typed: on to the window
            e.accepted = text !== value
            text = value
            focus = !e.accepted
        }
        Text {
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            text: "#"
            color: Theme.t3
            font: hexBox.font
        }
    }

    // Bars: the colours, then a slider a channel
    Row {
        visible: !panel.wheel
        x: 12
        y: 10
        width: parent.width - 24
        spacing: 12
        ColorPair {
            fg: Tools.fg
            bg: Tools.bg
            onFgClicked: panel.pick(true)
            onBgClicked: panel.pick(false)
        }
        Column {
            width: parent.width - 52
            spacing: 6
            Repeater {
                model: 3  // the rows stay while the colour moves; only what they show changes
                Row {
                    id: row
                    required property int index
                    readonly property var bar: panel.bars[index]
                    height: 20
                    spacing: 8
                    Text {
                        width: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.bar[0]
                        color: Theme.t3
                        font.pointSize: Theme.pt(12)
                    }
                    SliderField {
                        width: row.parent.width - 10 - 50 - 16
                        anchors.verticalCenter: parent.verticalCenter
                        value: Math.round(row.bar[3])
                        min: row.bar[1]
                        max: row.bar[2]
                        track: row.bar[5]
                        Accessible.name: row.bar[6]
                        onMoved: x => row.bar[7](x)
                    }
                    Val { bar: row.bar }
                }
            }
        }
    }

    // The wheel: hue and saturation round it, Brightness beside
    Row {
        visible: panel.wheel
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 16
        ColorField {
            wheel: true
            size: 150
            h: panel.hue
            s: panel.hsb.s
            v: panel.hsb.v
            onMoved: c => panel.setHsb(c.h, c.s, c.v)
        }
        Column {
            width: parent.width - 166
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            ColorPair {
                fg: Tools.fg
                bg: Tools.bg
                onFgClicked: panel.pick(true)
                onBgClicked: panel.pick(false)
            }
            SliderField {
                width: parent.width
                label: qsTr("Brightness")
                value: Math.round(panel.hsb.v * 100)
                unit: "%"
                track: ["#000000", Color.css(Color.hsv2rgb(panel.hue, panel.hsb.s, 1))]
                onMoved: x => panel.setHsb(panel.hue, panel.hsb.s, x / 100)
            }
        }
    }

    // The mode, the hex, + to Swatches
    Row {
        x: 12
        y: parent.height - 34
        width: parent.width - 24
        spacing: 8
        Dropdown {
            id: mode
            width: Math.round((panel.wheel ? 100 : 72) * Theme.fontScale)  // Lớn needs more for "Color Wheel"
            options: panel.modes
            value: Tools.colorMode
            Accessible.name: qsTr("Kiểu thanh màu")
            onPicked: v => { Tools.colorMode = v }
        }
        Hex {
            width: parent.width - mode.width - 26 - 16
            value: Color.hex(panel.rgb)
            Accessible.name: qsTr("Mã hex của màu trước")
            onPicked: c => Tools.setFg(c)
        }
        ToolToggle {
            icon.name: "add"
            icon.width: 16
            ToolTip.text: qsTr("Thêm vào Swatches · Add to Swatches")
            Accessible.name: qsTr("Thêm màu trước vào Swatches")
            onClicked: panel.addSwatch()
        }
    }
}
