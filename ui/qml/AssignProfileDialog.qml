import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Edit › Assign Profile (build/proto/ColorSettings.dc.html): tags Docs.current with a profile and leaves its numbers:
// none (Không quản lý màu), the working space of Color Settings, or one picked. It opens on the document's own choice.
// OK is one History step (assignProfile; none when the profile stays); Hủy, Esc, × leave it. Preview only switches: the
// mock's image does not follow (decision 3 of docs/ui-tasks.md). Not Bound: SaveOptionsDialog makes its Radio.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property bool gray: !!doc && doc.mode === "Gray"
    // Color Settings' working spaces (Prefs keys it adds), the prototype's lists without them.
    readonly property string working: Prefs.value(gray ? "colorGray" : "colorRgb") ?? (gray ? "Gray Gamma 2.2" : "sRGB IEC61966-2.1")
    readonly property var profiles: (gray ? ["Gray Gamma 2.2", "Gray Gamma 1.8", "sGray", "Dot Gain 20%"]
                                          : ["sRGB IEC61966-2.1", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"]).filter(p => p !== working)
    readonly property string untagged: qsTr("Không quản lý màu")  // the profile of an untagged document, as New (U16) names it
    property string choice: !doc || doc.profile === untagged ? "none" : doc.profile === working ? "ws" : "prof"
    property string picked: doc && profiles.indexOf(doc.profile) >= 0 ? doc.profile : profiles[0]
    property bool preview: true
    readonly property string profile: choice === "none" ? untagged : choice === "ws" ? working : picked

    title: qsTr("Assign Profile")
    boxWidth: 460
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, disabled: !dlg.doc, act: () => dlg.apply() }]

    function apply() {
        if (doc)
            doc.run({ op: "assignProfile", profile: profile })
        close()
    }

    // A radio button, the prototype's <input type=radio>: `checked` comes from the owner, clicked() asks it to pick this one.
    component Radio: AbstractButton {
        id: r
        implicitWidth: implicitContentWidth
        implicitHeight: 20
        padding: 0
        hoverEnabled: true
        font.pointSize: Theme.pt(12)
        Accessible.role: Accessible.RadioButton
        Accessible.checkable: true
        Accessible.checked: checked
        contentItem: Row {
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 13
                height: 13
                radius: 6.5
                color: "transparent"
                border.width: r.checked ? 1.5 : 1
                border.color: !r.enabled ? Theme.t4 : r.checked ? Theme.accent : r.hovered ? Theme.t2 : Theme.t3
                Rectangle {
                    visible: r.checked
                    anchors.centerIn: parent
                    width: 7
                    height: 7
                    radius: 3.5
                    color: r.enabled ? Theme.accent : Theme.t4
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: r.text
                font: r.font
                color: r.enabled ? Theme.t2 : Theme.t4
            }
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: 10
        Radio {
            text: qsTr("Không quản lý màu cho tài liệu này")
            checked: dlg.choice === "none"
            onClicked: dlg.choice = "none"
        }
        Radio {
            text: qsTr("Không gian làm việc: %1").arg(dlg.working)
            checked: dlg.choice === "ws"
            onClicked: dlg.choice = "ws"
        }
        RowLayout {
            spacing: 10
            Radio {
                text: qsTr("Hồ sơ")
                checked: dlg.choice === "prof"
                onClicked: dlg.choice = "prof"
            }
            Select {
                Layout.fillWidth: true
                enabled: dlg.choice === "prof"
                options: dlg.profiles.map(p => [p, p])
                value: dlg.picked
                onPicked: v => dlg.picked = v
            }
        }
    }
    CheckRow {  // on the footer's left (after the buttons in Tab order)
        parent: dlg.footer
        x: 16
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Preview")
        checked: dlg.preview
        onClicked: dlg.preview = !dlg.preview
    }
}
