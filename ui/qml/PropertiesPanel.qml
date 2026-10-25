pragma ComponentBehavior: Bound
import QtQuick
import "Mock.js" as Mock

// The Properties panel (docs/ui-tasks.md, U10; PanelsA, Adjustments, the pp, ad, mk parts of Main.dc.html), a tab of
// the dock's group Properties, Adjustments, Retouch: what the active layer is, the document without one. `page` names
// it, each page a file (U35, build/orch/api/u35.md), the panel giving it doc, active, cur: none (PropertiesNone); doc
// (PropertiesDocument: canvas size, resolution, orientation, mode, profile; Select Subject, Remove Background); px
// (PropertiesPixel, a pixel layer or a group: size, place, angle, flips, alignment); so (PropertiesSmartObject: its
// file, size, Sửa nội dung, Rasterize); text (PropertiesText: Character, Paragraph in short); shape (PropertiesShape:
// size, Fill, Stroke, corners, path operation); fill (PropertiesFill: its colour); adj (PropertiesAdjustment: its rows,
// AdjustRows; at the foot clip, view the previous state, reset, eye, delete); mask (PropertiesMask: the layer's mask
// while it takes the brush, Doc.editMask: Density, Feather, Select and Mask…, Color Range…, Invert; at the foot load as
// a selection, apply, disable, delete); what they share in PropertiesPage. What changes the document is one History
// step on Docs.current, a drag one too; a layer's size, place, angle are the prototype's own fields in Tools.values (its
// v: "L:<id>:w"…), the type's those of the Character and Paragraph panels (U13: "char:size", "para:align"…), no step, as
// the mock transforms nothing.
Item {
    id: panel
    property var menu: []  // the items of the … of its tab strip (PopupMenu's): none, as the prototype's
    readonly property Doc doc: Docs.current
    // Decision 4: what its pages change the core does not have yet, so a real document's are greyed (DocModel.can()).
    enabled: !doc || doc.supports("panel:Properties")
    readonly property string page: !doc ? "none" : !doc.active ? "doc" : doc.editMask && active.props.mask ? "mask"
                                 : ["adj", "text", "shape", "fill", "so"].indexOf(active.kind) >= 0 ? active.kind : "px"
    // The active layer and the document, stand-ins without them: a page's bindings may run once more as it goes.
    readonly property var active: doc && doc.active ? doc.active : ({ id: -1, kind: "", props: Mock.PROPS })
    readonly property var cur: doc || ({ w: 1, h: 1, ppi: 72, mode: "RGB", bits: 8, profile: "" })
    property bool peeking  // Giữ để xem trạng thái trước held, the adjustment page's: the mock shows the same image
    implicitWidth: 308
    implicitHeight: body.implicitHeight

    Loader {
        id: body
        width: parent.width
        sourceComponent: ({ none: noDoc, doc: docPage, px: pxPage, so: soPage, text: textPage, shape: shapePage, fill: fillPage, adj: adjPage, mask: maskPage })[panel.page]
    }

    // Each page's file, given what the panel has.
    Component { id: noDoc; PropertiesNone {} }
    Component { id: docPage; PropertiesDocument { doc: panel.doc; active: panel.active; cur: panel.cur } }
    Component { id: pxPage; PropertiesPixel { doc: panel.doc; active: panel.active; cur: panel.cur } }
    Component { id: soPage; PropertiesSmartObject { doc: panel.doc; active: panel.active; cur: panel.cur } }
    Component { id: textPage; PropertiesText { doc: panel.doc; active: panel.active; cur: panel.cur } }
    Component { id: shapePage; PropertiesShape { doc: panel.doc; active: panel.active; cur: panel.cur } }
    Component { id: fillPage; PropertiesFill { doc: panel.doc; active: panel.active; cur: panel.cur } }
    Component {
        id: adjPage
        PropertiesAdjustment {
            id: adjust
            doc: panel.doc
            active: panel.active
            cur: panel.cur
            onPeekingChanged: panel.peeking = adjust.peeking
        }
    }
    Component { id: maskPage; PropertiesMask { doc: panel.doc; active: panel.active; cur: panel.cur } }
}
