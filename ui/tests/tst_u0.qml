import QtQuick
import QtTest
import PhotoBeauty

// U0: the window main.cpp opens, and the properties it starts it with; only what later tasks keep.
Item {
    id: root
    width: 200
    height: 100
    Component { id: mainC; Main {} }

    TestCase {
        name: "U0"
        when: windowShown

        function test_start() {  // U4 opens the state and the theme
            const w = createTemporaryObject(mainC, root, { startState: "Main", startTheme: "light" })
            compare(w.startState, "Main")
            compare(w.startTheme, "light")
            compare(w.persist, false)  // tests start from the defaults
        }

        function test_window() {  // 1440 × 900 on the dark theme's bg, rendered offscreen
            const w = createTemporaryObject(mainC, root)
            verify(Qt.colorEqual(w.color, "#161617"), w.color)
            waitForRendering(w.contentItem)
            const img = grabImage(w.contentItem)
            compare(img.width, 1440)
            compare(img.height, 900)
        }
    }
}
