pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Controls.Basic

// The background job on the top bar (docs/ui-tasks.md, U4; Docs.job, artboard Main): while it runs, the spinner, what
// it does, its progress and Hủy; done, a tick and "Đã lưu …" until Docs hides it. A click shows the details.
Rectangle {
    id: ji
    readonly property var job: Docs.job
    readonly property bool popupOpen: details.visible
    visible: !!job.on
    onVisibleChanged: if (!visible) details.close()
    implicitWidth: row.implicitWidth
    implicitHeight: 28
    radius: 6
    color: Theme.p2

    Row {
        id: row
        height: parent.height
        AbstractButton {
            id: show
            height: parent.height
            leftPadding: 10
            rightPadding: 8
            hoverEnabled: true
            ToolTip.visible: hovered && !details.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("Việc chạy nền: bấm để xem chi tiết")
            onClicked: details.visible ? details.close() : details.open()
            contentItem: Row {
                spacing: 8
                Spinner { visible: !!ji.job.run; anchors.verticalCenter: parent.verticalCenter }
                Icon { visible: !!ji.job.done; name: "check"; size: 16; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: ji.job.label || ""
                    color: Theme.t2
                    font.pointSize: Theme.pt(12)
                }
                Bar { visible: !!ji.job.run; width: 56; p: ji.job.p || 0; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    visible: !!ji.job.run
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(ji.job.p || 0) + "%"
                    color: Theme.t3
                    font.family: Theme.mono
                    font.pointSize: Theme.pt(11)
                }
            }
        }
        IconButton {
            visible: !!ji.job.run
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 24
            implicitHeight: 24
            icon.name: "close"
            icon.width: 14
            ToolTip.text: qsTr("Hủy")
            onClicked: Docs.cancelJob()
        }
        Item { visible: !!ji.job.run; width: 2; height: 1 }
    }

    Popup {
        id: details
        popupType: Popup.Item
        x: ji.width - width
        y: 34
        width: 300
        padding: 12
        focus: true  // Esc closes it
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent  // the job's button toggles it
        background: Rectangle {
            radius: 8
            color: Theme.p2
            border.color: Theme.ln
            RectangularShadow { z: -1; anchors.fill: parent; offset.y: 12; blur: 32; radius: 8; color: Theme.shadow }
        }
        contentItem: Column {
            spacing: 10
            Text {
                text: qsTr("Việc chạy nền")
                color: Theme.t3
                font.pointSize: Theme.pt(10.5)
                font.letterSpacing: 0.63  // .06em
                font.capitalization: Font.AllUppercase
            }
            RowLayout {
                width: parent.width
                spacing: 10
                Column {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        width: parent.width
                        text: ji.job.label || ""
                        color: Theme.t1
                        font.pointSize: Theme.pt(12)
                        elide: Text.ElideRight
                    }
                    Bar { width: parent.width; p: ji.job.p || 0 }
                }
                Btn {
                    visible: !!ji.job.run
                    implicitHeight: 24
                    leftPadding: 10
                    rightPadding: 10
                    text: qsTr("Hủy")
                    onClicked: Docs.cancelJob()
                }
            }
            Text {
                width: parent.width
                text: qsTr("Canvas vẫn dùng được trong lúc chạy.")
                color: Theme.t3
                font.pointSize: Theme.pt(11)
                wrapMode: Text.Wrap
            }
        }
    }

    // The job's progress: 3 px of accent over the track.
    component Bar: Rectangle {
        id: track
        property real p  // 0..100
        height: 3
        radius: 2
        color: Theme.trk
        Rectangle { width: track.width * Math.min(100, track.p) / 100; height: 3; radius: 2; color: Theme.accent }
    }
}
