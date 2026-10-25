pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "Mock.js" as Mock

// The Properties panel's page of a smart object ("so", U35): the file it holds, size, place, Sửa nội dung,
// Rasterize.
PropertiesPage {
    id: page
    objectName: "page:so"
    implicitHeight: oc.implicitHeight + 18
    ColumnLayout {
        id: oc
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        PropertiesPage.Hd {
            icon: "deployed_code"
            text: qsTr("Smart object nhúng")
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Math.round(12 + (12 + 10.5) * 1.35 * Theme.fontScale)  // padding 6 around CSS's line boxes of the two
            radius: 6
            color: Theme.p2
            Row {
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44
                    height: 28
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(88, 56)
                    source: Mock.photoUrl(page.active.props.photo)
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: page.active.props.name + ".jpg"
                        color: Theme.t1
                        font.pointSize: Theme.pt(12)
                    }
                    Text {
                        text: "1.800 × 1.125 px · RGB/8"
                        color: Theme.t3
                        font.family: Theme.mono
                        font.pointSize: Theme.pt(10.5)
                    }
                }
            }
        }
        PropertiesPage.Size { host: page }
        Row {
            spacing: 6
            Btn {
                width: (oc.width - 6) / 2
                implicitHeight: 26
                leftPadding: 10
                rightPadding: 10
                icon.name: "open_in_new"
                text: qsTr("Sửa nội dung")
                onClicked: Docs.open(page.active.props.name + ".psb")
            }
            Btn {
                width: (oc.width - 6) / 2
                implicitHeight: 26
                text: qsTr("Rasterize")
                onClicked: page.run({ op: "rasterize" })
            }
        }
    }
}
