import QtQuick
import "Mock.js" as Mock

// The Histogram panel (docs/ui-tasks.md, U12; PanelsB, the histo part of Main.dc.html), opened beside the icon column:
// the compact RGB view (Histogram, as snapone's), counted from a copy of what the canvas shows 160 px wide, drawn in a
// hidden Canvas as snapone's probe: the document's sample photo cropped to it as MockCanvas draws it, a new
// document's background colour; nothing for a transparent one. Counted again while shown when either changes, and by
// the refresh button on the tab strip (tabTool).
Item {
    id: panel
    readonly property Doc doc: Docs.current
    readonly property var bgl: doc ? doc.layers.find(l => l.props.background) || null : null  // the Background layer
    readonly property bool shown: !bgl || bgl.props.visible
    readonly property var photo: doc && shown ? Mock.PHOTOS[doc.photo] || null : null
    readonly property url src: photo ? Mock.photoUrl(doc.photo) : ""
    readonly property string fill: !photo && bgl && shown && bgl.props.color ? bgl.props.color : ""
    property var counts: null  // [r, g, b], 256 bins each, as the core's Histogram; null for none
    property var menu: []
    // The refresh, on the side panel's tab strip as PanelsB's compact view (SidePanel shows a panel's tabTool).
    readonly property var tabTool: ({ icon: "refresh", tip: qsTr("Tính lại từ ảnh hiện tại · Uncached Refresh"), name: qsTr("Tính lại histogram"),
                                      act: () => panel.refresh() })
    implicitWidth: 280
    implicitHeight: 118  // 10 + 96 + 12: the chart right under the tabs

    // The counts of RGBA pixels px (an ImageData's data): [r, g, b], 256 bins each.
    function tally(px) {
        const h = [[], [], []]
        for (let k = 0; k < 3; k++)
            for (let i = 0; i < 256; i++)
                h[k].push(0)
        for (let i = 0; i < px.length; i += 4) {
            h[0][px[i]]++
            h[1][px[i + 1]]++
            h[2][px[i + 2]]++
        }
        return h
    }
    function refresh() { probe.fetch() }

    readonly property var deps: [src, fill, probe.size, visible]
    onDepsChanged: if (visible) refresh()

    Histogram {
        x: 12
        y: 10
        width: parent.width - 24
        counts: panel.counts
    }
    Canvas {
        id: probe
        readonly property var size: {  // the photo's size covering the probe
            const p = panel.photo
            const s = p ? Math.max(width / p.w, height / p.h) : 0
            return p ? Qt.size(Math.ceil(p.w * s), Math.ceil(p.h * s)) : null
        }
        width: 160
        height: panel.doc ? Math.max(1, Math.round(160 * panel.doc.h / panel.doc.w)) : 1
        opacity: 0  // painted, not seen
        function fetch() {  // decoded small; already in the cache, it is loaded at once
            if (size)
                loadImage(panel.src, size)
            requestPaint()
        }
        onImageLoaded: requestPaint()
        onPaint: {
            const ctx = getContext("2d"), w = width, h = height
            ctx.reset()
            if (size && isImageLoaded(panel.src))
                ctx.drawImage(panel.src, (w - size.width) / 2, (h - size.height) / 2)
            else if (panel.fill) {
                ctx.fillStyle = panel.fill
                ctx.fillRect(0, 0, w, h)
            } else {
                if (!size)  // nothing drawn: a transparent document
                    panel.counts = null
                return
            }
            panel.counts = panel.tally(ctx.getImageData(0, 0, w, h).data)
        }
    }
}
