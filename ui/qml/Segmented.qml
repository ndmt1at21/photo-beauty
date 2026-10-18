pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import QtQuick.Controls.Basic

// Segmented control of the prototype (seg of the options bar, SEG of the adjustments, .seg of
// Thiết lập): one of `options` is on, each a string or { t, icon, grad, tip }: t text, icon a Material
// Symbols name, grad a swatch of a gradient type ("linear", "radial", "angle", "reflected",
// "diamond": GTYPES), tip the tooltip. `current` is the index on, picked(index) asks the owner to
// change it; fill: the segments share the width (adjustments) instead of fitting their content.
Rectangle {
    id: g
    property var options: []
    property int current: 0
    property bool fill: false
    signal picked(int index)

    implicitWidth: {  // the segments' own widths, also when they share the control's
        let w = 2 + Math.max(0, segs.count - 1)
        for (let i = 0; i < segs.count; i++) w += segs.itemAt(i)?.implicitWidth ?? 0
        return w
    }
    implicitHeight: 24
    radius: 5
    color: Theme.p2

    Row {
        id: row
        x: 1
        y: 1
        spacing: 1
        Repeater {
            id: segs
            model: g.options
            AbstractButton {
                id: b
                required property var modelData
                required property int index
                readonly property var o: typeof modelData === "string" ? ({ t: modelData }) : modelData
                readonly property bool on: index === g.current
                width: g.fill ? (g.width - 2 - (g.options.length - 1)) / g.options.length : implicitWidth
                implicitWidth: Math.max(26, implicitContentWidth + leftPadding + rightPadding)
                height: g.height - 2
                leftPadding: o.icon || o.grad ? 2 : 10
                rightPadding: leftPadding
                hoverEnabled: true
                font.pointSize: Theme.pt(12)
                text: o.t || ""
                ToolTip.visible: hovered && ToolTip.text !== ""
                ToolTip.delay: 600
                ToolTip.text: o.tip || (o.icon || o.grad ? o.t || "" : "")
                Accessible.role: Accessible.RadioButton
                Accessible.checked: on
                onClicked: g.picked(index)
                background: Rectangle { radius: 4; color: b.on ? Theme.p3 : "transparent" }
                contentItem: Item {
                    implicitWidth: b.o.icon || b.o.grad ? 16 : label.implicitWidth
                    implicitHeight: 16
                    Text {
                        id: label
                        visible: !b.o.icon && !b.o.grad
                        anchors.centerIn: parent
                        text: b.text
                        font: b.font
                        color: !b.enabled ? Theme.t4 : b.on || b.hovered ? Theme.t1 : Theme.t3
                    }
                    Icon {
                        visible: !!b.o.icon
                        anchors.centerIn: parent
                        name: b.o.icon || ""
                        size: 16
                        color: !b.enabled ? Theme.t4 : b.on || b.hovered ? Theme.t1 : Theme.t3
                    }
                    Shape {  // white to black as the gradient type lays it out
                        visible: !!b.o.grad
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        ShapePath {
                            strokeColor: "transparent"
                            fillGradient: b.o.grad === "radial" ? radial : b.o.grad === "angle" ? angle
                                        : b.o.grad === "diamond" ? diamond : b.o.grad === "reflected" ? reflected : linear
                            PathRectangle { width: 16; height: 16; radius: 2 }
                        }
                        LinearGradient {
                            id: linear
                            x2: 16
                            GradientStop { position: 0; color: "#ffffff" }
                            GradientStop { position: 1; color: "#000000" }
                        }
                        LinearGradient {
                            id: reflected
                            x2: 16
                            GradientStop { position: 0; color: "#000000" }
                            GradientStop { position: 0.5; color: "#ffffff" }
                            GradientStop { position: 1; color: "#000000" }
                        }
                        RadialGradient {
                            id: radial
                            centerX: 8; centerY: 8; centerRadius: 11.3
                            focalX: 8; focalY: 8
                            GradientStop { position: 0; color: "#ffffff" }
                            GradientStop { position: 1; color: "#000000" }
                        }
                        ConicalGradient {  // Qt turns counter-clockwise, CSS's conic-gradient clockwise: the stops reversed
                            id: angle
                            centerX: 8; centerY: 8
                            angle: 90
                            GradientStop { position: 0; color: "#000000" }
                            GradientStop { position: 1; color: "#ffffff" }
                        }
                        ConicalGradient {
                            id: diamond
                            centerX: 8; centerY: 8
                            angle: 45
                            GradientStop { position: 0; color: "#ffffff" }
                            GradientStop { position: 0.25; color: "#000000" }
                            GradientStop { position: 0.5; color: "#ffffff" }
                            GradientStop { position: 0.75; color: "#000000" }
                            GradientStop { position: 1; color: "#ffffff" }
                        }
                    }
                }
            }
        }
    }
}
