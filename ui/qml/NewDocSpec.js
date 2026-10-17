.pragma library
.import "Fmt.js" as Fmt
// Tables of the New dialog (build/proto/NewDoc.dc.html: UNITS, PER_IN, ABBR, PROFILES, BG, CATS, ORDER, the saved
// presets) and of the Start screen (Start.dc.html: PRESETS, fold, the sorts of Gần đây), with what both compute from
// them. The dialog keeps sizes in px; a preset's w, h are in its own unit u, at ppi px/inch. Modes, depths and
// backgrounds are the values Docs.create() takes.

var UNITS = ["Pixels", "Inches", "Centimeters", "Millimeters", "Points", "Picas"];
var PER_IN = { Inches: 1, Centimeters: 2.54, Millimeters: 25.4, Points: 72, Picas: 6 };
var ABBR = { Pixels: "px", Inches: "in", Centimeters: "cm", Millimeters: "mm", Points: "pt", Picas: "pica" };
var RES_UNITS = ["Pixels/Inch", "Pixels/Centimeter"];
var MODES = [["RGB", "RGB Color"], ["Gray", "Grayscale"]];
var BITS = [[8, "8 bit"], [16, "16 bit"]];
var PROFILES = { RGB: ["sRGB IEC61966-2.1", "Adobe RGB (1998)", "Display P3", "ProPhoto RGB", qsTr("Không quản lý màu")], Gray: ["Gray Gamma 2.2", "Dot Gain 20%", qsTr("Không quản lý màu")] };
// Nền: [value, label, colour of the swatch]; "background" shows Tools.bg, "custom" the colour picked.
var BG = [["white", qsTr("Trắng"), "#ffffff"], ["black", qsTr("Đen"), "#000000"], ["background", qsTr("Màu nền sau"), ""], ["transparent", qsTr("Trong suốt"), "#ffffff"], ["custom", qsTr("Tùy chọn"), ""]];

// preset: size in its own unit, resolution in px/inch
function P(n, w, h, u, ppi, o) { return Object.assign({ n: n, w: w, h: h, u: u, ppi: ppi || 300, mode: "RGB", bits: 8, bg: "white" }, o); }
// [key, label, presets] in the prototype's ORDER; Đã lưu's are the user's (the dialog keeps them in Prefs).
var CATS = [
  ["recent", qsTr("Gần đây"), [P(qsTr("Chân dung"), 4400, 5600, "Pixels", 300, { bits: 16 }), P("A4", 210, 297, "Millimeters"), P(qsTr("Instagram dọc"), 1080, 1350, "Pixels", 72), P("HDTV 1080p", 1920, 1080, "Pixels", 72), P(qsTr("Ảnh 10 × 15 cm"), 10, 15, "Centimeters")]],
  ["saved", qsTr("Đã lưu"), []],
  ["photo", qsTr("Ảnh"), [P(qsTr("Dọc 4 × 6"), 4, 6, "Inches"), P(qsTr("Ngang 4 × 6"), 6, 4, "Inches"), P(qsTr("Dọc 5 × 7"), 5, 7, "Inches"), P(qsTr("Ngang 5 × 7"), 7, 5, "Inches"), P(qsTr("Dọc 8 × 10"), 8, 10, "Inches"), P(qsTr("Vuông 5 × 5"), 5, 5, "Inches")]],
  ["print", qsTr("In"), [P("A4", 210, 297, "Millimeters"), P("A5", 148, 210, "Millimeters"), P("A3", 297, 420, "Millimeters"), P("Letter", 8.5, 11, "Inches"), P(qsTr("Ảnh 10 × 15 cm"), 10, 15, "Centimeters"), P(qsTr("Ảnh 13 × 18 cm"), 13, 18, "Centimeters"), P(qsTr("Danh thiếp 90 × 54 mm"), 90, 54, "Millimeters"), P(qsTr("Poster 50 × 70 cm"), 50, 70, "Centimeters")]],
  ["art", qsTr("Minh họa"), [P(qsTr("Poster 18 × 24 in"), 18, 24, "Inches"), P("Tabloid", 11, 17, "Inches"), P(qsTr("Bưu thiếp"), 6, 4, "Inches"), P(qsTr("Trang truyện tranh"), 6.625, 10.25, "Inches"), P(qsTr("Tranh số"), 4000, 3000, "Pixels"), P(qsTr("Minh họa vuông"), 3000, 3000, "Pixels")]],
  ["web", "Web", [P(qsTr("Web phổ biến"), 1366, 768, "Pixels", 72), P(qsTr("Web lớn"), 1920, 1080, "Pixels", 72), P(qsTr("Web tối thiểu"), 1024, 768, "Pixels", 72), P("MacBook Pro 14\"", 3024, 1964, "Pixels", 72), P("iMac 24\"", 4480, 2520, "Pixels", 72), P(qsTr("Ảnh chia sẻ Open Graph"), 1200, 630, "Pixels", 72), P(qsTr("Banner ngang"), 728, 90, "Pixels", 72)]],
  ["mobile", qsTr("Di động"), [P("iPhone 15 Pro", 1179, 2556, "Pixels", 72), P("iPhone 15 Pro Max", 1290, 2796, "Pixels", 72), P("iPhone SE", 750, 1334, "Pixels", 72), P("Android 1080p", 1080, 1920, "Pixels", 72), P("Pixel 8", 1080, 2400, "Pixels", 72), P("iPad Pro 12,9\"", 2048, 2732, "Pixels", 72), P("Apple Watch 45 mm", 396, 484, "Pixels", 72)]],
  ["video", "Video", [P("HDTV 720p", 1280, 720, "Pixels", 72), P("HDTV 1080p", 1920, 1080, "Pixels", 72), P("UHD 4K", 3840, 2160, "Pixels", 72), P("DCI 4K", 4096, 2160, "Pixels", 72), P("UHD 8K", 7680, 4320, "Pixels", 72), P(qsTr("Video dọc"), 1080, 1920, "Pixels", 72), P("PAL D1/DV", 720, 576, "Pixels", 72)]]
];
// Đã lưu of the mock, before the user's (Prefs' newDocPresets).
var SAVED = [P(qsTr("Ảnh thẻ 3 × 4 cm"), 3, 4, "Centimeters"), P(qsTr("Retouch 16 bit"), 4000, 6000, "Pixels", 300, { bits: 16 }), P(qsTr("Bìa Facebook"), 851, 315, "Pixels", 72), P(qsTr("Mockup trong suốt"), 2000, 2000, "Pixels", 72, { bg: "transparent" })];
// The dialog as the prototype opens it: In, its A4 on, the size in pixels.
var DEFAULT = { name: "", w: 2480, h: 3508, unit: "Pixels", ppi: 300, ru: "Pixels/Inch", mode: "RGB", bits: 8, bg: "white", bgColor: "#e9c7b3", profile: "sRGB IEC61966-2.1" };

// The Start screen's row (its PRESETS), each with the resolution its tip gives, 72 where it gives none as New's Video.
var START = [
  { name: qsTr("A4 · 300 ppi"), w: 2480, h: 3508, ppi: 300, tip: qsTr("In A4, 210 × 297 mm, 300 ppi") },
  { name: qsTr("Ảnh 10 × 15 cm"), w: 1181, h: 1772, ppi: 300, tip: qsTr("In ảnh 10 × 15 cm, 300 ppi") },
  { name: qsTr("Mạng xã hội dọc"), w: 1080, h: 1350, ppi: 72, tip: qsTr("Ảnh dọc 4:5 cho mạng xã hội, 72 ppi") },
  { name: "Full HD", w: 1920, h: 1080, ppi: 72, tip: qsTr("Màn hình, video 16:9") },
  { name: "4K UHD", w: 3840, h: 2160, ppi: 72, tip: qsTr("Màn hình, video 4K") }
];

function presets(cat) { const c = CATS.find((x) => x[0] === cat); return c ? c[2] : []; }
function toPx(x, u, ppi) { return u === "Pixels" ? x : x / PER_IN[u] * ppi; }
function fromPx(px, u, ppi) { return u === "Pixels" ? px : px / ppi * PER_IN[u]; }
// 1.234,5 with at most d decimals, trailing zeros dropped (the prototype's fmt).
function num(x, d) { return Fmt.vn(x, d || 0).replace(/(,\d*?)0+$/, "$1").replace(/,$/, ""); }
// A length as W and H show it: whole pixels, else two decimals.
function shown(px, u, ppi) { return u === "Pixels" ? num(Math.round(px)) : num(fromPx(px, u, ppi), 2); }
// A card's second line: "210 × 297 mm · 300 ppi".
function sizeText(p) { return num(p.w, 3) + " × " + num(p.h, 3) + " " + ABBR[p.u] + " · " + num(p.ppi, 2) + " ppi"; }
// The size line under Chi tiết: "26,1 MB · 8,27 × 11,69 in".
function info(w, h, ppi, mode, bits) {
  const W = Math.round(w), H = Math.round(h), bytes = W * H * (mode === "Gray" ? 1 : 3) * (bits === 16 ? 2 : 1);
  return (bytes >= 1e6 ? num(bytes / 1e6, bytes >= 1e8 ? 0 : 1) + " MB · " : num(bytes / 1e3) + " KB · ") + num(w / ppi, 2) + " × " + num(h / ppi, 2) + " in";
}
// A page of w × h drawn within mw × mh px, as the cards draw it.
function box(w, h, mw, mh) { const k = Math.min(mw / w, mh / h); return { w: Math.round(w * k), h: Math.round(h * k) }; }
// Chưa đặt tên-1, or the first number no open document has.
function untitled(docs) {
  for (let i = 1; ; i++) {
    const n = qsTr("Chưa đặt tên-%1").arg(i);
    if (!docs.some((d) => d.file === n)) return n;
  }
}

// Gần đây as Start lists it: names holding q, diacritics and case aside ("chan dung" finds "chân dung.psd"), by Ngày
// sửa (0, newest first), Tên (1) or Dung lượng (2, largest first).
function fold(t) { return String(t).toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/đ/g, "d"); }
// Vietnamese order as the prototype's localeCompare(…, "vi"), which Qt's ignores for the system's: a < ă < â, d < đ,
// e < ê, o < ô < ơ, u < ư; tones only break ties.
function viKey(t) {
  return String(t).toLowerCase().normalize("NFD").replace(/[\u0300\u0301\u0303\u0309\u0323]/g, "").replace(/đ/g, "d\u0306")
    .replace(/[^\u0302\u0306\u031b][\u0302\u0306\u031b]?/g, (m) => m[0] + (m.length > 1 ? { "\u0306": "1", "\u0302": "2", "\u031b": "3" }[m[1]] : "0"));
}
function byName(a, b) { const x = viKey(a), y = viKey(b); return x < y ? -1 : x > y ? 1 : a < b ? -1 : a > b ? 1 : 0; }
function recentShown(list, q, sort) {
  const f = fold(String(q).trim());
  return list.filter((r) => !f || fold(r.file).indexOf(f) >= 0)
    .sort((a, b) => (sort === 1 ? byName(a.file, b.file) : sort === 2 ? b.mb - a.mb : b.order - a.order));
}
