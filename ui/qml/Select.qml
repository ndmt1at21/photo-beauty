// copied from snapone 6bdb5c8 (ui/qml/Select.qml)
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic

// Drop-down of the prototype's dialogs and Thiết lập (.dd, .pop, .mi): `options` is
// [[value, label]]. `value` comes from the model; picking emits picked() and the model answers
// by changing `value`. The bars' 24 px drop-down is Dropdown.
ComboBox {
    id: c
    property var options: []
    property var value
    readonly property int valueIndex: options.findIndex(o => o[0] === value)
    signal picked(var value)

    model: options
    displayText: valueIndex >= 0 ? options[valueIndex][1] : ""
    // currentIndex is not bound: ComboBox overwrites it on every pick.
    Component.onCompleted: currentIndex = valueIndex
    onValueIndexChanged: currentIndex = valueIndex
    onActivated: i => c.picked(options[i][0])

    implicitHeight: 28
    leftPadding: 10
    rightPadding: 28  // the indicator and 6 px on each side of it
    hoverEnabled: true
    font.pointSize: Theme.pt(12)

    background: Rectangle {
        radius: 6
        color: c.enabled && (c.hovered || c.popup.visible) ? Theme.p3 : Theme.p2
    }
    contentItem: Text {
        text: c.displayText
        font: c.font
        color: c.enabled ? Theme.t1 : Theme.t4
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: c.width - width - 4
        y: (c.height - height) / 2
        name: "expand_more"
        size: 16
        color: c.enabled ? Theme.t3 : Theme.t4
    }
    delegate: ItemDelegate {
        id: opt
        required property var modelData
        required property int index
        width: ListView.view.width
        height: 28
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        highlighted: c.highlightedIndex === index
        text: modelData[1]
        font.pointSize: Theme.pt(12.5)
        font.weight: index === c.valueIndex ? Font.DemiBold : Font.Normal
        background: Rectangle {
            color: opt.highlighted ? Theme.sel : "transparent"
        }
        contentItem: Text {
            text: opt.text
            font: opt.font
            color: Theme.t1
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    popup: Popup {
        popupType: Popup.Item
        y: c.height + 4
        width: c.width
        topPadding: 6
        bottomPadding: 6
        leftPadding: 0
        rightPadding: 0
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }
        contentItem: ListView {
            implicitHeight: contentHeight
            model: c.delegateModel
            currentIndex: c.highlightedIndex
            clip: true
            boundsBehavior: Flickable.StopAtBounds
        }
        background: Rectangle {
            radius: 8
            color: Theme.p2
            border.color: Theme.ln
            RectangularShadow {
                z: -1
                anchors.fill: parent
                offset.y: 12
                blur: 32
                radius: 8
                color: Theme.shadow
            }
        }
    }
}
