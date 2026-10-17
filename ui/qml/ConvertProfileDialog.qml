pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Edit › Convert to Profile (build/proto/ColorSettings.dc.html): the colours of Docs.current into another profile of its
// mode, with an intent, Black Point Compensation, dither, and flattened if asked so the look holds. OK is one History
// step, "Convert to Profile" (flattening in it); Hủy, Esc, × leave it. Preview only switches: the mock's image does not
// follow (decision 3 of docs/ui-tasks.md).
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property string source: doc ? doc.profile : ""
    readonly property var profiles: doc && doc.mode === "Gray" ? ["Gray Gamma 2.2", "Gray Gamma 1.8", "sGray", "Dot Gain 20%"]
                                                               : ["sRGB IEC61966-2.1", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"]
    property string target: profiles.find(p => p !== source) ?? ""
    property string intent: "Perceptual"
    property bool bpc: true
    property bool dither: true
    property bool flatten: false
    property bool preview: true

    title: qsTr("Convert to Profile")
    boxWidth: 460
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, disabled: !dlg.doc, act: () => dlg.apply() }]

    function apply() {
        if (doc) {
            doc.beginGesture()  // the flattening joins the conversion's step
            if (doc.run({ op: "convertProfile", profile: target, intent: intent, bpc: bpc, dither: dither }) && flatten)
                doc.run({ op: "flattenImage" })
            doc.endGesture()
        }
        close()
    }

    component Label: Text {
        Layout.preferredWidth: 90
        color: Theme.t2
        font.pointSize: Theme.pt(12)
    }

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 10
        rowSpacing: 10
        Label { text: qsTr("Nguồn") }
        Text {
            Layout.fillWidth: true
            text: dlg.source
            color: Theme.t1
            font.pointSize: Theme.pt(12)
            elide: Text.ElideRight
        }
        Label { text: qsTr("Đích") }
        Select {
            Layout.fillWidth: true
            options: dlg.profiles.map(p => [p, p])
            value: dlg.target
            onPicked: v => dlg.target = v
        }
        Label { text: qsTr("Intent") }
        Select {
            Layout.fillWidth: true
            options: ["Perceptual", "Saturation", "Relative Colorimetric", "Absolute Colorimetric"].map(p => [p, p])
            value: dlg.intent
            onPicked: v => dlg.intent = v
        }
        Item { implicitHeight: 1 }
        ColumnLayout {
            spacing: 6
            CheckRow {
                text: qsTr("Black Point Compensation")
                checked: dlg.bpc
                onClicked: dlg.bpc = !dlg.bpc
            }
            CheckRow {
                text: qsTr("Dither")
                checked: dlg.dither
                onClicked: dlg.dither = !dlg.dither
            }
            CheckRow {
                text: qsTr("Làm phẳng ảnh để giữ diện mạo")
                checked: dlg.flatten
                onClicked: dlg.flatten = !dlg.flatten
            }
        }
    }
    CheckRow {  // on the footer's left (after the buttons in Tab order)
        parent: dlg.footer
        x: 16
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Preview")
        checked: dlg.preview
        onClicked: dlg.preview = !dlg.preview
    }
}
