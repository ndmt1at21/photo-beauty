.pragma library
.import "Mock.js" as Mock
.import "Fmt.js" as Fmt
// Tables of U17's dialogs (docs/ui-tasks.md): Image Size, Canvas Size (build/proto/ImageSize.dc.html: UNITS, PER_IN, FIT,
// EXT), Fill, Stroke (FillStroke.dc.html: MODES) and the filters of the Filter menu's "…" items: Filters.dc.html's five,
// the others' parameters, ranges and defaults as Photoshop's. Option lists are [value, label] pairs, as Dropdown takes
// them; values are keys, labels go through qsTr.

// ---- Image Size, Canvas Size -----------------------------------------------------------------------------------------
var UNITS = [["%", qsTr("Percent")], ["px", qsTr("Pixels")], ["in", qsTr("Inches")], ["cm", qsTr("Centimeters")],
  ["mm", qsTr("Millimeters")], ["pt", qsTr("Points")], ["pc", qsTr("Picas")]];
var PER_INCH = { in: 1, cm: 2.54, mm: 25.4, pt: 72, pc: 6 };
// px as unit u at ppi (px per inch), % of `base` px; and back.
function toUnit(px, u, ppi, base) { return u === "px" ? px : u === "%" ? px / base * 100 : px / ppi * PER_INCH[u]; }
function fromUnit(x, u, ppi, base) { return u === "px" ? x : u === "%" ? x * base / 100 : x / PER_INCH[u] * ppi; }
function round(x, dec) { const k = Math.pow(10, dec); return Math.round(x * k) / k; }
// Fit To: the document as it is first, any other size last; a preset is w × h in `unit` at ppi.
var FIT = [
  { name: qsTr("Gốc") },
  { name: qsTr("4 × 6 in 300 ppi"), w: 4, h: 6, unit: "in", ppi: 300 },
  { name: qsTr("5 × 7 in 300 ppi"), w: 5, h: 7, unit: "in", ppi: 300 },
  { name: qsTr("8 × 10 in 300 ppi"), w: 8, h: 10, unit: "in", ppi: 300 },
  { name: qsTr("A4 210 × 297 mm 300 ppi"), w: 210, h: 297, unit: "mm", ppi: 300 },
  { name: qsTr("Letter 8,5 × 11 in 300 ppi"), w: 8.5, h: 11, unit: "in", ppi: 300 },
  { name: qsTr("1.920 × 1.080 px 72 ppi"), w: 1920, h: 1080, unit: "px", ppi: 72 },
  { name: qsTr("Tùy chỉnh") }
];
var RESOLUTION_UNITS = [["in", qsTr("Pixels/Inch")], ["cm", qsTr("Pixels/Centimeter")]];
var RESAMPLE = [["automatic", qsTr("Automatic")], ["bicubic", qsTr("Bicubic")], ["bicubicSmoother", qsTr("Bicubic Smoother")],
  ["bicubicSharper", qsTr("Bicubic Sharper")], ["bilinear", qsTr("Bilinear")], ["nearestNeighbor", qsTr("Nearest Neighbor")], ["lanczos", qsTr("Lanczos")]];
// Canvas Size's extension colour: fg and bg are Tools', other the dialog's own.
var EXTENSION = [["fg", qsTr("Màu trước")], ["bg", qsTr("Màu sau")], ["white", qsTr("Trắng")], ["black", qsTr("Đen")], ["gray", qsTr("Xám")],
  ["other", qsTr("Khác…")]];
var GREYS = { white: "#ffffff", black: "#000000", gray: "#808080" };
// Bytes per pixel of a document as Cỡ ảnh counts them: 6 for RGB/16, as the prototype's BPP.
function bytesPerPixel(mode, bits) { return (mode === "Gray" ? 1 : 3) * bits / 8; }
// "148 MB", "26,1 MB", "6 MB"
function megabytes(bytes) { return Fmt.vn(bytes / 1e6, bytes >= 1e8 ? 0 : 1).replace(/,0$/, "") + " MB"; }

// ---- Fill, Stroke ---------------------------------------------------------------------------------------------------
var CONTENTS = [["fg", qsTr("Màu trước")], ["bg", qsTr("Màu sau")], ["color", qsTr("Màu…")], ["history", qsTr("Lịch sử")], ["black", qsTr("Đen")],
  ["gray", qsTr("Xám 50%")], ["white", qsTr("Trắng")]];
// The painting modes: the layer blend modes but Pass Through, with Behind and Clear after Dissolve.
var MODES = [["normal", "Normal"], ["dissolve", "Dissolve"], ["behind", "Behind"], ["clear", "Clear"]]
  .concat(Mock.BLENDS.slice(3).map((b, i) => [b, Mock.BLEND_LABELS[i + 3]]));
var LOCATIONS = [["inside", qsTr("Trong")], ["center", qsTr("Giữa")], ["outside", qsTr("Ngoài")]];

// ---- filters --------------------------------------------------------------------------------------------------------
// The preview's zoom steps, %; 100 % first.
var ZOOMS = [6, 12, 25, 33, 50, 66, 100, 200, 300, 400, 800, 1600];
var ZOOM_100 = 6;
// A filter's rows, shown by kind in this order: slider (a number field over a slider: dec decimals, unit), select,
// radio, check, kernel (Custom's 5 × 5 cells, row by row, and number fields under them).
function slider(key, label, def, min, max, dec, unit) { return { kind: "slider", key: key, label: label, def: def, min: min, max: max, dec: dec, unit: unit }; }
function select(key, label, def, options) { return { kind: "select", key: key, label: label, def: def, options: options }; }
function radio(key, label, def, options) { return { kind: "radio", key: key, label: label, def: def, options: options }; }
function check(key, label, def) { return { kind: "check", key: key, label: label, def: def }; }
const LEVELS = qsTr("mức");
const PRESERVE = [["squareness", qsTr("Squareness")], ["roundness", qsTr("Roundness")]];
// By the key Main.openDialog("Filter", { filter }) takes, in the menu's order: title (also the History step's), w (the
// dialog's width, 400 when not given), rows. Values: { key: value } of the rows.
var FILTERS = {
  boxBlur: { title: qsTr("Box Blur"), rows: [slider("radius", qsTr("Radius"), 10, 1, 2000, 0, "px")] },
  gaussianBlur: { title: qsTr("Gaussian Blur"), rows: [slider("radius", qsTr("Radius"), 4, 0.1, 1000, 1, "px")] },
  motionBlur: { title: qsTr("Motion Blur"), rows: [slider("angle", qsTr("Angle"), 0, -360, 360, 0, "°"), slider("distance", qsTr("Distance"), 10, 1, 2000, 0, "px")] },
  surfaceBlur: { title: qsTr("Surface Blur"), rows: [slider("radius", qsTr("Radius"), 5, 1, 100, 0, "px"), slider("threshold", qsTr("Threshold"), 15, 2, 255, 0, LEVELS)] },
  addNoise: { title: qsTr("Add Noise"), rows: [slider("amount", qsTr("Amount"), 4, 0.1, 400, 1, "%"),
    radio("distribution", qsTr("Phân bố"), "gaussian", [["uniform", qsTr("Uniform")], ["gaussian", qsTr("Gaussian")]]), check("monochromatic", qsTr("Monochromatic"), true)] },
  dustScratches: { title: qsTr("Dust & Scratches"), rows: [slider("radius", qsTr("Radius"), 1, 1, 500, 0, "px"), slider("threshold", qsTr("Threshold"), 0, 0, 255, 0, LEVELS)] },
  median: { title: qsTr("Median"), rows: [slider("radius", qsTr("Radius"), 1, 1, 500, 0, "px")] },
  reduceNoise: { title: qsTr("Reduce Noise"), rows: [slider("strength", qsTr("Strength"), 6, 0, 10, 0, ""), slider("preserveDetails", qsTr("Preserve Details"), 60, 0, 100, 0, "%"),
    slider("reduceColorNoise", qsTr("Reduce Color Noise"), 45, 0, 100, 0, "%"), slider("sharpenDetails", qsTr("Sharpen Details"), 25, 0, 100, 0, "%"),
    check("removeJpegArtifact", qsTr("Remove JPEG Artifact"), false)] },
  unsharpMask: { title: qsTr("Unsharp Mask"), rows: [slider("amount", qsTr("Amount"), 120, 1, 500, 0, "%"), slider("radius", qsTr("Radius"), 1.2, 0.1, 1000, 1, "px"),
    slider("threshold", qsTr("Threshold"), 3, 0, 255, 0, LEVELS)] },
  custom: { title: qsTr("Custom"), w: 440, rows: [{ kind: "kernel", key: "kernel", def: [0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, -1, 5, -1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0], min: -999, max: 999,
    fields: [{ key: "scale", label: qsTr("Scale"), def: 1, min: 1, max: 9999 }, { key: "offset", label: qsTr("Offset"), def: 0, min: -9999, max: 9999 }] }] },
  highPass: { title: qsTr("High Pass"), rows: [slider("radius", qsTr("Radius"), 10, 0.1, 1000, 1, "px")] },
  maximum: { title: qsTr("Maximum"), rows: [slider("radius", qsTr("Radius"), 1, 1, 500, 0, "px"), select("preserve", qsTr("Preserve"), "squareness", PRESERVE)] },
  minimum: { title: qsTr("Minimum"), rows: [slider("radius", qsTr("Radius"), 1, 1, 500, 0, "px"), select("preserve", qsTr("Preserve"), "squareness", PRESERVE)] },
  offset: { title: qsTr("Offset"), rows: [slider("horizontal", qsTr("Horizontal"), 0, -30000, 30000, 0, "px"), slider("vertical", qsTr("Vertical"), 0, -30000, 30000, 0, "px"),
    radio("undefinedAreas", qsTr("Undefined Areas"), "wrap", [["transparent", qsTr("Set to Transparent")], ["repeat", qsTr("Repeat Edge Pixels")], ["wrap", qsTr("Wrap Around")]])] }
};
// A filter's values as it opens.
function defaults(f) {
  const o = {};
  for (const r of f ? f.rows : []) {
    o[r.key] = Array.isArray(r.def) ? r.def.slice() : r.def;
    for (const g of r.fields || []) o[g.key] = g.def;
  }
  return o;
}
