// after snapone 6bdb5c8 (ui/qml/Chips.qml), whose segmented chips needed Spec.flex
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Toggle chips (.chip of the prototype, Retouch's Da, Mắt, Môi, Răng): 24 px pills that wrap 6 apart,
// each on or off by itself, in the accent when on. options: [key, label, dot colour (optional)];
// `value` maps key → on and comes from the owner, picked(key) asks it to flip that one.
Flow {
    id: c
    property var options: []
    property var value: ({})
    signal picked(var key)
    spacing: 6

    Repeater {
        model: c.options
        AbstractButton {
            id: chip
            required property var modelData
            readonly property bool on: !!c.value[modelData[0]]
            implicitWidth: implicitContentWidth + leftPadding + rightPadding
            implicitHeight: 24
            leftPadding: 10
            rightPadding: 10
            text: modelData[1]
            font.pointSize: Theme.pt(12)
            Accessible.checkable: true
            Accessible.checked: on
            onClicked: c.picked(modelData[0])
            background: Rectangle { radius: 12; color: chip.on ? Theme.acc2 : Theme.p2 }
            contentItem: Row {
                spacing: 6
                Rectangle {
                    visible: !!chip.modelData[2]
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 2
                    color: chip.modelData[2] || "transparent"
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: chip.text
                    font: chip.font
                    color: chip.on ? Theme.accent : Theme.t2
                }
            }
        }
    }
}
