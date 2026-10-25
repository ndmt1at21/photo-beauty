import QtQuick

// The Properties panel's page without a document ("none", U35).
Item {
    objectName: "page:none"
    implicitHeight: 44
    PropertiesPage.Dim {
        x: 12
        y: 14
        text: qsTr("Chưa mở tài liệu nào.")
    }
}
