import QtQuick
import QtQuick.Controls.Basic

// The Paragraph panel (docs/ui-tasks.md, U13; PanelsB.dc.html, the para part of Main.dc.html), beside the icon column
// with Character: the seven alignments, the left, right and first line indents, the space before and after a paragraph,
// in pt. Values in Tools: "para:align" (an index of PARA), "para:indentLeft", "para:indentRight", "para:indentFirst",
// "para:spaceBefore", "para:spaceAfter"; a key not set has the default of DEF. Field is the number box of this panel and
// Character's (no ComponentBehavior pragma here, so Character can make one).
Item {
    id: panel
    // The prototype's PARA: [icon, tip].
    readonly property var aligns: [["format_align_left", qsTr("Căn trái · Left Align Text")], ["format_align_center", qsTr("Căn giữa · Center Text")],
        ["format_align_right", qsTr("Căn phải · Right Align Text")], ["just_l", qsTr("Căn đều, dòng cuối căn trái · Justify Last Left")],
        ["just_c", qsTr("Căn đều, dòng cuối căn giữa · Justify Last Centered")], ["just_r", qsTr("Căn đều, dòng cuối căn phải · Justify Last Right")],
        ["just_a", qsTr("Căn đều mọi dòng · Justify All")]]
    readonly property var def: ({ "para:align": 0, "para:indentLeft": 0, "para:indentRight": 0, "para:indentFirst": 0, "para:spaceBefore": 0, "para:spaceAfter": 12 })
    property var menu: [{ l: qsTr("Đặt lại Paragraph"), act: () => panel.reset() }]
    implicitWidth: 280
    implicitHeight: col.implicitHeight

    function value(key) { return Tools.value(key, def[key]) }
    function reset() {  // Reset Paragraph: every key back to its default
        const o = {}
        for (const k in def)
            o[k] = null
        Tools.setValues(o)
    }

    Column {
        id: col
        width: parent.width
        topPadding: 8
        bottomPadding: 10
        leftPadding: 12
        rightPadding: 12
        spacing: 8
        Row {
            spacing: 2
            Repeater {
                model: panel.aligns
                ToolToggle {
                    required property var modelData
                    required property int index
                    icon.name: modelData[0]
                    icon.width: 16
                    checked: Tools.value("para:align", 0) === index  // the singleton: no ids of the file in here
                    ToolTip.text: modelData[1]
                    Accessible.name: modelData[1].split(" · ")[0]
                    Accessible.role: Accessible.RadioButton
                    onClicked: Tools.setValue("para:align", index)
                }
            }
        }
        Grid {
            id: grid
            readonly property real cell: (width - 6) / 2
            width: parent.width - 24
            columns: 2
            spacing: 6
            Field {
                width: grid.cell
                icon: "format_indent_increase"
                tip: qsTr("Thụt trái · Indent Left Margin")
                value: panel.value("para:indentLeft")
                unit: "pt"
                min: -1296
                max: 1296
                onMoved: v => Tools.setValue("para:indentLeft", v)
                onReset: Tools.setValue("para:indentLeft", null)
            }
            Field {
                width: grid.cell
                icon: "format_indent_increase"
                mirror: true
                tip: qsTr("Thụt phải · Indent Right Margin")
                value: panel.value("para:indentRight")
                unit: "pt"
                min: -1296
                max: 1296
                onMoved: v => Tools.setValue("para:indentRight", v)
                onReset: Tools.setValue("para:indentRight", null)
            }
            Field {
                width: grid.cell
                label: qsTr("Dòng đầu")
                tip: qsTr("Thụt dòng đầu · Indent First Line")
                value: panel.value("para:indentFirst")
                unit: "pt"
                min: -1296
                max: 1296
                onMoved: v => Tools.setValue("para:indentFirst", v)
                onReset: Tools.setValue("para:indentFirst", null)
            }
            Item { width: grid.cell; height: 24 }
            Field {
                width: grid.cell
                icon: "vertical_align_top"
                tip: qsTr("Khoảng trước đoạn · Add Space Before Paragraph")
                value: panel.value("para:spaceBefore")
                unit: "pt"
                min: -1296
                max: 1296
                onMoved: v => Tools.setValue("para:spaceBefore", v)
                onReset: Tools.setValue("para:spaceBefore", null)
            }
            Field {
                width: grid.cell
                icon: "vertical_align_bottom"
                tip: qsTr("Khoảng sau đoạn · Add Space After Paragraph")
                value: panel.value("para:spaceAfter")
                unit: "pt"
                min: -1296
                max: 1296
                onMoved: v => Tools.setValue("para:spaceAfter", v)
                onReset: Tools.setValue("para:spaceAfter", null)
            }
        }
    }

    // A number box of the type panels (.fld of PanelsB): an icon (mirror: flipped) or a short label, then the number in
    // `unit`; NaN shows `none` ("Auto") until a number is typed or dragged in, starting from `from`. Drag the icon or the
    // label to change it (Shift: 10 a pixel), double-click it to ask for reset(); the number box is NumberField's (a sum
    // or a unit typed, ↑ ↓). moved(v) at each change.
    component Field: Rectangle {
        id: fd
        property string icon
        property bool mirror
        property string label
        property bool bold  // the label's 600, as "VA" of the artboard
        property string tip
        property real value
        property string none
        property real from
        property string unit
        property int decimals
        property real min: -Infinity
        property real max: Infinity
        readonly property alias field: nf
        signal moved(real v)
        signal reset()

        implicitWidth: 120
        implicitHeight: 24
        radius: 4
        color: Theme.p2
        ToolTip.visible: head.containsMouse && !nf.scrubbing
        ToolTip.delay: 600
        ToolTip.text: tip

        Icon {
            visible: fd.icon !== ""
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            name: fd.icon
            size: 15
            color: Theme.t3
            transform: Scale { origin.x: 7.5; xScale: fd.mirror ? -1 : 1 }
        }
        Text {
            id: lbl
            visible: fd.icon === ""
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            text: fd.label
            color: nf.scrubbing ? Theme.t1 : Theme.t3
            font.pointSize: Theme.pt(11)
            font.weight: fd.bold ? Font.DemiBold : Font.Normal
        }
        MouseArea {  // the icon or the label, dragged as NumberField's own label
            id: head
            width: nf.x
            height: parent.height
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            preventStealing: true
            onPressed: e => nf.scrubStart(e.x)
            onPositionChanged: e => nf.scrubTo(e.x, e.modifiers & Qt.ShiftModifier)
            onReleased: nf.scrubEnd()
            onCanceled: nf.scrubEnd()
            onDoubleClicked: fd.reset()
        }
        NumberField {
            id: nf
            x: 6 + (fd.icon !== "" ? 15 : lbl.implicitWidth)
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            value: isNaN(fd.value) ? fd.from : fd.value
            unit: fd.unit
            decimals: fd.decimals
            min: fd.min
            max: fd.max
            Accessible.name: fd.tip.split(" · ")[0]
            onMoved: v => fd.moved(v)
        }
        Rectangle {  // NaN: `none` over the number
            visible: isNaN(fd.value) && !nf.editing && !nf.scrubbing
            x: nf.x + 2
            width: nf.width - 4
            height: 20
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.p2
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: fd.none
                color: fd.enabled ? Theme.t1 : Theme.t4
                font.family: Theme.mono
                font.pointSize: Theme.pt(11.5)
            }
        }
    }
}
