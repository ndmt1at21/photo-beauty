// copied from snapone 6bdb5c8 (ui/qml/Icon.qml)
pragma ComponentBehavior: Bound
import QtQuick
import "Icons.js" as Icons
import "StrokeIcons.js" as StrokeIcons

// An icon by name: a Material Symbols glyph (names listed in ui/fonts/make-icons.sh) or one the
// prototype draws itself (StrokeIcons.js), drawn by a StrokeIcon over a blank glyph of the font
// so both kinds take the same room.
Text {
    id: ic
    property string name
    property int size: 20
    readonly property bool drawn: StrokeIcons.K[name] !== undefined
    text: Icons.cp[name] || (drawn ? "\u00a0" : "")
    font.family: Theme.icons
    font.pixelSize: size
    color: Theme.t2
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
    Loader {
        anchors.fill: parent
        active: ic.drawn
        sourceComponent: StrokeIcon { name: ic.name; size: ic.size; color: ic.color }
    }
}
