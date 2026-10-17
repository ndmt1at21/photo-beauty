import QtQuick
import QtQuick.Shapes
import "View.js" as View

// Retouch's overlay on a portrait (docs/ui-tasks.md, U7; the prototype's ov.skin, ov.eyes, ov.lips, ov.teeth, ov.face,
// ov.cap): the face's box and name, and, with Xem lớp phủ on, the masks whose chips are on in their colours, the
// caption bottom left of the frame. The prototype's shapes, drawn on a 550 × 700 portrait, stretched to the document.
// The owner lays it over the frame.
Item {
    id: fo
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property bool active: false  // drawn at all: Overlays, the Retouch panel shown on a document with faces
    property var retouch: Tools.retouch  // { chips: { skin, eyes, lips, teeth }, overlay, feather }
    property string label: qsTr("Mặt 1")

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0 && active
    readonly property bool overlay: ok && !!retouch && !!retouch.overlay
    // The masks drawn, and the caption's text ("" none).
    readonly property var parts: {
        const c = overlay && retouch.chips ? retouch.chips : {}
        return { skin: !!c.skin, eyes: !!c.eyes, lips: !!c.lips, teeth: !!c.teeth }
    }
    readonly property string caption: {
        const names = [["skin", qsTr("Da")], ["eyes", qsTr("Mắt")], ["lips", qsTr("Môi")], ["teeth", qsTr("Răng")]].filter(c => parts[c[0]]).map(c => c[1])
        return names.length ? qsTr("Xem trước Mask: %1 · Feather %2 px").arg(names.join(", ")).arg(retouch.feather) : ""
    }
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property var origin: ok ? View.toCanvas(view, size, { x: 0, y: 0 }) : ({ x: 0, y: 0 })
    readonly property var face: ({ x: 214 / 550 * docW, y: 132 / 700 * docH, w: 206 / 550 * docW, h: 322 / 700 * docH })  // document px
    readonly property var faceAt: ok ? View.toCanvas(view, size, face) : ({ x: 0, y: 0 })

    Item {  // the document's box, turned with the view
        x: Math.round(fo.origin.x)
        y: Math.round(fo.origin.y)
        width: Math.round(fo.docW * fo.s)
        height: Math.round(fo.docH * fo.s)
        rotation: fo.ok ? fo.view.angle : 0
        transformOrigin: Item.TopLeft
        visible: fo.ok
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeWidth: -1
                fillColor: Qt.rgba(1, 45 / 255, 85 / 255, 0.42)
                fillRule: ShapePath.OddEvenFill
                scale: Qt.size(fo.docW * fo.s / 550, fo.docH * fo.s / 700)
                PathSvg {
                    path: fo.parts.skin ? "M228 181 L271 151 L322 142 L374 151 L404 185 L413 241 L404 310 L391 379 L357 426 L322 444 L279 426 L249 388 L230 327 L219 258 Z M249 422 L374 426 L383 487 L236 487 Z M249 276 a26 9.5 0 1 0 52 0 a26 9.5 0 1 0 -52 0 Z M344 276 a26 9.5 0 1 0 52 0 a26 9.5 0 1 0 -52 0 Z M241 252 a30 7 0 1 0 60 0 a30 7 0 1 0 -60 0 Z M340 252 a30 7 0 1 0 60 0 a30 7 0 1 0 -60 0 Z M282 381 a40 15 0 1 0 80 0 a40 15 0 1 0 -80 0 Z" : ""
                }
            }
            ShapePath {
                strokeWidth: -1
                fillColor: Qt.rgba(1, 214 / 255, 0, 0.5)
                scale: Qt.size(fo.docW * fo.s / 550, fo.docH * fo.s / 700)
                PathSvg { path: fo.parts.eyes ? "M249 276 a26 9.5 0 1 0 52 0 a26 9.5 0 1 0 -52 0 Z M344 276 a26 9.5 0 1 0 52 0 a26 9.5 0 1 0 -52 0 Z" : "" }
            }
            ShapePath {
                strokeWidth: -1
                fillColor: Qt.rgba(170 / 255, 60 / 255, 1, 0.45)
                scale: Qt.size(fo.docW * fo.s / 550, fo.docH * fo.s / 700)
                PathSvg { path: fo.parts.lips ? "M282 381 a40 15 0 1 0 80 0 a40 15 0 1 0 -80 0 Z" : "" }
            }
            ShapePath {
                strokeWidth: -1
                fillColor: Qt.rgba(0, 200 / 255, 1, 0.55)
                scale: Qt.size(fo.docW * fo.s / 550, fo.docH * fo.s / 700)
                PathSvg { path: fo.parts.teeth ? "M300 383 a22 5 0 1 0 44 0 a22 5 0 1 0 -44 0 Z" : "" }
            }
        }
        Rectangle {  // the face's box: white, dark around it
            x: Math.round(fo.face.x * fo.s) - 1
            y: Math.round(fo.face.y * fo.s) - 1
            width: Math.round(fo.face.w * fo.s) + 2
            height: Math.round(fo.face.h * fo.s) + 2
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.35)
            Rectangle {
                x: 1
                y: 1
                width: parent.width - 2
                height: parent.height - 2
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.9)
            }
        }
    }
    Rectangle {  // the face's name above its box
        visible: fo.ok
        x: Math.round(fo.faceAt.x)
        y: Math.round(fo.faceAt.y) - 22
        width: name.implicitWidth + 14
        height: name.implicitHeight + 4
        radius: 4
        color: Theme.onPhoto
        Text {
            id: name
            x: 7
            y: 2
            text: fo.label
            color: "#ffffff"
            font.pointSize: Theme.pt(11)
        }
    }
    Rectangle {  // Xem trước Mask: …
        visible: fo.caption !== ""
        x: 12
        y: fo.height - 12 - height
        width: cap.implicitWidth + 38
        height: cap.implicitHeight + 10
        radius: 6
        color: Theme.onPhoto
        Rectangle {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 10
            height: 10
            radius: 2
            color: Qt.rgba(1, 45 / 255, 85 / 255, 0.7)
        }
        Text {
            id: cap
            x: 28
            y: 5
            text: fo.caption
            color: "#ffffff"
            font.pointSize: Theme.pt(11)
        }
    }
}
