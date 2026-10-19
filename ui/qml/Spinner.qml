// copied from snapone 6bdb5c8 (ui/qml/Spinner.qml)
import QtQuick

// Busy indicator of the prototype (.spin): the progress_activity glyph in `arc` colour, one
// turn a second. Turned on the GUI thread: a RotationAnimator keeps the render thread drawing every
// vsync, which held the canvas's pointer events back (p95 27,5 against 11,8 ms) and without vsync
// ran at 639 fps (C3c, build/orch/api/c3c-vsync.md).
Icon {
    id: sp
    property color arc: Theme.accent
    name: "progress_activity"
    size: 16
    color: arc
    RotationAnimation on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: sp.visible }
}
