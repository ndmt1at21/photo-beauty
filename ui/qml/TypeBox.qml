pragma ComponentBehavior: Bound
import QtQuick
import "View.js" as View

// Paragraph text being typed on the canvas (docs/ui-tasks.md, U7; artboard Overlays): its box white then dark with 8
// small handles, the text at the canvas's scale turned with the view, a text cursor; selecting, and the input method's
// underline while it composes, are TextEdit's own. The owner lays it over the frame.
Item {
    id: tx
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property var box: null  // { x, y, w, h }, document px
    property string text
    property real fontSize: 200  // document px
    property color color: "#ffffff"
    property string fontFamily: Theme.sans
    readonly property alias editor: edit

    onTextChanged: edit.text = text  // another layer's: typing broke the binding
    readonly property bool ok: !!view && !!size && !!box
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property var origin: ok ? View.toCanvas(view, size, { x: box.x, y: box.y }) : ({ x: 0, y: 0 })

    Item {  // the text's box, turned with the view
        id: frame
        visible: tx.ok
        x: Math.round(tx.origin.x)
        y: Math.round(tx.origin.y)
        width: tx.ok ? Math.round(tx.box.w * tx.s) : 0
        height: tx.ok ? Math.round(tx.box.h * tx.s) : 0
        rotation: tx.ok ? tx.view.angle : 0
        transformOrigin: Item.TopLeft
        Rectangle {  // white, then dark, outside it
            x: -2
            y: -2
            width: frame.width + 4
            height: frame.height + 4
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.4)
            Rectangle { x: 1; y: 1; width: parent.width - 2; height: parent.height - 2; color: "transparent"; border.color: "#ffffff" }
        }
        TextEdit {
            id: edit
            readonly property real em: Math.max(1, Math.round(tx.fontSize * tx.s))
            anchors.fill: parent
            padding: Math.round(em * 0.37)
            text: tx.text
            color: tx.color
            font.family: tx.fontFamily
            font.pixelSize: em  // the image's text, not the interface's: no Theme.pt
            font.weight: Font.Bold
            font.letterSpacing: -0.01 * em
            wrapMode: TextEdit.Wrap
            selectionColor: Qt.rgba(1, 1, 1, 0.3)
            selectedTextColor: tx.color
            clip: true
            Keys.onEscapePressed: focus = false  // done typing: the keys are the canvas's again
            cursorDelegate: Rectangle {
                id: caret
                width: 2
                color: "#ffffff"
                Timer {  // blinks while the text has the focus
                    interval: 530
                    repeat: true
                    running: edit.activeFocus
                    onRunningChanged: caret.opacity = 1
                    onTriggered: caret.opacity = 1 - caret.opacity
                }
            }
        }
        Repeater {
            model: [0, 1, 2, 3, 5, 6, 7, 8]
            Rectangle {
                required property int modelData
                x: Math.round(frame.width * (modelData % 3) / 2) - 4
                y: Math.round(frame.height * Math.floor(modelData / 3) / 2) - 4
                width: 8
                height: 8
                color: "#1c1c1e"
                Rectangle { x: 1; y: 1; width: 6; height: 6; color: "#ffffff" }
            }
        }
    }
}
