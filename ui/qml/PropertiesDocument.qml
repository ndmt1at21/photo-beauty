pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec

// The Properties panel's page with no layer picked ("doc", U35): the document's canvas size, orientation,
// resolution, mode, profile; Select Subject, Remove Background.
PropertiesPage {
    id: page
    objectName: "page:doc"
    implicitHeight: dc.implicitHeight + 18
    ColumnLayout {
        id: dc
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        PropertiesPage.Hd {
            icon: "description"
            text: qsTr("Tài liệu")
        }
        PropertiesPage.Cap { text: qsTr("Canvas") }
        Row {
            spacing: 6
            NumberField {
                width: page.half
                label: "W"
                labelColor: Theme.t3
                unit: "px"
                value: page.cur.w
                min: 1
                max: 300000
                ppi: page.cur.ppi
                Accessible.name: qsTr("Chiều rộng canvas")
                onStarted: page.doc.beginGesture()
                onMoved: v => page.run({ op: "canvasSize", w: v, h: page.doc.h })
                onCommitted: page.doc.endGesture()
            }
            Item {
                width: 24
                height: 24
            }
            NumberField {
                width: page.half
                label: "H"
                labelColor: Theme.t3
                unit: "px"
                value: page.cur.h
                min: 1
                max: 300000
                ppi: page.cur.ppi
                Accessible.name: qsTr("Chiều cao canvas")
                onStarted: page.doc.beginGesture()
                onMoved: v => page.run({ op: "canvasSize", w: page.doc.w, h: v })
                onCommitted: page.doc.endGesture()
            }
        }
        RowLayout {
            spacing: 6
            ToolToggle {
                icon.name: "crop_portrait"
                icon.width: 16
                checked: page.cur.h >= page.cur.w
                ToolTip.text: qsTr("Dọc · Portrait")
                Accessible.name: qsTr("Dọc")
                onClicked: if (!checked) page.run({ op: "swapOrientation" })
            }
            ToolToggle {
                icon.name: "crop_landscape"
                icon.width: 16
                checked: page.cur.w > page.cur.h
                ToolTip.text: qsTr("Ngang · Landscape")
                Accessible.name: qsTr("Ngang")
                onClicked: if (!checked) page.run({ op: "swapOrientation" })
            }
            Item { Layout.fillWidth: true }
            NumberField {
                Layout.preferredWidth: Math.round(150 * Theme.fontScale)  // the artboard's 132 cuts "300 ppi"; Lớn needs more
                label: qsTr("Độ phân giải")
                labelColor: Theme.t3
                unit: "ppi"
                value: page.cur.ppi
                min: 1
                max: 10000
                Accessible.name: qsTr("Độ phân giải")
                onStarted: page.doc.beginGesture()
                onMoved: v => page.run({ op: "imageSize", w: page.doc.w, h: page.doc.h, ppi: v })
                onCommitted: page.doc.endGesture()
            }
        }
        Row {
            spacing: 6
            Dropdown {
                width: (dc.width - 6) * 0.4
                options: Spec.LISTS.docMode
                value: (page.cur.mode === "Gray" ? "Grayscale" : "RGB") + ", " + page.cur.bits + " bit"
                Accessible.name: qsTr("Mode")
                onPicked: v => page.run({ op: "setMode", mode: v.indexOf("Gray") === 0 ? "Gray" : "RGB", bits: v.indexOf("16") > 0 ? 16 : 8 })
            }
            Dropdown {
                width: (dc.width - 6) * 0.6
                options: Spec.LISTS.profile
                value: page.cur.profile
                Accessible.name: qsTr("Hồ sơ màu")
                onPicked: v => page.run({ op: "assignProfile", profile: v })
            }
        }
        PropertiesPage.Cap {
            Layout.topMargin: 4
            text: qsTr("Thao tác nhanh")
        }
        // Side by side as the artboard, a label into the padding (Vừa's "Remove Background" up to 1 px past each side,
        // as the artboard's); one under the other when a label is wider than half (Lớn).
        Grid {
            id: quick
            readonly property real half: (dc.width - 6) / 2
            readonly property bool pair: Math.max(subject.implicitContentWidth, removeBg.implicitContentWidth) <= half + 2
            columns: pair ? 2 : 1
            spacing: 6
            Btn {
                id: subject
                width: quick.pair ? quick.half : dc.width
                implicitHeight: 26
                leftPadding: 10
                rightPadding: 10
                icon.name: "frame_person"
                text: qsTr("Select Subject")
                onClicked: page.run({ op: "select", shape: "subject" })
            }
            Btn {
                id: removeBg
                width: quick.pair ? quick.half : dc.width
                implicitHeight: 26
                leftPadding: 10
                rightPadding: 10
                icon.name: "person"
                text: qsTr("Remove Background")
                onClicked: page.run({ op: "removeBackground" })
            }
        }
    }
}
