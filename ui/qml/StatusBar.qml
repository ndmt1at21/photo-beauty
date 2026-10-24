pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt
import "ToolSpec.js" as ToolSpec

// The status bar, 24 high (docs/ui-tasks.md, U4; artboard Main): the zoom to type ("12,5%", Enter or leaving the box
// sets it, Esc gives it back), the box Thông tin picks (Tools.statInfo), the document's size and resolution, its mode,
// depth and profile, RAM and scratch in use: the tile store's (Session.stats(), K1; N2) once a second on a real document
// or on this machine (Prefs.machine), RAM with its compressed part, against the budget; else the prototype's
// (Prefs.system).
Rectangle {
    id: sb
    readonly property Doc doc: Docs.current
    readonly property bool popupOpen: info.popup.visible
    readonly property var infos: [qsTr("Tài liệu"), qsTr("Hồ sơ màu"), qsTr("Kích thước"), qsTr("RAM, scratch"), qsTr("Thời gian thao tác"), qsTr("Công cụ đang dùng")]
    readonly property var system: Prefs.system
    readonly property bool live: Prefs.machine || !!doc && doc.real
    property var stats: Session.stats()  // bytes: raw, compressed, scratch, ram (what the budget counts), budget…
    function refresh() { stats = Session.stats() }
    Timer { interval: 1000; repeat: true; triggeredOnStart: true; running: sb.live && sb.visible; onTriggered: sb.refresh() }
    // Bytes as KB below 1 MB ("504 KB"), MB below 1 GB ("48 MB"), else GB with one decimal ("1,2 GB"); none "0 MB".
    function bytes(b) {
        const kb = (b || 0) / 1024, mb = kb / 1024
        return !b ? "0 MB" : kb < 1023.5 ? Fmt.vn(kb, 0) + " KB" : mb < 1023.95 ? Fmt.vn(mb, 0) + " MB" : Fmt.vn(mb / 1024, 1) + " GB"
    }
    readonly property string ramText: live ? qsTr("%1 / %2 · nén %3").arg(bytes(stats.ram)).arg(bytes(stats.budget)).arg(bytes(stats.compressed))
                                           : qsTr("%1 / %2 GB").arg(Fmt.vn(system.ramUsed, 1)).arg(Fmt.vn(system.ramGB * Prefs.value("ramBudget") / 100, 1))
    readonly property string scratchText: live ? bytes(stats.scratch) : qsTr("%1 GB").arg(Fmt.vn(system.scratchUsed, 1))
    readonly property string infoText: {
        const d = doc
        if (!d)
            return "—"
        switch (Tools.statInfo) {
        case 0: return d.mem || "—"
        case 1: return d.profile
        case 2: return qsTr("%1 × %2 cm").arg(Fmt.vn(d.w / d.ppi * 2.54, 2)).arg(Fmt.vn(d.h / d.ppi * 2.54, 2))
        case 3: return live ? bytes(stats.ram) + " · " + bytes(stats.scratch) : qsTr("%1 GB · %2 GB").arg(Fmt.vn(system.ramUsed, 1)).arg(Fmt.vn(system.scratchUsed, 1))
        case 4: return qsTr("%1 giây").arg(Fmt.vn(0.4, 1))
        default: return Tools.xf ? qsTr("Free Transform") : ToolSpec.name(Tools.tool)
        }
    }
    readonly property string zoomText: doc ? Fmt.zt(doc.view.zoom * 100) : ""

    implicitWidth: 1440
    implicitHeight: 24
    color: Theme.p1
    Rectangle { width: parent.width; height: 1; color: Theme.ln }

    RowLayout {
        y: 1
        width: parent.width
        height: parent.height - 1
        spacing: 0
        TextField {
            id: zoom
            Layout.preferredWidth: 66
            Layout.fillHeight: true
            leftPadding: 6
            rightPadding: 6
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            text: sb.zoomText
            color: Theme.t1
            selectionColor: Theme.accent
            selectedTextColor: Theme.acct
            selectByMouse: true
            font.family: Theme.mono
            font.pointSize: Theme.pt(11.5)
            Accessible.name: qsTr("Zoom, gõ được")
            background: Rectangle { color: Theme.p2; border.color: zoom.activeFocus ? Theme.t3 : "transparent" }
            onEditingFinished: apply()  // Enter, or leaving the box
            Keys.onEscapePressed: {
                revert()
                focus = false
            }
            function apply() {
                const x = Fmt.pnum(text)
                if (x && x > 0 && sb.doc)
                    sb.doc.zoomTo(x)
                revert()
            }
            function revert() { text = Qt.binding(() => sb.zoomText) }
        }
        Dropdown {
            id: info
            Layout.leftMargin: 6
            Layout.preferredWidth: 110
            Layout.fillHeight: true
            font.pointSize: Theme.pt(11)
            options: sb.infos.map((t, i) => [i, t])
            value: Tools.statInfo
            onPicked: v => Tools.statInfo = v
        }
        Mono { text: sb.infoText }
        Rule {}
        Mono { text: sb.doc ? qsTr("%1 × %2 px · %3 ppi").arg(Fmt.vn(sb.doc.w)).arg(Fmt.vn(sb.doc.h)).arg(Fmt.vn(sb.doc.ppi)) : "—" }
        Rule {}
        Plain { text: sb.doc ? (sb.doc.mode === "Gray" ? "Gray" : "RGB") + "/" + sb.doc.bits + " · " + sb.doc.profile : "" }
        Item { Layout.fillWidth: true }
        Row {
            leftPadding: 10
            rightPadding: 10
            spacing: 4
            Plain { text: qsTr("RAM"); leftPadding: 0; rightPadding: 0 }
            Mono { text: sb.ramText; leftPadding: 0; rightPadding: 0 }
        }
        Rule {}
        Row {
            leftPadding: 10
            rightPadding: 12
            spacing: 4
            Plain { text: qsTr("Scratch"); leftPadding: 0; rightPadding: 0 }
            Mono { text: sb.scratchText; leftPadding: 0; rightPadding: 0 }
        }
    }

    component Plain: Text {
        leftPadding: 10
        rightPadding: 10
        color: Theme.t3
        font.pointSize: Theme.pt(11)
        verticalAlignment: Text.AlignVCenter
    }
    component Mono: Plain {
        color: Theme.t2
        font.family: Theme.mono
    }
    component Rule: Rectangle {
        implicitWidth: 1
        implicitHeight: 14
        color: Theme.ln
    }
}
