import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "FilterSpec.js" as Spec

// Edit › Fill (build/proto/FillStroke.dc.html, plan.md's Hộp thoại): what to fill with (a colour of Tools or one of the
// dialog's own, History, black, 50 % grey, white), Mode, Opacity, Preserve Transparency. OK is one History step on
// Docs.current, which the mock does not draw ({ op: "step" } with the choices); Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    property string contents: "fg"  // of Spec.CONTENTS
    property color custom: "#e9c7b3"  // Màu…
    property string mode: "normal"  // of Spec.MODES
    property real fillOpacity: 100  // %
    property bool preserve: false
    readonly property var fillColor: ({ fg: Tools.fg, bg: Tools.bg, color: custom.toString(), black: "#000000", gray: "#808080", white: "#ffffff" })[contents] ?? null

    title: qsTr("Fill")
    boxWidth: 420
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function apply() {
        if (doc)
            doc.run({ op: "step", name: qsTr("Fill"), icon: "format_color_fill", contents: contents, color: fillColor, mode: mode, opacity: fillOpacity / 100,
                      preserveTransparency: preserve })
        close()
    }

    // Shared with StrokeDialog and CanvasSizeDialog (FillDialog.ColorButton, FillDialog.Cap): the file is not Bound, as
    // inline components of a Bound one cannot be made in another.
    // A colour cell that opens the Color Picker (Main.openDialog, U11's dialog) on its colour; chosen(c) is the one picked.
    component ColorButton: Swatch {
        id: cb
        property string title
        signal chosen(color c)
        implicitHeight: 24
        radius: 5
        ring: Theme.t4
        ToolTip.text: title
        Accessible.name: title
        onClicked: (cb.Window.window as Main)?.openDialog("ColorPicker", { color: cb.color.toString(), title: cb.title, picked: c => cb.chosen(c) })
    }
    // A group's caption, the prototype's .cap.
    component Cap: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63
        font.capitalization: Font.AllUppercase
    }
    component RowLabel: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
        Layout.preferredWidth: 90
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        RowLayout {
            spacing: 10
            RowLabel { text: qsTr("Nội dung") }
            Dropdown {
                Layout.fillWidth: true
                options: Spec.CONTENTS
                value: dlg.contents
                onPicked: v => dlg.contents = v
            }
            ColorButton {
                visible: dlg.contents === "color"
                Layout.preferredWidth: 40
                color: dlg.custom
                title: qsTr("Màu tô")
                onChosen: c => dlg.custom = c
            }
        }
        Cap { text: qsTr("Hòa trộn") }
        RowLayout {
            spacing: 10
            RowLabel { text: qsTr("Mode") }
            Dropdown {
                Layout.fillWidth: true
                options: Spec.MODES
                value: dlg.mode
                onPicked: v => dlg.mode = v
            }
        }
        RowLayout {
            spacing: 10
            RowLabel { text: qsTr("Opacity") }
            NumberField {
                Layout.preferredWidth: 96
                value: dlg.fillOpacity
                unit: "%"
                min: 0
                max: 100
                onMoved: v => dlg.fillOpacity = v
            }
        }
        CheckRow {
            Layout.leftMargin: 100
            text: qsTr("Preserve Transparency")
            checked: dlg.preserve
            onClicked: dlg.preserve = !dlg.preserve
        }
    }
}
