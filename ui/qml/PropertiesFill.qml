pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec

// The Properties panel's page of a fill layer ("fill", U35): its colour, a gradient fill's gradient.
PropertiesPage {
    id: page
    objectName: "page:fill"
    implicitHeight: fc.implicitHeight + 18
    readonly property string fill: page.active.props.color || "#808080"
    readonly property var stops: Spec.colors(fill)
    ColumnLayout {
        id: fc
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        PropertiesPage.Hd {
            icon: "format_color_fill"
            text: page.active.props.name
        }
        RowLayout {
            spacing: 8
            PropertiesPage.Dim { text: qsTr("Màu") }
            Swatch {
                id: fs
                readonly property bool solid: page.fill.charAt(0) === "#"
                Layout.preferredWidth: 60
                Layout.preferredHeight: 22
                radius: 4
                ring: Theme.t4
                color: fs.solid ? page.fill : "transparent"
                ToolTip.text: qsTr("Màu của fill layer · Mở Color Picker")
                Accessible.name: qsTr("Màu của fill layer")
                onClicked: {
                    if (fs.solid)
                        page.pickColor(fs.color, qsTr("màu của fill layer"), c => page.run({ op: "setProps", props: { color: String(c) } }))
                    else
                        Tools.showPanel("Gradients")
                }
                Row {  // a gradient fill's: a two-colour piece between each pair of its stops
                    visible: !fs.solid
                    x: 1
                    y: 1
                    Repeater {
                        model: Math.max(0, page.stops.length - 1)
                        Rectangle {
                            id: fp
                            required property int index
                            width: 58 / (page.stops.length - 1)
                            height: 20
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: page.stops[fp.index] }
                                GradientStop { position: 1; color: page.stops[fp.index + 1] }
                            }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
