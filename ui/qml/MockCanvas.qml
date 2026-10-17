pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import "View.js" as View
import "Mock.js" as Mock

// The mock canvas (docs/ui-tasks.md, U6; decision 3: adjustments, filters, masks leave the photo as it is): the
// document's sample photo where View.js puts it, turned and flipped as Image › Image Rotation and Edit › Transform left
// the canvas and its layer (the prototype's cv: one turn of rot + layerRot, then a flip where the canvas's and the
// layer's differ), a new document's background colour, the checkerboard under what is transparent (Thiết lập › Trong
// suốt và gam), the border of Thiết lập › Viền canvas. V1's Canvas takes its place, given the same doc, view and size.
Item {
    id: mc
    property Doc doc: null
    property var view: null  // Viewport.view: { x, y, zoom, angle }
    property var size: null  // Viewport.size: { w, h, dpr }

    readonly property bool ok: !!doc && !!view && !!size
    readonly property real s: ok ? View.scale(view, size) : 1  // logical pixels per document pixel
    readonly property var centre: ok ? View.toCanvas(view, size, { x: doc.w / 2, y: doc.h / 2 }) : ({ x: 0, y: 0 })
    readonly property var bg: ok ? doc.layers.find(l => l.props.background) || null : null  // the Background layer
    readonly property bool bgHidden: !!bg && hidden(bg)
    readonly property bool showPhoto: ok && doc.photo !== null && doc.photo !== undefined && !bgHidden
    readonly property bool solid: ok && !showPhoto && !!bg && !bgHidden && !!bg.props.color  // a new document's background
    readonly property bool transparent: ok && (bgHidden || doc.layerRot % 180 !== 0 || (!showPhoto && !solid))
    // The checker's square, logical px (Cỡ ô caro: Không, Nhỏ, Vừa as the prototype's 8 px tile, Lớn), and its colours.
    readonly property int square: [0, 2, 4, 8][Prefs.value("checkerSize")] ?? 4
    readonly property color ink1: Prefs.value("checkerColor1")
    readonly property color ink2: Prefs.value("checkerColor2")
    // The part of the page in the frame, in the page's own coordinates: where the checker goes, never more than the frame
    // holds at any zoom.
    readonly property rect seen: {
        if (!ok)
            return Qt.rect(0, 0, 0, 0)
        const xs = [], ys = []
        for (const p of [{ x: 0, y: 0 }, { x: size.w, y: 0 }, { x: 0, y: size.h }, { x: size.w, y: size.h }]) {
            const d = View.toDoc(view, size, p)
            xs.push(d.x * s)
            ys.push(d.y * s)
        }
        const x0 = Math.max(0, Math.min.apply(null, xs)), y0 = Math.max(0, Math.min.apply(null, ys))
        return Qt.rect(x0, y0, Math.max(0, Math.min(page.width, Math.max.apply(null, xs)) - x0), Math.max(0, Math.min(page.height, Math.max.apply(null, ys)) - y0))
    }

    function hidden(l) {  // l or a group it is in hidden
        for (let x = l; x; x = x.parent ? doc.layer(x.parent) : null)
            if (!x.props.visible)
                return true
        return false
    }
    // A 2q × 2q tile of the checkerboard, a at its top left, as the prototype's .ck: a PPM in a data URL.
    function tile(q, a, b) {
        const rgb = c => Math.round(c.r * 255) + " " + Math.round(c.g * 255) + " " + Math.round(c.b * 255)
        const A = rgb(a), B = rgb(b), rows = []
        for (let y = 0; y < 2 * q; y++) {
            const row = []
            for (let x = 0; x < 2 * q; x++)
                row.push((x < q) === (y < q) ? A : B)
            rows.push(row.join(" "))
        }
        return "data:image/x-portable-pixmap," + encodeURIComponent("P3 " + 2 * q + " " + 2 * q + " 255 " + rows.join(" "))
    }

    // The document: w × h document pixels at the view's scale, turned about its centre.
    Item {
        id: page
        visible: mc.ok
        width: mc.ok ? mc.doc.w * mc.s : 0
        height: mc.ok ? mc.doc.h * mc.s : 0
        x: mc.centre.x - width / 2
        y: mc.centre.y - height / 2
        rotation: mc.ok ? mc.view.angle : 0

        RectangularShadow {  // Viền canvas › Bóng: the prototype's box-shadow; a shader, so not offscreen
            anchors.fill: parent
            visible: Prefs.value("canvasBorder") === 0
            offset.y: 8
            blur: 28
            color: Theme.shadow
        }
        Rectangle {  // Bóng and Đường: a line of black on the image's edge
            x: -1
            y: -1
            width: parent.width + 2
            height: parent.height + 2
            visible: Prefs.value("canvasBorder") !== 2
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.4)
        }
        Item {
            anchors.fill: parent
            clip: true  // a layer turned by 90° sticks out
            Rectangle {  // Cỡ ô caro › Không
                anchors.fill: parent
                visible: mc.transparent && !mc.square
                color: mc.ink1
            }
            Image {
                readonly property int t: 2 * mc.square
                visible: mc.transparent && t > 0
                x: t ? Math.floor(mc.seen.x / t) * t : 0  // on the page's grid of tiles, as it pans
                y: t ? Math.floor(mc.seen.y / t) * t : 0
                width: mc.seen.x + mc.seen.width - x
                height: mc.seen.y + mc.seen.height - y
                fillMode: Image.Tile
                horizontalAlignment: Image.AlignLeft  // tiles from the top left, not centred
                verticalAlignment: Image.AlignTop
                smooth: false
                source: t > 0 ? mc.tile(mc.square, mc.ink1, mc.ink2) : ""  // a data URL loads in the background: kept, not reloaded when shown
            }
            Rectangle {
                anchors.fill: parent
                visible: mc.solid
                color: mc.solid ? mc.bg.props.color : "transparent"
            }
            Image {  // the canvas before Image Rotation, covered by the photo; turned, then flipped in the page
                id: image
                readonly property bool turned: mc.ok && mc.doc.rot % 180 !== 0
                anchors.centerIn: parent
                visible: mc.showPhoto
                width: (turned ? parent.height : parent.width)
                height: (turned ? parent.width : parent.height)
                rotation: mc.ok ? (mc.doc.rot + mc.doc.layerRot) % 360 : 0
                transform: Scale {
                    origin.x: image.width / 2
                    origin.y: image.height / 2
                    xScale: mc.ok && mc.doc.flipH !== mc.doc.layerFlipH ? -1 : 1
                    yScale: mc.ok && mc.doc.flipV !== mc.doc.layerFlipV ? -1 : 1
                }
                source: mc.showPhoto ? Mock.photoUrl(mc.doc.photo) : ""
                fillMode: Image.PreserveAspectCrop
                smooth: true
            }
        }
    }
}
