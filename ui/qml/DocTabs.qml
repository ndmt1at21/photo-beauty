pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt

// The document tabs, 28 high (docs/ui-tasks.md, U6; artboard Main): "file @ zoom (layer, RGB/16)", "Quick Mask/16",
// "Layer Mask/16"; the unsaved dot turns into the close button under the pointer. As wide as their names up to 340, all
// shrunk alike when short of room (the prototype's flex), none under 140: those that do not fit go under "+N", a menu,
// the current one shown still. Right click: Đóng, Đóng tab khác.
Rectangle {
    id: bar
    readonly property int minTab: 140
    readonly property int plusWidth: 40
    // How many tabs show: all when each has minTab, else what fits beside +N.
    readonly property int fits: Docs.docs.length * minTab <= width ? Docs.docs.length : Math.max(1, Math.floor((width - plusWidth) / minTab))
    readonly property var shown: {  // the indices of Docs.docs with a tab
        const out = []
        for (let i = 0; i < Math.min(fits, Docs.docs.length); i++)
            out.push(i)
        if (Docs.index >= out.length && out.length)
            out[out.length - 1] = Docs.index
        return out
    }
    readonly property var hidden: Docs.docs.map((d, i) => i).filter(i => shown.indexOf(i) < 0)

    implicitWidth: 1052
    implicitHeight: 28
    color: Theme.bg

    // "chân dung.psd @ 12,5% (Dodge & Burn, RGB/16)": the active layer, or the mask or Quick Mask being edited.
    function label(d, current) {
        const a = d.active
        const inner = current && Tools.quickMask ? qsTr("Quick Mask/%1").arg(d.bits)
            : (a ? a.props.name + ", " : "") + (current && d.editMask ? qsTr("Layer Mask") : d.mode === "Gray" ? qsTr("Gray") : qsTr("RGB")) + "/" + d.bits
        return qsTr("%1 @ %2 (%3)").arg(d.file).arg(Fmt.zt(d.view.zoom * 100)).arg(inner)
    }
    function closeDoc(d) {  // as File › Đóng: Main asks about unsaved changes
        const w = bar.Window.window as Main
        if (w)
            w.closeDoc(d)
        else
            Docs.close(d)
    }
    function closeOthers(keep) {  // one SaveChanges for those unsaved, as Main's closeAll()
        const others = Docs.docs.filter(d => d !== keep), dirty = others.filter(d => d.dirty), w = bar.Window.window as Main
        const go = () => {
            for (const d of others)
                Docs.close(d)
            Docs.select(Docs.docs.indexOf(keep))
        }
        if (dirty.length && w && w.hasDialog("SaveChanges"))
            w.openDialog("SaveChanges", { docs: dirty, done: go })
        else
            go()
    }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
    RowLayout {
        width: parent.width
        height: parent.height - 1
        spacing: 0
        Repeater {
            model: Docs.docs
            delegate: AbstractButton {
                id: tab
                required property Doc modelData
                required property int index
                readonly property bool current: index === Docs.index
                readonly property real natural: Math.min(340, title.implicitWidth + 43)
                visible: bar.shown.indexOf(index) >= 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: natural
                Layout.maximumWidth: natural
                Layout.minimumWidth: Math.min(natural, bar.minTab)
                leftPadding: 12
                rightPadding: 6
                focusPolicy: Qt.TabFocus
                text: bar.label(modelData, current)
                Accessible.name: text
                onClicked: Docs.select(index)
                HoverHandler { id: hover }  // over the close button too
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: (point, button) => menu.show([{ l: qsTr("Đóng"), act: () => bar.closeDoc(tab.modelData) },
                        { l: qsTr("Đóng tab khác"), disabled: Docs.docs.length < 2, act: () => bar.closeOthers(tab.modelData) }], tab, point.position.x, point.position.y)
                }
                background: Rectangle {
                    color: tab.current ? Theme.p1 : "transparent"
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.ln }
                }
                contentItem: RowLayout {
                    spacing: 6
                    Text {
                        id: title
                        Layout.fillWidth: true
                        text: tab.text
                        elide: Text.ElideRight
                        color: tab.current ? Theme.t1 : Theme.t3
                        font.pointSize: Theme.pt(12)
                    }
                    Item {
                        implicitWidth: 18
                        implicitHeight: 18
                        Rectangle {  // unsaved
                            anchors.centerIn: parent
                            visible: tab.modelData.dirty && !hover.hovered
                            width: 6
                            height: 6
                            radius: 3
                            color: Theme.t2
                        }
                        AbstractButton {
                            id: close
                            anchors.fill: parent
                            visible: !tab.modelData.dirty || hover.hovered
                            hoverEnabled: true
                            focusPolicy: Qt.TabFocus
                            Accessible.name: qsTr("Đóng %1").arg(tab.modelData.file)
                            onClicked: bar.closeDoc(tab.modelData)
                            background: Rectangle { radius: 4; color: close.hovered ? Theme.sel : "transparent" }
                            contentItem: Icon { name: "close"; size: 14; color: Theme.t3 }
                        }
                    }
                }
            }
        }
        AbstractButton {
            id: more
            visible: bar.hidden.length > 0
            Layout.preferredWidth: bar.plusWidth
            Layout.fillHeight: true
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            text: "+" + bar.hidden.length
            Accessible.name: qsTr("%n tài liệu khác", "", bar.hidden.length)
            onClicked: menu.show(bar.hidden.map(i => ({ l: Docs.docs[i].file, act: () => Docs.select(i) })), more, 0, more.height)
            background: Rectangle { color: more.hovered || menu.visible ? Theme.sel : "transparent" }
            contentItem: Text {
                text: more.text
                color: Theme.t2
                font.pointSize: Theme.pt(12)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        Item { Layout.fillWidth: true }
    }
    PopupMenu {
        id: menu
        width: 220
        modal: true  // the press that closes it ends there, not on a tab or the canvas
        dim: false
    }
}
