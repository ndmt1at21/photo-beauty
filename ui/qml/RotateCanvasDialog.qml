pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// Image › Image Rotation › Tùy ý… (docs/ui-tasks.md, U25; build/design2/project/ImageDialogs.dc.html, Rotate Canvas):
// an angle, −359,99 to 359,99° with two decimals, clockwise or counterclockwise. OK is one History step "Rotate Canvas"
// on Docs.current; the mock turns the image by quarters only (Mock's rotateCanvas, inside the step), so another angle
// is the step alone, and 0° none. Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    property real angle: 0  // °
    property bool clockwise: true

    title: qsTr("Rotate Canvas")
    boxWidth: 360
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function apply() {
        const a = clockwise ? angle : -angle, q = (a % 360 + 360) % 360  // ° clockwise; as 0..360
        if (doc && a !== 0) {
            doc.beginGesture()
            doc.run({ op: "step", name: qsTr("Rotate Canvas"), icon: "rotate_right", angle: a })
            if (q % 90 === 0)
                doc.run({ op: "rotateCanvas", angle: q })
            doc.endGesture()
        }
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        RowLayout {
            spacing: 10
            Text {
                Layout.preferredWidth: 60
                text: qsTr("Góc")
                color: Theme.t2
                font.pointSize: Theme.pt(12)
            }
            NumberField {
                boxHeight: 28
                Layout.preferredWidth: 96
                value: dlg.angle
                unit: "°"
                decimals: 2
                min: -359.99
                max: 359.99
                Accessible.name: qsTr("Góc, độ")
                onMoved: v => dlg.angle = v
            }
        }
        Column {
            Layout.leftMargin: 70
            spacing: 8
            Radio {
                spacing: 8
                text: qsTr("Chiều kim đồng hồ")
                checked: dlg.clockwise
                onClicked: dlg.clockwise = true
            }
            Radio {
                spacing: 8
                text: qsTr("Ngược chiều kim đồng hồ")
                checked: !dlg.clockwise
                onClicked: dlg.clockwise = false
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Canvas nới ra cho vừa ảnh đã xoay; phần mới của Background tô màu sau.")
            color: Theme.t3
            font.pointSize: Theme.pt(11)
            lineHeightMode: Text.FixedHeight
            lineHeight: Math.round(11 * 1.35 * Theme.fontScale)
            wrapMode: Text.Wrap
        }
    }
}
