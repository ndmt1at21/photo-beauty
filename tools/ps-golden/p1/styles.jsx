// Photoshop's images of P1's layer styles, type, shapes, smart objects and mask Feather (F31), for L30-L35, Y30, Y32
// and B36's tests; README-filters.md has the steps. Writes out/styles/ next to this script: <case>.png, index.json
// (each case as photo-beauty's scene: a style as props.fx in LayerStyleDialog's keys, type and shapes as props.source,
// a smart object's transform as props.transform; tests/golden.h reads it), photoshop.txt. The four effects of P1 on
// shapes.png (B30's input: hard and soft edges, a 1-pixel line), each with its values, over a transparent canvas (the
// alpha is the effect's cover) or over ramp.png (blend modes); Photoshop's own defaults of each effect, read back into
// index.json; Fill 0, knockout, a layer mask, layer opacity; Space Grotesk at five anti-aliasings, sizes, tracking,
// Vietnamese; live shapes and their strokes; a smart object scaled and rotated; a layer mask feathered. 8 bits: the
// 16-bit composite rounded, no dither (decision 6 of docs/p1-tasks.md). 74 cases, about 1.0 MB, about 5 minutes. Not
// run in Photoshop yet: what it is unsure of is in README-filters.md. Strings past ASCII are \u escapes, so that the
// file reads the same in any encoding. The descriptors of styles, shapes and type are psd.jsx's (E33): a fix to one
// is likely a fix to the other.
//
// Keys past lib.jsx's (PB.layerKeys, PB.kinds):
//   props.fx {dropShadow, stroke, colorOverlay, gradientOverlay}: each effect with every key (S.KEYS, LayerStyleDialog
//     .qml's and L30's: on, blend, color '#rrggbb', opacity %, angle, distance, spread %, size, noise %, knockOut;
//     position; gradient (D30's JSON), method, style, scale %, reverse, dither, align, offset [x %, y %]); an effect of
//     {on: true} alone is Photoshop's defaults, which index.json gets read back from Photoshop.
//   kind 'text', props.source {text, font (PostScript name), family, style, size (pt: px at 72 ppi), color, tracking
//     (thousandths of an em), aa (none, sharp, crisp, strong, smooth), at [x, y] (point text: the start of the
//     baseline)}: psd.jsx's, with family and style; index.json gets the PostScript name Photoshop used.
//   kind 'shape', props.source {type (rectangle, ellipse, polygon, line), box [left, top, right, bottom], radii [tl, tr,
//     br, bl], sides, star %, from, to, weight, fill '#rrggbb' or null, stroke {width, color, align (inside, center,
//     outside), cap, join} or null}: psd.jsx's.
//   kind 'so' (a smart object): src (an input: its layer converted to a smart object, so that its contents are those
//     pixels, sRGB, 16 bits), place {scale %, rotate (degrees clockwise)} about the canvas's centre, props.transform
//     (A31: a 3 x 3 matrix by rows from the contents' pixels to the document's, x' = m0 u + m1 v + m2, y' = m3 u + m4 v
//     + m5, corners of pixels at whole numbers); index.json gets the one Photoshop read back.
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
#include "lib.jsx"

var S = {};

// ---- Small things (psd.jsx's) --------------------------------------------------------------------------------------

S.c = function (s) { return charIDToTypeID(s); };
S.s = function (s) { return stringIDToTypeID(s); };
S.id = function (s) { return s.length == 4 ? S.c(s) : S.s(s); };  // 4 letters: a char ID, else a string ID
S.or = function (v, d) { return v === undefined ? d : v; };
S.HEX = /^#[0-9a-f]{6}$/;
S.rgb3 = function (hex) {  // '#rrggbb' as [r, g, b], 0..255
  if (typeof hex != 'string' || !S.HEX.test(hex)) throw new Error('a colour ' + hex);
  var v = parseInt(hex.slice(1), 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
};
S.hex = function (rgb) {
  var s = '#';
  for (var i = 0; i < 3; i++) s += ('0' + Math.max(0, Math.min(255, Math.round(rgb[i]))).toString(16)).slice(-2);
  return s;
};
S.rgbc = function (c) {  // [r, g, b] or '#rrggbb' as Photoshop's RGBC
  var v = typeof c == 'string' ? S.rgb3(c) : c, d = new ActionDescriptor();
  d.putDouble(S.c('Rd  '), v[0]);
  d.putDouble(S.c('Grn '), v[1]);
  d.putDouble(S.c('Bl  '), v[2]);
  return d;
};
S.solid = function (hex) {
  var c = new SolidColor(), v = S.rgb3(hex);
  c.rgb.red = v[0];
  c.rgb.green = v[1];
  c.rgb.blue = v[2];
  return c;
};
// photo-beauty's blend modes (kBlendNames) as Action Manager's BlnM, and back.
S.BLNM = {normal: 'Nrml', dissolve: 'Dslv', darken: 'Drkn', multiply: 'Mltp', colorBurn: 'CBrn', linearBurn: 'linearBurn',
          darkerColor: 'darkerColor', lighten: 'Lghn', screen: 'Scrn', colorDodge: 'CDdg', linearDodge: 'linearDodge',
          lighterColor: 'lighterColor', overlay: 'Ovrl', softLight: 'SftL', hardLight: 'HrdL', vividLight: 'vividLight',
          linearLight: 'linearLight', pinLight: 'pinLight', hardMix: 'hardMix', difference: 'Dfrn', exclusion: 'Xclu',
          subtract: 'blendSubtraction', divide: 'blendDivide', hue: 'H   ', saturation: 'Strt', color: 'Clr ',
          luminosity: 'Lmns'};
S.blnm = function (name) {
  if (!S.BLNM[name]) throw new Error('no blend mode ' + name);
  return S.id(S.BLNM[name]);
};
S.blendName = function (id) {
  for (var k in S.BLNM)
    if (S.BLNM.hasOwnProperty(k) && S.id(S.BLNM[k]) == id) return k;
  return PB.keyName(id);
};
S.round = function (v, k) { return Math.round(v * k) / k; };

// ---- Layer styles (L30's fx) ---------------------------------------------------------------------------------------

// The four effects of P1: Photoshop's class (and the list it may keep them in), and every key of index.json, in order.
S.FX = {dropShadow: 'DrSh', stroke: 'FrFX', colorOverlay: 'SoFi', gradientOverlay: 'GrFl'};
S.MULTI = {dropShadow: 'dropShadowMulti', stroke: 'frameFXMulti', colorOverlay: 'solidFillMulti',
           gradientOverlay: 'gradientFillMulti'};
S.KEYS = {
  dropShadow: ['on', 'blend', 'color', 'opacity', 'angle', 'distance', 'spread', 'size', 'noise', 'knockOut'],
  stroke: ['on', 'size', 'position', 'blend', 'opacity', 'color'],
  colorOverlay: ['on', 'blend', 'color', 'opacity'],
  gradientOverlay: ['on', 'blend', 'opacity', 'gradient', 'method', 'style', 'angle', 'scale', 'reverse', 'dither',
                    'align', 'offset']
};
// What a case leaves out of an effect: LayerStyleDialog.qml's factory() (the prototype's values), turned on; Gradient
// Overlay's gradient (an index of Presets.GR_GROUPS there) as D30's JSON, three colours and an opacity ramp, mixed
// Classic with Smoothness 0 (as encoded, linearly: the published way, so that L34 tests its shapes, not D30's methods).
S.GRADIENT = {colors: [{p: 0, c: [24, 40, 168]}, {p: 0.5, c: [244, 200, 48]}, {p: 1, c: [208, 32, 64]}],
              alphas: [{p: 0, a: 1}, {p: 1, a: 0.4}], smoothness: 0, name: 'F31'};
S.BASE = {
  dropShadow: {on: true, blend: 'multiply', color: '#2b1d4e', opacity: 35, angle: 120, distance: 12, spread: 0, size: 24,
               noise: 0, knockOut: true},
  stroke: {on: true, size: 6, position: 'outside', blend: 'normal', opacity: 100, color: '#f7c873'},
  colorOverlay: {on: true, blend: 'normal', color: '#ff0000', opacity: 100},
  gradientOverlay: {on: true, blend: 'normal', opacity: 100, gradient: S.GRADIENT, method: 'classic', style: 'linear',
                    angle: 90, scale: 100, reverse: false, dither: false, align: true, offset: [0, 0]}
};
S.METHOD = {perceptual: 'Perc', linear: 'Lnr ', classic: 'Gcls', smooth: 'Smoo', stripes: 'Stri'};  // Photoshop 2023's
S.STYLE = {linear: 'Lnr ', radial: 'Rdl ', angle: 'Angl', reflected: 'Rflc', diamond: 'Dmnd'};
S.POSITION = {outside: 'OutF', inside: 'InsF', center: 'CtrF'};
S.RANGES = {opacity: [0, 100], angle: [-360, 360], distance: [0, 30000], spread: [0, 100], size: [0, 250], noise: [0, 100],
            scale: [10, 150]};
// An effect of a case, every key: given over S.BASE, checked. 'defaults' stays {on: true}: Photoshop's own (S.applyFx).
S.effectOf = function (name, given) {
  if (!S.KEYS[name]) throw new Error('no effect ' + name);
  if (given == 'defaults') return {on: true};
  var keys = S.KEYS[name], out = {}, k, i;
  for (k in given)
    if (given.hasOwnProperty(k) && !PB.has(keys, k)) throw new Error(name + ' has no key ' + k);
  for (i = 0; i < keys.length; i++) {
    k = keys[i];
    var v = given[k] === undefined ? PB.copy(S.BASE[name][k]) : given[k], range = S.RANGES[k], ok = true;
    if (k == 'on' || k == 'knockOut' || k == 'reverse' || k == 'dither' || k == 'align') ok = typeof v == 'boolean';
    else if (k == 'blend') ok = !!S.BLNM[v];
    else if (k == 'color') ok = typeof v == 'string' && S.HEX.test(v);
    else if (k == 'position') ok = !!S.POSITION[v];
    else if (k == 'method') ok = !!S.METHOD[v];
    else if (k == 'style') ok = !!S.STYLE[v];
    else if (k == 'offset') ok = v instanceof Array && v.length == 2;
    else if (k == 'gradient') ok = v && v.colors instanceof Array && v.colors.length >= 2;
    else if (name == 'stroke' && k == 'size') ok = typeof v == 'number' && v >= 1 && v <= 250;
    else if (range) ok = typeof v == 'number' && v >= range[0] && v <= range[1];
    if (!ok) throw new Error(name + ': ' + k + ' ' + PB.json(v));
    out[k] = v;
  }
  return out;
};
// A layer's fx: each effect by S.effectOf, in S.FX's order.
S.fx = function (given) {
  var out = {};
  for (var k in given)
    if (given.hasOwnProperty(k) && !S.FX[k]) throw new Error('fx has no effect ' + k);
  for (k in S.FX)
    if (S.FX.hasOwnProperty(k) && given[k] !== undefined) out[k] = S.effectOf(k, given[k]);
  return out;
};
S.isDefaults = function (v) {
  for (var k in v)
    if (v.hasOwnProperty(k) && k != 'on') return false;
  return true;
};

// D30's gradient JSON ({colors: [{p, c: [r, g, b], m}], alphas: [{p, a, m}], smoothness, name}) as Photoshop's Grdn:
// Lctn is p in 4096ths, Mdpn m in % (0.5 by default: these cases leave every midpoint there, which psd.jsx and D30
// read alike), Intr the smoothness in 4096ths.
S.gradient = function (g) {
  var c = S.c, d = new ActionDescriptor(), list = new ActionList(), i, o;
  d.putString(c('Nm  '), g.name || 'Custom');
  d.putEnumerated(c('GrdF'), c('GrdF'), c('CstS'));
  d.putDouble(c('Intr'), S.or(g.smoothness, 100) * 40.96);
  for (i = 0; i < g.colors.length; i++) {
    o = new ActionDescriptor();
    o.putObject(c('Clr '), c('RGBC'), S.rgbc(g.colors[i].c));
    o.putEnumerated(c('Type'), c('Clry'), c('UsrS'));
    o.putInteger(c('Lctn'), Math.round(g.colors[i].p * 4096));
    o.putInteger(c('Mdpn'), Math.round(S.or(g.colors[i].m, 0.5) * 100));
    list.putObject(c('Clrt'), o);
  }
  d.putList(c('Clrs'), list);
  var alphas = g.alphas || [{p: 0, a: 1}, {p: 1, a: 1}];
  list = new ActionList();
  for (i = 0; i < alphas.length; i++) {
    o = new ActionDescriptor();
    o.putUnitDouble(c('Opct'), c('#Prc'), alphas[i].a * 100);
    o.putInteger(c('Lctn'), Math.round(alphas[i].p * 4096));
    o.putInteger(c('Mdpn'), Math.round(S.or(alphas[i].m, 0.5) * 100));
    list.putObject(c('TrnS'), o);
  }
  d.putList(c('Trns'), list);
  return d;
};
S.contour = function () {  // Linear
  var c = S.c, d = new ActionDescriptor(), list = new ActionList();
  d.putString(c('Nm  '), 'Linear');
  for (var i = 0; i < 2; i++) {
    var p = new ActionDescriptor();
    p.putDouble(c('Hrzn'), i * 255);
    p.putDouble(c('Vrtc'), i * 255);
    list.putObject(c('CrPt'), p);
  }
  d.putList(c('Crv '), list);
  return d;
};
// An effect as Photoshop's descriptor: no Global Light, Linear contour, not anti-aliased (P2's, at their defaults). An
// effect of {on} alone: only that it is there, the rest Photoshop's.
S.effect = function (name, v) {
  var c = S.c, s = S.s, d = new ActionDescriptor();
  var unit = function (key, u, x) { d.putUnitDouble(c(key), c(u), x); };
  d.putBoolean(c('enab'), v.on !== false);
  d.putBoolean(s('present'), true);
  d.putBoolean(s('showInDialog'), true);
  if (S.isDefaults(v)) return d;
  d.putEnumerated(c('Md  '), c('BlnM'), S.blnm(v.blend));
  if (v.color !== undefined) d.putObject(c('Clr '), c('RGBC'), S.rgbc(v.color));
  unit('Opct', '#Prc', v.opacity);
  if (name == 'dropShadow') {
    d.putBoolean(c('uglg'), false);
    unit('lagl', '#Ang', v.angle);
    unit('Dstn', '#Pxl', v.distance);
    unit('Ckmt', '#Prc', v.spread);
    unit('blur', '#Pxl', v.size);
    unit('Nose', '#Prc', v.noise);
    d.putBoolean(c('AntA'), false);
    d.putObject(c('TrnS'), c('ShpC'), S.contour());
    d.putBoolean(s('layerConceals'), v.knockOut);
  } else if (name == 'stroke') {
    d.putEnumerated(c('Styl'), c('FStl'), c(S.POSITION[v.position]));
    d.putEnumerated(c('PntT'), c('FrFl'), c('SClr'));
    unit('Sz  ', '#Pxl', v.size);
    d.putBoolean(s('overprint'), false);
  } else if (name == 'gradientOverlay') {
    var off = new ActionDescriptor();
    d.putObject(c('Grad'), c('Grdn'), S.gradient(v.gradient));
    d.putEnumerated(s('gradientsInterpolationMethod'), s('gradientInterpolationMethodType'), c(S.METHOD[v.method]));
    d.putEnumerated(c('Type'), c('GrdT'), c(S.STYLE[v.style]));
    unit('Angl', '#Ang', v.angle);
    unit('Scl ', '#Prc', v.scale);
    d.putBoolean(c('Rvrs'), v.reverse);
    d.putBoolean(c('Dthr'), v.dither);
    d.putBoolean(c('Algn'), v.align);
    off.putUnitDouble(c('Hrzn'), c('#Prc'), v.offset[0]);
    off.putUnitDouble(c('Vrtc'), c('#Prc'), v.offset[1]);
    d.putObject(c('Ofst'), c('Pnt '), off);
  }
  return d;
};
// The active layer's style as fx has it (Scale 100%); returns the Lefx set, to check what Photoshop read.
S.setStyle = function (fx) {
  var c = S.c, d = new ActionDescriptor(), r = new ActionReference(), l = new ActionDescriptor();
  r.putProperty(c('Prpr'), c('Lefx'));
  r.putEnumerated(c('Lyr '), c('Ordn'), c('Trgt'));
  d.putReference(c('null'), r);
  l.putUnitDouble(c('Scl '), c('#Prc'), 100);
  for (var k in S.FX)
    if (S.FX.hasOwnProperty(k) && fx[k]) l.putObject(c(S.FX[k]), c(S.FX[k]), S.effect(k, fx[k]));
  d.putObject(c('T   '), c('Lefx'), l);
  executeAction(c('setd'), d, DialogModes.NO);
  return l;
};

// ---- Reading back (psd.jsx's) --------------------------------------------------------------------------------------

// A layer's descriptor, by its id without changing the active layer; else as the active layer.
S.layerDesc = function (doc, layer) {
  try {
    var r = new ActionReference();
    r.putIdentifier(S.c('Lyr '), layer.id);
    return executeActionGet(r);
  } catch (e) {
    doc.activeLayer = layer;
    return PB.activeLayerDesc();
  }
};
S.flag = function (d, key, def) {
  return d && d.hasKey(key) && d.getType(key) == DescValueType.BOOLEANTYPE ? d.getBoolean(key) : def;
};
S.num = function (d, key, def) {
  if (!d || !d.hasKey(key)) return def;
  var t = d.getType(key);
  if (t == DescValueType.UNITDOUBLE) return d.getUnitDoubleValue(key);
  if (t == DescValueType.DOUBLETYPE) return d.getDouble(key);
  if (t == DescValueType.INTEGERTYPE) return d.getInteger(key);
  return def;
};
S.obj = function (d, key) {
  return d && d.hasKey(key) && d.getType(key) == DescValueType.OBJECTTYPE ? d.getObjectValue(key) : null;
};
S.enumOf = function (d, key) {
  return d && d.hasKey(key) && d.getType(key) == DescValueType.ENUMERATEDTYPE ? d.getEnumerationValue(key) : null;
};
// A value of a descriptor (k a key) or list (k an index) as plain data, to compare.
S.value = function (from, k, t) {
  if (t == DescValueType.BOOLEANTYPE) return from.getBoolean(k);
  if (t == DescValueType.INTEGERTYPE) return from.getInteger(k);
  if (t == DescValueType.DOUBLETYPE) return from.getDouble(k);
  if (t == DescValueType.UNITDOUBLE) return {unit: from.getUnitDoubleType(k), v: from.getUnitDoubleValue(k)};
  if (t == DescValueType.STRINGTYPE) return from.getString(k);
  if (t == DescValueType.ENUMERATEDTYPE) return {enumerated: from.getEnumerationValue(k)};
  if (t == DescValueType.OBJECTTYPE) return {object: from.getObjectValue(k)};
  if (t == DescValueType.LISTTYPE) return {list: from.getList(k)};
  return {other: String(t)};
};
S.numberOf = function (v) { return typeof v == 'number' ? v : v && v.unit !== undefined ? v.v : null; };
S.show = function (v) {
  if (v === undefined || v === null || typeof v != 'object') return String(v);
  if (v.unit !== undefined) return v.v + PB.keyName(v.unit);
  if (v.enumerated !== undefined) return PB.keyName(v.enumerated);
  if (v.object) return '{' + PB.dump(v.object) + '}';
  if (v.list) return 'a list of ' + v.list.count;
  return v.other || 'a value';
};
S.tolerance = function (w, key, v) {
  if (w.unit !== undefined) return w.unit == S.c('#Pxl') ? 0.05 : 0.5;  // % and degrees as the dialogs show them
  if (key !== null && (key == S.c('Rd  ') || key == S.c('Grn ') || key == S.c('Bl  '))) return 0.6;  // a colour
  return 1e-3 + 1e-4 * Math.abs(v);
};
// Each value of want (set) that got (read back) has otherwise: lines into out; keys got lacks, and names (strings)
// otherwise, into absent (noted, not wrong). A list of objects matches in any order, one of values in order.
S.diff = function (want, got, at, out, absent) {
  for (var i = 0; i < want.count; i++) {
    var k = want.getKey(i), name = at + PB.keyName(k);
    if (!got.hasKey(k)) absent.push(name);
    else S.compare(S.value(want, k, want.getType(k)), S.value(got, k, got.getType(k)), name, k, out, absent);
  }
};
S.compare = function (w, g, name, key, out, absent) {
  var wn = S.numberOf(w), gn = S.numberOf(g), off = gn - wn;
  if (wn !== null && gn !== null && w.unit !== undefined && w.unit == S.c('#Ang'))  // -90 may come back as 270
    off = ((gn - wn) % 360 + 540) % 360 - 180;
  if (wn !== null) {
    if (gn === null) out.push(name + ' ' + S.show(g) + ', not ' + S.show(w));
    else if (w.unit !== undefined && g.unit !== undefined && w.unit != g.unit) out.push(name + ' ' + S.show(g) + ', not ' + S.show(w));
    else if (Math.abs(off) > S.tolerance(w, key, wn)) out.push(name + ' ' + gn + ', not ' + wn);
  } else if (typeof w != 'object') {
    if (w !== g && typeof w == 'string' && typeof g == 'string') absent.push(name + ' read back as "' + g + '"');
    else if (w !== g) out.push(name + ' ' + S.show(g) + ', not ' + S.show(w));
  } else if (w.enumerated !== undefined) {
    if (!g || g.enumerated != w.enumerated) out.push(name + ' ' + S.show(g) + ', not ' + S.show(w));
  } else if (w.object) {
    if (!g || !g.object) out.push(name + ' ' + S.show(g) + ', not an object');
    else S.diff(w.object, g.object, name + '.', out, absent);
  } else if (w.list) {
    if (!g || !g.list) out.push(name + ' ' + S.show(g) + ', not a list');
    else S.diffList(w.list, g.list, name, out, absent);
  }
};
S.diffList = function (w, g, name, out, absent) {
  var objects = w.count > 0, i, j, used = [];
  for (i = 0; i < w.count; i++)
    if (w.getType(i) != DescValueType.OBJECTTYPE) objects = false;
  if (!objects) {
    if (w.count != g.count) out.push(name + ' has ' + g.count + ' values, not ' + w.count);
    else
      for (i = 0; i < w.count; i++) S.compare(S.value(w, i, w.getType(i)), S.value(g, i, g.getType(i)), name + '[' + i + ']', null, out, absent);
    return;
  }
  for (i = 0; i < w.count; i++) {
    var found = -1, first = null;
    for (j = 0; j < g.count && found < 0; j++) {
      if (used[j] || g.getType(j) != DescValueType.OBJECTTYPE) continue;
      var lines = [];
      S.diff(w.getObjectValue(i), g.getObjectValue(j), name + '[' + i + '].', lines, []);
      if (!lines.length) found = j;
      else if (!first) first = lines;
    }
    if (found >= 0) {
      used[found] = true;
      S.diff(w.getObjectValue(i), g.getObjectValue(found), name + '[' + i + '].', [], absent);
    } else {
      out.push(name + '[' + i + '] matches none of the ' + g.count + ' read back' + (first ? ' (' + first.join('; ') + ')' : ''));
    }
  }
};
// An effect of a layer's style read back: its object, or the first of the list Photoshop may keep it in.
S.effectIn = function (got, name) {
  var d = S.obj(got, S.c(S.FX[name])), m = S.s(S.MULTI[name]);
  if (!d && got.hasKey(m) && got.getType(m) == DescValueType.LISTTYPE && got.getList(m).count)
    d = got.getList(m).getObjectValue(0);
  return d;
};

// ---- Photoshop's defaults, back into photo-beauty's keys -----------------------------------------------------------

// A colour of a descriptor (RGBC, HSBC or grey) as '#rrggbb'; null for another.
S.colorOf = function (d) {
  var c = S.c;
  if (!d) return null;
  if (d.hasKey(c('Rd  '))) return S.hex([S.num(d, c('Rd  '), 0), S.num(d, c('Grn '), 0), S.num(d, c('Bl  '), 0)]);
  if (d.hasKey(c('Gry '))) {
    var g = 255 * (1 - S.num(d, c('Gry '), 0) / 100);
    return S.hex([g, g, g]);
  }
  if (d.hasKey(c('H   '))) {  // hue in degrees, saturation and brightness in %
    var h = S.num(d, c('H   '), 0) / 60, sat = S.num(d, c('Strt'), 0) / 100, v = S.num(d, c('Brgh'), 0) / 100;
    var i = Math.floor(h) % 6, f = h - Math.floor(h), p = v * (1 - sat), q = v * (1 - sat * f), t = v * (1 - sat * (1 - f));
    var rgb = [[v, t, p], [q, v, p], [p, v, t], [p, q, v], [t, p, v], [v, p, q]][(i + 6) % 6];
    return S.hex([rgb[0] * 255, rgb[1] * 255, rgb[2] * 255]);
  }
  return null;
};
S.nameOf = function (table, id) {  // the key of table whose 4-letter or string ID is id
  for (var k in table)
    if (table.hasOwnProperty(k) && S.id(table[k]) == id) return k;
  return id === null ? null : PB.keyName(id);
};
S.gradientFrom = function (g) {
  var c = S.c, out = {colors: [], alphas: []}, list, i, o;
  if (!g) return null;
  if (g.hasKey(c('Clrs'))) {
    list = g.getList(c('Clrs'));
    for (i = 0; i < list.count; i++) {
      var stop = list.getObjectValue(i), type = PB.keyName(S.enumOf(stop, c('Type')) || c('UsrS'));
      o = {p: S.round(S.num(stop, c('Lctn'), 0) / 4096, 1e6)};
      var col = S.colorOf(S.obj(stop, c('Clr ')));
      if (col) o.c = S.rgb3(col);
      o.m = S.round(S.num(stop, c('Mdpn'), 50) / 100, 1e4);
      if (type == 'FrgC') o.type = 'fg';
      if (type == 'BckC') o.type = 'bg';
      out.colors.push(o);
    }
  }
  if (g.hasKey(c('Trns'))) {
    list = g.getList(c('Trns'));
    for (i = 0; i < list.count; i++) {
      var a = list.getObjectValue(i);
      out.alphas.push({p: S.round(S.num(a, c('Lctn'), 0) / 4096, 1e6), a: S.round(S.num(a, c('Opct'), 100) / 100, 1e4),
                       m: S.round(S.num(a, c('Mdpn'), 50) / 100, 1e4)});
    }
  }
  out.smoothness = S.round(S.num(g, c('Intr'), 4096) / 40.96, 100);
  if (g.hasKey(c('Nm  '))) out.name = g.getString(c('Nm  '));
  return out;
};
// The document's Global Light angle, or null when it cannot be read.
S.globalAngle = function () {
  try {
    var r = new ActionReference();
    r.putProperty(S.c('Prpr'), S.c('gblA'));
    r.putEnumerated(S.c('Dcmn'), S.c('Ordn'), S.c('Trgt'));
    var d = executeActionGet(r), k = S.c('gblA');
    if (d.hasKey(k) && d.getType(k) == DescValueType.OBJECTTYPE) return S.num(d.getObjectValue(k), S.c('gagl'), null);
    return S.num(d, k, null);
  } catch (e) {
    return null;
  }
};
// An effect read back as index.json has effects (S.KEYS); Drop Shadow also says globalLight (and the document's angle,
// globalAngle) when Photoshop's default uses Global Light, which photo-beauty's P1 does not.
S.fxFrom = function (name, d) {
  var c = S.c, s = S.s, o = {on: S.flag(d, c('enab'), true)};
  o.blend = S.blendName(S.enumOf(d, c('Md  ')));
  if (name != 'gradientOverlay') o.color = S.colorOf(S.obj(d, c('Clr ')));
  o.opacity = S.num(d, c('Opct'), null);
  if (name == 'dropShadow') {
    o.angle = S.num(d, c('lagl'), null);
    o.distance = S.num(d, c('Dstn'), null);
    o.spread = S.num(d, c('Ckmt'), null);
    o.size = S.num(d, c('blur'), null);
    o.noise = S.num(d, c('Nose'), null);
    o.knockOut = S.flag(d, s('layerConceals'), true);
    if (S.flag(d, c('uglg'), false)) {
      o.globalLight = true;
      o.globalAngle = S.globalAngle();
    }
  } else if (name == 'stroke') {
    o.size = S.num(d, c('Sz  '), null);
    o.position = S.nameOf(S.POSITION, S.enumOf(d, c('Styl')));
    if (S.enumOf(d, c('PntT')) != c('SClr')) o.fillType = PB.keyName(S.enumOf(d, c('PntT')));
  } else if (name == 'gradientOverlay') {
    o.gradient = S.gradientFrom(S.obj(d, c('Grad')));
    o.method = S.nameOf(S.METHOD, S.enumOf(d, s('gradientsInterpolationMethod')));
    o.style = S.nameOf(S.STYLE, S.enumOf(d, c('Type')));
    o.angle = S.num(d, c('Angl'), null);
    o.scale = S.num(d, c('Scl '), null);
    o.reverse = S.flag(d, c('Rvrs'), false);
    o.dither = S.flag(d, c('Dthr'), false);
    o.align = S.flag(d, c('Algn'), true);
    var off = S.obj(d, c('Ofst'));
    o.offset = [S.num(off, c('Hrzn'), 0), S.num(off, c('Vrtc'), 0)];
  }
  var out = {}, keys = S.KEYS[name], k;  // S.KEYS' order, then what else
  for (var i = 0; i < keys.length; i++)
    if (o[keys[i]] !== undefined) out[keys[i]] = o[keys[i]];
  for (k in o)
    if (o.hasOwnProperty(k) && out[k] === undefined) out[k] = o[k];
  return out;
};

// props.fx on its layer (PB.layerKeys.fx): set, read back and checked; an effect of Photoshop's defaults gets the
// values Photoshop gave it, so index.json has them.
S.applyFx = function (fx, layer, doc, l) {
  doc.activeLayer = layer;
  var want = S.setStyle(fx), got = S.obj(S.layerDesc(doc, layer), S.s('layerEffects')), out = [], absent = [];
  if (!got) throw new Error(l.props.name + ': no layer style read back');
  for (var k in S.FX) {
    if (!S.FX.hasOwnProperty(k) || !fx[k]) continue;
    var g = S.effectIn(got, k);
    if (!g) {
      out.push(k + ' not there');
      continue;
    }
    S.diff(want.getObjectValue(S.c(S.FX[k])), g, k + '.', out, absent);
    if (S.isDefaults(fx[k])) {
      fx[k] = S.fxFrom(k, g);
      PB.note('    ' + k + ', Photoshop\'s defaults: ' + PB.json(fx[k]));
    }
  }
  PB.note('    style read back: ' + PB.dump(got));
  if (absent.length) PB.note('    not read back as set: ' + absent.join(', '));
  if (out.length) throw new Error(l.props.name + ': Photoshop has ' + out.join('; '));
};
PB.layerKeys.fx = S.applyFx;

// ---- Type (Y30) ----------------------------------------------------------------------------------------------------

S.AA = {none: 'NONE', sharp: 'SHARP', crisp: 'CRISP', strong: 'STRONG', smooth: 'SMOOTH'};
S.fonts = null;
// The PostScript name Photoshop has for a font: its own, else the one of that family and style (Photoshop may name the
// instances of the variable SpaceGrotesk.ttf otherwise: README-filters.md), noted. No Space Grotesk: the case fails,
// as another font's images are of no use to Y30.
S.font = function (s) {
  var i, f;
  if (!S.fonts) {
    S.fonts = [];
    for (i = 0; i < app.fonts.length; i++) S.fonts.push(app.fonts[i]);
  }
  for (i = 0; i < S.fonts.length; i++)
    if (S.fonts[i].postScriptName == s.font) return s.font;
  for (i = 0; i < S.fonts.length; i++) {
    f = S.fonts[i];
    if ((f.family == s.family && f.style == s.style) || f.family == s.family + ' ' + s.style) {
      PB.note('    font ' + s.font + ' is ' + f.postScriptName + ' here (' + f.family + ', ' + f.style + ')');
      return f.postScriptName;
    }
  }
  var have = [];
  for (i = 0; i < S.fonts.length; i++)
    if (/grotesk/i.test(S.fonts[i].family)) have.push(S.fonts[i].postScriptName + ' (' + S.fonts[i].family + ', ' + S.fonts[i].style + ')');
  throw new Error('no font ' + s.font + ' (' + s.family + ' ' + s.style + '): install ui/fonts/SpaceGrotesk*.ttf ' +
                  '(README-filters.md); Space Grotesk here: ' + (have.length ? have.join(', ') : 'none'));
};
// A point text layer of source s, every Character setting photo-beauty has set (a new layer takes the Type tool's).
S.makeText = function (doc, s) {
  var layer = doc.artLayers.add(), t, failed = [];
  layer.kind = LayerKind.TEXT;
  t = layer.textItem;
  t.contents = s.text;
  s.font = S.font(s);  // index.json gets the one used
  t.font = s.font;
  t.size = new UnitValue(s.size, 'pt');
  t.color = S.solid(s.color);
  t.tracking = s.tracking;
  t.antiAliasMethod = AntiAlias[S.AA[s.aa]];
  var set = [['autoKerning', function () { t.autoKerning = AutoKernType.METRICS; }],
             ['useAutoLeading', function () { t.useAutoLeading = true; }],
             ['horizontalScale', function () { t.horizontalScale = 100; }],
             ['verticalScale', function () { t.verticalScale = 100; }],
             ['baselineShift', function () { t.baselineShift = 0; }],
             ['fauxBold', function () { t.fauxBold = false; }],
             ['fauxItalic', function () { t.fauxItalic = false; }],
             ['capitalization', function () { t.capitalization = TextCase.NORMAL; }],
             ['underline', function () { t.underline = UnderlineType.UNDERLINEOFF; }],
             ['strikeThru', function () { t.strikeThru = StrikeThruType.STRIKEOFF; }],
             ['justification', function () { t.justification = Justification.LEFT; }]];
  for (var i = 0; i < set.length; i++)
    try {
      set[i][1]();
    } catch (e) {
      failed.push(set[i][0] + ' (' + e + ')');
    }
  if (failed.length) PB.note('    could not set ' + failed.join(', '));
  t.position = [s.at[0], s.at[1]];  // last: the anchor, at the start of the baseline
  PB.note('    text "' + s.text + '": ' + t.font + ' ' + t.size + ', AA ' + t.antiAliasMethod + ', tracking ' + t.tracking +
          ', at ' + t.position[0] + ', ' + t.position[1] + '; bounds ' + layer.bounds.join(' '));
  return layer;
};

// ---- Shapes (Y32): live shapes of Photoshop 2021 (22) -------------------------------------------------------------

S.CAP = {butt: 'strokeStyleButtCap', round: 'strokeStyleRoundCap', square: 'strokeStyleSquareCap'};
S.JOIN = {miter: 'strokeStyleMiterJoin', round: 'strokeStyleRoundJoin', bevel: 'strokeStyleBevelJoin'};
S.ALIGN = {inside: 'strokeStyleAlignInside', center: 'strokeStyleAlignCenter', outside: 'strokeStyleAlignOutside'};
S.strokeStyle = function (src) {
  var c = S.c, s = S.s, d = new ActionDescriptor(), st = src.stroke, content = new ActionDescriptor();
  var e = function (key, value) { d.putEnumerated(s(key), s(key), s(value)); };
  d.putInteger(s('strokeStyleVersion'), 2);
  d.putBoolean(s('strokeEnabled'), !!st);
  d.putBoolean(s('fillEnabled'), src.fill !== null);
  d.putUnitDouble(s('strokeStyleLineWidth'), c('#Pxl'), st ? st.width : 1);
  d.putUnitDouble(s('strokeStyleLineDashOffset'), c('#Pnt'), 0);
  d.putDouble(s('strokeStyleMiterLimit'), 100);
  e('strokeStyleLineCapType', S.CAP[st && st.cap || 'butt']);
  e('strokeStyleLineJoinType', S.JOIN[st && st.join || 'miter']);
  e('strokeStyleLineAlignment', S.ALIGN[st && st.align || 'inside']);
  d.putBoolean(s('strokeStyleScaleLock'), false);
  d.putBoolean(s('strokeStyleStrokeAdjust'), false);
  d.putList(s('strokeStyleLineDashSet'), new ActionList());
  d.putEnumerated(s('strokeStyleBlendMode'), c('BlnM'), c('Nrml'));
  d.putUnitDouble(s('strokeStyleOpacity'), c('#Prc'), 100);
  content.putObject(c('Clr '), c('RGBC'), S.rgbc(st ? st.color : '#000000'));
  d.putObject(s('strokeStyleContent'), s('solidColorLayer'), content);
  d.putDouble(s('strokeStyleResolution'), 72);
  return d;
};
// The shape's geometry as each way of writing it: [[name, class, descriptor], ...], tried in order (Polygon and Line
// are the least sure: README-filters.md).
S.shapeVariants = function (src) {
  var c = S.c, s = S.s, b = src.box, out = [], d;
  var quad = function (q) {
    q.putInteger(s('unitValueQuadVersion'), 1);
    q.putUnitDouble(c('Top '), c('#Pxl'), b[1]);
    q.putUnitDouble(c('Left'), c('#Pxl'), b[0]);
    q.putUnitDouble(c('Btom'), c('#Pxl'), b[3]);
    q.putUnitDouble(c('Rght'), c('#Pxl'), b[2]);
    return q;
  };
  var point = function (p) {
    var q = new ActionDescriptor();
    q.putUnitDouble(c('Hrzn'), c('#Pxl'), p[0]);
    q.putUnitDouble(c('Vrtc'), c('#Pxl'), p[1]);
    return q;
  };
  var polygon = function () {
    var q = quad(new ActionDescriptor());
    q.putInteger(s('sides'), src.sides);
    q.putUnitDouble(s('polygonCornerRadius'), c('#Pxl'), 0);
    q.putDouble(s('starRatio'), S.or(src.star, 100));
    return q;
  };
  if (src.type == 'rectangle') {
    d = quad(new ActionDescriptor());
    var r = src.radii || [0, 0, 0, 0];
    d.putUnitDouble(s('topLeft'), c('#Pxl'), r[0]);
    d.putUnitDouble(s('topRight'), c('#Pxl'), r[1]);
    d.putUnitDouble(s('bottomRight'), c('#Pxl'), r[2]);
    d.putUnitDouble(s('bottomLeft'), c('#Pxl'), r[3]);
    out.push(['Rctn', c('Rctn'), d]);
  } else if (src.type == 'ellipse') {
    out.push(['Elps', c('Elps'), quad(new ActionDescriptor())]);
  } else if (src.type == 'polygon') {
    out.push(['polygon', s('polygon'), polygon()]);
    out.push(['Plgn', c('Plgn'), polygon()]);
  } else if (src.type == 'line') {
    d = new ActionDescriptor();  // the Line tool's, as before 2021
    d.putObject(c('Strt'), c('Pnt '), point(src.from));
    d.putObject(c('End '), c('Pnt '), point(src.to));
    d.putUnitDouble(c('Wdth'), c('#Pxl'), src.weight);
    out.push(['Ln  ', c('Ln  '), d]);
    d = new ActionDescriptor();
    d.putObject(s('lineStart'), c('Pnt '), point(src.from));
    d.putObject(s('lineEnd'), c('Pnt '), point(src.to));
    d.putUnitDouble(s('lineWeight'), c('#Pxl'), src.weight);
    out.push(['line', s('line'), d]);
  } else {
    throw new Error('no shape ' + src.type);
  }
  return out;
};
// A shape layer of source src, made above the active layer: the new layer.
S.makeShape = function (doc, src) {
  var c = S.c, s = S.s, variants = S.shapeVariants(src), errors = [];
  for (var i = 0; i < variants.length; i++) {
    var d = new ActionDescriptor(), r = new ActionReference(), use = new ActionDescriptor(), fill = new ActionDescriptor();
    fill.putObject(c('Clr '), c('RGBC'), S.rgbc(src.fill || '#000000'));
    use.putObject(c('Type'), s('solidColorLayer'), fill);
    use.putObject(c('Shp '), variants[i][1], variants[i][2]);
    use.putObject(s('strokeStyle'), s('strokeStyle'), S.strokeStyle(src));
    r.putClass(s('contentLayer'));
    d.putReference(c('null'), r);
    d.putObject(c('Usng'), s('contentLayer'), use);
    try {
      executeAction(c('Mk  '), d, DialogModes.NO);
      PB.note('    ' + src.type + ': made as ' + variants[i][0] + (errors.length ? ' (' + errors.join('; ') + ')' : '') +
              '; bounds ' + doc.activeLayer.bounds.join(' '));
      return doc.activeLayer;
    } catch (e) {
      errors.push(variants[i][0] + ': ' + e);
    }
  }
  throw new Error('no ' + src.type + ' made: ' + errors.join('; '));
};

// ---- Smart objects (L35) -------------------------------------------------------------------------------------------

// A31's transform of a smart object placed {scale, rotate} about the centre (cx, cy): T(c) R S T(-c), by rows, 6
// decimals. y points down, so a clockwise turn is [cos -sin; sin cos].
S.transform = function (place, cx, cy) {
  var k = S.or(place.scale, 100) / 100, a = S.or(place.rotate, 0) * Math.PI / 180;
  var m = [k * Math.cos(a), -k * Math.sin(a), 0, k * Math.sin(a), k * Math.cos(a), 0, 0, 0, 1];
  m[2] = cx - m[0] * cx - m[1] * cy;
  m[5] = cy - m[3] * cx - m[4] * cy;
  for (var i = 0; i < 9; i++) m[i] = S.round(m[i], 1e6) + 0;  // + 0: no -0
  return m;
};
// What Photoshop has of a smart object: from the corners of its contents in the document (top left, top right, bottom
// right, bottom left) and their size (psd.jsx's E.smartInfo, E.affine); null when it does not say.
S.smartTransform = function (doc, layer) {
  var more = S.obj(S.layerDesc(doc, layer), S.s('smartObjectMore')), k = S.s('transform');
  if (!more || !more.hasKey(k)) return null;
  var t = more.getList(k), size = S.obj(more, S.s('size')), q = [], i;
  if (t.count != 8 || !size) return null;
  for (i = 0; i < 8; i++) q.push(t.getDouble(i));
  var w = S.num(size, S.c('Wdth'), 0), h = S.num(size, S.c('Hght'), 0);
  if (!w || !h) return null;
  var m = [(q[2] - q[0]) / w, (q[6] - q[0]) / h, q[0], (q[3] - q[1]) / w, (q[7] - q[1]) / h, q[1], 0, 0, 1];
  for (i = 0; i < 9; i++) m[i] = S.round(m[i], 1e6) + 0;
  PB.note('    smart object: corners ' + q.join(' ') + ', contents ' + w + ' x ' + h);
  return m;
};
// A smart object of an input: its layer copied, Convert to Smart Object, then scaled and turned about the centre;
// props.transform becomes what Photoshop read back (noted when it is not the one the case asked).
S.makeSmart = function (l, doc) {
  var layer = PB.copyLayer(doc, l.src, 0), t = l.place || {};
  doc.activeLayer = layer;
  executeAction(S.s('newPlacedLayer'), undefined, DialogModes.NO);
  layer = doc.activeLayer;
  if (layer.kind != LayerKind.SMARTOBJECT) throw new Error(l.props.name + ': not converted to a smart object');
  if (t.scale) layer.resize(t.scale, t.scale, AnchorPosition.MIDDLECENTER);
  if (t.rotate) layer.rotate(t.rotate, AnchorPosition.MIDDLECENTER);
  var got = S.smartTransform(doc, layer), want = l.props.transform;
  if (!got) {
    PB.note('    smart object: no transform read back; index.json keeps ' + PB.json(want));
  } else {
    var off = 0;
    for (var i = 0; i < 9; i++) off = Math.max(off, Math.abs(got[i] - want[i]) * (i == 2 || i == 5 ? 1 : 256));
    if (off > 0.01) PB.note('    smart object: Photoshop has transform ' + PB.json(got) + ', not ' + PB.json(want) + ' (' + off + ' px)');
    l.props.transform = got;
  }
  return layer;
};

PB.kinds.text = function (l, doc) { return S.makeText(doc, l.props.source); };
PB.kinds.shape = function (l, doc) { return S.makeShape(doc, l.props.source); };
PB.kinds.so = S.makeSmart;
PB.layerKeys.source = function () {};  // PB.kinds' text and shape make the layer of it
PB.layerKeys.transform = function () {};  // PB.kinds.so's: read back there
PB.layerKeys.place = function () {};

// ---- The cases -----------------------------------------------------------------------------------------------------

S.SQUARE = [256, 256];
S.REGULAR = {font: 'SpaceGrotesk-Regular', family: 'Space Grotesk', style: 'Regular'};  // of SpaceGrotesk.ttf
S.SEMIBOLD = {font: 'SpaceGrotesk-SemiBold', family: 'Space Grotesk', style: 'SemiBold'};  // SpaceGrotesk-SemiBold.ttf
// Type's source: S.REGULAR's font unless face says, black, tracking 0, smooth unless said.
S.text = function (text, size, at, more) {
  var face = more && more.face || S.REGULAR;
  var s = {text: text, font: face.font, family: face.family, style: face.style, size: size, color: '#000000',
           tracking: more && more.tracking || 0, aa: more && more.aa || 'smooth', at: at};
  if (!S.AA[s.aa]) throw new Error('no anti-aliasing ' + s.aa);
  return s;
};

function cases() {
  var c = [], i, j;
  var shapes = function (props, more) {
    var l = {kind: 'pixel', src: 'shapes.png', props: props || {}};
    l.props.name = 'shapes';
    if (more && more.mask) l.mask = more.mask;
    return l;
  };
  var ramp = function () { return {kind: 'pixel', src: 'ramp.png', props: {name: 'ramp'}}; };
  // shapes.png with a style (props.fx of fx), over a transparent canvas or (over) ramp.png.
  var styled = function (name, fx, props, more) {
    var l = shapes(props, more);
    l.props.fx = S.fx(fx);
    c.push({name: name, canvas: S.SQUARE, layers: more && more.over ? [ramp(), l] : [l]});
  };
  var over = {over: true};

  // Photoshop's defaults of each effect (L30 takes them): index.json gets the values Photoshop gave it.
  for (var k in S.FX)
    if (S.FX.hasOwnProperty(k)) {
      var d = {};
      d[k] = 'defaults';
      styled('defaults-' + k, d);
    }
  // Drop Shadow (L32): angle and distance (120 degrees: light from the top left), Spread, Size (0 a hard copy, 50 a
  // blur of the shape alone), black Multiply 75% over nothing (the alpha is its cover); and the prototype's over
  // ramp.png.
  var shadow = function (angle, distance, spread, size) {
    return {dropShadow: {color: '#000000', opacity: 75, angle: angle, distance: distance, spread: spread, size: size}};
  };
  styled('shadow-a120-d12-z24', shadow(120, 12, 0, 24));
  styled('shadow-a30-d20-z10', shadow(30, 20, 0, 10));
  styled('shadow-a-90-d8-z4', shadow(-90, 8, 0, 4));
  styled('shadow-a45-d10-z0', shadow(45, 10, 0, 0));
  styled('shadow-spread50-z20', shadow(120, 6, 50, 20));
  styled('shadow-spread100-z12', shadow(120, 6, 100, 12));
  styled('shadow-d0-z50', shadow(120, 0, 0, 50));
  styled('shadow-ramp', {dropShadow: {opacity: 60}}, {}, over);
  // Stroke (L31): Size 1, 3, 10, 50 at each position; Multiply 50% over ramp.png.
  var sizes = [1, 3, 10, 50], positions = ['outside', 'inside', 'center'];
  for (i = 0; i < positions.length; i++)
    for (j = 0; j < sizes.length; j++)
      styled('stroke-' + positions[i] + '-' + sizes[j], {stroke: {size: sizes[j], position: positions[i], color: '#e0402a'}});
  styled('stroke-ramp-center6-multiply50', {stroke: {size: 6, position: 'center', blend: 'multiply', opacity: 50,
                                                     color: '#e0402a'}}, {}, over);
  // Color Overlay over ramp.png in a few modes (L34: onto all below where the layer is not opaque, the soft dot).
  var modes = ['normal', 'multiply', 'screen', 'overlay', 'difference', 'luminosity'];
  for (i = 0; i < modes.length; i++)
    styled('coloroverlay-' + modes[i], {colorOverlay: {blend: modes[i], color: '#2f9e6a'}}, {}, over);
  styled('coloroverlay-normal-60', {colorOverlay: {color: '#2f9e6a', opacity: 60}}, {}, over);
  // Gradient Overlay over ramp.png: the five styles, angle, scale, reverse, not aligned with the layer and moved, a
  // mode; and Photoshop's Perceptual with Smoothness 100% (D30's methods).
  var styles = ['linear', 'radial', 'angle', 'reflected', 'diamond'];
  for (i = 0; i < styles.length; i++) styled('gradient-' + styles[i], {gradientOverlay: {style: styles[i]}}, {}, over);
  styled('gradient-linear-a30-s50-reverse', {gradientOverlay: {angle: 30, scale: 50, reverse: true}}, {}, over);
  styled('gradient-radial-unaligned-offset', {gradientOverlay: {style: 'radial', align: false, scale: 80, offset: [20, -10]}},
         {}, over);
  styled('gradient-linear-multiply-60', {gradientOverlay: {blend: 'multiply', opacity: 60}}, {}, over);
  var smooth = PB.copy(S.GRADIENT);
  smooth.smoothness = 100;
  styled('gradient-perceptual', {gradientOverlay: {gradient: smooth, method: 'perceptual'}}, {}, over);
  // Fill 0 (the effects alone: Blend Interior Effects as Group off), Layer Knocks Out Drop Shadow off, Opacity 50% (the
  // whole), Fill 50% in a special mode (B31's rule), a layer mask (Layer Mask Hides Effects off: the effects follow the
  // masked shape), everything at once (L34's order).
  var hard = {color: '#000000', opacity: 75, distance: 12, size: 16};
  styled('fill0-shadow', {dropShadow: hard}, {fill: 0});
  styled('fill0-shadow-noknockout', {dropShadow: {color: '#000000', opacity: 75, distance: 12, size: 16, knockOut: false}},
         {fill: 0});
  styled('fill0-stroke-coloroverlay', {stroke: {size: 4, color: '#e0402a'}, colorOverlay: {color: '#2f9e6a'}}, {fill: 0});
  styled('opacity50-shadow-stroke', {dropShadow: hard, stroke: {size: 4, color: '#e0402a'}}, {opacity: 0.5});
  styled('fill50-colorDodge-shadow', {dropShadow: hard}, {blend: 'colorDodge', fill: 0.5}, over);
  styled('mask-shadow-stroke', {dropShadow: hard, stroke: {size: 4, color: '#e0402a'}}, {}, {mask: 'alpha.png'});
  styled('all-effects', {dropShadow: {}, stroke: {size: 4, position: 'center', color: '#e0402a'},
                         colorOverlay: {blend: 'multiply', color: '#2f9e6a', opacity: 50}, gradientOverlay: {}}, {}, over);

  // Type (Y30): the five anti-aliasings at 32 pt, 12 and 72 pt, tracking both ways, Vietnamese (letters with two marks,
  // a mark below), the static SemiBold. Canvases of their own size; the text's colour black, so its alpha is its cover.
  var text = function (name, canvas, s) { c.push({name: name, canvas: canvas, layers: [{kind: 'text', props: {name: 'text', source: s}}]}); };
  for (k in S.AA)
    if (S.AA.hasOwnProperty(k)) text('text-aa-' + k, [240, 48], S.text('Photo Beauty', 32, [8, 34], {aa: k}));
  text('text-size12', [128, 20], S.text('Photo Beauty 2026', 12, [4, 14]));
  text('text-size72', [216, 96], S.text('Pb 72', 72, [8, 72]));
  text('text-tracking200', [224, 44], S.text('Track +200', 28, [8, 30], {tracking: 200}));
  text('text-tracking-60', [144, 44], S.text('Track -60', 28, [8, 30], {tracking: -60}));
  text('text-vietnamese', [264, 48], S.text('Tiếng Việt: ố ữ ặ ỷ', 28, [8, 34]));
  text('text-semibold', [232, 48], S.text('SemiBold Ảnh', 32, [8, 34], {face: S.SEMIBOLD}));
  // Shapes (Y32): rounded rectangles (one radius, four), an ellipse, a hexagon and a star, a line; a stroke at each
  // alignment over the fill, an ellipse with a stroke and no fill.
  var shape = function (name, s) { c.push({name: name, canvas: S.SQUARE, layers: [{kind: 'shape', props: {name: 'shape', source: s}}]}); };
  var blue = '#3a7bd5';
  shape('shape-rectangle-r24', {type: 'rectangle', box: [32, 48, 224, 208], radii: [24, 24, 24, 24], fill: blue, stroke: null});
  shape('shape-rectangle-radii', {type: 'rectangle', box: [32, 48, 224, 208], radii: [0, 12, 40, 6], fill: blue, stroke: null});
  shape('shape-ellipse', {type: 'ellipse', box: [40, 56, 216, 200], fill: blue, stroke: null});
  shape('shape-polygon6', {type: 'polygon', box: [48, 48, 208, 208], sides: 6, star: 100, fill: blue, stroke: null});
  shape('shape-star5', {type: 'polygon', box: [48, 48, 208, 208], sides: 5, star: 50, fill: blue, stroke: null});
  shape('shape-line', {type: 'line', from: [40, 208], to: [216, 64], weight: 8, fill: blue, stroke: null});
  for (i = 0; i < positions.length; i++)
    shape('shape-stroke-' + positions[i], {type: 'rectangle', box: [32, 48, 224, 208], radii: [24, 24, 24, 24], fill: blue,
          stroke: {width: 10, color: '#f2a33a', align: positions[i], cap: 'butt', join: 'miter'}});
  shape('shape-ellipse-stroke-only', {type: 'ellipse', box: [40, 56, 216, 200], fill: null,
        stroke: {width: 6, color: '#2b1d4e', align: 'center', cap: 'butt', join: 'miter'}});
  // Smart objects (L35): photo.png's layer converted, then scaled 50%, turned 30 degrees clockwise, both, about the
  // canvas's centre; Image Interpolation Bicubic Automatic (Photoshop's default: this script sets it while it runs).
  var places = [['so-scale50', {scale: 50}], ['so-rotate30', {rotate: 30}], ['so-scale50-rotate30', {scale: 50, rotate: 30}]];
  for (i = 0; i < places.length; i++)
    c.push({name: places[i][0], canvas: S.SQUARE, layers: [{kind: 'so', src: 'photo.png', place: places[i][1],
            props: {name: 'photo', transform: S.transform(places[i][1], 128, 128)}}]});
  // A layer mask feathered (B36: sigma of the Feather radius): ramp.png masked by shapes.png's alpha (hard edges, a
  // 1-pixel line: the kernel itself), Feather 10 and 3 px.
  var feathers = [10, 3];
  for (i = 0; i < feathers.length; i++)
    c.push({name: 'mask-feather' + feathers[i], canvas: S.SQUARE, layers: [{kind: 'pixel', src: 'ramp.png', mask: 'shapes.png',
            props: {name: 'ramp', maskFeather: feathers[i]}}]});
  return c;
}

// Image Interpolation as the smart objects need it while the script runs (Photoshop's default), then back.
S.run = function () {
  var was = null;
  try {
    was = app.preferences.interpolation;
    if (ResampleMethod.BICUBICAUTOMATIC !== undefined) app.preferences.interpolation = ResampleMethod.BICUBICAUTOMATIC;
    PB.note('Image Interpolation: ' + was + ' before, ' + app.preferences.interpolation + ' while this runs');
  } catch (e) {
    PB.note('Image Interpolation: cannot read or set it (' + e + ')');
  }
  try {
    PB.run('styles', cases(), {bits: 8, dir: HERE});
  } finally {
    try {
      if (was !== null) app.preferences.interpolation = was;
    } catch (e) {}
  }
};
if (typeof app != 'undefined') S.run();  // jscheck runs it without Photoshop
