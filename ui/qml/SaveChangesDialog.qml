import QtQuick

// Lưu thay đổi (docs/ui-tasks.md, U20; build/proto/Notices.dc.html, "Lưu khi đóng"), as Main opens it when what closes
// is not saved, a tab, Đóng tất cả or the window: { docs, done }. It asks about each of docs in turn, as Photoshop
// does: Lưu saves it (Docs.save, the background job), Không lưu leaves it; after the last, done() goes on closing.
// Hủy, Esc, a click outside stop there and nothing closes. Without docs (--state SaveChanges), those not saved.
DialogBox {
    id: dlg
    property var params: ({})
    property var docs: []  // read once: a save ending while it asks must not move the one asked about
    Component.onCompleted: docs = params.docs || Docs.docs.filter(d => d.dirty)
    property int at: 0  // the one asked about
    readonly property Doc doc: docs[at] || null
    // ponytail: the steps since it was opened (History's index): the mock keeps no step of the last save, N2 does.
    readonly property int steps: doc ? doc.index : 0
    readonly property int after: docs.length - at - 1  // asked about after this one

    header: null
    function next(save) {
        if (save && doc)
            Docs.save(doc)
        if (after > 0) {
            at++
            return
        }
        close()
        if (params.done)
            params.done()
    }

    contentItem: Column {
        topPadding: 20
        leftPadding: 18
        rightPadding: 18
        bottomPadding: 12
        spacing: 8
        Text {
            width: parent.width - 36
            text: qsTr("Lưu thay đổi của “%1”?").arg(dlg.doc ? dlg.doc.file : "")
            color: Theme.t1
            font.pointSize: Theme.pt(14)
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width - 36
            text: dlg.steps > 0 ? qsTr("Không lưu thì mất %1 bước chỉnh từ lần lưu trước.", "", dlg.steps).arg(dlg.steps)
                                : qsTr("Không lưu thì mất các thay đổi từ lần lưu trước.")
            color: Theme.t2
            font.pointSize: Theme.pt(12)
            wrapMode: Text.Wrap
        }
    }
    footer: Item {  // .df without its rule; Không lưu apart, at the left
        implicitHeight: 52
        Btn {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Không lưu")
            onClicked: dlg.next(false)
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Btn {
                text: qsTr("Hủy")
                onClicked: dlg.close()
            }
            Btn {
                width: Math.max(84, implicitWidth)
                primary: true
                text: qsTr("Lưu")
                onClicked: dlg.next(true)
            }
        }
    }
}
