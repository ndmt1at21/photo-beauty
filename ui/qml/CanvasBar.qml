import QtQuick
import QtQuick.Controls.Basic
import "Icons.js" as Icons

// The notice bar and the slim progress over the canvas (docs/ui-tasks.md, U7; artboard Overlays, plan.md: Thanh báo,
// Tiến độ trên canvas): neither blocks the canvas. The owner lays it over the frame. Hud, the label the other overlays
// write over the photo (the prototype's .hud), is here too: CanvasBar.Hud. Glyphs are drawn as text, not Icon, and the
// buttons are plain: no Icon in the canvas's tree (Viewport's test finds its own pointer Icon first).
Item {
    id: cb
    property string notice  // "" for none
    property bool details: false  // its Xem chi tiết button
    property string task  // what runs, as "Gaussian Blur · cả ảnh"
    property real progress: -1  // 0 to 1; below 0 none
    signal detailsClicked()
    signal dismissed()
    signal canceled()

    // A label over the photo: white monospace on black 55 %.
    component Hud: Label {
        color: "#ffffff"
        font.family: Theme.mono
        font.pointSize: Theme.pt(11)
        font.weight: Font.Medium
        leftPadding: 7
        rightPadding: 7
        topPadding: 3
        bottomPadding: 3
        background: Rectangle { radius: 4; color: Theme.onPhoto }
    }
    // A text button of the bar: fill when on (Xem chi tiết), else only under the pointer (Bỏ qua).
    component BarButton: AbstractButton {
        id: bb
        property bool filled: false
        implicitWidth: label.implicitWidth + leftPadding + rightPadding
        implicitHeight: 24
        leftPadding: filled ? 8 : 6
        rightPadding: leftPadding
        hoverEnabled: true
        font.pointSize: Theme.pt(11.5)
        background: Rectangle { radius: 4; color: bb.filled ? (bb.hovered || bb.down ? Theme.trk : Theme.p3) : bb.hovered || bb.down ? Theme.sel : "transparent" }
        contentItem: Text { id: label; text: bb.text; font: bb.font; color: bb.filled ? Theme.t1 : Theme.t2; verticalAlignment: Text.AlignVCenter }
    }

    Rectangle {  // the progress along the top edge
        visible: cb.progress >= 0
        width: Math.round(cb.width * Math.min(1, cb.progress))
        height: 2
        color: "#ffffff"
        Rectangle { y: 2; width: parent.width; height: 1; color: Qt.rgba(0, 0, 0, 0.5) }
    }
    Rectangle {
        id: bar
        visible: cb.notice !== ""
        x: 10
        y: 10
        width: cb.width - 20
        height: row.implicitHeight + 16
        radius: 8
        color: Theme.p2
        border.color: Theme.ln
        Row {
            id: row
            x: 10
            y: 8
            width: parent.width - 20
            spacing: 8
            Text {
                id: warn
                anchors.verticalCenter: parent.verticalCenter
                text: Icons.cp.warning
                font.family: Theme.icons
                font.pixelSize: 18
                color: Theme.warn
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: row.width - warn.width - (more.visible ? more.width + row.spacing : 0) - skip.width - 2 * row.spacing
                text: cb.notice
                color: Theme.t1
                font.pointSize: Theme.pt(11.5)
                wrapMode: Text.Wrap
            }
            BarButton {
                id: more
                visible: cb.details
                anchors.verticalCenter: parent.verticalCenter
                filled: true
                text: qsTr("Xem chi tiết")
                onClicked: cb.detailsClicked()
            }
            BarButton {
                id: skip
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Bỏ qua")
                onClicked: cb.dismissed()
            }
        }
    }
    Rectangle {  // what runs, its share done, a cancel button
        visible: cb.progress >= 0 && cb.task !== ""
        anchors.right: parent.right
        anchors.rightMargin: 10
        y: bar.visible ? bar.y + bar.height + 8 : 10
        width: job.implicitWidth + 16
        height: 28
        radius: 6
        color: Theme.onPhoto
        Row {
            id: job
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: cb.task
                color: "#ffffff"
                font.pointSize: Theme.pt(11)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(cb.progress * 100) + "%"
                color: "#ffffff"
                font.family: Theme.mono
                font.pointSize: Theme.pt(11)
            }
            AbstractButton {
                id: cancel
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 18
                implicitHeight: 18
                hoverEnabled: true
                Accessible.name: qsTr("Hủy")
                background: Rectangle { radius: 4; color: cancel.hovered || cancel.down ? Qt.rgba(1, 1, 1, 0.15) : "transparent" }
                contentItem: Text {
                    text: Icons.cp.close
                    font.family: Theme.icons
                    font.pixelSize: 14
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: cb.canceled()
            }
        }
    }
}
