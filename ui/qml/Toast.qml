// copied from snapone 6bdb5c8 (the toast of ui/qml/Main.qml and App.toast() of ui/qml/App.qml)
import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic

// Toast of the prototype: a line at the bottom centre of the window, above dialogs too. `text`
// shows it ("" hides it); `ok` puts the accent check before it, `err` the error icon in warn;
// `act` ([label, function]) adds a button. done() asks the owner to clear `text`: `duration` ms
// went by or the button was pressed (its function runs right after). Same text again: restart().
Rectangle {
    id: t
    property string text
    property bool ok: false
    property bool err: false
    property var act: null
    property int duration: err || act ? 4500 : 2200
    signal done()
    function restart() { if (text !== "") timer.restart() }

    parent: Overlay.overlay
    z: 100
    visible: text !== ""
    x: (parent.width - width) / 2
    y: parent.height - height - 22
    width: line.implicitWidth + 28
    height: 36
    radius: 8
    color: Theme.p2
    onTextChanged: if (text !== "") timer.restart(); else timer.stop()
    Timer { id: timer; interval: t.duration; onTriggered: t.done() }
    RectangularShadow { z: -1; anchors.fill: parent; offset.y: 8; blur: 24; radius: 8; color: Theme.shadow }
    Row {
        id: line
        anchors.centerIn: parent
        spacing: 10
        Icon {
            visible: t.ok || t.err
            anchors.verticalCenter: parent.verticalCenter
            name: t.err ? "error" : "check"
            size: 16
            color: t.err ? Theme.warn : Theme.accent
        }
        Text { anchors.verticalCenter: parent.verticalCenter; text: t.text; color: Theme.t1; font.pointSize: Theme.pt(12) }
        Item { id: slot; visible: !!t.act; width: btn.implicitWidth; height: btn.implicitHeight }
    }
    // The button in a popup of its own over `slot`: the overlay keeps clicks from its plain items
    // while a modal dialog is open; the rest of the toast lets clicks through to what is under it.
    Popup {
        parent: t.parent
        z: 101
        visible: t.visible && !!t.act
        popupType: Popup.Item
        closePolicy: Popup.NoAutoClose
        focus: false
        padding: 0
        background: null
        x: t.x + line.x + slot.x
        y: t.y + line.y + slot.y
        contentItem: Btn {
            id: btn
            implicitHeight: 24
            leftPadding: 10
            rightPadding: 10
            primary: true
            text: t.act?.[0] ?? ""
            onClicked: { const a = t.act[1]; t.done(); a() }  // a() may raise the next toast
        }
    }
}
