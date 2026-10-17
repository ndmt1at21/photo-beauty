// copied from snapone 6bdb5c8 (ui/qml/PanelSection.qml)
import QtQuick

// Always-open group of a panel: title (13 px, 600), its rows, a rule below. The collapsible one
// is Section.
Column {
    id: g
    property string title
    default property alias rows: body.data
    Item {
        width: g.width
        height: 34
        Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: g.title; color: Theme.t1; font.pointSize: Theme.pt(13); font.weight: Font.DemiBold }
    }
    Column { id: body; x: 12; width: g.width - 24; bottomPadding: 12 }
    Rectangle { width: g.width; height: 1; color: Theme.ln }
}
