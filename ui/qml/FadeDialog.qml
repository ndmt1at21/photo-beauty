pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "Mock.js" as Mock

// Edit › Fade… (docs/ui-tasks.md, U25; build/design2/project/EditDialogs.dc.html, Fade): the last step at an Opacity
// and a Mode, as Photoshop's Fade. The title names that step, as the menu item does (MenuSpec's fade). OK is one History
// step "Fade <step>" on Docs.current, which the mock does not draw ({ op: "step" } with the values); Hủy, Esc, × leave
// it. Preview keeps its tick: decision 3, the mock shows nothing.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    property var last: null  // the step faded, as the dialog opened: OK's step must not rename it while it fades out
    property real fadeOpacity: 100  // %
    property string mode: "normal"  // a key of Mock.BLENDS but Pass Through
    property bool preview: true
    readonly property var modes: Mock.BLENDS.slice(1).map((b, i) => [b, Mock.BLEND_LABELS[i + 1]])

    title: last ? qsTr("Fade %1").arg(last.name) : qsTr("Fade")
    boxWidth: 400
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, disabled: !dlg.last, act: () => dlg.apply() }]
    Component.onCompleted: last = doc && doc.index > 0 ? doc.history[doc.index] : null

    function apply() {
        if (last)
            doc.run({ op: "step", name: title, icon: last.icon, opacity: fadeOpacity / 100, mode: mode })
        close()
    }

    component Plain: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
    }

    Item {  // the artboard's .body is 14 px under the title, DialogBox's 16
        width: parent.width
        implicitHeight: lay.implicitHeight - 2
        ColumnLayout {
            id: lay
            y: -2
            width: parent.width
            spacing: 12
            Item {  // Opacity: the number over the slider, as the filters' rows
                Layout.fillWidth: true
                implicitHeight: 35  // the field 26, 6, the 3 px track
                RowLayout {
                    width: parent.width
                    spacing: 10
                    Plain {
                        Layout.fillWidth: true
                        text: qsTr("Opacity")
                    }
                    NumberField {
                        boxHeight: 26
                        Layout.preferredWidth: 86
                        unitApart: true
                        horizontalAlignment: TextInput.AlignRight
                        value: dlg.fadeOpacity
                        unit: "%"
                        min: 0
                        max: 100
                        Accessible.name: qsTr("Opacity, phần trăm")
                        onMoved: v => dlg.fadeOpacity = v
                    }
                }
                SliderField {
                    y: 32
                    width: parent.width
                    value: dlg.fadeOpacity
                    min: 0
                    max: 100
                    onMoved: v => dlg.fadeOpacity = v
                }
            }
            RowLayout {
                spacing: 10
                Plain {
                    Layout.preferredWidth: 60
                    text: qsTr("Mode")
                }
                Dropdown {
                    implicitHeight: 26
                    Layout.fillWidth: true
                    options: dlg.modes
                    value: dlg.mode
                    onPicked: v => dlg.mode = v
                }
            }
            CheckRow {
                spacing: 8
                text: qsTr("Preview")
                checked: dlg.preview
                onClicked: dlg.preview = !dlg.preview
            }
        }
    }
}
