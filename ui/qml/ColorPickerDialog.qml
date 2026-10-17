pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Color.js" as Color

// The Color Picker (docs/ui-tasks.md, U11; build/proto/ColorPicker.dc.html): the square and strip of ColorField on the
// channel of the radio picked, the new colour over the current one, H S B, R G B, L a b and the hex, Thêm vào Swatches.
// params: { color (the current one, "#rrggbb"; Tools.fg when none), title (after "Color Picker ·"; "màu trước" when
// none), picked(c) }: OK hands picked "#rrggbb", or without picked makes it the foreground colour (Tools.setFg); Hủy,
// Esc, × change nothing.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property string what: params.title !== undefined ? String(params.title) : qsTr("màu trước")
    readonly property var start: Color.parse(params.color !== undefined ? params.color : Tools.fg) || [0, 0, 0, 1]
    property real h
    property real s
    property real v
    property string ch: "H"  // the channel of the strip, the prototype's keys: H S Bh R G Bl L a b
    property string added  // the hex Thêm vào Swatches took, so the button says so
    readonly property var rgb: Color.hsv2rgb(h, s, v)
    readonly property var lab: Color.rgb2lab(rgb)
    readonly property string hex: Color.hex(rgb)
    // the nine fields as the prototype's grid, a row of three: [key, letter, name, value, unit, min, max, set(x)]
    readonly property var fields: [
        ["H", "H", qsTr("Hue"), h, "°", 0, 360, x => set({ h: x, s: s, v: v })],
        ["R", "R", qsTr("Red"), rgb[0], "", 0, 255, x => setRgb(0, x)],
        ["L", "L", qsTr("Lab L"), lab[0], "", 0, 100, x => setLab(0, x)],
        ["S", "S", qsTr("Saturation"), s * 100, "%", 0, 100, x => set({ h: h, s: x / 100, v: v })],
        ["G", "G", qsTr("Green"), rgb[1], "", 0, 255, x => setRgb(1, x)],
        ["a", "a", qsTr("Lab a"), lab[1], "", -128, 127, x => setLab(1, x)],
        ["Bh", "B", qsTr("Brightness"), v * 100, "%", 0, 100, x => set({ h: h, s: s, v: x / 100 })],
        ["Bl", "B", qsTr("Blue"), rgb[2], "", 0, 255, x => setRgb(2, x)],
        ["b", "b", qsTr("Lab b"), lab[2], "", -128, 127, x => setLab(2, x)]
    ]

    title: qsTr("Color Picker")
    boxWidth: 640
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.ok() }]

    Component.onCompleted: set(Color.rgb2hsv(start, params.picked ? 0 : Tools.fgHue))
    function set(c) {
        h = c.h
        s = c.s
        v = c.v
    }
    function setRgb(i, x) {
        const c = rgb.slice()
        c[i] = x
        set(Color.rgb2hsv(c, h))
    }
    function setLab(i, x) {
        const l = lab.slice()
        l[i] = x
        set(Color.rgb2hsv(Color.lab2rgb(l), h))
    }
    function ok() {
        const c = "#" + hex.toLowerCase()
        if (typeof params.picked === "function")
            params.picked(c)
        else
            Tools.setFg(c, h, true)
        close()
    }

    header: Item {  // the title and, lighter, what is picked: "Color Picker · màu trước"
        implicitHeight: 46
        Row {
            x: 18
            height: parent.height
            spacing: 4
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: dlg.title
                color: Theme.t1
                font.pointSize: Theme.pt(14)
                font.weight: Font.DemiBold
            }
            Text {
                visible: dlg.what !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: "· " + dlg.what
                color: Theme.t3
                font.pointSize: Theme.pt(14)
            }
        }
        IconButton {
            x: parent.width - 8 - width
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 30
            implicitHeight: 30
            icon.name: "close"
            icon.width: 18
            ToolTip.text: qsTr("Đóng")
            onClicked: dlg.close()
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
    }

    Row {
        spacing: 16
        ColorField {
            id: field
            h: dlg.h
            s: dlg.s
            v: dlg.v
            channel: dlg.ch
            onMoved: c => dlg.set(c)
        }
        Column {
            width: dlg.width - 36 - field.width - 16
            spacing: 10
            Row {
                spacing: 10
                Item {  // the new colour over the current one, 72 wide
                    width: 72
                    height: 72
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -1
                        radius: 6
                        color: Theme.ln
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 5
                        color: Color.css(dlg.start)
                    }
                    Rectangle {
                        width: parent.width
                        height: 36
                        radius: 5
                        color: Color.css(dlg.rgb)
                        Rectangle { y: 30; width: parent.width; height: 6; color: parent.color }
                    }
                }
                Column {
                    height: 72
                    Text {
                        height: 36
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("mới")
                        color: Theme.t3
                        font.pointSize: Theme.pt(11)
                    }
                    Text {
                        height: 36
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("hiện tại")
                        color: Theme.t3
                        font.pointSize: Theme.pt(11)
                    }
                }
            }
            Btn {
                text: dlg.added === dlg.hex ? qsTr("Đã thêm vào Swatches") : qsTr("Thêm vào Swatches")
                onClicked: {
                    Prefs.set("swatches", Color.addSwatch(Prefs.values.swatches, "#" + dlg.hex.toLowerCase()))
                    dlg.added = dlg.hex
                }
            }
            Grid {
                id: grid
                width: parent.width
                columns: 3
                rowSpacing: 6
                columnSpacing: 10
                Repeater {
                    model: 9  // the fields stay while the colour moves; only what they show changes
                    Row {
                        id: cell
                        required property int index
                        readonly property var f: dlg.fields[index]
                        width: (grid.width - 20) / 3
                        spacing: 6
                        StrokeDialog.Radio {
                            width: 33
                            anchors.verticalCenter: parent.verticalCenter
                            text: cell.f[1]
                            checked: dlg.ch === cell.f[0]
                            Accessible.name: qsTr("Ô màu theo %1").arg(cell.f[2])
                            onClicked: dlg.ch = cell.f[0]
                        }
                        NumberField {
                            width: parent.width - 39
                            value: Math.round(cell.f[3])
                            unit: cell.f[4]
                            min: cell.f[5]
                            max: cell.f[6]
                            Accessible.name: cell.f[2]
                            onMoved: x => cell.f[7](x)
                        }
                    }
                }
            }
            TextBox {
                id: hexBox
                width: 150
                implicitHeight: 24
                leftPadding: 20
                font.family: Theme.mono
                font.pointSize: Theme.pt(12)
                Accessible.name: qsTr("Mã hex")
                Component.onCompleted: text = dlg.hex
                Connections {
                    target: dlg
                    function onHexChanged() { if (!hexBox.activeFocus) hexBox.text = dlg.hex }
                }
                onEditingFinished: {
                    const c = Color.parseHex(text)
                    if (c)
                        dlg.set(Color.rgb2hsv(c, dlg.h))
                    text = dlg.hex
                }
                onAccepted: focus = false
                Keys.onEscapePressed: e => {  // nothing typed: on to the dialog, which closes
                    e.accepted = text !== dlg.hex
                    text = dlg.hex
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
        }
    }
}
