// Photoshop's PSD suite for P1 (E33): small PSD files, a P1 feature each, for photo-beauty's PSD readers (M40-M47)
// and E34; README-psd.md next to this script has the steps. Pass one builds each case of cases() in a document of its
// own (RGB, 8 bits, sRGB unless the case says), checks what Photoshop made against the case (the layer tree, every
// layer's props, the descriptors of adjustments, fills and styles read back), and saves out/psd/ (out/psd-big/ for
// the large ones): <case>.psd or .psb, <case>.png (Photoshop's composite at the document's depth: 16 bits for 16 and
// 32 bits; CMYK and Lab made sRGB first), index.json (each case as photo-beauty's layers, and what its file is) and
// photoshop.txt (version, Color Settings, each layer as Photoshop reads it). Pass two opens the files photo-beauty
// saved (E34) from ours/ next to this script and writes out/psd-read/: each one's composite, its layers as Photoshop
// reads them, index.json. Inputs: p1/in/ (tests/golden/ps/p1/in, B30's). ExtendScript (ECMAScript 3), Photoshop 2025
// (26) on Windows. Not run in Photoshop yet: what it is unsure of is in README-psd.md. Strings past ASCII are \u
// escapes, so that the file reads the same in any encoding.
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
#include "p1/lib.jsx"

var E = {};
E.nothing = function () {};

// ---- Keys past lib.jsx's -----------------------------------------------------------------------------------------

// A layer's props past PB.PROPS (A40's locks, Background and label colour; A30's fx, content, source; A31's
// transform), a layer's keys (how its pixels, mask or smart object are made), a case's keys (its document): PB.normalise
// takes them; this script does them, not lib.jsx's PB.apply or PB.build.
//   cell [x, y, w, h]: the layer keeps its pixels there only; move [dx, dy]: then moved (pixels past the canvas kept);
//   paint [[x, y, w, h, '#rrggbb'], ...]: rectangles of a colour (a layer without src: new and empty, then painted);
//   maskRect [x, y, w, h]: a layer mask revealing that rectangle only; maskAll 'white' or 'black': Reveal or Hide All;
//   smart {id, file | layers, linked}: a smart object of an input placed, or of its own layers converted, or (only id)
//   a copy of an earlier one, sharing its contents; place {scale %, rotate degrees clockwise, move}: its transform;
//   smartFilters [{filter: 'gaussianBlur', radius}]; advanced {knockout, blendInterior, blendClipped,
//   transparencyShapes, maskHidesEffects, vectorMaskHidesEffects} (P2's Advanced Blending); vectorMask [[x, y], ...].
//   Case: mode 'rgb' | 'gray' | 'cmyk' | 'lab', depth 8 | 16 | 32, big (out/psd-big/), psb, compat (Maximize
//   Compatibility, true unless false), globalLight [angle, altitude], crop [x, y, w, h] (after the layers), alphas,
//   spots, guides [['v' | 'h', px], ...], meta, ppi, convert 'indexed' | 'bitmap' (flattened, then that mode),
//   selected (the layer active when saved).
E.PROPS = ['lockTransparency', 'lockPixels', 'lockPosition', 'lockAll', 'background', 'color', 'fx', 'content', 'source',
           'transform'];
E.LAYER = ['cell', 'move', 'paint', 'maskRect', 'maskAll', 'smart', 'place', 'smartFilters', 'advanced', 'vectorMask'];
E.CASE = ['mode', 'depth', 'big', 'psb', 'compat', 'globalLight', 'crop', 'alphas', 'spots', 'guides', 'meta', 'ppi',
          'convert', 'selected'];
E.KINDS = ['fill', 'text', 'shape', 'so'];  // A30's and A31's (Mock.KINDS)
(function () {
  var i;
  for (i = 0; i < E.PROPS.length; i++) PB.layerKeys[E.PROPS[i]] = E.nothing;
  for (i = 0; i < E.LAYER.length; i++) PB.layerKeys[E.LAYER[i]] = E.nothing;
  for (i = 0; i < E.CASE.length; i++) PB.steps[E.CASE[i]] = E.nothing;
  for (i = 0; i < E.KINDS.length; i++) PB.kinds[E.KINDS[i]] = E.nothing;
})();

// ---- Small things --------------------------------------------------------------------------------------------------

E.or = function (v, d) { return v === undefined ? d : v; };
E.merge = function (o, more) {
  if (more)
    for (var k in more)
      if (more.hasOwnProperty(k)) o[k] = more[k];
  return o;
};
E.HEX = /^#[0-9a-fA-F]{6}$/;
E.rgb3 = function (hex) {  // '#rrggbb' as [r, g, b], 0..255
  if (typeof hex != 'string' || !E.HEX.test(hex)) throw new Error('a colour ' + hex);
  var v = parseInt(hex.slice(1), 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
};
E.rgbc = function (c) {  // [r, g, b] or '#rrggbb' as Photoshop's RGBC
  var v = typeof c == 'string' ? E.rgb3(c) : c, d = new ActionDescriptor();
  d.putDouble(PB.c2t('Rd  '), v[0]);
  d.putDouble(PB.c2t('Grn '), v[1]);
  d.putDouble(PB.c2t('Bl  '), v[2]);
  return d;
};
E.solid = function (hex) {
  var c = new SolidColor(), v = E.rgb3(hex);
  c.rgb.red = v[0];
  c.rgb.green = v[1];
  c.rgb.blue = v[2];
  return c;
};
E.region = function (r) { return [[r[0], r[1]], [r[0] + r[2], r[1]], [r[0] + r[2], r[1] + r[3]], [r[0], r[1] + r[3]]]; };
E.grid = function (cols, rows, w, h) {  // cells [x, y, w, h], across then down
  var out = [];
  for (var j = 0; j < rows; j++)
    for (var i = 0; i < cols; i++) out.push([i * w, j * h, w, h]);
  return out;
};
// IDs of tables whose 4-letter entries are char IDs and longer ones string IDs.
E.id = function (s) { return s.length == 4 ? PB.c2t(s) : PB.s2t(s); };
// photo-beauty's blend modes (kBlendNames) as Action Manager's BlnM.
E.BLNM = {normal: 'Nrml', dissolve: 'Dslv', darken: 'Drkn', multiply: 'Mltp', colorBurn: 'CBrn', linearBurn: 'linearBurn',
          darkerColor: 'darkerColor', lighten: 'Lghn', screen: 'Scrn', colorDodge: 'CDdg', linearDodge: 'linearDodge',
          lighterColor: 'lighterColor', overlay: 'Ovrl', softLight: 'SftL', hardLight: 'HrdL', vividLight: 'vividLight',
          linearLight: 'linearLight', pinLight: 'pinLight', hardMix: 'hardMix', difference: 'Dfrn', exclusion: 'Xclu',
          subtract: 'blendSubtraction', divide: 'blendDivide', hue: 'H   ', saturation: 'Strt', color: 'Clr ',
          luminosity: 'Lmns'};
E.blnm = function (name) {
  if (!E.BLNM[name]) throw new Error('no blend mode ' + name);
  return E.id(E.BLNM[name]);
};
// A40's label colours (LayerProps::color, PSD lclr) as the Layers panel's.
E.LABELS = {none: 'None', red: 'Rd  ', orange: 'Orng', yellow: 'Ylw ', green: 'Grn ', blue: 'Bl  ', violet: 'Vlt ',
            gray: 'Gry '};
E.MODES = ['normal', 'dissolve', 'darken', 'multiply', 'colorBurn', 'linearBurn', 'darkerColor', 'lighten', 'screen',
           'colorDodge', 'linearDodge', 'lighterColor', 'overlay', 'softLight', 'hardLight', 'vividLight', 'linearLight',
           'pinLight', 'hardMix', 'difference', 'exclusion', 'subtract', 'divide', 'hue', 'saturation', 'color',
           'luminosity'];  // a layer's 27, the menu's order

// ---- Descriptors ---------------------------------------------------------------------------------------------------

// D30's gradient JSON ({colors: [{p, c: [r, g, b], m}], alphas: [{p, a, m}], smoothness, name}) as Photoshop's Grdn:
// Lctn is p in 4096ths, Mdpn the stop's m in % (the same stop's: README-psd.md says why that is unsure), Intr the
// smoothness in 4096ths.
E.gradient = function (g) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), i, o;
  d.putString(c('Nm  '), g.name || 'Custom');
  d.putEnumerated(c('GrdF'), c('GrdF'), c('CstS'));
  d.putDouble(c('Intr'), E.or(g.smoothness, 100) * 40.96);
  for (i = 0; i < g.colors.length; i++) {
    o = new ActionDescriptor();
    o.putObject(c('Clr '), c('RGBC'), E.rgbc(g.colors[i].c));
    o.putEnumerated(c('Type'), c('Clry'), c('UsrS'));
    o.putInteger(c('Lctn'), Math.round(g.colors[i].p * 4096));
    o.putInteger(c('Mdpn'), Math.round(E.or(g.colors[i].m, 0.5) * 100));
    list.putObject(c('Clrt'), o);
  }
  d.putList(c('Clrs'), list);
  var alphas = g.alphas || [{p: 0, a: 1}, {p: 1, a: 1}];
  list = new ActionList();
  for (i = 0; i < alphas.length; i++) {
    o = new ActionDescriptor();
    o.putUnitDouble(c('Opct'), c('#Prc'), alphas[i].a * 100);
    o.putInteger(c('Lctn'), Math.round(alphas[i].p * 4096));
    o.putInteger(c('Mdpn'), Math.round(E.or(alphas[i].m, 0.5) * 100));
    list.putObject(c('TrnS'), o);
  }
  d.putList(c('Trns'), list);
  return d;
};
// Photoshop 2023's gradient Method (D30's names) and the five styles.
E.METHOD = {perceptual: 'Perc', linear: 'Lnr ', classic: 'Gcls', smooth: 'Smoo', stripes: 'Stri'};
E.STYLE = {linear: 'Lnr ', radial: 'Rdl ', angle: 'Angl', reflected: 'Rflc', diamond: 'Dmnd'};
E.putMethod = function (d, method) {
  var id = E.METHOD[String(method).toLowerCase()];
  if (!id) throw new Error('no gradient method ' + method);
  d.putEnumerated(PB.s2t('gradientsInterpolationMethod'), PB.s2t('gradientInterpolationMethodType'), PB.c2t(id));
};
// D30's GradientFill keys (gradient, method, style, angle, scale, reverse, dither, align, offset) into d: a Gradient
// fill layer's, Gradient Overlay's, a gradient Stroke's.
E.gradientFill = function (d, f) {
  var c = PB.c2t, o = new ActionDescriptor(), off = f.offset || [0, 0];
  if (!E.STYLE[f.style || 'linear']) throw new Error('no gradient style ' + f.style);
  d.putObject(c('Grad'), c('Grdn'), E.gradient(f.gradient));
  E.putMethod(d, f.method || 'perceptual');
  d.putEnumerated(c('Type'), c('GrdT'), c(E.STYLE[f.style || 'linear']));
  d.putUnitDouble(c('Angl'), c('#Ang'), E.or(f.angle, 90));
  d.putUnitDouble(c('Scl '), c('#Prc'), E.or(f.scale, 100));
  d.putBoolean(c('Rvrs'), !!f.reverse);
  d.putBoolean(c('Dthr'), !!f.dither);
  d.putBoolean(c('Algn'), f.align !== false);
  o.putUnitDouble(c('Hrzn'), c('#Prc'), off[0]);
  o.putUnitDouble(c('Vrtc'), c('#Prc'), off[1]);
  d.putObject(c('Ofst'), c('Pnt '), o);
};
// A fill layer of D40's content, {color} or a GradientFill, made above the active layer: [class, descriptor].
E.makeFill = function (content) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor(), r = new ActionReference(), use = new ActionDescriptor();
  var t = new ActionDescriptor(), cls;
  if (content.color !== undefined) {
    t.putObject(c('Clr '), c('RGBC'), E.rgbc(content.color));
    cls = s('solidColorLayer');
  } else {
    E.gradientFill(t, content);
    cls = s('gradientLayer');
  }
  r.putClass(s('contentLayer'));
  d.putReference(c('null'), r);
  use.putObject(c('Type'), cls, t);
  d.putObject(c('Usng'), s('contentLayer'), use);
  executeAction(c('Mk  '), d, DialogModes.NO);
  return [cls, t];
};

// Layer styles: L30's fx (LayerStyleDialog's keys: on, blend, color '#rrggbb', opacity %, angle, distance, spread %,
// size, noise %, knockOut; position; style, gradient...; globalLight, antiAliased, range, altitude, depth this
// script's) as Photoshop's Lefx. The P2 effects by this script's names.
E.FX = {dropShadow: 'DrSh', innerShadow: 'IrSh', outerGlow: 'OrGl', innerGlow: 'IrGl', bevelEmboss: 'ebbl', satin: 'ChFX',
        colorOverlay: 'SoFi', gradientOverlay: 'GrFl', stroke: 'FrFX'};
E.MULTI = {dropShadowMulti: ['dropShadowMulti', 'DrSh'], strokeMulti: ['frameFXMulti', 'FrFX'],
           colorOverlayMulti: ['solidFillMulti', 'SoFi'], gradientOverlayMulti: ['gradientFillMulti', 'GrFl']};
E.contour = function () {  // Linear
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList();
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
E.effect = function (name, v) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor();
  var unit = function (key, u, x) { d.putUnitDouble(c(key), c(u), x); };
  d.putBoolean(c('enab'), v.on !== false);
  d.putBoolean(s('present'), true);
  d.putBoolean(s('showInDialog'), true);
  if (name == 'bevelEmboss') {
    d.putEnumerated(c('hglM'), c('BlnM'), E.blnm('screen'));
    d.putObject(c('hglC'), c('RGBC'), E.rgbc('#ffffff'));
    unit('hglO', '#Prc', 75);
    d.putEnumerated(c('sdwM'), c('BlnM'), E.blnm('multiply'));
    d.putObject(c('sdwC'), c('RGBC'), E.rgbc('#000000'));
    unit('sdwO', '#Prc', 75);
    d.putEnumerated(c('bvlT'), c('bvlT'), c('SfBL'));
    d.putEnumerated(c('bvlS'), c('BESl'), c('InrB'));
    d.putBoolean(c('uglg'), false);
    unit('lagl', '#Ang', v.angle);
    unit('Lald', '#Ang', v.altitude);
    unit('srgR', '#Prc', v.depth);
    unit('blur', '#Pxl', v.size);
    d.putEnumerated(c('bvlD'), c('BESs'), c('In  '));
    d.putObject(c('TrnS'), c('ShpC'), E.contour());
    d.putBoolean(s('antialiasGloss'), false);
    unit('Sftn', '#Pxl', 0);
    d.putBoolean(s('useShape'), false);
    d.putBoolean(s('useTexture'), false);
    return d;
  }
  d.putEnumerated(c('Md  '), c('BlnM'), E.blnm(v.blend));
  if (v.color !== undefined) d.putObject(c('Clr '), c('RGBC'), E.rgbc(v.color));
  unit('Opct', '#Prc', v.opacity);
  if (name == 'dropShadow' || name == 'innerShadow') {
    d.putBoolean(c('uglg'), !!v.globalLight);
    unit('lagl', '#Ang', v.angle);
    unit('Dstn', '#Pxl', v.distance);
    unit('Ckmt', '#Prc', v.spread);
    unit('blur', '#Pxl', v.size);
    unit('Nose', '#Prc', v.noise || 0);
    d.putBoolean(c('AntA'), !!v.antiAliased);
    d.putObject(c('TrnS'), c('ShpC'), E.contour());
    if (name == 'dropShadow') d.putBoolean(s('layerConceals'), v.knockOut !== false);
  } else if (name == 'outerGlow' || name == 'innerGlow') {
    d.putEnumerated(c('GlwT'), c('BETE'), c('SfBL'));
    unit('Ckmt', '#Prc', v.spread);
    unit('blur', '#Pxl', v.size);
    unit('Nose', '#Prc', v.noise || 0);
    unit('ShdN', '#Prc', 0);
    d.putBoolean(c('AntA'), !!v.antiAliased);
    d.putObject(c('TrnS'), c('ShpC'), E.contour());
    unit('Inpr', '#Prc', E.or(v.range, 50));
    if (name == 'innerGlow') d.putEnumerated(c('glwS'), c('IGSr'), c('SrcE'));
  } else if (name == 'satin') {
    d.putBoolean(c('AntA'), !!v.antiAliased);
    d.putBoolean(c('Invr'), v.invert !== false);
    unit('lagl', '#Ang', v.angle);
    unit('Dstn', '#Pxl', v.distance);
    unit('blur', '#Pxl', v.size);
    d.putObject(c('MpgS'), c('ShpC'), E.contour());
  } else if (name == 'gradientOverlay') {
    E.gradientFill(d, v);
  } else if (name == 'stroke') {
    var pos = {outside: 'OutF', inside: 'InsF', center: 'CtrF'}[v.position];
    if (!pos) throw new Error('stroke position ' + v.position);
    d.putEnumerated(c('Styl'), c('FStl'), c(pos));
    d.putEnumerated(c('PntT'), c('FrFl'), c(v.fillType == 'gradient' ? 'GrFl' : 'SClr'));
    unit('Sz  ', '#Pxl', v.size);
    d.putBoolean(s('overprint'), false);
    if (v.fillType == 'gradient') E.gradientFill(d, v);
  }
  return d;
};
// The active layer's style as fx has it; returns the Lefx set, to check what Photoshop read.
E.setStyle = function (fx) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor(), r = new ActionReference(), l = new ActionDescriptor(), k, i;
  r.putProperty(c('Prpr'), c('Lefx'));
  r.putEnumerated(c('Lyr '), c('Ordn'), c('Trgt'));
  d.putReference(c('null'), r);
  l.putUnitDouble(c('Scl '), c('#Prc'), E.or(fx.scale, 100));
  for (k in fx)
    if (fx.hasOwnProperty(k) && !E.FX[k] && !E.MULTI[k] && k != 'scale' && k != 'on') throw new Error('fx ' + k);
  for (k in E.FX)
    if (E.FX.hasOwnProperty(k) && fx[k]) l.putObject(c(E.FX[k]), c(E.FX[k]), E.effect(k, fx[k]));
  for (k in E.MULTI)
    if (E.MULTI.hasOwnProperty(k) && fx[k]) {
      var list = new ActionList();
      for (i = 0; i < fx[k].length; i++) list.putObject(c(E.MULTI[k][1]), E.effect(k.replace('Multi', ''), fx[k][i]));
      l.putList(s(E.MULTI[k][0]), list);
    }
  d.putObject(c('T   '), c('Lefx'), l);
  executeAction(c('setd'), d, DialogModes.NO);
  if (fx.on === false) {  // the eye of Effects: every effect hidden
    var h = new ActionDescriptor(), refs = new ActionList(), e = new ActionReference();
    e.putClass(c('Lefx'));
    e.putEnumerated(c('Lyr '), c('Ordn'), c('Trgt'));
    refs.putReference(e);
    h.putList(c('null'), refs);
    executeAction(c('Hd  '), h, DialogModes.NO);
  }
  return l;
};
// The document's Global Light (M44: resources 0x040D, 0x0419).
E.globalLight = function (angle, altitude) {
  var c = PB.c2t, d = new ActionDescriptor(), r = new ActionReference(), g = new ActionDescriptor();
  r.putProperty(c('Prpr'), c('gblA'));
  r.putEnumerated(c('Dcmn'), c('Ordn'), c('Trgt'));
  d.putReference(c('null'), r);
  g.putUnitDouble(c('gagl'), c('#Ang'), angle);
  g.putUnitDouble(PB.s2t('globalAltitude'), c('#Ang'), altitude);
  d.putObject(c('T   '), c('gblA'), g);
  executeAction(c('setd'), d, DialogModes.NO);
};

// Shapes: Y32's source as this script writes it ({type, box [left, top, right, bottom], radii [tl, tr, br, bl],
// radius, sides, star %, from, to, weight, points, fill '#rrggbb' or null, stroke {width, color, align, cap, join} or
// null}). Live shapes are Photoshop 2021's (22): the descriptors of Triangle, Polygon and Line are the least sure
// (README-psd.md), so each has variants, tried in order, and the one that made it goes into photoshop.txt.
E.CAP = {butt: 'strokeStyleButtCap', round: 'strokeStyleRoundCap', square: 'strokeStyleSquareCap'};
E.JOIN = {miter: 'strokeStyleMiterJoin', round: 'strokeStyleRoundJoin', bevel: 'strokeStyleBevelJoin'};
E.ALIGN = {inside: 'strokeStyleAlignInside', center: 'strokeStyleAlignCenter', outside: 'strokeStyleAlignOutside'};
E.strokeStyle = function (src) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor(), st = src.stroke, content = new ActionDescriptor();
  var e = function (key, value) { d.putEnumerated(s(key), s(key), s(value)); };
  d.putInteger(s('strokeStyleVersion'), 2);
  d.putBoolean(s('strokeEnabled'), !!st);
  d.putBoolean(s('fillEnabled'), src.fill !== null);
  d.putUnitDouble(s('strokeStyleLineWidth'), c('#Pxl'), st ? st.width : 1);
  d.putUnitDouble(s('strokeStyleLineDashOffset'), c('#Pnt'), 0);
  d.putDouble(s('strokeStyleMiterLimit'), 100);
  e('strokeStyleLineCapType', E.CAP[st && st.cap || 'butt']);
  e('strokeStyleLineJoinType', E.JOIN[st && st.join || 'miter']);
  e('strokeStyleLineAlignment', E.ALIGN[st && st.align || 'inside']);
  d.putBoolean(s('strokeStyleScaleLock'), false);
  d.putBoolean(s('strokeStyleStrokeAdjust'), false);
  d.putList(s('strokeStyleLineDashSet'), new ActionList());
  d.putEnumerated(s('strokeStyleBlendMode'), c('BlnM'), c('Nrml'));
  d.putUnitDouble(s('strokeStyleOpacity'), c('#Prc'), 100);
  content.putObject(c('Clr '), c('RGBC'), E.rgbc(st ? st.color : '#000000'));
  d.putObject(s('strokeStyleContent'), s('solidColorLayer'), content);
  d.putDouble(s('strokeStyleResolution'), 72);
  return d;
};
// The shape's geometry as each variant writes it: [[name, class, descriptor], ...].
E.shapeVariants = function (src) {
  var c = PB.c2t, s = PB.s2t, b = src.box, out = [];
  var quad = function (d) {
    d.putInteger(s('unitValueQuadVersion'), 1);
    d.putUnitDouble(c('Top '), c('#Pxl'), b[1]);
    d.putUnitDouble(c('Left'), c('#Pxl'), b[0]);
    d.putUnitDouble(c('Btom'), c('#Pxl'), b[3]);
    d.putUnitDouble(c('Rght'), c('#Pxl'), b[2]);
    return d;
  };
  var point = function (p) {
    var d = new ActionDescriptor();
    d.putUnitDouble(c('Hrzn'), c('#Pxl'), p[0]);
    d.putUnitDouble(c('Vrtc'), c('#Pxl'), p[1]);
    return d;
  };
  var d;
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
  } else if (src.type == 'triangle' || src.type == 'polygon') {
    var sides = src.type == 'triangle' ? 3 : src.sides;
    d = quad(new ActionDescriptor());  // Photoshop 2021's Triangle and Polygon tools, as best known
    d.putInteger(s('sides'), sides);
    d.putUnitDouble(s('polygonCornerRadius'), c('#Pxl'), src.radius || 0);
    if (src.type == 'polygon') d.putDouble(s('starRatio'), E.or(src.star, 100));
    out.push([src.type, s(src.type), d]);
    d = quad(new ActionDescriptor());
    d.putInteger(s('sides'), sides);
    d.putUnitDouble(s('polygonCornerRadius'), c('#Pxl'), src.radius || 0);
    if (src.type == 'polygon') d.putDouble(s('starRatio'), E.or(src.star, 100));
    out.push(['polygon', s('polygon'), d]);
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
// A shape layer above the active layer; returns its fill's [class, descriptor] (what can be read back of it).
E.makeShape = function (doc, src) {
  if (src.type == 'path') return E.makePathShape(doc, src);
  var c = PB.c2t, s = PB.s2t, variants = E.shapeVariants(src), errors = [];
  for (var i = 0; i < variants.length; i++) {
    var d = new ActionDescriptor(), r = new ActionReference(), use = new ActionDescriptor(), fill = new ActionDescriptor();
    fill.putObject(c('Clr '), c('RGBC'), E.rgbc(src.fill || '#000000'));
    use.putObject(c('Type'), s('solidColorLayer'), fill);
    use.putObject(c('Shp '), variants[i][1], variants[i][2]);
    use.putObject(s('strokeStyle'), s('strokeStyle'), E.strokeStyle(src));
    r.putClass(s('contentLayer'));
    d.putReference(c('null'), r);
    d.putObject(c('Usng'), s('contentLayer'), use);
    try {
      executeAction(c('Mk  '), d, DialogModes.NO);
      PB.note('  ' + src.type + ': made as ' + variants[i][0] + (errors.length ? ' (' + errors.join('; ') + ')' : ''));
      return [s('solidColorLayer'), fill];
    } catch (e) {
      errors.push(variants[i][0] + ': ' + e);
    }
  }
  throw new Error('no ' + src.type + ' made: ' + errors.join('; '));
};
// A closed path of corner points, saved in the Paths panel.
E.path = function (doc, points) {
  var anchors = [];
  for (var i = 0; i < points.length; i++) {
    var p = new PathPointInfo();
    p.kind = PointKind.CORNERPOINT;
    p.anchor = points[i];
    p.leftDirection = points[i];
    p.rightDirection = points[i];
    anchors.push(p);
  }
  var sub = new SubPathInfo();
  sub.operation = ShapeOperation.SHAPEADD;
  sub.closed = true;
  sub.entireSubPath = anchors;
  return doc.pathItems.add('e33 path', [sub]);
};
// A shape layer of a path (not a live shape: P2's path, M46's notice): a Solid Color fill layer made while the path
// is active takes it as its vector mask.
E.makePathShape = function (doc, src) {
  var path = E.path(doc, src.points);
  path.select();
  try {
    return E.makeFill({color: src.fill});
  } finally {
    path.remove();
  }
};
// A vector mask of a path on the active layer (Layer > Vector Mask > Current Path).
E.addVectorMask = function (doc, layer, points) {
  var c = PB.c2t, s = PB.s2t, path = E.path(doc, points);
  doc.activeLayer = layer;
  path.select();
  try {
    var d = new ActionDescriptor(), r = new ActionReference(), at = new ActionReference(), use = new ActionReference();
    r.putClass(c('Pth '));
    d.putReference(c('null'), r);
    at.putEnumerated(c('Pth '), c('Pth '), s('vectorMask'));
    d.putReference(c('At  '), at);
    use.putEnumerated(c('Pth '), c('Ordn'), c('Trgt'));
    d.putReference(c('Usng'), use);
    executeAction(c('Mk  '), d, DialogModes.NO);
  } finally {
    path.remove();
  }
};

// ---- Adjustments (PB.adjustments, by AdjustSpec.ORDER name) ---------------------------------------------------------

// vals as index.json has them: AdjustSpec's keys, off their defaults; where Photoshop keeps a set of values for each
// channel, range, tone or colour and AdjustSpec one set and a selector (Levels' and Curves' ch, Hue/Saturation's rng,
// Color Balance's tone, Channel Mixer's out, Selective Color's col), an object for each, by the selector's name
// (AdjustSpec.LISTS), the selector itself left out: Levels {RGB: {in0, gam, in1, out0, out1}, Red: {...}}, Curves'
// pts as AdjustSpec has them. README-psd.md lists each.
E.preset = function (d) {
  d.putEnumerated(PB.s2t('presetKind'), PB.s2t('presetKindType'), PB.s2t('presetKindCustom'));
};
E.CHANNELS = [['RGB', 'Cmps'], ['Red', 'Rd  '], ['Green', 'Grn '], ['Blue', 'Bl  ']];
E.channelRef = function (id) {
  var r = new ActionReference();
  r.putEnumerated(PB.c2t('Chnl'), PB.c2t('Chnl'), PB.c2t(id));
  return r;
};
PB.adjustments['Brightness/Contrast'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  d.putInteger(c('Brgh'), v.b || 0);
  d.putInteger(c('Cntr'), v.c || 0);
  d.putBoolean(PB.s2t('useLegacy'), !!v.legacy);
  return [c('BrgC'), d];
};
PB.adjustments.Levels = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList();
  E.preset(d);
  for (var i = 0; i < E.CHANNELS.length; i++) {
    var ch = v[E.CHANNELS[i][0]];
    if (!ch) continue;
    var a = new ActionDescriptor(), into = new ActionList(), out = new ActionList();
    a.putReference(c('Chnl'), E.channelRef(E.CHANNELS[i][1]));
    into.putInteger(E.or(ch.in0, 0));
    into.putInteger(E.or(ch.in1, 255));
    a.putList(c('Inpt'), into);
    a.putDouble(c('Gmm '), E.or(ch.gam, 1));
    out.putInteger(E.or(ch.out0, 0));
    out.putInteger(E.or(ch.out1, 255));
    a.putList(c('Otpt'), out);
    list.putObject(c('LvlA'), a);
  }
  d.putList(c('Adjs'), list);
  return [c('Lvls'), d];
};
PB.adjustments.Curves = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), pts = v.pts || {};
  E.preset(d);
  for (var i = 0; i < E.CHANNELS.length; i++) {
    var p = pts[E.CHANNELS[i][0]];
    if (!p) continue;
    var a = new ActionDescriptor(), curve = new ActionList();
    a.putReference(c('Chnl'), E.channelRef(E.CHANNELS[i][1]));
    for (var j = 0; j < p.length; j++) {
      var q = new ActionDescriptor();
      q.putDouble(c('Hrzn'), p[j][0]);
      q.putDouble(c('Vrtc'), p[j][1]);
      curve.putObject(c('Pnt '), q);
    }
    a.putList(c('Crv '), curve);
    list.putObject(c('CrvA'), a);
  }
  d.putList(c('Adjs'), list);
  return [c('Crvs'), d];
};
PB.adjustments.Exposure = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  E.preset(d);
  d.putDouble(c('Exps'), E.or(v.e, 0));
  d.putDouble(c('Ofst'), E.or(v.o, 0));
  d.putDouble(PB.s2t('gammaCorrection'), E.or(v.g, 1));
  return [c('Exps'), d];
};
PB.adjustments.Vibrance = function (v) {
  var d = new ActionDescriptor();
  d.putInteger(PB.s2t('vibrance'), E.or(v.v, 0));
  d.putInteger(PB.c2t('Strt'), E.or(v.s, 0));
  return [PB.s2t('vibrance'), d];
};
E.HUES = ['Reds', 'Yellows', 'Greens', 'Cyans', 'Blues', 'Magentas'];  // LclR 1..6
E.SPANS = {Reds: [315, 345, 15, 45], Yellows: [15, 45, 75, 105], Greens: [75, 105, 135, 165],
           Cyans: [135, 165, 195, 225], Blues: [195, 225, 255, 285], Magentas: [255, 285, 315, 345]};  // AdjustSpec's
PB.adjustments['Hue/Saturation'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), m = v.Master || {}, a, i;
  E.preset(d);
  d.putBoolean(c('Clrz'), !!v.colorize);
  a = new ActionDescriptor();  // Master; with Colorize its hue 0..360 and saturation 0..100 are the colour's
  a.putInteger(c('H   '), E.or(m.h, 0));
  a.putInteger(c('Strt'), E.or(m.s, 0));
  a.putInteger(c('Lght'), E.or(m.l, 0));
  list.putObject(c('Hst2'), a);
  for (i = 0; i < E.HUES.length; i++) {
    var r = v[E.HUES[i]];
    if (!r) continue;
    var span = r.span || E.SPANS[E.HUES[i]];
    a = new ActionDescriptor();
    a.putInteger(c('LclR'), i + 1);
    a.putInteger(c('BgnR'), span[0]);
    a.putInteger(c('BgnS'), span[1]);
    a.putInteger(c('EndS'), span[2]);
    a.putInteger(c('EndR'), span[3]);
    a.putInteger(c('H   '), E.or(r.h, 0));
    a.putInteger(c('Strt'), E.or(r.s, 0));
    a.putInteger(c('Lght'), E.or(r.l, 0));
    list.putObject(c('Hst2'), a);
  }
  d.putList(c('Adjs'), list);
  return [c('HStr'), d];
};
PB.adjustments['Color Balance'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), tones = [['Shadows', 'ShdL'], ['Midtones', 'MdtL'], ['Highlights', 'HghL']];
  for (var i = 0; i < tones.length; i++) {
    var t = v[tones[i][0]] || {}, list = new ActionList();
    list.putInteger(E.or(t.cr, 0));
    list.putInteger(E.or(t.mg, 0));
    list.putInteger(E.or(t.yb, 0));
    d.putList(c(tones[i][1]), list);
  }
  d.putBoolean(c('PrsL'), v.pl !== false);
  return [c('ClrB'), d];
};
PB.adjustments['Black & White'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  E.preset(d);
  d.putInteger(c('Rd  '), E.or(v.r, 40));
  d.putInteger(c('Yllw'), E.or(v.y, 60));
  d.putInteger(c('Grn '), E.or(v.g, 40));
  d.putInteger(c('Cyn '), E.or(v.c, 60));
  d.putInteger(c('Bl  '), E.or(v.b, 20));
  d.putInteger(c('Mgnt'), E.or(v.m, 80));
  d.putBoolean(PB.s2t('useTint'), !!v.tint);
  d.putObject(PB.s2t('tintColor'), c('RGBC'), E.rgbc(v.tintColor || '#c8a27c'));
  return [c('BanW'), d];
};
PB.adjustments['Photo Filter'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  if (v.pf !== 1) throw new Error('Photo Filter: a colour of its own only here (pf 1)');
  d.putObject(c('Clr '), c('RGBC'), E.rgbc(v.pf1));
  d.putInteger(c('Dnst'), E.or(v.d, 25));
  d.putBoolean(c('PrsL'), v.pl !== false);
  return [PB.s2t('photoFilter'), d];
};
PB.adjustments['Channel Mixer'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), unit = {Red: [100, 0, 0], Green: [0, 100, 0], Blue: [0, 0, 100], Gray: [100, 0, 0]};
  var outs = v.mono ? [['Gray', 'Gry ']] : [['Red', 'Rd  '], ['Green', 'Grn '], ['Blue', 'Bl  ']];
  E.preset(d);
  d.putBoolean(c('Mnch'), !!v.mono);
  for (var i = 0; i < outs.length; i++) {
    var o = v[outs[i][0]] || {}, base = unit[outs[i][0]], m = new ActionDescriptor();
    m.putUnitDouble(c('Rd  '), c('#Prc'), E.or(o.r, base[0]));
    m.putUnitDouble(c('Grn '), c('#Prc'), E.or(o.g, base[1]));
    m.putUnitDouble(c('Bl  '), c('#Prc'), E.or(o.b, base[2]));
    m.putUnitDouble(c('Cnst'), c('#Prc'), E.or(o.k, 0));
    d.putObject(c(outs[i][1]), c('ChMx'), m);
  }
  return [c('ChnM'), d];
};
// A 3D LUT of a file: its bytes embedded as when it is loaded in the dialog (E.lutFile: the .cube this script writes).
PB.adjustments['Color Lookup'] = function (v) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor();
  if (!E.lutFile) throw new Error('Color Lookup: no LUT file written');
  d.putEnumerated(s('lookupType'), s('colorLookupType'), s('3DLUT'));
  d.putString(c('Nm  '), E.lutFile.fsName);
  d.putBoolean(c('Dthr'), !!v.dither);
  d.putEnumerated(s('LUTFormat'), s('LUTFormatType'), s('LUTFormatCUBE'));
  d.putData(s('LUT3DFileData'), v.lut);
  d.putString(s('LUT3DFileName'), E.lutFile.fsName);
  return [s('colorLookup'), d];
};
PB.adjustments.Posterize = function (v) {
  var d = new ActionDescriptor();
  d.putInteger(PB.c2t('Lvls'), E.or(v.lv, 4));
  return [PB.c2t('Pstr'), d];
};
PB.adjustments.Threshold = function (v) {
  var d = new ActionDescriptor();
  d.putInteger(PB.c2t('Lvl '), E.or(v.t, 128));
  return [PB.c2t('Thrs'), d];
};
PB.adjustments['Gradient Map'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  d.putObject(c('Grad'), c('Grdn'), E.gradient(v.gradient));
  d.putBoolean(c('Rvrs'), !!v.rev);
  d.putBoolean(c('Dthr'), !!v.dither);
  E.putMethod(d, v.method || 'perceptual');
  return [c('GdMp'), d];
};
E.SELECTIVE = [['Reds', 'Rds '], ['Yellows', 'Ylws'], ['Greens', 'Grns'], ['Cyans', 'Cyns'], ['Blues', 'Bls '],
               ['Magentas', 'Mgnt'], ['Whites', 'Whts'], ['Neutrals', 'Ntrl'], ['Blacks', 'Blks']];
PB.adjustments['Selective Color'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList();
  E.preset(d);
  for (var i = 0; i < E.SELECTIVE.length; i++) {
    var o = v[E.SELECTIVE[i][0]];
    if (!o) continue;
    var a = new ActionDescriptor();
    a.putEnumerated(c('Clrs'), c('Clrs'), c(E.SELECTIVE[i][1]));
    a.putUnitDouble(c('Cyn '), c('#Prc'), E.or(o.c, 0));
    a.putUnitDouble(c('Mgnt'), c('#Prc'), E.or(o.m, 0));
    a.putUnitDouble(c('Yllw'), c('#Prc'), E.or(o.y, 0));
    a.putUnitDouble(c('Blck'), c('#Prc'), E.or(o.k, 0));
    list.putObject(c('ClrC'), a);
  }
  d.putList(c('ClrC'), list);
  d.putEnumerated(c('Mthd'), c('CrcM'), c(v.rel ? 'Absl' : 'Rltv'));
  return [c('SlcC'), d];
};
// The LUT of adj-color-lookup: a warm look on a 5 x 5 x 5 lattice, red fastest (.cube), six decimals.
E.cube = function () {
  var n = 5, lines = ['TITLE "photo-beauty E33 warm"', 'LUT_3D_SIZE ' + n];
  for (var b = 0; b < n; b++)
    for (var g = 0; g < n; g++)
      for (var r = 0; r < n; r++) {
        var R = r / (n - 1), G = g / (n - 1), B = b / (n - 1);
        var out = [Math.min(1, 0.04 + 0.98 * Math.pow(R, 0.9)), Math.pow(G, 1.05), 0.9 * B + 0.02 * (1 - B)];
        lines.push(out[0].toFixed(6) + ' ' + out[1].toFixed(6) + ' ' + out[2].toFixed(6));
      }
  return lines.join('\n') + '\n';
};
E.LUT_NAME = 'e33-warm.cube';

// ---- Building a case -----------------------------------------------------------------------------------------------

// The case's document: its mode, depth and canvas, transparent; RGB in sRGB, the others in Photoshop's working
// spaces (Color Settings: photoshop.txt has their names).
E.newDocument = function (c) {
  var mode = {rgb: NewDocumentMode.RGB, gray: NewDocumentMode.GRAYSCALE, cmyk: NewDocumentMode.CMYK,
              lab: NewDocumentMode.LAB}[c.mode || 'rgb'];
  var bits = {8: BitsPerChannelType.EIGHT, 16: BitsPerChannelType.SIXTEEN, 32: BitsPerChannelType.THIRTYTWO}[c.depth || 8];
  if (!mode || !bits) throw new Error('mode ' + c.mode + ', depth ' + c.depth);
  if ((c.mode || 'rgb') == 'rgb')
    return app.documents.add(c.canvas[0], c.canvas[1], 72, c.name, mode, DocumentFill.TRANSPARENT, 1, bits, PB.SRGB);
  return app.documents.add(c.canvas[0], c.canvas[1], 72, c.name, mode, DocumentFill.TRANSPARENT, 1, bits);
};
// A pixel layer: an input copied (cell, then move) or a new one painted.
E.pixel = function (doc, l) {
  var layer;
  if (l.src) {
    layer = PB.copyLayer(doc, l.src, l.rotate || 0);
    doc.activeLayer = layer;
    if (l.cell) E.keepOnly(doc, l.cell);
  } else {
    layer = doc.artLayers.add();
  }
  doc.activeLayer = layer;
  if (l.paint)
    for (var i = 0; i < l.paint.length; i++) {
      doc.selection.select(E.region(l.paint[i]));
      doc.selection.fill(E.solid(l.paint[i][4]));
    }
  doc.selection.deselect();
  if (l.move) layer.translate(l.move[0], l.move[1]);
  return layer;
};
E.keepOnly = function (doc, r) {  // the active layer's pixels in r only
  if (r[0] <= 0 && r[1] <= 0 && r[0] + r[2] >= doc.width.as('px') && r[1] + r[3] >= doc.height.as('px')) return;
  doc.selection.select(E.region(r));
  doc.selection.invert();
  doc.selection.clear();
  doc.selection.deselect();
};
// The selection from an input's transparency (lib.jsx's PB.maskFrom's way), or a rectangle.
E.selectFrom = function (doc, from) {
  if (typeof from != 'string') {
    doc.selection.select(E.region(from));
    return;
  }
  var c = PB.c2t, keep = doc.activeLayer, tmp = PB.copyLayer(doc, from, 0), d = new ActionDescriptor();
  var r = new ActionReference(), t = new ActionReference();
  doc.activeLayer = tmp;
  r.putProperty(c('Chnl'), c('fsel'));
  d.putReference(c('null'), r);
  t.putEnumerated(c('Chnl'), c('Chnl'), c('Trsp'));
  d.putReference(c('T   '), t);
  executeAction(c('setd'), d, DialogModes.NO);
  tmp.remove();
  doc.activeLayer = keep;
};
E.makeMask = function (using) {  // on the active layer: RvlS (the selection), RvlA, HdAl
  var c = PB.c2t, d = new ActionDescriptor(), r = new ActionReference();
  d.putClass(c('Nw  '), c('Chnl'));
  r.putEnumerated(c('Chnl'), c('Chnl'), c('Msk '));
  d.putReference(c('At  '), r);
  d.putEnumerated(c('Usng'), c('UsrM'), c(using));
  executeAction(c('Mk  '), d, DialogModes.NO);
};
E.hasMask = function (l) { return !!(l.mask || l.maskRect || l.maskAll); };
E.label = function (color) {
  if (!E.LABELS[color]) throw new Error('no label colour ' + color);
  PB.setLayer(function (d) { d.putEnumerated(PB.c2t('Clr '), PB.c2t('Clr '), PB.c2t(E.LABELS[color])); });
};
E.ADVANCED = [['blendInterior', 'blendInterior'], ['blendClipped', 'blendClipped'],
              ['transparencyShapes', 'transparencyShapesLayer'], ['maskHidesEffects', 'layerMaskAsGlobalMask'],
              ['vectorMaskHidesEffects', 'vectorMaskAsGlobalMask']];
E.advanced = function (a) {
  var s = PB.s2t;
  PB.setLayer(function (d) {
    if (a.knockout) d.putEnumerated(s('knockout'), s('knockout'), s(a.knockout));
    for (var i = 0; i < E.ADVANCED.length; i++)
      if (a[E.ADVANCED[i][0]] !== undefined) d.putBoolean(s(E.ADVANCED[i][1]), a[E.ADVANCED[i][0]]);
  });
};
// Selects one more layer, the active ones staying.
E.selectMore = function (layer) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor(), r = new ActionReference();
  if (layer.id !== undefined) r.putIdentifier(c('Lyr '), layer.id);
  else r.putName(c('Lyr '), layer.name);
  d.putReference(c('null'), r);
  d.putEnumerated(s('selectionModifier'), s('selectionModifierType'), s('addToSelection'));
  d.putBoolean(c('MkVs'), false);
  executeAction(c('slct'), d, DialogModes.NO);
};
// An input placed as an embedded smart object (linked: Place Linked), in the middle of the canvas.
E.place = function (file, linked) {
  if (!file.exists) throw new Error('no input ' + file.fsName);
  var c = PB.c2t, d = new ActionDescriptor(), o = new ActionDescriptor();
  d.putPath(c('null'), file);
  if (linked) d.putBoolean(c('Lnkd'), true);
  d.putEnumerated(c('FTcs'), c('QCSt'), c('Qcsa'));
  o.putUnitDouble(c('Hrzn'), c('#Pxl'), 0);
  o.putUnitDouble(c('Vrtc'), c('#Pxl'), 0);
  d.putObject(c('Ofst'), c('Ofst'), o);
  executeAction(c('Plc '), d, DialogModes.NO);
  return app.activeDocument.activeLayer;
};
// A smart object: placed, of its own layers (made, set, checked, then Convert to Smart Object), or a copy of one.
E.makeSmart = function (doc, l, box) {
  var s = l.smart, t = l.place || {}, layer, i;
  if (s.file) {
    layer = E.place(new File(PB.inDir + '/' + s.file), s.linked);
  } else if (s.layers) {
    var inner = [];
    E.make(doc, s.layers, {set: box.set, last: box.last}, inner);
    for (i = 0; i < inner.length; i++) E.apply(doc, inner[i]);
    E.finish(doc, inner);
    for (i = 0; i < inner.length; i++) E.check(doc, inner[i]);
    doc.activeLayer = inner[0].layer;
    for (i = 1; i < inner.length; i++) E.selectMore(inner[i].layer);
    executeAction(PB.s2t('newPlacedLayer'), undefined, DialogModes.NO);
    layer = doc.activeLayer;
  } else {
    if (!E.smarts[s.id]) throw new Error(l.props.name + ': no smart object ' + s.id + ' below it');
    layer = E.smarts[s.id].duplicate();
  }
  if (s.file || s.layers) E.smarts[s.id] = layer;
  if (t.scale) layer.resize(t.scale, t.scale, AnchorPosition.MIDDLECENTER);
  if (t.rotate) layer.rotate(t.rotate, AnchorPosition.MIDDLECENTER);
  if (t.move) layer.translate(t.move[0], t.move[1]);
  var filters = l.smartFilters || [];
  for (i = 0; i < filters.length; i++) {
    if (filters[i].filter != 'gaussianBlur') throw new Error('no smart filter ' + filters[i].filter);
    layer.applyGaussianBlur(filters[i].radius);
  }
  return layer;
};
// Text: Y30's source as this script writes it ({text, font (PostScript name), size pt, color, tracking, aa, at [x, y]
// (point text: the baseline's start) or box [x, y, w, h] (paragraph text), align}). At 72 ppi a point is a pixel.
E.AA = {none: 'NONE', sharp: 'SHARP', crisp: 'CRISP', strong: 'STRONG', smooth: 'SMOOTH'};
E.JUSTIFY = {left: 'LEFT', center: 'CENTER', right: 'RIGHT', justifyLeft: 'LEFTJUSTIFIED', justifyCenter: 'CENTERJUSTIFIED',
             justifyRight: 'RIGHTJUSTIFIED', justifyAll: 'FULLYJUSTIFIED'};
E.fonts = null;
E.font = function (want) {  // want if Photoshop has it, else Space Grotesk's others, else Arial (noted)
  if (!E.fonts) {
    E.fonts = {};
    for (var i = 0; i < app.fonts.length; i++) E.fonts[app.fonts[i].postScriptName] = true;
  }
  var tries = [want, 'SpaceGrotesk-SemiBold', 'SpaceGrotesk-Regular', 'SpaceGrotesk-Light', 'ArialMT'];
  for (var j = 0; j < tries.length; j++)
    if (E.fonts[tries[j]]) {
      if (j) PB.note('  font ' + want + ' is not installed (README-psd.md): ' + tries[j] + ' instead');
      return tries[j];
    }
  throw new Error('no font ' + want + ', no Space Grotesk, no Arial');
};
E.makeText = function (doc, s) {
  var layer = doc.artLayers.add(), t;
  layer.kind = LayerKind.TEXT;
  t = layer.textItem;
  if (s.box) {
    t.kind = TextType.PARAGRAPHTEXT;
    t.position = [s.box[0], s.box[1]];
    t.width = new UnitValue(s.box[2], 'px');
    t.height = new UnitValue(s.box[3], 'px');
  } else {
    t.position = [s.at[0], s.at[1]];
  }
  t.contents = s.text;
  s.font = E.font(s.font);  // index.json gets the one used
  t.font = s.font;
  t.size = new UnitValue(s.size, 'pt');
  t.color = E.solid(s.color);
  if (s.tracking) t.tracking = s.tracking;
  t.antiAliasMethod = AntiAlias[E.AA[s.aa || 'smooth']];
  if (s.align) t.justification = Justification[E.JUSTIFY[s.align]];
  return layer;
};

// The layers of a case, bottom first, made and stacked as lib.jsx's PB.make (box {set, last}): [{json, layer}], groups
// before their children. Adjustment and fill layers lose the white mask Photoshop gives them (a case says where it
// has a mask); their descriptors are read back here.
E.make = function (doc, layers, box, built) {
  for (var i = 0; i < layers.length; i++) {
    var l = layers[i], p = l.props, item, made = null;
    if (l.kind == 'pixel') {
      item = E.pixel(doc, l);
    } else if (l.kind == 'group') {
      item = doc.layerSets.add();
    } else if (l.kind == 'adj') {
      doc.selection.deselect();
      item = PB.makeAdjustment(doc, p.adj, p.vals);
      made = PB.adjustments[p.adj](p.vals || {});
    } else if (l.kind == 'fill') {
      doc.selection.deselect();
      made = E.makeFill(p.content);
      item = doc.activeLayer;
    } else if (l.kind == 'text') {
      item = E.makeText(doc, p.source);
    } else if (l.kind == 'shape') {
      doc.selection.deselect();
      made = E.makeShape(doc, p.source);
      item = doc.activeLayer;
    } else if (l.kind == 'so') {
      item = E.makeSmart(doc, l, box);
    } else {
      throw new Error('no kind ' + l.kind);
    }
    if (l.kind == 'adj' || l.kind == 'fill' || (l.kind == 'shape' && p.source.type == 'path')) PB.deleteMask();
    if (made && made[1]) E.checkDescriptor(doc, item, made, p.name);
    item.name = p.name;
    PB.place(item, box);
    built.push({json: l, layer: item});
    if (l.kind == 'group') E.make(doc, l.children, {set: item, last: null}, built);
  }
};
// A layer's props and keys, as lib.jsx's PB.apply does them, and this script's.
E.apply = function (doc, b) {
  var l = b.json, p = l.props, layer = b.layer, group = l.kind == 'group';
  doc.activeLayer = layer;
  if (!p.background) {
    layer.blendMode = PB.blendMode(p.blend || (group ? 'passThrough' : 'normal'));
    if (p.opacity !== undefined) layer.opacity = p.opacity * 100;
    if (p.fill !== undefined) {
      if (group) throw new Error(p.name + ': fill on a group');
      layer.fillOpacity = p.fill * 100;
    }
  }
  if (p.clip) layer.grouped = true;
  if (p.visible === false) layer.visible = false;
  if (l.mask) {
    PB.maskFrom(doc, layer, l.mask);
  } else if (l.maskRect) {
    PB.deleteMask();
    doc.selection.select(E.region(l.maskRect));
    E.makeMask('RvlS');
    doc.selection.deselect();
  } else if (l.maskAll) {
    PB.deleteMask();
    doc.selection.deselect();
    E.makeMask(l.maskAll == 'black' ? 'HdAl' : 'RvlA');
  }
  doc.activeLayer = layer;
  if (p.maskDensity !== undefined || p.maskFeather !== undefined || p.maskOff || p.maskLinked === false) {
    if (!E.hasMask(l)) throw new Error(p.name + ': mask props without a mask');
    PB.setLayer(function (d) {
      if (p.maskDensity !== undefined) d.putUnitDouble(PB.s2t('userMaskDensity'), PB.c2t('#Prc'), p.maskDensity * 100);
      if (p.maskFeather !== undefined) d.putUnitDouble(PB.s2t('userMaskFeather'), PB.c2t('#Pxl'), p.maskFeather);
      if (p.maskOff) d.putBoolean(PB.s2t('userMaskEnabled'), false);
      if (p.maskLinked === false) d.putBoolean(PB.s2t('userMaskLinked'), false);
    });
  }
  if (p.blendIf) PB.blendIf(p.blendIf);
  if (p.channels) PB.channels(p.channels);
  if (p.color && p.color != 'none') E.label(p.color);
  if (l.advanced) E.advanced(l.advanced);
  if (l.vectorMask) E.addVectorMask(doc, layer, l.vectorMask);
  if (p.fx) E.checkStyle(doc, layer, E.setStyle(p.fx), p.name);
};
// After every layer's props: the Background, then the locks (they stop the edits before them).
E.finish = function (doc, built) {
  var i, p, layer;
  for (i = 0; i < built.length; i++)
    if (built[i].json.props.background) {
      doc.activeLayer = built[i].layer;
      built[i].layer.isBackgroundLayer = true;
    }
  for (i = 0; i < built.length; i++) {
    p = built[i].json.props, layer = built[i].layer;
    if (p.background) continue;
    if (p.lockTransparency) layer.transparentPixelsLocked = true;
    if (p.lockPixels) layer.pixelsLocked = true;
    if (p.lockPosition) layer.positionLocked = true;
    if (p.lockAll) layer.allLocked = true;
  }
};
// The case built in a new document and checked; closed again if it cannot be.
E.build = function (c) {
  var doc = E.newDocument(c);
  try {
    if (c.globalLight) E.globalLight(c.globalLight[0], c.globalLight[1]);
    var placeholder = doc.layers[0], built = [], i;  // the new document's empty layer: under the case's until they are in
    E.smarts = {};
    E.make(doc, c.layers, {set: null, last: placeholder}, built);
    placeholder.remove();
    for (i = 0; i < built.length; i++) E.apply(doc, built[i]);
    E.finish(doc, built);
    doc.selection.deselect();
    var got = PB.json(PB.tree(doc.layers)), want = PB.json(PB.wantTree(c.layers));
    if (got != want) throw new Error('the layers came out ' + got + ', not ' + want);
    for (i = 0; i < built.length; i++) E.check(doc, built[i]);
    E.document(doc, c);
  } catch (e) {
    PB.close(doc);
    throw e;
  }
  return doc;
};
// The case's keys of the document, after its layers, checked as Photoshop reads them back.
E.document = function (doc, c) {
  var i, bad = [];
  if (c.crop) {
    doc.crop([c.crop[0], c.crop[1], c.crop[0] + c.crop[2], c.crop[1] + c.crop[3]]);
    c.canvas = [c.crop[2], c.crop[3]];
  }
  for (i = 0; c.alphas && i < c.alphas.length; i++) E.alpha(doc, c.alphas[i]);
  for (i = 0; c.spots && i < c.spots.length; i++) E.spot(doc, c.spots[i]);
  for (i = 0; c.guides && i < c.guides.length; i++)
    doc.guides.add(c.guides[i][0] == 'v' ? Direction.VERTICAL : Direction.HORIZONTAL, new UnitValue(c.guides[i][1], 'px'));
  if (c.meta) {
    var m = c.meta, info = doc.info;
    if (m.title) info.title = m.title;
    if (m.author) info.author = m.author;
    if (m.caption) info.caption = m.caption;
    if (m.keywords) info.keywords = m.keywords;
    if (m.copyright) {
      info.copyrighted = CopyrightedType.COPYRIGHTEDWORK;
      info.copyrightNotice = m.copyright;
    }
  }
  if (c.ppi) doc.resizeImage(undefined, undefined, c.ppi, ResampleMethod.NONE);
  if (doc.width.as('px') != c.canvas[0] || doc.height.as('px') != c.canvas[1])
    bad.push('canvas ' + doc.width.as('px') + ' x ' + doc.height.as('px'));
  if (c.ppi && Math.abs(doc.resolution - c.ppi) > 1e-3) bad.push('ppi ' + doc.resolution);
  var alphas = E.readAlphas(doc), want = (c.alphas || []).concat(c.spots || []);
  if (alphas.length != want.length) bad.push(alphas.length + ' alpha and spot channels');
  for (i = 0; i < want.length && i < alphas.length; i++)
    if (alphas[i].name != want[i].name || alphas[i].kind != (want[i].kind || (want[i].solidity !== undefined ? 'spot' : 'masked')))
      bad.push('channel ' + i + ' ' + PB.json(alphas[i]));
  // Guides: Photoshop may round one at a fraction of a pixel (128.5, 200.25); within half a pixel, noted and
  // index.json gets them as read back.
  var guides = E.readGuides(doc), wantGuides = c.guides || [], near = guides.length == wantGuides.length;
  for (i = 0; near && i < guides.length; i++)
    near = guides[i][0] == wantGuides[i][0] && Math.abs(guides[i][1] - wantGuides[i][1]) <= 0.5;
  if (!near) bad.push('guides ' + PB.json(guides));
  else if (PB.json(guides) != PB.json(wantGuides)) {
    PB.note('  guides read back as ' + PB.json(guides) + ', not ' + PB.json(wantGuides));
    c.guides = guides;
  }
  if (c.meta && c.meta.author && doc.info.author != c.meta.author) bad.push('author ' + doc.info.author);
  if (bad.length) throw new Error('the document: ' + bad.join('; '));
  if (c.convert) {
    E.convert(doc, c.convert);
    c.madeOf = c.layers;  // the layers it was flattened from
    c.layers = E.readTree(doc, doc.layers);
  }
  if (c.selected) doc.activeLayer = E.findLayer(doc.layers, c.selected);
};
E.alpha = function (doc, a) {  // a saved selection: {name, from (an input's transparency) | rect, kind, color, opacity}
  E.selectFrom(doc, a.from || a.rect);
  var ch = doc.channels.add();
  ch.name = a.name;
  ch.kind = a.kind == 'selected' ? ChannelType.SELECTEDAREA : ChannelType.MASKEDAREA;
  if (a.color) ch.color = E.solid(a.color);
  if (a.opacity !== undefined) ch.opacity = a.opacity * 100;
  doc.selection.store(ch, SelectionType.REPLACE);
  doc.selection.deselect();
  ch.visible = false;
  doc.activeChannels = doc.componentChannels;
};
E.spot = function (doc, s) {  // {name, rect (full ink there), color, solidity 0..1}
  var ch = doc.channels.add();
  ch.kind = ChannelType.SPOTCOLOR;
  ch.name = s.name;
  ch.color = E.solid(s.color);
  ch.opacity = s.solidity * 100;
  doc.selection.select(E.region(s.rect));
  doc.activeChannels = [ch];
  doc.selection.fill(E.solid('#000000'));
  doc.selection.deselect();
  doc.activeChannels = doc.componentChannels;
};
E.convert = function (doc, to) {  // flattened, then Indexed (64 colours, no dither) or Bitmap (50% Threshold)
  doc.flatten();
  if (to == 'indexed') {
    var o = new IndexedConversionOptions();
    o.palette = Palette.LOCALPERCEPTUAL;
    o.colors = 64;
    o.dither = Dither.NONE;
    o.forced = ForcedColors.NONE;
    o.transparency = false;
    doc.changeMode(ChangeMode.INDEXEDCOLOR, o);
  } else if (to == 'bitmap') {
    doc.changeMode(ChangeMode.GRAYSCALE);
    var b = new BitmapConversionOptions();
    b.method = BitmapConversionType.HALFTHRESHOLD;
    b.resolution = doc.resolution;
    doc.changeMode(ChangeMode.BITMAP, b);
  } else {
    throw new Error('no conversion to ' + to);
  }
};
E.findLayer = function (layers, name) {
  for (var i = 0; i < layers.length; i++) {
    if (layers[i].name == name) return layers[i];
    if (layers[i].typename == 'LayerSet') {
      var l = E.findLayer(layers[i].layers, name);
      if (l) return l;
    }
  }
  return null;
};

// ---- Reading back, checking ------------------------------------------------------------------------------------

// A layer's descriptor, by its id without changing the active layer; else (no id, or not found by it: a Background
// may not be) as the active layer.
E.layerDesc = function (doc, layer) {
  try {
    var r = new ActionReference();
    r.putIdentifier(PB.c2t('Lyr '), layer.id);
    return executeActionGet(r);
  } catch (e) {
    doc.activeLayer = layer;
    return PB.activeLayerDesc();
  }
};
E.flag = function (d, key, def) {
  return d && d.hasKey(key) && d.getType(key) == DescValueType.BOOLEANTYPE ? d.getBoolean(key) : def;
};
E.num = function (d, key, def) {
  if (!d || !d.hasKey(key)) return def;
  var t = d.getType(key);
  if (t == DescValueType.UNITDOUBLE) return d.getUnitDoubleValue(key);
  if (t == DescValueType.DOUBLETYPE) return d.getDouble(key);
  if (t == DescValueType.INTEGERTYPE) return d.getInteger(key);
  return def;
};
E.obj = function (d, key) {
  return d && d.hasKey(key) && d.getType(key) == DescValueType.OBJECTTYPE ? d.getObjectValue(key) : null;
};
E.opacityOf = function (percent) { return Math.round(percent * 2.55) / 255; };  // as Photoshop keeps opacity: a byte
E.blendOf = function (mode) {
  for (var m in PB.BLENDS)
    if (PB.BLENDS.hasOwnProperty(m) && PB.blendMode(m) == mode) return m;
  return String(mode);
};
E.ADJ_KINDS = [['BRIGHTNESSCONTRAST', 'Brightness/Contrast'], ['LEVELS', 'Levels'], ['CURVES', 'Curves'],
               ['EXPOSURE', 'Exposure'], ['VIBRANCE', 'Vibrance'], ['HUESATURATION', 'Hue/Saturation'],
               ['COLORBALANCE', 'Color Balance'], ['BLACKANDWHITE', 'Black & White'], ['PHOTOFILTER', 'Photo Filter'],
               ['CHANNELMIXER', 'Channel Mixer'], ['COLORLOOKUP', 'Color Lookup'], ['INVERSION', 'Invert'],
               ['POSTERIZE', 'Posterize'], ['THRESHOLD', 'Threshold'], ['GRADIENTMAP', 'Gradient Map'],
               ['SELECTIVECOLOR', 'Selective Color']];
E.adjOf = function (kind) {
  for (var i = 0; i < E.ADJ_KINDS.length; i++)
    if (LayerKind[E.ADJ_KINDS[i][0]] !== undefined && kind == LayerKind[E.ADJ_KINDS[i][0]]) return E.ADJ_KINDS[i][1];
  return null;
};
// photo-beauty's kind (Mock.KINDS) of a layer; a fill layer with a vector mask is a shape layer (layerKind 4).
E.kindOf = function (layer, d) {
  if (layer.typename == 'LayerSet') return 'group';
  var k = layer.kind, vector = E.flag(d, PB.s2t('hasVectorMask'), false) || E.num(d, PB.s2t('layerKind'), 0) == 4;
  if (k == LayerKind.TEXT) return 'text';
  if (k == LayerKind.SMARTOBJECT) return 'so';
  if (k == LayerKind.SOLIDFILL || k == LayerKind.GRADIENTFILL || k == LayerKind.PATTERNFILL) return vector ? 'shape' : 'fill';
  if (k == LayerKind.NORMAL) return 'pixel';
  if (E.adjOf(k)) return 'adj';
  return String(k);
};
E.labelOf = function (d) {
  var key = PB.s2t('color');
  if (!d.hasKey(key) || d.getType(key) != DescValueType.ENUMERATEDTYPE) return 'none';
  var v = d.getEnumerationValue(key);
  for (var n in E.LABELS)
    if (E.LABELS.hasOwnProperty(n) && PB.c2t(E.LABELS[n]) == v) return n;
  return PB.keyName(v);
};
// Blend If as A30's JSON (PB.blendIfOf's): the channels off 0, 0, 255, 255; undefined when none is.
E.blendIfOf = function (d) {
  var c = PB.c2t, key = PB.s2t('blendRange');
  if (!d.hasKey(key) || d.getType(key) != DescValueType.LISTTYPE) return undefined;
  var list = d.getList(key), out = {}, ids = {gray: 'Gry ', red: 'Rd  ', green: 'Grn ', blue: 'Bl  '};
  var keys = ['SrcB', 'Srcl', 'SrcW', 'Srcm', 'DstB', 'Dstl', 'DstW', 'Dstt'];
  for (var i = 0; i < list.count; i++) {
    var o = list.getObjectValue(i), v = [], name = null;
    for (var j = 0; j < keys.length; j++) v.push(E.num(o, c(keys[j]), j % 4 < 2 ? 0 : 255));
    try {
      var ch = o.getReference(c('Chnl')).getEnumeratedValue();
      for (var n in ids)
        if (ids.hasOwnProperty(n) && c(ids[n]) == ch) name = n;
    } catch (e) {}
    if (name) out[name] = {layer: v.slice(0, 4), under: v.slice(4)};
  }
  return PB.blendIfOf(out, 'read back');
};
E.channelsOf = function (d, doc) {
  var key = PB.s2t('channelRestrictions');
  if (doc.mode != DocumentMode.RGB || !d.hasKey(key) || d.getType(key) != DescValueType.LISTTYPE) return undefined;
  var list = d.getList(key), on = {r: false, g: false, b: false}, ids = {r: 'Rd  ', g: 'Grn ', b: 'Bl  '};
  for (var i = 0; i < list.count; i++)
    for (var n in ids)
      if (ids.hasOwnProperty(n) && list.getEnumerationValue(i) == PB.c2t(ids[n])) on[n] = true;
  return PB.channelsOf(on, 'read back');
};
// A layer as Photoshop has it, in photo-beauty's terms: {kind, props (every key, defaults too), mask, vector, fx, ...}.
E.readLayer = function (doc, layer) {
  var s = PB.s2t, d = E.layerDesc(doc, layer), group = layer.typename == 'LayerSet', p = {}, o = {desc: d};
  o.kind = E.kindOf(layer, d);
  p.name = layer.name;
  p.blend = E.blendOf(layer.blendMode);
  p.opacity = E.opacityOf(layer.opacity);
  p.fill = group ? 1 : E.opacityOf(layer.fillOpacity);
  p.visible = layer.visible;
  p.clip = E.flag(d, s('group'), false);
  var lock = E.obj(d, s('layerLocking'));
  p.lockTransparency = lock ? E.flag(lock, s('protectTransparency'), false) : !group && layer.transparentPixelsLocked;
  p.lockPixels = lock ? E.flag(lock, s('protectComposite'), false) : !group && layer.pixelsLocked;
  p.lockPosition = lock ? E.flag(lock, s('protectPosition'), false) : !group && layer.positionLocked;
  p.lockAll = lock ? E.flag(lock, s('protectAll'), false) : layer.allLocked;
  p.background = !group && layer.isBackgroundLayer;
  p.color = E.labelOf(d);
  o.mask = E.flag(d, s('hasUserMask'), false);
  if (o.mask) {
    p.maskDensity = Math.round(E.num(d, s('userMaskDensity'), 100) * 2.55) / 255;
    p.maskFeather = E.num(d, s('userMaskFeather'), 0);
    p.maskOff = !E.flag(d, s('userMaskEnabled'), true);
    p.maskLinked = E.flag(d, s('userMaskLinked'), true);
  }
  p.blendIf = E.blendIfOf(d);
  p.channels = E.channelsOf(d, doc);
  if (o.kind == 'adj') p.adj = E.adjOf(layer.kind);
  o.vector = E.flag(d, s('hasVectorMask'), false);
  var fx = E.obj(d, s('layerEffects'));
  if (fx) {
    o.fx = [];
    for (var i = 0; i < fx.count; i++)
      if (fx.getType(fx.getKey(i)) != DescValueType.UNITDOUBLE) o.fx.push(PB.keyName(fx.getKey(i)));
    if (!E.flag(d, s('layerFXVisible'), true)) o.fx.push('hidden');
  }
  if (o.kind == 'so') o.smart = E.smartInfo(d);
  if (o.kind == 'text') {
    try {
      o.text = {text: layer.textItem.contents, font: layer.textItem.font};
    } catch (e) {}
  }
  if (!group) {
    try {
      var b = layer.bounds;
      o.bounds = [b[0].as('px'), b[1].as('px'), b[2].as('px'), b[3].as('px')];
    } catch (e) {}
  }
  o.props = p;
  return o;
};
// A smart object's source and transform as Photoshop reads them: the 8 numbers of its corners (top left, top right,
// bottom right, bottom left), the size of its contents, and A31's transform: a 3 x 3 matrix by rows from the source's
// pixels to the document's, x' = m0 u + m1 v + m2, y' = m3 u + m4 v + m5 (affine: from three corners).
E.smarts = {};  // E.makeSmart's, by id; replaced per case
E.smartInfo = function (d) {
  var s = PB.s2t, more = E.obj(d, s('smartObjectMore')), so = E.obj(d, s('smartObject')), out = {};
  if (so) {
    if (so.hasKey(s('fileReference'))) out.fileName = so.getString(s('fileReference'));
    out.linked = E.flag(so, s('linked'), false);
  }
  if (!more) return out;
  var t = more.hasKey(s('transform')) ? more.getList(s('transform')) : null, size = E.obj(more, s('size'));
  if (t && t.count == 8) {
    out.corners = [];
    for (var i = 0; i < 8; i++) out.corners.push(t.getDouble(i));
  }
  if (size) out.size = [E.num(size, PB.c2t('Wdth'), 0), E.num(size, PB.c2t('Hght'), 0)];
  if (out.corners && out.size && out.size[0] && out.size[1]) out.transform = E.affine(out.corners, out.size[0], out.size[1]);
  return out;
};
E.affine = function (q, w, h) {
  return [(q[2] - q[0]) / w, (q[6] - q[0]) / h, q[0], (q[3] - q[1]) / w, (q[7] - q[1]) / h, q[1], 0, 0, 1];
};
E.readAlphas = function (doc) {  // the alpha and spot channels: {name, kind, color, opacity}
  var out = [];
  for (var i = 0; i < doc.channels.length; i++) {
    var ch = doc.channels[i];
    if (ch.kind == ChannelType.COMPONENT) continue;
    var a = {name: ch.name, kind: ch.kind == ChannelType.SPOTCOLOR ? 'spot' : ch.kind == ChannelType.SELECTEDAREA ? 'selected' : 'masked'};
    try {
      a.color = '#' + ch.color.rgb.hexValue.toLowerCase();
      a.opacity = ch.opacity / 100;
    } catch (e) {}
    out.push(a);
  }
  return out;
};
E.readGuides = function (doc) {
  var out = [];
  try {
    for (var i = 0; i < doc.guides.length; i++)
      out.push([doc.guides[i].direction == Direction.VERTICAL ? 'v' : 'h', doc.guides[i].coordinate.as('px')]);
  } catch (e) {}
  return out;
};
// What of a case's layer Photoshop has otherwise: lines.
E.verify = function (l, got) {
  var p = l.props, g = got.props, bad = [], k, i;
  var same = function (key, want, have) {
    if (PB.json(want) != PB.json(have)) bad.push(key + ' ' + PB.json(have) + ', not ' + PB.json(want));
  };
  var bytes = function (key, want, have) {
    if (Math.abs(Math.round(want * 255) - Math.round(have * 255)) > 1) bad.push(key + ' ' + have + ', not ' + want);
  };
  same('kind', l.kind, got.kind);
  same('name', p.name, g.name);
  if (!p.background) {
    same('blend', p.blend || (l.kind == 'group' ? 'passThrough' : 'normal'), g.blend);
    bytes('opacity', E.or(p.opacity, 1), g.opacity);
    bytes('fill', E.or(p.fill, 1), g.fill);
    var locks = p.lockAll ? ['lockAll'] : ['lockTransparency', 'lockPixels', 'lockPosition', 'lockAll'];
    for (i = 0; i < locks.length; i++) same(locks[i], !!p[locks[i]], g[locks[i]]);
  }
  same('visible', p.visible !== false, g.visible);
  same('clip', !!p.clip, g.clip);
  same('background', !!p.background, g.background);
  same('color', p.color || 'none', g.color);
  same('mask', E.hasMask(l), got.mask);
  if (E.hasMask(l) && got.mask) {
    bytes('maskDensity', E.or(p.maskDensity, 1), g.maskDensity);
    if (Math.abs(E.or(p.maskFeather, 0) - g.maskFeather) > 0.05) bad.push('maskFeather ' + g.maskFeather);
    same('maskOff', !!p.maskOff, g.maskOff);
    same('maskLinked', p.maskLinked !== false, g.maskLinked);
  }
  same('blendIf', p.blendIf, g.blendIf);
  same('channels', p.channels, g.channels);
  if (l.kind == 'adj') same('adjustment', p.adj, g.adj);
  if (l.kind == 'shape' && !got.vector) bad.push('no vector mask: not a shape layer');
  if (l.vectorMask && !got.vector) bad.push('no vector mask made');
  if (p.fx && !got.fx) bad.push('no layer style');
  if (p.fx && p.fx.on === false && got.fx && got.fx[got.fx.length - 1] != 'hidden') bad.push('effects not hidden');
  if (l.kind == 'text') {
    if (!got.text) bad.push('no text read back');
    else for (k in {text: 1, font: 1}) same(k, p.source[k], got.text[k]);
  }
  return bad;
};
// The case's layer read back and checked; a smart object's source and transform go into its JSON as Photoshop has
// them (they are of where it placed it).
E.check = function (doc, b) {
  var l = b.json, got = E.readLayer(doc, b.layer), bad = E.verify(l, got);
  if (l.kind == 'so') {
    var sm = got.smart;
    if (!sm || !sm.transform) bad.push('no smart object transform read back');
    else {
      l.props.transform = sm.transform;
      if (sm.size) l.smart.canvas = sm.size;
      if (sm.fileName) l.smart.fileName = sm.fileName;
      if (!!sm.linked != !!l.smart.linked) bad.push('linked ' + sm.linked);
    }
  }
  if (l.advanced) {
    var d = got.desc, s = PB.s2t;
    for (var i = 0; i < E.ADVANCED.length; i++) {
      var want = l.advanced[E.ADVANCED[i][0]];
      if (want !== undefined && E.flag(d, s(E.ADVANCED[i][1]), null) !== want) bad.push(E.ADVANCED[i][1] + ' not ' + want);
    }
  }
  PB.note('  ' + l.props.name + ': ' + E.describe(got));
  if (bad.length) throw new Error(l.props.name + ': ' + bad.join('; '));
};
// For photoshop.txt: what Photoshop has of a layer.
E.DUMP = ['layerKind', 'mode', 'opacity', 'fillOpacity', 'group', 'visible', 'background', 'color', 'layerLocking',
          'hasUserMask', 'userMaskDensity', 'userMaskFeather', 'userMaskEnabled', 'userMaskLinked', 'hasVectorMask',
          'blendRange', 'channelRestrictions', 'knockout', 'blendInterior', 'blendClipped', 'transparencyShapesLayer',
          'layerMaskAsGlobalMask', 'vectorMaskAsGlobalMask', 'adjustment', 'layerEffects', 'layerFXVisible',
          'smartObjectMore'];
E.describe = function (got) {
  var d = got.desc, s = [];
  for (var i = 0; i < E.DUMP.length; i++) {
    var k = PB.s2t(E.DUMP[i]);
    if (d.hasKey(k)) s.push(E.DUMP[i] + '=' + PB.dumpValue(d, k, d.getType(k)));
  }
  if (got.text) s.push('text=' + PB.json(got.text));
  if (got.smart) s.push('smart=' + PB.json(got.smart));
  if (got.bounds) s.push('bounds=' + got.bounds.join(' '));
  return s.join(', ');
};

// Descriptors read back. A value of a descriptor (k a key) or list (k an index) as plain data:
E.value = function (from, k, t) {
  if (t == DescValueType.BOOLEANTYPE) return from.getBoolean(k);
  if (t == DescValueType.INTEGERTYPE) return from.getInteger(k);
  if (t == DescValueType.DOUBLETYPE) return from.getDouble(k);
  if (t == DescValueType.UNITDOUBLE) return {unit: from.getUnitDoubleType(k), v: from.getUnitDoubleValue(k)};
  if (t == DescValueType.STRINGTYPE) return from.getString(k);
  if (t == DescValueType.ENUMERATEDTYPE) return {enumerated: from.getEnumerationValue(k)};
  if (t == DescValueType.OBJECTTYPE) return {object: from.getObjectValue(k)};
  if (t == DescValueType.LISTTYPE) return {list: from.getList(k)};
  if (t == DescValueType.REFERENCETYPE) return {reference: from.getReference(k)};
  if (t == DescValueType.RAWTYPE) return {raw: from.getData(k)};
  return {other: String(t)};
};
E.numberOf = function (v) { return typeof v == 'number' ? v : v && v.unit !== undefined ? v.v : null; };
E.show = function (v) {
  if (v === undefined || v === null || typeof v != 'object') return String(v);
  if (v.unit !== undefined) return v.v + PB.keyName(v.unit);
  if (v.enumerated !== undefined) return PB.keyName(v.enumerated);
  if (v.object) return '{' + PB.dump(v.object) + '}';
  if (v.list) return 'a list of ' + v.list.count;
  return v.other || 'a ' + (v.reference ? 'reference' : 'value');
};
E.tolerance = function (w, key, v) {
  if (w.unit !== undefined) return w.unit == PB.c2t('#Pxl') ? 0.05 : 0.5;  // % and degrees as the dialogs show them
  if (key !== null && (key == PB.c2t('Rd  ') || key == PB.c2t('Grn ') || key == PB.c2t('Bl  '))) return 0.6;  // a colour
  return 1e-3 + 1e-4 * Math.abs(v);
};
// Each value of want (what this script set) that got (Photoshop's, read back) has otherwise: lines into out. Keys
// got lacks go to absent (noted, not wrong: Photoshop leaves out some of those it was given), and so do strings
// otherwise (names only: a gradient's, the LUT's file, which Photoshop may keep as it likes). A list of objects
// matches in any order (Levels' channels, a gradient's stops); one of values in order.
E.diff = function (want, got, at, out, absent) {
  for (var i = 0; i < want.count; i++) {
    var k = want.getKey(i), name = at + PB.keyName(k);
    if (!got.hasKey(k)) {
      absent.push(name);
      continue;
    }
    E.compare(E.value(want, k, want.getType(k)), E.value(got, k, got.getType(k)), name, k, out, absent);
  }
};
E.compare = function (w, g, name, key, out, absent) {
  var wn = E.numberOf(w), gn = E.numberOf(g);
  if (wn !== null) {
    if (gn === null) out.push(name + ' ' + E.show(g) + ', not ' + E.show(w));
    else if (w.unit !== undefined && g.unit !== undefined && w.unit != g.unit) out.push(name + ' ' + E.show(g) + ', not ' + E.show(w));
    else if (Math.abs(wn - gn) > E.tolerance(w, key, wn)) out.push(name + ' ' + gn + ', not ' + wn);
  } else if (typeof w != 'object') {
    if (w !== g && typeof w == 'string' && typeof g == 'string') absent.push(name + ' read back as "' + g + '"');
    else if (w !== g) out.push(name + ' ' + E.show(g) + ', not ' + E.show(w));
  } else if (w.enumerated !== undefined) {
    if (!g || g.enumerated != w.enumerated) out.push(name + ' ' + E.show(g) + ', not ' + E.show(w));
  } else if (w.object) {
    if (!g || !g.object) out.push(name + ' ' + E.show(g) + ', not an object');
    else E.diff(w.object, g.object, name + '.', out, absent);
  } else if (w.list) {
    if (!g || !g.list) out.push(name + ' ' + E.show(g) + ', not a list');
    else E.diffList(w.list, g.list, name, out, absent);
  } else if (w.reference) {
    try {
      if (!g || !g.reference) out.push(name + ' ' + E.show(g) + ', not a reference');
      else if (w.reference.getEnumeratedValue() != g.reference.getEnumeratedValue())
        out.push(name + ' ' + PB.keyName(g.reference.getEnumeratedValue()) + ', not ' + PB.keyName(w.reference.getEnumeratedValue()));
    } catch (e) {}
  } else if (w.raw !== undefined) {
    if (!g || g.raw === undefined || g.raw.length != w.raw.length || g.raw != w.raw)
      absent.push(name + ' (' + w.raw.length + ' bytes given, ' + (g && g.raw !== undefined ? g.raw.length + ' read back' : 'none') + ')');
  }
};
E.diffList = function (w, g, name, out, absent) {
  var objects = w.count > 0, i, j;
  for (i = 0; i < w.count; i++)
    if (w.getType(i) != DescValueType.OBJECTTYPE) objects = false;
  if (!objects) {
    if (w.count != g.count) {
      out.push(name + ' has ' + g.count + ' values, not ' + w.count);
      return;
    }
    for (i = 0; i < w.count; i++) E.compare(E.value(w, i, w.getType(i)), E.value(g, i, g.getType(i)), name + '[' + i + ']', null, out, absent);
    return;
  }
  var used = [];
  for (i = 0; i < w.count; i++) {
    var found = -1, first = null;
    for (j = 0; j < g.count && found < 0; j++) {
      if (used[j] || g.getType(j) != DescValueType.OBJECTTYPE) continue;
      var lines = [];
      E.diff(w.getObjectValue(i), g.getObjectValue(j), name + '[' + i + '].', lines, []);
      if (!lines.length) found = j;
      else if (!first) first = lines;
    }
    if (found >= 0) {
      used[found] = true;
      E.diff(w.getObjectValue(i), g.getObjectValue(found), name + '[' + i + '].', [], absent);
    } else {
      out.push(name + '[' + i + '] matches none of the ' + g.count + ' read back' + (first ? ' (' + first.join('; ') + ')' : ''));
    }
  }
};
// An adjustment's, fill's or shape fill's descriptor ([class, descriptor] as made) against the layer's.
E.checkDescriptor = function (doc, layer, made, name) {
  var d = E.layerDesc(doc, layer), key = PB.s2t('adjustment'), out = [];
  if (!d.hasKey(key) || d.getType(key) != DescValueType.LISTTYPE || !d.getList(key).count)
    throw new Error(name + ': no adjustment read back');
  E.diff(made[1], d.getList(key).getObjectValue(0), '', out, E.absent);
  if (out.length) throw new Error(name + ': Photoshop has ' + out.join('; '));
};
// The style set (Lefx) against the layer's: each effect, or the first of its list when Photoshop keeps several.
E.checkStyle = function (doc, layer, want, name) {
  var d = E.layerDesc(doc, layer), s = PB.s2t, got = E.obj(d, s('layerEffects')), out = [];
  if (!got) throw new Error(name + ': no layer style read back');
  for (var i = 0; i < want.count; i++) {
    var k = want.getKey(i), t = want.getType(k);
    if (t == DescValueType.OBJECTTYPE) {
      var g = E.obj(got, k);
      if (!g)
        for (var j = 0; j < got.count && !g; j++) {
          var gk = got.getKey(j);
          if (got.getType(gk) == DescValueType.LISTTYPE && /Multi$/.test(PB.keyName(gk)) && got.getList(gk).count &&
              got.getList(gk).getObjectType(0) == want.getObjectType(k))
            g = got.getList(gk).getObjectValue(0);
        }
      if (!g) out.push(PB.keyName(k) + ' not there');
      else E.diff(want.getObjectValue(k), g, PB.keyName(k) + '.', out, E.absent);
    } else if (t == DescValueType.LISTTYPE) {
      if (!got.hasKey(k)) out.push(PB.keyName(k) + ' not there');
      else E.diffList(want.getList(k), got.getList(k), PB.keyName(k), out, E.absent);
    }
  }
  if (out.length) throw new Error(name + ': Photoshop has ' + out.join('; '));
};

// ---- Files ---------------------------------------------------------------------------------------------------------

E.folder = function (name) {
  var out = new Folder(E.HERE + '/out'), f = new Folder(out + '/' + name);
  if (!out.exists) out.create();
  if (!f.exists) f.create();
  return f;
};
E.readBytes = function (file) {  // the file as a string of bytes
  var f = new File(file);
  f.encoding = 'BINARY';
  if (!f.open('r')) throw new Error('cannot read ' + f.fsName);
  try {
    return f.read();
  } finally {
    f.close();
  }
};
E.u16 = function (s, i) { return (s.charCodeAt(i) << 8) | s.charCodeAt(i + 1); };
E.u32 = function (s, i) { return ((E.u16(s, i) << 16) | E.u16(s, i + 2)) >>> 0; };
// What a PSD or PSB file says of itself: its header, the IDs of its image resources in order, hasRealMergedData of
// 0x0421 (Maximize Compatibility), the merged image's compression (0 raw, 1 RLE, 2 ZIP, 3 ZIP with prediction).
E.psdInfo = function (b) {
  if (b.substr(0, 4) != '8BPS') throw new Error('not a PSD file');
  var v = E.u16(b, 4), info = {version: v, channels: E.u16(b, 12), height: E.u32(b, 14), width: E.u32(b, 18),
                               depth: E.u16(b, 22), mode: E.u16(b, 24), resources: []};
  var p = 26 + 4 + E.u32(b, 26), end = p + 4 + E.u32(b, p);
  for (p += 4; p + 12 <= end && b.substr(p, 4) == '8BIM';) {
    var id = E.u16(b, p + 4), q = p + 6 + ((b.charCodeAt(p + 6) + 2) & ~1), size = E.u32(b, q);
    info.resources.push(id);
    if (id == 0x0421 && size >= 5) info.merged = b.charCodeAt(q + 8) != 0;
    p = q + 4 + size + (size & 1);
  }
  p = end;
  var layers = v == 2 ? E.u32(b, p) * 4294967296 + E.u32(b, p + 4) : E.u32(b, p);
  p += (v == 2 ? 8 : 4) + layers;
  if (p + 2 <= b.length) info.compression = E.u16(b, p);
  return info;
};
E.pngInfo = function (b) {  // {width, height, bits, color} of a PNG's IHDR
  if (b.substr(1, 3) != 'PNG' || b.substr(12, 4) != 'IHDR') throw new Error('not a PNG file');
  return {width: E.u32(b, 16), height: E.u32(b, 20), bits: b.charCodeAt(24), color: b.charCodeAt(25)};
};
E.crcTable = null;
E.crc = function (bytes) {  // CRC-32 (zlib's) of a string of bytes
  var i, k, c;
  if (!E.crcTable) {
    E.crcTable = [];
    for (i = 0; i < 256; i++) {
      for (c = i, k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      E.crcTable[i] = c >>> 0;
    }
  }
  for (c = 0xFFFFFFFF, i = 0; i < bytes.length; i++) c = E.crcTable[(c ^ bytes.charCodeAt(i)) & 255] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
};
E.hex8 = function (v) {
  var s = v.toString(16);
  while (s.length < 8) s = '0' + s;
  return s;
};
// The document saved as PSD or PSB, with Maximize Compatibility on or off: Action Manager's Save As (the format's
// descriptor has maximizeCompatibility), the preference set to match; for a PSD, the DOM's if that fails.
E.saveAs = function (file, format, compat) {
  var c = PB.c2t, d = new ActionDescriptor(), o = new ActionDescriptor();
  o.putBoolean(PB.s2t('maximizeCompatibility'), compat);
  d.putObject(c('As  '), format, o);
  d.putPath(c('In  '), file);
  d.putBoolean(c('Cpy '), true);
  d.putBoolean(c('LwCs'), true);
  executeAction(c('save'), d, DialogModes.NO);
};
E.saveDocument = function (doc, file, psb, compat) {
  var prefs = app.preferences, had, errors = [], formats = psb ? [PB.s2t('largeDocumentFormat'), PB.c2t('Pht8')] : [PB.c2t('Pht3')];
  try {
    had = prefs.maximizeCompatibility;
    prefs.maximizeCompatibility = compat ? QueryStateType.ALWAYS : QueryStateType.NEVER;
  } catch (e) {
    had = undefined;
    errors.push('preference: ' + e);
  }
  try {
    for (var i = 0; i < formats.length; i++) {
      try {
        E.saveAs(file, formats[i], compat);
        return;
      } catch (e) {
        errors.push(PB.keyName(formats[i]) + ': ' + e);
      }
    }
    if (psb) throw new Error('cannot save ' + file.name + ': ' + errors.join('; '));
    var o = new PhotoshopSaveOptions();
    o.layers = true;
    o.alphaChannels = true;
    o.spotColors = true;
    o.embedColorProfile = true;
    doc.saveAs(file, o, true, Extension.LOWERCASE);
  } finally {
    if (had !== undefined) prefs.maximizeCompatibility = had;
    if (errors.length) PB.note('  saving ' + file.name + ': ' + errors.join('; '));
  }
};
// Photoshop's composite as <name>.png: of the document as it is when RGB or grey at 8 or 16 bits or Indexed; of a
// copy made 16 bits and sRGB (Relative Colorimetric, Black Point Compensation, no dither) when CMYK or Lab, made grey
// when Bitmap; a 32-bit document's as a 32-bit float TIFF, and as PNG through HDR Toning's Exposure and Gamma (0, 1)
// if that goes. A PNG that Photoshop cannot write (31.000 px) becomes a TIFF. Into e: png, pngInfo, tif.
E.composite = function (doc, e, dir) {
  var mode = doc.mode, base = dir + '/' + e.name, png = new File(base + '.png'), copy = null, tif;
  var simple = (mode == DocumentMode.RGB || mode == DocumentMode.GRAYSCALE || mode == DocumentMode.INDEXEDCOLOR) &&
               doc.bitsPerChannel != BitsPerChannelType.THIRTYTWO;
  try {
    if (!simple) {
      copy = doc.duplicate(e.name + ' composite', false);
      if (copy.layers.length > 1 || !copy.layers[0].isBackgroundLayer) copy.flatten();
      if (mode == DocumentMode.BITMAP) {
        copy.changeMode(ChangeMode.GRAYSCALE);
      } else if (doc.bitsPerChannel == BitsPerChannelType.THIRTYTWO) {
        tif = new File(base + '.tif');
        var t = new TiffSaveOptions();
        t.layers = false;
        t.alphaChannels = false;
        t.embedColorProfile = true;
        t.imageCompression = TIFFEncoding.NONE;
        t.byteOrder = ByteOrder.IBM;
        copy.saveAs(tif, t, true, Extension.LOWERCASE);
        e.tif = tif.name;
        E.toneDown(copy);
      } else if (mode != DocumentMode.RGB && mode != DocumentMode.GRAYSCALE) {
        copy.bitsPerChannel = BitsPerChannelType.SIXTEEN;
        copy.convertProfile(PB.SRGB, Intent.RELATIVECOLORIMETRIC, true, false);
      }
    }
    try {
      (copy || doc).saveAs(png, PB.pngOptions(), true, Extension.LOWERCASE);
    } catch (x) {
      if (doc.bitsPerChannel == BitsPerChannelType.THIRTYTWO) throw x;
      PB.note('  no PNG of ' + e.name + ' (' + x + '): a TIFF instead');
      tif = new File(base + '.tif');
      var o = new TiffSaveOptions();
      o.layers = false;
      o.imageCompression = TIFFEncoding.TIFFLZW;
      (copy || doc).saveAs(tif, o, true, Extension.LOWERCASE);
      e.tif = tif.name;
      png = null;
    }
  } catch (x) {
    if (!(doc.bitsPerChannel == BitsPerChannelType.THIRTYTWO && e.tif)) throw x;
    PB.note('  no PNG of the 32-bit ' + e.name + ': ' + x + ' (its TIFF is the composite)');
    png = null;
  } finally {
    if (copy) PB.close(copy);
    app.activeDocument = doc;
  }
  if (png && png.exists) {
    e.png = png.name;
    e.pngInfo = E.pngInfo(E.readBytes(png));
    e.pngInfo.bytes = png.length;
  }
};
// 32 bits to 16 by HDR Toning's Exposure and Gamma, exposure 0, gamma 1 (M47's conversion), not Local Adaptation.
E.toneDown = function (doc) {
  var c = PB.c2t, d = new ActionDescriptor(), o = new ActionDescriptor();
  d.putInteger(c('Dpth'), 16);
  o.putInteger(c('Vrsn'), 6);
  o.putInteger(c('Mthd'), 0);
  o.putDouble(c('Exps'), 0);
  o.putDouble(c('Gmm '), 1);
  d.putObject(c('With'), PB.s2t('hdrOptions'), o);
  executeAction(c('CnvM'), d, DialogModes.NO);
  if (doc.bitsPerChannel != BitsPerChannelType.SIXTEEN) throw new Error('HDR Toning left ' + doc.bitsPerChannel);
};
// A case saved: its PSD or PSB (checked: the format, Maximize Compatibility), then its composite. Into c: file, psd.
E.saveCase = function (doc, c, dir) {
  var file = new File(dir + '/' + c.name + (c.psb ? '.psb' : '.psd'));
  E.saveDocument(doc, file, !!c.psb, c.compat !== false);
  c.file = file.name;
  c.psd = E.psdInfo(E.readBytes(file));
  c.psd.bytes = file.length;
  if (c.psd.version != (c.psb ? 2 : 1)) throw new Error(file.name + ' came out version ' + c.psd.version);
  if (c.psd.merged !== (c.compat !== false))
    throw new Error(file.name + ': hasRealMergedData ' + c.psd.merged + ', Maximize Compatibility ' + (c.compat !== false));
  E.composite(doc, c, dir);
  if (c.pngInfo && c.pngInfo.bits != c.bits) {  // index.json's bits are the PNG's
    PB.note('  ' + c.png + ' came out ' + c.pngInfo.bits + ' bits, not ' + c.bits);
    c.bits = c.pngInfo.bits;
  }
  try {
    c.profile = doc.colorProfileName;
  } catch (e) {}
};
E.remove = function (dir, name) {
  var ext = ['.psd', '.psb', '.png', '.tif'];
  for (var i = 0; i < ext.length; i++) {
    var f = new File(dir + '/' + name + ext[i]);
    if (f.exists) f.remove();
  }
};

// ---- Running -------------------------------------------------------------------------------------------------------

// Photoshop's settings for a run, and what puts them back. Image Interpolation is Photoshop's default, Bicubic
// Automatic, while it runs (styles.jsx's): the smart objects' scale and turn (so-placed, so-layers) resample by it.
E.settings = function () {
  var prefs = app.preferences, units = prefs.rulerUnits, type = prefs.typeUnits, dialogs = app.displayDialogs;
  var fg = app.foregroundColor, bg = app.backgroundColor, interp = null;
  prefs.rulerUnits = Units.PIXELS;  // documents at 72 ppi: points are pixels too
  prefs.typeUnits = TypeUnits.POINTS;
  app.displayDialogs = DialogModes.NO;
  app.foregroundColor = E.solid('#000000');
  app.backgroundColor = E.solid('#ffffff');  // what flattening fills with
  try {
    interp = prefs.interpolation;
    if (ResampleMethod.BICUBICAUTOMATIC !== undefined) prefs.interpolation = ResampleMethod.BICUBICAUTOMATIC;
    PB.note('Image Interpolation: ' + interp + ' before, ' + prefs.interpolation + ' while this runs');
  } catch (e) {
    PB.note('Image Interpolation: cannot read or set it (' + e + ')');
  }
  return function () {
    prefs.rulerUnits = units;
    prefs.typeUnits = type;
    app.displayDialogs = dialogs;
    app.foregroundColor = fg;
    app.backgroundColor = bg;
    try {
      if (interp !== null) prefs.interpolation = interp;
    } catch (e) {}
  };
};
E.SETS = {psd: '../ps/p1/in', 'psd-big': '../../tests/golden/ps/p1/in'};  // index.json's "inputs": from each folder
// A case as index.json has it: PB.normalise's (a pixel layer without src is a new one), a smart object's own layers
// too, bits of its PNG (16 for 16- and 32-bit documents).
E.NOSRC = '(none)';
E.normalise = function (c) {
  var pre = function (layers) {
    var out = [];
    for (var i = 0; i < layers.length; i++) {
      var l = PB.copy(layers[i]);
      if ((l.kind === undefined || l.kind == 'pixel') && l.src === undefined) l.src = E.NOSRC;
      if (l.children) l.children = pre(l.children);
      out.push(l);
    }
    return out;
  };
  var bits = c.depth == 16 || c.depth == 32 || c.mode == 'cmyk' || c.mode == 'lab' ? 16 : 8;
  var post = function (layers, at) {
    for (var i = 0; i < layers.length; i++) {
      var l = layers[i];
      if (l.src === E.NOSRC) delete l.src;
      if (l.children) post(l.children, at);
      if (l.smart && l.smart.layers) {
        var inner = PB.normalise({name: at + ': ' + l.props.name, canvas: c.canvas, layers: pre(l.smart.layers)}, bits);
        post(inner.layers, at);
        l.smart.layers = inner.layers;
      }
    }
  };
  var copy = PB.copy(c);
  copy.layers = pre(c.layers);
  var n = PB.normalise(copy, bits);
  post(n.layers, n.name);
  if (c.depth !== undefined && c.depth != 8 && c.depth != 16 && c.depth != 32) throw new Error(c.name + ': depth ' + c.depth);
  if (c.mode !== undefined && !PB.has(['rgb', 'gray', 'cmyk', 'lab'], c.mode)) throw new Error(c.name + ': mode ' + c.mode);
  return n;
};
E.main = function () {
  E.HERE = HERE;  // tools/ps-golden/ (ps-golden\ on Windows), with p1\ in it
  E.selfTest();
  var ours = new Folder(E.HERE + '/ours');
  var files = ours.exists ? ours.getFiles(function (f) { return f instanceof File && /\.ps[db]$/i.test(f.name); }) : [];
  files.sort(function (a, b) { return a.name < b.name ? -1 : a.name > b.name ? 1 : 0; });
  if (files.length && confirm('psd.jsx: ' + files.length + ' PSD files of photo-beauty in ' + ours.fsName +
                              '.\n\nYes: open them (pass two, E34).\nNo: make the PSD suite (pass one).'))
    E.passTwo(files);
  else
    E.passOne(cases());
};
// Pass one: every case into out/psd/ or out/psd-big/. A case that fails is noted and skipped; a failed check of the
// settings stops it all.
E.passOne = function (all) {
  var started = new Date(), dirs = {psd: E.folder('psd'), 'psd-big': E.folder('psd-big')}, sets = {}, k, i;
  for (k in dirs)
    if (dirs.hasOwnProperty(k)) sets[k] = {entries: [], failed: []};
  PB.LOG = [];
  PB.inDir = new Folder(E.HERE + '/p1/in');
  if (!PB.inDir.exists) {
    PB.inDir = Folder.selectDialog('The folder with the inputs of psd.jsx: ps-golden\\p1\\in (README-psd.md)');
    if (!PB.inDir) return;
  }
  var restoreApp = E.settings(), restore = function () {}, fatal = null;
  PB.note('Photoshop ' + app.version + ', ' + $.os + ', ' + started + '; psd.jsx pass one, ' + all.length + ' cases');
  try {
    PB.note('Maximize Compatibility preference: ' + app.preferences.maximizeCompatibility);
  } catch (e) {}
  try {
    restore = PB.colorSettings();
    PB.checkBlending();
    PB.checkDither();
    for (var m in PB.BLENDS)
      if (PB.BLENDS.hasOwnProperty(m)) PB.blendMode(m);
    var lut = dirs.psd + '/' + E.LUT_NAME;
    PB.write(lut, E.cube());
    E.lutFile = new File(lut);
  } catch (e) {
    fatal = e;
  }
  for (i = 0; i < all.length && !fatal; i++) {
    var c = null, doc = null, set = all[i].big ? 'psd-big' : 'psd';
    try {
      c = E.normalise(all[i]);
      PB.note(c.name);
      E.absent = [];
      doc = E.build(c);
      E.saveCase(doc, c, dirs[set]);
      if (E.absent.length) PB.note('  not read back as set (noted only): ' + E.absent.join(', '));
      PB.note('  ' + c.file + ': ' + PB.json(c.psd) + (c.png ? '; ' + c.png + ': ' + PB.json(c.pngInfo) : '') +
              (c.tif ? '; ' + c.tif : ''));
      sets[set].entries.push(c);
    } catch (e) {
      var name = c ? c.name : 'case ' + i;
      sets[set].failed.push({name: name, error: String(e) + (e.line ? ' (line ' + e.line + ')' : '')});
      PB.note('FAILED ' + name + ': ' + e + (e.line ? ' (line ' + e.line + ')' : ''));
      if (c) E.remove(dirs[set], c.name);
    }
    if (doc) PB.close(doc);
  }
  if (fatal) PB.note('STOPPED: ' + fatal + (fatal.line ? ' (line ' + fatal.line + ')' : ''));
  for (k in PB.inputs)
    if (PB.inputs.hasOwnProperty(k)) PB.close(PB.inputs[k]);
  PB.inputs = {};
  restore();
  restoreApp();
  var done = 0, failed = 0;
  for (k in sets)
    if (sets.hasOwnProperty(k)) done += sets[k].entries.length, failed += sets[k].failed.length;
  PB.note(done + ' files, ' + failed + ' failed, ' + Math.round((new Date() - started) / 1000) + ' s');
  for (k in sets)
    if (sets.hasOwnProperty(k)) {
      var info = {script: 'psd.jsx', pass: 1, photoshop: app.version, os: $.os, date: String(started), complete: !fatal,
                  inputs: E.SETS[k]};
      if (fatal) info.error = String(fatal);
      PB.write(dirs[k] + '/index.json', PB.indexText(info, sets[k].entries, sets[k].failed));
      PB.write(dirs[k] + '/photoshop.txt', PB.LOG.join('\n') + '\n');
    }
  alert(fatal ? 'Stopped: ' + fatal + '\n\nDetails: ' + dirs.psd.fsName + '\\photoshop.txt'
              : 'Done: ' + done + ' PSD files' + (failed ? ', ' + failed + ' failed (photoshop.txt)' : '') + '\n' +
                dirs.psd.fsName + '\n' + dirs['psd-big'].fsName);
};
// Pass two: the files photo-beauty saved (E34), each opened and read as Photoshop has it: canvas, mode, depth, the
// layers (photo-beauty's JSON: kind, props off their defaults, mask, children...), alpha channels, guides, and its
// composite, into out/psd-read/ with index.json. crc32 and bytes say which file it was, so that E34 knows when its
// own has changed since.
E.passTwo = function (files) {
  var started = new Date(), dir = E.folder('psd-read'), entries = [], failed = [];
  PB.LOG = [];
  PB.note('Photoshop ' + app.version + ', ' + $.os + ', ' + started + '; psd.jsx pass two, ' + files.length + ' files');
  var restoreApp = E.settings(), restore = function () {};
  try {
    restore = PB.colorSettings();
  } catch (e) {
    PB.note('colour settings: ' + e);
  }
  for (var i = 0; i < files.length; i++) {
    var f = files[i], name = File.decode(f.name), doc = null;  // File's name is URI-encoded
    var e = {name: name, file: name, bytes: f.length};
    PB.note(name);
    try {
      e.crc32 = E.hex8(E.crc(E.readBytes(f)));
      doc = app.open(f);
      e.canvas = [doc.width.as('px'), doc.height.as('px')];
      e.mode = E.modeOf(doc.mode);
      e.depth = {EIGHT: 8, SIXTEEN: 16, THIRTYTWO: 32, ONE: 1}[String(doc.bitsPerChannel).replace('BitsPerChannelType.', '')] ||
                String(doc.bitsPerChannel);
      try {
        e.profile = doc.colorProfileName;
      } catch (x) {}
      e.ppi = doc.resolution;
      e.layers = E.readTree(doc, doc.layers);
      e.count = E.count(doc.layers);
      e.alphas = E.readAlphas(doc);
      e.guides = E.readGuides(doc);
      E.composite(doc, e, dir);
      entries.push(e);
    } catch (x) {
      failed.push({name: name, error: String(x) + (x.line ? ' (line ' + x.line + ')' : '')});
      PB.note('FAILED ' + name + ': ' + x + (x.line ? ' (line ' + x.line + ')' : ''));
    }
    if (doc) PB.close(doc);
  }
  restore();
  restoreApp();
  PB.note(entries.length + ' files read, ' + failed.length + ' failed, ' + Math.round((new Date() - started) / 1000) + ' s');
  PB.write(dir + '/index.json', PB.indexText({script: 'psd.jsx', pass: 2, photoshop: app.version, os: $.os,
                                              date: String(started), complete: true}, entries, failed));
  PB.write(dir + '/photoshop.txt', PB.LOG.join('\n') + '\n');
  alert('Done: ' + entries.length + ' files of photo-beauty read' + (failed.length ? ', ' + failed.length + ' failed' : '') +
        '\n' + dir.fsName);
};
E.MODE_NAMES = [['RGB', 'rgb'], ['GRAYSCALE', 'gray'], ['CMYK', 'cmyk'], ['LAB', 'lab'], ['INDEXEDCOLOR', 'indexed'],
                ['BITMAP', 'bitmap'], ['DUOTONE', 'duotone'], ['MULTICHANNEL', 'multichannel']];
E.modeOf = function (mode) {
  for (var i = 0; i < E.MODE_NAMES.length; i++)
    if (DocumentMode[E.MODE_NAMES[i][0]] == mode) return E.MODE_NAMES[i][1];
  return String(mode);
};
// The layers of a container as Photoshop reads them, bottom first: {kind, props off their defaults (A30's to_json()
// order), mask, vectorMask, fx (the effects there, 'hidden' when all are), smart, text, children}.
E.readTree = function (doc, layers) {
  var out = [];
  for (var i = layers.length - 1; i >= 0; i--) {
    var got = E.readLayer(doc, layers[i]), o = {kind: got.kind, props: E.compact(got.props, got.kind == 'group')};
    if (got.mask) o.mask = true;
    if (got.vector) o.vectorMask = true;
    if (got.fx) o.fx = got.fx;
    if (got.smart) o.smart = got.smart;
    if (got.text) o.text = got.text;
    if (got.kind == 'group') o.children = E.readTree(doc, layers[i].layers);
    PB.note('  ' + got.props.name + ': ' + E.describe(got));
    out.push(o);
  }
  return out;
};
E.count = function (layers) {
  var n = 0;
  for (var i = 0; i < layers.length; i++) n += 1 + (layers[i].typename == 'LayerSet' ? E.count(layers[i].layers) : 0);
  return n;
};
E.COMPACT = [['name', ''], ['blend', 'normal'], ['opacity', 1], ['fill', 1], ['maskDensity', 1], ['maskFeather', 0],
             ['visible', true], ['lockTransparency', false], ['lockPixels', false], ['lockPosition', false],
             ['lockAll', false], ['clip', false], ['maskOff', false], ['maskLinked', true], ['blendIf', undefined],
             ['channels', undefined], ['adj', undefined], ['background', false], ['color', 'none']];
E.compact = function (p, group) {  // props off their defaults, as to_json() writes them (a group's blend always)
  var out = {version: 1};
  for (var i = 0; i < E.COMPACT.length; i++) {
    var k = E.COMPACT[i][0];
    if (p[k] !== undefined && (p[k] !== E.COMPACT[i][1] || (k == 'blend' && group))) out[k] = p[k];
  }
  return out;
};

// ---- Without Photoshop (jscheck) -----------------------------------------------------------------------------------

// What pass one would write as index.json of a folder ('psd', 'psd-big'), the cases as they are before Photoshop:
// jscheck --eval "E.offline('psd')" (tests/golden/psd/cases.json is it).
E.offline = function (set) {
  var all = cases(), entries = [], seen = {};
  for (var i = 0; i < all.length; i++) {
    if (seen[all[i].name]) throw new Error('two cases named ' + all[i].name);
    seen[all[i].name] = true;
    if (!!all[i].big != (set == 'psd-big')) continue;
    var e = E.normalise(all[i]);
    e.file = e.name + (e.psb ? '.psb' : '.psd');
    entries.push(e);
  }
  return PB.indexText({script: 'psd.jsx', pass: 1, complete: true, inputs: E.SETS[set]}, entries, []);
};
E.bytes = function (n, v) {  // v as n bytes, big-endian
  var s = '';
  for (var i = n - 1; i >= 0; i--) s += String.fromCharCode(Math.floor(v / Math.pow(256, i)) % 256);
  return s;
};
// The parts that need no Photoshop, checked (pass one and two run it first; jscheck --eval "E.selfTest()").
E.selfTest = function () {
  if (E.hex8(E.crc('123456789')) != 'cbf43926') throw new Error('self test: CRC-32');
  var res = '8BIM' + E.bytes(2, 0x03ED) + E.bytes(2, 0) + E.bytes(4, 16) + E.bytes(16, 0) +
            '8BIM' + E.bytes(2, 0x0421) + E.bytes(1, 3) + 'abc' + E.bytes(4, 5) + E.bytes(4, 1) + E.bytes(1, 0) + E.bytes(1, 0);
  var psd = '8BPS' + E.bytes(2, 1) + E.bytes(6, 0) + E.bytes(2, 4) + E.bytes(4, 20) + E.bytes(4, 30) + E.bytes(2, 8) +
            E.bytes(2, 3) + E.bytes(4, 0) + E.bytes(4, res.length) + res + E.bytes(4, 2) + E.bytes(2, 0) + E.bytes(2, 1);
  var info = E.psdInfo(psd);
  if (PB.json(info) != '{"version": 1, "channels": 4, "height": 20, "width": 30, "depth": 8, "mode": 3, "resources": [1005, 1057], "merged": false, "compression": 1}')
    throw new Error('self test: psdInfo ' + PB.json(info));
  var png = String.fromCharCode(137) + 'PNG' + E.bytes(4, 0) + E.bytes(4, 13) + 'IHDR' + E.bytes(4, 300) + E.bytes(4, 2) +
            E.bytes(1, 16) + E.bytes(1, 6);
  if (PB.json(E.pngInfo(png)) != '{"width": 300, "height": 2, "bits": 16, "color": 6}') throw new Error('self test: pngInfo');
  var m = E.affine([10, 20, 110, 20, 110, 70, 10, 70], 200, 100);  // halved, at (10, 20)
  if (PB.json(m) != '[0.5, 0, 10, 0, 0.5, 20, 0, 0, 1]') throw new Error('self test: affine ' + PB.json(m));
  return 'ok';
};

// ---- The cases -----------------------------------------------------------------------------------------------------

// Content cheap in PackBits (Photoshop's RLE of 8-bit layers and composites): ramp.png turned 90 (bands down, rows
// alike), turned 270 (the other way), shapes.png (flat shapes on transparent), painted rectangles. Masks of a
// rectangle or of shapes.png's alpha; a gradient only where it runs down (rows alike) or in a strip.
E.px = function (src, props, more) { return E.merge({kind: 'pixel', src: src, props: props}, more); };
E.bands = function (props, more) { return E.px('ramp.png', props || {name: 'bands'}, E.merge({rotate: 90}, more)); };
E.ramp = function (props, cell, more) { return E.px('ramp.png', props, E.merge({rotate: 270, cell: cell}, more)); };
E.shapes = function (props, more) { return E.px('shapes.png', props, more); };
E.paint = function (props, rects, more) { return E.merge({kind: 'pixel', props: props, paint: rects}, more); };
E.ground = function (hex) { return E.paint({name: 'ground'}, [[0, 0, 256, 256, hex || '#e8e4dc']]); };
E.group = function (props, children, more) { return E.merge({kind: 'group', props: props, children: children}, more); };
E.adj = function (name, vals, props, more) {
  props.adj = name;
  if (vals) props.vals = vals;
  return E.merge({kind: 'adj', props: props}, more);
};
E.fill = function (content, props, more) {
  props.content = content;
  return E.merge({kind: 'fill', props: props}, more);
};
E.text = function (source, props) {
  props.source = source;
  return {kind: 'text', props: props};
};
E.shape = function (source, props) {
  props.source = source;
  return {kind: 'shape', props: props};
};
E.so = function (props, smart, place, more) { return E.merge({kind: 'so', props: props, smart: smart, place: place}, more); };
// Four cells of 128 x 128 with a shape of shapes.png's each (disc; rounded rectangle and line; ring; soft dot and
// slanted line), as layers of their own: layer styles' cases.
E.QUARTERS = [[0, 0, 128, 128], [128, 0, 128, 128], [0, 128, 128, 128], [128, 128, 128, 128]];
E.styled = function (names, fxs, props) {
  var out = [E.ground()];
  for (var i = 0; i < names.length; i++)
    out.push(E.shapes(E.merge({name: names[i], fx: fxs[i]}, props && props[i]), {cell: E.QUARTERS[i]}));
  return out;
};
// D30's gradient: three colour stops, the middle one with its midpoint at 40%, opacity 100% to 80%.
E.GRADIENT = {colors: [{p: 0, c: [27, 16, 32]}, {p: 0.5, c: [158, 47, 69], m: 0.4}, {p: 1, c: [233, 199, 179]}],
              alphas: [{p: 0, a: 1}, {p: 1, a: 0.8}], smoothness: 100};
E.shadow = function (more) {  // LayerStyleDialog's dropShadow, every key
  return E.merge({on: true, blend: 'multiply', color: '#000000', opacity: 75, angle: 120, distance: 8, spread: 0, size: 10,
                 noise: 0, knockOut: true, globalLight: false, antiAliased: false}, more);
};
E.stroke = function (more) { return E.merge({on: true, size: 3, position: 'outside', blend: 'normal', opacity: 100, color: '#c03030'}, more); };
E.overlay = function (more) { return E.merge({on: true, blend: 'normal', color: '#ff0000', opacity: 100}, more); };
E.gradOverlay = function (more) {
  return E.merge({on: true, blend: 'normal', opacity: 100, gradient: E.GRADIENT, method: 'perceptual', style: 'linear',
                 angle: 90, scale: 100, reverse: false, dither: false, align: true, offset: [0, 0]}, more);
};

function cases() {
  var c = [], i, layers, S = [256, 256], cells;
  var left = [0, 0, 128, 256], right = [128, 0, 128, 256], strip = [0, 32, 256, 192];

  // ---- Blending and the layer tree (M40, M42; E34) ----
  cells = E.grid(4, 8, 64, 32);
  layers = [E.bands()];
  for (i = 0; i < E.MODES.length; i++) layers.push(E.ramp({name: E.MODES[i], blend: E.MODES[i]}, cells[i]));
  layers.push(E.ramp({name: 'dissolve 50%', blend: 'dissolve', opacity: 0.5}, cells[27]));
  c.push({name: 'modes', canvas: S, layers: layers,
          note: 'the 27 blend modes of a layer, each a 64 x 32 cell of ramp.png turned 270 over ramp.png turned 90; Dissolve at 50% too'});
  cells = E.grid(4, 4, 64, 64);
  var opacities = [{opacity: 0.75}, {opacity: 0.5}, {opacity: 0.25}, {opacity: 0}, {fill: 0.75}, {fill: 0.5}, {fill: 0.25},
                   {fill: 0}, {opacity: 0.5, fill: 0.5}, {blend: 'multiply', fill: 0.5}, {blend: 'linearDodge', fill: 0.5},
                   {blend: 'colorDodge', fill: 0.5}, {blend: 'vividLight', fill: 0.5}, {blend: 'hardMix', fill: 0.5},
                   {blend: 'difference', fill: 0.5}, {blend: 'linearBurn', opacity: 0.5, fill: 0.25}];
  layers = [E.bands()];
  for (i = 0; i < opacities.length; i++) {
    var o = opacities[i], name = (o.blend ? o.blend + ' ' : '') + (o.opacity !== undefined ? 'opacity ' + o.opacity * 100 + '% ' : '') +
                                 (o.fill !== undefined ? 'fill ' + o.fill * 100 + '%' : '');
    layers.push(E.ramp(E.merge({name: name.replace(/ $/, '')}, o), cells[i]));
  }
  c.push({name: 'opacity', canvas: S, layers: layers,
          note: 'Opacity and Fill, apart and together; Fill on Multiply and on 6 of the 8 modes whose Fill is not their opacity'});
  c.push({name: 'groups', canvas: S, layers: [E.bands(),
    E.group({name: 'pass through 1', blend: 'passThrough', opacity: 0.8}, [
      E.shapes({name: 'shapes', blend: 'multiply'}),
      E.group({name: 'isolated', blend: 'normal'}, [
        E.ramp({name: 'ramp a', blend: 'screen'}, [0, 0, 128, 128]),
        E.group({name: 'multiply 75%', blend: 'multiply', opacity: 0.75}, [
          E.group({name: 'pass through 2', blend: 'passThrough'}, [
            E.ramp({name: 'ramp b', blend: 'overlay'}, [64, 64, 128, 128])])])])]),
    E.group({name: 'hidden', visible: false}, [E.shapes({name: 'shapes turned'}, {rotate: 180})]),
    E.group({name: 'empty'}, []),
    E.group({name: 'masked', blend: 'normal'}, [E.ramp({name: 'ramp c', blend: 'difference'}, [128, 128, 128, 128])],
            {mask: 'shapes.png'})],
    note: 'groups 4 deep: Pass Through, isolated Normal, Multiply at 75%, Pass Through; a hidden one, an empty one, one with a mask'});
  cells = E.grid(2, 4, 128, 64);
  c.push({name: 'masks', canvas: S, layers: [E.bands(),
    E.ramp({name: 'mask'}, cells[0], {mask: 'shapes.png'}),
    E.ramp({name: 'density 50%', maskDensity: 0.5}, cells[1], {mask: 'shapes.png'}),
    E.ramp({name: 'feather 10 px', maskFeather: 10}, cells[2], {maskRect: [16, 80, 96, 32]}),
    E.ramp({name: 'mask off', maskOff: true}, cells[3], {mask: 'shapes.png'}),
    E.ramp({name: 'not linked', maskLinked: false}, cells[4], {maskRect: [16, 144, 96, 32]}),
    E.ramp({name: 'reveal all'}, cells[5], {maskAll: 'white'}),
    E.ramp({name: 'hide all'}, cells[6], {maskAll: 'black'}),
    E.shapes({name: 'mask on alpha'}, {rotate: 180, cell: cells[7], mask: 'shapes.png'})],
    note: 'layer masks: of shapes.png\'s alpha, at Density 50%, Feather 10 px on a rectangle, off, not linked, Reveal All, Hide All, on a layer with alpha of its own'});
  c.push({name: 'clip', canvas: S, layers: [E.bands(),
    E.shapes({name: 'base'}),
    E.ramp({name: 'clipped', clip: true}, [0, 0, 256, 256]),
    E.shapes({name: 'clipped multiply 50%', clip: true, blend: 'multiply', opacity: 0.5}, {rotate: 90}),
    E.ramp({name: 'clipped screen, masked', clip: true, blend: 'screen'}, [0, 128, 256, 128], {maskRect: [0, 160, 128, 96]}),
    E.adj('Invert', null, {name: 'clipped invert', clip: true}, {maskRect: right}),
    E.group({name: 'base group', blend: 'normal'}, [E.paint({name: 'square'}, [[160, 160, 80, 80, '#3a7bd5']])]),
    E.ramp({name: 'clipped to the group', clip: true}, [128, 128, 128, 128])],
    note: 'a clipping chain of four on shapes.png (Multiply 50%, Screen with a mask, an Invert adjustment layer); a layer clipped to a group'});
  c.push({name: 'blend-if', canvas: S, layers: [E.bands(),
    E.ramp({name: 'gray, this layer, split', blendIf: {gray: {layer: [32, 96, 160, 224]}}}, E.QUARTERS[0]),
    E.ramp({name: 'gray, underlying, split', blendIf: {gray: {under: [0, 64, 192, 255]}}}, E.QUARTERS[1]),
    E.ramp({name: 'red and blue', blendIf: {red: {layer: [0, 0, 128, 200]}, blue: {under: [50, 100, 255, 255]}}}, E.QUARTERS[2]),
    E.ramp({name: 'green off', channels: {g: false}}, E.QUARTERS[3])],
    note: 'Blend If split (This Layer, Underlying Layer, gray and colour channels); Advanced Blending\'s G off'});
  cells = E.grid(4, 4, 64, 64);
  var swatch = function (n) { return [[cells[n][0] + 8, cells[n][1] + 8, 48, 48, ['#e05050', '#f09030', '#e5d03a', '#4caf50', '#4c6ef5', '#9c5cd6', '#888888', '#202020'][n % 8]]]; };
  c.push({name: 'locks-labels', canvas: S, layers: [E.bands({name: 'Background', background: true}),
    E.paint({name: 'red', color: 'red', lockTransparency: true}, swatch(0)),
    E.paint({name: 'orange', color: 'orange', lockPixels: true}, swatch(1)),
    E.paint({name: 'yellow', color: 'yellow', lockPosition: true}, swatch(2)),
    E.paint({name: 'green', color: 'green', lockAll: true}, swatch(3)),
    E.paint({name: 'blue', color: 'blue', lockTransparency: true, lockPosition: true}, swatch(4)),
    E.paint({name: 'violet', color: 'violet'}, swatch(5)),
    E.paint({name: 'gray', color: 'gray'}, swatch(6)),
    E.paint({name: 'no colour', lockPixels: true, lockPosition: true}, swatch(7)),
    E.group({name: 'red group', color: 'red'}, [E.paint({name: 'in the red group'}, swatch(8))])],
    note: 'a Background; the four locks alone and together; the seven label colours and none, on a group too'});
  c.push({name: 'edges', canvas: S, layers: [E.ground('#d8d0c0'),
    E.paint({name: 'empty'}, []),
    E.shapes({name: 'past the edges'}, {move: [-80, -48]}),
    E.shapes({name: 'off the canvas'}, {move: [300, 0]}),
    E.paint({name: 'one pixel'}, [[200, 200, 1, 1, '#ff0000']]),
    E.paint({name: 'one colour'}, [[16, 160, 96, 64, '#3a7bd5']]),
    E.shapes({name: 'hidden', visible: false}, {rotate: 180})],
    note: 'an empty layer; layers part and wholly off the canvas (their pixels kept); one pixel; one colour; a hidden one'});
  c.push({name: 'names', canvas: S, layers: [E.ground(),
    E.paint({name: 'L\u1edbp \u1ea3nh ti\u1ebfng Vi\u1ec7t: \u1ed1 \u1eef \u1eb7 \u1ef7 \u0111'}, [[8, 8, 32, 32, '#e05050']]),
    E.paint({name: '\u65e5\u672c\u8a9e\u306e\u30ec\u30a4\u30e4\u30fc'}, [[48, 8, 32, 32, '#4caf50']]),
    E.paint({name: '\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac, \u043a\u0438\u0440\u0438\u043b\u043b\u0438\u0446\u0430'}, [[88, 8, 32, 32, '#4c6ef5']]),
    E.paint({name: 'emoji \ud83c\udfa8 \u2615'}, [[128, 8, 32, 32, '#e5d03a']]),
    E.paint({name: 'a name of more than thirty-one characters, for luni'}, [[168, 8, 32, 32, '#9c5cd6']]),
    E.group({name: 'Nh\u00f3m \u2713'}, [E.paint({name: 'trong nh\u00f3m'}, [[8, 48, 32, 32, '#202020']])])],
    note: 'names past Latin-1 (luni): Vietnamese, Japanese, Greek and Cyrillic, an emoji past the BMP, longer than a Pascal name'});
  c.push({name: 'advanced', canvas: S, layers: [E.bands(),
    E.group({name: 'knockout group', blend: 'normal'}, [
      E.shapes({name: 'shapes'}),
      E.ramp({name: 'knocks out', fill: 0}, [32, 32, 192, 192], {advanced: {knockout: 'shallow', blendInterior: true,
              blendClipped: false, transparencyShapes: false, maskHidesEffects: true, vectorMaskHidesEffects: true}})])],
    note: 'P2\'s Advanced Blending off its defaults (Knockout Shallow, the four boxes): kept, with a notice (M42)'});

  // ---- The document (M40, M42) ----
  c.push({name: 'channels-guides', canvas: S, layers: [E.bands(), E.shapes({name: 'shapes'})],
          alphas: [{name: 'Alpha 1', from: 'shapes.png'},
                   {name: 'K\u00eanh ch\u1ecdn', rect: [32, 96, 192, 64], kind: 'selected', color: '#00c000', opacity: 0.7}],
          guides: [['v', 64], ['v', 128.5], ['h', 32], ['h', 200.25]],
          note: 'two alpha channels (of shapes.png\'s alpha; a rectangle, Selected Areas, green at 70%) and four guides, two of them between pixels'});
  c.push({name: 'spot', canvas: S, layers: [E.bands()], spots: [{name: 'Spot 1', rect: [64, 64, 128, 128], color: '#e0663f', solidity: 0}],
          note: 'a spot channel (P3: kept, with a notice); the PNG has no spot ink'});
  c.push({name: 'resources', canvas: S, layers: [E.bands(), E.shapes({name: 'shapes'}), E.paint({name: 'top'}, [[16, 16, 32, 32, '#202020']])],
          ppi: 300, selected: 'shapes',
          meta: {title: 'E33 resources', author: 'photo-beauty E33',
                 caption: 'Ch\u00fa th\u00edch ti\u1ebfng Vi\u1ec7t: \u1ea3nh th\u1eed PSD',
                 copyright: '\u00a9 2026 photo-beauty', keywords: ['psd', 'e33']},
          note: '300 ppi (0x03ED), XMP and IPTC of title, author, caption, copyright and keywords, the layer active when saved'});
  c.push({name: 'no-composite', canvas: S, compat: false, layers: [E.bands(), E.shapes({name: 'shapes', blend: 'multiply'})],
          note: 'saved with Maximize Compatibility off: no real merged image (0x0421); the PNG is Photoshop\'s composite still'});

  // ---- Adjustment layers (M43; D33-D39), each with a mask ----
  var adjCase = function (file, layersOf, note) { c.push({name: 'adj-' + file, canvas: S, layers: [E.bands()].concat(layersOf), note: note}); };
  adjCase('brightness-contrast', [E.adj('Brightness/Contrast', {b: 40, c: 30}, {name: 'Brightness/Contrast'}, {maskRect: left}),
                                  E.adj('Brightness/Contrast', {b: -30, c: 50, legacy: true}, {name: 'legacy'}, {maskRect: right})],
          'the new one on the left, Use Legacy on the right');
  adjCase('levels', [E.adj('Levels', {RGB: {in0: 20, gam: 1.3, in1: 235, out0: 10, out1: 245}, Red: {gam: 0.8, in1: 200}, Blue: {out0: 30}},
                           {name: 'Levels'}, {mask: 'shapes.png'})], 'RGB, Red and Blue; a mask of shapes.png\'s alpha');
  adjCase('curves', [E.adj('Curves', {pts: {RGB: [[0, 0], [64, 48], [192, 208], [255, 255]], Green: [[0, 20], [128, 140], [255, 235]],
                                            Blue: [[0, 0], [3, 12], [252, 244], [255, 255]]}}, {name: 'Curves'}, {maskRect: strip})],
          'an S on RGB, Green\'s ends moved, points next to the ends on Blue');
  adjCase('exposure', [E.adj('Exposure', {e: 0.8, o: -0.02, g: 1.2}, {name: 'Exposure'}, {maskRect: strip})], '');
  adjCase('vibrance', [E.adj('Vibrance', {v: 50, s: -20}, {name: 'Vibrance'}, {maskRect: strip})], '');
  adjCase('hue-saturation', [E.adj('Hue/Saturation', {Master: {h: 30, s: 20, l: -10}, Reds: {s: 40}, Blues: {h: -20, span: [190, 220, 250, 280]}},
                                   {name: 'Hue/Saturation'}, {maskRect: left}),
                             E.adj('Hue/Saturation', {colorize: true, Master: {h: 200, s: 40}}, {name: 'colorize'}, {maskRect: right})],
          'Master, Reds and Blues (a span of its own) on the left, Colorize on the right');
  adjCase('color-balance', [E.adj('Color Balance', {Shadows: {cr: 20, mg: -10, yb: 5}, Midtones: {cr: -15, mg: 25}, Highlights: {yb: -30}, pl: false},
                                  {name: 'Color Balance'}, {maskRect: strip})], 'three tones, Preserve Luminosity off');
  adjCase('black-white', [E.adj('Black & White', {r: 60, y: 20, g: 30, c: 70, b: 10, m: 90, tint: true, tintColor: '#e1d3b3'},
                                {name: 'Black & White'}, {maskRect: strip})], 'six sliders, Tint');
  adjCase('photo-filter', [E.adj('Photo Filter', {pf: 1, pf1: '#ec8a00', d: 60, pl: false}, {name: 'Photo Filter'}, {maskRect: strip})],
          'a colour, Density 60%, Preserve Luminosity off');
  adjCase('channel-mixer', [E.adj('Channel Mixer', {Red: {r: 80, g: 30, b: -10}, Green: {g: 90, b: 10, k: 5}, Blue: {r: -10, g: 20, k: -5}},
                                  {name: 'Channel Mixer'}, {maskRect: left}),
                            E.adj('Channel Mixer', {mono: true, Gray: {r: 40, g: 40, b: 20}}, {name: 'monochrome'}, {maskRect: right})],
          'three outputs with Constant on the left, Monochrome on the right');
  adjCase('color-lookup', [E.adj('Color Lookup', {lf0: E.LUT_NAME, lut: E.cube()}, {name: 'Color Lookup'}, {maskRect: strip})],
          'a 5 x 5 x 5 .cube embedded (' + E.LUT_NAME + ', written next to the files)');
  adjCase('invert', [E.adj('Invert', null, {name: 'Invert'}, {maskRect: strip})], '');
  adjCase('posterize', [E.adj('Posterize', {lv: 5}, {name: 'Posterize'}, {maskRect: strip})], '');
  adjCase('threshold', [E.adj('Threshold', {t: 100}, {name: 'Threshold'}, {maskRect: strip})], '');
  adjCase('gradient-map', [E.adj('Gradient Map', {gradient: E.GRADIENT, method: 'smooth', dither: true}, {name: 'Gradient Map'}, {maskRect: strip})],
          'three stops, a midpoint, opacity stops; Method Smooth, Dither');
  adjCase('selective-color', [E.adj('Selective Color', {Reds: {c: -20, m: 10, y: 30}, Neutrals: {y: -15, k: 10}, Blacks: {k: 20}},
                                    {name: 'Selective Color'}, {maskRect: left}),
                              E.adj('Selective Color', {Blues: {c: 30, k: -10}, rel: 1}, {name: 'absolute'}, {maskRect: right})],
          'Relative on the left, Absolute on the right');
  for (i = c.length - 16; i < c.length; i++)
    if (!c[i].note) c[i].note = c[i].layers[1].props.adj + ' off its defaults, with a mask';

  // ---- Fill layers (M43; D40) ----
  c.push({name: 'fill-solid', canvas: S, layers: [E.bands(),
    E.fill({color: '#3a7bd5'}, {name: 'Color Fill', blend: 'multiply', opacity: 0.8}, {mask: 'shapes.png'}),
    E.fill({color: '#f7c873'}, {name: 'Color Fill 2', opacity: 0.3}, {maskAll: 'white'})],
    note: 'Solid Color fill layers: Multiply at 80% with a mask of shapes.png; one at 30% keeping the white mask Photoshop makes'});
  var fills = [{style: 'linear', method: 'perceptual', angle: 90}, {style: 'radial', method: 'linear', reverse: true, scale: 80},
               {style: 'angle', method: 'classic', angle: 30}, {style: 'reflected', method: 'smooth', angle: 0, dither: true},
               {style: 'diamond', method: 'stripes', offset: [10, -5], align: false}];
  layers = [E.ground()];
  for (i = 0; i < fills.length; i++)
    layers.push(E.fill(E.merge({gradient: E.GRADIENT, method: 'perceptual', style: 'linear', angle: 90, scale: 100, reverse: false,
                               dither: false, align: true, offset: [0, 0]}, fills[i]),
                       {name: fills[i].style + ', ' + fills[i].method}, {maskRect: [0, 16 + 48 * i, 256, 32]}));
  c.push({name: 'fill-gradient', canvas: S, layers: layers,
          note: 'Gradient fill layers in strips: the five styles, the five methods, Reverse, Scale, Dither, Align off, an offset'});

  // ---- Layer styles (M44; L30-L34), on the four shapes of shapes.png ----
  c.push({name: 'fx-drop-shadow', canvas: S, globalLight: [120, 30], layers: E.styled(['global light', 'spread 30%', 'no knockout', 'off'], [
    {dropShadow: E.shadow({globalLight: true})},
    {dropShadow: E.shadow({color: '#2b1d4e', opacity: 50, angle: 45, distance: 12, spread: 30, size: 6})},
    {dropShadow: E.shadow({blend: 'normal', color: '#c03030', opacity: 100, angle: 90, distance: 0, spread: 50, size: 20, noise: 10, knockOut: false})},
    {dropShadow: E.shadow({on: false})}]),
    note: 'Drop Shadow: Global Light (120, 30), Spread, Noise, no knockout, turned off'});
  c.push({name: 'fx-stroke', canvas: S, layers: E.styled(['outside 3', 'inside 10', 'center 6', 'outside 50'], [
    {stroke: E.stroke()}, {stroke: E.stroke({size: 10, position: 'inside', color: '#3a7bd5'})},
    {stroke: E.stroke({size: 6, position: 'center', blend: 'multiply', opacity: 60, color: '#1b1020'})},
    {stroke: E.stroke({size: 50, color: '#f7c873'})}]),
    note: 'Stroke: the three positions, sizes 3, 10, 6 and 50, Multiply at 60%'});
  c.push({name: 'fx-color-overlay', canvas: S, layers: E.styled(['normal', 'multiply 60%', 'screen 80%', 'overlay, fill 0'], [
    {colorOverlay: E.overlay()}, {colorOverlay: E.overlay({blend: 'multiply', color: '#3a7bd5', opacity: 60})},
    {colorOverlay: E.overlay({blend: 'screen', color: '#f7c873', opacity: 80})},
    {colorOverlay: E.overlay({blend: 'overlay', color: '#2b1d4e'})}], [null, null, null, {fill: 0}]),
    note: 'Color Overlay in four modes; on a layer at Fill 0 (the effect stays)'});
  c.push({name: 'fx-gradient-overlay', canvas: S, layers: E.styled(['linear', 'radial', 'angle', 'reflected'], [
    {gradientOverlay: E.gradOverlay()}, {gradientOverlay: E.gradOverlay({style: 'radial', scale: 50, reverse: true})},
    {gradientOverlay: E.gradOverlay({style: 'angle', angle: 30, method: 'classic', align: false})},
    {gradientOverlay: E.gradOverlay({style: 'reflected', dither: true, offset: [20, 10], blend: 'multiply', opacity: 70})}]),
    note: 'Gradient Overlay: four styles, Scale, Reverse, Align off, Dither, an offset, Multiply at 70%'});
  c.push({name: 'fx-p2', canvas: S, layers: E.styled(['inner shadow, outer glow', 'bevel, satin', 'inner glow, two shadows', 'gradient stroke, hidden'], [
    {innerShadow: {on: true, blend: 'multiply', color: '#000000', opacity: 75, angle: 120, distance: 5, spread: 0, size: 5},
     outerGlow: {on: true, blend: 'screen', color: '#f7c873', opacity: 75, spread: 0, size: 10, range: 50}},
    {bevelEmboss: {on: true, angle: 120, altitude: 30, depth: 100, size: 5},
     satin: {on: true, blend: 'multiply', color: '#000000', opacity: 50, angle: 19, distance: 11, size: 14}},
    {innerGlow: {on: true, blend: 'screen', color: '#ffffbe', opacity: 75, spread: 0, size: 5, range: 50},
     dropShadowMulti: [E.shadow(), E.shadow({color: '#c03030', angle: 300, distance: 6, size: 3})]},
    {on: false, stroke: E.stroke({size: 6, fillType: 'gradient', gradient: E.GRADIENT, method: 'perceptual', style: 'linear',
                                 angle: 90, scale: 100, reverse: false, dither: false, align: true, offset: [0, 0]})}]),
    note: 'P2\'s effects (Inner Shadow, Outer Glow, Bevel and Emboss, Satin, Inner Glow), two Drop Shadows, a gradient Stroke, Effects hidden: kept, with notices (M44)'});

  // ---- Smart objects (M45; A31, L33, L35) ----
  c.push({name: 'so-placed', canvas: S, layers: [E.ground(),
    E.so({name: 'shapes.png'}, {id: 'shapes', file: 'shapes.png'}, {scale: 50, rotate: 30})],
    note: 'shapes.png placed embedded (its PNG kept in the file), scaled 50% and turned 30 degrees clockwise'});
  c.push({name: 'so-layers', canvas: S, layers: [E.ground(),
    E.so({name: 'smart'}, {id: 'inner', layers: [E.shapes({name: 'shapes'}), E.ramp({name: 'ramp', blend: 'multiply'}, [64, 64, 128, 128])]},
         {scale: 75, move: [-30, -20]}),
    E.so({name: 'smart copy'}, {id: 'inner'}, {rotate: -20, move: [40, 40]})],
    note: 'two layers converted to a smart object (an embedded PSB), scaled 75%; a copy sharing its contents, turned 20 degrees back'});
  c.push({name: 'so-filter', canvas: S, layers: [E.ground(),
    E.so({name: 'blurred'}, {id: 'blurred', file: 'shapes.png'}, {scale: 80}, {smartFilters: [{filter: 'gaussianBlur', radius: 3}]})],
    note: 'a smart filter (Gaussian Blur 3 px) on a placed smart object: P2, kept, with a notice'});
  c.push({name: 'so-linked', canvas: S, layers: [E.ground(),
    E.so({name: 'linked'}, {id: 'linked', file: 'shapes.png', linked: true}, {scale: 60})],
    note: 'shapes.png placed linked (its path on the machine that made it): P2, kept, with a notice'});

  // ---- Text and shapes (M46; Y30, Y32) ----
  c.push({name: 'text', canvas: S, layers: [E.ground(),
    E.text({text: 'Xin ch\u00e0o Photoshop', font: 'SpaceGrotesk-SemiBold', size: 28, color: '#1b1020', tracking: 20, aa: 'smooth', at: [16, 60]},
           {name: 'point text'}),
    E.text({text: 'Ti\u1ebfng Vi\u1ec7t c\u00f3 d\u1ea5u: \u1ed1 \u1eef \u1eb7 \u1ef7. Ch\u1eef \u0111o\u1ea1n c\u0103n gi\u1eefa trong khung.',
            font: 'SpaceGrotesk-Regular', size: 14, color: '#3a3a3a', aa: 'sharp', box: [16, 96, 224, 96], align: 'center'},
           {name: 'paragraph text'}),
    E.text({text: 'ch\u1eef nh\u1ecf kh\u00f4ng kh\u1eed r\u0103ng c\u01b0a', font: 'SpaceGrotesk-SemiBold', size: 10, color: '#3a7bd5', aa: 'none', at: [16, 232]},
           {name: 'small, no anti-aliasing'})],
    note: 'text layers (Space Grotesk, README-psd.md): point text with tracking; paragraph text, centred, Vietnamese; Anti-aliasing None'});
  var shapeCase = function (name, source, note) {
    c.push({name: 'shape-' + name, canvas: S, layers: [E.ground(), E.shape(source, {name: name})], note: note});
  };
  shapeCase('rectangle', {type: 'rectangle', box: [32, 48, 224, 208], radii: [8, 16, 24, 32], fill: '#c0587e',
                          stroke: {width: 4, color: '#1b1020', align: 'inside', cap: 'butt', join: 'miter'}},
            'a live Rectangle, each corner its own radius, Stroke 4 px inside');
  shapeCase('ellipse', {type: 'ellipse', box: [48, 72, 208, 184], fill: '#3a7bd5', stroke: {width: 6, color: '#f7c873', align: 'center', cap: 'butt', join: 'miter'}},
            'a live Ellipse, Stroke 6 px centred');
  shapeCase('triangle', {type: 'triangle', box: [40, 40, 216, 216], radius: 6, fill: '#4caf50',
                         stroke: {width: 3, color: '#1b1020', align: 'outside', cap: 'butt', join: 'miter'}},
            'a live Triangle (Photoshop 2021), corners 6 px round, Stroke 3 px outside');
  shapeCase('polygon', {type: 'polygon', box: [40, 40, 216, 216], sides: 6, star: 60, radius: 0, fill: '#e5d03a', stroke: null},
            'a live Polygon (Photoshop 2021), 6 sides, Star Ratio 60%, no stroke');
  shapeCase('line', {type: 'line', from: [32, 200], to: [224, 48], weight: 6, fill: '#1b1020', stroke: null},
            'a live Line 6 px');
  shapeCase('path', {type: 'path', points: [[40, 200], [128, 32], [216, 200], [128, 150]], fill: '#9e2f45', stroke: null},
            'a shape layer of a path, not a live shape: P2, with a notice (M46)');
  c.push({name: 'vector-mask', canvas: S, layers: [E.bands(),
    E.ramp({name: 'masked by a path'}, [0, 0, 256, 256], {vectorMask: [[32, 32], [224, 64], [192, 224], [48, 192]]})],
    note: 'a vector mask on a pixel layer: P2, kept, with a notice (M46)'});

  // ---- Depth and mode (M40, M41, M47) ----
  var deep = function () {
    return [E.bands(), E.shapes({name: 'shapes', blend: 'multiply'}, {mask: 'shapes.png'}),
            E.group({name: 'group', blend: 'screen', opacity: 0.6}, [E.ramp({name: 'ramp'}, [64, 64, 128, 128])])];
  };
  c.push({name: 'depth16', canvas: S, depth: 16, crop: [64, 64, 128, 128], layers: deep(),
          note: 'RGB 16 bits (layers ZIP with prediction), cropped to 128 x 128 to stay small'});
  c.push({name: 'gray8', canvas: S, mode: 'gray', layers: deep(), note: 'Grayscale 8 bits, the working grey profile'});
  c.push({name: 'gray16', canvas: S, mode: 'gray', depth: 16, crop: [64, 64, 128, 128], layers: deep(),
          note: 'Grayscale 16 bits, cropped to 128 x 128'});
  c.push({name: 'cmyk', canvas: S, mode: 'cmyk', layers: [E.bands(), E.shapes({name: 'shapes'})],
          note: 'CMYK 8 bits, the working CMYK profile, two layers (M47: two RGB layers and a notice); the PNG made sRGB'});
  c.push({name: 'lab', canvas: S, mode: 'lab', layers: [E.bands(), E.shapes({name: 'shapes', opacity: 0.8})],
          note: 'Lab 8 bits, two layers; the PNG made sRGB'});
  c.push({name: 'rgb32', canvas: S, depth: 32, crop: [96, 96, 64, 64], layers: [E.bands(), E.shapes({name: 'shapes', blend: 'multiply'})],
          note: '32 bits a channel (Lr32), cropped to 64 x 64; the composite as a 32-bit TIFF, the PNG through Exposure and Gamma'});
  c.push({name: 'indexed', canvas: S, convert: 'indexed', layers: [E.bands(), E.shapes({name: 'shapes'})],
          note: 'flattened, Indexed Color (64 colours, no dither): no layers in the file, the composite only'});
  c.push({name: 'bitmap', canvas: S, convert: 'bitmap', layers: [E.bands(), E.shapes({name: 'shapes'})],
          note: 'flattened, Bitmap (50% Threshold): 1 bit, the composite only'});

  // ---- Large: out/psd-big/, to samples/psd/ (not in git) ----
  c.push({name: 'psb-wide', big: true, psb: true, canvas: [31000, 256], layers: [
    E.fill({color: '#d8d0c0'}, {name: 'ground'}),
    E.paint({name: 'strip'}, [[0, 200, 31000, 40, '#3a7bd5']]),
    E.shapes({name: 'left'}),
    E.shapes({name: 'right'}, {move: [30700, 0]}),
    E.group({name: 'middle', blend: 'normal'}, [E.px('ramp.png', {name: 'bands', blend: 'multiply'}, {rotate: 90, move: [15400, 0]})])],
    note: 'a PSB 31,000 px wide: a fill layer, a strip across it all, shapes at both ends (one past 30,000 px), a group in the middle'});
  return c;
}

if (typeof app != 'undefined') E.main();  // jscheck runs it without Photoshop
