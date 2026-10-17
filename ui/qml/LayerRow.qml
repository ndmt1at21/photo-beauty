pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Mock.js" as Mock

// A row of the Layers panel (docs/ui-tasks.md, U9; the LayerRow card of Controls, PanelsA, the rows of Main.dc.html):
// size + 8 tall (32 with the 24 thumbnail; Nhỏ, Vừa, Lớn are 16, 24, 40): the eye, 16 of indent a level, the clipping
// arrow, a group's twist and folder, the thumbnail by kind, the mask behind its link (striped while off), the name
// (italic for the Background), fx, the lock, the colour label on the left edge. Under a layer with effects, its Hiệu ứng
// row and a row per effect, each with an eye. It shows what it is given and tells what was done to it; LayersPanel
// runs the commands.
Item {
    id: row
    property var entry: null  // a Doc layers entry: { id, kind, props, depth, parent, index, count }
    property string fx  // "": the layer; "effects": its Hiệu ứng row; else the key of one of its effects (props.fx)
    property string label  // an effect row's text
    property bool shown: true  // the eye open
    property bool hasFx  // the layer has effects: its fx button
    property url src  // the photo of a pixel layer or smart object
    property bool selected
    property bool editMask  // its mask takes the brush, so the ring goes round the mask
    property bool open  // a group's layers shown
    property bool fxOpen  // its effects listed
    property bool renaming
    property int size: 24  // the thumbnail's side
    readonly property var p: entry ? entry.props : ({})
    readonly property string kind: entry ? entry.kind : ""
    readonly property string text: fx ? label : p.name || ""
    readonly property bool locked: !!(p.lockAll || p.lockTransparency || p.lockPixels || p.lockPosition)
    // 16 a level, 4 more for a clipped layer; an effect row under its layer's name (PanelsA)
    readonly property int indent: entry ? entry.depth * 16 + (fx === "effects" ? 30 : fx ? 42 : p.clip ? 4 : 0) : 0
    // What the thumbnail shows: photo, gray, heal, empty (the checker), fill (a colour or gradient), text, adj, shape;
    // nothing for a group (its folder) or an effect.
    readonly property string thumb: fx || kind === "group" ? "" : ["text", "adj", "shape", "fill"].indexOf(kind) >= 0 ? kind
        : p.thumb === "gray" || p.thumb === "heal" ? p.thumb : p.photo !== null && p.photo !== undefined && !p.thumb ? "photo" : p.color && !p.thumb ? "fill" : "empty"

    signal eyeClicked(int modifiers)
    signal twistClicked()
    signal fxClicked()
    signal picked(int modifiers, string part)  // a click; part: "thumb", "mask", else "row"
    signal opened(string part)  // a double click; part: "name" on the name, else "row"
    signal menuAsked(real x, real y)  // the right button, in the row
    signal dragged(real x, real y)  // the left button held and moved past 6 px, in the row
    signal dropped(int modifiers)  // let go after dragged()
    signal renamed(string name, bool ok)  // the name field: Enter or left (ok), Esc

    implicitWidth: 307
    implicitHeight: size + 8

    // A click's or double click's part of the row at x.
    function partAt(x) {
        const over = it => it.visible && x >= it.mapToItem(row, 0, 0).x && x < it.mapToItem(row, 0, 0).x + it.width
        return over(maskBox) ? "mask" : over(thumbBox) ? "thumb" : x >= nameText.x && x < nameText.x + Math.min(nameText.contentWidth, nameText.width) + 4 && !fx ? "name" : "row"
    }

    Rectangle {
        anchors.fill: parent
        color: row.selected && !row.fx ? Theme.acc2 : hover.hovered ? Theme.sel : "transparent"
    }
    HoverHandler { id: hover }
    Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.p2 }
    Rectangle { visible: !!row.p.tag && !row.fx; width: 3; height: parent.height; color: row.p.tag || "transparent" }  // the colour label

    AbstractButton {  // the row for the keyboard (Enter picks it); the mouse is the MouseArea's
        id: pick
        x: 30
        width: parent.width - 30
        height: parent.height - 1
        Accessible.name: row.fx ? row.text : qsTr("Chọn layer %1").arg(row.text)
        onClicked: row.picked(0, "row")
        MouseArea {
            id: hit
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            property real x0
            property real y0
            property bool moved
            onPressed: e => {
                x0 = e.x
                y0 = e.y
                moved = false
                if (e.button === Qt.RightButton)
                    row.menuAsked(e.x + pick.x, e.y)
            }
            onPositionChanged: e => {
                if (!(e.buttons & Qt.LeftButton) || (!moved && Math.abs(e.x - x0) + Math.abs(e.y - y0) < 6))
                    return
                moved = true
                row.dragged(e.x + pick.x, e.y)
            }
            onReleased: e => {
                if (e.button !== Qt.LeftButton)
                    return
                if (moved)
                    row.dropped(e.modifiers)
                else
                    row.picked(e.modifiers, row.partAt(e.x + pick.x))
            }
            onCanceled: if (moved) row.dropped(-1)
            onDoubleClicked: e => { if (e.button === Qt.LeftButton) row.opened(row.partAt(e.x + pick.x)) }
        }
    }

    AbstractButton {
        id: eye
        width: 30
        height: parent.height - 1
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Ẩn hiện · Indicates layer visibility (Alt nhấp: chỉ hiện layer này)")
        Accessible.name: qsTr("Ẩn hiện %1").arg(row.text)
        Accessible.checkable: true
        Accessible.checked: row.shown
        onClicked: row.eyeClicked(0)  // from the keyboard; the mouse brings its modifiers
        background: Rectangle { x: 29; width: 1; height: parent.height; color: Theme.p2 }
        contentItem: Icon { name: row.shown ? "visibility" : "visibility_off"; size: 16; color: row.shown ? Theme.t2 : Theme.t4 }
        MouseArea { anchors.fill: parent; onClicked: e => row.eyeClicked(e.modifiers) }
    }

    Row {  // .lrow's items after the eye and indent, 6 apart
        id: items
        x: 42 + row.indent
        height: parent.height - 1
        spacing: 6
        Icon { visible: !!row.p.clip && !row.fx; anchors.verticalCenter: parent.verticalCenter; name: "subdirectory_arrow_right"; size: 14; color: Theme.t3 }
        AbstractButton {
            visible: row.kind === "group" && !row.fx
            width: 14
            height: 24
            anchors.verticalCenter: parent.verticalCenter
            Accessible.name: qsTr("Mở hay thu nhóm %1").arg(row.text)
            onClicked: row.twistClicked()
            contentItem: Icon { name: row.open ? "expand_more" : "chevron_right"; size: 14; color: Theme.t3 }
        }
        Icon { visible: row.kind === "group" && !row.fx; anchors.verticalCenter: parent.verticalCenter; name: "folder"; size: 18; color: Theme.t2 }
        Item {
            id: thumbBox
            visible: row.thumb !== ""
            width: row.size
            height: row.size
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {  // text, adjustment, shape: on p3
                anchors.fill: parent
                visible: row.thumb === "text" || row.thumb === "adj" || row.thumb === "shape"
                radius: 2
                color: Theme.p3
                Text {
                    visible: row.thumb === "text"
                    anchors.centerIn: parent
                    text: "T"
                    color: Theme.t1
                    font.pointSize: Theme.pt(14 * row.size / 24)
                    font.weight: Font.DemiBold
                }
                Icon { visible: row.thumb === "adj"; anchors.centerIn: parent; name: Mock.ADJ_ICONS[row.p.adj] || "tune"; size: 16 * row.size / 24; color: Theme.t1 }
                Rectangle {
                    visible: row.thumb === "shape"
                    anchors.centerIn: parent
                    width: 14 * row.size / 24
                    height: 11 * row.size / 24
                    radius: row.p.shape === "ellipS" ? height / 2 : 2
                    color: row.p.color || "#c0587e"
                }
            }
            Rectangle { anchors.fill: parent; visible: row.thumb === "gray"; radius: 2; color: "#808080" }  // 50 % grey
            Image {
                id: photo
                anchors.fill: parent
                visible: row.thumb === "photo" && !row.p.look
                source: row.thumb === "photo" ? row.src : ""
                sourceSize: Qt.size(2 * row.size, 2 * row.size)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                onStatusChanged: if (row.p.look) art.requestPaint()
            }
            Canvas {  // the checker, a fill's colour or gradient, a photo through the prototype's CSS filter
                id: art
                anchors.fill: parent
                visible: row.thumb === "heal" || row.thumb === "empty" || row.thumb === "fill" || (row.thumb === "photo" && !!row.p.look)
                readonly property var deps: [row.thumb, row.p.color, row.p.look, width, visible]
                onDepsChanged: requestPaint()
                onImageLoaded: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"), w = width, h = height
                    ctx.reset()
                    if (!visible)
                        return
                    ctx.beginPath()
                    ctx.roundedRect(0, 0, w, h, 2, 2)
                    ctx.clip()
                    if (row.thumb === "fill")
                        return row.paintCss(ctx, row.p.color || "#808080", w, h)
                    if (row.thumb === "photo") {  // decoded at the size covering it, the Image's shape (object-fit: cover)
                        const url = String(row.src), iw = photo.implicitWidth, ih = photo.implicitHeight
                        if (photo.status !== Image.Ready || !iw || !ih)
                            return
                        const k = Math.max(w / iw, h / ih), cw = Math.ceil(iw * k), ch = Math.ceil(ih * k)
                        if (!isImageLoaded(url))
                            return loadImage(url, Qt.size(cw, ch))
                        ctx.drawImage(url, (w - cw) / 2, (h - ch) / 2, cw, ch)
                        return row.filter(ctx, row.p.look, w, h)
                    }
                    row.checker(ctx, w, h)
                    if (row.thumb === "heal") {  // three healed spots, as the prototype's
                        ctx.fillStyle = "#c98f74"
                        const k = w / 24
                        for (const d of [[7.5, 9.5, 1.5], [15.5, 14.5, 1.5], [11, 18, 1]]) {
                            ctx.beginPath()
                            ctx.arc(d[0] * k, d[1] * k, d[2] * k, 0, 2 * Math.PI)
                            ctx.fill()
                        }
                    }
                }
            }
            Rectangle {  // smart object badge
                visible: row.kind === "so"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 12
                height: 12
                color: Theme.p1
                topLeftRadius: 2
                Icon { anchors.centerIn: parent; name: "deployed_code"; size: 11; color: Theme.t1 }
            }
            Rectangle {  // the ring of the layer taking the brush; a grey or a fill one 1 px off it
                readonly property int gap: row.thumb === "gray" || row.thumb === "fill" ? 1 : 0
                anchors.fill: parent
                anchors.margins: -1 - gap
                radius: 3 + gap
                color: "transparent"
                border.width: 1
                border.color: row.selected && !row.editMask ? Theme.t1 : "transparent"
            }
        }
        Item {  // the link between the thumbnail and the mask (.i 12 with margins of −3)
            visible: !!row.p.mask && !row.fx
            width: 6
            height: parent.height
            Icon { anchors.centerIn: parent; name: "link"; size: 12; color: Theme.t3 }
        }
        Item {
            id: maskBox
            visible: !!row.p.mask && !row.fx
            width: row.size
            height: row.size
            anchors.verticalCenter: parent.verticalCenter
            Canvas {
                anchors.fill: parent
                readonly property var deps: [row.p.mask, row.p.maskOff, width, maskBox.visible]
                onDepsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"), w = width, h = height
                    ctx.reset()
                    if (!row.p.mask)
                        return
                    ctx.beginPath()
                    ctx.roundedRect(0, 0, w, h, 2, 2)
                    ctx.clip()
                    row.paintCss(ctx, row.p.mask, w, h)
                    if (!row.p.maskOff)
                        return
                    ctx.strokeStyle = Theme.danger  // off: red stripes over it, as the prototype's
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    for (let k = -h; k < w; k += 6 * Math.SQRT2) {
                        ctx.moveTo(k, 0)
                        ctx.lineTo(k + h, h)
                    }
                    ctx.stroke()
                }
            }
            Rectangle {  // 1 px off it: t4, t1 while the mask takes the brush
                anchors.fill: parent
                anchors.margins: -2
                radius: 4
                color: "transparent"
                border.width: 1
                border.color: row.selected && row.editMask ? Theme.t1 : Theme.t4
            }
        }
    }
    Text {
        id: nameText
        x: items.x + (items.width > 0 ? items.width + 6 : 0)
        width: Math.max(0, (tail.width > 0 ? tail.x - 6 : row.width - 8) - x)
        height: parent.height - 1
        visible: !row.renaming
        text: row.text
        color: row.selected && !row.fx ? Theme.t1 : row.shown ? Theme.t2 : Theme.t3
        font.pointSize: Theme.pt(12)
        font.italic: !!row.fx || !!row.p.background
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    Loader {
        active: row.renaming
        x: nameText.x
        width: nameText.width
        anchors.verticalCenter: parent.verticalCenter
        sourceComponent: TextBox {
            implicitHeight: 24
            leftPadding: 6
            rightPadding: 6
            text: row.p.name || ""
            Accessible.name: qsTr("Tên layer")
            Component.onCompleted: {
                forceActiveFocus()
                selectAll()
            }
            onAccepted: row.renamed(text, true)
            onActiveFocusChanged: if (!activeFocus) row.renamed(text, true)
            Keys.onEscapePressed: row.renamed(text, false)
        }
    }
    Row {
        id: tail
        anchors.right: parent.right
        anchors.rightMargin: 8
        height: parent.height - 1
        spacing: 6
        AbstractButton {
            visible: row.hasFx && !row.fx
            height: parent.height
            implicitWidth: fxRow.implicitWidth
            anchors.verticalCenter: parent.verticalCenter
            Accessible.name: qsTr("Hiệu ứng của %1").arg(row.text)
            onClicked: row.fxClicked()
            contentItem: Row {
                id: fxRow
                spacing: 1
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "fx"
                    color: Theme.t2
                    font.pointSize: Theme.pt(12)
                    font.italic: true
                    font.weight: Font.DemiBold
                }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: row.fxOpen ? "expand_less" : "expand_more"; size: 12; color: Theme.t2 }
            }
        }
        Icon { visible: row.locked && !row.fx; anchors.verticalCenter: parent.verticalCenter; name: "lock"; size: 14; color: Theme.t3 }
    }

    // The transparency checker (.ck: 4 px squares, #c9c9c9 top right).
    function checker(ctx, w, h) {
        ctx.fillStyle = "#ffffff"
        ctx.fillRect(0, 0, w, h)
        ctx.fillStyle = "#c9c9c9"
        for (let y = 0; y < h; y += 4)
            for (let x = (y / 4) % 2 ? 0 : 4; x < w; x += 8)
                ctx.fillRect(x, y, 4, 4)
    }
    // A CSS background as the prototype writes masks and fills: a colour, or linear-gradient() and radial-gradient()
    // layers over one, the first on top. Stops are colours with positions in % (none: evenly spaced); "transparent" is
    // its neighbour's colour at alpha 0; a radial one is an ellipse of rx% ry% at x% y%, or a circle reaching the
    // farthest corner.
    function paintCss(ctx, css, w, h) {
        const split = s => {  // at the commas outside brackets
            const out = []
            let depth = 0, from = 0
            for (let i = 0; i < s.length; i++) {
                if (s[i] === "(") depth++
                else if (s[i] === ")") depth--
                else if (s[i] === "," && !depth) { out.push(s.slice(from, i).trim()); from = i + 1 }
            }
            out.push(s.slice(from).trim())
            return out
        }
        const layers = split(String(css))
        for (let i = layers.length - 1; i >= 0; i--) {
            const m = /^(linear|radial)-gradient\((.*)\)$/.exec(layers[i])
            if (!m) {
                ctx.fillStyle = layers[i]
                ctx.fillRect(0, 0, w, h)
                continue
            }
            const parts = split(m[2])
            const head = /^(#|transparent)/.test(parts[0]) ? "" : parts.shift()
            const stops = []
            parts.forEach(s => {
                const c = s.split(/\s+/)
                for (const at of c.length > 1 ? c.slice(1) : [""])
                    stops.push([c[0], at === "" ? NaN : parseFloat(at) / 100])
            })
            let g, box = [0, 0, w, h]
            if (m[1] === "linear") {  // "135deg" (to the bottom right); none: down
                const a = (head ? parseFloat(head) : 180) * Math.PI / 180, dx = Math.sin(a), dy = -Math.cos(a)
                const half = (Math.abs(w * dx) + Math.abs(h * dy)) / 2
                g = ctx.createLinearGradient(w / 2 - dx * half, h / 2 - dy * half, w / 2 + dx * half, h / 2 + dy * half)
            } else {  // "ellipse 30% 26% at 57% 42%", "circle at 42% 40%": a unit circle, scaled
                const at = /at\s+([\d.]+)%\s+([\d.]+)%/.exec(head), r = /([\d.]+)%\s+([\d.]+)%\s+at/.exec(head)
                const cx = at ? parseFloat(at[1]) / 100 * w : w / 2, cy = at ? parseFloat(at[2]) / 100 * h : h / 2
                const far = Math.sqrt(Math.pow(Math.max(cx, w - cx), 2) + Math.pow(Math.max(cy, h - cy), 2))
                const rx = r ? parseFloat(r[1]) / 100 * w : far, ry = r ? parseFloat(r[2]) / 100 * h : far
                ctx.setTransform(rx, 0, 0, ry, cx, cy)
                g = ctx.createRadialGradient(0, 0, 0, 0, 0, 1)
                box = [-cx / rx, -cy / ry, w / rx, h / ry]
            }
            stops.forEach((s, k) => {
                const near = s[0] !== "transparent" ? s[0] : (stops[k - 1] || stops[k + 1] || ["#000000"])[0]
                const c = Qt.color(near === "transparent" ? "#000000" : near)
                g.addColorStop(Math.max(0, Math.min(1, isNaN(s[1]) ? (stops.length > 1 ? k / (stops.length - 1) : 0) : s[1])),
                               Qt.rgba(c.r, c.g, c.b, s[0] === "transparent" ? 0 : c.a))
            })
            ctx.fillStyle = g
            ctx.fillRect(box[0], box[1], box[2], box[3])
            ctx.setTransform(1, 0, 0, 1, 0, 0)
        }
    }
    // The prototype's CSS filter on a thumbnail ("grayscale(1) contrast(0.16) brightness(1.25)", "blur(1.4px)"), its
    // functions in order, on what the canvas holds.
    function filter(ctx, look, w, h) {
        const d = ctx.getImageData(0, 0, w, h), px = d.data, n = w * h * 4
        let a = []
        for (let i = 0; i < n; i++)
            a.push(px[i])
        const re = /(\w+)\(([\d.]+)/g
        for (let f = re.exec(look); f; f = re.exec(look)) {
            const k = parseFloat(f[2])
            if (f[1] === "blur") {  // 3 × 3 boxes, each a variance of 2/3, as many as make σ²
                for (let pass = 0; pass < Math.max(1, Math.round(k * k * 1.5)); pass++) {
                    const b = a.slice()
                    for (let y = 0; y < h; y++)
                        for (let x = 0; x < w; x++)
                            for (let c = 0; c < 3; c++) {
                                let s = 0, m = 0
                                for (let v = Math.max(0, y - 1); v <= Math.min(h - 1, y + 1); v++)
                                    for (let u = Math.max(0, x - 1); u <= Math.min(w - 1, x + 1); u++) {
                                        s += b[(v * w + u) * 4 + c]
                                        m++
                                    }
                                a[(y * w + x) * 4 + c] = s / m
                            }
                }
                continue
            }
            for (let i = 0; i < n; i += 4) {
                const lum = 0.2126 * a[i] + 0.7152 * a[i + 1] + 0.0722 * a[i + 2]
                for (let c = i; c < i + 3; c++)
                    a[c] = f[1] === "grayscale" ? a[c] + (lum - a[c]) * Math.min(1, k) : f[1] === "saturate" ? lum + (a[c] - lum) * k
                         : f[1] === "contrast" ? (a[c] - 127.5) * k + 127.5 : f[1] === "brightness" ? a[c] * k : a[c]
            }
        }
        for (let i = 0; i < n; i++)
            px[i] = Math.max(0, Math.min(255, Math.round(a[i])))
        ctx.putImageData(d, 0, 0, 0, 0, w, h)  // Qt's Context2D does nothing given fewer than all seven
    }
}
