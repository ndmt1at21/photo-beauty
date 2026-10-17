import QtQuick

// What is drawn over the canvas (docs/ui-tasks.md: U6 places it, U7 fills it). Viewport lays it over the canvas, the
// frame's size, above its own pointer handling, and gives it what the canvas shows: a document point p is at
// View.toCanvas(view, size, p) in it, a point of it at View.toDoc(view, size, q) in the document.
Item {
    property Doc doc: null  // the document shown, null without one
    property var view: null  // Viewport.view: { x, y, zoom, angle }
    property var size: null  // Viewport.size: { w, h, dpr }, the frame
}
