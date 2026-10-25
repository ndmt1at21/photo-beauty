// Photoshop's images of its selection commands (S39), for S30-S34's tests: Select > Modify (Feather, Expand,
// Contract, Border, Smooth), Inverse, Magic Wand, Grow, Similar, Color Range, Lasso, Elliptical and Rectangular
// Marquee, and the four ways of combining selections; README-select.md has the steps. Writes out/select/ next to this
// script: <case>.png (the selection, saved as a channel, as a grey 16-bit PNG: white is selected), index.json (each case
// as photo-beauty's scene, its selection's steps as "select", tests/golden.h reads it), photoshop.txt. Uses p1/lib.jsx
// (B30's PB.run: a case built in a 16-bit sRGB document; PB.save replaced here by S.save). Inputs by their path from
// this script's folder, as from tests/golden/ps/ in the repo: select/in/ (pb-bench ps-select: hard, anti-aliased and
// soft selections, two ramps), loaded as selections through a channel; p1/in/ (B30's: photo.png, ramp.png, cube.png,
// shapes.png), what Magic Wand and Color Range pick from. 79 cases, about 1.2 MB, about 4 minutes. Not run in Photoshop
// yet: what it is unsure of is in README-select.md.
//
// A case: {name, canvas, layers (one, or two for Sample All Layers), select: [step, ...]}; a step {op, ...}: op one of
// S.SPEC's keys, its values in S.SPEC's order (S.op: checked, defaults filled in). The steps run in order on the built
// document, then the selection is saved as a channel (Select > Save Selection) and that channel becomes the PNG.
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
#include "p1/lib.jsx"

var S = {};

// ---- The steps -----------------------------------------------------------------------------------------------------

// Each step's values, in the order index.json has them: [key, default, kind, min, max]; default null: the case must
// give it; undefined: left out unless given. Kinds: 'input' (a path of an input, from tests/golden/ps/), 'mode' (of
// S.MODES), 'bool', 'number' (min to max, multiples of 1/1000), 'int', 'point' ([x, y] whole pixels), 'samples' (an
// input's RGBA samples there, 16 bits, as in the file), 'lab' ([L, a, b] of photo-beauty's sRGB to CIELAB D50, 2
// decimals), 'pick' (of S.PICKS), 'range' ([low, high] levels of 8 bits), 'points' ([[x, y], ...] multiples of 1/8),
// 'rect' ([x, y, w, h] multiples of 1/8), 'string'. Keys and units as the interface's: Select > Modify's dialog
// (ModifySelectionDialog.qml: px, canvasBounds), Magic Wand's options (ToolSpec.js: Tolerance, Anti-alias, Contiguous,
// Sample All Layers), Color Range (ColorRangeDialog.qml: select, fuzziness, range, detectFaces, invert).
S.SPEC = {
  load: [['src', null, 'input'], ['mode', 'new', 'mode']],  // Select > Load Selection of a grey input's channel
  inverse: [],                                              // Select > Inverse
  feather: [['px', null, 'number', 0.1, 1000], ['canvasBounds', false, 'bool']],
  expand: [['px', null, 'int', 1, 500], ['canvasBounds', false, 'bool']],
  contract: [['px', null, 'int', 1, 500], ['canvasBounds', false, 'bool']],
  border: [['px', null, 'int', 1, 200]],                    // no Apply effect at canvas bounds in its dialog
  smooth: [['px', null, 'int', 1, 500], ['canvasBounds', false, 'bool']],
  wand: [['at', null, 'point'], ['color', undefined, 'samples'], ['tolerance', 32, 'int', 0, 255],
         ['antiAlias', true, 'bool'], ['contiguous', true, 'bool'], ['sampleAll', false, 'bool'],
         ['layer', undefined, 'string'], ['sampleSize', 'point', 'string'], ['mode', 'new', 'mode']],
  grow: [['tolerance', 32, 'int', 0, 255], ['antiAlias', true, 'bool']],
  similar: [['tolerance', 32, 'int', 0, 255], ['antiAlias', true, 'bool']],
  colorRange: [['select', 'sampledColors', 'pick'], ['fuzziness', undefined, 'int', 0, 200], ['range', undefined, 'range'],
               ['at', undefined, 'point'], ['color', undefined, 'samples'], ['lab', undefined, 'lab'],
               ['detectFaces', undefined, 'bool'], ['invert', false, 'bool']],
  lasso: [['points', null, 'points'], ['antiAlias', true, 'bool'], ['mode', 'new', 'mode']],
  ellipse: [['rect', null, 'rect'], ['antiAlias', true, 'bool'], ['mode', 'new', 'mode']],
  rect: [['rect', null, 'rect'], ['mode', 'new', 'mode']]
};
S.MODES = ['new', 'add', 'subtract', 'intersect'];  // S31's: New, Add (Shift), Subtract (Alt), Intersect (Shift Alt)
// Color Range's Select, by ColorRangeDialog.qml's names: Photoshop's Clrs (a charID, s: a stringID), null for Sampled
// Colors (Mnm , Mxm  instead); tones: the keys of their Fuzziness and Range (Photoshop CC).
S.PICKS = {sampledColors: null, reds: 'Rds ', yellows: 'Ylws', greens: 'Grns', cyans: 'Cyns', blues: 'Bls ',
           magentas: 'Mgnt', highlights: 'Hghl', midtones: 'Mdtn', shadows: 'Shdw', skinTones: 's:skinTone',
           outOfGamut: 'OtOf'};
S.TONES = {highlights: ['highlightsFuzziness', 'highlightsLowerLimit', null],
           midtones: ['midtonesFuzziness', 'midtonesLowerLimit', 'midtonesUpperLimit'],
           shadows: ['shadowsFuzziness', null, 'shadowsUpperLimit']};

S.multiple = function (v, k) {  // of 1/k, as decimals written as such are (0.1 * 1000 may be off by an ulp)
  return typeof v == 'number' && isFinite(v) && Math.abs(v * k - Math.round(v * k)) < 1e-6;
};
S.isPair = function (v, k) { return v instanceof Array && v.length == 2 && S.multiple(v[0], k) && S.multiple(v[1], k); };
S.valid = function (spec, v) {
  var kind = spec[2], i;
  if (kind == 'input') return typeof v == 'string' && /^(select|p1)\/in\/[a-z0-9-]+\.png$/.test(v);
  if (kind == 'mode') return PB.has(S.MODES, v);
  if (kind == 'bool') return typeof v == 'boolean';
  if (kind == 'string') return typeof v == 'string' && v.length > 0;
  if (kind == 'number') return S.multiple(v, 1000) && v >= spec[3] && v <= spec[4];
  if (kind == 'int') return S.multiple(v, 1) && v >= spec[3] && v <= spec[4];
  if (kind == 'point') return S.isPair(v, 1) && v[0] >= 0 && v[1] >= 0;
  if (kind == 'pick') return S.PICKS[v] !== undefined;
  if (kind == 'range') return S.isPair(v, 1) && v[0] >= 0 && v[0] <= v[1] && v[1] <= 255;
  if (kind == 'lab') return v instanceof Array && v.length == 3 && S.multiple(v[0], 100) && S.multiple(v[1], 100) && S.multiple(v[2], 100);
  if (kind == 'samples') {
    if (!(v instanceof Array) || v.length != 4) return false;
    for (i = 0; i < 4; i++)
      if (!S.multiple(v[i], 1) || v[i] < 0 || v[i] > 65535) return false;
    return true;
  }
  if (kind == 'rect') return v instanceof Array && v.length == 4 && S.isPair([v[0], v[1]], 8) && S.isPair([v[2], v[3]], 8) &&
                             v[2] > 0 && v[3] > 0;
  if (kind == 'points') {
    if (!(v instanceof Array) || v.length < 3) return false;
    for (i = 0; i < v.length; i++)
      if (!S.isPair(v[i], 8)) return false;
    return true;
  }
  throw new Error('no kind ' + kind);
};
// A step, checked: its values in S.SPEC's order, the defaults of those the case leaves out.
S.op = function (op, given) {
  var spec = S.SPEC[op], out = {op: op}, i, k;
  if (!spec) throw new Error('no step ' + op);
  given = given || {};
  for (k in given)
    if (given.hasOwnProperty(k)) {
      var known = false;
      for (i = 0; i < spec.length; i++)
        if (spec[i][0] == k) known = true;
      if (!known) throw new Error(op + ' has no value ' + k);
    }
  for (i = 0; i < spec.length; i++) {
    var s = spec[i], v = given[s[0]] === undefined ? PB.copy(s[1]) : given[s[0]];
    if (v === undefined) continue;
    if (v === null) throw new Error(op + ' needs ' + s[0]);
    if (!S.valid(s, v)) throw new Error(op + ': ' + s[0] + ' ' + PB.json(v));
    out[s[0]] = v;
  }
  if (op == 'colorRange') S.checkRange(out);
  return out;
};
// Color Range's values by what it selects: Sampled Colors a colour (lab, from at and color) and Fuzziness; the tones
// Fuzziness and Range; Skin Tones Fuzziness and Detect Faces; the colour families nothing more.
S.checkRange = function (o) {
  var tone = S.TONES[o.select] !== undefined, sampled = o.select == 'sampledColors', skin = o.select == 'skinTones';
  if ((sampled || tone || skin) != (o.fuzziness !== undefined)) throw new Error(o.select + ': fuzziness ' + o.fuzziness);
  if (tone != (o.range !== undefined)) throw new Error(o.select + ': range ' + PB.json(o.range));
  if (sampled != (o.lab !== undefined)) throw new Error(o.select + ': lab ' + PB.json(o.lab));
  if (skin != (o.detectFaces !== undefined)) throw new Error(o.select + ': detectFaces ' + o.detectFaces);
  if (o.select == 'highlights' && o.range[1] != 255) throw new Error('highlights: a range to 255');
  if (o.select == 'shadows' && o.range[0] != 0) throw new Error('shadows: a range from 0');
};

// sRGB samples (16 bits, 0..65535) as CIELAB D50 (ICC's connection space, Color Range's Mnm  and Mxm ): the sRGB
// curve, the D50 matrix of the sRGB IEC61966-2.1 profile, D50's white; 2 decimals, the same digits whatever engine.
S.lab = function (samples) {
  var lin = [], i;
  for (i = 0; i < 3; i++) {
    var s = samples[i] / 65535;
    lin.push(s <= 0.04045 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4));
  }
  var x = (0.4360747 * lin[0] + 0.3850649 * lin[1] + 0.1430804 * lin[2]) / 0.9642;
  var y = 0.2225045 * lin[0] + 0.7168786 * lin[1] + 0.0606169 * lin[2];
  var z = (0.0139322 * lin[0] + 0.0971045 * lin[1] + 0.7141733 * lin[2]) / 0.8249;
  var f = function (t) { return t > 216 / 24389 ? Math.pow(t, 1 / 3) : (24389 / 27 * t + 16) / 116; };
  var r = function (v) { return Math.round(v * 100) / 100; };
  return [r(116 * f(y) - 16), r(500 * (f(x) - f(y))), r(200 * (f(y) - f(z)))];
};

// ---- Running them in Photoshop -------------------------------------------------------------------------------------

S.c = function (s) { return charIDToTypeID(s); };
S.s = function (s) { return stringIDToTypeID(s); };
S.id = function (s) { return /^s:/.test(s) ? S.s(s.slice(2)) : S.c(s); };
S.OUT = 'selection';  // the channel a case's selection ends in
S.TYPE = function (mode) {
  return {'new': SelectionType.REPLACE, add: SelectionType.EXTEND, subtract: SelectionType.DIMINISH,
          intersect: SelectionType.INTERSECT}[mode];
};
S.EVENT = {'new': 'setd', add: 'AddT', subtract: 'SbtF', intersect: 'IntW'};  // a marquee's, Magic Wand's, by mode
S.fsel = function (d) {  // the selection as the target of an event
  var r = new ActionReference();
  r.putProperty(S.c('Chnl'), S.c('fsel'));
  d.putReference(S.c('null'), r);
};
S.px = function (d, key, v) { d.putUnitDouble(S.c(key), S.c('#Pxl'), v); };
S.event = function (id, fill) {
  var d = new ActionDescriptor();
  fill(d);
  PB.note('      AM ' + PB.keyName(id) + ' {' + PB.dump(d) + '}');
  executeAction(id, d, DialogModes.NO);
};
S.channel = function (doc, name) {
  for (var i = 0; i < doc.channels.length; i++)
    if (doc.channels[i].name == name) return doc.channels[i];
  return null;
};
S.components = function (doc) {  // the colour channels active again, the alpha ones hidden: what tools and fills see
  doc.activeChannels = doc.componentChannels;
  for (var i = 0; i < doc.channels.length; i++)
    if (doc.channels[i].kind != ChannelType.COMPONENT) doc.channels[i].visible = false;
};
// A channel's histogram as a string, to compare two; null where Photoshop gives none (noted once), and then the checks
// that compare them pass.
S.histogram = function (ch) {
  try {
    return ch.histogram.join(',');
  } catch (e) {
    if (!S.noHistogram) PB.note('no histogram of channel ' + ch.name + ' (' + e + '): channels are not compared');
    S.noHistogram = true;
    return null;
  }
};
S.noHistogram = false;
S.alike = function (a, b) { return a === null || b === null || a == b; };
S.unlike = function (got, want) {  // where two histograms (S.histogram's) part: for photoshop.txt
  var a = got.split(','), b = want.split(',');
  for (var i = 0; i < a.length; i++)
    if (a[i] != b[i]) return 'level ' + i + ' of 255 has ' + a[i] + ' pixels, not ' + b[i];
  return 'the same';
};

// An input's red channel (its grey: the selection it holds) as an alpha channel of doc, "in <src>": made once a case,
// before any step, as the second way loads a selection. Duplicate Channel from the input's document; else the input's
// layer copied on top (opaque, so the composite is it), its red channel loaded as the selection (Ctrl click), saved.
// Either way the channel's histogram must be the red one's.
S.inputChannel = function (doc, src) {
  var name = 'in ' + src, ch = S.channel(doc, name);
  if (ch) return ch;
  var input = PB.input(src, 0), errors = [];
  app.activeDocument = input;
  var red = S.histogram(input.channels[0]);
  var ways = [['Duplicate Channel', function () {
    app.activeDocument = input;
    input.channels[0].duplicate(doc);
    app.activeDocument = doc;
    var c = doc.channels[doc.channels.length - 1];
    if (c.kind == ChannelType.COMPONENT) throw new Error('no channel came');
    return c;
  }], ['its red loaded', function () {
    app.activeDocument = doc;
    var keep = doc.activeLayer, tmp = PB.copyLayer(doc, src, 0), d = new ActionDescriptor(), t = new ActionReference();
    doc.activeLayer = tmp;
    S.fsel(d);
    t.putEnumerated(S.c('Chnl'), S.c('Chnl'), S.c('Rd  '));
    d.putReference(S.c('T   '), t);
    executeAction(S.c('setd'), d, DialogModes.NO);
    var c = doc.channels.add();
    doc.selection.store(c, SelectionType.REPLACE);
    doc.selection.deselect();
    tmp.remove();
    doc.activeLayer = keep;
    return c;
  }]];
  for (var i = 0; i < ways.length; i++) {
    try {
      ch = ways[i][1]();
      S.components(doc);
      if (S.alike(S.histogram(ch), red)) {
        ch.name = name;
        PB.note('    ' + name + ': ' + ways[i][0] + (errors.length ? ' (' + errors.join('; ') + ')' : ''));
        return ch;
      }
      errors.push(ways[i][0] + ': its histogram is not the red one\'s (' + S.unlike(S.histogram(ch), red) + ')');
      ch.remove();
    } catch (e) {
      app.activeDocument = doc;
      errors.push(ways[i][0] + ': ' + e);
    }
  }
  throw new Error(src + ': no channel of it (' + errors.join('; ') + ')');
};

// Magic Wand as Reset Tool leaves it (Sample Size: Point Sample), once a run; photoshop.txt gets its options.
S.wandReady = false;
S.resetWand = function () {
  if (S.wandReady) return;
  S.wandReady = true;
  try {
    var d = new ActionDescriptor(), r = new ActionReference(), o = new ActionReference();
    r.putClass(S.s('magicWandTool'));
    d.putReference(S.c('null'), r);
    executeAction(S.c('slct'), d, DialogModes.NO);
    o.putProperty(S.c('Prpr'), S.c('CrnT'));
    o.putEnumerated(S.c('capp'), S.c('Ordn'), S.c('Trgt'));
    d = new ActionDescriptor();
    d.putReference(S.c('null'), o);
    executeAction(S.c('Rset'), d, DialogModes.NO);  // Reset Tool
    PB.note('magic wand reset: ' + PB.dump(executeActionGet(o).getObjectValue(S.c('CrnT'))));
  } catch (e) {
    PB.note('magic wand: no Reset Tool (' + e + '): its Sample Size is what it was');
  }
};

// Select > Modify's commands: Action Manager, as the dialog records them (the DOM has no Apply effect at canvas bounds).
S.modify = function (id, key) {
  return function (o) {
    S.event(S.c(id), function (d) {
      S.px(d, key, o.px);
      if (o.canvasBounds !== undefined) d.putBoolean(S.s('selectionModifyEffectAtCanvasBounds'), o.canvasBounds);
    });
  };
};
// A marquee's shape: rect [x, y, w, h] as Top, Left, Btom, Rght, by mode.
S.shape = function (cls) {
  return function (o) {
    S.event(S.c(S.EVENT[o.mode]), function (d) {
      var b = new ActionDescriptor(), r = o.rect;
      S.fsel(d);
      S.px(b, 'Top ', r[1]);
      S.px(b, 'Left', r[0]);
      S.px(b, 'Btom', r[1] + r[3]);
      S.px(b, 'Rght', r[0] + r[2]);
      d.putObject(S.c('T   '), S.c(cls), b);
      if (o.antiAlias !== undefined) d.putBoolean(S.c('AntA'), o.antiAlias);
    });
  };
};
S.layerNamed = function (built, name) {
  for (var i = 0; i < built.length; i++)
    if (built[i].json.props.name == name) return built[i].layer;
  throw new Error('no layer ' + name);
};
// Each step: function (step, doc, built), on the document built.
S.RUN = {
  load: function (o, doc) { doc.selection.load(S.inputChannel(doc, o.src), S.TYPE(o.mode), false); },
  inverse: function (o, doc) { doc.selection.invert(); },
  feather: S.modify('Fthr', 'Rds '),
  expand: S.modify('Expn', 'By  '),
  contract: S.modify('Cntc', 'By  '),
  border: S.modify('Brdr', 'Wdth'),
  smooth: S.modify('Smth', 'Rds '),
  wand: function (o, doc, built) {
    S.resetWand();
    if (o.layer) doc.activeLayer = S.layerNamed(built, o.layer);
    S.components(doc);
    S.event(S.c(S.EVENT[o.mode]), function (d) {
      var p = new ActionDescriptor();
      S.fsel(d);
      S.px(p, 'Hrzn', o.at[0]);
      S.px(p, 'Vrtc', o.at[1]);
      d.putObject(S.c('T   '), S.c('Pnt '), p);
      d.putInteger(S.c('Tlrn'), o.tolerance);
      d.putBoolean(S.c('AntA'), o.antiAlias);
      d.putBoolean(S.c('Cntg'), o.contiguous);
      d.putBoolean(S.c('Mrgd'), o.sampleAll);
    });
  },
  grow: function (o, doc) { doc.selection.grow(o.tolerance, o.antiAlias); },
  similar: function (o, doc) { doc.selection.similar(o.tolerance, o.antiAlias); },
  colorRange: function (o, doc) {
    doc.selection.deselect();  // Color Range picks inside a selection there is
    S.components(doc);
    S.event(S.c('ClrR'), function (d) {
      var tone = S.TONES[o.select];
      if (o.select == 'sampledColors') {
        var lab = new ActionDescriptor();
        lab.putDouble(S.c('Lmnc'), o.lab[0]);
        lab.putDouble(S.c('A   '), o.lab[1]);
        lab.putDouble(S.c('B   '), o.lab[2]);
        d.putInteger(S.c('Fzns'), o.fuzziness);
        d.putObject(S.c('Mnm '), S.c('LbCl'), lab);
        d.putObject(S.c('Mxm '), S.c('LbCl'), lab);
      } else {
        d.putEnumerated(S.c('Clrs'), S.c('Clrs'), S.id(S.PICKS[o.select]));
        if (o.select == 'skinTones') {
          d.putInteger(S.c('Fzns'), o.fuzziness);
          d.putBoolean(S.s('UseFacesKey'), o.detectFaces);
        }
        if (tone) {
          d.putInteger(S.s(tone[0]), o.fuzziness);
          if (tone[1]) d.putInteger(S.s(tone[1]), o.range[0]);
          if (tone[2]) d.putInteger(S.s(tone[2]), o.range[1]);
        }
      }
      if (o.invert) d.putBoolean(S.c('Invr'), true);
      d.putInteger(S.s('colorModel'), 0);
    });
  },
  lasso: function (o, doc) { doc.selection.select(o.points, S.TYPE(o.mode), 0, o.antiAlias); },
  ellipse: S.shape('Elps'),
  rect: S.shape('Rctn')
};

// PB.steps.select: the steps of a case on its document, after its layers (lib.jsx's PB.build), then the selection
// saved as channel S.OUT (Select > Save Selection) for S.save; the inputs' channels first.
S.step = function (steps, doc, built) {
  var i;
  for (i = 0; i < steps.length; i++)
    if (steps[i].op == 'load') S.inputChannel(doc, steps[i].src);
  doc.activeLayer = built[built.length - 1].layer;  // the top one, unless a step says
  for (i = 0; i < steps.length; i++) {
    var o = steps[i];
    if (!S.RUN[o.op]) throw new Error('no step ' + o.op);
    PB.note('    ' + PB.json(o));
    S.RUN[o.op](o, doc, built);
    var bounds = 'none';
    try {
      var b = doc.selection.bounds;
      bounds = [b[0].as('px'), b[1].as('px'), b[2].as('px'), b[3].as('px')].join(', ');
    } catch (e) {}
    PB.note('      selection bounds: ' + bounds);
  }
  var out = doc.channels.add();
  out.name = S.OUT;
  try {
    doc.selection.store(out, SelectionType.REPLACE);
  } catch (e) {
    throw new Error('no selection to save (' + e + ')');
  }
  S.components(doc);
};
PB.steps.select = S.step;

// PB.save for this script: channel S.OUT of doc as a grey 16-bit PNG, <name>.png: a new grey document of the canvas,
// black, white filled into it through that channel as the selection (a fill by the selection: level s of 32768 makes
// s exactly), its histogram checked against the channel's. The channel comes as a selection by Duplicate Channel into
// the grey document, loaded there; else by Load Selection of the case's document's channel. The first PNG is opened
// again to see it is grey at 16 bits.
S.save = function (doc, name, bits) {
  var out = S.channel(doc, S.OUT), w = doc.width.as('px'), h = doc.height.as('px'), errors = [];
  if (!out) throw new Error('no channel ' + S.OUT);
  if (bits != 16) throw new Error('bits ' + bits);
  app.activeDocument = doc;
  var want = S.histogram(out), from = doc.name;
  var grey = app.documents.add(w, h, 72, name + ' selection', NewDocumentMode.GRAYSCALE, DocumentFill.WHITE, 1,
                               BitsPerChannelType.SIXTEEN);
  var ways = [['Duplicate Channel', function () {
    app.activeDocument = doc;
    out.duplicate(grey);
    app.activeDocument = grey;
    var copy = grey.channels[grey.channels.length - 1];
    if (copy.kind == ChannelType.COMPONENT) throw new Error('the channel did not come');
    S.components(grey);
    grey.selection.load(copy, SelectionType.REPLACE, false);
    return copy;
  }], ['Load Selection of its document', function () {
    app.activeDocument = grey;
    var d = new ActionDescriptor(), t = new ActionReference();
    S.fsel(d);
    t.putName(S.c('Chnl'), S.OUT);
    t.putName(S.c('Dcmn'), from);
    d.putReference(S.c('T   '), t);
    executeAction(S.c('setd'), d, DialogModes.NO);
    return null;
  }]];
  try {
    for (var i = 0; i < ways.length; i++) {
      app.activeDocument = grey;
      S.components(grey);
      grey.selection.selectAll();
      grey.selection.fill(PB.solid(0));
      var black = S.histogram(grey.channels[0]);
      if (black !== null && black.split(',')[0] != w * h) throw new Error('black is not 0 in the grey document');
      var copy = null;
      try {
        copy = ways[i][1]();
        app.activeDocument = grey;
        grey.selection.fill(PB.solid(1));
        grey.selection.deselect();
        if (copy) copy.remove();
      } catch (e) {
        errors.push(ways[i][0] + ': ' + e);
        continue;
      }
      var got = S.histogram(grey.channels[0]);
      if (S.alike(got, want)) break;
      errors.push(ways[i][0] + ': its histogram is not the channel\'s (' + S.unlike(got, want) + ')');
    }
    if (i == ways.length) throw new Error('the selection did not come to the grey image (' + errors.join('; ') + ')');
    if (errors.length) PB.note('    saved by ' + ways[i][0] + ' (' + errors.join('; ') + ')');
    grey.saveAs(new File(PB.out + '/' + name + '.png'), PB.pngOptions(), true, Extension.LOWERCASE);
  } finally {
    PB.close(grey);
    app.activeDocument = doc;
  }
  if (S.checked) return;
  var check = app.open(new File(PB.out + '/' + name + '.png')), mode = check.mode, depth = check.bitsPerChannel;
  check.close(SaveOptions.DONOTSAVECHANGES);
  app.activeDocument = doc;
  if (mode != DocumentMode.GRAYSCALE || depth != BitsPerChannelType.SIXTEEN)
    throw new Error('Photoshop saved ' + name + '.png as ' + mode + ', ' + depth + ', not grey at 16 bits');
  S.checked = true;
};
S.checked = false;

// Every case, in Photoshop: lib.jsx's PB.run, the selection saved instead of the composite.
S.run = function () {
  PB.save = S.save;
  PB.run('select', cases(), S.options());
};
S.options = function () {  // the inputs from this folder (select/in/, p1/in/): from tests/golden/ps/ in the repo
  return {bits: 16, dir: HERE, inputs: '.', repoInputs: '..'};
};

// ---- The cases -----------------------------------------------------------------------------------------------------

// Inputs by their path from tests/golden/ps/ (from this script's folder in Photoshop): this set's selections, B30's
// images.
S.HARD = 'select/in/hard.png';
S.AA = 'select/in/aa.png';
S.SOFT = 'select/in/soft.png';
S.RAMP_X = 'select/in/ramp-x.png';
S.RAMP_Y = 'select/in/ramp-y.png';
S.PHOTO = 'p1/in/photo.png';
S.RAMP = 'p1/in/ramp.png';
S.CUBE = 'p1/in/cube.png';
S.SHAPES = 'p1/in/shapes.png';
// Where Magic Wand and Color Range pick: [x, y] and that pixel's samples in its input (RGBA, 16 bits;
// test_ps_select_index checks them against the files).
S.AT = {
  bowl: [[60, 160], [49924, 48380, 45296, 65535]],    // photo.png: inside the bowl, light grey beige
  chilli: [[225, 190], [64990, 8690, 5220, 65535]],   // photo.png: the red chilli on the right
  wood: [[20, 20], [49458, 48172, 46630, 65535]],     // photo.png: the table, top left
  woodClear: [[20, 20], [0, 0, 0, 0]],                // shapes.png there: transparent
  grey: [[128, 16], [32896, 32896, 32896, 65535]],    // ramp.png: band 0 (grey) at 128 / 255
  lime: [[64, 144], [32768, 65535, 0, 65535]],        // ramp.png: band 4, hue 90 degrees at (S, V) (1, 1)
  mid: [[108, 94], [32768, 32768, 32768, 65535]],     // cube.png: lattice colour (16, 16, 16) of 32
  rust: [[12, 48], [49152, 24576, 16384, 65535]],     // cube.png: lattice colour (24, 12, 8)
  line: [[150, 136], [62914, 60294, 19660, 65535]],   // shapes.png: the line 1 px high
  clear: [[8, 8], [0, 0, 0, 0]]                       // shapes.png: transparent black
};
S.wand = function (at, vals) {
  vals = PB.copy(vals || {});
  vals.at = S.AT[at][0];
  vals.color = S.AT[at][1];
  return S.op('wand', vals);
};
S.sampled = function (at, fuzziness) {  // Color Range, Sampled Colors: one colour, the pixel's (its Lab given)
  var c = S.AT[at][1];
  return S.op('colorRange', {select: 'sampledColors', fuzziness: fuzziness, at: S.AT[at][0], color: c, lab: S.lab(c)});
};
// A case: its layers (inputs, bottom first: the image the steps pick from, else the selection loaded) and its steps.
S.one = function (name, srcs, steps) {
  srcs = srcs instanceof Array ? srcs : [srcs];
  var layers = [];
  for (var i = 0; i < srcs.length; i++)
    layers.push({kind: 'pixel', src: srcs[i], props: {name: srcs[i].replace(/^.*\//, '').replace(/\.png$/, '')}});
  return {name: name, canvas: srcs[0] == S.CUBE ? [190, 190] : [256, 256], layers: layers, select: steps};
};
// A pentagram about (128, 128), its points 100 px out (multiples of 1/8), drawn point to point: it crosses itself,
// the pentagon in the middle wound twice (filled by the non-zero rule, a hole by the even-odd one).
S.STAR = [[128, 28], [186.75, 208.875], [32.875, 97.125], [223.125, 97.125], [69.25, 208.875]];

function cases() {
  var c = [], o = S.op, i, j, k;
  var add = function (name, srcs, steps) { c.push(S.one(name, srcs, steps)); };
  var load = function (src, mode) { return o('load', {src: src, mode: mode}); };

  // The way in and out: each input loaded and saved as it was (test_ps_select: to the level of 16 bits); Inverse.
  var ins = [['hard', S.HARD], ['aa', S.AA], ['soft', S.SOFT], ['ramp-x', S.RAMP_X]];
  for (i = 0; i < ins.length; i++) add('load-' + ins[i][0], ins[i][1], [load(ins[i][1])]);
  add('inverse-soft', S.SOFT, [load(S.SOFT), o('inverse')]);

  // Feather (S30): Radius 1, 5, 20, 100 px, Apply effect at canvas bounds off and on, and 0.5, 2.5 (sigma at small
  // radii), of the hard selection (straight edges, corners, lines, single pixels: test_ps_select fits sigma); 5 px of
  // the anti-aliased and the soft one.
  var radii = [[0.5, '0_5'], [1, '1'], [2.5, '2_5'], [5, '5'], [20, '20'], [100, '100']];
  for (i = 0; i < radii.length; i++)
    for (k = 0; k < 2; k++)
      if (!k || radii[i][0] % 1 == 0)
        add('feather-hard-r' + radii[i][1] + (k ? '-bounds' : ''), S.HARD,
            [load(S.HARD), o('feather', {px: radii[i][0], canvasBounds: k == 1})]);
  add('feather-aa-r5', S.AA, [load(S.AA), o('feather', {px: 5})]);
  add('feather-soft-r5', S.SOFT, [load(S.SOFT), o('feather', {px: 5})]);

  // Expand, Contract (S32): 5 and 50 px, bounds off and on, of the hard selection; 5 px of the anti-aliased and the
  // soft one (sub-pixel edges; which plateaus of soft.png count as selected). Border 10 px (its dialog has no canvas
  // bounds box), Smooth 10 px.
  var modify = ['expand', 'contract'];
  for (j = 0; j < 2; j++) {
    for (i = 0; i < 2; i++)
      for (k = 0; k < 2; k++)
        add(modify[j] + '-hard-r' + [5, 50][i] + (k ? '-bounds' : ''), S.HARD,
            [load(S.HARD), o(modify[j], {px: [5, 50][i], canvasBounds: k == 1})]);
    add(modify[j] + '-aa-r5', S.AA, [load(S.AA), o(modify[j], {px: 5})]);
    add(modify[j] + '-soft-r5', S.SOFT, [load(S.SOFT), o(modify[j], {px: 5})]);
  }
  add('border-hard-r10', S.HARD, [load(S.HARD), o('border', {px: 10})]);
  add('border-aa-r10', S.AA, [load(S.AA), o('border', {px: 10})]);
  add('smooth-hard-r10', S.HARD, [load(S.HARD), o('smooth', {px: 10})]);
  add('smooth-hard-r10-bounds', S.HARD, [load(S.HARD), o('smooth', {px: 10, canvasBounds: true})]);
  add('smooth-aa-r10', S.AA, [load(S.AA), o('smooth', {px: 10})]);

  // Magic Wand (S33) inside the photograph's bowl: Tolerance 0, 32, 100 x Contiguous x Anti-alias.
  var tolerances = [0, 32, 100];
  for (i = 0; i < 3; i++)
    for (j = 0; j < 2; j++)
      for (k = 0; k < 2; k++)
        add('wand-photo-t' + tolerances[i] + (j ? '-noncontig' : '') + (k ? '-noaa' : ''), S.PHOTO,
            [S.wand('bowl', {tolerance: tolerances[i], contiguous: j == 0, antiAlias: k == 0})]);
  // How far Tolerance reaches along a grey and a hue ramp; which colours of the lattice are within it (a box or a
  // ball of RGB); alpha: a line of shapes.png, both lines of its colour, the transparent background.
  add('wand-ramp-grey-t32', S.RAMP, [S.wand('grey', {antiAlias: false})]);
  add('wand-ramp-lime-t32-noncontig', S.RAMP, [S.wand('lime', {antiAlias: false, contiguous: false})]);
  add('wand-cube-mid-t32', S.CUBE, [S.wand('mid', {antiAlias: false, contiguous: false})]);
  add('wand-cube-rust-t64', S.CUBE, [S.wand('rust', {tolerance: 64, antiAlias: false, contiguous: false})]);
  add('wand-shapes-line', S.SHAPES, [S.wand('line')]);
  add('wand-shapes-line-noncontig', S.SHAPES, [S.wand('line', {contiguous: false})]);
  add('wand-shapes-clear', S.SHAPES, [S.wand('clear')]);
  // Sample All Layers: shapes.png over the photograph, at a pixel shapes.png leaves clear: the composite's colour
  // (the table), or the top layer's own (transparent).
  add('wand-stack-all', [S.PHOTO, S.SHAPES], [S.wand('wood', {sampleAll: true})]);
  add('wand-stack-top', [S.PHOTO, S.SHAPES], [S.wand('woodClear', {layer: 'shapes'})]);
  // Grow and Similar from Magic Wand's selection, at its Tolerance: the photograph's bowl, the grey ramp (how far
  // the range of the colours selected widens).
  add('grow-photo-t16', S.PHOTO, [S.wand('bowl', {tolerance: 16}), o('grow', {tolerance: 16})]);
  add('similar-photo-t16', S.PHOTO, [S.wand('bowl', {tolerance: 16}), o('similar', {tolerance: 16})]);
  add('grow-ramp-t8', S.RAMP, [S.wand('grey', {tolerance: 8, antiAlias: false}), o('grow', {tolerance: 8, antiAlias: false})]);
  add('similar-ramp-t8', S.RAMP,
      [S.wand('grey', {tolerance: 8, antiAlias: false}), o('similar', {tolerance: 8, antiAlias: false})]);

  // Color Range (S34): Sampled Colors, Fuzziness 40 and 100, on the lattice (each colour's share: the fall-off with
  // distance) and the photograph's chilli; the tones at Photoshop's defaults (Fuzziness 20, Range 190, 105-150, 65)
  // on ramp.png (the grey band: the curve; the others: which lightness) and Highlights off them; Highlights and Skin
  // Tones on the lattice; Skin Tones on the photograph (Detect Faces off: no face); Reds.
  add('colorRange-cube-rust-f40', S.CUBE, [S.sampled('rust', 40)]);
  add('colorRange-cube-rust-f100', S.CUBE, [S.sampled('rust', 100)]);
  add('colorRange-photo-chilli-f40', S.PHOTO, [S.sampled('chilli', 40)]);
  add('colorRange-photo-chilli-f100', S.PHOTO, [S.sampled('chilli', 100)]);
  add('colorRange-ramp-highlights', S.RAMP, [o('colorRange', {select: 'highlights', fuzziness: 20, range: [190, 255]})]);
  add('colorRange-ramp-midtones', S.RAMP, [o('colorRange', {select: 'midtones', fuzziness: 20, range: [105, 150]})]);
  add('colorRange-ramp-shadows', S.RAMP, [o('colorRange', {select: 'shadows', fuzziness: 20, range: [0, 65]})]);
  add('colorRange-ramp-highlights-f50-r128', S.RAMP,
      [o('colorRange', {select: 'highlights', fuzziness: 50, range: [128, 255]})]);
  add('colorRange-cube-highlights', S.CUBE, [o('colorRange', {select: 'highlights', fuzziness: 20, range: [190, 255]})]);
  add('colorRange-cube-skin', S.CUBE, [o('colorRange', {select: 'skinTones', fuzziness: 40, detectFaces: false})]);
  add('colorRange-photo-skin', S.PHOTO, [o('colorRange', {select: 'skinTones', fuzziness: 40, detectFaces: false})]);
  add('colorRange-cube-reds', S.CUBE, [o('colorRange', {select: 'reds'})]);

  // Shapes (S31): Lasso's pentagram, anti-aliased and not (the fill rule); ellipses of 180 x 100, 60 x 60, 7 x 5 and
  // 3 x 3 px, added, with and without Anti-alias; rectangles with sides at quarters of a pixel (how Photoshop rounds
  // them) and a whole one.
  add('lasso-star', S.RAMP, [o('lasso', {points: S.STAR})]);
  add('lasso-star-noaa', S.RAMP, [o('lasso', {points: S.STAR, antiAlias: false})]);
  var ellipses = function (aa) {
    return [o('ellipse', {rect: [20, 30, 180, 100], antiAlias: aa}), o('ellipse', {rect: [30, 150, 60, 60], antiAlias: aa, mode: 'add'}),
            o('ellipse', {rect: [210, 150, 7, 5], antiAlias: aa, mode: 'add'}),
            o('ellipse', {rect: [220, 200, 3, 3], antiAlias: aa, mode: 'add'})];
  };
  add('ellipse', S.RAMP, ellipses(true));
  add('ellipse-noaa', S.RAMP, ellipses(false));
  add('rect-fractions', S.RAMP, [o('rect', {rect: [10.25, 10.5, 30.5, 19.75]}),
      o('rect', {rect: [60.5, 10.75, 30, 19.75], mode: 'add'}), o('rect', {rect: [110.75, 10.25, 29.5, 20.5], mode: 'add'}),
      o('rect', {rect: [160, 10, 30, 20], mode: 'add'})]);
  // Add, Subtract, Intersect of two soft selections (S31): ramp-x loaded, then ramp-y with the mode: the image is the
  // combination of every pair of levels (x the first, y the second).
  var modes = ['add', 'subtract', 'intersect'];
  for (i = 0; i < 3; i++) add(modes[i] + '-ramps', S.RAMP_X, [load(S.RAMP_X), load(S.RAMP_Y, modes[i])]);
  return c;
}

if (typeof app != 'undefined') S.run();  // jscheck runs it without Photoshop
