import QtQuick
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt
import "View.js" as View

// The Navigator panel (docs/ui-tasks.md, U12; PanelsB, the nv part of Main.dc.html), opened beside the icon column: the
// whole document small (MockCanvas, as the canvas draws it) with the part of it in the canvas's frame outlined, turned
// as the view is; dragging the outline pans, a press off it centres the view there first. Under it the zoom to type,
// − and + by the steps of View, a slider of the zoom on a log scale (1 % to 3.200 %, as the prototype's).
Item {
    id: panel
    readonly property Doc doc: Docs.current
    // The canvas's frame, device px (Viewport keeps Docs.viewport), and the view shown in it: Doc.view as the Viewport
    // constrains it (View.js).
    readonly property var size: ({ w: Docs.viewport.w, h: Docs.viewport.h, dpr: 1 })
    readonly property var view: doc ? View.constrain(doc.view, size, doc.w, doc.h) : null
    // The thumbnail, as the prototype's tw × th: 220 wide when the document is wide, else 200 high; k its px per
    // document px.
    readonly property bool wide: !!doc && doc.w / doc.h >= 1.1
    readonly property int tw: !doc ? 0 : wide ? 220 : Math.round(200 * doc.w / doc.h)
    readonly property int th: !doc ? 0 : wide ? Math.round(220 * doc.h / doc.w) : 200
    readonly property real k: doc ? tw / doc.w : 1
    readonly property real logMax: Math.log(3200)
    readonly property string zoomText: doc ? Fmt.zt(doc.view.zoom * 100) : ""
    property var menu: []
    implicitWidth: 280
    implicitHeight: 252

    // The view centred on document point p, constrained as the Viewport shows it.
    function centre(p) { doc.setView(View.constrain(Object.assign({}, doc.view, { x: p.x, y: p.y }), size, doc.w, doc.h)) }

    Rectangle {
        width: parent.width
        height: 212
        color: Theme.view
        Item {
            id: thumb
            objectName: "thumb"
            anchors.centerIn: parent
            width: panel.tw
            height: panel.th
            clip: true  // overflow: hidden, the outline of a view showing all of it too
            MockCanvas {
                anchors.fill: parent
                doc: panel.doc
                view: panel.doc ? { x: panel.doc.w / 2, y: panel.doc.h / 2, zoom: panel.k, angle: 0 } : null
                size: ({ w: panel.tw, h: panel.th, dpr: 1 })
            }
            Item {  // the frame: the prototype's two rings, 1.5 px of white then 1 of black at 60 %, outside it
                id: frame
                objectName: "frame"
                readonly property real fw: panel.view ? panel.size.w / panel.view.zoom * panel.k : 0
                readonly property real fh: panel.view ? panel.size.h / panel.view.zoom * panel.k : 0
                visible: !!panel.view
                x: panel.view ? panel.view.x * panel.k - fw / 2 : 0
                y: panel.view ? panel.view.y * panel.k - fh / 2 : 0
                width: fw
                height: fh
                rotation: panel.view ? -panel.view.angle : 0
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2.5
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(0, 0, 0, 0.6)
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1.5
                    color: "transparent"
                    border.width: 1.5
                    border.color: "white"
                }
            }
            MouseArea {
                id: drag
                property var start: null  // { x, y, view }
                anchors.fill: parent
                enabled: !!panel.doc
                preventStealing: true
                onPressed: e => {
                    const p = { x: e.x / panel.k, y: e.y / panel.k }, q = View.toCanvas(panel.view, panel.size, p)
                    if (q.x < 0 || q.y < 0 || q.x > panel.size.w || q.y > panel.size.h)
                        panel.centre(p)
                    start = { x: e.x, y: e.y, view: panel.view }
                }
                onPositionChanged: e => {
                    if (start)
                        panel.centre({ x: start.view.x + (e.x - start.x) / panel.k, y: start.view.y + (e.y - start.y) / panel.k })
                }
                onReleased: start = null
                onCanceled: start = null
            }
        }
    }
    Row {
        x: 12
        y: 220
        width: parent.width - 24
        height: 24
        spacing: 6
        TextField {
            id: zoom
            width: 66
            height: 24
            leftPadding: 6
            rightPadding: 6
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            text: panel.zoomText
            color: Theme.t1
            selectionColor: Theme.accent
            selectedTextColor: Theme.acct
            selectByMouse: true
            font.family: Theme.mono
            font.pointSize: Theme.pt(11.5)
            Accessible.name: qsTr("Zoom, gõ được")
            background: Rectangle { radius: 4; color: Theme.p2; border.color: zoom.activeFocus ? Theme.t3 : "transparent" }
            onEditingFinished: apply()  // Enter, or leaving the box
            Keys.onEscapePressed: e => {
                if (text === panel.zoomText)
                    e.accepted = false  // nothing typed: Esc closes the panel
                revert()
            }
            function apply() {
                const x = Fmt.pnum(text)
                if (x && x > 0 && panel.doc)
                    panel.doc.zoomTo(x)
                revert()
            }
            function revert() { text = Qt.binding(() => panel.zoomText) }
        }
        ToolToggle {
            icon.name: "zoom_out"
            icon.width: 16
            ToolTip.text: qsTr("Thu nhỏ · Zoom Out")
            Accessible.name: qsTr("Thu nhỏ")
            onClicked: Docs.exec("@zoomOut")
        }
        SliderField {
            id: slider
            width: parent.width - 66 - 2 * 26 - 3 * 6
            anchors.verticalCenter: parent.verticalCenter
            min: 0
            max: 1000
            value: panel.doc ? Math.max(0, Math.min(1, Math.log(Math.max(1, panel.doc.view.zoom * 100)) / panel.logMax)) * 1000 : 0
            Accessible.name: qsTr("Zoom")
            onMoved: v => panel.doc.zoomTo(Math.exp(v / 1000 * panel.logMax))
        }
        ToolToggle {
            icon.name: "zoom_in"
            icon.width: 16
            ToolTip.text: qsTr("Phóng to · Zoom In")
            Accessible.name: qsTr("Phóng to")
            onClicked: Docs.exec("@zoomIn")
        }
    }
}
