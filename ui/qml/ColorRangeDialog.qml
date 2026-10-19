pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Mock.js" as Mock

// Select › Color Range…, also the mask page of Properties (U25, build/design2/project/Main.dc.html): what to select (a
// colour family, the tones, skin, or the colours the droppers sample), Detect Faces (Skin Tones only), Localized Color
// Clusters and its Range, Fuzziness; a 300 px preview of the document's photo as the selection's grey mask (white is
// selected; brighter by the pick, harder as Fuzziness falls, as the artboard's CSS filter) or as the image; Invert; the
// selection preview on the canvas (Xem trước trên ảnh, kept, the mock has no such view); three droppers, one at a time.
// OK selects on Docs.current as one History step "Color Range": the mock's shape for it (skin for Skin Tones, else the
// subject), inverted with Invert, the choices in the step's commands. Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    property string pick: "sampledColors"  // of PICKS
    property bool faces: false  // Detect Faces, taken with Skin Tones only
    property bool localized: false
    property real fuzziness: 40
    property real range: 100  // %, with Localized Color Clusters only
    property bool showImage: false  // the preview shows the image, not the selection
    property bool invert: false
    property string matte: "none"  // Xem trước trên ảnh, of MATTES
    property int dropper: 0  // Lấy mẫu, Thêm vào mẫu, Bớt khỏi mẫu
    // The artboard's PICKS and PREVIEWS (copied by a script), keyed in camelCase; LOOK: how bright a pick leaves the mask.
    readonly property var picks: [["sampledColors", "Sampled Colors"], ["reds", "Reds"], ["yellows", "Yellows"], ["greens", "Greens"], ["cyans", "Cyans"],
        ["blues", "Blues"], ["magentas", "Magentas"], ["highlights", "Highlights"], ["midtones", "Midtones"], ["shadows", "Shadows"], ["skinTones", "Skin Tones"],
        ["outOfGamut", "Out Of Gamut"]]
    readonly property var mattes: [["none", "None"], ["grayscale", "Grayscale"], ["blackMatte", "Black Matte"], ["whiteMatte", "White Matte"], ["quickMask", "Quick Mask"]]
    readonly property var look: ({ highlights: 1.6, midtones: 1.1, shadows: 0.55, skinTones: 1.35, sampledColors: 1.25 })
    readonly property bool skin: pick === "skinTones"

    title: qsTr("Color Range")
    boxWidth: 600
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    // The mask's grey for a pixel of luminance l (0–255), as the artboard's grayscale(1) brightness(b) contrast(c)
    // [invert(1)] on sRGB: b by the pick, c = 1 + (200 − Fuzziness) / 45.
    function level(l) {
        const b = look[pick] ?? 0.9, c = 1 + (200 - fuzziness) / 45
        const v = Math.min(1, Math.max(0, (Math.min(1, l / 255 * b) - 0.5) * c + 0.5))
        return Math.round((invert ? 1 - v : v) * 255)
    }
    function apply() {
        if (doc) {
            doc.beginGesture()  // the inverse joins the selection's step
            doc.run({ op: "select", shape: skin ? "skin" : "subject", name: qsTr("Color Range"), select: pick, fuzziness: fuzziness, localized: localized,
                      range: localized ? range : undefined, detectFaces: skin && faces, invert: invert })
            if (invert)
                doc.run({ op: "inverse" })
            doc.endGesture()
        }
        close()
    }

    component RowLabel: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
    }
    // A number field over a slider on one value (the artboard's grid of minmax(0, 1fr) 86px, 6 px under the field the
    // 3 px track), as the filters' rows.
    component SliderRow: Item {
        id: sr
        property string label
        property real value
        property real max
        property string unit
        signal moved(real v)
        Layout.fillWidth: true
        implicitHeight: 35
        RowLayout {
            width: parent.width
            spacing: 10
            RowLabel {
                Layout.fillWidth: true
                text: sr.label
            }
            NumberField {
                boxHeight: 26
                Layout.preferredWidth: 86
                unitApart: true
                horizontalAlignment: TextInput.AlignRight
                value: sr.value
                unit: sr.unit
                min: 0
                max: sr.max
                onMoved: v => sr.moved(v)
            }
        }
        SliderField {
            y: 32
            width: parent.width
            value: sr.value
            max: sr.max
            onMoved: v => sr.moved(v)
        }
    }

    Item {  // the body's padding is 14 at the top, DialogBox's 16
        width: parent.width
        implicitHeight: body.implicitHeight - 2
        RowLayout {
            id: body
            y: -2
            width: parent.width
            spacing: 14
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 12
                RowLayout {
                    spacing: 10
                    RowLabel {
                        Layout.preferredWidth: 70
                        text: qsTr("Chọn")
                    }
                    Dropdown {
                        implicitHeight: 26
                        Layout.fillWidth: true
                        options: dlg.picks
                        value: dlg.pick
                        onPicked: v => dlg.pick = v
                    }
                }
                Row {
                    Layout.leftMargin: 80
                    spacing: 18
                    CheckRow {
                        spacing: 8
                        enabled: dlg.skin
                        text: qsTr("Detect Faces")
                        checked: dlg.skin && dlg.faces
                        onClicked: dlg.faces = !dlg.faces
                    }
                    CheckRow {
                        spacing: 8
                        text: qsTr("Localized Color Clusters")
                        checked: dlg.localized
                        onClicked: dlg.localized = !dlg.localized
                    }
                }
                SliderRow {
                    label: qsTr("Fuzziness")
                    value: dlg.fuzziness
                    max: 200
                    onMoved: v => dlg.fuzziness = v
                }
                SliderRow {
                    opacity: enabled ? 1 : 0.4  // the artboard fades the row
                    enabled: dlg.localized
                    label: qsTr("Range")
                    value: dlg.range
                    max: 100
                    unit: "%"
                    onMoved: v => dlg.range = v
                }
                Rectangle {  // the preview, .pv
                    id: pv
                    readonly property url src: Mock.photoUrl(dlg.doc ? dlg.doc.photo : null)
                    Layout.fillWidth: true
                    implicitHeight: 300
                    radius: 6
                    color: Theme.view
                    clip: true
                    Image {
                        visible: dlg.showImage
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop  // object-fit: cover
                        source: pv.src
                    }
                    Canvas {  // the mask, worked out at half size (~300 px wide; the renderer offscreen draws no shader)
                        id: mask
                        readonly property var size: {  // the photo's size covering the canvas
                            const p = dlg.doc ? Mock.PHOTOS[dlg.doc.photo] : null
                            const s = p ? Math.max(width / p.w, height / p.h) : 0
                            return p && s ? Qt.size(Math.ceil(p.w * s), Math.ceil(p.h * s)) : null
                        }
                        readonly property var levels: {  // level() of each luminance, for the current choices
                            const t = []
                            for (let l = 0; l < 256; l++)
                                t.push(dlg.level(l))
                            return t
                        }
                        property var lum: null  // the photo's luminance, a byte a pixel
                        property var pixels: null  // the ImageData the mask is written into
                        property bool ready: false  // the mask drawn for the current levels
                        visible: !dlg.showImage
                        width: Math.round(pv.width / 2)
                        height: Math.round(pv.height / 2)
                        scale: 2
                        transformOrigin: Item.TopLeft
                        function fetch() {  // decoded at the size drawn; already in the cache, it is loaded at once
                            lum = null
                            ready = false
                            unloadImage(pv.src)  // Canvas keeps an image by its url: else the size laid out first stays
                            if (size)
                                loadImage(pv.src, size)
                            requestPaint()
                        }
                        onSizeChanged: fetch()
                        onImageLoaded: requestPaint()
                        onLevelsChanged: {
                            ready = false
                            requestPaint()
                        }
                        onPaint: {
                            const ctx = getContext("2d"), w = width, h = height
                            if (!lum) {
                                if (!size || !isImageLoaded(pv.src))
                                    return
                                ctx.reset()
                                ctx.drawImage(pv.src, (w - size.width) / 2, (h - size.height) / 2, size.width, size.height)
                                pixels = ctx.getImageData(0, 0, w, h)
                                const p = pixels.data, l = new Uint8Array(w * h)
                                for (let i = 0; i < l.length; i++)  // grayscale(1): Rec. 709 luma on sRGB
                                    l[i] = Math.round(0.2126 * p[4 * i] + 0.7152 * p[4 * i + 1] + 0.0722 * p[4 * i + 2])
                                lum = l
                            }
                            const p = pixels.data, t = levels
                            for (let i = 0; i < lum.length; i++) {
                                const v = t[lum[i]]
                                p[4 * i] = v
                                p[4 * i + 1] = v
                                p[4 * i + 2] = v
                            }
                            ctx.putImageData(pixels, 0, 0, 0, 0, w, h)  // Qt's Context2D does nothing given fewer than all seven
                            ready = true
                        }
                    }
                    Rectangle {  // rounds the corners: clip only cuts square ones
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: 10
                        color: "transparent"
                        border.width: 4
                        border.color: Theme.p1
                    }
                    MouseArea {
                        id: pvArea
                        anchors.fill: parent
                        hoverEnabled: true
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Nhấp lên ảnh để lấy mẫu màu")
                    }
                }
                RowLayout {
                    spacing: 18
                    Radio {
                        spacing: 8
                        text: qsTr("Selection")
                        checked: !dlg.showImage
                        onClicked: dlg.showImage = false
                    }
                    Radio {
                        spacing: 8
                        text: qsTr("Image")
                        checked: dlg.showImage
                        onClicked: dlg.showImage = true
                    }
                    Item { Layout.fillWidth: true }
                    CheckRow {
                        spacing: 8
                        text: qsTr("Invert")
                        checked: dlg.invert
                        onClicked: dlg.invert = !dlg.invert
                    }
                }
                RowLayout {
                    spacing: 10
                    RowLabel {
                        Layout.preferredWidth: 120
                        text: qsTr("Xem trước trên ảnh")
                    }
                    Dropdown {
                        implicitHeight: 26
                        Layout.fillWidth: true
                        options: dlg.mattes
                        value: dlg.matte
                        onPicked: v => dlg.matte = v
                    }
                }
            }
            Column {  // the droppers, a toolbar
                Layout.alignment: Qt.AlignTop
                topPadding: 2
                spacing: 6
                Repeater {
                    model: [[qsTr("Lấy mẫu màu"), qsTr("Lấy mẫu"), ""], [qsTr("Thêm vào mẫu"), qsTr("Thêm vào mẫu (Shift)"), "+"],
                        [qsTr("Bớt khỏi mẫu"), qsTr("Bớt khỏi mẫu (Alt)"), "−"]]
                    IconButton {
                        id: dr
                        required property var modelData
                        required property int index
                        implicitWidth: 30
                        implicitHeight: 30
                        icon.name: "colorize"
                        on: dlg.dropper === index
                        ToolTip.text: modelData[1]
                        Accessible.name: modelData[0]
                        Accessible.checkable: true
                        Accessible.checked: on
                        onClicked: dlg.dropper = index
                        Text {  // + or −, at the corner
                            x: dr.width - 3 - width
                            y: dr.height - 2 - height
                            height: 11
                            verticalAlignment: Text.AlignVCenter
                            text: dr.modelData[2]
                            color: (dr.contentItem as Icon).color  // the icon's, as it turns on, off, hovered
                            font.pointSize: Theme.pt(11)
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }
}
