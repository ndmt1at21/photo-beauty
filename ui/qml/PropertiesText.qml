pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec
import "ToolSpec.js" as ToolSpec

// The Properties panel's page of a type layer ("text", U35): Character and Paragraph in short, their values those
// of the Character and Paragraph panels (U13: "char:size", "para:align"…).
PropertiesPage {
    id: page
    objectName: "page:text"
    implicitHeight: tc.implicitHeight + 18
    ColumnLayout {
        id: tc
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        RowLayout {
            spacing: 8
            Layout.preferredHeight: 20
            Text {
                Layout.preferredWidth: 16
                horizontalAlignment: Text.AlignHCenter
                text: "T"
                color: Theme.t3
                font.weight: Font.DemiBold
                font.pointSize: Theme.pt(12)
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Layer chữ")
                color: Theme.t2
                font.pointSize: Theme.pt(12)
            }
        }
        PropertiesPage.Size { host: page }
        PropertiesPage.Cap {
            Layout.topMargin: 2
            text: qsTr("Character")
        }
        Dropdown {
            Layout.fillWidth: true
            implicitHeight: 26
            font.weight: Font.DemiBold
            options: ToolSpec.LISTS.font
            value: Tools.value("char:font", "Be Vietnam Pro")
            Accessible.name: qsTr("Font")
            onPicked: v => Tools.setValue("char:font", v)
        }
        GridLayout {
            columns: 2
            columnSpacing: 6
            rowSpacing: 6
            Dropdown {
                Layout.fillWidth: true
                Layout.preferredWidth: 100
                options: ToolSpec.LISTS.fstyle
                value: Tools.value("char:style", "SemiBold")
                Accessible.name: qsTr("Kiểu chữ")
                onPicked: v => Tools.setValue("char:style", v)
            }
            ParagraphPanel.Field {  // Character's fields, its values
                Layout.fillWidth: true
                Layout.preferredWidth: 100
                icon: "format_size"
                tip: qsTr("Cỡ chữ · Set the font size")
                value: Tools.value("char:size", 48)
                unit: "pt"
                min: 1
                max: 1296
                onMoved: v => Tools.setValue("char:size", v)
                onReset: Tools.setValue("char:size", null)
            }
            ParagraphPanel.Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 100
                icon: "format_line_spacing"
                tip: qsTr("Leading · Set the leading")
                value: Tools.value("char:leading", NaN)
                none: qsTr("Auto")
                from: Math.round(Tools.value("char:size", 48) * 1.2)  // Auto: 120 % of the size, as Photoshop's
                unit: "pt"
                min: 0
                max: 5000
                onMoved: v => Tools.setValue("char:leading", v)
                onReset: Tools.setValue("char:leading", null)
            }
            ParagraphPanel.Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 100
                icon: "format_letter_spacing"
                tip: qsTr("Tracking · Set the tracking for the selected characters")
                value: Tools.value("char:tracking", 0)
                min: -1000
                max: 10000
                onMoved: v => Tools.setValue("char:tracking", v)
                onReset: Tools.setValue("char:tracking", null)
            }
        }
        RowLayout {
            spacing: 8
            PropertiesPage.Dim { text: qsTr("Màu") }
            Swatch {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 22
                radius: 4
                ring: Theme.t4
                color: Tools.value("char:color", "#f4ede4")
                ToolTip.text: qsTr("Màu chữ · Set the text color")
                Accessible.name: qsTr("Màu chữ")
                onClicked: page.pickColor(color, qsTr("màu chữ"), c => Tools.setValue("char:color", String(c)))
            }
            Item { Layout.fillWidth: true }
            Dropdown {
                Layout.preferredWidth: 110
                options: Spec.LISTS.aa
                value: "aA " + Tools.antiAlias
                Accessible.name: qsTr("Khử răng cưa")
                onPicked: v => Tools.antiAlias = v.replace("aA ", "")
            }
        }
        PropertiesPage.Cap {
            Layout.topMargin: 2
            text: qsTr("Paragraph")
        }
        RowLayout {
            spacing: 2
            Repeater {
                model: [0, 1, 2, 6]
                ToolToggle {
                    id: pa
                    required property int modelData
                    icon.name: Spec.PARA[pa.modelData][0]
                    icon.width: 16
                    checked: Tools.value("para:align", 0) === pa.modelData
                    ToolTip.text: Spec.PARA[pa.modelData][1]
                    Accessible.name: Spec.PARA[pa.modelData][1].split(" · ")[0]
                    onClicked: Tools.setValue("para:align", pa.modelData)
                }
            }
            Item { Layout.fillWidth: true }
            Btn {
                implicitHeight: 24
                leftPadding: 8
                rightPadding: 8
                text: qsTr("Mở Character")
                onClicked: Tools.showPanel("Character")
            }
        }
    }
}
