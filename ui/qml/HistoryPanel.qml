pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Mock.js" as Mock

// The History panel (docs/ui-tasks.md, U12; PanelsA, the hs part of Main.dc.html), opened beside the icon column: the
// snapshots with their thumbnails, then the steps with their tools' icons, the one History is on lit and those after it
// dimmed; a click goes to one. At the foot, as in its menu: a new document from the state shown, a snapshot of it, the
// step deleted with those after it. As tall as its rows (SidePanel scrolls it).
Item {
    id: panel
    readonly property Doc doc: Docs.current
    readonly property bool canDelete: !!doc && doc.index > 0
    readonly property var bgl: doc ? doc.layers.find(l => l.props.background) || null : null  // the Background layer
    readonly property color fill: bgl && bgl.props.color ? bgl.props.color : "white"  // a new document's, for a thumbnail
    property var menu: [  // en: the English names (Phím tắt, U20), as the foot's tooltips
        { l: qsTr("Tài liệu mới từ bước này"), en: "New Document from Current State", act: () => panel.newDoc() },
        { l: qsTr("Tạo snapshot"), en: "Create New Snapshot", act: () => panel.snapshot() },
        { l: qsTr("Xóa bước này và các bước sau"), en: "Delete current state", disabled: !canDelete, act: () => panel.deleteStep() }
    ]
    implicitWidth: 280
    implicitHeight: snaps.height + 1 + steps.height + 32

    function newDoc() { return Docs.duplicate(doc) }  // Docs.duplicate copies the state shown: the step's, or the snapshot's
    function snapshot() { if (doc) doc.newSnapshot() }
    function deleteStep() { if (canDelete) doc.deleteStep() }

    Column {
        id: snaps
        width: parent.width
        topPadding: 4
        bottomPadding: 4
        Repeater {
            model: panel.doc ? panel.doc.snapshots : []
            AbstractButton {
                id: snap
                required property var modelData
                required property int index
                width: snaps.width
                height: 38
                leftPadding: 10
                rightPadding: 10
                Accessible.name: modelData.name
                onClicked: panel.doc.jumpToSnapshot(index)
                background: Rectangle { color: panel.doc && panel.doc.snapshot === snap.index ? Theme.acc2 : "transparent" }
                contentItem: Row {
                    spacing: 10
                    Thumb {
                        visible: !snap.modelData.url
                        anchors.verticalCenter: parent.verticalCenter
                        photo: panel.doc ? panel.doc.photo : null
                        look: snap.modelData.look
                        fill: panel.fill
                    }
                    Image {  // a real document's: the snapshot's own state (image://pb, N1)
                        visible: !!snap.modelData.url
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 32
                        source: snap.modelData.url || ""
                        sourceSize: Qt.size(52, 64)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Text {
                        width: parent.width - 36
                        anchors.verticalCenter: parent.verticalCenter
                        text: snap.modelData.name
                        color: Theme.t1
                        font.pointSize: Theme.pt(12)
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
    Rectangle {
        y: snaps.height
        width: parent.width
        height: 1
        color: Theme.ln
    }
    Column {
        id: steps
        y: snaps.height + 1
        width: parent.width
        topPadding: 4
        bottomPadding: 4
        Repeater {
            model: panel.doc ? panel.doc.history : []
            AbstractButton {
                id: step
                required property var modelData
                required property int index
                readonly property bool cur: !!panel.doc && index === panel.doc.index && panel.doc.snapshot < 0
                readonly property color ink: !!panel.doc && index > panel.doc.index ? Theme.t4 : cur ? Theme.t1 : Theme.t2
                width: steps.width
                height: 26
                leftPadding: 10
                rightPadding: 10
                Accessible.name: modelData.name
                Accessible.checked: cur
                onClicked: panel.doc.jumpTo(index)
                background: Rectangle { color: step.cur ? Theme.acc2 : "transparent" }
                contentItem: Row {
                    spacing: 10
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: step.modelData.icon
                        size: 16
                        color: step.ink
                    }
                    Text {
                        width: parent.width - 26
                        anchors.verticalCenter: parent.verticalCenter
                        text: step.modelData.name
                        color: step.ink
                        font.pointSize: Theme.pt(12)
                        elide: Text.ElideRight
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
            icon.name: "note_add"
            ToolTip.text: qsTr("Tài liệu mới từ bước này · New Document from Current State")
            Accessible.name: qsTr("Tài liệu mới từ bước này")
            onClicked: panel.newDoc()
        }
        ToolToggle {
            icon.name: "photo_camera"
            ToolTip.text: qsTr("Tạo snapshot · Create New Snapshot")
            Accessible.name: qsTr("Tạo snapshot")
            onClicked: panel.snapshot()
        }
        ToolToggle {
            enabled: panel.canDelete
            icon.name: "delete"
            ToolTip.text: qsTr("Xóa bước · Delete current state")
            Accessible.name: qsTr("Xóa bước này và các bước sau")
            onClicked: panel.deleteStep()
        }
    }

    // A snapshot's thumbnail, 26 × 32: the document's photo cropped to fill it (object-fit: cover), its CSS look applied
    // (the prototype's saturate()); without a photo, fill.
    // A mock document's: the photo, not the snapshot's own state (the mock's photo stays as it is, decision 3); a real
    // one's snapshots show their own state (the Image beside it, image://pb).
    component Thumb: Canvas {
        required property var photo  // a Mock.PHOTOS index, null for none
        required property string look
        required property color fill
        readonly property url src: Mock.photoUrl(photo)
        readonly property var size: {  // the photo's size covering the thumbnail
            const p = Mock.PHOTOS[photo]
            const s = p ? Math.max(width / p.w, height / p.h) : 0
            return p ? Qt.size(Math.ceil(p.w * s), Math.ceil(p.h * s)) : null
        }
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
        onLookChanged: requestPaint()
        onFillChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"), w = width, h = height
            ctx.reset()
            ctx.beginPath()
            ctx.roundedRect(0, 0, w, h, 2, 2)
            ctx.clip()
            if (!size || !isImageLoaded(src)) {
                ctx.fillStyle = fill
                ctx.fillRect(0, 0, w, h)
                return
            }
            ctx.drawImage(src, (w - size.width) / 2, (h - size.height) / 2)
            const m = /saturate\(([\d.]+)\)/.exec(look)
            if (!m)
                return
            const s = +m[1], d = ctx.getImageData(0, 0, w, h), p = d.data
            for (let i = 0; i < p.length; i += 4) {  // CSS's saturate(): towards the luminance of its matrix
                const l = 0.213 * p[i] + 0.715 * p[i + 1] + 0.072 * p[i + 2]
                for (let c = 0; c < 3; c++)
                    p[i + c] = l + s * (p[i + c] - l)
            }
            ctx.putImageData(d, 0, 0, 0, 0, w, h)  // Qt's Context2D does nothing given fewer than all seven
        }
    }
}
