pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "AdjustSpec.js" as Spec

// The dialog of Image › Adjustments' "…" items (plan.md, Adjustment; the dlg part of Main.dc.html): params.adjust is
// MenuSpec's key ("curves"), the rows those of Properties (AdjustRows), then Preview; Hủy, OK at the foot as every
// DialogBox. The values live here until OK, one History step on Docs.current named after the adjustment, with them
// ({ op: "step", adjust, values: what differs from the defaults }); Hủy, Esc, × leave the document as it was. As
// decision 3 of docs/ui-tasks.md has it, the image does not follow the values, Preview or not.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property string name: Spec.nameOf(params.adjust || "")
    property var values: Spec.defaults(name)
    property bool preview: true

    title: name
    boxWidth: 340
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, disabled: !dlg.name || !dlg.doc, act: () => dlg.apply() }]

    function apply() {
        if (doc && name)
            doc.run({ op: "step", name: name, icon: Spec.ADJ[name].ic, adjust: params.adjust, values: Spec.changed(name, values) })
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        AdjustRows {
            Layout.fillWidth: true
            name: dlg.name
            values: dlg.values
            onMoved: v => dlg.values = v
        }
        CheckRow {
            text: qsTr("Preview")
            checked: dlg.preview
            onClicked: dlg.preview = !dlg.preview
        }
    }
}
