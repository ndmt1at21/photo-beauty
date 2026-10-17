// copied from snapone 6bdb5c8 (ui/qml/Spinner.qml)
import QtQuick

// Busy indicator of the prototype (.spin): the progress_activity glyph in `arc` colour, one
// turn a second.
Icon {
    id: sp
    property color arc: Theme.accent
    name: "progress_activity"
    size: 16
    color: arc
    RotationAnimator on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: sp.visible }
}
