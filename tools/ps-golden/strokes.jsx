// Photoshop's images of its painting and retouching tools (C41), for C30-C38 and H30-H35's tests: Brush (hardness,
// flow, opacity, modes, Wet Edges, Noise, an elliptical tip), Pencil, Eraser (Brush, Pencil, Block), Clone Stamp,
// Healing Brush (Diffusion), Spot Healing Brush (Proximity Match, Create Texture), Dodge and Burn (the three ranges,
// Protect Tones), Sponge (Saturate, Desaturate, Vibrance), Gradient (five styles, five methods) and Paint Bucket;
// README-strokes.md has the steps. Writes out/strokes/ next to this script: <case>.png (the composite, RGB(A) at 16
// bits), index.json (each case as photo-beauty's scene, what it painted as "paint", tests/golden.h reads it),
// photoshop.txt. Uses p1/lib.jsx (B30's PB.run: a case built in a 16-bit sRGB document, then PB.steps.paint here).
// Inputs by their path from this script's folder, as from tests/golden/ps/ in the repo: strokes/in/ (pb-bench
// ps-strokes: white.png, patches.png, tones.png, skin.png, and paths.json, the paths the strokes follow); nothing of
// p1/in/. 59 cases, about 1 MB, about 4 minutes. Not run in Photoshop yet: what it is unsure of is in
// README-strokes.md.
//
// A case: {name, canvas, layers (one pixel layer, an input), paint}; paint {tool, ...}: tool one of C.SPEC's keys, its
// values in C.SPEC's order (C.op: checked, defaults filled in). Each stroke is one path of paths.json (a subpath of
// its own, stroked by PathItem.strokePath with the tool's options set by Action Manager first); a gradient is drawn
// along a line, the paint bucket fills from a point.
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
#include "p1/lib.jsx"

var C = {};

// ---- What a case paints --------------------------------------------------------------------------------------------

// The brush tip of the tools that paint with one, before their own values: [key, default, kind, min, max] as C.SPEC.
C.TIP = [['size', null, 'number', 1, 5000], ['hardness', 1, 'unit'], ['spacing', 0.25, 'number', 0.01, 10],
         ['angle', 0, 'number', -180, 180], ['roundness', 1, 'unit']];
// Each tool's values, in the order index.json has them: [key, default, kind, min, max]; default null: the case must
// give it; undefined: left out unless given. Kinds: 'unit' (0..1), 'number' (min to max), 'int' (whole, min to max),
// all multiples of 1/1000; 'bool'; 'mode' (C.MODES: the options bar's Mode, kBlendNames' names and behind, clear);
// 'healMode' (C.HEAL_MODES); a list (one of its names); 'color' ('#rrggbb'); 'paths' (names in paths.json); 'offset'
// ([dx, dy] whole pixels: where a source is from a stroke's pixel); 'rects' ([[x, y, w, h], ...] whole pixels);
// 'gradient' (D30's JSON); 'line' ([[x0, y0], [x1, y1]]) and 'point' ([x, y]), multiples of 1/8. Keys and units as
// photo-beauty's (BrushParams of core/stroke.h, ToolSpec.js; opacity, flow, exposure 0..1, size px), and spot's
// "areas": where its strokes paint (strokes_in::area() of tests/ps_strokes_inputs.h), for statistics of the retouch.
C.SPEC = {
  brush: [['mode', 'normal', 'mode'], ['opacity', 1, 'unit'], ['flow', 1, 'unit'], ['color', '#000000', 'color'],
          ['wetEdges', false, 'bool'], ['noise', false, 'bool'], ['paths', null, 'paths']],
  pencil: [['mode', 'normal', 'mode'], ['opacity', 1, 'unit'], ['color', '#000000', 'color'], ['paths', null, 'paths']],
  eraser: [['eraser', 'brush', ['brush', 'pencil', 'block']], ['opacity', 1, 'unit'], ['flow', 1, 'unit'],
           ['paths', null, 'paths']],
  clone: [['mode', 'normal', 'mode'], ['opacity', 1, 'unit'], ['flow', 1, 'unit'], ['source', null, 'offset'],
          ['aligned', true, 'bool'], ['paths', null, 'paths'], ['areas', undefined, 'rects']],
  heal: [['mode', 'normal', 'healMode'], ['source', null, 'offset'], ['aligned', true, 'bool'],
         ['diffusion', 5, 'int', 1, 7], ['paths', null, 'paths'], ['areas', undefined, 'rects']],
  spot: [['mode', 'normal', 'healMode'], ['type', 'proximity', ['proximity', 'texture']], ['paths', null, 'paths'],
         ['areas', undefined, 'rects']],
  dodge: [['range', 'midtones', ['shadows', 'midtones', 'highlights']], ['exposure', 0.5, 'unit'],
          ['protectTones', true, 'bool'], ['paths', null, 'paths']],
  burn: [['range', 'midtones', ['shadows', 'midtones', 'highlights']], ['exposure', 0.5, 'unit'],
         ['protectTones', true, 'bool'], ['paths', null, 'paths']],
  sponge: [['sponge', 'desaturate', ['desaturate', 'saturate']], ['flow', 0.5, 'unit'], ['vibrance', true, 'bool'],
           ['paths', null, 'paths']],
  gradient: [['gradient', null, 'gradient'], ['method', 'perceptual', ['perceptual', 'linear', 'classic', 'smooth', 'stripes']],
             ['style', 'linear', ['linear', 'radial', 'angle', 'reflected', 'diamond']], ['mode', 'normal', 'mode'],
             ['opacity', 1, 'unit'], ['reverse', false, 'bool'], ['dither', false, 'bool'], ['transparency', true, 'bool'],
             ['line', null, 'line']],
  bucket: [['color', '#000000', 'color'], ['mode', 'normal', 'mode'], ['opacity', 1, 'unit'], ['tolerance', 32, 'int', 0, 255],
           ['antiAlias', true, 'bool'], ['contiguous', true, 'bool'], ['allLayers', false, 'bool'], ['at', null, 'point']]
};
C.TIPPED = ['brush', 'pencil', 'eraser', 'clone', 'heal', 'spot', 'dodge', 'burn', 'sponge'];  // C.TIP first
C.MODES = ['normal', 'dissolve', 'behind', 'clear', 'darken', 'multiply', 'colorBurn', 'linearBurn', 'darkerColor',
           'lighten', 'screen', 'colorDodge', 'linearDodge', 'lighterColor', 'overlay', 'softLight', 'hardLight',
           'vividLight', 'linearLight', 'pinLight', 'hardMix', 'difference', 'exclusion', 'subtract', 'divide', 'hue',
           'saturation', 'color', 'luminosity'];  // ToolSpec's "paint" list
C.HEAL_MODES = ['normal', 'replace', 'multiply', 'screen', 'darken', 'lighten', 'color', 'luminosity'];

C.multiple = function (v, k) {  // of 1/k, as decimals written as such are (0.1 * 1000 may be off by an ulp)
  return typeof v == 'number' && isFinite(v) && Math.abs(v * k - Math.round(v * k)) < 1e-6;
};
C.isPair = function (v, k) { return v instanceof Array && v.length == 2 && C.multiple(v[0], k) && C.multiple(v[1], k); };
C.valid = function (spec, v) {
  var kind = spec[2], i;
  if (kind instanceof Array) return PB.has(kind, v);
  if (kind == 'unit') return C.multiple(v, 1000) && v >= 0 && v <= 1;
  if (kind == 'number') return C.multiple(v, 1000) && v >= spec[3] && v <= spec[4];
  if (kind == 'int') return C.multiple(v, 1) && v >= spec[3] && v <= spec[4];
  if (kind == 'bool') return typeof v == 'boolean';
  if (kind == 'mode') return PB.has(C.MODES, v);
  if (kind == 'healMode') return PB.has(C.HEAL_MODES, v);
  if (kind == 'color') return typeof v == 'string' && /^#[0-9a-f]{6}$/.test(v);
  if (kind == 'offset') return C.isPair(v, 1);
  if (kind == 'point') return C.isPair(v, 8);
  if (kind == 'line') return v instanceof Array && v.length == 2 && C.isPair(v[0], 8) && C.isPair(v[1], 8);
  if (kind == 'paths' || kind == 'rects') {
    if (!(v instanceof Array) || !v.length) return false;
    for (i = 0; i < v.length; i++) {
      if (kind == 'paths' && !(typeof v[i] == 'string' && /^[a-z0-9-]+$/.test(v[i]))) return false;
      if (kind == 'rects' && !(v[i] instanceof Array && v[i].length == 4 && C.isPair([v[i][0], v[i][1]], 1) &&
                               C.isPair([v[i][2], v[i][3]], 1) && v[i][2] > 0 && v[i][3] > 0)) return false;
    }
    return true;
  }
  if (kind == 'gradient') return C.validGradient(v);
  throw new Error('no kind ' + kind);
};
// D30's gradient JSON as the cases write it: colors [{p, c: [r, g, b] 0..255, m}], alphas [{p, a, m}], smoothness.
C.validGradient = function (g) {
  if (!g || typeof g != 'object' || !(g.colors instanceof Array) || g.colors.length < 2) return false;
  var stops = g.colors.concat(g.alphas || []);
  for (var i = 0; i < stops.length; i++) {
    var s = stops[i], colour = i < g.colors.length;
    if (!C.multiple(s.p, 1000) || s.p < 0 || s.p > 1) return false;
    if (s.m !== undefined && !(C.multiple(s.m, 100) && s.m >= 0.05 && s.m <= 0.95)) return false;
    if (colour && !(s.c instanceof Array && s.c.length == 3)) return false;
    if (!colour && !(C.multiple(s.a, 1000) && s.a >= 0 && s.a <= 1)) return false;
  }
  return g.smoothness === undefined || (C.multiple(g.smoothness, 1) && g.smoothness >= 0 && g.smoothness <= 100);
};
// A case's paint, checked: tool, the tip's values for a tool with one, then the tool's, the defaults of those the case
// leaves out.
C.op = function (tool, given) {
  var spec = C.SPEC[tool], out = {tool: tool}, i, k;
  if (!spec) throw new Error('no tool ' + tool);
  if (PB.has(C.TIPPED, tool)) spec = C.TIP.concat(spec);
  given = given || {};
  for (k in given)
    if (given.hasOwnProperty(k)) {
      var known = false;
      for (i = 0; i < spec.length; i++)
        if (spec[i][0] == k) known = true;
      if (!known) throw new Error(tool + ' has no value ' + k);
    }
  for (i = 0; i < spec.length; i++) {
    var s = spec[i], v = given[s[0]] === undefined ? PB.copy(s[1]) : given[s[0]];
    if (v === undefined) continue;
    if (v === null) throw new Error(tool + ' needs ' + s[0]);
    if (!C.valid(s, v)) throw new Error(tool + ': ' + s[0] + ' ' + PB.json(v));
    out[s[0]] = v;
  }
  return out;
};

// ---- Running them in Photoshop -------------------------------------------------------------------------------------

C.c = function (s) { return charIDToTypeID(s); };
C.s = function (s) { return stringIDToTypeID(s); };
C.id = function (s) { return s.length == 4 ? C.c(s) : C.s(s); };  // 4 letters a char ID, else a string ID

// Each tool: the class Photoshop selects (slct), its ToolType for PathItem.strokePath (none: Action Manager's Stroke,
// Strk, with the class). The Clone Stamp is the Pattern Stamp, its pattern the input shifted (C.pattern), as the
// Healing Brush takes that pattern for its source: a script cannot Alt-click a source point (README-strokes.md).
C.TOOLS = {
  brush: ['paintbrushTool', 'BRUSH'], pencil: ['pencilTool', 'PENCIL'], eraser: ['eraserTool', 'ERASER'],
  clone: ['patternStampTool', 'PATTERNSTAMP'], heal: ['magicStampTool', 'HEALINGBRUSH'],
  spot: ['spotHealingBrushTool', 'SPOTHEALINGBRUSH'], dodge: ['dodgeTool', 'DODGE'], burn: ['burnInTool', 'BURN'],
  sponge: ['saturationTool', 'SPONGE'], gradient: ['gradientTool', null], bucket: ['bucketTool', null]
};
// The modes as Action Manager's BlnM: psd.jsx's (E33), and the painting ones, Behind, Clear, the Healing Brush's
// Replace.
C.BLNM = {normal: 'Nrml', dissolve: 'Dslv', behind: 'Bhnd', clear: 'Clar', darken: 'Drkn', multiply: 'Mltp',
          colorBurn: 'CBrn', linearBurn: 'linearBurn', darkerColor: 'darkerColor', lighten: 'Lghn', screen: 'Scrn',
          colorDodge: 'CDdg', linearDodge: 'linearDodge', lighterColor: 'lighterColor', overlay: 'Ovrl', softLight: 'SftL',
          hardLight: 'HrdL', vividLight: 'vividLight', linearLight: 'linearLight', pinLight: 'pinLight', hardMix: 'hardMix',
          difference: 'Dfrn', exclusion: 'Xclu', subtract: 'blendSubtraction', divide: 'blendDivide', hue: 'H   ',
          saturation: 'Strt', color: 'Clr ', luminosity: 'Lmns', replace: 'Rplc'};
// Tools whose strokes may leave the composite's histograms as they were (alpha alone, grey under the Sponge): noted,
// not failed.
C.MAY_KEEP = {eraser: true, sponge: true, behind: true, clear: true};

C.solid = function (hex) {
  var c = new SolidColor(), v = parseInt(hex.slice(1), 16);
  c.rgb.red = (v >> 16) & 255;
  c.rgb.green = (v >> 8) & 255;
  c.rgb.blue = v & 255;
  return c;
};
C.point = function (p) {
  var d = new ActionDescriptor();
  d.putUnitDouble(C.c('Hrzn'), C.c('#Pxl'), p[0]);
  d.putUnitDouble(C.c('Vrtc'), C.c('#Pxl'), p[1]);
  return d;
};
C.event = function (id, d) {
  PB.note('      AM ' + PB.keyName(id) + ' {' + PB.dump(d) + '}');
  return executeAction(id, d, DialogModes.NO);
};
// The composite's three histograms, to see a stroke changed it.
C.signature = function (doc) {
  var s = [];
  for (var i = 0; i < 3; i++) s.push(doc.channels[i].histogram.join(','));
  return s.join(';');
};

// paths.json (pb-bench ps-strokes) beside the inputs: {"paths": {name: [[x, y] | [x, y, in x, in y, out x, out y]]}}.
C.PATHS = null;
C.path = function (name) {
  if (!C.PATHS) {
    var f = new File(PB.inDir + '/strokes/in/paths.json');
    if (!f.exists) throw new Error('no ' + f.fsName + ' (pb-bench ps-strokes makes it)');
    f.encoding = 'UTF-8';
    f.open('r');
    var text = f.read();
    f.close();
    C.PATHS = eval('(' + text + ')').paths;  // JSON is a literal of ExtendScript's, which has no JSON
  }
  var p = C.PATHS[name];
  if (!(p instanceof Array) || !p.length) throw new Error('no path ' + name + ' in paths.json');
  return p;
};
// A path of one subpath, its points as paths.json has them: corner points, handles where it gives them.
C.makePath = function (doc, name) {
  var pts = C.path(name), infos = [];
  for (var i = 0; i < pts.length; i++) {
    var q = pts[i], p = new PathPointInfo(), h = q.length == 6;
    p.kind = PointKind.CORNERPOINT;
    p.anchor = [q[0], q[1]];
    p.leftDirection = h ? [q[2], q[3]] : [q[0], q[1]];
    p.rightDirection = h ? [q[4], q[5]] : [q[0], q[1]];
    infos.push(p);
  }
  var s = new SubPathInfo();
  s.closed = false;
  s.operation = ShapeOperation.SHAPEADD;
  s.entireSubPath = infos;
  return doc.pathItems.add('c41 ' + name, [s]);
};
// Stroke Path with the tool's options as they are: PathItem.strokePath, or Strk where ToolType lacks the tool.
C.stroke = function (item, tool) {
  var t = C.TOOLS[tool], type = t[1] ? ToolType[t[1]] : undefined;
  if (type !== undefined && type !== null) {
    item.strokePath(type, false);
    return 'strokePath(ToolType.' + t[1] + ')';
  }
  var d = new ActionDescriptor(), r = new ActionReference();
  r.putName(C.c('Path'), item.name);
  d.putReference(C.c('null'), r);
  d.putClass(C.c('Usng'), C.s(t[0]));
  C.event(C.c('Strk'), d);
  return 'Strk using ' + t[0];
};

// The current tool's options (CrnT), B4's way to reset and set them.
C.optionsRef = function () {
  var r = new ActionReference();
  r.putProperty(C.c('Prpr'), C.c('CrnT'));
  r.putEnumerated(C.c('capp'), C.c('Ordn'), C.c('Trgt'));
  return r;
};
C.options = function () { return executeActionGet(C.optionsRef()).getObjectValue(C.c('CrnT')); };
C.select = function (cls) {
  var d = new ActionDescriptor(), r = new ActionReference();
  r.putClass(C.s(cls));
  d.putReference(C.c('null'), r);
  executeAction(C.c('slct'), d, DialogModes.NO);
};
C.setOptions = function (cls, opts) {
  var d = new ActionDescriptor(), r = new ActionReference();
  r.putClass(C.s(cls));
  d.putReference(C.c('null'), r);
  d.putObject(C.c('T   '), C.c('null'), opts);
  executeAction(C.c('setd'), d, DialogModes.NO);
};
C.keys = function (d) {  // a descriptor's keys, for a failure's message
  var s = [];
  for (var i = 0; i < d.count; i++) s.push(PB.keyName(d.getKey(i)));
  return s.join(', ');
};
C.dumped = {};  // the tools whose options photoshop.txt has whole
// The tool selected, its options reset (Reset Tool), what B4 turns off off (dynamics, Noise, Wet Edges, Airbrush).
C.fresh = function (cls) {
  C.select(cls);
  try {
    var d = new ActionDescriptor();
    d.putReference(C.c('null'), C.optionsRef());
    executeAction(C.c('Rset'), d, DialogModes.NO);
  } catch (e) {
    PB.note('    ' + cls + ': no Reset Tool (' + e + ')');
  }
  var opts = C.options(), off = [];
  if (!C.dumped[cls]) PB.note('    ' + cls + ' as reset: ' + PB.dump(opts));
  C.dumped[cls] = true;
  var plain = new RegExp('^(use(TipDynamics|Scatter|Texture|DualBrush|ColorDynamics|PaintDynamics|BrushPose)|' +
                         'dualBrush|noise|wetEdges|repeat)$');
  for (var i = 0; i < opts.count; i++) {
    var k = opts.getKey(i);
    if (opts.getType(k) == DescValueType.BOOLEANTYPE && opts.getBoolean(k) && plain.test(PB.keyName(k))) {
      opts.putBoolean(k, false);
      off.push(PB.keyName(k));
    }
  }
  if (off.length) {
    C.setOptions(cls, opts);
    PB.note('    ' + cls + ': turned off ' + off.join(', '));
  }
};
// The tip (B4's computedBrush), read back from the options' brush.
C.tip = function (o) {
  var b = new ActionDescriptor(), d = new ActionDescriptor(), r = new ActionReference();
  b.putUnitDouble(C.c('Dmtr'), C.c('#Pxl'), o.size);
  b.putUnitDouble(C.c('Hrdn'), C.c('#Prc'), o.hardness * 100);
  b.putUnitDouble(C.c('Angl'), C.c('#Ang'), o.angle);
  b.putUnitDouble(C.c('Rndn'), C.c('#Prc'), o.roundness * 100);
  b.putUnitDouble(C.c('Spcn'), C.c('#Prc'), o.spacing * 100);
  b.putBoolean(C.c('Intr'), true);
  b.putBoolean(C.s('flipX'), false);
  b.putBoolean(C.s('flipY'), false);
  r.putEnumerated(C.c('Brsh'), C.c('Ordn'), C.c('Trgt'));
  d.putReference(C.c('null'), r);
  d.putObject(C.c('T   '), C.s('computedBrush'), b);
  executeAction(C.c('setd'), d, DialogModes.NO);
  var opts = C.options(), k = C.s('brush');
  if (!opts.hasKey(k) || opts.getType(k) != DescValueType.OBJECTTYPE) {
    PB.note('    no brush among the options to read the tip back from (' + C.keys(opts) + ')');
    return;
  }
  var got = opts.getObjectValue(k), want = [['Dmtr', o.size], ['Angl', o.angle], ['Rndn', o.roundness * 100],
                                            ['Spcn', o.spacing * 100]], t;
  if (o.tool != 'pencil') want.push(['Hrdn', o.hardness * 100]);  // the pencil's has none that counts
  for (var i = 0; i < want.length; i++) {
    var key = C.c(want[i][0]);
    if (!got.hasKey(key)) continue;
    t = got.getType(key);
    var v = t == DescValueType.UNITDOUBLE ? got.getUnitDoubleValue(key) : t == DescValueType.INTEGERTYPE ? got.getInteger(key)
                                          : got.getDouble(key);
    if (Math.abs(v - want[i][1]) > 0.5) throw new Error('the tip\'s ' + want[i][0] + ' came back ' + v + ', not ' + want[i][1]);
  }
};

// The options a case sets, found among the tool's by name (versions name them as they please: photoshop.txt has them
// all): name, a pattern of the key's name, or for a list its enumeration type's, or of its value now; key, the one to
// add when there is none; kind; values, Photoshop's enumerated values for each of the case's (tried in turn, the first
// that reads back wins).
C.OPTIONS = {
  opacity: {name: /^opacity$/, key: 'opacity', kind: 'percent'},
  flow: {name: /^flow$/, key: 'flow', kind: 'percent'},
  exposure: {name: /expos/i, key: 'exposure', kind: 'percent'},
  diffusion: {name: /diffus/i, key: 'diffusion', kind: 'int'},
  wetEdges: {name: /^wetEdges$/i, key: 'wetEdges', kind: 'bool'},
  noise: {name: /^noise$/i, key: 'noise', kind: 'bool'},
  protectTones: {name: /protect/i, key: 'protectTones', kind: 'bool'},
  vibrance: {name: /vibran/i, key: 'vibrance', kind: 'bool'},
  aligned: {name: /^(aligned?|Algn)$/i, key: 'Algn', kind: 'bool'},
  impressionist: {name: /impress/i, key: 'impressionist', kind: 'bool'},
  mode: {name: /^mode$/, kind: 'enum', values: C.BLNM},
  range: {name: /range/i, kind: 'enum',
          values: {shadows: ['Shdw', 'shadows'], midtones: ['Mdtn', 'midtones'], highlights: ['Hghl', 'highlights']}},
  sponge: {name: /^mode$|satur/i, kind: 'enum',
           values: {desaturate: ['desaturate', 'Dstt', 'Dsat'], saturate: ['saturate', 'Strt', 'Satr']}},
  eraser: {name: /^mode$|eras/i, kind: 'enum',
           values: {brush: ['Brsh', 'paintbrushEraser', 'brush'], pencil: ['Pncl', 'pencilEraser', 'pencil'],
                    block: ['Blk ', 'blockEraser', 'block']}},
  type: {name: /type|kind|method/i, kind: 'enum',
         values: {proximity: ['proximityMatch', 'Prxm'], texture: ['createTexture', 'CrtT'],
                  contentAware: ['contentAware', 'CntA']}},
  source: {name: /source|sampl/i, kind: 'enum', values: {sampled: ['Smpl', 'sampled'], pattern: ['Ptrn', 'pattern']}}
};
C.candidates = function (o, value) {  // Photoshop's values for one of the case's
  var v = o.values[value];
  return v instanceof Array ? v : [v];
};
C.anyCandidate = function (o, name) {
  for (var k in o.values)
    if (o.values.hasOwnProperty(k)) {
      var c = C.candidates(o, k);
      for (var i = 0; i < c.length; i++)
        if (PB.keyName(C.id(c[i])) == name) return true;
    }
  return false;
};
C.findKey = function (opts, option) {
  var o = C.OPTIONS[option];
  var want = o.kind == 'bool' ? ['BOOLEANTYPE'] : o.kind == 'enum' ? ['ENUMERATEDTYPE'] : ['INTEGERTYPE', 'UNITDOUBLE', 'DOUBLETYPE'];
  // A list's: its name fits and its value now is a candidate, then either; another kind's: its name fits.
  for (var pass = o.kind == 'enum' ? 0 : 2; pass < 3; pass++)
    for (var i = 0; i < opts.count; i++) {
      var k = opts.getKey(i), t = String(opts.getType(k)).replace(/^DescValueType\./, '');
      if (!PB.has(want, t)) continue;
      var named = o.name.test(PB.keyName(k)) || (o.kind == 'enum' && o.name.test(PB.keyName(opts.getEnumerationType(k))));
      var now = o.kind == 'enum' && C.anyCandidate(o, PB.keyName(opts.getEnumerationValue(k)));
      if (pass == 0 ? named && now : pass == 1 ? now : named) return k;
    }
  return null;
};
C.put = function (opts, key, o, value, candidate) {
  if (o.kind == 'bool') return opts.putBoolean(key, value);
  if (o.kind == 'enum') return opts.putEnumerated(key, opts.getEnumerationType(key), C.id(candidate));
  var v = o.kind == 'percent' ? value * 100 : value, t = opts.hasKey(key) ? opts.getType(key) : DescValueType.INTEGERTYPE;
  if (t == DescValueType.UNITDOUBLE) opts.putUnitDouble(key, opts.getUnitDoubleType(key), v);
  else if (t == DescValueType.DOUBLETYPE) opts.putDouble(key, v);
  else opts.putInteger(key, Math.round(v));
};
C.got = function (opts, key, o) {  // the option as the case writes it; an enumerated value's name
  if (!opts.hasKey(key)) return undefined;
  var t = opts.getType(key);
  if (o.kind == 'bool') return opts.getBoolean(key);
  if (o.kind == 'enum') return PB.keyName(opts.getEnumerationValue(key));
  var v = t == DescValueType.UNITDOUBLE ? opts.getUnitDoubleValue(key) : t == DescValueType.DOUBLETYPE ? opts.getDouble(key)
                                        : opts.getInteger(key);
  return o.kind == 'percent' ? v / 100 : v;
};
C.same = function (o, got, value, candidate) {
  if (o.kind == 'enum') return got == PB.keyName(C.id(candidate));
  if (o.kind == 'bool') return got === value;
  return typeof got == 'number' && Math.abs(got - value) <= (o.kind == 'percent' ? 0.005 : 0.5);
};
// One option of the tool set, and read back: Photoshop's key and value, for photoshop.txt.
C.option = function (cls, option, value) {
  var o = C.OPTIONS[option], opts = C.options(), key = C.findKey(opts, option);
  if (key === null && o.key) key = C.id(o.key);  // not there: added
  if (key === null) throw new Error(cls + ': no option for ' + option + ' among ' + C.keys(opts));
  var tries = o.kind == 'enum' ? C.candidates(o, value) : [null], errors = [];
  for (var i = 0; i < tries.length; i++) {
    try {
      opts = C.options();
      C.put(opts, key, o, value, tries[i]);
      C.setOptions(cls, opts);
      var now = C.options(), got = C.got(now, key, o);
      if (C.same(o, got, value, tries[i])) return PB.keyName(key) + '=' + PB.dumpValue(now, key, now.getType(key));
      errors.push((tries[i] === null ? '' : tries[i] + ': ') + 'came back ' + got);
    } catch (e) {
      errors.push((tries[i] === null ? '' : tries[i] + ': ') + e);
    }
  }
  throw new Error(cls + ': ' + option + ' ' + value + ' as ' + PB.keyName(key) + ' did not take (' + errors.join('; ') + ')');
};
// The pattern of the tool's options (the Pattern Stamp's, the Healing Brush's Source Pattern): the one named.
C.patternOption = function (cls, p) {
  var opts = C.options(), key = null, d = new ActionDescriptor();
  for (var i = 0; i < opts.count && key === null; i++)
    if (opts.getType(opts.getKey(i)) == DescValueType.OBJECTTYPE && /^(pattern|Ptrn)$/i.test(PB.keyName(opts.getKey(i))))
      key = opts.getKey(i);
  if (key === null) key = C.c('Ptrn');
  d.putString(C.c('Nm  '), p.name);
  if (p.id) d.putString(C.c('Idnt'), p.id);
  opts.putObject(key, C.c('Ptrn'), d);
  C.setOptions(cls, opts);
  opts = C.options();
  var got = opts.hasKey(key) ? opts.getObjectValue(key) : null;
  if (!got || !got.hasKey(C.c('Nm  ')) || got.getString(C.c('Nm  ')) != p.name)
    throw new Error(cls + ': its pattern came back ' + (got ? PB.dump(got) : 'none') + ', not ' + p.name);
  return PB.keyName(key) + '=' + p.name;
};

// The values of each tool the case sets as options, in order.
C.WANTS = {
  brush: ['mode', 'opacity', 'flow', 'wetEdges', 'noise'], pencil: ['mode', 'opacity'], eraser: ['eraser', 'opacity', 'flow'],
  clone: ['mode', 'opacity', 'flow', 'aligned'], heal: ['mode', 'aligned', 'diffusion'], spot: ['mode', 'type'],
  dodge: ['range', 'exposure', 'protectTones'], burn: ['range', 'exposure', 'protectTones'],
  sponge: ['sponge', 'flow', 'vibrance']
};
C.setTool = function (o) {
  var cls = C.TOOLS[o.tool][0], set = [], i;
  C.fresh(cls);
  C.tip(o);
  if (o.tool == 'clone') set.push(C.option(cls, 'impressionist', false));
  if (o.tool == 'heal') set.push(C.option(cls, 'source', 'pattern'));
  for (i = 0; i < C.WANTS[o.tool].length; i++) set.push(C.option(cls, C.WANTS[o.tool][i], o[C.WANTS[o.tool][i]]));
  if (o.pattern) set.push(C.patternOption(cls, o.pattern));
  PB.note('    ' + cls + ': ' + set.join(', '));
};

// The input of the case's layer shifted by -source (wrapped round: Filter > Other > Offset), defined as a pattern: as
// the Pattern Stamp and the Healing Brush tile it from the document's top left (Aligned), what they lay at (x, y) is
// the input's (x, y) + source, as a Clone Stamp source point that far would give where it stays inside the canvas.
// Deleted after the case (C.dropPattern): patterns are presets, kept past the run.
C.pattern = function (src, offset) {
  var input = PB.input(src, 0), keep = app.activeDocument, name = 'c41 ' + src + ' ' + offset.join(' ');
  app.activeDocument = input;
  var copy = input.duplicate(name), p = {name: name};
  try {
    app.activeDocument = copy;
    copy.layers[0].applyOffset(-offset[0], -offset[1], OffsetUndefinedAreas.WRAPAROUND);
    copy.selection.selectAll();
    var d = new ActionDescriptor(), r = new ActionReference(), u = new ActionReference();
    r.putClass(C.c('Ptrn'));
    d.putReference(C.c('null'), r);
    u.putProperty(C.c('Prpr'), C.c('fsel'));
    u.putEnumerated(C.c('Dcmn'), C.c('Ordn'), C.c('Trgt'));
    d.putReference(C.c('Usng'), u);
    d.putString(C.c('Nm  '), name);
    var made = C.event(C.c('Mk  '), d);
    if (made && made.hasKey(C.c('Idnt'))) p.id = made.getString(C.c('Idnt'));
  } finally {
    PB.close(copy);
    app.activeDocument = keep;
  }
  return p;
};
C.dropPattern = function (p) {
  try {
    var d = new ActionDescriptor(), r = new ActionReference();
    r.putName(C.c('Ptrn'), p.name);
    d.putReference(C.c('null'), r);
    executeAction(C.c('Dlt '), d, DialogModes.NO);
  } catch (e) {
    PB.note('    pattern ' + p.name + ' not deleted (' + e + '): delete it in Window > Patterns');
  }
};

// The classic Gradient tool's drag (Grdn): from line[0] to line[1], the gradient as psd.jsx's E.gradient makes it,
// Method as its E.putMethod.
C.METHOD = {perceptual: 'Perc', linear: 'Lnr ', classic: 'Gcls', smooth: 'Smoo', stripes: 'Stri'};
C.STYLE = {linear: 'Lnr ', radial: 'Rdl ', angle: 'Angl', reflected: 'Rflc', diamond: 'Dmnd'};
C.gradientDesc = function (g) {
  var c = C.c, d = new ActionDescriptor(), list = new ActionList(), i, o, s;
  d.putString(c('Nm  '), 'Custom');
  d.putEnumerated(c('GrdF'), c('GrdF'), c('CstS'));
  d.putDouble(c('Intr'), (g.smoothness === undefined ? 100 : g.smoothness) * 40.96);
  for (i = 0; i < g.colors.length; i++) {
    var col = new ActionDescriptor();
    s = g.colors[i];
    col.putDouble(c('Rd  '), s.c[0]);
    col.putDouble(c('Grn '), s.c[1]);
    col.putDouble(c('Bl  '), s.c[2]);
    o = new ActionDescriptor();
    o.putObject(c('Clr '), c('RGBC'), col);
    o.putEnumerated(c('Type'), c('Clry'), c('UsrS'));
    o.putInteger(c('Lctn'), Math.round(s.p * 4096));
    o.putInteger(c('Mdpn'), Math.round((s.m === undefined ? 0.5 : s.m) * 100));
    list.putObject(c('Clrt'), o);
  }
  d.putList(c('Clrs'), list);
  var alphas = g.alphas || [{p: 0, a: 1}, {p: 1, a: 1}];
  list = new ActionList();
  for (i = 0; i < alphas.length; i++) {
    o = new ActionDescriptor();
    o.putUnitDouble(c('Opct'), c('#Prc'), alphas[i].a * 100);
    o.putInteger(c('Lctn'), Math.round(alphas[i].p * 4096));
    o.putInteger(c('Mdpn'), Math.round((alphas[i].m === undefined ? 0.5 : alphas[i].m) * 100));
    list.putObject(c('TrnS'), o);
  }
  d.putList(c('Trns'), list);
  return d;
};
C.gradient = function (doc, o) {
  var layers = doc.layers.length, errors = [];
  var ways = [function (d) {  // Method by its char ID (psd.jsx's), then by a string ID
    d.putEnumerated(C.s('gradientsInterpolationMethod'), C.s('gradientInterpolationMethodType'), C.c(C.METHOD[o.method]));
  }, function (d) {
    d.putEnumerated(C.s('gradientsInterpolationMethod'), C.s('gradientInterpolationMethodType'), C.s(o.method));
  }];
  for (var i = 0; i < ways.length; i++) {
    var d = new ActionDescriptor();
    d.putObject(C.c('From'), C.c('Pnt '), C.point(o.line[0]));
    d.putObject(C.c('T   '), C.c('Pnt '), C.point(o.line[1]));
    d.putEnumerated(C.c('Type'), C.c('GrdT'), C.c(C.STYLE[o.style]));
    d.putBoolean(C.c('Dthr'), o.dither);
    d.putBoolean(C.c('UsMs'), o.transparency);
    d.putBoolean(C.c('Rvrs'), o.reverse);
    d.putEnumerated(C.c('Md  '), C.c('BlnM'), C.id(C.BLNM[o.mode]));
    d.putUnitDouble(C.c('Opct'), C.c('#Prc'), o.opacity * 100);
    d.putObject(C.c('Grad'), C.c('Grdn'), C.gradientDesc(o.gradient));
    ways[i](d);
    try {
      C.event(C.c('Grdn'), d);
      break;
    } catch (e) {
      errors.push(e);
    }
  }
  if (i == ways.length) throw new Error('the gradient was not drawn (' + errors.join('; ') + ')');
  if (errors.length) PB.note('    gradient: Method by its string ID (' + errors.join('; ') + ')');
  if (doc.layers.length != layers) throw new Error('Grdn made a layer: the gradient tool is not drawing as Classic');
};
// The Paint Bucket's click (Fl  with From), the foreground colour.
C.bucket = function (doc, o) {
  var d = new ActionDescriptor();
  d.putObject(C.c('From'), C.c('Pnt '), C.point(o.at));
  d.putInteger(C.c('Tlrn'), o.tolerance);
  d.putBoolean(C.c('AntA'), o.antiAlias);
  d.putBoolean(C.c('Cntg'), o.contiguous);
  if (o.allLayers) d.putBoolean(C.c('Mrgd'), true);
  d.putEnumerated(C.c('Usng'), C.c('FlCn'), C.c('FrgC'));
  d.putEnumerated(C.c('Md  '), C.c('BlnM'), C.id(C.BLNM[o.mode]));
  d.putUnitDouble(C.c('Opct'), C.c('#Prc'), o.opacity * 100);
  C.event(C.c('Fl  '), d);
};
C.actualPixels = function () {  // View > 100%: the Block eraser is 16 screen pixels
  var d = new ActionDescriptor(), r = new ActionReference();
  r.putEnumerated(C.c('Mn  '), C.c('MnIt'), C.c('ActP'));
  d.putReference(C.c('null'), r);
  executeAction(C.c('slct'), d, DialogModes.NO);
};

// PB.steps.paint: a case's paint on its document, after its layers (lib.jsx's PB.build): the top layer's.
C.paint = function (o, doc, built) {
  var top = built[built.length - 1], layer = top.layer, pattern = null, want = o;
  doc.activeLayer = layer;
  app.foregroundColor = C.solid(o.color || '#000000');
  PB.note('    ' + PB.json(o));
  if (o.tool == 'gradient') return C.gradient(doc, o);
  if (o.tool == 'bucket') return C.bucket(doc, o);
  if (o.source) {
    pattern = C.pattern(top.json.src, o.source);
    want = PB.copy(o);
    want.pattern = pattern;
  }
  try {
    C.setTool(want);
    if (o.tool == 'eraser' && o.eraser == 'block') C.actualPixels();
    for (var i = 0; i < o.paths.length; i++) {
      var before = C.signature(doc), item = C.makePath(doc, o.paths[i]), how;
      doc.activeLayer = layer;
      try {
        how = C.stroke(item, o.tool);
      } finally {
        item.remove();
      }
      var kept = C.signature(doc) == before;
      PB.note('      ' + o.paths[i] + ': ' + how + (kept ? ', the histograms as they were' : ''));
      if (kept && !C.MAY_KEEP[o.tool] && !C.MAY_KEEP[o.mode]) throw new Error(o.paths[i] + ': the stroke changed nothing');
    }
  } finally {
    if (pattern) C.dropPattern(pattern);
  }
};
PB.steps.paint = C.paint;

// Every case, in Photoshop: lib.jsx's PB.run.
C.run = function () { PB.run('strokes', cases(), C.runOptions()); };
C.runOptions = function () {  // the inputs from this folder (strokes/in/): from tests/golden/ps/ in the repo
  return {bits: 16, dir: HERE, inputs: '.', repoInputs: '..'};
};

// ---- The cases -----------------------------------------------------------------------------------------------------

// Inputs by their path from tests/golden/ps/ (from this script's folder in Photoshop), and their sizes.
C.WHITE = 'strokes/in/white.png';
C.PATCHES = 'strokes/in/patches.png';
C.TONES = 'strokes/in/tones.png';
C.SKIN = 'strokes/in/skin.png';
C.SIZES = {'strokes/in/white.png': [256, 160], 'strokes/in/patches.png': [256, 160], 'strokes/in/tones.png': [256, 128],
           'strokes/in/skin.png': [128, 128]};
// Paths of paths.json (tests/ps_strokes_inputs.h, strokes_in::paths(), says where each goes): on white.png the
// brush's six, on patches.png one across each row of cells, on tones.png one along each band, on skin.png one over
// each spot.
C.BASIC = ['line', 'cross', 'dot', 'diagonal', 'curve', 'loop'];
C.ROWS = ['h16', 'h48', 'h80', 'h112', 'h144'];
C.BANDS = ['band0', 'band1', 'band2', 'band3', 'band4', 'band5', 'band6', 'band7'];
C.SPOTS = ['spot-a', 'spot-b', 'spot-c', 'spot-d'];
C.AREAS = [[9, 13, 22, 22], [17, 49, 38, 22], [7, 87, 22, 22], [29, 93, 22, 22]];  // strokes_in::area() of C.SPOTS, 20 px
C.SOURCE = [64, 0];  // the clean skin right of the spots
// Gradients (D30's JSON): three colours; one fading out.
C.GRADIENT = {colors: [{p: 0, c: [233, 199, 179], m: 0.5}, {p: 0.6, c: [158, 47, 69], m: 0.5}, {p: 1, c: [24, 52, 120], m: 0.5}],
              alphas: [{p: 0, a: 1, m: 0.5}, {p: 1, a: 1, m: 0.5}], smoothness: 100};
C.FADE = {colors: [{p: 0, c: [158, 47, 69], m: 0.5}, {p: 1, c: [24, 52, 120], m: 0.5}],
          alphas: [{p: 0, a: 1, m: 0.5}, {p: 1, a: 0, m: 0.5}], smoothness: 100};
C.ACROSS = [[32, 80], [224, 80]];   // a linear gradient's line, left to right
C.SLANT = [[128, 80], [208, 112]];  // the other styles': from the middle, slanting down

C.merge = function (a, b) {  // a copy of a with b's values
  var out = PB.copy(a);
  for (var k in b)
    if (b.hasOwnProperty(k)) out[k] = PB.copy(b[k]);
  return out;
};
// A case: one pixel layer of an input, the paint on it.
C.one = function (name, src, paint) {
  return {name: name, canvas: C.SIZES[src], paint: paint,
          layers: [{kind: 'pixel', src: src, props: {name: src.replace(/^.*\//, '').replace(/\.png$/, '')}}]};
};

function cases() {
  var c = [], i, j, k;
  var add = function (name, src, tool, vals) { c.push(C.one(name, src, C.op(tool, vals))); };

  // Brush (C30-C35, P0's C1-C3): black on white, 24 px, along C.BASIC: hardness 0, 50, 100% (the dab's fall-off);
  // Flow 30% (each dab's share), Opacity 50% (the cap of a stroke: the loop crossing itself stays at it, the cross
  // over the line does not), both.
  var brush = {size: 24, paths: C.BASIC};
  add('brush-h0', C.WHITE, 'brush', C.merge(brush, {hardness: 0}));
  add('brush-h50', C.WHITE, 'brush', C.merge(brush, {hardness: 0.5}));
  add('brush-h100', C.WHITE, 'brush', C.merge(brush, {hardness: 1}));
  add('brush-h50-flow30', C.WHITE, 'brush', C.merge(brush, {hardness: 0.5, flow: 0.3}));
  add('brush-h50-o50', C.WHITE, 'brush', C.merge(brush, {hardness: 0.5, opacity: 0.5}));
  add('brush-h0-flow30-o50', C.WHITE, 'brush', C.merge(brush, {hardness: 0, flow: 0.3, opacity: 0.5}));
  // Modes (C33): blue across each row of patches.png (opaque cells, alpha x / 255, transparent): Normal, Multiply,
  // Screen, Behind (only where it is transparent), Clear (as the eraser).
  var rows = {size: 24, hardness: 0.5, color: '#3399cc', paths: C.ROWS}, modes = ['normal', 'multiply', 'screen', 'behind', 'clear'];
  for (i = 0; i < modes.length; i++) add('brush-' + modes[i], C.PATCHES, 'brush', C.merge(rows, {mode: modes[i]}));
  // Wet Edges at hardness 100 and 0%, Noise at 0% (C31); an elliptical tip, 40% round, at 30 degrees (C30).
  var wet = {size: 24, color: '#3399cc', wetEdges: true, paths: ['line', 'dot', 'curve', 'loop']};
  add('brush-wet-h100', C.WHITE, 'brush', C.merge(wet, {hardness: 1}));
  add('brush-wet-h0', C.WHITE, 'brush', C.merge(wet, {hardness: 0}));
  add('brush-noise-h0', C.WHITE, 'brush', {size: 24, hardness: 0, noise: true, paths: ['line', 'dot', 'curve']});
  add('brush-ellipse', C.WHITE, 'brush',
      {size: 32, hardness: 0.5, angle: 30, roundness: 0.4, paths: ['line', 'dot', 'diagonal', 'curve']});

  // Pencil (C30): 1 px (a shallow line, a diagonal, a curve: 8-connected, never two thick), 9 px (pixel centres in).
  add('pencil-1', C.WHITE, 'pencil', {size: 1, paths: ['shallow', 'diagonal', 'curve', 'dot']});
  add('pencil-9', C.WHITE, 'pencil', {size: 9, paths: ['line', 'dot', 'curve', 'loop']});

  // Eraser (C33, P0's erasing): Brush (hardness 50%, at 100 and 50%), Pencil, Block (16 px at 100%) across each row
  // of patches.png, a layer, not a Background: what is left of alpha.
  add('eraser-brush', C.PATCHES, 'eraser', {size: 24, hardness: 0.5, paths: C.ROWS});
  add('eraser-brush-o50', C.PATCHES, 'eraser', {size: 24, hardness: 0.5, opacity: 0.5, paths: C.ROWS});
  add('eraser-pencil', C.PATCHES, 'eraser', {size: 9, eraser: 'pencil', paths: C.ROWS});
  add('eraser-block', C.PATCHES, 'eraser', {size: 24, eraser: 'block', paths: C.ROWS});

  // Clone Stamp (H30): skin from 64 px right over each spot of skin.png, hard and soft; Healing Brush (H31, H32) the
  // same at Diffusion 1, 5, 7; Spot Healing Brush (H33), Proximity Match and Create Texture. "areas": where they
  // paint, for statistics (their algorithms are Adobe's own: README-strokes.md).
  var spots = {size: 20, paths: C.SPOTS, areas: C.AREAS};
  add('clone-h100', C.SKIN, 'clone', C.merge(spots, {source: C.SOURCE}));
  add('clone-h0', C.SKIN, 'clone', C.merge(spots, {hardness: 0, source: C.SOURCE}));
  var diffusion = [1, 5, 7];
  for (i = 0; i < 3; i++) add('heal-d' + diffusion[i], C.SKIN, 'heal', C.merge(spots, {source: C.SOURCE, diffusion: diffusion[i]}));
  add('spot-proximity', C.SKIN, 'spot', C.merge(spots, {type: 'proximity'}));
  add('spot-texture', C.SKIN, 'spot', C.merge(spots, {type: 'texture'}));

  // Dodge, Burn (H35): each range with Protect Tones on and off, Exposure 50%, a hard 14 px brush along each band of
  // tones.png (every level of grey, of each primary, of hues at four saturations and values); Sponge, each mode with
  // Vibrance on and off, Flow 50%.
  var tools = ['dodge', 'burn'], ranges = ['shadows', 'midtones', 'highlights'], band = {size: 14, hardness: 1, paths: C.BANDS};
  for (i = 0; i < 2; i++)
    for (j = 0; j < 3; j++)
      for (k = 0; k < 2; k++)
        add(tools[i] + '-' + ranges[j] + (k ? '-noprotect' : ''), C.TONES, tools[i],
            C.merge(band, {range: ranges[j], protectTones: k == 0}));
  var sponge = ['desaturate', 'saturate'];
  for (i = 0; i < 2; i++)
    for (k = 0; k < 2; k++)
      add('sponge-' + sponge[i] + (k ? '-vibrance' : ''), C.TONES, 'sponge', C.merge(band, {sponge: sponge[i], vibrance: k == 1}));

  // Gradient (C36): the five styles at Perceptual, the other methods linear; one fading to transparent over white,
  // one in Multiply over patches.png, one reversed at 50%. Dither off: Photoshop's noise is its own.
  var styles = ['linear', 'radial', 'angle', 'reflected', 'diamond'], methods = ['linear', 'classic', 'smooth', 'stripes'];
  for (i = 0; i < styles.length; i++)
    add('gradient-' + styles[i], C.WHITE, 'gradient', {gradient: C.GRADIENT, style: styles[i], line: i ? C.SLANT : C.ACROSS});
  for (i = 0; i < methods.length; i++)
    add('gradient-linear-' + methods[i], C.WHITE, 'gradient', {gradient: C.GRADIENT, method: methods[i], line: C.ACROSS});
  add('gradient-linear-transparent', C.WHITE, 'gradient', {gradient: C.FADE, line: C.ACROSS});
  add('gradient-linear-multiply', C.PATCHES, 'gradient', {gradient: C.GRADIENT, mode: 'multiply', line: C.ACROSS});
  add('gradient-radial-reverse-o50', C.WHITE, 'gradient',
      {gradient: C.GRADIENT, style: 'radial', reverse: true, opacity: 0.5, line: C.SLANT});

  // Paint Bucket (C38): Tolerance 32 from the middle of the grey band of tones.png (how far along it, into which
  // bands), Contiguous off, Anti-alias off.
  var bucket = {color: '#00a0ff', at: [128, 8]};
  add('bucket-t32', C.TONES, 'bucket', bucket);
  add('bucket-t32-noncontig', C.TONES, 'bucket', C.merge(bucket, {contiguous: false}));
  add('bucket-t32-noaa', C.TONES, 'bucket', C.merge(bucket, {antiAlias: false}));
  return c;
}

if (typeof app != 'undefined') C.run();  // jscheck runs it without Photoshop
