pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import "Mock.js" as Mock
import "Presets.js" as Presets
import "Fmt.js" as Fmt

// Edit › Define Brush Preset… (docs/ui-tasks.md, U25; build/design2/project/EditDialogs.dc.html, Brush Name): the tip
// the selection makes, a soft dot as the artboard draws it with its size under it (the larger side of the selection's
// bounds, the whole image's without one; at most 5.000 px, Photoshop's largest tip), and its name, "Cọ mẫu N" with the
// next N. OK adds the preset to the group "Của tôi" of the Brushes panel (Tools' "brushes:groups"; the group made at
// the end when there is none, and opened) and one History step "Define Brush Preset"; Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property var groups: Tools.value("brushes:groups", Presets.BR_GROUPS)
    readonly property string mine: qsTr("Của tôi")
    readonly property int size: {
        if (!doc)
            return 0
        let pts = [[0, 0], [1, 1]]  // in units of the image (Mock.SHAPES): rectangles, ellipses, polygons
        if (doc.selection && !doc.inverse)
            pts = Mock.SHAPES[doc.selection].reduce((a, p) => a.concat(p[0] === "r" ? [[p[1], p[2]], [p[1] + p[3], p[2] + p[4]]]
                : p[0] === "e" ? [[p[1] - p[3], p[2] - p[4]], [p[1] + p[3], p[2] + p[4]]] : p[1]), [])
        const xs = pts.map(q => q[0]), ys = pts.map(q => q[1])
        return Math.min(5000, Math.round(Math.max((Math.max(...xs) - Math.min(...xs)) * doc.w, (Math.max(...ys) - Math.min(...ys)) * doc.h)))
    }

    title: qsTr("Brush Name")
    boxWidth: 440
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, disabled: !nameBox.text.trim(), act: () => dlg.apply() }]
    Component.onCompleted: nameBox.text = nextName()  // once: OK's preset must not rename it while it fades out

    // "Cọ mẫu N", N one past the largest a preset has.
    function nextName() {
        const re = new RegExp("^" + qsTr("Cọ mẫu %1").replace("%1", "(\\d+)") + "$")
        let n = 0
        for (const g of groups)
            for (const b of g[1])
                n = Math.max(n, +((re.exec(b[0]) || [0, 0])[1]))
        return qsTr("Cọ mẫu %1").arg(n + 1)
    }
    function apply() {
        const name = nameBox.text.trim(), gs = groups.map(g => [g[0], g[1].slice()]), o = Tools.value("brushes:open", [true, true, false]).slice()
        let g = gs.findIndex(x => x[0] === mine)
        if (g < 0)
            g = gs.push([mine, []]) - 1
        gs[g][1].push([name, size, 0, ""])
        o[g] = true
        Tools.setValues({ "brushes:groups": gs, "brushes:open": o })
        if (doc)
            doc.run({ op: "step", name: qsTr("Define Brush Preset"), icon: "brush", brush: name, size: size })
        close()
    }

    component Note: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(11)
        lineHeightMode: Text.FixedHeight
        lineHeight: Math.round(11 * 1.35 * Theme.fontScale)
    }

    Item {  // the artboard's .body is 14 px under the title, DialogBox's 16
        width: parent.width
        implicitHeight: lay.implicitHeight - 2
        RowLayout {
            id: lay
            y: -2
            width: parent.width
            spacing: 16
            Column {  // the tip and its size
                Layout.alignment: Qt.AlignTop
                spacing: 6
                Rectangle {
                    width: 84
                    height: 84
                    radius: 6
                    color: Theme.p2
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Đầu cọ từ vùng chọn, %1").arg(Fmt.unit(dlg.size, "px"))
                    Rectangle {  // its 1 px ring, outside as the artboard's box-shadow
                        z: -1
                        anchors.fill: parent
                        anchors.margins: -1
                        radius: 7
                        color: Theme.ln
                    }
                    Shape {  // radial-gradient(circle, dab 0 30%, transparent 70%) on a round 62 px: the corner is 100 %
                        id: dot
                        x: 11
                        y: 11
                        width: 62
                        height: 62
                        ShapePath {
                            strokeColor: "transparent"
                            fillGradient: RadialGradient {
                                centerX: 31
                                centerY: 31
                                centerRadius: 31 * Math.SQRT2
                                focalX: 31
                                focalY: 31
                                GradientStop { position: 0.3; color: Theme.t1 }
                                GradientStop { position: 0.7; color: Qt.alpha(Theme.t1, 0) }
                            }
                            PathAngleArc {
                                centerX: 31
                                centerY: 31
                                radiusX: 31
                                radiusY: 31
                                sweepAngle: 360
                            }
                        }
                    }
                }
                Note {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Fmt.unit(dlg.size, "px")
                    font.family: Theme.mono
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 8
                Text {
                    text: qsTr("Tên")
                    color: Theme.t2
                    font.pointSize: Theme.pt(12)
                }
                TextBox {
                    id: nameBox
                    Layout.fillWidth: true
                    implicitHeight: 26
                    Accessible.name: qsTr("Tên")
                }
                Note {
                    Layout.fillWidth: true
                    text: qsTr("Lưu vào nhóm Của tôi trong panel Brushes.")
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
