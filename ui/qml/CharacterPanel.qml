import QtQuick
import QtQuick.Controls.Basic
import "ToolSpec.js" as ToolSpec

// The Character panel (docs/ui-tasks.md, U13; PanelsB.dc.html, the chr part of Main.dc.html), beside the icon column
// with Paragraph: font, style, size, kerning, leading, tracking, baseline shift, vertical and horizontal scale, colour,
// anti-aliasing, All Caps, underline, strikethrough. Values in Tools: "char:font", "char:style" (of ToolSpec.LISTS),
// "char:size", "char:leading" (pt; not set: Auto), "char:kerning" (thousandths of an em; not set: Metrics),
// "char:tracking", "char:baseline" (pt), "char:vscale", "char:hscale" (%), "char:color", "char:caps", "char:underline",
// "char:strike"; a key not set has the default of DEF. Anti-aliasing is Tools.antiAlias, as the Type bar's and menu's.
Item {
    id: panel
    readonly property var def: ({ "char:font": "Be Vietnam Pro", "char:style": "SemiBold", "char:size": 48, "char:leading": NaN, "char:kerning": NaN,
        "char:tracking": 0, "char:baseline": 0, "char:vscale": 100, "char:hscale": 100, "char:color": "#f4ede4", "char:caps": false, "char:underline": false,
        "char:strike": false })
    readonly property real size: value("char:size")
    property var menu: [{ l: qsTr("Đặt lại Character"), act: () => panel.reset() }]
    implicitWidth: 280
    implicitHeight: col.implicitHeight

    function value(key) { return Tools.value(key, def[key]) }
    function reset() {  // Reset Character: every key back to its default
        const o = {}
        for (const k in def)
            o[k] = null
        Tools.setValues(o)
    }
    function pickColor() {
        const w = panel.Window.window as Main
        if (w && w.hasDialog("ColorPicker"))
            w.openDialog("ColorPicker", { color: value("char:color"), title: qsTr("màu chữ"), picked: c => Tools.setValue("char:color", String(c)) })
    }

    Column {
        id: col
        readonly property real inner: width - 24
        width: parent.width
        topPadding: 8
        bottomPadding: 10
        leftPadding: 12
        rightPadding: 12
        spacing: 8
        Dropdown {
            width: col.inner
            height: 28
            font.pointSize: Theme.pt(13)
            font.weight: Font.DemiBold
            options: ToolSpec.LISTS.font
            value: panel.value("char:font")
            ToolTip.visible: hovered && !popup.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("Font · Set the font family")
            Accessible.name: qsTr("Font")
            onPicked: v => Tools.setValue("char:font", v)
        }
        Grid {
            id: grid
            readonly property real cell: (width - 6) / 2
            width: col.inner
            columns: 2
            spacing: 6
            Dropdown {
                width: grid.cell
                options: ToolSpec.LISTS.fstyle
                value: panel.value("char:style")
                ToolTip.visible: hovered && !popup.visible
                ToolTip.delay: 600
                ToolTip.text: qsTr("Kiểu chữ · Set the font style")
                Accessible.name: qsTr("Kiểu chữ")
                onPicked: v => Tools.setValue("char:style", v)
            }
            ParagraphPanel.Field {
                width: grid.cell
                icon: "format_size"
                tip: qsTr("Cỡ chữ · Set the font size")
                value: panel.size
                unit: "pt"
                min: 1
                max: 1296
                onMoved: v => Tools.setValue("char:size", v)
                onReset: Tools.setValue("char:size", null)
            }
            ParagraphPanel.Field {
                width: grid.cell
                label: "VA"
                bold: true
                tip: qsTr("Kerning · Set the kerning between two characters")
                value: panel.value("char:kerning")
                none: qsTr("Metrics")
                min: -1000
                max: 10000
                onMoved: v => Tools.setValue("char:kerning", v)
                onReset: Tools.setValue("char:kerning", null)
            }
            ParagraphPanel.Field {
                width: grid.cell
                icon: "format_line_spacing"
                tip: qsTr("Leading · Set the leading")
                value: panel.value("char:leading")
                none: qsTr("Auto")
                from: Math.round(panel.size * 1.2)  // Auto: 120 % of the size, as Photoshop's
                unit: "pt"
                min: 0
                max: 5000
                onMoved: v => Tools.setValue("char:leading", v)
                onReset: Tools.setValue("char:leading", null)
            }
            ParagraphPanel.Field {
                width: grid.cell
                icon: "format_letter_spacing"
                tip: qsTr("Tracking · Set the tracking for the selected characters")
                value: panel.value("char:tracking")
                min: -1000
                max: 10000
                onMoved: v => Tools.setValue("char:tracking", v)
                onReset: Tools.setValue("char:tracking", null)
            }
            ParagraphPanel.Field {
                width: grid.cell
                icon: "vertical_align_top"
                tip: qsTr("Baseline shift · Set the baseline shift")
                value: panel.value("char:baseline")
                unit: "pt"
                min: -1296
                max: 1296
                onMoved: v => Tools.setValue("char:baseline", v)
                onReset: Tools.setValue("char:baseline", null)
            }
            ParagraphPanel.Field {
                width: grid.cell
                icon: "height"
                tip: qsTr("Co giãn dọc · Vertically scale")
                value: panel.value("char:vscale")
                unit: "%"
                min: 0
                max: 1000
                onMoved: v => Tools.setValue("char:vscale", v)
                onReset: Tools.setValue("char:vscale", null)
            }
            ParagraphPanel.Field {
                width: grid.cell
                icon: "width"
                tip: qsTr("Co giãn ngang · Horizontally scale")
                value: panel.value("char:hscale")
                unit: "%"
                min: 0
                max: 1000
                onMoved: v => Tools.setValue("char:hscale", v)
                onReset: Tools.setValue("char:hscale", null)
            }
        }
        Item {
            width: col.inner
            height: 24
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Màu")
                color: Theme.t3
                font.pointSize: Theme.pt(12)
            }
            Swatch {
                x: 30
                anchors.verticalCenter: parent.verticalCenter
                width: 34
                height: 22
                radius: 4
                color: panel.value("char:color")
                ring: Theme.t4
                ToolTip.text: qsTr("Màu chữ · Set the text color")
                Accessible.name: qsTr("Màu chữ: %1").arg(String(panel.value("char:color")).toUpperCase())
                onClicked: panel.pickColor()
            }
            Dropdown {
                anchors.right: parent.right
                width: 110
                options: ["None", "Sharp", "Crisp", "Strong", "Smooth"].map(x => [x, "aA " + x])
                value: Tools.antiAlias
                ToolTip.visible: hovered && !popup.visible
                ToolTip.delay: 600
                ToolTip.text: qsTr("Khử răng cưa · Set the anti-aliasing method")
                Accessible.name: qsTr("Khử răng cưa")
                onPicked: v => Tools.antiAlias = v
            }
        }
        Row {
            spacing: 2
            Deco { key: "char:caps"; glyph: "TT"; tip: qsTr("Chữ hoa · All Caps") }
            Deco { key: "char:underline"; glyph: "T"; under: true; tip: qsTr("Gạch chân · Underline") }
            Deco { key: "char:strike"; glyph: "T"; strike: true; tip: qsTr("Gạch ngang · Strikethrough") }
        }
    }

    // A button of the artboard's .tb with letters in it: on when `key` is.
    component Deco: ToolToggle {
        id: st
        property string key
        property string glyph
        property bool under
        property bool strike
        property string tip
        checked: !!Tools.value(key, false)
        ToolTip.text: tip
        Accessible.name: tip.split(" · ")[0]
        Accessible.checkable: true
        Accessible.checked: checked
        onClicked: Tools.setValue(key, !checked)
        contentItem: Text {
            text: st.glyph
            font.pointSize: Theme.pt(st.glyph.length > 1 ? 11.5 : 12)
            font.weight: Font.DemiBold
            font.underline: st.under
            font.strikeout: st.strike
            color: !st.enabled ? Theme.t4 : st.checked ? Theme.accent : st.hovered ? Theme.t1 : Theme.t2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
