.pragma library
// Mock documents (docs/ui-tasks.md, U3): the sample photos, documents and recent files of the prototype
// (build/proto/Main.dc.html, Start.dc.html), and the document model behind Doc.qml, ported from the prototype's class
// Component (mkL, docBase, mkPortrait, upd, step, restore, histTo, exec, the Layers, Channels and Retouch parts). A state
// is immutable: a command makes a new one, a History step keeps it with the commands that made it. Commands carry the
// names and JSON fields of T1's (core/doc.h), so N1's DocModel can stand in for a mock document. Plain JavaScript but
// for qsTr() and String.arg(), so node runs it with those two shimmed. Exported names are `var` and `function`; `const`
// stays inside.

// ---- sample photos ------------------------------------------------------------------------------------------------
// ui/mock/photos among the module's resources; the one place to change when the prefix does.
var PHOTO_URL = "qrc:/qt/qml/PhotoBeauty/mock/photos/";
// The prototype's PH, by index (its /_blob/ assets are these files); w, h of the file.
var PHOTOS = [
  { file: "chandung2.jpg", w: 1300, h: 1625 },
  { file: "chandung1.jpg", w: 1800, h: 1200 },
  { file: "halong.jpg", w: 1800, h: 1200 },
  { file: "saigon.jpg", w: 1800, h: 1201 },
  { file: "monan.jpg", w: 1800, h: 1200 },
  { file: "hoanghon.jpg", w: 1800, h: 1013 },
  { file: "trangan.jpg", w: 1800, h: 1200 },
  { file: "cauvang.jpg", w: 1800, h: 1125 }
];
function photoUrl(i) { return i === null || i === undefined || !PHOTOS[i] ? "" : PHOTO_URL + PHOTOS[i].file; }

// Recent files: the Start screen's list (newest first) with the editor's fields for opening one. `size` and `when` as
// it shows them; `mb` sorts by size, `order` by date modified (newer higher).
var RECENT = [
  { file: "chân dung.psd", photo: 0, w: 4400, h: 5600, ppi: 300, bits: 16, mem: "148 MB / 1,02 GB", size: "312 MB", mb: 312, when: qsTr("Hôm nay, 14:32"), order: 8 },
  { file: "poster-tet.pbd", photo: 3, w: 2480, h: 3508, ppi: 300, bits: 8, mem: "26,1 MB / 96 MB", size: "96 MB", mb: 96, when: qsTr("Hôm qua, 21:10"), order: 7 },
  { file: "san-pham.webp", photo: 4, w: 1800, h: 1200, ppi: 72, bits: 8, mem: "6,48 MB / 6,48 MB", size: "410 KB", mb: 0.41, when: qsTr("Hôm qua, 16:45"), order: 6 },
  { file: "halong-pano.psb", photo: 2, w: 36000, h: 24000, ppi: 300, bits: 16, mem: "5,18 GB / 9,4 GB", size: "3,4 GB", mb: 3400, when: "12/10/2026", order: 5 },
  { file: "cuoi-trang-an.psd", photo: 6, w: 8256, h: 5504, ppi: 300, bits: 16, mem: "273 MB / 1,6 GB", size: "1,6 GB", mb: 1600, when: "10/10/2026", order: 4 },
  { file: "chan-dung-ho.psd", photo: 1, w: 6000, h: 4000, ppi: 300, bits: 16, mem: "144 MB / 702 MB", size: "702 MB", mb: 702, when: "09/10/2026", order: 3 },
  { file: "hoang-hon.tif", photo: 5, w: 1800, h: 1013, ppi: 300, bits: 8, mem: "5,47 MB / 10 MB", size: "10 MB", mb: 10, when: "08/10/2026", order: 2 },
  { file: "cau-vang.jpg", photo: 7, w: 1800, h: 1125, ppi: 72, bits: 8, mem: "6,08 MB / 6,08 MB", size: "254 KB", mb: 0.25, when: "05/10/2026", order: 1 }
];
// Files left unsaved when the app last closed (Start, Notices).
var RECOVERY = [
  { file: "chân dung-final.psd", time: qsTr("tự lưu 14:38 hôm nay") },
  { file: "Chưa đặt tên-3", time: qsTr("tự lưu 14:29 hôm nay") }
];
// Retouch.dc.html's INIT: Main with these changes (Docs.reset, Tools.reset).
var RETOUCH_INIT = { docSel: "heal", fsClosed: true, state: { ptab: "Retouch", tool: "spot", rulers: false, min: { c: true, p: false, l: false }, job: { on: false, run: false, done: false, verb: "lưu", name: "", p: 0, sp: 1, doc: 0, wait: 0 } } };

// ---- tables of the prototype ------------------------------------------------------------------------------------------
var ZOOMS = [1, 2, 3, 4, 5, 6.25, 8.33, 12.5, 16.67, 25, 33.33, 50, 66.67, 100, 200, 300, 400, 600, 800, 1200, 1600, 2400, 3200, 6400, 12800];  // %
function fitZoom(w, h, vw, vh) { return Math.round(Math.min((vw - 40) / w, (vh - 40) / h) * 10000) / 100; }  // %, of a view vw × vh

// Selection shapes in units of the image (0..1), as the marching ants and Quick Mask draw them.
var SHAPES = {
  all: [["r", 0, 0, 1, 1]],
  skin: [["e", 0.576, 0.419, 0.187, 0.23]],
  ellip: [["e", 0.576, 0.419, 0.187, 0.23]],
  eyes: [["e", 0.5, 0.394, 0.047, 0.0136], ["e", 0.673, 0.394, 0.047, 0.0136]],
  lips: [["e", 0.585, 0.544, 0.073, 0.021]],
  teeth: [["e", 0.585, 0.547, 0.04, 0.008]],
  rect: [["r", 0.39, 0.19, 0.375, 0.46]],
  lasso: [["p", [[0.42, 0.22], [0.55, 0.16], [0.7, 0.2], [0.77, 0.33], [0.76, 0.5], [0.69, 0.62], [0.58, 0.66], [0.46, 0.6], [0.4, 0.46], [0.39, 0.32]]]],
  subject: [["p", [[0.3, 1], [0.32, 0.82], [0.4, 0.7], [0.44, 0.62], [0.4, 0.45], [0.4, 0.28], [0.47, 0.17], [0.58, 0.14], [0.7, 0.18], [0.76, 0.3], [0.76, 0.45], [0.71, 0.62], [0.75, 0.7], [0.86, 0.8], [0.9, 1]]]]
};
var ANTS_NAME = { all: qsTr("Chọn tất cả"), subject: qsTr("Select Subject"), skin: qsTr("Chọn da"), eyes: qsTr("Chọn mắt"), lips: qsTr("Chọn môi"), teeth: qsTr("Chọn răng"), rect: qsTr("Rectangular Marquee"), ellip: qsTr("Elliptical Marquee"), lasso: qsTr("Lasso") };
// Shape k on an image of W × H as SVG path data (the prototype's shapeD).
function shapePath(k, W, H) {
  const r = (v) => Math.round(v * 10) / 10;
  const ell = (cx, cy, rx, ry) => "M" + (cx - rx) + " " + cy + "a" + rx + " " + ry + " 0 1 0 " + 2 * rx + " 0a" + rx + " " + ry + " 0 1 0 " + (-2 * rx) + " 0Z";
  const rect = (x, y, w, h) => "M" + x + " " + y + "h" + w + "v" + h + "h" + (-w) + "Z";
  return (SHAPES[k] || []).map((p) => {
    if (p[0] === "r") return rect(r(p[1] * W) + 0.5, r(p[2] * H) + 0.5, r(p[3] * W) - 1, r(p[4] * H) - 1);
    if (p[0] === "e") return ell(r(p[1] * W), r(p[2] * H), r(p[3] * W), r(p[4] * H));
    return "M" + p[1].map((q) => r(q[0] * W) + " " + r(q[1] * H)).join("L") + "Z";
  }).join("");
}

// Blend modes: the JSON names of kBlendNames (core/doc.h), Pass Through first, and Photoshop's labels (the prototype's
// BLEND), the same order.
var BLENDS = ["passThrough", "normal", "dissolve", "darken", "multiply", "colorBurn", "linearBurn", "darkerColor", "lighten", "screen", "colorDodge", "linearDodge",
  "lighterColor", "overlay", "softLight", "hardLight", "vividLight", "linearLight", "pinLight", "hardMix", "difference", "exclusion", "subtract", "divide", "hue",
  "saturation", "color", "luminosity"];
var BLEND_LABELS = ["Pass Through", "Normal", "Dissolve", "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color", "Lighten", "Screen", "Color Dodge",
  "Linear Dodge (Add)", "Lighter Color", "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix", "Difference", "Exclusion",
  "Subtract", "Divide", "Hue", "Saturation", "Color", "Luminosity"];
// The 16 adjustments (the prototype's ADJ_ORDER) and their icons (ADJ's ic).
var ADJ_ICONS = {
  "Brightness/Contrast": "brightness_6", "Levels": "bar_chart", "Curves": "curve", "Exposure": "exposure", "Vibrance": "change_history", "Hue/Saturation": "looks",
  "Color Balance": "balance", "Black & White": "filter_b_and_w", "Photo Filter": "camera", "Channel Mixer": "join", "Color Lookup": "grid_on", "Invert": "invert_colors",
  "Posterize": "stairs", "Threshold": "contrast", "Gradient Map": "gradient", "Selective Color": "palette"
};
// The shape tools (GROUPS): name, icon.
var SHAPE_TOOLS = { rectS: ["Rectangle", "rectangle"], ellipS: ["Ellipse", "circle"], tri: ["Triangle", "change_history"], poly: ["Polygon", "pentagon"], line: ["Line", "pen_size_2"] };
// What a menu action needs (REQ): a key of cx(), '!' negating it; none, always.
var REQ = {
  close: "doc", closeAll: "doc", save: "doc", revert: "undo", place: "doc", quickExport: "doc", undo: "undo", redo: "redo", toggleLast: "hist", paste: "doc",
  xf: "layer", warpOn: "layer", rot: "layer", flip: "layer", mode: "doc", bits: "doc", dlg: "doc", invert: "doc", step: "doc", crot: "doc", cflip: "doc", cropSel: "sel", dupDoc: "doc",
  newLayer: "doc", newGroup: "doc", dupLayer: "layer", cutLayer: "sel", rename: "layer", delLayer: "multi", delHidden: "hidden", fill: "doc", adj: "doc", clip: "layer", toSO: "layer", rast: "layer",
  groupLayer: "layer", ungroup: "grp", hideLayer: "layer", arr: "layer", alignSel: "sel", lockAll: "layer", merge: "multi", selAll: "doc", deselect: "sel", reselect: "prev", inverse: "sel",
  ants: "doc", qm: "doc", loadSel: "doc", saveSel: "sel", zoomIn: "doc", zoomOut: "doc", zoomFit: "doc", zoom100: "doc", zoomFill: "doc", clearGuides: "doc", newGuide: "doc"
};
// The keys of cx().
var CX = ["doc", "sel", "prev", "undo", "redo", "hist", "layer", "multi", "hidden", "grp", "text", "shape", "so", "mask", "bgl", "below", "faces"];
// The portrait's History as it opens (the mock keeps no pixels, so every step has the state it opened with).
const H0 = [["Mở", "folder_open"], ["Tách tần số", "face_retouching_natural"], ["Spot Healing Brush", "spot_heal"], ["Spot Healing Brush", "spot_heal"], ["Liquify", "face"],
  ["Layer Curves mới", "curve"], ["Brush", "brush"], ["Brush", "brush"], ["Brush", "brush"], ["Brush", "brush"]];
// Masks the prototype draws, as CSS.
const MASK_SEL = "radial-gradient(ellipse 30% 26% at 57% 42%, #ffffff 0 70%, #000000 100%)";
const MASK_INV = "radial-gradient(ellipse 30% 26% at 57% 42%, #000000 0 70%, #ffffff 100%)";
const MASK_SUBJECT = "radial-gradient(ellipse 42% 47% at 57% 55%, #ffffff 0 80%, #000000 100%)";
const MASK_EYES = "radial-gradient(circle at 42% 40%, #ffffff 0 8%, transparent 13%), radial-gradient(circle at 66% 40%, #ffffff 0 8%, transparent 13%), #000000";

// The prototype's vn(): 1.234,5 and "−".
function vn(x, dec) {
  const s = Math.abs(x).toFixed(dec || 0).split(".");
  return (x < 0 && Math.abs(x) >= Math.pow(10, -(dec || 0)) / 2 ? "−" : "") + s[0].replace(/\B(?=(\d{3})+(?!\d))/g, ".") + (s[1] ? "," + s[1] : "");
}

// ---- layers -------------------------------------------------------------------------------------------------------
// A layer: { id, kind, props }, and a group's children, bottom first as T1's; the root is { id: 0, kind: "group",
// children }. Kinds: T1's pixel and group, and P1's adj (adjustment layer), fill, text, shape, so (smart object). Props:
// T1's LayerProps (opacity, fill 0..1; blend one of BLENDS), then the P1 keys T1 keeps in `extra`. Nodes are replaced,
// never changed, so states share what a command leaves alone.
var KINDS = ["pixel", "group", "adj", "fill", "text", "shape", "so"];
var PROPS = {
  name: "", visible: true, opacity: 1, fill: 1, blend: "normal", lockTransparency: false, lockPixels: false, lockPosition: false, lockAll: false,
  background: false,  // Photoshop's Background: at the bottom, does not move
  clip: false,        // clipped to the layer below
  mask: null,         // layer mask as the prototype's CSS: "#ffffff" reveals all, "#000000" hides all, else a gradient
  maskOff: false, maskDensity: 1, maskFeather: 0,  // density 0..1, feather px
  photo: null,        // the PHOTOS index a pixel layer or smart object shows
  thumb: null,        // a pixel layer's thumbnail when it has no photo: "empty", "gray" (50 % grey), "heal"
  look: null,         // the prototype's CSS filter on the thumbnail
  adj: null, vals: null,  // an adjustment layer's adjustment (a key of ADJ_ICONS) and the values it changed
  color: null,        // a fill layer's colour or CSS gradient, a shape layer's colour
  shape: null,        // a shape layer's tool: a key of SHAPE_TOOLS
  fx: null            // layer style (U18)
};

function bad(what) { return new Error("pb: " + what); }

function checked(p, kind) {
  if (BLENDS.indexOf(p.blend) < 0) throw bad("no blend mode " + p.blend);
  if (p.blend === "passThrough" && kind !== "group") throw bad("Pass Through is for groups");
  if (!(p.opacity >= 0 && p.opacity <= 1 && p.fill >= 0 && p.fill <= 1 && p.maskDensity >= 0 && p.maskDensity <= 1)) throw bad("opacity, fill, density go 0..1");
  if (typeof p.name !== "string" || typeof p.visible !== "boolean") throw bad("a name is text, visible true or false");
  return p;
}
function props(kind, o) { return checked(Object.assign({}, PROPS, o || {}), kind); }
function node(id, kind, p, children) {
  const n = { id: id, kind: kind, props: p };
  if (kind === "group") n.children = children || [];
  return n;
}

// Where layer id is under g: { node, parent, index }, null when not there.
// ponytail: a walk of the tree per lookup, fine at the mock's sizes; an index by id if a 1.000-layer document crawls.
function find(g, id) {
  if (!g.children) return null;
  for (let i = 0; i < g.children.length; i++) {
    const c = g.children[i];
    if (c.id === id) return { node: c, parent: g, index: i };
    const f = find(c, id);
    if (f) return f;
  }
  return null;
}
function need(s, id) {  // a layer, not the root
  const f = id ? find(s.root, id) : null;
  if (!f) throw bad("no layer " + id);
  return f;
}
function group(s, id) {  // a group, the root (0) too
  const g = id === 0 ? s.root : (find(s.root, id) || {}).node;
  if (!g || g.kind !== "group") throw bad("no group " + id);
  return g;
}

// The tree with edit applied to every node, children first: edit(n) gives undefined to keep n, else the nodes to put
// in its place. Nodes it leaves alone stay shared with the old tree (T1's rebuild()).
function rebuild(n, edit) {
  let now = n;
  if (n.children) {
    const kids = [];
    let changed = false;
    for (const c of n.children) {
      const r = rebuild(c, edit);
      if (r.length !== 1 || r[0] !== c) changed = true;
      for (const x of r) kids.push(x);
    }
    if (changed) now = Object.assign({}, n, { children: kids });
  }
  const e = edit(now);
  return e === undefined ? [now] : e;
}
function rebuilt(s, edit) { return rebuild(s.root, edit)[0]; }
function inserted(g, at, layers) {
  const k = g.children.slice();
  k.splice.apply(k, [at, 0].concat(layers));
  return Object.assign({}, g, { children: k });
}
// The tree with layer id's props patched; throws when that changes nothing.
function patched(s, id, patch) {
  const f = need(s, id);
  const p = checked(Object.assign({}, f.node.props, patch), f.node.kind);
  if (JSON.stringify(p) === JSON.stringify(f.node.props)) throw bad("no change");
  return rebuilt(s, (n) => (n.id === id ? [Object.assign({}, n, { props: p })] : undefined));
}

// The layers of ids, once each, but for those inside another of them, in the tree's order (bottom first, a group before
// what it holds), as T1's top_level(). Throws for the root or a layer not there.
function topLevel(s, ids) {
  const want = {};
  let n = 0;
  for (const id of ids) {
    if (!id) throw bad("the root is not a layer");
    if (!want[id]) n++;
    want[id] = true;
  }
  const out = [];
  let found = 0;
  const walk = (g, inside) => {
    for (const c of g.children) {
      const listed = !!want[c.id];
      if (listed) found++;
      if (listed && !inside) out.push(c);
      if (c.children) walk(c, inside || listed);
    }
  };
  walk(s.root, false);
  if (found !== n) throw bad("a layer not there");
  return out;
}

// Every layer, top first as the Layers panel lists them: { id, kind, props, depth, parent (0: the root), index (among
// its parent's children, 0 at the bottom), count (a group's children) }.
function flatten(root) {
  const out = [];
  const walk = (g, depth) => {
    for (let i = g.children.length - 1; i >= 0; i--) {
      const l = g.children[i];
      out.push({ id: l.id, kind: l.kind, props: l.props, depth: depth, parent: g.id, index: i, count: l.children ? l.children.length : 0 });
      if (l.children) walk(l, depth + 1);
    }
  };
  walk(root, 0);
  return out;
}
// The rows the Layers panel shows: layers not inside a collapsed group (collapsed: { id: true }).
function rowsOf(layers, collapsed) {
  const out = [];
  let skip = -1;  // depth under which rows are hidden
  for (const l of layers) {
    if (skip >= 0 && l.depth > skip) continue;
    skip = l.kind === "group" && collapsed[l.id] ? l.depth : -1;
    out.push(l);
  }
  return out;
}

// "base N" for the next N no layer has, as the prototype's nextName().
function nextName(s, base) {
  let n = 0;
  const re = new RegExp("^" + base.replace(/[.*+?^${}()|[\]\\/]/g, "\\$&") + " (\\d+)$");
  for (const l of flatten(s.root)) {
    const m = l.props.name.match(re);
    if (m) n = Math.max(n, +m[1]);
  }
  return base + " " + (n + 1);
}
function active(s) { return s.selected.length ? s.selected[s.selected.length - 1] : 0; }  // the layer Properties shows
function target(s, c) {  // the layer c names, else the active one
  const id = c.layer !== undefined ? c.layer : active(s);
  need(s, id);
  return id;
}
function put(s, o) { return Object.assign({}, s, o); }
// What a command gives: the state after it, the History step's name and icon, and the commands to keep for replaying it,
// with every target spelled out (`c`, one or a list).
function out(s, name, icon, c) { return { s: s, name: name, icon: icon, c: c }; }
function size(w, h) {
  if (!(Number.isInteger(w) && Number.isInteger(h) && w >= 1 && h >= 1 && w <= 300000 && h <= 300000)) throw bad("no size " + w + " × " + h);
}
function nextAlpha(s) {
  let n = s.alphas.length + 1;
  while (s.alphas.indexOf(qsTr("Alpha %1").arg(n)) >= 0) n++;
  return s.alphas.concat([qsTr("Alpha %1").arg(n)]);
}

// ---- commands -----------------------------------------------------------------------------------------------------
// By op. T1's eight take its JSON fields (layers by id; `index` among the parent's children, 0 at the bottom); where a
// field is left out, the command takes the selection, as the menus do. The rest are named after Photoshop's commands.
// Each throws, leaving the state alone, for what it cannot do; a command that would change nothing throws too.
const OPS = {
  // T1 ----
  addLayer(s, c) {
    const g = group(s, c.parent);
    if (!(Number.isInteger(c.index) && c.index >= 0 && c.index <= g.children.length)) throw bad("no index " + c.index);
    if (KINDS.indexOf(c.kind) < 0) throw bad("no kind " + c.kind);
    const l = node(s.nextId, c.kind, props(c.kind, c.props));
    const root = rebuilt(s, (n) => (n.id === c.parent ? [inserted(n, c.index, [l])] : undefined));
    const grp = c.kind === "group";
    return out(put(s, { root: root, nextId: s.nextId + 1, selected: [l.id] }), grp ? qsTr("Nhóm mới") : qsTr("Layer mới"), grp ? "folder" : "add_box", c);
  },
  // Selects the layer that takes the place of the topmost one gone, as the prototype (T1 leaves that to the UI).
  deleteLayers(s, c) {
    const top = topLevel(s, c.layers !== undefined ? c.layers : s.selected);
    if (!top.length) throw bad("no layers to delete");
    const gone = {};
    const mark = (l) => { gone[l.id] = true; if (l.children) l.children.forEach(mark); };
    top.forEach(mark);
    const i = flatten(s.root).findIndex((l) => gone[l.id]);
    const root = rebuilt(s, (n) => (gone[n.id] ? [] : undefined));
    const rest = flatten(root);
    return out(put(s, { root: root, selected: rest.length ? [rest[Math.min(i, rest.length - 1)].id] : [] }), qsTr("Xóa layer"), "delete",
      { op: "deleteLayers", layers: top.map((l) => l.id) });
  },
  // Each copy right above its layer, new ids before what they hold, named "… bản sao" and unlocked (T1 keeps the name).
  duplicateLayers(s, c) {
    const top = topLevel(s, c.layers !== undefined ? c.layers : s.selected);
    if (!top.length) throw bad("no layers to duplicate");
    const next = { id: s.nextId };
    const copy = (l) => node(next.id++, l.kind, l.props, l.children ? l.children.map(copy) : undefined);
    const copies = {};
    const sel = [];
    for (const l of top) {
      const k = copy(l);
      k.props = Object.assign({}, l.props, { name: l.props.name + qsTr(" bản sao"), background: false, lockTransparency: false, lockPixels: false, lockPosition: false, lockAll: false });
      copies[l.id] = k;
      sel.push(k.id);
    }
    const root = rebuilt(s, (n) => (copies[n.id] ? [n, copies[n.id]] : undefined));
    return out(put(s, { root: root, nextId: next.id, selected: sel }), qsTr("Nhân bản layer"), "content_copy", { op: "duplicateLayers", layers: top.map((l) => l.id) });
  },
  // `index` counts without the layer. Nothing goes below the Background, which stays.
  moveLayer(s, c) {
    const f = need(s, c.layer);
    const g = group(s, c.parent);
    if (c.parent === c.layer || find(f.node, c.parent)) throw bad("a group inside itself");
    if (f.node.props.background) throw bad("the Background stays where it is");
    const rest = g.children.filter((l) => l.id !== c.layer);
    if (!(Number.isInteger(c.index) && c.index >= 0 && c.index <= rest.length)) throw bad("no index " + c.index);
    if (c.index === 0 && rest.length && rest[0].props.background) throw bad("nothing goes below the Background");
    if (f.parent.id === c.parent && f.index === c.index) throw bad("no change");
    const root = rebuilt(s, (n) => (n.id === c.layer ? [] : n.id === c.parent ? [inserted(n, c.index, [f.node])] : undefined));
    return out(put(s, { root: root }), qsTr("Sắp xếp layer"), "drag_indicator", { op: "moveLayer", layer: c.layer, parent: c.parent, index: c.index });
  },
  // Into a new group where the topmost of them was, as T1's; props: the group's, "Group N" Pass Through when left out.
  groupLayers(s, c) {
    const top = topLevel(s, c.layers !== undefined ? c.layers : s.selected);
    if (!top.length) throw bad("no layers to group");
    if (top.some((l) => l.props.background)) throw bad("the Background stays where it is");
    const p = c.props !== undefined ? c.props : { name: nextName(s, qsTr("Group")), blend: "passThrough" };
    const g = node(s.nextId, "group", props("group", p), top);
    const moved = {};
    for (const l of top) moved[l.id] = true;
    const t = find(s.root, top[top.length - 1].id);
    let at = 0;  // the topmost's index among the layers that stay
    for (let i = 0; i < t.index; i++) if (!moved[t.parent.children[i].id]) at++;
    const root = rebuilt(s, (n) => (moved[n.id] ? [] : n.id === t.parent.id ? [inserted(n, at, [g])] : undefined));
    return out(put(s, { root: root, nextId: s.nextId + 1, selected: [g.id] }), qsTr("Nhóm layer"), "folder", { op: "groupLayers", layers: top.map((l) => l.id), props: p });
  },
  ungroup(s, c) {  // the group's children take its place, selected
    const id = c.group !== undefined ? c.group : active(s);
    const f = need(s, id);
    if (f.node.kind !== "group") throw bad("layer " + id + " is not a group");
    const root = rebuilt(s, (n) => (n.id === id ? n.children : undefined));
    return out(put(s, { root: root, selected: f.node.children.map((k) => k.id) }), qsTr("Bỏ nhóm"), "folder", { op: "ungroup", group: id });
  },
  // `props`: the keys to change (T1's SetProps takes them all: N1 fills in the rest from the layer). The step's name
  // follows the keys, as the prototype names them.
  setProps(s, c) {
    const id = target(s, c);
    const root = patched(s, id, c.props);
    const k = Object.keys(c.props || {});
    const only = (...keys) => k.length > 0 && k.every((x) => keys.indexOf(x) >= 0);
    const p = find(root, id).node.props;
    let name = qsTr("Thuộc tính layer"), icon = "tune";
    if (only("name")) { name = qsTr("Đổi tên layer"); icon = "edit"; }
    else if (only("visible")) { name = p.visible ? qsTr("Hiện layer") : qsTr("Ẩn layer"); icon = "visibility"; }
    else if (only("blend")) { name = qsTr("Blend mode"); }
    else if (only("opacity")) { name = qsTr("Opacity"); icon = "opacity"; }
    else if (only("fill")) { name = qsTr("Fill"); icon = "opacity"; }
    else if (only("lockTransparency", "lockPixels", "lockPosition", "lockAll")) { name = qsTr("Khóa layer"); icon = "lock"; }
    else if (only("clip")) { name = qsTr("Clipping Mask"); icon = "subdirectory_arrow_right"; }
    else if (only("vals")) { name = qsTr("Sửa %1").arg(p.name); icon = ADJ_ICONS[p.adj] || "tune"; }
    else if (only("maskDensity", "maskFeather")) { name = qsTr("Sửa Layer Mask"); icon = "mask"; }
    else if (only("fx")) { name = qsTr("Layer Style"); icon = "shadow"; }
    return out(put(s, { root: root }), name, icon, { op: "setProps", layer: id, props: c.props });
  },
  // Not a History step: Doc adds it to the step it is on.
  selectLayers(s, c) {
    const ids = [];
    for (const id of c.layers) {
      need(s, id);
      if (ids.indexOf(id) < 0) ids.push(id);
    }
    if (JSON.stringify(ids) === JSON.stringify(s.selected)) throw bad("no change");
    return { s: put(s, { selected: ids }), select: true, c: { op: "selectLayers", layers: ids } };
  },

  // New layers (Layer › Mới, the Layers panel, tools) ----
  newLayer(s) { return addAbove(s, "pixel", { name: nextName(s, qsTr("Layer")), thumb: "empty" }, qsTr("Layer mới"), "add_box"); },
  newGroup(s) { return addAbove(s, "group", { name: nextName(s, qsTr("Group")), blend: "passThrough" }, qsTr("Nhóm mới"), "folder"); },
  paste(s) { return addAbove(s, "pixel", { name: nextName(s, qsTr("Layer")), thumb: "empty" }, qsTr("Dán"), "content_copy"); },
  cutToLayer(s) {
    if (!s.selection) throw bad("no selection");
    return addAbove(s, "pixel", { name: nextName(s, qsTr("Layer")), thumb: "empty" }, qsTr("Cắt sang layer mới"), "content_copy");
  },
  placeEmbedded(s) { return addAbove(s, "so", { name: "cau-vang", photo: 7 }, qsTr("Place Embedded"), "deployed_code"); },
  newAdjustmentLayer(s, c) {  // adjustment: a key of ADJ_ICONS
    const a = c.adjustment;
    if (!ADJ_ICONS[a]) throw bad("no adjustment " + a);
    return addAbove(s, "adj", { name: nextName(s, a), adj: a, mask: "#ffffff", vals: {} }, qsTr("Layer %1 mới").arg(a), ADJ_ICONS[a]);
  },
  newFillLayer(s, c) {  // fill: "Solid Color" or "Gradient"; color: its colour or CSS gradient
    if (c.fill !== "Solid Color" && c.fill !== "Gradient") throw bad("no fill " + c.fill);
    const name = nextName(s, c.fill === "Gradient" ? qsTr("Gradient Fill") : qsTr("Color Fill"));
    return addAbove(s, "fill", { name: name, color: c.color || "#ffffff", mask: "#ffffff" }, qsTr("Layer %1 mới").arg(c.fill), "format_color_fill");
  },
  newTypeLayer(s) { return addAbove(s, "text", { name: qsTr("Chữ mẫu") }, qsTr("Horizontal Type"), "title"); },
  newShapeLayer(s, c) {  // shape: a key of SHAPE_TOOLS
    const t = SHAPE_TOOLS[c.shape];
    if (!t) throw bad("no shape " + c.shape);
    return addAbove(s, "shape", { name: nextName(s, t[0]), shape: c.shape, color: "#c0587e" }, t[0], t[1]);
  },

  // Layer › … on one layer (`layer`, else the active one) ----
  // action: reveal, hide (Hiện, Ẩn tất cả), sel, selInv (Hiện, Ẩn vùng chọn), del, apply, off (on again too), inv.
  mask(s, c) {
    const id = target(s, c);
    const l = need(s, id).node.props;
    const has = !!l.mask;
    const p = {
      reveal: { mask: "#ffffff" }, hide: { mask: "#000000" }, sel: { mask: MASK_SEL }, selInv: { mask: MASK_INV },
      del: has ? { mask: null, maskOff: false } : null, apply: has ? { mask: null, maskOff: false } : null, off: has ? { maskOff: !l.maskOff } : null,
      inv: has ? { mask: l.mask === "#ffffff" ? "#000000" : l.mask === "#000000" ? "#ffffff" : l.mask === MASK_SEL ? MASK_INV : MASK_SEL } : null
    }[c.action];
    if (p === undefined) throw bad("no mask action " + c.action);
    if (p === null) throw bad("layer " + id + " has no mask");
    const names = { reveal: qsTr("Thêm Layer Mask"), hide: qsTr("Thêm Layer Mask"), sel: qsTr("Mask từ vùng chọn"), selInv: qsTr("Mask từ vùng chọn"), del: qsTr("Xóa Layer Mask"),
      apply: qsTr("Áp Layer Mask"), off: qsTr("Tắt Layer Mask"), inv: qsTr("Đảo Layer Mask") };
    return out(put(s, { root: patched(s, id, p) }), names[c.action], "mask", { op: "mask", layer: id, action: c.action });
  },
  clippingMask(s, c) {  // Tạo Clipping Mask, and back
    const id = target(s, c);
    return out(put(s, { root: patched(s, id, { clip: !need(s, id).node.props.clip }) }), qsTr("Clipping Mask"), "subdirectory_arrow_right", { op: "clippingMask", layer: id });
  },
  toSmartObject(s, c) {  // a group's layers go inside it
    const id = target(s, c);
    const l = need(s, id).node;
    if (l.kind === "so") throw bad("layer " + id + " is a smart object");
    const p = checked(Object.assign({}, l.props, { photo: l.props.photo !== null ? l.props.photo : s.photo, blend: l.props.blend === "passThrough" ? "normal" : l.props.blend }), "so");
    const root = rebuilt(s, (n) => (n.id === id ? [node(id, "so", p)] : undefined));
    return out(put(s, { root: root }), qsTr("Chuyển thành Smart Object"), "deployed_code", { op: "toSmartObject", layer: id });
  },
  rasterize(s, c) {  // a text, shape, fill layer or smart object into a pixel layer
    const id = target(s, c);
    const l = need(s, id).node;
    if (["text", "shape", "so", "fill"].indexOf(l.kind) < 0) throw bad("layer " + id + " has nothing to rasterize");
    const p = Object.assign({}, l.props, l.props.photo === null ? { thumb: "empty" } : {});
    const root = rebuilt(s, (n) => (n.id === id ? [node(id, "pixel", p)] : undefined));
    return out(put(s, { root: root }), qsTr("Rasterize"), "image", { op: "rasterize", layer: id });
  },
  resetAdjustment(s, c) {  // Properties: Đặt lại
    const id = target(s, c);
    const l = need(s, id).node;
    if (l.kind !== "adj") throw bad("layer " + id + " is no adjustment layer");
    return out(put(s, { root: patched(s, id, { vals: {} }) }), qsTr("Đặt lại %1").arg(l.props.name), "restart_alt", { op: "resetAdjustment", layer: id });
  },
  removeBackground(s, c) {  // Properties: a mask on the subject; `layer`, else the Background
    const bg = flatten(s.root).find((l) => l.props.background);
    const id = c.layer !== undefined ? c.layer : bg ? bg.id : 0;
    return out(put(s, { root: patched(s, id, { mask: MASK_SUBJECT }) }), qsTr("Remove Background"), "person", { op: "removeBackground", layer: id });
  },
  // Layer › Sắp xếp: dir top, up, down, bottom among its group's layers.
  arrange(s, c) {
    const id = target(s, c);
    const f = need(s, id);
    const n = f.parent.children.length;
    const floor = f.parent.children[0].props.background ? 1 : 0;
    const to = { top: n - 1, up: Math.min(f.index + 1, n - 1), down: Math.max(f.index - 1, floor), bottom: floor }[c.dir];
    if (to === undefined) throw bad("no direction " + c.dir);
    return OPS.moveLayer(s, { op: "moveLayer", layer: id, parent: f.parent.id, index: to });
  },
  // Into the layer below it in its group, which gets selected; the mock has no pixels to merge.
  mergeDown(s, c) {
    const id = target(s, c);
    const f = need(s, id);
    if (f.node.kind === "group" || f.index === 0) throw bad("nothing below layer " + id + " to merge into");
    const r = OPS.deleteLayers(s, { op: "deleteLayers", layers: [id] });
    return out(r.s, qsTr("Gộp xuống"), "layers", r.c);
  },
  // The visible layers of the root into one where the lowest was, with its name; hidden ones stay.
  mergeVisible(s) {
    const kids = s.root.children;
    const vis = kids.filter((l) => l.props.visible);
    if (!vis.length) throw bad("no visible layers");
    const low = vis[0];
    let at = 0;
    for (let i = 0; kids[i] !== low; i++) at++;
    const del = OPS.deleteLayers(s, { op: "deleteLayers", layers: vis.map((l) => l.id) });
    const add = { op: "addLayer", parent: 0, index: at, kind: "pixel", props: { name: low.props.name, background: low.props.background, lockAll: low.props.lockAll, photo: s.photo } };
    return out(OPS.addLayer(del.s, add).s, qsTr("Gộp layer đang hiện"), "layers", [del.c, add]);
  },
  flattenImage(s) {
    if (!s.root.children.length) throw bad("no layers");
    const del = OPS.deleteLayers(s, { op: "deleteLayers", layers: s.root.children.map((l) => l.id) });
    const add = { op: "addLayer", parent: 0, index: 0, kind: "pixel", props: { name: qsTr("Background"), background: true, lockAll: true, photo: s.photo } };
    return out(OPS.addLayer(del.s, add).s, qsTr("Làm phẳng ảnh"), "layers", [del.c, add]);
  },
  deleteHidden(s) {
    const hidden = flatten(s.root).filter((l) => !l.props.visible).map((l) => l.id);
    if (!hidden.length) throw bad("no hidden layers");
    const r = OPS.deleteLayers(s, { op: "deleteLayers", layers: hidden });
    if (!r.s.root.children.length) throw bad("every layer hidden");
    return out(r.s, qsTr("Xóa layer đang ẩn"), "delete", r.c);
  },
  // Edit › Transform on the layer; the mock turns the image (layerRot, layerFlipH, layerFlipV).
  rotateLayer(s, c) {
    const id = target(s, c);
    if ([90, 180, 270].indexOf(c.angle) < 0) throw bad("no angle " + c.angle);
    return out(put(s, { layerRot: (s.layerRot + c.angle) % 360 }), c.angle === 180 ? qsTr("Xoay 180°") : qsTr("Xoay 90°"), "rotate_right", { op: "rotateLayer", layer: id, angle: c.angle });
  },
  flipLayer(s, c) {
    const id = target(s, c);
    if (c.axis !== "h" && c.axis !== "v") throw bad("no axis " + c.axis);
    return out(put(s, c.axis === "h" ? { layerFlipH: !s.layerFlipH } : { layerFlipV: !s.layerFlipV }), c.axis === "h" ? qsTr("Lật ngang") : qsTr("Lật dọc"), "flip",
      { op: "flipLayer", layer: id, axis: c.axis });
  },

  // The document (Image, Edit, Properties) ----
  rotateCanvas(s, c) {  // angle 90, 180, 270 clockwise
    if ([90, 180, 270].indexOf(c.angle) < 0) throw bad("no angle " + c.angle);
    const o = { rot: (s.rot + c.angle) % 360 };
    if (c.angle % 180) { o.w = s.h; o.h = s.w; }
    return out(put(s, o), qsTr("Image Rotation"), "rotate_right", c);
  },
  flipCanvas(s, c) {
    if (c.axis !== "h" && c.axis !== "v") throw bad("no axis " + c.axis);
    return out(put(s, c.axis === "h" ? { flipH: !s.flipH } : { flipV: !s.flipV }), c.axis === "h" ? qsTr("Lật canvas ngang") : qsTr("Lật canvas dọc"), "flip", c);
  },
  setMode(s, c) {  // mode "RGB" or "Gray", bits 8 or 16; either or both
    const mode = c.mode !== undefined ? c.mode : s.mode, bits = c.bits !== undefined ? c.bits : s.bits;
    if ((mode !== "RGB" && mode !== "Gray") || (bits !== 8 && bits !== 16)) throw bad("no mode " + mode + ", " + bits + " bits");
    if (mode === s.mode && bits === s.bits) throw bad("no change");
    const name = c.mode !== undefined && c.bits !== undefined ? qsTr("Mode") : c.mode !== undefined ? (mode === "Gray" ? qsTr("Grayscale") : qsTr("RGB Color")) : qsTr("%1 Bits/Channel").arg(bits);
    return out(put(s, { mode: mode, bits: bits }), name, "image", c);
  },
  swapOrientation(s, c) {
    if (s.w === s.h) throw bad("no change");
    return out(put(s, { w: s.h, h: s.w }), qsTr("Đổi hướng canvas"), "crop_rotate", c);
  },
  crop(s, c) {  // the Crop tool: w, h
    size(c.w, c.h);
    return out(put(s, { w: c.w, h: c.h }), qsTr("Crop"), "crop", c);
  },
  cropToSelection(s, c) {  // Image › Crop: to the selection's bounds (the mock's: the Rectangular Marquee's)
    if (!s.selection) throw bad("no selection");
    return out(put(s, { w: Math.round(s.w * 0.375), h: Math.round(s.h * 0.46), selection: null }), qsTr("Crop"), "crop", c);
  },
  imageSize(s, c) {  // w, h, ppi
    size(c.w, c.h);
    if (c.ppi !== undefined && !(c.ppi > 0)) throw bad("no resolution " + c.ppi);
    return out(put(s, { w: c.w, h: c.h, ppi: c.ppi !== undefined ? c.ppi : s.ppi }), qsTr("Image Size"), "image", c);
  },
  canvasSize(s, c) {  // w, h; anchor kept for the record
    size(c.w, c.h);
    return out(put(s, { w: c.w, h: c.h }), qsTr("Canvas Size"), "crop_square", c);
  },
  assignProfile(s, c) {
    if (!c.profile || c.profile === s.profile) throw bad("no profile, or the same");
    return out(put(s, { profile: c.profile }), qsTr("Assign Profile"), "palette", c);
  },
  convertProfile(s, c) {
    if (!c.profile || c.profile === s.profile) throw bad("no profile, or the same");
    return out(put(s, { profile: c.profile }), qsTr("Convert to Profile"), "palette", c);
  },
  // A step whose pixels the mock does not draw: a filter, Auto Tone, Invert, a stroke, Free Transform, Fill…; name, icon.
  step(s, c) {
    if (!c.name) throw bad("a step needs a name");
    return out(s, c.name, c.icon || "filter", c);
  },

  // Selection (Select, the selection tools) and channels ----
  select(s, c) {  // shape: a key of SHAPES; name: the step's (a tool's), else ANTS_NAME's
    if (!SHAPES[c.shape]) throw bad("no selection shape " + c.shape);
    return out(put(s, { prevSelection: s.selection || s.prevSelection, selection: c.shape, inverse: false }), c.name || ANTS_NAME[c.shape], "select", c);
  },
  deselect(s, c) {
    if (!s.selection) throw bad("no selection");
    return out(put(s, { prevSelection: s.selection, selection: null, inverse: false }), qsTr("Bỏ chọn"), "deselect", c);
  },
  reselect(s, c) {
    if (s.selection || !s.prevSelection) throw bad("nothing to select again");
    return out(put(s, { selection: s.prevSelection }), qsTr("Chọn lại"), "select", c);
  },
  inverse(s, c) {
    if (!s.selection) throw bad("no selection");
    return out(put(s, { inverse: !s.inverse }), qsTr("Đảo vùng chọn"), "select", c);
  },
  saveSelection(s, c) {  // into a new alpha channel
    if (!s.selection) throw bad("no selection");
    return out(put(s, { alphas: nextAlpha(s) }), qsTr("Lưu vùng chọn"), "save", c);
  },
  newChannel(s, c) { return out(put(s, { alphas: nextAlpha(s) }), qsTr("Kênh mới"), "add_box", c); },
  deleteChannel(s, c) {  // channel: an alpha channel's name
    if (s.alphas.indexOf(c.channel) < 0) throw bad("no alpha channel " + c.channel);
    return out(put(s, { alphas: s.alphas.filter((a) => a !== c.channel) }), qsTr("Xóa kênh"), "delete", c);
  },

  // Retouch (the panel's buttons, one step each) ----
  // A group of Cao (Linear Light) over Thấp above the Background; Tạo lại when there is one (the mock has no pixels).
  frequencySeparation(s, c) {
    const name = qsTr("Tách tần số");
    if (flatten(s.root).some((l) => l.kind === "group" && l.props.name === name)) return out(s, name, "face_retouching_natural", c);
    const b = s.root.children.findIndex((l) => l.props.background);
    const g = node(s.nextId, "group", props("group", { name: name, blend: "passThrough" }), [
      node(s.nextId + 2, "pixel", props("pixel", { name: qsTr("Thấp"), photo: s.photo, look: "blur(1.4px)" })),
      node(s.nextId + 1, "pixel", props("pixel", { name: qsTr("Cao"), photo: s.photo, look: "grayscale(1) contrast(0.16) brightness(1.25)", blend: "linearLight" }))
    ]);
    const root = rebuilt(s, (n) => (n.id === 0 ? [inserted(n, b >= 0 ? b + 1 : s.root.children.length, [g])] : undefined));
    return out(put(s, { root: root, nextId: s.nextId + 3, selected: [s.nextId + 1] }), name, "face_retouching_natural", c);
  },
  // Mask tự động: parts (skin, eyes, lips, teeth) to a selection (output 0) or to a mask on the active layer (1).
  autoMask(s, c) {
    const parts = (c.parts || []).filter((k) => SHAPES[k]);
    if (!parts.length || !s.faces) throw bad("no face, or no part of it");
    return c.output === 1 ? OPS.mask(s, { op: "mask", action: "sel" }) : OPS.select(s, { op: "select", shape: parts[0], name: qsTr("Mask tự động") });
  },
  smoothSkin(s) {
    return addAbove(s, "pixel", { name: nextName(s, qsTr("Làm mịn da")), photo: s.photo, look: "blur(0.8px)", mask: MASK_SEL }, qsTr("Làm mịn da"), "face_retouching_natural");
  },
  // method 0: a Curves pair, Burn and Dodge, with black masks; 1: a 50 % grey layer in Soft Light. One step (the
  // prototype makes the pair in two).
  dodgeBurn(s, c) {
    if (c.method === 1) return addAbove(s, "pixel", { name: nextName(s, qsTr("Dodge & Burn")), thumb: "gray", blend: "softLight" }, qsTr("Dodge & Burn"), "dodge");
    const a = addAbove(s, "adj", { name: qsTr("Burn"), adj: "Curves", mask: "#000000", vals: { pre: "Darker (RGB)" } });
    const b = addAbove(a.s, "adj", { name: qsTr("Dodge"), adj: "Curves", mask: "#000000", vals: { pre: "Lighter (RGB)" } });
    return out(b.s, qsTr("Dodge & Burn"), "dodge", [a.c, b.c]);
  },
  eyesTeeth(s) {
    return addAbove(s, "adj", { name: nextName(s, qsTr("Làm sáng mắt")), adj: "Curves", mask: MASK_EYES, vals: { pre: "Lighter (RGB)" } }, qsTr("Mắt, răng"), "face_retouching_natural");
  }
};

// A new layer above the active one, in its group; at the top without one (the prototype's addLayer()).
function addAbove(s, kind, p, name, icon) {
  const a = active(s);
  const f = a ? find(s.root, a) : null;
  const c = { op: "addLayer", parent: f ? f.parent.id : 0, index: f ? f.index + 1 : s.root.children.length, kind: kind, props: p };
  return out(OPS.addLayer(s, c).s, name, icon, c);
}

// The state after command c: { s, name, icon, c } (c: what to replay) or, for selectLayers, { s, select: true, c }.
// Throws for a command that cannot run.
function perform(s, c) {
  if (!c || !Object.prototype.hasOwnProperty.call(OPS, c.op)) throw bad("no command " + (c && c.op));
  return OPS[c.op](s, c);
}

// ---- documents ----------------------------------------------------------------------------------------------------
// What a History step keeps of a document: canvas, mode, profile, the layer tree and selected layers (T1's DocState,
// the active layer last), the selection (a key of SHAPES), the one before it, alpha channels, canvas and layer turns.
// photo and faces only say what the mock shows.
function docState(o) {
  return Object.assign({
    w: 1000, h: 1000, ppi: 72, mode: "RGB", bits: 8, profile: "sRGB IEC61966-2.1", photo: null, faces: false,
    root: { id: 0, kind: "group", children: [] }, selected: [], nextId: 1,
    selection: null, prevSelection: null, inverse: false, alphas: [],
    rot: 0, flipH: false, flipV: false, layerRot: 0, layerFlipH: false, layerFlipV: false
  }, o);
}

// The prototype's mkL() rows (top first, parent by id) as a tree, ids from 1 up from the bottom: { root, ids, next }.
function protoTree(rows) {
  const ids = {};
  let next = 1;
  for (let i = rows.length - 1; i >= 0; i--) ids[rows[i].id] = next++;
  const kids = (parent) => rows.filter((r) => (r.parent || null) === parent).reverse().map((r) => {
    const kind = r.kind || "pixel";
    return node(ids[r.id], kind, props(kind, {
      name: r.name, blend: BLENDS[BLEND_LABELS.indexOf(r.blend || "Normal")], adj: r.adj || null, vals: r.vals || null, mask: r.mask || null,
      thumb: r.thumb || null, photo: r.ph !== undefined ? r.ph : null, look: r.f || null, lockAll: r.lock === "a", background: !!r.bgl
    }), kind === "group" ? kids(r.id) : undefined);
  });
  return { root: { id: 0, kind: "group", children: kids(null) }, ids: ids, next: next };
}

// Each sample below is what makeDoc() takes: { file, mem, dirty, zoom (%) or view, state, history [{ name, icon }],
// snapshots [{ name, at, look }] }.
// chân dung.psd, as the prototype's mkPortrait(); ids: its layers' ids by the prototype's (c1, teeth, eyes, db, fs, hi,
// lo, heal, bg).
function portrait() {
  const t = protoTree([
    { id: "c1", name: "Curves 1", kind: "adj", adj: "Curves", mask: "#ffffff", vals: { pre: "Medium Contrast (RGB)" } },
    { id: "teeth", name: "Làm trắng răng", kind: "adj", adj: "Hue/Saturation", mask: "radial-gradient(ellipse 24% 13% at 55% 68%, #ffffff 0 55%, #000000 100%)", vals: { rng: "Yellows", s: -30, l: 10 } },
    { id: "eyes", name: "Làm sáng mắt", kind: "adj", adj: "Curves", mask: MASK_EYES, vals: { pre: "Lighter (RGB)" } },
    { id: "db", name: "Dodge & Burn", thumb: "gray", blend: "Soft Light" },
    { id: "fs", name: "Tách tần số", kind: "group", blend: "Pass Through" },
    { id: "hi", name: "Cao", parent: "fs", ph: 0, f: "grayscale(1) contrast(0.16) brightness(1.25)", blend: "Linear Light" },
    { id: "lo", name: "Thấp", parent: "fs", ph: 0, f: "blur(1.4px)" },
    { id: "heal", name: "Heal", thumb: "heal" },
    { id: "bg", name: "Background", ph: 0, lock: "a", bgl: true }
  ]);
  return {
    file: "chân dung.psd", mem: "148 MB / 1,02 GB", dirty: true, zoom: 12.5, ids: t.ids,
    state: docState({ w: 4400, h: 5600, ppi: 300, bits: 16, photo: 0, faces: true, root: t.root, nextId: t.next, selected: [t.ids.db], alphas: ["Vùng chọn da"] }),
    history: H0.map((h) => ({ name: h[0], icon: h[1] })),
    snapshots: [{ name: "chân dung.psd", at: 0, look: "none" }, { name: "Trước khi Liquify", at: 3, look: "saturate(.8)" }]
  };
}
// A file opened (r: a RECENT entry, or one like it): its Background, fitted to a view of vw × vh (the prototype's mkDoc()).
function opened(r, vw, vh) {
  const t = protoTree([{ id: "bg", name: "Background", ph: r.photo, lock: "a", bgl: true }]);
  return {
    file: r.file, mem: r.mem, dirty: false, zoom: fitZoom(r.w, r.h, vw, vh),
    state: docState({ w: r.w, h: r.h, ppi: r.ppi, bits: r.bits, photo: r.photo, root: t.root, nextId: t.next, selected: [t.ids.bg] }),
    history: [{ name: qsTr("Mở"), icon: "folder_open" }], snapshots: [{ name: r.file, at: 0, look: "none" }]
  };
}
// A file not in RECENT, as a drop or recovery opens it: the portrait's photo at its size.
function guess(file) {
  const p = PHOTOS[0];
  return { file: file, photo: 0, w: p.w, h: p.h, ppi: 72, bits: 8, mem: vn(p.w * p.h * 3 / 1e6, 1) + " MB / " + vn(p.w * p.h * 3 / 1e6, 1) + " MB" };
}
// Tạo mới (the New dialog): { name, w, h (px), ppi, mode "RGB" | "Gray", bits 8 | 16, background "white" | "black" |
// "transparent" | "custom" (color: "#rrggbb"; Màu nền sau is a custom one), profile }.
function created(o, vw, vh) {
  size(o.w, o.h);
  const mode = o.mode || "RGB", bits = o.bits || 8;
  if ((mode !== "RGB" && mode !== "Gray") || (bits !== 8 && bits !== 16) || !(o.ppi === undefined || o.ppi > 0)) throw bad("no mode, depth or resolution");
  const bg = o.background || "white";
  const color = bg === "white" ? "#ffffff" : bg === "black" ? "#000000" : o.color || "#ffffff";
  const layer = bg === "transparent" ? { name: qsTr("Layer 1"), thumb: "empty" } : { name: qsTr("Background"), background: true, lockAll: true, color: color };
  const mb = o.w * o.h * (mode === "Gray" ? 1 : 3) * bits / 8 / 1e6;
  const file = o.name || qsTr("Chưa đặt tên-1");
  return {
    file: file, mem: vn(mb, mb >= 100 ? 0 : 1) + " MB / " + vn(mb, mb >= 100 ? 0 : 1) + " MB", dirty: false, zoom: fitZoom(o.w, o.h, vw, vh),
    state: docState({ w: o.w, h: o.h, ppi: o.ppi || 72, mode: mode, bits: bits, profile: o.profile || (mode === "Gray" ? "Gray Gamma 2.2" : "sRGB IEC61966-2.1"),
      root: { id: 0, kind: "group", children: [node(1, "pixel", props("pixel", layer))] }, nextId: 2, selected: [1] }),
    history: [{ name: qsTr("Tạo mới"), icon: "add_box" }], snapshots: [{ name: file, at: 0, look: "none" }]
  };
}
// Image › Nhân bản and History's new document: d's state as a new file.
function duplicated(d) {
  return {
    file: /\.[^.]+$/.test(d.file) ? d.file.replace(/(\.[^.]+)$/, qsTr(" bản sao") + "$1") : d.file + qsTr(" bản sao"), mem: d.mem, dirty: true, view: d.view, state: d.state,
    history: [{ name: qsTr("Nhân bản"), icon: "content_copy" }], snapshots: [{ name: qsTr("Bản sao"), at: 0, look: "none" }]
  };
}

// What the menus test (the prototype's cx), for document d (null: none open).
function cx(d) {
  if (!d) return {};
  const L = d.layers, a = d.active, i = a ? L.indexOf(a) : -1;
  return {
    doc: true, sel: !!d.selection, prev: !!d.prevSelection && !d.selection, undo: d.index > 0, redo: d.index < d.history.length - 1, hist: d.history.length > 1,
    layer: !!a, multi: L.length > 1, hidden: L.some((l) => !l.props.visible) && L.some((l) => l.props.visible),
    grp: !!a && a.kind === "group", text: !!a && a.kind === "text", shape: !!a && a.kind === "shape", so: !!a && a.kind === "so",
    mask: !!a && !!a.props.mask, bgl: !!a && a.props.background, below: i >= 0 && i < L.length - 1, faces: !!d.faces
  };
}
// Whether `name` can run on d: a key of CX ('!' negates it), or a menu action ("delLayer", "@merge:down") by its REQ.
function can(d, name) {
  let k = String(name).replace(/^@/, "").split(":")[0];
  if (CX.indexOf(k.replace(/^!/, "")) < 0) {
    k = REQ[k];
    if (!k) return true;
  }
  const c = cx(d);
  return k[0] === "!" ? !c[k.slice(1)] : !!c[k];
}

// What Doc.qml shows of a document, each replaced when it changes (Doc's sync() tests them with !==).
var FIELDS = ["file", "mem", "dirty", "view", "state", "tree", "layers", "rows", "active", "selected", "editMask", "collapsed", "w", "h", "ppi", "mode", "bits",
  "profile", "photo", "faces", "selection", "prevSelection", "inverse", "alphas", "channel", "hiddenChannels", "rot", "flipH", "flipV", "layerRot", "layerFlipH",
  "layerFlipV", "history", "index", "snapshots", "snapshot"];

// The mock document behind Doc.qml (o as portrait() gives): its state and History, each step { name, icon, cmds, state }
// with the commands that made it from the step before, and what History does not keep (view, collapsed groups, mask
// editing, the channel shown, hidden channels, unsaved).
function makeDoc(o) {
  const s0 = o.state;
  const d = {
    file: o.file, mem: o.mem || "", dirty: !!o.dirty, state: s0,
    view: o.view || { x: s0.w / 2, y: s0.h / 2, zoom: (o.zoom || 100) / 100, angle: 0 },
    history: (o.history || [{ name: qsTr("Mở"), icon: "folder_open" }]).map((h) => ({ name: h.name, icon: h.icon, cmds: [], state: s0 })),
    snapshots: (o.snapshots || []).map((x) => ({ name: x.name, at: x.at, look: x.look || "none", state: s0 })),
    snapshot: -1, collapsed: {}, editMask: false, maskOf: 0, channel: "RGB", hiddenChannels: {}, gesture: 0, gestureOpen: false, error: ""
  };
  d.index = d.history.length - 1;

  // The fields that follow the state; lists stay the same objects while what they come from does.
  const derive = () => {
    const s = d.state;
    if (d.tree !== s.root) { d.tree = s.root; d.layers = flatten(s.root); }
    if (d.rowsFrom !== d.layers || d.rowsOpen !== d.collapsed) { d.rowsFrom = d.layers; d.rowsOpen = d.collapsed; d.rows = rowsOf(d.layers, d.collapsed); }
    const a = active(s);
    if (!d.active || d.active.id !== a || d.layers.indexOf(d.active) < 0) d.active = a ? d.layers.find((l) => l.id === a) || null : null;
    if (d.editMask && !(d.active && d.active.id === d.maskOf && d.active.props.mask)) d.editMask = false;
    const colour = ["Red", "Green", "Blue"].indexOf(d.channel) >= 0;
    if (d.channel !== "RGB" && (colour ? s.mode !== "RGB" : s.alphas.indexOf(d.channel) < 0)) d.channel = "RGB";
    for (const k of ["w", "h", "ppi", "mode", "bits", "profile", "photo", "faces", "selected", "selection", "prevSelection", "inverse", "alphas", "rot", "flipH", "flipV",
      "layerRot", "layerFlipH", "layerFlipV"]) d[k] = s[k];
  };
  // History: a command starts a step, but within a gesture after its first and for selectLayers, which join the step
  // History is on.
  d.run = (c) => {
    let r;
    try { r = perform(d.state, JSON.parse(JSON.stringify(c))); } catch (e) { d.error = String(e.message || e); return false; }
    const cmds = [].concat(r.c);
    if (r.select || d.gestureOpen) {
      const e = d.history[d.index];
      d.history = d.history.slice(0, d.index).concat([{ name: e.name, icon: e.icon, cmds: e.cmds.concat(cmds), state: r.s }], d.history.slice(d.index + 1));
      if (!r.select) d.dirty = true;
    } else {
      d.history = d.history.slice(0, d.index + 1).concat([{ name: r.name, icon: r.icon, cmds: cmds, state: r.s }]);
      d.index = d.history.length - 1;
      d.snapshot = -1;
      d.dirty = true;
      d.gestureOpen = d.gesture > 0;
    }
    d.state = r.s;
    derive();
    return true;
  };
  d.jumpTo = (i) => {  // to History step i
    d.gesture = 0;
    d.gestureOpen = false;
    d.index = Math.max(0, Math.min(i, d.history.length - 1));
    d.state = d.history[d.index].state;
    d.snapshot = -1;
    derive();
  };
  d.undo = () => { if (d.index > 0) d.jumpTo(d.index - 1); };
  d.redo = () => { if (d.index < d.history.length - 1) d.jumpTo(d.index + 1); };
  d.toggleLast = () => { if (d.history.length > 1) d.jumpTo(d.index === d.history.length - 1 ? d.index - 1 : d.history.length - 1); };
  d.revert = () => d.jumpTo(0);
  d.jumpToSnapshot = (i) => {  // its state, History at the step it was taken on
    const x = d.snapshots[i];
    if (!x) return;
    d.jumpTo(x.at);
    d.state = x.state;
    d.snapshot = i;
    derive();
  };
  d.newSnapshot = () => { d.snapshots = d.snapshots.concat([{ name: qsTr("Snapshot %1").arg(d.snapshots.length), at: d.index, look: "none", state: d.state }]); };
  d.deleteStep = () => {  // the step History is on and those after it
    if (d.index < 1) return;
    d.history = d.history.slice(0, d.index);
    d.jumpTo(d.history.length - 1);
  };
  d.beginGesture = () => { d.gesture++; };
  d.endGesture = () => {
    if (d.gesture > 0) d.gesture--;
    if (!d.gesture) d.gestureOpen = false;
  };
  d.markSaved = () => { d.dirty = false; };
  d.setView = (v) => { d.view = Object.assign({}, d.view, v); };
  d.zoomTo = (pct) => d.setView({ zoom: Math.max(1, Math.min(12800, Math.round(pct * 100) / 100)) / 100 });
  d.toggleOpen = (id) => {  // a group's rows shown or not in Layers
    const c = Object.assign({}, d.collapsed);
    if (c[id]) delete c[id];
    else c[id] = true;
    d.collapsed = c;
    derive();
  };
  d.setEditMask = (on) => {  // the active layer's mask, not the layer, takes the brush (when it has one)
    d.editMask = !!on && !!d.active && !!d.active.props.mask;
    d.maskOf = d.active ? d.active.id : 0;
  };
  d.setChannel = (n) => {  // RGB, Red, Green, Blue or an alpha channel
    d.channel = n;
    derive();
  };
  d.toggleChannel = (n) => {  // shown or hidden
    const c = Object.assign({}, d.hiddenChannels);
    if (c[n]) delete c[n];
    else c[n] = true;
    d.hiddenChannels = c;
  };
  d.can = (name) => can(d, name);
  derive();
  return d;
}

// The commands that rebuild History up to step i from its first state (for tests).
function replay(d, i) {
  let s = d.history[0].state;
  for (let k = 0; k <= i; k++)
    for (const c of d.history[k].cmds) {
      try { s = perform(s, c).s; } catch (e) { if (c.op !== "selectLayers") throw e; }
    }
  return s;
}
// Throws unless s keeps what the commands keep: layer ids unique, 1 to below nextId; groups and only groups with
// children; props in range; the selection there (T1's validate()).
function validate(s) {
  const seen = {};
  const walk = (g) => {
    for (const l of g.children) {
      if (!(l.id >= 1 && l.id < s.nextId) || seen[l.id]) throw bad("layer id " + l.id + " twice or out of range");
      seen[l.id] = true;
      if (KINDS.indexOf(l.kind) < 0 || !!l.children !== (l.kind === "group")) throw bad("layer " + l.id + " of kind " + l.kind);
      checked(l.props, l.kind);
      if (l.children) walk(l);
    }
  };
  walk(s.root);
  const sel = {};
  for (const id of s.selected) {
    if (!seen[id] || sel[id]) throw bad("selected layer " + id + " not there, or twice");
    sel[id] = true;
  }
}
