import QtQuick
import QtQuick.Layouts

// Select › Modify › Border…, Smooth…, Expand…, Contract…, Feather… (U25, build/design2/project/SelectModify.dc.html):
// params.kind picks one of `kinds` (Feather without one), a number of px in its range and, but for Border, Apply effect at
// canvas bounds. OK is one History step on Docs.current named as Photoshop's ({ op: "step", modify: kind, px,
// canvasBounds }); the mock's selection keeps its shape. Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    // The artboard's dialogs (its D() rows): title, label, default, min, max, decimals, the bounds box; then the step's name.
    readonly property var kinds: ({
        border: [qsTr("Border Selection"), qsTr("Width"), 10, 1, 200, 0, false, qsTr("Border")],
        smooth: [qsTr("Smooth Selection"), qsTr("Sample Radius"), 5, 1, 500, 0, true, qsTr("Smooth")],
        expand: [qsTr("Expand Selection"), qsTr("Expand By"), 5, 1, 500, 0, true, qsTr("Expand")],
        contract: [qsTr("Contract Selection"), qsTr("Contract By"), 5, 1, 500, 0, true, qsTr("Contract")],
        feather: [qsTr("Feather Selection"), qsTr("Feather Radius"), 2, 0.1, 1000, 1, true, qsTr("Feather")]
    })
    readonly property string kind: params.kind in kinds ? params.kind : "feather"
    readonly property var spec: kinds[kind]
    property real px: spec[2]
    property bool bounds: false  // Apply effect at canvas bounds

    title: spec[0]
    boxWidth: 300
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function apply() {
        if (doc)
            doc.run({ op: "step", name: spec[7], icon: "select", modify: kind, px: px, canvasBounds: spec[6] && bounds })
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        RowLayout {
            spacing: 10
            Text {
                Layout.preferredWidth: 100
                text: dlg.spec[1]
                color: Theme.t2
                font.pointSize: Theme.pt(12)
            }
            NumberField {
                boxHeight: 28
                Layout.preferredWidth: 110
                value: dlg.px
                unit: "px"
                min: dlg.spec[3]
                max: dlg.spec[4]
                decimals: dlg.spec[5]
                step: Math.pow(10, -dlg.spec[5])
                onMoved: v => dlg.px = v
            }
        }
        CheckRow {
            visible: dlg.spec[6]
            Layout.leftMargin: 110
            spacing: 8
            wrapWidth: 154  // two lines, as the artboard's 154 px cell
            text: qsTr("Apply effect at canvas bounds")
            checked: dlg.bounds
            onClicked: dlg.bounds = !dlg.bounds
        }
    }
}
