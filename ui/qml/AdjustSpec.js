.pragma library
.import "Fmt.js" as Fmt
// The 16 adjustments (docs/ui-tasks.md, U10): ADJ, ADJ_ORDER, PRESETS, TRK, ATOOLS, CURVE_D, SC_DOT and the LISTS the
// rows use, copied by a script from build/proto/Main.dc.html, and what AdjustRows builds the rows of Properties and of
// the Adjust dialog from. Two rows are the Adjustments artboard's where Main's ADJ differs: Levels' Auto and its options
// sit in the channel row (no Auto row), Threshold's level is a field under the histogram (no slider).
// Row DSL: 'PRE~presetList', 'DDL~key~label~default~list~auto~options', 'BTN~text', 'S~key~label~min~max~default~decimals~track~unit',
// 'CHK~key~label~on', 'SEG~key~a|b|c~index', 'TOOLS~h|b|g|w', 'NOTE~text', 'HIST~levels|threshold', 'OUT', 'FLDS~k=v|k=v',
// 'LF~k=Label=v|…', 'CURVE', 'RANGE', 'GRAD~css', 'RADIO~group~index~label~list-or-#hex~default', 'TINT~on~#hex', 'TOT~label'.
// Names are Photoshop's (plan.md, point 9); an adjustment layer keeps the name (props.adj) and the values it changed
// (props.vals: changed()), the Adjust dialog takes MenuSpec's camelCase key (key()).

// ADJ_ORDER: the Adjustments panel's grid, the menus' order.
var ORDER = ["Brightness/Contrast", "Levels", "Curves", "Exposure", "Vibrance", "Hue/Saturation", "Color Balance", "Black & White", "Photo Filter", "Channel Mixer", "Color Lookup", "Invert", "Posterize", "Threshold", "Gradient Map", "Selective Color"];
// ADJ: icon, the grid's short name, rows.
var ADJ = {
  "Brightness/Contrast": { ic: "brightness_6", sh: "Brightness", rows: ["BTN~Auto", "S~b~Brightness~-150~150~0", "S~c~Contrast~-50~100~0", "CHK~legacy~Use Legacy~0"] },
  "Levels": { ic: "bar_chart", sh: "Levels", rows: ["PRE~levels", "DDL~ch~~RGB~chan~1~1", "TOOLS~b|g|w", "HIST~levels", "FLDS~in0=0|gam=1,00|in1=255", "OUT", "FLDS~out0=0|out1=255"] },
  "Curves": { ic: "curve", sh: "Curves", rows: ["PRE~curves", "DDL~ch~~RGB~chan~1", "TOOLS~h|b|g|w", "CURVE", "LF~cin=Input=64|cout=Output=52"] },
  "Exposure": { ic: "exposure", sh: "Exposure", rows: ["S~e~Exposure~-20~20~0~2", "S~o~Offset~-0.5~0.5~0~4", "S~g~Gamma Correction~0.01~9.99~1~2", "TOOLS~b|g|w"] },
  "Vibrance": { ic: "change_history", sh: "Vibrance", rows: ["S~v~Vibrance~-100~100~0~0~vib", "S~s~Saturation~-100~100~0~0~sat"] },
  "Hue/Saturation": { ic: "looks", sh: "Hue/Sat", rows: ["PRE~hue", "DDL~rng~~Master~hueRange", "TOOLS~h", "S~h~Hue~-180~180~0~0~hue", "S~s~Saturation~-100~100~0~0~sat", "S~l~Lightness~-100~100~0~0~lig", "CHK~colorize~Colorize~0", "TOOLS~d|dp|dm", "RANGE"] },
  "Color Balance": { ic: "balance", sh: "Color Bal.", rows: ["SEG~tone~Shadows|Midtones|Highlights~1", "S~cr~Cyan · Red~-100~100~0~0~cr", "S~mg~Magenta · Green~-100~100~0~0~mg", "S~yb~Yellow · Blue~-100~100~0~0~yb", "CHK~pl~Preserve Luminosity~1"] },
  "Black & White": { ic: "filter_b_and_w", sh: "B & W", rows: ["PRE~bw", "TINT~0~#c8a27c", "BTN~Auto", "TOOLS~h", "S~r~Reds~-200~300~40~0~reds~%", "S~y~Yellows~-200~300~60~0~yel~%", "S~g~Greens~-200~300~40~0~gre~%", "S~c~Cyans~-200~300~60~0~cya~%", "S~b~Blues~-200~300~20~0~blu~%", "S~m~Magentas~-200~300~80~0~mag~%"] },
  "Photo Filter": { ic: "camera", sh: "Photo Filter", rows: ["RADIO~pf~0~Filter~pfilter~Warming Filter (85)", "RADIO~pf~1~Color~#ec8a00", "S~d~Density~1~100~25~0~~%", "CHK~pl~Preserve Luminosity~1"] },
  "Channel Mixer": { ic: "join", sh: "Ch. Mixer", rows: ["DDL~out~Output Channel~Red~chan3", "CHK~mono~Monochrome~0", "S~r~Red~-200~200~100~0~~%", "S~g~Green~-200~200~0~0~~%", "S~b~Blue~-200~200~0~0~~%", "TOT~Total", "S~k~Constant~-200~200~0~0~~%"] },
  "Color Lookup": { ic: "grid_on", sh: "Lookup", rows: ["RADIO~lf~0~3DLUT File~lut~Phim ấm.cube", "RADIO~lf~1~Abstract~lutA~Chọn…", "RADIO~lf~2~Device Link~lutA~Chọn…", "CHK~dither~Dither~0"] },
  "Invert": { ic: "invert_colors", sh: "Invert", rows: ["NOTE~" + qsTr("Invert không có thông số; bật tắt bằng mắt của layer.")] },
  "Posterize": { ic: "stairs", sh: "Posterize", rows: ["S~lv~Levels~2~255~4"] },
  "Threshold": { ic: "contrast", sh: "Threshold", rows: ["HIST~threshold", "LF~t=Threshold Level=128"] },
  "Gradient Map": { ic: "gradient", sh: "Grad. Map", rows: ["GRAD~linear-gradient(90deg, #1b1020, #9e2f45, #e9c7b3)", "CHK~dither~Dither~0", "CHK~rev~Reverse~0", "DDL~method~Method~Perceptual~method"] },
  "Selective Color": { ic: "palette", sh: "Selective", rows: ["DDL~col~Colors~Reds~scColors", "S~c~Cyan~-100~100~0~0~~%", "S~m~Magenta~-100~100~0~0~~%", "S~y~Yellow~-100~100~0~0~~%", "S~k~Black~-100~100~0~0~~%", "SEG~rel~Relative|Absolute~0"] }
};
var PRESETS = {
  curves: ["Default", "Medium Contrast (RGB)", "Strong Contrast (RGB)", "Linear Contrast (RGB)", "Increase Contrast (RGB)", "Lighter (RGB)", "Darker (RGB)", "Negative (RGB)"],
  levels: ["Default", "Increase Contrast 1", "Increase Contrast 2", "Lighter", "Darker", "Midtones Brighter", "Midtones Darker"],
  hue: ["Default", "Cyanotype", "Increase Red Saturation", "Old Style", "Red Boost", "Sepia", "Strong Saturation", "Yellow Boost"],
  bw: ["Default", "Blue Filter", "Green Filter", "High Contrast Red Filter", "Infrared", "Lighter", "Darker", "Red Filter", "Yellow Filter"]
};
// Slider tracks (CSS gradients; colors() reads their stops).
var TRK = {
  hue: "linear-gradient(90deg, #ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff, #ff0000)", sat: "linear-gradient(90deg, #808080, #e0663f)", vib: "linear-gradient(90deg, #7d7d7d, #e0663f)",
  lig: "linear-gradient(90deg, #000000, #808080, #ffffff)", cr: "linear-gradient(90deg, #00ffff, #808080, #ff0000)", mg: "linear-gradient(90deg, #ff00ff, #808080, #00ff00)",
  yb: "linear-gradient(90deg, #ffff00, #808080, #0000ff)", reds: "linear-gradient(90deg, #3a0000, #ff4d4d)", yel: "linear-gradient(90deg, #3a3a00, #ffff4d)", gre: "linear-gradient(90deg, #003a00, #4dff4d)",
  cya: "linear-gradient(90deg, #003a3a, #4dffff)", blu: "linear-gradient(90deg, #00003a, #4d4dff)", mag: "linear-gradient(90deg, #3a003a, #ff4dff)"
};
// The TOOLS buttons: icon, tooltip, swatch ("#…") or mark (+, −).
var ATOOLS = {
  h: ["touch_app", qsTr("Kéo trên ảnh để chỉnh · Targeted Adjustment"), ""], b: ["colorize", qsTr("Ống hút điểm đen · Set Black Point"), "#000000"], g: ["colorize", qsTr("Ống hút điểm xám · Set Gray Point"), "#808080"],
  w: ["colorize", qsTr("Ống hút điểm trắng · Set White Point"), "#ffffff"], d: ["colorize", qsTr("Ống hút dải màu"), ""], dp: ["colorize", qsTr("Thêm vào dải màu · Add to Sample"), "+"], dm: ["colorize", qsTr("Bớt khỏi dải màu · Subtract from Sample"), "−"]
};
// Each Curves preset's curve as the prototype draws it, in a 284 × 220 box (presetPoints() reads its points).
var CURVE_D = {
  "Default": "M0 220L284 0", "Medium Contrast (RGB)": "M0 220 C 40 206, 52 186, 71 175 C 110 152, 170 76, 214 42 C 240 22, 262 6, 284 0",
  "Strong Contrast (RGB)": "M0 220 C 40 214, 52 200, 71 192 C 110 160, 170 60, 214 26 C 240 10, 262 4, 284 0", "Linear Contrast (RGB)": "M0 220 L71 186 L214 34 L284 0",
  "Increase Contrast (RGB)": "M0 220 C 50 210, 60 190, 71 180 C 110 150, 170 70, 214 40 C 240 24, 262 8, 284 0", "Lighter (RGB)": "M0 220 C 60 150, 140 70, 284 0",
  "Darker (RGB)": "M0 220 C 140 150, 220 70, 284 0", "Negative (RGB)": "M0 0L284 220"
};
// Selective Color's dot by colour.
var SC_DOT = { Reds: "#ef5350", Yellows: "#e5d03a", Greens: "#4caf50", Cyans: "#26c6da", Blues: "#4c6ef5", Magentas: "#d946ef", Whites: "#ffffff", Neutrals: "#808080", Blacks: "#000000" };
// The lists of LISTS the rows and Properties' pages pick from.
var LISTS = {
  chan: ["RGB", "Red", "Green", "Blue"],
  chan3: ["Red", "Green", "Blue"],
  hueRange: ["Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"],
  scColors: ["Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Whites", "Neutrals", "Blacks"],
  pfilter: ["Warming Filter (85)", "Warming Filter (LBA)", "Warming Filter (81)", "Cooling Filter (80)", "Cooling Filter (LBB)", "Cooling Filter (82)", "Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Violet", "Magenta", "Sepia", "Underwater"],
  lut: [qsTr("Phim ấm.cube"), qsTr("Phim lạnh.cube"), "Teal Orange.3dl", qsTr("Nạp 3DLUT…")],
  lutA: [qsTr("Chọn…")],
  method: ["Perceptual", "Linear", "Classic", "Smooth", "Stripes"],
  docMode: ["RGB, 8 bit", "RGB, 16 bit", "Grayscale, 8 bit", "Grayscale, 16 bit"],
  profile: ["sRGB IEC61966-2.1", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"],
  font: ["Be Vietnam Pro", "Montserrat", "Lora", "Playfair Display", "Noto Serif"],
  fstyle: ["Regular", "Medium", "SemiBold", "Bold"],
  aa: ["aA None", "aA Sharp", "aA Crisp", "aA Strong", "aA Smooth"]
};
// Properties of a layer: the six alignments (a step each in the mock), the paragraph's (PARA, Properties shows 0, 1, 2, 6), a shape's path operations.
var ALIGN = [["align_horizontal_left", qsTr("Căn trái")], ["align_horizontal_center", qsTr("Căn giữa ngang")], ["align_horizontal_right", qsTr("Căn phải")], ["align_vertical_top", qsTr("Căn trên")], ["align_vertical_center", qsTr("Căn giữa dọc")], ["align_vertical_bottom", qsTr("Căn dưới")]];
var PARA = [["format_align_left", qsTr("Căn trái · Left Align Text")], ["format_align_center", qsTr("Căn giữa · Center Text")], ["format_align_right", qsTr("Căn phải · Right Align Text")], ["just_l", qsTr("Căn đều, dòng cuối căn trái · Justify Last Left")], ["just_c", qsTr("Căn đều, dòng cuối căn giữa · Justify Last Centered")], ["just_r", qsTr("Căn đều, dòng cuối căn phải · Justify Last Right")], ["just_a", qsTr("Căn đều mọi dòng · Justify All")]];
var PATH_OPS = [["mode_add", qsTr("Cộng · Combine Shapes")], ["mode_sub", qsTr("Trừ · Subtract Front Shape")], ["mode_int", qsTr("Giao · Intersect Shape Areas")], ["mode_exc", qsTr("Loại phần chồng nhau · Exclude Overlapping Shapes")]];

// The image's histogram as the prototype draws it: Levels' and Threshold's in 284 × 96, Curves' in 284 × 220.
var HIST_D = "M0 96 L0 88 C 22 84, 34 52, 60 46 C 88 40, 100 66, 126 58 C 152 50, 170 14, 200 20 C 226 26, 244 62, 284 84 L284 96 Z";
var CURVE_HIST_D = "M0 220 L0 206 C 26 200, 40 150, 70 140 C 100 130, 116 168, 146 158 C 176 148, 194 74, 226 84 C 252 92, 268 170, 284 196 L284 220 Z";
// The number fields of FLDS and LF: [min, max, decimals, name]; Levels' inputs stay 2 apart (limits()). Curves' Input and
// Output are the point picked on the curve (POINT: which coordinate), no values of their own.
var FIELDS = {
  in0: [0, 253, 0, qsTr("Điểm đen đầu vào")], gam: [0.01, 9.99, 2, qsTr("Gamma")], in1: [2, 255, 0, qsTr("Điểm trắng đầu vào")],
  out0: [0, 255, 0, qsTr("Điểm đen đầu ra")], out1: [0, 255, 0, qsTr("Điểm trắng đầu ra")], cin: [0, 255, 0, qsTr("Input")], cout: [0, 255, 0, qsTr("Output")],
  t: [1, 255, 0, qsTr("Threshold Level")]
};
var POINT = { cin: 0, cout: 1 };
// Sliders whose values print without "+" (Photoshop's percentages of Black & White, as the artboard prints them).
var PLAIN = { "Black & White": true };
// Hue/Saturation: Photoshop's falloff and range of each colour range, ° ([a, b, c, d]: 315°/345° 15°\45° for Reds), and
// the hue its bars centre on (Master: red at both ends, as the prototype's bar).
var SPANS = { Reds: [315, 345, 15, 45], Yellows: [15, 45, 75, 105], Greens: [75, 105, 135, 165], Cyans: [135, 165, 195, 225], Blues: [195, 225, 255, 285],
  Magentas: [255, 285, 315, 345] };
var HUES = { Master: 180, Reds: 0, Yellows: 60, Greens: 120, Cyans: 180, Blues: 240, Magentas: 300 };
// ponytail: what a preset and Auto set. The prototype keeps preset names only (and Curves' curves, CURVE_D); Levels'
// follow its EFX (contrast ×1,08, ×1,16; brightness ×1,1, ×0,9, ×1,05, ×0,95), the rest is near Photoshop's, Auto's made
// up (the mock has no pixels to look at). The core's preset files and Auto replace them.
var PRESET_VALS = {
  "Levels": {
    "Increase Contrast 1": { in0: 10, in1: 245 }, "Increase Contrast 2": { in0: 18, in1: 237 }, "Lighter": { in1: 232 }, "Darker": { out1: 230 },
    "Midtones Brighter": { gam: 1.25 }, "Midtones Darker": { gam: 0.8 }
  },
  "Hue/Saturation": {
    "Cyanotype": { colorize: true, h: -160, s: 25 }, "Increase Red Saturation": { rng: "Reds", s: 30 }, "Old Style": { s: -45, l: 5 },
    "Red Boost": { rng: "Reds", s: 50 }, "Sepia": { colorize: true, h: 35, s: 25 }, "Strong Saturation": { s: 50 }, "Yellow Boost": { rng: "Yellows", s: 50 }
  },
  "Black & White": {
    "Blue Filter": { r: 0, y: 0, g: 40, c: 100, b: 140, m: 100 }, "Green Filter": { r: 40, y: 100, g: 140, c: 60, b: 20, m: 40 },
    "High Contrast Red Filter": { r: 120, y: 120, g: -10, c: -50, b: -50, m: 120 }, "Infrared": { r: -40, y: 235, g: 144, c: -68, b: -3, m: -107 },
    "Lighter": { r: 60, y: 80, g: 60, c: 80, b: 40, m: 100 }, "Darker": { r: 20, y: 40, g: 20, c: 40, b: 0, m: 60 },
    "Red Filter": { r: 120, y: 110, g: -10, c: -50, b: -50, m: 120 }, "Yellow Filter": { r: 120, y: 110, g: 40, c: -30, b: 0, m: 70 }
  }
};
var AUTO = { "Brightness/Contrast": { b: 12, c: 8 }, "Levels": { in0: 8, in1: 238 }, "Curves": { pts: { RGB: [[8, 0], [238, 255]] } },
  "Black & White": { r: 52, y: 70, g: 36, c: 54, b: 18, m: 74 } };

// MenuSpec's params.adjust of an adjustment, and back: "Hue/Saturation" is "hueSaturation".
function key(name) { const s = name.replace(/[^A-Za-z]/g, ""); return s.charAt(0).toLowerCase() + s.slice(1); }
function nameOf(k) { return ORDER.find((n) => key(n) === k) || ""; }
// The colours of a CSS gradient, in order.
function colors(css) { return String(css || "").match(/#[0-9a-fA-F]{6}/g) || []; }

// The rows of an adjustment, parsed once: { k (the DSL's kind), key, label, … } as each kind needs.
const ROWS = {};
function rows(name) {
  if (!ROWS[name]) ROWS[name] = (ADJ[name] ? ADJ[name].rows : []).map(row);
  return ROWS[name];
}
function row(raw) {
  const a = raw.split("~"), k = a[0];
  switch (k) {
  case "PRE": return { k: k, list: PRESETS[a[1]] || ["Default"] };
  case "DDL": return { k: k, key: a[1], label: a[2] || "", def: a[3], list: LISTS[a[4]] || [a[3]], auto: a[5] === "1", options: a[6] === "1" };
  case "BTN": return { k: k, label: a[1] };
  case "S": return { k: k, key: a[1], label: a[2], min: +a[3], max: +a[4], def: +a[5], dec: +(a[6] || 0), track: colors(TRK[a[7]]), unit: a[8] || "" };
  case "CHK": return { k: k, key: a[1], label: a[2], def: a[3] === "1" };
  case "SEG": return { k: k, key: a[1], list: a[2].split("|"), def: +a[3] };
  case "TOOLS": return { k: k, list: a[1].split("|") };
  case "NOTE": return { k: k, label: a[1] };
  case "HIST": return { k: k, mode: a[1] };
  case "FLDS": case "LF": return { k: k, fields: a[1].split("|").map((f) => { const p = f.split("="); return { key: p[0], label: k === "LF" ? p[1] : "", def: Fmt.pnum(p[p.length - 1]) }; }) };
  case "GRAD": return { k: k, css: a[1], colors: colors(a[1]) };
  case "RADIO": return { k: k, key: a[1], index: +a[2], label: a[3], list: a[4][0] === "#" ? null : LISTS[a[4]] || [a[5]], def: a[4][0] === "#" ? a[4] : a[5] };
  case "TINT": return { k: k, def: a[1] === "1", color: a[2] };
  case "TOT": return { k: k, label: a[1] };
  default: return { k: k };  // OUT, CURVE, RANGE
  }
}

// Every value of an adjustment as it starts, by key, as the prototype's adjDefaults(); with the colours of its swatches
// (RADIO's at key + index, TINT's tintColor), Curves' points by channel (pts: none of their own, the preset's) and
// Hue/Saturation's span (null: its colour range's, SPANS).
const DEFAULTS = {};
function defaults(name) {
  if (DEFAULTS[name]) return DEFAULTS[name];
  const v = {};
  for (const r of rows(name)) {
    if (r.k === "PRE") v.pre = "Default";
    else if (r.k === "RADIO") { if (r.index === 0) v[r.key] = 0; v[r.key + r.index] = r.def; }
    else if (r.k === "TINT") { v.tint = r.def; v.tintColor = r.color; }
    else if (r.k === "FLDS" || r.k === "LF") { for (const f of r.fields) if (POINT[f.key] === undefined) v[f.key] = f.def; }
    else if (r.k === "CURVE") v.pts = {};
    else if (r.k === "RANGE") v.span = null;
    else if (r.key) v[r.key] = r.def;
  }
  DEFAULTS[name] = v;
  return v;
}
function full(name, vals) { return Object.assign({}, defaults(name), vals || {}); }
// Those of v that are not the defaults: what an adjustment layer keeps (vals).
function changed(name, v) {
  const d = defaults(name), o = {};
  for (const k in v) if (JSON.stringify(v[k]) !== JSON.stringify(d[k])) o[k] = v[k];
  return o;
}
// What picking a preset sets: its values over the defaults of what any of the adjustment's presets sets; Curves' takes
// the preset's curve and lets the channels' own go, a colour range its span.
function presetValues(name, pre) {
  const P = PRESET_VALS[name] || {}, d = defaults(name), o = { pre: pre };
  for (const p in P) for (const k in P[p]) o[k] = d[k];
  Object.assign(o, P[pre] || {});
  if (name === "Curves") o.pts = {};
  if (o.rng !== undefined) o.span = null;
  return o;
}

// A preset's points, 0..255, ends included, read off its path in CURVE_D: where its segments meet, or the middle of
// the only one (Lighter, Darker).
const POINTS = {};
function presetPoints(pre) {
  if (!CURVE_D[pre]) pre = "Default";
  if (POINTS[pre]) return POINTS[pre];
  const segs = [], re = /([MLC])([^MLC]*)/g;
  let m;
  while ((m = re.exec(CURVE_D[pre]))) segs.push({ c: m[1], n: m[2].match(/-?[\d.]+/g).map(Number) });
  const at = (x, y) => [Math.round(x * 255 / 284), Math.round((220 - y) * 255 / 220)];
  const pts = segs.map((s) => at(s.n[s.n.length - 2], s.n[s.n.length - 1]));
  if (segs.length === 2 && segs[1].c === "C") {
    const p = segs[0].n.concat(segs[1].n), mid = (i) => (p[i] + 3 * p[i + 2] + 3 * p[i + 4] + p[i + 6]) / 8;
    pts.splice(1, 0, at(mid(0), mid(1)));
  }
  POINTS[pre] = pts;
  return pts;
}
// The points of a channel's curve: its own, else the preset's (RGB) or the diagonal.
function curvePoints(v, ch) { return (v.pts && v.pts[ch]) || (ch === "RGB" ? presetPoints(v.pre) : [[0, 0], [255, 255]]); }

// Levels' grey triangle: the input that comes out at 50 % (Photoshop's out = in ^ (1 / gamma)), and the gamma of a
// triangle there; what dragging triangle i (black, grey, white) to x sets.
function midOf(in0, in1, gam) { return in0 + (in1 - in0) * Math.pow(0.5, gam); }
function gammaOf(in0, in1, x) {
  const t = Math.max(0.001, Math.min(0.999, (x - in0) / (in1 - in0)));
  return Math.max(0.01, Math.min(9.99, Math.round(Math.log(t) / Math.log(0.5) * 100) / 100));
}
function levelsAt(v, i, x) {
  return i === 0 ? { in0: Math.max(0, Math.min(v.in1 - 2, x)) } : i === 2 ? { in1: Math.min(255, Math.max(v.in0 + 2, x)) } : { gam: gammaOf(v.in0, v.in1, x) };
}
// A field's [min, max] with the values as they are.
function limits(k, v) {
  const f = FIELDS[k];
  return k === "in0" ? [f[0], v.in1 - 2] : k === "in1" ? [v.in0 + 2, f[1]] : [f[0], f[1]];
}
