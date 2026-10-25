// Photoshop's images of Image Size, Free Transform, Warp, Image Rotation and Canvas Size (T38), for the tests of T33
// (resample()), X30 (sampling through an inverse map), X31 (Free Transform), X32 (Warp) and T34 (Canvas Size, Image
// Rotation); README-resample.md has the steps. Writes out/resample/ next to this script: <case>.png, index.json (each
// case as photo-beauty's scene and the operations on it, tests/golden.h reads it), photoshop.txt. Inputs from
// resample-in/ (tests/golden/ps/resample/in, pb-bench ps-resample's): checker.png (squares of 10 px, and checks of 1, 2
// and 5 px), ramp.png (R and G where each pixel's centre is, B a curve), photo.png, 100 x 70, RGB; checker-pad.png,
// ramp-pad.png (its B 0.5), photo-pad.png, the same at (30, 29) of a transparent 160 x 128: the box a transform or a
// warp works on, room around it for what they make of it. 16-bit images but the photograph's: 8 bits, the 16-bit
// result rounded, no dither (decision 6 of docs/p1-tasks.md), as it is compared in dE2000 or to a level of 8 bits; the
// kernels and the maps are read off the checks and ramps at 16 bits. 177 cases, about 2.2 MB, about 12 minutes. Not
// run in Photoshop yet: what it is unsure of is in README-resample.md.
//
// A case's keys past lib.jsx's (PB.steps, PB.layerKeys):
//   ops: what is done to the document once its layer is in, in order, each as photo-beauty's UI asks it of Docs.run()
//     (ImageSizeDialog.qml, CanvasSizeDialog.qml, RotateCanvasDialog.qml), with what photo-beauty's core needs:
//     {op: 'imageSize', w, h (px), method}: Image > Image Size; method as FilterSpec.RESAMPLE's keys, and
//       preserveDetails, Photoshop's Preserve Details (enlargement) at Reduce Noise 0.
//     {op: 'rotateCanvas', angle (degrees clockwise), interp, color}: Image > Image Rotation > Arbitrary, interp the
//       Image Interpolation of Preferences while it ran; color '#rrggbb', what a Background shows where it turned
//       from (Photoshop's Background colour), null for a layer that is none.
//     {op: 'canvasSize', w, h (px), anchor (0-8 by rows from the top left, as AnchorGrid's), color}: Image > Canvas
//       Size; color the extension of a Background, null for a layer that is none.
//     {op: 'freeTransform', w, h (%), angle (degrees clockwise), skew [h, v] (degrees), corners (where the box's
//       corners go: top left, top right, bottom right, bottom left, x and y, for Perspective and Distort), interp,
//       transform, warp}: Edit > Free Transform, the fields of its options bar, about the box's centre.
//     {op: 'warp', style (R.STYLES or custom), bend, h, v (%: Bend, H and V Distortion), orientation (horizontal,
//       vertical), interp, warp}: Edit > Transform > Warp.
//     interp: as LayerWarp's (render.h's Interp): nearestNeighbor, bilinear, bicubic, bicubicSmoother, bicubicSharper,
//       automatic (Bicubic Automatic). transform: A31's matrix, 9 numbers by rows from the layer's pixels as they were
//       to the document's, x' = (m0 x + m1 y + m2) / (m6 x + m7 y + m8), corners of pixels at whole numbers. warp:
//       A30's LayerWarp as X30 reads it, {matrix (document to content: transform's inverse), mesh (A30's BezierMesh:
//       cols, rows, xs, ys, points [x, y] row by row, in the document's pixels), interp}; mesh for a custom warp, and
//       for a style when Photoshop gives its own (read back off a smart object's copy given the same warp).
//     Offline (PB.offline, the sample index) transform and warp are this script's guesses; PB.run puts in what
//     Photoshop read back of the same transform on a smart object's copy (photoshop.txt notes where they differ).
//   size: [w, h] of the image the operations leave (Photoshop's, after B33).
// A layer's props.background: it is made the Background (named so) before the operations (A40's key).
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
#include "p1/lib.jsx"

var R = {};

// ---- Small things --------------------------------------------------------------------------------------------------

R.c = function (s) { return charIDToTypeID(s); };
R.s = function (s) { return stringIDToTypeID(s); };
R.id = function (s) { return s.length == 4 ? R.c(s) : R.s(s); };  // 4 letters: a char ID, else a string ID
R.round = function (v, k) { return Math.round(v * k) / k + 0; };  // + 0: no -0
R.num = function (d, key, def) {  // a number of a descriptor (key) or a list (index)
  if (!d || (d.hasKey && !d.hasKey(key))) return def;
  var t = d.getType(key);
  if (t == DescValueType.UNITDOUBLE) return d.getUnitDoubleValue(key);
  if (t == DescValueType.DOUBLETYPE) return d.getDouble(key);
  if (t == DescValueType.INTEGERTYPE) return d.getInteger(key);
  return def;
};
R.obj = function (d, key) {
  return d && d.hasKey(key) && d.getType(key) == DescValueType.OBJECTTYPE ? d.getObjectValue(key) : null;
};
R.list = function (d, key) {
  return d && d.hasKey(key) && d.getType(key) == DescValueType.LISTTYPE ? d.getList(key) : null;
};
R.solid = function (hex) {  // '#rrggbb'
  var c = new SolidColor();
  c.rgb.red = parseInt(hex.substr(1, 2), 16);
  c.rgb.green = parseInt(hex.substr(3, 2), 16);
  c.rgb.blue = parseInt(hex.substr(5, 2), 16);
  return c;
};
// Tries the ways of doing a thing in turn until one goes ([[name, function], ...]): what Action Manager takes is not
// sure for some (README-resample.md); photoshop.txt gets the one that ran and why the others did not.
R.tryEach = function (what, ways) {
  var errors = [];
  for (var i = 0; i < ways.length; i++) {
    try {
      ways[i][1]();
      PB.note('    ' + what + ': ran as ' + ways[i][0] + (errors.length ? ' (' + errors.join('; ') + ')' : ''));
      return ways[i][0];
    } catch (e) {
      errors.push(ways[i][0] + ': ' + e);
    }
  }
  throw new Error(what + ' did not run: ' + errors.join('; '));
};

// ---- Matrices: 3 x 3 by rows, x' = (m0 x + m1 y + m2) / (m6 x + m7 y + m8) ------------------------------------------

R.IDENTITY = [1, 0, 0, 0, 1, 0, 0, 0, 1];
R.mul = function (p, q) {
  var out = [];
  for (var r = 0; r < 3; r++)
    for (var c = 0; c < 3; c++) out.push(p[3 * r] * q[c] + p[3 * r + 1] * q[3 + c] + p[3 * r + 2] * q[6 + c]);
  return out;
};
R.scaled = function (m) {  // m8 made 1
  var out = [];
  for (var i = 0; i < 9; i++) out.push(m[i] / m[8]);
  return out;
};
R.invert = function (m) {
  var a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], k = m[8];
  return R.scaled([e * k - f * h, c * h - b * k, b * f - c * e, f * g - d * k, a * k - c * g, c * d - a * f,
                   d * h - e * g, b * g - a * h, a * e - b * d]);
};
R.about = function (m, cx, cy) { return R.mul([1, 0, cx, 0, 1, cy, 0, 0, 1], R.mul(m, [1, 0, -cx, 0, 1, -cy, 0, 0, 1])); };
R.apply = function (m, x, y) {
  var w = m[6] * x + m[7] * y + m[8];
  return [(m[0] * x + m[1] * y + m[2]) / w, (m[3] * x + m[4] * y + m[5]) / w];
};
R.rounded = function (m) {  // as index.json gets them
  var out = [];
  for (var i = 0; i < m.length; i++) out.push(R.round(m[i], 1e9));
  return out;
};
// The projective map taking four points to four (8 numbers each, x and y): m8 1. Gauss-Jordan on the 8 x 8 system.
R.homography = function (from, to) {
  var a = [], i, j, r;
  for (i = 0; i < 4; i++) {
    var x = from[2 * i], y = from[2 * i + 1], u = to[2 * i], v = to[2 * i + 1];
    a.push([x, y, 1, 0, 0, 0, -u * x, -u * y, u]);
    a.push([0, 0, 0, x, y, 1, -v * x, -v * y, v]);
  }
  for (i = 0; i < 8; i++) {
    var p = i;
    for (r = i + 1; r < 8; r++)
      if (Math.abs(a[r][i]) > Math.abs(a[p][i])) p = r;
    var t = a[i];
    a[i] = a[p];
    a[p] = t;
    if (Math.abs(a[i][i]) < 1e-12) throw new Error('no projective map takes ' + from.join(' ') + ' to ' + to.join(' '));
    for (r = 0; r < 8; r++) {
      if (r == i) continue;
      var f = a[r][i] / a[i][i];
      for (j = i; j < 9; j++) a[r][j] -= f * a[i][j];
    }
  }
  var m = [];
  for (i = 0; i < 8; i++) m.push(a[i][8] / a[i][i]);
  m.push(1);
  return m;
};
R.boxCorners = function (box) {  // [left, top, right, bottom]: top left, top right, bottom right, bottom left
  return [box[0], box[1], box[2], box[1], box[2], box[3], box[0], box[3]];
};
R.cornersOf = function (m, box) {  // where m takes the box's corners
  var q = R.boxCorners(box), out = [];
  for (var i = 0; i < 4; i++) {
    var p = R.apply(m, q[2 * i], q[2 * i + 1]);
    out.push(p[0], p[1]);
  }
  return out;
};
R.far = function (p, q) {  // the largest difference of two lists of numbers
  var d = 0;
  for (var i = 0; i < p.length; i++) d = Math.max(d, Math.abs(p[i] - q[i]));
  return d;
};

// ---- Photoshop's names ---------------------------------------------------------------------------------------------

// Interpolation by photo-beauty's names: [the DOM's ResampleMethod, Action Manager's Intp]. automatic is Bicubic
// Automatic (Free Transform's, Preferences'); Image Size's Automatic (R.SIZE_AUTOMATIC) is its own entry of the dialog.
R.INTERP = {
  nearestNeighbor: ['NEARESTNEIGHBOR', 'Nrst'], bilinear: ['BILINEAR', 'Blnr'], bicubic: ['BICUBIC', 'Bcbc'],
  bicubicSmoother: ['BICUBICSMOOTHER', 'bicubicSmoother'], bicubicSharper: ['BICUBICSHARPER', 'bicubicSharper'],
  automatic: ['BICUBICAUTOMATIC', 'bicubicAutomatic'], preserveDetails: ['PRESERVEDETAILS', 'preserveDetailsUpscale']
};
R.SIZE_AUTOMATIC = ['AUTOMATIC', 'automaticInterpolation'];
R.method = function (name, imageSize) {
  if (!R.INTERP[name]) throw new Error('no interpolation ' + name);
  return imageSize && name == 'automatic' ? R.SIZE_AUTOMATIC : R.INTERP[name];
};
// Warp's styles of P1 (X32's, ToolSpec.js' warpStyle but Custom) by photo-beauty's names: Photoshop's are warp<Name>.
R.STYLES = ['arc', 'arcLower', 'arcUpper', 'arch', 'bulge', 'shellLower', 'shellUpper', 'flag', 'wave', 'fish', 'rise',
            'fisheye', 'inflate', 'squeeze', 'twist'];
R.styleId = function (style) { return 'warp' + style.charAt(0).toUpperCase() + style.substr(1); };
R.ANCHORS = ['TOPLEFT', 'TOPCENTER', 'TOPRIGHT', 'MIDDLELEFT', 'MIDDLECENTER', 'MIDDLERIGHT', 'BOTTOMLEFT',
             'BOTTOMCENTER', 'BOTTOMRIGHT'];  // AnchorPosition's, by rows from the top left as AnchorGrid's 0-8

// Preferences > General > Image Interpolation, what Image Rotation, and Free Transform and Warp where their event
// takes no interpolation, use. R.run puts the user's back.
R.interpolation = function (name) {
  var m = ResampleMethod[R.method(name, false)[0]];
  if (m === undefined) throw new Error('this Photoshop has no ResampleMethod.' + R.method(name, false)[0]);
  app.preferences.interpolation = m;
};

// ---- Reading a layer -----------------------------------------------------------------------------------------------

R.bounds = function (layer) {  // [left, top, right, bottom], px
  var b = layer.bounds, out = [];
  for (var i = 0; i < 4; i++) out.push(b[i].as('px'));
  return out;
};
R.layerDesc = function (doc, layer) {
  try {
    var r = new ActionReference();
    r.putIdentifier(R.c('Lyr '), layer.id);
    return executeActionGet(r);
  } catch (e) {
    doc.activeLayer = layer;
    return PB.activeLayerDesc();
  }
};
// A smart object's corners in the document (top left, top right, bottom right, bottom left: smartObjectMore's
// transform, psd.jsx's E.smartInfo) and the rest of what Photoshop says of it; null when it does not.
R.smart = function (doc, layer) {
  var more = R.obj(R.layerDesc(doc, layer), R.s('smartObjectMore')), t = R.list(more, R.s('transform'));
  if (!t || t.count != 8) return null;
  var q = [];
  for (var i = 0; i < 8; i++) q.push(R.num(t, i, 0));
  return {corners: q, more: more};
};

// ---- Free Transform and Warp: the event --------------------------------------------------------------------------

// Edit > Free Transform's event on the active layer, about the box's centre (Reference Point in the middle); the
// rest is put in by the caller.
R.trnf = function () {
  var d = new ActionDescriptor(), r = new ActionReference(), o = new ActionDescriptor();
  r.putEnumerated(R.c('Lyr '), R.c('Ordn'), R.c('Trgt'));
  d.putReference(R.c('null'), r);
  d.putEnumerated(R.c('FTcs'), R.c('QCSt'), R.c('Qcsa'));
  o.putUnitDouble(R.c('Hrzn'), R.c('#Pxl'), 0);
  o.putUnitDouble(R.c('Vrtc'), R.c('#Pxl'), 0);
  d.putObject(R.c('Ofst'), R.c('Ofst'), o);
  return d;
};
R.point = function (x, y, unit) {
  var p = new ActionDescriptor();
  p.putUnitDouble(R.c('Hrzn'), R.c(unit || '#Pxl'), x);
  p.putUnitDouble(R.c('Vrtc'), R.c(unit || '#Pxl'), y);
  return p;
};
// The ways an op's transform may be put to the event ([[name, function () -> descriptor]]): the options bar's fields;
// or the corners, as offsets from the box's (quadCorner0-3, Ofst), else as points: which Photoshop takes is not sure
// (README-resample.md), and a smart object's copy shows where each puts them.
R.transformWays = function (op, box) {
  if (!op.corners)
    return [['W, H, angle, skew', function () {
      var d = R.trnf();
      if (op.w !== undefined) d.putUnitDouble(R.c('Wdth'), R.c('#Prc'), op.w);
      if (op.h !== undefined) d.putUnitDouble(R.c('Hght'), R.c('#Prc'), op.h);
      if (op.angle) d.putUnitDouble(R.c('Angl'), R.c('#Ang'), op.angle);
      if (op.skew) d.putObject(R.c('Skew'), R.c('Pnt '), R.point(op.skew[0], op.skew[1], '#Ang'));
      return d;
    }]];
  var from = R.boxCorners(box), to = op.corners;
  var quad = function (absolute) {
    return function () {
      var d = R.trnf();
      for (var i = 0; i < 4; i++)
        d.putObject(R.s('quadCorner' + i), R.c(absolute ? 'Pnt ' : 'Ofst'),
                    R.point(to[2 * i] - (absolute ? 0 : from[2 * i]), to[2 * i + 1] - (absolute ? 0 : from[2 * i + 1])));
      return d;
    };
  };
  return [['quadCorner0-3 offsets', quad(false)], ['quadCorner0-3 points', quad(true)]];
};
// Warp's part of the event: a style with its values; a custom mesh's 16 points row by row in the document's pixels
// (meshPoints a list of points, else one point of two lists: which Photoshop takes is not sure) with the box.
R.warpDesc = function (op, box, form) {
  var w = new ActionDescriptor(), custom = op.style == 'custom';
  w.putEnumerated(R.s('warpStyle'), R.s('warpStyle'), R.s(custom ? 'warpCustom' : R.styleId(op.style)));
  w.putDouble(R.s('warpValue'), custom ? 0 : op.bend);
  w.putDouble(R.s('warpPerspective'), custom ? 0 : op.h);
  w.putDouble(R.s('warpPerspectiveOther'), custom ? 0 : op.v);
  w.putEnumerated(R.s('warpRotate'), R.c('Ornt'), R.c(!custom && op.orientation == 'vertical' ? 'Vrtc' : 'Hrzn'));
  if (!custom) return w;
  var b = new ActionDescriptor(), e = new ActionDescriptor(), pts = op.warp.mesh.points, i;
  b.putUnitDouble(R.c('Top '), R.c('#Pxl'), box[1]);
  b.putUnitDouble(R.c('Left'), R.c('#Pxl'), box[0]);
  b.putUnitDouble(R.c('Btom'), R.c('#Pxl'), box[3]);
  b.putUnitDouble(R.c('Rght'), R.c('#Pxl'), box[2]);
  w.putObject(R.s('bounds'), R.c('Rctn'), b);
  w.putInteger(R.s('uOrder'), 4);
  w.putInteger(R.s('vOrder'), 4);
  if (form == 'points') {
    var list = new ActionList();
    for (i = 0; i < pts.length; i++) list.putObject(R.s('rationalPoint'), R.point(pts[i][0], pts[i][1]));
    e.putList(R.s('meshPoints'), list);
  } else {
    var xs = new ActionList(), ys = new ActionList(), p = new ActionDescriptor();
    for (i = 0; i < pts.length; i++) {
      xs.putUnitDouble(R.c('#Pxl'), pts[i][0]);
      ys.putUnitDouble(R.c('#Pxl'), pts[i][1]);
    }
    p.putList(R.c('Hrzn'), xs);
    p.putList(R.c('Vrtc'), ys);
    e.putObject(R.s('meshPoints'), R.s('rationalPoint'), p);
  }
  w.putObject(R.s('customEnvelopeWarp'), R.s('customEnvelopeWarp'), e);
  return w;
};
R.warpWays = function (op, box) {
  var make = function (form) {
    return function () {
      var d = R.trnf();
      d.putObject(R.s('warp'), R.s('warp'), R.warpDesc(op, box, form));
      return d;
    };
  };
  if (op.style != 'custom') return [['warpStyle ' + R.styleId(op.style), make('')]];
  return [['meshPoints of points', make('points')], ['meshPoints of two lists', make('lists')]];
};
R.withInterp = function (d, interp) {
  var id = R.method(interp, false)[1];
  d.putEnumerated(R.c('Intr'), R.c('Intp'), R.id(id));
  return d;
};

// What Photoshop makes of a transform or a warp: the layer copied, the copy made a smart object and given the events
// (functions making them, in turn), what Photoshop then says of it read, the copy deleted. {before, after}: its corners
// before and after (R.smart's: corners, more), null where it says none; neither when the copy did not become a smart
// object (noted). Throws what an event throws.
R.probe = function (doc, layer, events) {
  doc.activeLayer = layer;
  var copy = layer.duplicate(), out = {};
  try {
    doc.activeLayer = copy;
    try {
      executeAction(R.s('newPlacedLayer'), undefined, DialogModes.NO);
    } catch (e) {
      PB.note('    no smart object of the copy (' + e + '): nothing to read back');
      return out;
    }
    copy = doc.activeLayer;
    out.before = R.smart(doc, copy);
    for (var i = 0; i < events.length; i++) executeAction(R.c('Trnf'), events[i](), DialogModes.NO);
    out.after = R.smart(doc, copy);
  } finally {
    copy.remove();
    doc.activeLayer = layer;
  }
  return out;
};
// The layer given a transform or a warp (a function making the event) with the op's interpolation; the event without
// it (Preferences' Image Interpolation then, R.interpolation's) if Photoshop will not take it so.
R.transformLayer = function (doc, layer, event, interp) {
  doc.activeLayer = layer;
  return R.tryEach('the layer', [
    ['with Intr', function () { executeAction(R.c('Trnf'), R.withInterp(event(), interp), DialogModes.NO); }],
    ['without Intr', function () { executeAction(R.c('Trnf'), event(), DialogModes.NO); }]
  ]);
};

// ---- Reading a warp back -------------------------------------------------------------------------------------------

// The 16 points of a warp's customEnvelopeWarp (meshPoints: a list of points, or one point of two lists), null when
// there are not 16.
R.meshPoints = function (env) {
  var key = R.s('meshPoints'), out = [], i;
  if (!env || !env.hasKey(key)) return null;
  var t = env.getType(key);
  if (t == DescValueType.LISTTYPE) {
    var l = env.getList(key);
    for (i = 0; i < l.count; i++) {
      if (l.getType(i) != DescValueType.OBJECTTYPE) return null;
      var p = l.getObjectValue(i);
      out.push([R.num(p, R.c('Hrzn'), NaN), R.num(p, R.c('Vrtc'), NaN)]);
    }
  } else if (t == DescValueType.OBJECTTYPE) {
    var o = env.getObjectValue(key), xs = R.list(o, R.c('Hrzn')), ys = R.list(o, R.c('Vrtc'));
    if (!xs || !ys || xs.count != ys.count) return null;
    for (i = 0; i < xs.count; i++) out.push([R.num(xs, i, NaN), R.num(ys, i, NaN)]);
  }
  for (i = 0; i < out.length; i++)
    if (isNaN(out[i][0]) || isNaN(out[i][1])) return null;
  return out.length == 16 ? out : null;
};
// A smart object's warp as A30's BezierMesh in the document's pixels: its points in the contents' (the copy is not
// transformed: the contents' (u, v) is the document's (u + left, v + top) of its corners), unless its bounds are the
// document's already; null when Photoshop keeps no points. Notes what it read.
R.meshOf = function (smart) {
  var w = smart ? R.obj(smart.more, R.s('warp')) : null;
  if (!w) return null;
  PB.note('      warp read back: ' + PB.dump(w));
  var pts = R.meshPoints(R.obj(w, R.s('customEnvelopeWarp'))), b = R.obj(w, R.s('bounds')), q = smart.corners;
  if (!pts) return null;
  var left = R.num(b, R.c('Left'), 0), top = R.num(b, R.c('Top '), 0);
  var dx = Math.abs(left - q[0]) < 0.01 && Math.abs(top - q[1]) < 0.01 ? 0 : q[0], dy = dx ? q[1] : 0;
  var out = [];
  for (var i = 0; i < 16; i++) out.push([R.round(pts[i][0] + dx, 1e4), R.round(pts[i][1] + dy, 1e4)]);
  return {cols: 1, rows: 1, xs: [R.round(q[0], 1e4), R.round(q[2], 1e4)], ys: [R.round(q[1], 1e4), R.round(q[5], 1e4)],
          points: out};
};
R.restGrid = function (box) {  // a patch at rest: its points at thirds
  var out = [];
  for (var j = 0; j < 4; j++)
    for (var i = 0; i < 4; i++) out.push([box[0] + (box[2] - box[0]) * i / 3, box[1] + (box[3] - box[1]) * j / 3]);
  return out;
};

// ---- The operations (PB.steps.ops) ---------------------------------------------------------------------------------

R.OPS = {};
R.OPS.imageSize = function (doc, layer, op) {
  var m = R.method(op.method, true);
  R.tryEach('Image Size ' + op.method, [
    ['ResampleMethod.' + m[0], function () {
      if (ResampleMethod[m[0]] === undefined) throw new Error('no ResampleMethod.' + m[0]);
      if (op.method == 'preserveDetails')
        doc.resizeImage(op.w, op.h, undefined, ResampleMethod[m[0]], 0);  // Reduce Noise 0
      else
        doc.resizeImage(op.w, op.h, undefined, ResampleMethod[m[0]]);
    }],
    ['ImgS ' + m[1], function () {
      var d = new ActionDescriptor();
      d.putUnitDouble(R.c('Wdth'), R.c('#Pxl'), op.w);
      d.putUnitDouble(R.c('Hght'), R.c('#Pxl'), op.h);
      d.putEnumerated(R.c('Intr'), R.c('Intp'), R.id(m[1]));
      executeAction(R.c('ImgS'), d, DialogModes.NO);
    }]
  ]);
  R.sized(doc, op.w, op.h);
};
R.OPS.rotateCanvas = function (doc, layer, op) {
  R.interpolation(op.interp);
  R.backgroundColour(doc, op.color);
  doc.rotateCanvas(op.angle);
};
R.OPS.canvasSize = function (doc, layer, op) {
  R.backgroundColour(doc, op.color);
  doc.resizeCanvas(op.w, op.h, AnchorPosition[R.ANCHORS[op.anchor]]);
  R.sized(doc, op.w, op.h);
};
// Free Transform: each way of putting it to the event on a smart object's copy until one puts the corners where op
// says (else the first that ran), then the layer by that way; op's transform and warp become Photoshop's, from the
// copy's corners (a projective map: four corners fix it).
R.OPS.freeTransform = function (doc, layer, op) {
  R.interpolation(op.interp);
  var box = R.bounds(layer), from = R.boxCorners(box), ways = R.transformWays(op, box), want = R.cornersOf(op.transform, box);
  var use = null, read = null, errors = [];
  for (var i = 0; i < ways.length && !(use && read && R.far(read, want) < 0.5); i++) {
    try {
      var p = R.probe(doc, layer, [ways[i][1]]);
      if (!p.before || !p.after || R.far(p.before.corners, from) > 0.01) {
        PB.note('    ' + ways[i][0] + ': nothing read back of the copy' + (p.before ? ' (its corners ' +
                p.before.corners.join(' ') + ', the box ' + from.join(' ') + ')' : ''));
        if (!use) use = ways[i];
        continue;
      }
      PB.note('    ' + ways[i][0] + ': the copy\'s corners go to ' + p.after.corners.join(' '));
      if (!use || !read || R.far(p.after.corners, want) < R.far(read, want)) use = ways[i], read = p.after.corners;
    } catch (e) {
      errors.push(ways[i][0] + ': ' + e);
    }
  }
  if (!use) throw new Error('the transform did not run: ' + errors.join('; '));
  if (errors.length) PB.note('    ' + errors.join('; '));
  if (read) {
    if (R.far(read, from) < 0.01) throw new Error(use[0] + ': the corners stayed where they were');
    if (R.far(read, want) > 0.01)
      PB.note('    Photoshop\'s corners ' + read.join(' ') + ', not ' + want.join(' ') + ' (' + R.round(R.far(read, want), 1e3) + ' px)');
    var m = R.homography(from, read);
    op.transform = R.rounded(m);
    op.warp.matrix = R.rounded(R.invert(m));
  } else {
    PB.note('    no transform read back: index.json keeps this script\'s');
  }
  R.transformLayer(doc, layer, use[1], op.interp);
  var b = R.bounds(layer), q = read || want, hull = [Math.min(q[0], q[2], q[4], q[6]), Math.min(q[1], q[3], q[5], q[7]),
                                                     Math.max(q[0], q[2], q[4], q[6]), Math.max(q[1], q[3], q[5], q[7])];
  PB.note('    ' + use[0] + '; the layer\'s bounds ' + b.join(' ') +
          (R.far(b, hull) > 1.5 ? ', not about those of the corners, ' + hull.join(' ') : ''));
};
// Warp: a style's mesh read back where Photoshop keeps one (a smart object's copy given the same warp; if it keeps the
// style alone, the copy's warp made Custom after it); the layer warped, its bounds checked to have moved (a warp
// Photoshop took as none leaves them).
R.OPS.warp = function (doc, layer, op) {
  R.interpolation(op.interp);
  var box = R.bounds(layer), ways = R.warpWays(op, box), mesh = null;
  if (op.style != 'custom') {
    var toCustom = function () {
      var d = R.trnf(), w = new ActionDescriptor();
      w.putEnumerated(R.s('warpStyle'), R.s('warpStyle'), R.s('warpCustom'));
      d.putObject(R.s('warp'), R.s('warp'), w);
      return d;
    };
    try {
      mesh = R.meshOf(R.probe(doc, layer, [ways[0][1]]).after);
      if (!mesh) mesh = R.meshOf(R.probe(doc, layer, [ways[0][1], toCustom]).after);
    } catch (e) {
      PB.note('    no mesh read back: ' + e);
    }
    if (mesh) op.warp = {matrix: op.warp.matrix, mesh: mesh, interp: op.warp.interp};  // LayerWarp's order
    else PB.note('    no mesh read back: index.json has the style alone');
  }
  var moved = function () {
    var b = R.bounds(layer);
    if (R.far(b, box) < 1) throw new Error('the layer\'s bounds stayed ' + b.join(' '));
  };
  var tries = [];
  for (var i = 0; i < ways.length; i++)
    (function (way) {
      tries.push([way[0], function () {
        R.transformLayer(doc, layer, way[1], op.interp);
        if (op.style == 'custom') moved();  // a mesh Photoshop did not read is no warp
      }]);
    })(ways[i]);
  R.tryEach('Warp', tries);
  PB.note('    the layer\'s bounds ' + R.bounds(layer).join(' '));
};
R.sized = function (doc, w, h) {
  if (doc.width.as('px') != w || doc.height.as('px') != h)
    throw new Error('the document came out ' + doc.width.as('px') + ' x ' + doc.height.as('px') + ', not ' + w + ' x ' + h);
};
// Photoshop's Background colour: what Image Rotation and Canvas Size show of a Background where it had no pixels.
R.backgroundColour = function (doc, color) {
  if (!color) return;
  if (!doc.layers[doc.layers.length - 1].isBackgroundLayer) throw new Error('a colour ' + color + ' without a Background');
  app.backgroundColor = R.solid(color);
};

// The case's operations on its one layer, after the layer is made the Background if props.background says.
PB.steps.ops = function (ops, doc, built) {
  var layer = built[0].layer;
  if (built[0].json.props.background) {
    doc.activeLayer = layer;
    layer.isBackgroundLayer = true;
    if (!layer.isBackgroundLayer) throw new Error('the layer did not become the Background');
  }
  for (var i = 0; i < ops.length; i++) {
    var op = ops[i];
    if (!R.OPS[op.op]) throw new Error('no op ' + op.op);
    app.activeDocument = doc;
    doc.activeLayer = layer;
    PB.note('    ' + op.op + ' ' + PB.json(R.brief(op)));
    R.OPS[op.op](doc, layer, op);
  }
};
// The image's size after them: Photoshop's (noted where this script supposed another).
PB.steps.size = function (size, doc) {
  var w = doc.width.as('px'), h = doc.height.as('px');
  if (w != size[0] || h != size[1]) PB.note('    the image is ' + w + ' x ' + h + ', not ' + size.join(' x '));
  size[0] = w;
  size[1] = h;
};
PB.layerKeys.background = function () {};  // PB.steps.ops makes the layer the Background, after its props
R.brief = function (op) {  // op without what is worked out of it
  var out = {};
  for (var k in op)
    if (op.hasOwnProperty(k) && k != 'op' && k != 'transform' && k != 'warp') out[k] = op[k];
  return out;
};

// ---- The cases -----------------------------------------------------------------------------------------------------

R.FULL = [100, 70];  // checker.png, ramp.png, photo.png (pb-bench ps-resample's kW x kH)
R.PAD = [160, 128];  // the *-pad.png
R.BOX = [30, 29, 130, 99];  // their content's bounds, [left, top, right, bottom]: the transform's and the warp's box
R.SIZE_METHODS = ['nearestNeighbor', 'bilinear', 'bicubic', 'bicubicSmoother', 'bicubicSharper', 'automatic',
                  'preserveDetails'];  // Image Size's seven (Preserve Details 2.0 is P3's)
R.TRANSFORM_INTERPS = ['nearestNeighbor', 'bilinear', 'bicubic', 'bicubicSmoother', 'bicubicSharper', 'automatic'];

// The ops as index.json has them, in order, with what is worked out of them.
R.imageSize = function (w, h, method) { return {op: 'imageSize', w: w, h: h, method: method}; };
R.rotateCanvas = function (angle, interp, color) {
  return {op: 'rotateCanvas', angle: angle, interp: interp, color: color || null};
};
R.canvasSize = function (w, h, anchor, color) { return {op: 'canvasSize', w: w, h: h, anchor: anchor, color: color || null}; };
// Free Transform of the box by the options bar's fields f (w, h, angle, skew; or corners), with this script's guess of
// Photoshop's matrix: scale W and H, skew V (y moves by tan v of x) then H (x by tan h of y), turn clockwise (y down),
// about the box's centre; corners: the projective map taking the box's to them.
R.freeTransform = function (f, interp) {
  var op = {op: 'freeTransform'}, keys = ['w', 'h', 'angle', 'skew', 'corners'], box = R.BOX, m, i;
  for (i = 0; i < keys.length; i++)
    if (f[keys[i]] !== undefined) op[keys[i]] = PB.copy(f[keys[i]]);
  op.interp = interp;
  if (f.corners) {
    m = R.homography(R.boxCorners(box), f.corners);
  } else {
    var a = (f.angle || 0) * Math.PI / 180, k = f.skew || [0, 0], deg = Math.PI / 180;
    var s = [(f.w === undefined ? 100 : f.w) / 100, 0, 0, 0, (f.h === undefined ? 100 : f.h) / 100, 0, 0, 0, 1];
    var kh = [1, Math.tan(k[0] * deg), 0, 0, 1, 0, 0, 0, 1], kv = [1, 0, 0, Math.tan(k[1] * deg), 1, 0, 0, 0, 1];
    var turn = [Math.cos(a), -Math.sin(a), 0, Math.sin(a), Math.cos(a), 0, 0, 0, 1];
    m = R.about(R.mul(turn, R.mul(kh, R.mul(kv, s))), (box[0] + box[2]) / 2, (box[1] + box[3]) / 2);
  }
  op.transform = R.rounded(R.scaled(m));
  op.warp = {matrix: R.rounded(R.invert(m)), interp: interp};
  return op;
};
// Warp of the box: a style at f's bend, h, v (%) and orientation; or custom, f.points the 16 of one patch over it.
R.warp = function (style, f, interp) {
  var op = {op: 'warp', style: style};
  if (style != 'custom') {
    op.bend = f.bend;
    op.h = f.h || 0;
    op.v = f.v || 0;
    op.orientation = f.orientation || 'horizontal';
  }
  op.interp = interp;
  op.warp = {matrix: R.IDENTITY.slice(0), interp: interp};
  if (style == 'custom')
    op.warp = {matrix: R.IDENTITY.slice(0), mesh: {cols: 1, rows: 1, xs: [R.BOX[0], R.BOX[2]], ys: [R.BOX[1], R.BOX[3]],
               points: f.points}, interp: interp};
  return op;
};
// A patch's 16 points: those at rest over the box moved by d (16 [dx, dy], row by row).
R.mesh = function (d) {
  var rest = R.restGrid(R.BOX), out = [];
  for (var i = 0; i < 16; i++) out.push([R.round(rest[i][0] + d[i][0], 1e4), R.round(rest[i][1] + d[i][1], 1e4)]);
  return out;
};
// Where the box's corners go for Perspective (the top edge narrowed, as Ctrl Alt Shift dragging a top corner) and
// Distort (each anywhere: both terms of perspective); the options bar's every field at once (how Photoshop composes
// them); two custom meshes, a gentle one (the corners stay, the edges and the inside bend) and a strong one.
R.KEYSTONE = [46, 29, 114, 29, 130, 99, 30, 99];
R.DISTORT = [42, 35, 124, 21, 140, 95, 26, 108];
R.COMBO = {w: 80, h: 60, angle: 30, skew: [15, -10]};
R.MESH_A = R.mesh([[0, 0], [0, -12], [0, -12], [0, 0], [-10, 0], [6, 4], [-4, 6], [6, 0],
                   [-10, 0], [5, -5], [-6, -3], [6, 0], [0, 0], [0, 8], [0, 8], [0, 0]]);
R.MESH_B = R.mesh([[-8, -6], [14, -14], [-6, -18], [10, 4], [-14, 10], [16, 8], [-10, 14], [12, -6],
                   [-6, -10], [12, -12], [-14, -6], [16, 8], [6, -4], [10, 14], [-12, 10], [-6, 10]]);
R.ROTATE_BG = '#2b6cb0';  // a Background's colour where Image Rotation turns it from: far from the photograph's

// The size the ops leave, as this script supposes (Photoshop's is read after them): Image Size's and Canvas Size's
// own; Image Rotation's the box that holds the turned canvas, whole pixels up.
R.sizeAfter = function (canvas, ops) {
  var w = canvas[0], h = canvas[1];
  for (var i = 0; i < ops.length; i++) {
    var op = ops[i];
    if (op.op == 'imageSize' || op.op == 'canvasSize') {
      w = op.w;
      h = op.h;
    } else if (op.op == 'rotateCanvas') {
      var a = op.angle * Math.PI / 180, c = Math.abs(Math.cos(a)), s = Math.abs(Math.sin(a));
      var w2 = Math.ceil(w * c + h * s - 1e-9);
      h = Math.ceil(w * s + h * c - 1e-9);
      w = w2;
    }
  }
  return [w, h];
};
// A case: one layer of input src (the Background if more.background), the ops on it, the size they leave; 16 bits, but
// the photograph's 8 (PB.run's) and more.bits.
R.make = function (name, src, ops, more) {
  var canvas = src.indexOf('-pad') >= 0 ? R.PAD : R.FULL, bg = more && more.background;
  var props = {name: bg ? 'Background' : src.replace(/\.png$/, '')};
  if (bg) props.background = true;
  var c = {name: name, canvas: canvas.slice(0), layers: [{kind: 'pixel', src: src, props: props}], ops: ops,
           size: R.sizeAfter(canvas, ops)};
  if (more && more.bits) c.bits = more.bits;
  else if (src.indexOf('photo') != 0) c.bits = 16;
  return c;
};

function cases() {
  var c = [], i, j, k, f;
  var add = function (name, src, ops, more) { c.push(R.make(name, src, ops, more)); };
  var stem = function (src) { return src.replace(/\.png$/, ''); };

  // Image Size (T33): each method at 200%, 50% and 37% of the checks (a kernel's step response; aliasing or not of
  // the fine checks), the ramp (where each output pixel samples; the edges; second order), the photograph (real
  // detail); the checks on transparency (alpha weighting at the content's edges); the ramp as a Background.
  var full = ['checker.png', 'ramp.png', 'photo.png'], scales = [[200, 200, 140], [50, 50, 35], [37, 37, 26]];
  for (i = 0; i < full.length; i++)
    for (j = 0; j < R.SIZE_METHODS.length; j++)
      for (k = 0; k < scales.length; k++)
        add('size-' + stem(full[i]) + '-' + R.SIZE_METHODS[j] + '-' + scales[k][0], full[i],
            [R.imageSize(scales[k][1], scales[k][2], R.SIZE_METHODS[j])]);
  var padScales = [[200, 320, 256], [37, 59, 47]];
  for (j = 0; j < R.TRANSFORM_INTERPS.length; j++)
    for (k = 0; k < padScales.length; k++)
      add('size-checker-pad-' + R.TRANSFORM_INTERPS[j] + '-' + padScales[k][0], 'checker-pad.png',
          [R.imageSize(padScales[k][1], padScales[k][2], R.TRANSFORM_INTERPS[j])]);
  add('size-ramp-bg-bicubic-200', 'ramp.png', [R.imageSize(200, 140, 'bicubic')], {background: true});
  add('size-ramp-bg-bicubic-37', 'ramp.png', [R.imageSize(37, 26, 'bicubic')], {background: true});

  // Free Transform (X31; X30 samples it): turned 30 degrees, scaled to 37%, skewed, in perspective, the checks with
  // each interpolation of the options bar; the ramp (where each pixel samples: Photoshop's matrix) with those, Distort
  // and every field at once; the photograph at Bicubic Automatic, Photoshop's default.
  var xf = [['rotate30', {angle: 30}], ['scale37', {w: 37, h: 37}], ['skew20', {skew: [20, 0]}],
            ['perspective', {corners: R.KEYSTONE}]];
  for (i = 0; i < xf.length; i++)
    for (j = 0; j < R.TRANSFORM_INTERPS.length; j++)
      add('transform-checker-' + xf[i][0] + '-' + R.TRANSFORM_INTERPS[j], 'checker-pad.png',
          [R.freeTransform(xf[i][1], R.TRANSFORM_INTERPS[j])]);
  f = xf.concat([['distort', {corners: R.DISTORT}], ['combo', R.COMBO]]);
  for (i = 0; i < f.length; i++) add('transform-ramp-' + f[i][0] + '-bilinear', 'ramp-pad.png', [R.freeTransform(f[i][1], 'bilinear')]);
  for (i = 0; i < xf.length; i++)
    add('transform-photo-' + xf[i][0] + '-automatic', 'photo-pad.png', [R.freeTransform(xf[i][1], 'automatic')]);
  add('transform-checker-distort-bicubic', 'checker-pad.png', [R.freeTransform({corners: R.DISTORT}, 'bicubic')]);
  add('transform-checker-combo-bicubic', 'checker-pad.png', [R.freeTransform(R.COMBO, 'bicubic')]);

  // Warp (X32 fits its styles to these; X30 samples meshes): the 15 styles at Bend 50% and -50% and a few with H, V
  // Distortion or turned vertical, on the ramp (each pixel says where in the box it came from); two custom meshes
  // (known: X30's sampling of one alone); the checks at 50% (8 bits: to see the shapes; the maps are the ramp's).
  var bends = [50, -50];
  for (i = 0; i < R.STYLES.length; i++)
    for (j = 0; j < bends.length; j++)
      add('warp-ramp-' + R.STYLES[i] + '-' + (bends[j] < 0 ? 'm' : '') + Math.abs(bends[j]), 'ramp-pad.png',
          [R.warp(R.STYLES[i], {bend: bends[j]}, 'bicubic')]);
  add('warp-ramp-arc-50-h50', 'ramp-pad.png', [R.warp('arc', {bend: 50, h: 50}, 'bicubic')]);
  add('warp-ramp-arc-50-v50', 'ramp-pad.png', [R.warp('arc', {bend: 50, v: 50}, 'bicubic')]);
  add('warp-ramp-arc-50-vertical', 'ramp-pad.png', [R.warp('arc', {bend: 50, orientation: 'vertical'}, 'bicubic')]);
  add('warp-ramp-wave-50-vertical', 'ramp-pad.png', [R.warp('wave', {bend: 50, orientation: 'vertical'}, 'bicubic')]);
  add('warp-ramp-custom-a', 'ramp-pad.png', [R.warp('custom', {points: R.MESH_A}, 'bicubic')]);
  add('warp-ramp-custom-b', 'ramp-pad.png', [R.warp('custom', {points: R.MESH_B}, 'bicubic')]);
  for (i = 0; i < R.STYLES.length; i++)
    add('warp-checker-' + R.STYLES[i] + '-50', 'checker-pad.png', [R.warp(R.STYLES[i], {bend: 50}, 'bicubic')], {bits: 8});
  add('warp-checker-custom-a', 'checker-pad.png', [R.warp('custom', {points: R.MESH_A}, 'bicubic')], {bits: 8});

  // Image Rotation > Arbitrary (T34): 15 degrees clockwise at Bicubic and at Bicubic Automatic (Photoshop's default),
  // 15 counterclockwise; the canvas grows to hold it (how it rounds); a Background shows Photoshop's Background colour
  // where it turned from, another layer nothing.
  add('rotate-checker-15-bicubic', 'checker.png', [R.rotateCanvas(15, 'bicubic')]);
  add('rotate-checker-15-automatic', 'checker.png', [R.rotateCanvas(15, 'automatic')]);
  add('rotate-ramp-15-bicubic', 'ramp.png', [R.rotateCanvas(15, 'bicubic')]);
  add('rotate-ramp-m15-bicubic', 'ramp.png', [R.rotateCanvas(-15, 'bicubic')]);
  add('rotate-photo-15-bicubic', 'photo.png', [R.rotateCanvas(15, 'bicubic')]);
  add('rotate-photo-bg-15-bicubic', 'photo.png', [R.rotateCanvas(15, 'bicubic', R.ROTATE_BG)], {background: true});

  // Canvas Size (T34), on the ramp (each pixel says where it was): grown about the centre by odd amounts (where the
  // odd pixel goes), a Background grown from a corner and from the bottom's middle with Other and Gray; shrunk to the
  // top right; shrunk and grown back, a layer (keeps what went past the canvas) and a Background (cut: White back).
  add('canvas-ramp-141x101-center', 'ramp.png', [R.canvasSize(141, 101, 4)]);
  add('canvas-ramp-bg-140x100-topleft', 'ramp.png', [R.canvasSize(140, 100, 0, '#e9c7b3')], {background: true});
  add('canvas-ramp-bg-131x99-bottom', 'ramp.png', [R.canvasSize(131, 99, 7, '#808080')], {background: true});
  add('canvas-ramp-63x45-topright', 'ramp.png', [R.canvasSize(63, 45, 2)]);
  add('canvas-ramp-61x41-back', 'ramp.png', [R.canvasSize(61, 41, 4), R.canvasSize(100, 70, 4)]);
  add('canvas-ramp-bg-61x41-back', 'ramp.png', [R.canvasSize(61, 41, 4, '#ffffff'), R.canvasSize(100, 70, 4, '#ffffff')],
      {background: true});
  return c;
}

// Preferences' Image Interpolation and the Background colour, which the cases set, put back after.
R.run = function () {
  var interp = null, bg = null;
  try {
    interp = app.preferences.interpolation;
    bg = app.backgroundColor;
    PB.note('Image Interpolation ' + interp + ', Background colour #' + bg.rgb.hexValue + ' before');
  } catch (e) {
    PB.note('Image Interpolation, Background colour: cannot read them (' + e + ')');
  }
  try {
    PB.run('resample', cases(), {bits: 8, dir: HERE, inputs: 'resample-in', repoInputs: 'in'});
  } finally {
    try {
      if (interp !== null) app.preferences.interpolation = interp;
      if (bg !== null) app.backgroundColor = bg;
    } catch (e) {}
  }
};
if (typeof app != 'undefined') R.run();  // jscheck runs it without Photoshop
