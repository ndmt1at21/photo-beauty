pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import QtQuick.Controls.Basic
import "ToolSpec.js" as ToolSpec
import "Presets.js" as Presets
import "Fmt.js" as Fmt

// The brush of a painting bar (docs/ui-tasks.md, U5; BP of the options bar and bp of Main.dc.html): a cell with the tip,
// the size and a chevron, which opens the brush picker under it: Size 1–5.000 px, Hardness, Spacing 1–1.000 %, the six
// tips of Presets.TIPS (a tip brings its hardness), the first six of Brushes' presets (size and hardness). Values go to Tools
// by "<bar>:size", ":hard", ":spacing", ":tip". The picker closes at a press outside, which stops there and does not
// reach the canvas. Tip draws a tip as the prototype's CSS does.
AbstractButton {
    id: bp
    property string bar: "brush"  // ToolSpec.brushBar()
    readonly property var start: ToolSpec.brush(bar)
    readonly property real size: Tools.value(bar + ":size", start.size)
    readonly property real hard: Tools.value(bar + ":hard", start.hard)
    readonly property real spacingPct: Tools.value(bar + ":spacing", start.spacing)
    readonly property int tip: Tools.value(bar + ":tip", start.tip)
    readonly property var presets: Tools.value("brushes:groups", Presets.BR_GROUPS).reduce((o, g) => o.concat(g[1]), []).slice(0, 6)  // Brushes' (U13)
    readonly property alias picker: pop

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: 24
    leftPadding: 4
    rightPadding: 2
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    ToolTip.visible: hovered && !pop.visible
    ToolTip.delay: 600
    ToolTip.text: qsTr("Bảng preset cọ")
    Accessible.name: qsTr("Bảng preset cọ: %1 px").arg(size)
    onClicked: pop.visible ? pop.close() : pop.open()
    background: Rectangle { radius: 4; color: Theme.p2 }
    contentItem: Row {
        spacing: 8
        Tip {
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            hard: bp.hard
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Fmt.vn(bp.size)
            color: Theme.t1
            font.family: Theme.mono
            font.pointSize: Theme.pt(11.5)
        }
        Icon { anchors.verticalCenter: parent.verticalCenter; name: "expand_more"; size: 16; color: Theme.t3 }
    }

    Popup {
        id: pop
        x: -13  // the prototype's 46 px in the bar, 34 down
        y: 28
        popupType: Popup.Item
        focus: true  // Esc closes it
        modal: true
        dim: false
        width: 300
        padding: 12
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
        contentItem: Column {
            spacing: 10
            SliderField {
                width: parent.width
                label: qsTr("Size")
                min: 1
                max: 5000
                unit: "px"
                value: bp.size
                onMoved: v => Tools.setValue(bp.bar + ":size", v)
            }
            SliderField {
                width: parent.width
                label: qsTr("Hardness")
                unit: "%"
                value: bp.hard
                onMoved: v => Tools.setValue(bp.bar + ":hard", v)
            }
            SliderField {
                width: parent.width
                label: qsTr("Spacing")
                min: 1
                max: 1000
                unit: "%"
                value: bp.spacingPct
                onMoved: v => Tools.setValue(bp.bar + ":spacing", v)
            }
            Grid {
                columns: 6
                spacing: 4
                Repeater {
                    model: Presets.TIPS
                    AbstractButton {
                        id: tb
                        required property var modelData
                        required property int index
                        readonly property bool on: index === bp.tip
                        width: (pop.availableWidth - 20) / 6
                        height: 40
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: modelData[0]
                        Accessible.name: qsTr("Đầu cọ %1").arg(modelData[0])
                        Accessible.checked: on
                        onClicked: Tools.setValues({ [bp.bar + ":tip"]: index, [bp.bar + ":hard"]: modelData[1] })
                        background: Rectangle {
                            radius: 4
                            color: Theme.bg
                            Rectangle {  // the ring of the tip in use, 2 px outside
                                visible: tb.on
                                anchors.fill: parent
                                anchors.margins: -2
                                radius: 6
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.accent
                            }
                        }
                        contentItem: Item {
                            Tip {
                                anchors.centerIn: parent
                                width: 24
                                height: 24
                                hard: tb.modelData[1]
                                square: tb.modelData[2] !== "50%"
                                angle: tb.modelData[3]
                                roundness: tb.modelData[4]
                                dots: tb.index === 5
                            }
                        }
                    }
                }
            }
            Column {
                width: parent.width
                Rectangle { width: parent.width; height: 1; color: Theme.ln }
                Item { width: 1; height: 6 }
                Repeater {
                    model: bp.presets
                    AbstractButton {
                        id: pr
                        required property var modelData
                        readonly property bool on: bp.size === modelData[1] && bp.hard === modelData[2]
                        width: parent.width
                        height: 32
                        leftPadding: 4
                        rightPadding: 4
                        hoverEnabled: true
                        Accessible.name: modelData[0]
                        Accessible.checked: on
                        onClicked: Tools.setValues({ [bp.bar + ":size"]: modelData[1], [bp.bar + ":hard"]: modelData[2] })
                        background: Rectangle { radius: 4; color: pr.on ? Theme.acc2 : pr.hovered ? Theme.sel : "transparent" }
                        contentItem: Row {
                            spacing: 10
                            Item {
                                width: 24
                                height: 24
                                anchors.verticalCenter: parent.verticalCenter
                                Tip {
                                    readonly property int s: Math.max(6, Math.min(22, Math.round(Math.sqrt(pr.modelData[1]) * 1.1)))
                                    anchors.centerIn: parent
                                    width: s
                                    height: s
                                    hard: pr.modelData[2]
                                }
                            }
                            Text {
                                width: pr.availableWidth - 34 - sz.width - 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: pr.modelData[0]
                                color: Theme.t1
                                font.pointSize: Theme.pt(12)
                                elide: Text.ElideRight
                            }
                            Text {
                                id: sz
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("%1 px").arg(Fmt.vn(pr.modelData[1])) + pr.modelData[3]
                                color: Theme.t3
                                font.family: Theme.mono
                                font.pointSize: Theme.pt(10.5)
                            }
                        }
                    }
                }
            }
        }
    }

    // A brush tip as the prototype's CSS draws it: under 34 % hardness soft (t1 to 12 %, clear at 70 % of the half
    // diagonal), under 67 % mid (45 %, 72 %), else solid; a circle, or square (corners 3); angle and roundness turn and
    // flatten it; dots, the grain tip's three dots.
    component Tip: Item {
        id: t
        property real hard
        property bool square
        property real angle
        property real roundness: 1
        property bool dots
        readonly property color clear: Qt.rgba(Theme.t1.r, Theme.t1.g, Theme.t1.b, 0)
        transform: [
            Scale { origin.x: t.width / 2; origin.y: t.height / 2; yScale: t.roundness },
            Rotation { origin.x: t.width / 2; origin.y: t.height / 2; angle: t.angle }
        ]
        Rectangle {
            visible: !t.dots && t.hard >= 67
            anchors.fill: parent
            radius: t.square ? 3 : width / 2
            color: Theme.t1
        }
        Shape {
            visible: !t.dots && t.hard < 67
            anchors.fill: parent
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: t.width / 2
                    centerY: t.height / 2
                    centerRadius: t.width / Math.SQRT2
                    focalX: t.width / 2
                    focalY: t.height / 2
                    GradientStop { position: 0; color: Theme.t1 }
                    GradientStop { position: t.hard >= 34 ? 0.45 : 0.12; color: Theme.t1 }
                    GradientStop { position: t.hard >= 34 ? 0.72 : 0.7; color: t.clear }
                }
                PathAngleArc {
                    centerX: t.width / 2
                    centerY: t.height / 2
                    radiusX: t.width / 2
                    radiusY: t.height / 2
                    sweepAngle: 360
                }
            }
        }
        Repeater {
            model: t.dots ? [[0.3, 0.3], [0.62, 0.55], [0.4, 0.75]] : []
            Rectangle {
                required property var modelData
                x: modelData[0] * t.width - 2.5
                y: modelData[1] * t.height - 2.5
                width: 5
                height: 5
                radius: 2.5
                color: Theme.t1
            }
        }
    }
}
