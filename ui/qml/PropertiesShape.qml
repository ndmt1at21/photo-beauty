pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec
import "Mock.js" as Mock

// The Properties panel's page of a shape layer ("shape", U35): size, place, Fill, Stroke, corners, path operation.
PropertiesPage {
    id: page
    objectName: "page:shape"
    implicitHeight: hc.implicitHeight + 18
    readonly property var tool: Mock.SHAPE_TOOLS[page.active.props.shape] || Mock.SHAPE_TOOLS.rectS  // [name, icon]
    component TextFld: Rectangle {  // a field the prototype keeps as text ("3 px", "24 px"), an icon before it
        id: tf
        property string icon
        property real turn  // the icon's rotation
        property string value
        signal edited(string text)
        implicitWidth: 64
        implicitHeight: 24
        radius: 4
        color: Theme.p2
        onValueChanged: if (!input.activeFocus) input.text = value
        Icon {
            visible: tf.icon !== ""
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            name: tf.icon
            size: 15
            color: Theme.t3
            rotation: tf.turn
        }
        TextInput {
            id: input
            x: tf.icon !== "" ? 27 : 6
            width: parent.width - x - 6
            anchors.verticalCenter: parent.verticalCenter
            clip: true
            color: Theme.t1
            selectByMouse: true
            font.family: Theme.mono
            font.pointSize: Theme.pt(11.5)
            Component.onCompleted: text = tf.value
            onEditingFinished: tf.edited(text)
            Keys.onEscapePressed: {
                text = tf.value
                focus = false
            }
        }
    }
    ColumnLayout {
        id: hc
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        PropertiesPage.Hd {
            icon: page.tool[1]
            text: ({ ellipS: qsTr("Hình elip"), tri: qsTr("Hình tam giác"), poly: qsTr("Hình đa giác"), line: qsTr("Đường thẳng") })[page.active.props.shape]
                  || qsTr("Hình chữ nhật")
        }
        PropertiesPage.Size { host: page }
        PropertiesPage.Cap {
            Layout.topMargin: 2
            text: qsTr("Diện mạo")
        }
        RowLayout {
            spacing: 8
            PropertiesPage.Dim { text: qsTr("Fill") }
            Swatch {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 22
                radius: 4
                ring: Theme.t4
                color: page.active.props.color || "#c0587e"
                ToolTip.text: qsTr("Fill · Set shape fill type")
                Accessible.name: qsTr("Màu Fill")
                onClicked: page.pickColor(color, qsTr("màu Fill"), c => page.run({ op: "setProps", props: { color: String(c) } }))
            }
            PropertiesPage.Dim {
                Layout.leftMargin: 6
                text: qsTr("Stroke")
            }
            Swatch {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 22
                radius: 4
                ring: Theme.t4
                none: !page.lf("stroke", "")
                color: page.lf("stroke", "") || "transparent"
                ToolTip.text: qsTr("Stroke · Set shape stroke type")
                Accessible.name: qsTr("Màu Stroke")
                onClicked: page.pickColor(page.lf("stroke", "#ffffff"), qsTr("màu Stroke"), c => page.setLf("stroke", String(c)))
            }
            TextFld {
                Layout.preferredWidth: 58
                value: Tools.value("f:sh_sw", "3 px")
                Accessible.name: qsTr("Độ dày viền")
                onEdited: t => Tools.setValue("f:sh_sw", t)
            }
            Item { Layout.fillWidth: true }
        }
        PropertiesPage.Cap {
            Layout.topMargin: 2
            text: qsTr("Bo góc")
        }
        Grid {  // top left, (link), top right; bottom left, bottom right
            columns: 3
            spacing: 6
            Repeater {
                model: [["r1", -90, qsTr("Bo góc trên trái")], null, ["r2", 0, qsTr("Bo góc trên phải")], ["r3", 180, qsTr("Bo góc dưới trái")], null,
                    ["r4", 90, qsTr("Bo góc dưới phải")]]
                Item {
                    id: corner
                    required property var modelData
                    required property int index
                    width: corner.modelData ? page.half : 24
                    height: 24
                    TextFld {
                        visible: !!corner.modelData
                        anchors.fill: parent
                        icon: "rounded_corner"
                        turn: corner.modelData ? corner.modelData[1] : 0
                        value: corner.modelData ? Tools.value("f:sh_" + corner.modelData[0], "24 px") : ""
                        Accessible.name: corner.modelData ? corner.modelData[2] : ""
                        onEdited: t => Tools.setValue("f:sh_" + corner.modelData[0], t)
                    }
                    ToolToggle {
                        visible: corner.index === 1
                        width: 24
                        icon.name: "link"
                        icon.width: 16
                        checked: Tools.value("sh:rlink", true)
                        ToolTip.text: qsTr("Bo đều bốn góc · Link together corner radius values")
                        Accessible.name: qsTr("Bo đều bốn góc")
                        onClicked: Tools.setValue("sh:rlink", !checked)
                    }
                }
            }
        }
        PropertiesPage.Cap {
            Layout.topMargin: 2
            text: qsTr("Thao tác path")
        }
        Row {
            spacing: 2
            Repeater {
                model: Spec.PATH_OPS
                ToolToggle {
                    id: po
                    required property var modelData
                    required property int index
                    icon.name: po.modelData[0]
                    icon.width: 16
                    checked: Tools.value("sh:op", 0) === po.index
                    ToolTip.text: po.modelData[1]
                    Accessible.name: po.modelData[1].split(" · ")[0]
                    onClicked: Tools.setValue("sh:op", po.index)
                }
            }
        }
    }
}
