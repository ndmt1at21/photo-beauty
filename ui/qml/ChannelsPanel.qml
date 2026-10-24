pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Mock.js" as Mock

// The Channels panel (docs/ui-tasks.md, U8; PanelsA, the chans and chf parts of Main.dc.html), the second tab of the
// dock's group Layers, Channels: the composite channel and Red, Green, Blue (Gray in a grey document) with their
// thumbnails, then the alpha channels. The eye shows or hides a channel, a click shows that channel alone (Ctrl 2–9 too,
// Dock's keys). At the foot, each one History step: the channel loaded as the selection, the selection saved as a
// channel, a new channel, the alpha channel shown deleted.
Item {
    id: panel
    readonly property Doc doc: Docs.current
    readonly property var colours: !doc ? [] : doc.mode === "Gray" ? ["Gray"] : ["RGB", "Red", "Green", "Blue"]
    readonly property bool alphaShown: !!doc && doc.alphas.indexOf(doc.channel) >= 0
    // Decision 4: the core has no channels yet (the real canvas shows the composite alone), so on a real document the
    // panel and its menu are greyed (DocModel.can()).
    enabled: !doc || doc.supports("panel:Channels")
    property var menu: [
        { l: qsTr("Kênh mới"), disabled: !enabled, act: () => panel.run({ op: "newChannel" }) },
        { l: qsTr("Xóa kênh"), disabled: !enabled || !alphaShown, act: () => panel.run({ op: "deleteChannel", channel: panel.doc.channel }) },
        { t: "sep" },
        { l: qsTr("Lưu vùng chọn thành kênh"), disabled: !enabled || !doc || !doc.selection, act: () => panel.run({ op: "saveSelection" }) },
        { l: qsTr("Nạp kênh làm vùng chọn"), disabled: !enabled, act: () => panel.load() }
    ]
    implicitWidth: 308
    implicitHeight: list.contentHeight + 32

    function run(cmd) { return !!doc && doc.run(cmd) }
    function load() { return run({ op: "select", shape: "skin", name: qsTr("Nạp kênh làm vùng chọn") }) }

    ListView {
        id: list
        width: parent.width
        height: parent.height - 32
        opacity: enabled ? 1 : 0.4  // its rows have no greyed look of their own (decision 4)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        // Names only: showing, hiding or picking a channel keeps the rows.
        model: panel.doc ? panel.colours.concat(panel.doc.alphas) : []
        delegate: Rectangle {
            id: row
            required property string modelData
            required property int index
            readonly property bool alpha: index >= panel.colours.length
            readonly property string channel: modelData === "Gray" ? "RGB" : modelData  // as Doc names it
            // The composite shows the colour channels with it; an alpha channel is shown alone.
            readonly property bool on: !!panel.doc && (panel.doc.channel === channel || (!alpha && panel.doc.channel === "RGB"))
            readonly property bool shown: !!panel.doc && !panel.doc.hiddenChannels[modelData]
            width: ListView.view.width
            height: 40
            color: on ? Theme.acc2 : "transparent"
            Rectangle { y: 39; width: parent.width; height: 1; color: Theme.p2 }
            AbstractButton {
                width: 30
                height: 39
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Ẩn hiện · Indicates channel visibility")
                Accessible.name: qsTr("Ẩn hiện kênh %1").arg(row.modelData)
                onClicked: panel.doc.toggleChannel(row.modelData)
                background: Rectangle { x: 29; width: 1; height: 39; color: Theme.p2 }
                contentItem: Icon { name: row.shown ? "visibility" : "visibility_off"; size: 16; color: row.shown ? Theme.t2 : Theme.t4 }
            }
            AbstractButton {
                x: 30
                width: parent.width - 30
                height: 39
                leftPadding: 10
                rightPadding: 10
                Accessible.name: row.modelData
                onClicked: panel.doc.setChannel(row.channel)
                contentItem: Row {
                    spacing: 10
                    Thumb {
                        anchors.verticalCenter: parent.verticalCenter
                        k: row.alpha ? -2 : row.modelData === "Gray" ? 3 : ["RGB", "Red", "Green", "Blue"].indexOf(row.modelData) - 1
                        photo: panel.doc ? panel.doc.photo : null
                    }
                    Text {
                        width: parent.width - 36 - key.width - 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData
                        color: Theme.t1
                        font.pointSize: Theme.pt(12)
                        elide: Text.ElideRight
                    }
                    Text {
                        id: key
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Ctrl " + (row.alpha ? 6 + row.index - panel.colours.length : 2 + row.index)
                        color: Theme.t3
                        font.family: Theme.mono
                        font.pointSize: Theme.pt(11)
                    }
                }
            }
        }
    }
    // .foot
    Rectangle {
        y: parent.height - 32
        width: parent.width
        height: 1
        color: Theme.ln
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        y: parent.height - 28
        spacing: 2
        ToolToggle {
            icon.name: "load_sel"
            ToolTip.text: qsTr("Nạp kênh làm vùng chọn · Load channel as selection")
            Accessible.name: qsTr("Nạp kênh làm vùng chọn")
            onClicked: panel.load()
        }
        ToolToggle {
            enabled: !!panel.doc && !!panel.doc.selection
            icon.name: "mask"
            ToolTip.text: qsTr("Lưu vùng chọn thành kênh · Save selection as channel")
            Accessible.name: qsTr("Lưu vùng chọn thành kênh")
            onClicked: panel.run({ op: "saveSelection" })
        }
        ToolToggle {
            icon.name: "add_box"
            ToolTip.text: qsTr("Kênh mới · Create new channel")
            Accessible.name: qsTr("Kênh mới")
            onClicked: panel.run({ op: "newChannel" })
        }
        ToolToggle {
            enabled: panel.alphaShown
            icon.name: "delete"
            ToolTip.text: qsTr("Xóa kênh · Delete current channel")
            Accessible.name: qsTr("Xóa kênh")
            onClicked: panel.run({ op: "deleteChannel", channel: panel.doc.channel })
        }
    }

    // A channel's thumbnail, 26 × 32, of the document's photo cropped to fill it (the prototype's object-fit: cover),
    // white for a new document: k -1 the colours; 0–2 Red, Green, Blue in grey, or in their colour with Thiết lập's
    // channelsInColor; 3 the grey of a grey document; -2 an alpha channel, the prototype's mask: white in black.
    component Thumb: Canvas {
        id: t
        required property int k
        required property var photo  // a Mock.PHOTOS index, null for none
        readonly property url src: Mock.photoUrl(photo)
        readonly property var size: {  // the photo's size covering the thumbnail
            const p = Mock.PHOTOS[photo]
            const s = p ? Math.max(width / p.w, height / p.h) : 0
            return p ? Qt.size(Math.ceil(p.w * s), Math.ceil(p.h * s)) : null
        }
        readonly property bool tint: !!Prefs.value("channelsInColor")
        width: 26
        height: 32
        function fetch() {  // decoded small; already in the cache, it is loaded at once
            if (size)
                loadImage(src, size)
            requestPaint()
        }
        Component.onCompleted: fetch()
        onSrcChanged: fetch()
        onImageLoaded: requestPaint()
        onTintChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"), w = width, h = height
            ctx.reset()
            ctx.beginPath()
            ctx.roundedRect(0, 0, w, h, 2, 2)
            ctx.clip()
            if (k === -2) {
                ctx.fillStyle = "black"
                ctx.fillRect(0, 0, w, h)
                ctx.translate(0.52 * w, 0.4 * h)
                ctx.scale(0.34 * w, 0.26 * h)
                const g = ctx.createRadialGradient(0, 0, 0, 0, 0, 1)
                g.addColorStop(0.7, "white")
                g.addColorStop(1, "black")
                ctx.fillStyle = g
                ctx.beginPath()
                ctx.arc(0, 0, 1, 0, 2 * Math.PI)
                ctx.fill()
                return
            }
            if (!size || !isImageLoaded(src)) {
                ctx.fillStyle = "white"
                ctx.fillRect(0, 0, w, h)
                return
            }
            ctx.drawImage(src, (w - size.width) / 2, (h - size.height) / 2)
            if (k < 0)
                return
            const d = ctx.getImageData(0, 0, w, h), p = d.data
            for (let i = 0; i < p.length; i += 4) {
                const v = k === 3 ? Math.round(0.299 * p[i] + 0.587 * p[i + 1] + 0.114 * p[i + 2]) : p[i + k]
                for (let c = 0; c < 3; c++)
                    p[i + c] = !tint || k === 3 || c === k ? v : 0
            }
            ctx.putImageData(d, 0, 0, 0, 0, w, h)  // Qt's Context2D does nothing given fewer than all seven
        }
    }
}
