// copied from snapone 6bdb5c8 (ui/qml/PanelHead.qml)
import QtQuick
import QtQuick.Layouts

// Caps title of a panel section (.cap: 10.5 px, 500, uppercase), with its buttons at the right.
Item {
    property alias text: title.text
    default property alias tools: tools.data
    Layout.fillWidth: true
    implicitHeight: 38
    Text {
        id: title
        x: 12
        y: 8
        height: 30
        verticalAlignment: Text.AlignVCenter
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63  // .06em
        font.capitalization: Font.AllUppercase
    }
    Row { id: tools; anchors.right: parent.right; anchors.rightMargin: 8; y: 11; spacing: 2 }
}
