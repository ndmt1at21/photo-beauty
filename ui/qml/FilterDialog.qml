pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "FilterSpec.js" as Spec
import "Fmt.js" as Fmt
import "Mock.js" as Mock

// One dialog for every filter of the Filter menu's "…" items (build/proto/Filters.dc.html, plan.md's Hộp thoại):
// params.filter picks one of Spec.FILTERS, whose rows, ranges and defaults it shows. The frame of them all: a preview
// with − + zoom that shows the original while pressed, Preview, OK, Hủy. As decision 3 of docs/ui-tasks.md has it, the
// mock's preview does not follow the values. OK is one History step on Docs.current ({ op: "step" } named after the
// filter, with its values); Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property var spec: Spec.FILTERS[params.filter] || null
    property var edits: ({})  // the values changed here
    readonly property var values: Object.assign(Spec.defaults(spec), edits)
    property int zoom: Spec.ZOOM_100  // of Spec.ZOOMS
    property bool preview: true
    readonly property bool comparing: pvArea.pressed  // the original, while the preview is held

    title: spec ? spec.title : ""
    boxWidth: spec && spec.w ? spec.w : 400
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, disabled: !dlg.spec, act: () => dlg.apply() }]

    function set(key, v) { edits = Object.assign({}, edits, { [key]: v }) }
    function rows(kind) { return spec ? spec.rows.filter(r => r.kind === kind) : [] }
    function apply() {
        if (doc && spec)
            doc.run({ op: "step", name: spec.title, icon: "filter", filter: params.filter, values: values })
        close()
    }

    component Plain: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        Rectangle {  // the preview, the prototype's .pv: the photo at 480 × 600 scaled about the centre
            id: pv
            Layout.fillWidth: true
            implicitHeight: 220
            radius: 6
            color: Theme.view
            clip: true
            Image {
                x: -60
                y: -150
                width: 480
                height: 600
                fillMode: Image.PreserveAspectCrop
                source: Mock.photoUrl(dlg.doc ? dlg.doc.photo : null)
                transform: Scale {
                    origin.x: pv.width / 2 + 60
                    origin.y: 260
                    xScale: Spec.ZOOMS[dlg.zoom] / 100
                    yScale: xScale
                }
            }
            Rectangle {  // rounds the photo's corners: clip only cuts square ones
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
                ToolTip.visible: containsMouse && !pressed
                ToolTip.delay: 600
                ToolTip.text: qsTr("Nhấp giữ để xem ảnh gốc")
            }
        }
        RowLayout {
            spacing: 10
            Btn {
                implicitWidth: 24
                implicitHeight: 24
                leftPadding: 0
                rightPadding: 0
                text: "−"
                enabled: dlg.zoom > 0
                ToolTip.text: qsTr("Thu nhỏ · Zoom Out")
                Accessible.name: qsTr("Thu nhỏ")
                onClicked: dlg.zoom--
            }
            Text {
                text: Fmt.zt(Spec.ZOOMS[dlg.zoom])
                color: Theme.t2
                font.family: Theme.mono
                font.pointSize: Theme.pt(11.5)
            }
            Btn {
                implicitWidth: 24
                implicitHeight: 24
                leftPadding: 0
                rightPadding: 0
                text: "+"
                enabled: dlg.zoom < Spec.ZOOMS.length - 1
                ToolTip.text: qsTr("Phóng to · Zoom In")
                Accessible.name: qsTr("Phóng to")
                onClicked: dlg.zoom++
            }
            Item { Layout.fillWidth: true }
            CheckRow {
                text: qsTr("Preview")
                checked: dlg.preview
                onClicked: dlg.preview = !dlg.preview
            }
        }
        Repeater {  // a number field over a slider on one value; the row ends at the track, the knob hangs over it
            model: dlg.rows("slider")
            Item {
                id: sl
                required property var modelData
                readonly property real value: dlg.values[modelData.key]
                Layout.fillWidth: true
                implicitHeight: 35  // the field 26, 6, the 3 px track
                RowLayout {
                    width: parent.width
                    spacing: 10
                    Plain {
                        Layout.fillWidth: true
                        text: sl.modelData.label
                    }
                    NumberField {
                        boxHeight: 26
                        Layout.preferredWidth: 86
                        unitApart: true
                        horizontalAlignment: TextInput.AlignRight
                        value: sl.value
                        unit: sl.modelData.unit
                        decimals: sl.modelData.dec
                        step: Math.pow(10, -sl.modelData.dec)
                        min: sl.modelData.min
                        max: sl.modelData.max
                        onMoved: v => dlg.set(sl.modelData.key, v)
                    }
                }
                SliderField {
                    y: 32  // the track 6 px under the field
                    width: parent.width
                    value: sl.value
                    min: sl.modelData.min
                    max: sl.modelData.max
                    decimals: sl.modelData.dec
                    step: Math.pow(10, -sl.modelData.dec)
                    onMoved: v => dlg.set(sl.modelData.key, v)
                }
            }
        }
        Repeater {
            model: dlg.rows("select")
            RowLayout {
                id: se
                required property var modelData
                spacing: 10
                Plain {
                    Layout.fillWidth: true
                    text: se.modelData.label
                }
                Dropdown {
                    implicitHeight: 26
                    Layout.preferredWidth: 140
                    options: se.modelData.options
                    value: dlg.values[se.modelData.key]
                    onPicked: v => dlg.set(se.modelData.key, v)
                }
            }
        }
        Repeater {
            model: dlg.rows("radio")
            Column {
                id: ra
                required property var modelData
                spacing: 8
                Text {
                    text: ra.modelData.label
                    color: Theme.t3
                    font.pointSize: Theme.pt(11)
                }
                Repeater {
                    model: ra.modelData.options
                    Radio {
                        id: opt
                        required property var modelData
                        spacing: 8
                        text: opt.modelData[1]
                        checked: dlg.values[ra.modelData.key] === opt.modelData[0]
                        onClicked: dlg.set(ra.modelData.key, opt.modelData[0])
                    }
                }
            }
        }
        Repeater {
            model: dlg.rows("check")
            CheckRow {
                id: ch
                required property var modelData
                text: ch.modelData.label
                checked: !!dlg.values[ch.modelData.key]
                onClicked: dlg.set(ch.modelData.key, !dlg.values[ch.modelData.key])
            }
        }
        Repeater {  // Custom's 5 × 5 kernel, the pixel worked out in the middle, then Scale and Offset
            model: dlg.rows("kernel")
            Column {  // not a ColumnLayout: one whose rows do not fill cannot be wider than they are
                id: ke
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                Grid {
                    anchors.horizontalCenter: parent.horizontalCenter
                    topPadding: 6
                    bottomPadding: 6
                    columns: 5
                    spacing: 6
                    Repeater {
                        model: 25
                        NumberField {
                            id: cell
                            required property int index
                            boxHeight: 32
                            width: 56
                            horizontalAlignment: TextInput.AlignHCenter
                            value: dlg.values[ke.modelData.key][cell.index]
                            min: ke.modelData.min
                            max: ke.modelData.max
                            Accessible.name: qsTr("Hệ số hàng %1, cột %2").arg(Math.floor(cell.index / 5) + 1).arg(cell.index % 5 + 1)
                            onMoved: v => {
                                const k = dlg.values[ke.modelData.key].slice()
                                k[cell.index] = v
                                dlg.set(ke.modelData.key, k)
                            }
                        }
                    }
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 14
                    Repeater {
                        model: ke.modelData.fields
                        Row {
                            id: fi
                            required property var modelData
                            spacing: 8
                            Plain {
                                anchors.verticalCenter: parent.verticalCenter
                                text: fi.modelData.label
                            }
                            NumberField {
                                boxHeight: 26
                                width: 64
                                horizontalAlignment: TextInput.AlignRight
                                value: dlg.values[fi.modelData.key]
                                min: fi.modelData.min
                                max: fi.modelData.max
                                onMoved: v => dlg.set(fi.modelData.key, v)
                            }
                        }
                    }
                }
            }
        }
    }
}
