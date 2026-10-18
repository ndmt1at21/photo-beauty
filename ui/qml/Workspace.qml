pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// The frame of a full-window space (docs/ui-tasks.md, U15; artboards SelectMask, Liquify), which Main.openSpace() lays
// over the window in the work area's place: a header "<title> · <file> · layer <name>", the space's options bar (none
// without `options`), its tools on the left (Tools.tool: Hand and Zoom are the canvas's own, Viewport pans and zooms by
// them), the document on a Viewport of its own (no rulers, no right-click menu, none of the window's overlays: the space
// draws its own in `overlay`), its Properties on the right (the default children), a check box, Hủy and OK at the foot.
// OK and Enter emit accepted(), Hủy and Esc rejected(); the space answers with finished(ok), and then the document's
// view and Docs.viewport are as the space found them: Main gives back the document and the tool.
Rectangle {
    id: ws
    property var params: ({})
    property string title
    property var tools: []  // the column's buttons: [id, icon, tip], null a rule between groups
    property string checkText  // the box at the foot: Remember Settings, Preview
    property bool checked
    default property alias content: column.data  // Properties' sections, top down
    property alias options: optionsRow.data  // the options bar's items
    property alias overlay: over.data  // over the canvas's frame; view and size say where the document is in it
    property string startTool  // the tool it opens on
    property Doc doc: null  // Docs.current as the space opened: it works on that document
    readonly property Viewport viewport: canvas.item as Viewport  // the space's canvas
    readonly property var view: viewport ? viewport.view : null  // Viewport's: { x, y, zoom, angle }
    readonly property var size: viewport ? viewport.size : null  // { w, h, dpr } of the frame
    property var found: null  // the document's view, Docs.viewport and the tool when the space opened
    property Item pointerArea: null  // Viewport's MouseArea: a BrushCursor's handlers go on it, the canvas keeps its hover
    readonly property var life: ({ open: true })  // false once finished: what a call put off reads, the space gone or not
    signal finished(bool ok)
    signal accepted()
    signal rejected()
    signal checkToggled()

    color: Theme.bg
    // Before the Viewport is made, as it writes the view and Docs.viewport; the tool once Main.openSpace() has noted
    // the one to give back.
    Component.onCompleted: {
        doc = Docs.current
        found = doc ? { view: doc.view, viewport: Docs.viewport, tool: Tools.tool } : null
        const life = ws.life, tool = startTool
        Qt.callLater(() => { if (life.open) Tools.tool = tool })
    }
    onFinished: {
        life.open = false
        if (!found)
            return
        doc.setView(found.view)
        Docs.viewport = found.viewport
        Tools.tool = found.tool
    }
    Keys.onEscapePressed: rejected()
    Keys.onReturnPressed: accepted()
    Keys.onEnterPressed: accepted()
    // The canvas's zoom, as the window's View menu has them; Vừa màn hình fits the space's frame (Docs.viewport).
    Instantiator {
        model: [[["Ctrl+0"], "@zoomFit"], [["Ctrl+1"], "@zoom100"], [["Ctrl++", "Ctrl+="], "@zoomIn"], [["Ctrl+-"], "@zoomOut"]]
        delegate: Shortcut {
            required property var modelData
            sequences: modelData[0]
            enabled: !!ws.doc
            onActivated: Docs.exec(modelData[1])
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {  // the header, as the window's top bar
            Layout.fillWidth: true
            implicitHeight: 44
            color: Theme.bg
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
            Row {
                x: 12
                height: 43
                Row {  // the mark and the wordmark
                    anchors.verticalCenter: parent.verticalCenter
                    rightPadding: 14
                    spacing: 8
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        radius: 5
                        color: Theme.accent
                        Rectangle { x: 4; y: 4; width: 7; height: 7; radius: 3.5; color: Theme.acct }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "photo-beauty"
                        color: Theme.t1
                        font.family: Theme.logo
                        font.weight: Font.ExtraBold
                        font.pointSize: Theme.pt(13.5)
                        font.letterSpacing: -0.135
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 10
                    rightPadding: 10
                    text: ws.title
                    color: Theme.t2
                    font.pointSize: Theme.pt(12)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: ws.doc ? "· " + ws.doc.file + (layer.text ? " · " + qsTr("layer") + " " : "") : ""
                    color: Theme.t3
                    font.pointSize: Theme.pt(12)
                }
                Text {  // the Background in italics, as Layers has it
                    id: layer
                    anchors.verticalCenter: parent.verticalCenter
                    text: ws.doc && ws.doc.active ? ws.doc.active.props.name : ""
                    color: Theme.t3
                    font.pointSize: Theme.pt(12)
                    font.italic: !!ws.doc && !!ws.doc.active && !!ws.doc.active.props.background
                }
            }
        }
        Rectangle {  // the options bar
            visible: optionsRow.children.length > 0
            Layout.fillWidth: true
            implicitHeight: 36
            color: Theme.p1
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
            RowLayout {
                id: optionsRow
                x: 10
                width: parent.width - 20
                height: 35
                spacing: 10
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {  // the tools
                Layout.fillHeight: true
                implicitWidth: 44
                color: Theme.p1
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.ln }
                Column {
                    x: 6
                    y: 6
                    spacing: 2
                    Repeater {
                        model: ws.tools
                        AbstractButton {
                            id: tb
                            required property var modelData
                            readonly property bool on: !!modelData && Tools.tool === modelData[0]
                            width: 32
                            height: modelData ? 32 : 9  // a rule: 1 px, 4 px each side
                            enabled: !!modelData
                            focusPolicy: modelData ? Qt.StrongFocus : Qt.NoFocus
                            hoverEnabled: true
                            text: modelData ? modelData[2] : ""
                            ToolTip.visible: hovered && !!modelData
                            ToolTip.delay: 600
                            ToolTip.text: text
                            Accessible.role: Accessible.Button
                            Accessible.checked: on
                            onClicked: Tools.tool = modelData[0]
                            background: Rectangle {
                                radius: 6
                                color: tb.on ? Theme.p3 : "transparent"
                                Rectangle { visible: !tb.modelData; x: 5; y: 4; width: 22; height: 1; color: Theme.ln }
                            }
                            contentItem: Icon {
                                name: tb.modelData ? tb.modelData[1] : ""
                                color: tb.on ? Theme.t1 : Theme.t2
                            }
                        }
                    }
                }
            }
            Item {  // the canvas
                id: area
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                Loader {  // once the view is noted: Viewport writes it into bounds as it comes
                    id: canvas
                    active: !!ws.found
                    // The rulers stay out of sight: the frame is the whole area.
                    x: ws.viewport && ws.viewport.rulers ? -18 : 0
                    y: x
                    width: area.width - x
                    height: area.height - y
                    sourceComponent: Viewport { doc: ws.doc }
                    onLoaded: ws.adopt(item)
                }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton }  // not the window's menus
                Item { id: over; anchors.fill: parent }  // the frame: the whole area
            }
            Rectangle {  // Properties
                Layout.fillHeight: true
                implicitWidth: 308
                color: Theme.p1
                Rectangle { width: 1; height: parent.height; color: Theme.ln }
                ColumnLayout {
                    x: 1
                    width: parent.width - 1
                    height: parent.height
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        leftPadding: 12
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Properties")
                        color: Theme.t1
                        font.pointSize: Theme.pt(12)
                        font.weight: Font.Medium
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
                    }
                    Flickable {
                        id: scroller
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentHeight: column.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        acceptedButtons: Qt.NoButton  // the wheel and the scroll bar; drags are the sliders'
                        ScrollBar.vertical: ScrollBar {}
                        ColumnLayout {
                            id: column
                            width: scroller.width
                            spacing: 0
                        }
                    }
                    Item {  // the foot
                        Layout.fillWidth: true
                        implicitHeight: 47
                        Rectangle { width: parent.width; height: 1; color: Theme.ln }
                        RowLayout {
                            x: 12
                            y: 11
                            width: parent.width - 24
                            spacing: 8
                            CheckRow {
                                Layout.fillWidth: true
                                text: ws.checkText
                                checked: ws.checked
                                onClicked: ws.checkToggled()
                            }
                            Btn {
                                implicitHeight: 26
                                leftPadding: 12
                                rightPadding: 12
                                text: qsTr("Hủy")
                                onClicked: ws.rejected()
                            }
                            Btn {
                                implicitWidth: Math.max(72, implicitContentWidth + leftPadding + rightPadding)
                                implicitHeight: 26
                                leftPadding: 12
                                rightPadding: 12
                                primary: true
                                text: qsTr("OK")
                                onClicked: ws.accepted()
                            }
                        }
                    }
                }
            }
        }
    }

    // A row of a drop-down's list with a colour chip, for Dropdown's delegate: options [value, label, colour ("" none),
    // italics, picked], as the prototype's lists of colours and layers.
    readonly property Component swatchRow: Component {
        ItemDelegate {
            id: row
            required property var modelData
            width: ListView.view.width
            height: 28
            leftPadding: 6
            rightPadding: 10
            hoverEnabled: true
            highlighted: ListView.isCurrentItem
            background: Rectangle { color: row.highlighted ? Theme.sel : "transparent" }
            contentItem: Row {
                spacing: 6
                Item {
                    width: 16
                    height: parent.height
                    Icon { anchors.centerIn: parent; visible: !!row.modelData[4]; name: "check"; size: 16; color: Theme.t2 }
                }
                Rectangle {
                    visible: !!row.modelData[2]
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    height: 12
                    radius: 4
                    color: row.modelData[2] || "transparent"
                }
                Text {
                    height: parent.height
                    text: row.modelData[1]
                    color: Theme.t1
                    font.pointSize: Theme.pt(12.5)
                    font.italic: !!row.modelData[3]
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // ponytail: found by type in the space's Viewport: the window's overlays (Overlays, U7), which draw the document's
    // own selection and tool, hidden; its MouseArea for the brush's handlers. A Viewport switch for them if V1 wants.
    function adopt(item) {
        for (const c of item.children) {
            if (c instanceof Overlays)
                c.visible = false
            else if (c instanceof MouseArea && c.hoverEnabled)
                pointerArea = c
            else
                adopt(c)
        }
    }
}
