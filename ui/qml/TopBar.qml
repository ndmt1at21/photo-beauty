pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// The top bar, 44 high (docs/ui-tasks.md, U4; artboard Main): logo and wordmark, the menus, the background job and, as
// the window's title bar on Windows (TitleBar, titlebar.cpp, as N12 of snapone), the window's buttons.
Rectangle {
    id: bar
    objectName: "titleBar"  // TitleBar.use(): the bar it makes the title bar
    // The window's title bar (TitleBar.use(), Windows): the window's buttons at the end; a press where no control is
    // moves the window.
    property bool titleBar: false
    property var can: function (item) { return true }  // Main's: whether a menu item can run now
    readonly property bool menuOpen: menus.open
    readonly property bool popupOpen: menus.open || job.popupOpen
    signal activated(var item)  // a menu item picked (MenuSpec.js)

    implicitWidth: 1440
    implicitHeight: 44
    color: Theme.bg
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }

    RowLayout {
        width: parent.width
        height: parent.height - 1  // above the line
        spacing: 0
        Row {  // a mark in the accent and the wordmark, as the prototype's header
            Layout.leftMargin: 12
            Layout.rightMargin: 14
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 5
                color: Theme.accent
                Rectangle { x: 4; y: 4; width: 7; height: 7; radius: 3.5; color: Theme.acct }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "photo-beauty"
                color: Theme.t1
                font.family: Theme.logo
                font.weight: Font.ExtraBold
                font.pointSize: Theme.pt(13.5)
                font.letterSpacing: -0.135  // -.01em
            }
        }
        AppMenu {
            id: menus
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 8  // the titles centred, their menus under them
            can: bar.can
            onActivated: item => bar.activated(item)
        }
        Item { Layout.fillWidth: true }
        JobIndicator { id: job; Layout.rightMargin: bar.titleBar ? 8 : 10 }
        Row {
            visible: bar.titleBar
            Layout.alignment: Qt.AlignTop
            WindowButton { text: qsTr("Thu nhỏ"); icon.name: "remove"; icon.width: 16; onClicked: bar.Window.window.showMinimized() }
            WindowButton {
                objectName: "maximize"  // TitleBar: the system's maximize button, for Snap Layouts
                readonly property bool maxed: bar.Window.window?.visibility === Window.Maximized
                text: maxed ? qsTr("Thu về") : qsTr("Phóng to")
                icon.name: "crop_square"
                icon.width: 15
                onClicked: maxed ? bar.Window.window.showNormal() : bar.Window.window.showMaximized()
            }
            WindowButton { text: qsTr("Đóng"); icon.name: "close"; icon.width: 17; danger: true; onClicked: bar.Window.window.close() }
        }
    }

    // Minimize, maximize, close: 46 wide as Windows' buttons, as tall as the bar, filled under the mouse, red to close.
    component WindowButton: AbstractButton {
        id: wb
        property bool danger: false
        property bool hot: false  // the mouse is the system's there (TitleBar, Snap Layouts)
        readonly property bool lit: hovered || hot
        implicitWidth: 46
        implicitHeight: 43
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: text
        background: Rectangle { color: !wb.lit ? "transparent" : wb.danger ? Theme.closeFill : Theme.sel }
        contentItem: Icon {
            name: wb.icon.name
            size: wb.icon.width
            color: wb.lit ? (wb.danger ? Theme.closeInk : Theme.t1) : Theme.t2
        }
    }
}
