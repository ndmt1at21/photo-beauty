import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "FilterSpec.js" as Spec

// Edit › Stroke (build/proto/FillStroke.dc.html, plan.md's Hộp thoại): width and colour of the line, where it runs
// (inside, centred on or outside the edge), Mode, Opacity, Preserve Transparency. OK is one History step on
// Docs.current, which the mock does not draw ({ op: "step" } with the choices); Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    property real strokeWidth: 4  // px
    property color strokeColor: Tools.fg
    property string location: "center"  // of Spec.LOCATIONS
    property string mode: "normal"  // of Spec.MODES
    property real strokeOpacity: 100  // %
    property bool preserve: false

    title: qsTr("Stroke")
    boxWidth: 440
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function apply() {
        if (doc)
            doc.run({ op: "step", name: qsTr("Stroke"), icon: "line_weight", width: strokeWidth, color: strokeColor.toString(), location: location, mode: mode,
                      opacity: strokeOpacity / 100, preserveTransparency: preserve })
        close()
    }

    // A radio button, the prototype's <input type=radio>: `checked` comes from the owner, clicked() asks it to pick this
    // one. Shared with FilterDialog (StrokeDialog.Radio), so this file is not Bound: inline components of a Bound file
    // cannot be made in another.
    component Radio: AbstractButton {
        id: r
        objectName: "radio"
        implicitWidth: implicitContentWidth
        implicitHeight: 20
        hoverEnabled: true
        font.pointSize: Theme.pt(12)
        Accessible.role: Accessible.RadioButton
        Accessible.checked: checked
        contentItem: Row {
            spacing: 6
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 13
                height: 13
                radius: 6.5
                color: "transparent"
                border.width: r.checked ? 1.5 : 1
                border.color: r.checked ? Theme.accent : r.hovered ? Theme.t2 : Theme.t3
                Rectangle {
                    visible: r.checked
                    anchors.centerIn: parent
                    width: 7
                    height: 7
                    radius: 3.5
                    color: Theme.accent
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: r.text
                font: r.font
                color: Theme.t2
            }
        }
    }
    component RowLabel: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
        Layout.preferredWidth: 90
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        FillDialog.Cap { text: qsTr("Viền") }
        RowLayout {
            spacing: 10
            RowLabel { text: qsTr("Độ dày") }
            NumberField {
                Layout.preferredWidth: 96
                value: dlg.strokeWidth
                unit: "px"
                min: 1
                max: 250
                onMoved: v => dlg.strokeWidth = v
            }
        }
        RowLayout {
            spacing: 10
            RowLabel { text: qsTr("Màu") }
            FillDialog.ColorButton {
                Layout.preferredWidth: 60
                color: dlg.strokeColor
                title: qsTr("Màu viền")
                onChosen: c => dlg.strokeColor = c
            }
        }
        FillDialog.Cap { text: qsTr("Vị trí") }
        Row {
            spacing: 18
            Radio {
                text: Spec.LOCATIONS[0][1]
                checked: dlg.location === Spec.LOCATIONS[0][0]
                onClicked: dlg.location = Spec.LOCATIONS[0][0]
            }
            Radio {
                text: Spec.LOCATIONS[1][1]
                checked: dlg.location === Spec.LOCATIONS[1][0]
                onClicked: dlg.location = Spec.LOCATIONS[1][0]
            }
            Radio {
                text: Spec.LOCATIONS[2][1]
                checked: dlg.location === Spec.LOCATIONS[2][0]
                onClicked: dlg.location = Spec.LOCATIONS[2][0]
            }
        }
        FillDialog.Cap { text: qsTr("Hòa trộn") }
        RowLayout {
            spacing: 10
            RowLabel { text: qsTr("Mode") }
            Dropdown {
                Layout.fillWidth: true
                options: Spec.MODES
                value: dlg.mode
                onPicked: v => dlg.mode = v
            }
        }
        RowLayout {
            spacing: 10
            RowLabel { text: qsTr("Opacity") }
            NumberField {
                Layout.preferredWidth: 96
                value: dlg.strokeOpacity
                unit: "%"
                min: 0
                max: 100
                onMoved: v => dlg.strokeOpacity = v
            }
        }
        CheckRow {
            Layout.leftMargin: 100
            text: qsTr("Preserve Transparency")
            checked: dlg.preserve
            onClicked: dlg.preserve = !dlg.preserve
        }
    }
}
