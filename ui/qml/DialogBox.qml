// copied from snapone 6bdb5c8 (ui/qml/DialogBox.qml)
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic

// Dialog frame of the prototype (.dlg, .dh, .df): a ruled 46 px title bar, scrolling content,
// buttons at the bottom right. buttons is [{ text, primary, disabled, act }]; children go into the
// content column, or a dialog sets its own contentItem. Esc or a click on the dimmed backdrop
// closes it; `closable` adds the ×, `icon` an icon in iconColor before the title (Notices), `note`
// a paragraph above the content; `compact` drops the rules (small boxes such as a rename).
Dialog {
    id: d
    property string note
    property var buttons: []
    property real boxWidth: 440
    property bool closable: false
    property bool compact: false
    property string icon
    property color iconColor: Theme.t2
    default property alias content: body.data

    popupType: Popup.Item
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(boxWidth, Overlay.overlay.width - 40)
    height: Math.min(implicitHeight, Overlay.overlay.height - 60)
    padding: 0
    Overlay.modal: Rectangle { color: Theme.scrim }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }

    background: Rectangle {
        radius: 10
        color: Theme.p1
        border.color: Theme.ln
        RectangularShadow {
            z: -1
            anchors.fill: parent
            offset.y: 24
            blur: 60
            radius: 10
            color: Theme.shadow
        }
    }
    header: Item {
        implicitHeight: 46
        Row {
            x: d.icon !== "" ? 16 : 18
            height: parent.height
            spacing: 10
            Icon {
                visible: d.icon !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: d.icon
                size: 18
                color: d.iconColor
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: d.width - parent.x - (d.icon !== "" ? 28 : 0) - (d.closable ? 46 : 18)
                text: d.title
                color: Theme.t1
                font.pointSize: Theme.pt(14)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
        IconButton {
            visible: d.closable
            x: d.width - 8 - width
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 30
            implicitHeight: 30
            icon.name: "close"
            icon.width: 18
            ToolTip.text: qsTr("Đóng")
            onClicked: d.close()
        }
        Rectangle { visible: !d.compact; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
    }
    contentItem: Flickable {
        implicitHeight: col.implicitHeight + 32
        contentHeight: col.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton  // wheel and scroll bar; drags belong to the content
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: col
            x: 18
            y: 16
            width: d.width - 36
            spacing: 10
            Text {
                visible: d.note !== ""
                width: parent.width
                text: d.note
                color: Theme.t2
                font.pointSize: Theme.pt(12)
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }
            Column {
                id: body
                width: parent.width
            }
        }
    }
    footer: Item {
        visible: d.buttons.length > 0
        implicitHeight: visible ? 52 : 0
        Rectangle { visible: !d.compact; width: parent.width; height: 1; color: Theme.ln }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Repeater {
                model: d.buttons
                Btn {
                    required property var modelData
                    width: primary ? Math.max(84, implicitWidth) : implicitWidth
                    text: modelData.text
                    primary: !!modelData.primary
                    enabled: !modelData.disabled
                    onClicked: modelData.act()
                }
            }
        }
    }
}
