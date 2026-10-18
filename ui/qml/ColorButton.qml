import QtQuick
import QtQuick.Controls.Basic

// A colour cell that opens the Color Picker (Main.openDialog, U11's dialog) on its colour; chosen(c) is the one picked.
// Fill, Stroke, Canvas Size.
Swatch {
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
