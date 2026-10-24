// What tools/ps-golden/p1/*.jsx share (B30): a script #includes this, lists its cases and calls PB.run(); the README
// next to the scripts' folder has the steps. ExtendScript (ECMAScript 3: no JSON, no Array indexOf, no let), Photoshop
// 2025 (26) on Windows; CC 2019 (20) on should run it too. Not run in Photoshop yet: what it is unsure of is in
// README.md, section P1.
//
// A case is a scene in photo-beauty's terms, written into index.json as it is (tests/golden.h reads it):
//   {name: 'multiply-50', canvas: [256, 384], bits: 16, layers: [  // bottom first, as DocState's children
//     {kind: 'pixel', src: 'base.png', props: {name: 'base'}},
//     {kind: 'pixel', src: 'top.png', props: {name: 'top', blend: 'multiply', opacity: 0.5}}]}
// Layers: kind 'pixel' (src: an input of in/, rotate: its turn clockwise in degrees), 'group' (children, bottom
// first), 'adj' (props.adj an AdjustSpec.ORDER name, props.vals its values: PB.adjustments makes it); mask: an input
// whose alpha is the layer mask. props are LayerProps' JSON as A30's to_json() writes it, in its order: name, blend
// (kBlendNames), opacity, fill, maskDensity (0..1), maskFeather (px), visible, clip, maskOff, maskLinked, blendIf
// ({gray, red, green, blue}: {layer, under: [black low, black high, white low, white high], 0..255}: the channels off
// 0, 0, 255, 255 only), channels ({r, g, b}: the boxes off only, false), adj, vals; version 1 is added.
// Photoshop builds it in a new 16-bit sRGB document of the canvas, and saves <name>.png at `bits` (8: the 16-bit
// composite rounded, no dither) into out/<script>/, with index.json (the cases done) and photoshop.txt (what ran).
// A later script adds what this does not know without changing it: PB.adjustments[name] (an adjustment's
// descriptor), PB.kinds[kind] (makes a layer of another kind), PB.layerKeys[key] (a prop or layer key, as fx for
// layer styles), PB.steps[key] (a case key, as a filter on the built layers).
#target photoshop

var PB = {};
PB.SRGB = 'sRGB IEC61966-2.1';
PB.LOG = [];
PB.inputs = {};  // 'file@turn': the input's document, opened once
PB.adjustments = {};
PB.kinds = {};
PB.layerKeys = {};
PB.steps = {};

PB.c2t = function (s) { return charIDToTypeID(s); };
PB.s2t = function (s) { return stringIDToTypeID(s); };
PB.note = function (s) { PB.LOG.push(s); };
PB.keyName = function (k) { return typeIDToStringID(k) || typeIDToCharID(k); };
PB.has = function (list, x) {
  for (var i = 0; i < list.length; i++)
    if (list[i] === x) return true;
  return false;
};

// A descriptor's values as name=value, objects and lists nested: for photoshop.txt.
PB.dump = function (d) {
  var s = [];
  for (var i = 0; i < d.count; i++) {
    var k = d.getKey(i);
    s.push(PB.keyName(k) + '=' + PB.dumpValue(d, k, d.getType(k)));
  }
  return s.join(', ');
};
PB.dumpValue = function (d, k, t) {  // d: a descriptor (k a key) or a list (k an index)
  if (t == DescValueType.BOOLEANTYPE) return d.getBoolean(k);
  if (t == DescValueType.INTEGERTYPE) return d.getInteger(k);
  if (t == DescValueType.DOUBLETYPE) return d.getDouble(k);
  if (t == DescValueType.UNITDOUBLE) return d.getUnitDoubleValue(k) + PB.keyName(d.getUnitDoubleType(k));
  if (t == DescValueType.STRINGTYPE) return '"' + d.getString(k) + '"';
  if (t == DescValueType.ENUMERATEDTYPE) return PB.keyName(d.getEnumerationValue(k));
  if (t == DescValueType.OBJECTTYPE) return '{' + PB.dump(d.getObjectValue(k)) + '}';
  if (t == DescValueType.LISTTYPE) {
    var l = d.getList(k), s = [];
    for (var i = 0; i < l.count; i++) s.push(PB.dumpValue(l, i, l.getType(i)));
    return '[' + s.join(', ') + ']';
  }
  return String(t);
};

// JSON, which ExtendScript lacks.
PB.quote = function (s) {
  var out = '"';
  for (var i = 0; i < s.length; i++) {
    var c = s.charAt(i), n = s.charCodeAt(i);
    if (c == '"' || c == '\\') out += '\\' + c;
    else if (n < 32) out += '\\u' + ('000' + n.toString(16)).slice(-4);
    else out += c;
  }
  return out + '"';
};
PB.json = function (v) {
  if (v === null || v === undefined) return 'null';
  var t = typeof v, parts = [], i;
  if (t == 'boolean') return v ? 'true' : 'false';
  if (t == 'number') {
    if (!isFinite(v)) throw new Error('JSON has no ' + v);
    return String(v);
  }
  if (t == 'string') return PB.quote(v);
  if (v instanceof Array) {
    for (i = 0; i < v.length; i++) parts.push(PB.json(v[i]));
    return '[' + parts.join(', ') + ']';
  }
  if (t == 'object') {
    for (var k in v)
      if (v.hasOwnProperty(k) && v[k] !== undefined && typeof v[k] != 'function') parts.push(PB.quote(k) + ': ' + PB.json(v[k]));
    return '{' + parts.join(', ') + '}';
  }
  throw new Error('no JSON for a ' + t);
};
PB.copy = function (v) {  // deep, of plain data
  if (v === null || typeof v != 'object') return v;
  var c = v instanceof Array ? [] : {};
  for (var k in v)
    if (v.hasOwnProperty(k)) c[k] = PB.copy(v[k]);
  return c;
};

// ---- Cases ---------------------------------------------------------------------------------------------------------

// Photoshop's blend modes by photo-beauty's names (kBlendNames): DOM names, the first one this Photoshop has.
PB.BLENDS = {
  passThrough: ['PASSTHROUGH'], normal: ['NORMAL'], dissolve: ['DISSOLVE'], darken: ['DARKEN'], multiply: ['MULTIPLY'],
  colorBurn: ['COLORBURN'], linearBurn: ['LINEARBURN'], darkerColor: ['DARKERCOLOR'], lighten: ['LIGHTEN'],
  screen: ['SCREEN'], colorDodge: ['COLORDODGE'], linearDodge: ['LINEARDODGE'], lighterColor: ['LIGHTERCOLOR'],
  overlay: ['OVERLAY'], softLight: ['SOFTLIGHT'], hardLight: ['HARDLIGHT'], vividLight: ['VIVIDLIGHT'],
  linearLight: ['LINEARLIGHT'], pinLight: ['PINLIGHT'], hardMix: ['HARDMIX'], difference: ['DIFFERENCE'],
  exclusion: ['EXCLUSION'], subtract: ['SUBTRACT'], divide: ['DIVIDE'], hue: ['HUE'],
  saturation: ['SATURATIONBLEND', 'SATURATION'], color: ['COLORBLEND', 'COLOR'], luminosity: ['LUMINOSITY']
};
PB.blendMode = function (name) {
  var names = PB.BLENDS[name];
  if (!names) throw new Error('no blend mode ' + name);
  for (var i = 0; i < names.length; i++)
    if (BlendMode[names[i]] !== undefined) return BlendMode[names[i]];
  throw new Error('this Photoshop has no BlendMode.' + names.join(' or BlendMode.') + ' (' + name + ')');
};
// LayerProps' JSON keys (A30's doc.cpp), in the order its to_json() writes them.
PB.PROPS = ['version', 'name', 'blend', 'opacity', 'fill', 'maskDensity', 'maskFeather', 'visible', 'clip', 'maskOff',
            'maskLinked', 'blendIf', 'channels', 'adj', 'vals'];
PB.UNIT = ['opacity', 'fill', 'maskDensity'];  // 0..1
PB.BLEND_IF = ['gray', 'red', 'green', 'blue'];  // kBlendIfNames
PB.CHANNELS = ['r', 'g', 'b', 'gray'];
PB.LAYER_KEYS = ['kind', 'src', 'rotate', 'mask', 'children', 'props'];
PB.CASE_KEYS = ['name', 'canvas', 'bits', 'layers', 'note'];

// Blend If as to_json() writes it: the channels off 0, 0, 255, 255, each with both sliders, in kBlendIfNames' order;
// undefined when none is. A slider is 4 whole numbers 0..255 in order; one left out is 0, 0, 255, 255.
PB.blendIfOf = function (v, name) {
  var none = [0, 0, 255, 255], out = {}, any = false, k;
  if (v === null) return undefined;  // to_json()'s default, as props_from_json() reads null
  if (typeof v != 'object' || v instanceof Array) throw new Error(name + ': Blend If ' + PB.json(v));
  var slider = function (a) {
    if (a === undefined) return none;
    if (!(a instanceof Array) || a.length != 4) throw new Error(name + ': a Blend If slider ' + PB.json(a));
    for (var i = 0; i < 4; i++)
      if (typeof a[i] != 'number' || a[i] != Math.round(a[i]) || a[i] < (i ? a[i - 1] : 0) || a[i] > 255)
        throw new Error(name + ': a Blend If slider ' + PB.json(a));
    return PB.copy(a);
  };
  for (k in v)
    if (v.hasOwnProperty(k) && !PB.has(PB.BLEND_IF, k)) throw new Error(name + ': no Blend If channel ' + k);
  for (var i = 0; i < PB.BLEND_IF.length; i++) {
    var c = v[PB.BLEND_IF[i]];
    if (c === undefined) continue;
    for (k in c)
      if (c.hasOwnProperty(k) && k != 'layer' && k != 'under') throw new Error(name + ': Blend If key ' + k);
    var r = {layer: slider(c.layer), under: slider(c.under)};
    if (PB.json(r) == PB.json({layer: none, under: none})) continue;
    out[PB.BLEND_IF[i]] = r;
    any = true;
  }
  return any ? out : undefined;
};
// Channels as to_json() writes them: the boxes off only; undefined when none is.
PB.channelsOf = function (v, name) {
  var out = {}, any = false;
  if (v === null) return undefined;
  if (typeof v != 'object' || v instanceof Array) throw new Error(name + ': channels ' + PB.json(v));
  for (var k in v)
    if (v.hasOwnProperty(k) && (!PB.has(PB.CHANNELS, k) || typeof v[k] != 'boolean'))
      throw new Error(name + ': channel ' + k + ': ' + PB.json(v[k]));
  for (var i = 0; i < PB.CHANNELS.length; i++)
    if (v[PB.CHANNELS[i]] === false) out[PB.CHANNELS[i]] = false, any = true;
  return any ? out : undefined;
};

// The case as index.json gets it, checked: keys in a set order, kind, props with version 1 first, a name (unique in
// the case) and the rest in to_json()'s order, a group's blend (Pass Through unless it says), bits.
PB.normalise = function (c, bits) {
  if (typeof c.name != 'string' || !c.name) throw new Error('a case without a name');
  if (!(c.canvas instanceof Array) || c.canvas.length != 2) throw new Error(c.name + ': no canvas [w, h]');
  var names = {}, count = 0, k;
  var rest = function (from, to, known) {  // the keys a later script handles, after the known ones
    for (var key in from)
      if (from.hasOwnProperty(key) && !PB.has(known, key)) to[key] = PB.copy(from[key]);
  };
  var walk = function (layers) {
    var out = [];
    for (var i = 0; i < layers.length; i++) {
      var l = layers[i], o = {kind: l.kind === undefined ? 'pixel' : l.kind}, p = {version: 1}, key;
      if (!PB.has(['pixel', 'group', 'adj'], o.kind) && !PB.kinds[o.kind]) throw new Error(c.name + ': no kind ' + o.kind);
      if (o.kind == 'pixel' && !l.src) throw new Error(c.name + ': a pixel layer without src');
      if (o.kind == 'group' && !(l.children instanceof Array)) throw new Error(c.name + ': a group without children');
      if (o.kind == 'adj' && !(l.props && l.props.adj)) throw new Error(c.name + ': an adjustment layer without props.adj');
      for (key in l)
        if (l.hasOwnProperty(key) && !PB.has(PB.LAYER_KEYS, key) && !PB.layerKeys[key]) throw new Error(c.name + ': layer key ' + key);
      count++;
      var props = l.props || {};
      p.name = props.name ? props.name : 'layer ' + count;
      if (names[p.name]) throw new Error(c.name + ': two layers named ' + p.name);
      names[p.name] = true;
      for (key in props)
        if (props.hasOwnProperty(key) && !PB.has(PB.PROPS, key) && !PB.layerKeys[key]) throw new Error(c.name + ': prop ' + key);
      for (var j = 2; j < PB.PROPS.length; j++) {  // to_json()'s order, then the keys of later scripts
        key = PB.PROPS[j];
        if (props[key] !== undefined) p[key] = PB.copy(props[key]);
        else if (key == 'blend' && o.kind == 'group') p.blend = 'passThrough';  // a new group's in Photoshop
      }
      rest(props, p, PB.PROPS);
      for (j = 0; j < PB.UNIT.length; j++)
        if (p[PB.UNIT[j]] !== undefined && !(p[PB.UNIT[j]] >= 0 && p[PB.UNIT[j]] <= 1))
          throw new Error(c.name + ': ' + PB.UNIT[j] + ' ' + p[PB.UNIT[j]] + ' (0..1)');
      if (p.blendIf !== undefined) p.blendIf = PB.blendIfOf(p.blendIf, c.name);
      if (p.channels !== undefined) p.channels = PB.channelsOf(p.channels, c.name);
      if (p.blend !== undefined && !PB.BLENDS[p.blend]) throw new Error(c.name + ': no blend mode ' + p.blend);
      if (p.blend == 'passThrough' && o.kind != 'group') throw new Error(c.name + ': Pass Through is for groups');
      if (l.src !== undefined) o.src = l.src;
      if (l.rotate !== undefined) o.rotate = l.rotate;
      if (l.mask !== undefined) o.mask = l.mask;
      o.props = p;
      if (o.kind == 'group') o.children = walk(l.children);
      rest(l, o, PB.LAYER_KEYS);
      out.push(o);
    }
    return out;
  };
  for (k in c)
    if (c.hasOwnProperty(k) && !PB.has(PB.CASE_KEYS, k) && !PB.steps[k]) throw new Error(c.name + ': case key ' + k);
  var out = {name: c.name, canvas: PB.copy(c.canvas), bits: c.bits === undefined ? bits : c.bits};
  if (out.bits != 8 && out.bits != 16) throw new Error(c.name + ': bits ' + out.bits);
  if (c.note !== undefined) out.note = c.note;
  out.layers = walk(c.layers);
  rest(c, out, PB.CASE_KEYS);
  return out;
};

// index.json: what ran, the cases done (one a line), and those that failed.
PB.indexText = function (info, entries, failed) {
  var head = [];
  for (var k in info)
    if (info.hasOwnProperty(k)) head.push(PB.quote(k) + ': ' + PB.json(info[k]));
  var lines = [];
  for (var i = 0; i < entries.length; i++) lines.push(PB.json(entries[i]));
  return '{' + head.join(', ') + ', "failed": ' + PB.json(failed) + ',\n"images": [\n' + lines.join(',\n') + '\n]}\n';
};

// PB.run()'s options: a number is bits. bits: of the PNGs, 16 or 8 (a case may say otherwise); inputs: the inputs'
// folder next to the script; repoInputs: where they are in the repo from the folder of the images (index.json's
// "inputs": p1's are tests/golden/ps/p1/in, the images tests/golden/ps/p1/<script>).
PB.options = function (o) {
  if (typeof o == 'number' || o === undefined) o = {bits: o};
  return {bits: o.bits || 16, inputs: o.inputs || 'in', repoInputs: o.repoInputs || '../in'};
};

// What PB.run() would write as index.json, without Photoshop (a check of the cases: build/jscheck).
PB.offline = function (script, cases, options) {
  var o = PB.options(options), entries = [], seen = {};
  for (var i = 0; i < cases.length; i++) {
    var e = PB.normalise(cases[i], o.bits);
    if (seen[e.name]) throw new Error('two cases named ' + e.name);
    seen[e.name] = true;
    entries.push(e);
  }
  return PB.indexText({script: script + '.jsx', complete: true, inputs: o.repoInputs}, entries, []);
};

// ---- Photoshop's settings ------------------------------------------------------------------------------------------

// Edit > Color Settings as the images need them: no dither, RGB blended as encoded (not at gamma 1), no asking about
// profiles (blend.jsx of B4's, the same). Keys matched by name, as versions name them as they please; photoshop.txt
// gets them all, before and after. Returns what puts them back.
PB.colorSettings = function () {
  var c2t = PB.c2t, s2t = PB.s2t;
  var ref = function () {
    var r = new ActionReference();
    r.putProperty(c2t('Prpr'), s2t('colorSettings'));
    r.putEnumerated(c2t('capp'), c2t('Ordn'), c2t('Trgt'));
    return r;
  };
  var get = function () { return executeActionGet(ref()).getObjectValue(s2t('colorSettings')); };
  var set = function (d) {
    var a = new ActionDescriptor();
    a.putReference(c2t('null'), ref());
    a.putObject(c2t('T   '), s2t('colorSettings'), d);
    executeAction(c2t('setd'), a, DialogModes.NO);
  };
  var nothing = function () {};
  var before, want;
  try {
    before = get();
    want = get();
  } catch (e) {
    PB.note('colour settings: cannot read them (' + e + '); set them by hand (README.md)');
    return nothing;
  }
  PB.note('colour settings before: ' + PB.dump(before));
  var flipped = [];
  for (var i = 0; i < want.count; i++) {
    var k = want.getKey(i), name = PB.keyName(k);
    if (want.getType(k) != DescValueType.BOOLEANTYPE || !want.getBoolean(k)) continue;
    var rgbGamma = /blend/i.test(name) && /gamma/i.test(name) && !/text/i.test(name);
    if (/dither/i.test(name) || /^ask/i.test(name) || rgbGamma) {
      want.putBoolean(k, false);
      flipped.push(name);
    }
  }
  if (!flipped.length) return nothing;
  try {
    set(want);
    PB.note('colour settings: turned off ' + flipped.join(', ') + '; after: ' + PB.dump(get()));
  } catch (e) {
    PB.note('colour settings: cannot turn off ' + flipped.join(', ') + ' (' + e + ')');
    return nothing;
  }
  return function () {
    try {
      set(before);
    } catch (e) {
      PB.note('colour settings: cannot put them back (' + e + '); turn ' + flipped.join(', ') + ' on again by hand');
    }
  };
};

PB.solid = function (v) {  // grey v, 0..1
  var c = new SolidColor();
  c.rgb.red = c.rgb.green = c.rgb.blue = v * 255;
  return c;
};

// That Photoshop blends RGB as encoded, as photo-beauty does (B4's check): grey at 100% and at 50% over black, from
// the red channel's histogram. Blending at gamma 1 makes the 50% about 0.36 of white, not half the 100%.
PB.checkBlending = function () {
  var doc = app.documents.add(16, 4, 72, 'blend-check', NewDocumentMode.RGB, DocumentFill.WHITE, 1,
                              BitsPerChannelType.SIXTEEN, PB.SRGB);
  doc.selection.selectAll();
  doc.selection.fill(PB.solid(0));
  doc.artLayers.add();
  doc.selection.select([[0, 0], [8, 0], [8, 4], [0, 4]]);
  doc.selection.fill(PB.solid(0.5));
  doc.artLayers.add().opacity = 50;
  doc.selection.select([[8, 0], [16, 0], [16, 4], [8, 4]]);
  doc.selection.fill(PB.solid(0.5));
  doc.selection.deselect();
  doc.flatten();
  var h = doc.channels[0].histogram, levels = [];
  for (var i = 0; i < 256; i++)
    if (h[i]) levels.push(i);
  doc.close(SaveOptions.DONOTSAVECHANGES);
  PB.note('blending: grey over black at 50% and 100% comes out ' + levels.join(' and ') + ' of 255');
  if (levels.length != 2 || Math.abs(levels[0] - levels[1] / 2) > 2)
    throw new Error('Photoshop blends RGB at gamma 1: uncheck Blend RGB Colors Using Gamma in Edit > Color Settings');
};

// That going from 16 bits to 8 does not dither (Use Dither off): a soft edge (black to white, Gaussian Blur) in a
// 16-bit document 8 rows high, made 8 bits. Rows are alike, so undithered every level counts a multiple of 8 pixels.
PB.checkDither = function () {
  var doc = app.documents.add(64, 8, 72, 'dither-check', NewDocumentMode.RGB, DocumentFill.WHITE, 1,
                              BitsPerChannelType.SIXTEEN, PB.SRGB);
  doc.selection.select([[0, 0], [32, 0], [32, 8], [0, 8]]);
  doc.selection.fill(PB.solid(0));
  doc.selection.deselect();
  doc.activeLayer.applyGaussianBlur(6);
  doc.bitsPerChannel = BitsPerChannelType.EIGHT;
  var h = doc.channels[0].histogram, levels = 0, odd = 0;
  for (var i = 0; i < 256; i++) {
    if (h[i]) levels++;
    if (h[i] % 8) odd++;
  }
  doc.close(SaveOptions.DONOTSAVECHANGES);
  PB.note('dither: a soft edge at 16 bits comes out in ' + levels + ' levels at 8, ' + odd + ' of them not whole rows');
  if (odd) throw new Error('Photoshop dithers going to 8 bits: uncheck Use Dither in Edit > Color Settings');
  if (levels < 16) throw new Error('the dither check made ' + levels + ' levels only: Gaussian Blur did not run');
};

// ---- Building a case -----------------------------------------------------------------------------------------------

// An input as the cases need it: RGB, 16 bits, its pixels as in the file (it has no profile, so opening converts
// nothing), sRGB assigned, not a Background, turned clockwise; opened once.
PB.input = function (file, turn) {
  var key = file + '@' + (turn || 0);
  if (PB.inputs[key]) return PB.inputs[key];
  var doc;
  if (turn) {  // a copy: opening a file already open gives the same document
    doc = PB.input(file, 0).duplicate(key);
    doc.rotateCanvas(turn);
  } else {
    var f = new File(PB.inDir + '/' + file);
    if (!f.exists) throw new Error('no input ' + f.fsName + ' (pb-bench ps-inputs makes them)');
    doc = app.open(f);
    if (doc.mode != DocumentMode.RGB || doc.bitsPerChannel != BitsPerChannelType.SIXTEEN)
      throw new Error(file + ' opened as ' + doc.mode + ', ' + doc.bitsPerChannel + ', not RGB at 16 bits');
    doc.colorProfileName = PB.SRGB;  // assigned, not converted
    var layer = doc.layers[0];
    if (layer.isBackgroundLayer) layer.isBackgroundLayer = false;
    PB.note(file + ': ' + doc.width.as('px') + ' x ' + doc.height.as('px') + ', ' + doc.bitsPerChannel + ', ' +
            doc.colorProfileName);
  }
  PB.inputs[key] = doc;
  return doc;
};

// The input's layer, copied to the top of doc.
PB.copyLayer = function (doc, file, turn) {
  var src = PB.input(file, turn);
  app.activeDocument = src;
  var layer = src.layers[0].duplicate(doc, ElementPlacement.PLACEATBEGINNING);
  app.activeDocument = doc;
  return layer;
};

// The active layer set as `fill` puts its keys into a layer descriptor.
PB.setLayer = function (fill) {
  var d = new ActionDescriptor(), r = new ActionReference(), l = new ActionDescriptor();
  r.putEnumerated(PB.c2t('Lyr '), PB.c2t('Ordn'), PB.c2t('Trgt'));
  d.putReference(PB.c2t('null'), r);
  fill(l);
  d.putObject(PB.c2t('T   '), PB.c2t('Lyr '), l);
  executeAction(PB.c2t('setd'), d, DialogModes.NO);
};
PB.activeLayerDesc = function () {
  var r = new ActionReference();
  r.putEnumerated(PB.c2t('Lyr '), PB.c2t('Ordn'), PB.c2t('Trgt'));
  return executeActionGet(r);
};

// The active layer's layer mask, if it has one, deleted (not applied): an adjustment layer comes with a white one.
PB.deleteMask = function () {
  if (!PB.activeLayerDesc().getBoolean(PB.s2t('hasUserMask'))) return;
  var d = new ActionDescriptor(), r = new ActionReference();
  r.putEnumerated(PB.c2t('Chnl'), PB.c2t('Chnl'), PB.c2t('Msk '));
  d.putReference(PB.c2t('null'), r);
  executeAction(PB.c2t('Dlt '), d, DialogModes.NO);
};

// A layer mask on the layer from the alpha of an input: its transparency loaded as the selection, Reveal Selection.
PB.maskFrom = function (doc, layer, file) {
  var c2t = PB.c2t, d, r;
  doc.activeLayer = layer;
  PB.deleteMask();
  var tmp = PB.copyLayer(doc, file, 0);
  doc.activeLayer = tmp;
  d = new ActionDescriptor(), r = new ActionReference();
  r.putProperty(c2t('Chnl'), c2t('fsel'));
  d.putReference(c2t('null'), r);
  var t = new ActionReference();
  t.putEnumerated(c2t('Chnl'), c2t('Chnl'), c2t('Trsp'));
  d.putReference(c2t('T   '), t);
  executeAction(c2t('setd'), d, DialogModes.NO);
  tmp.remove();
  doc.activeLayer = layer;
  d = new ActionDescriptor(), r = new ActionReference();
  d.putClass(c2t('Nw  '), c2t('Chnl'));
  r.putEnumerated(c2t('Chnl'), c2t('Chnl'), c2t('Msk '));
  d.putReference(c2t('At  '), r);
  d.putEnumerated(c2t('Usng'), c2t('UsrM'), c2t('RvlS'));
  executeAction(c2t('Mk  '), d, DialogModes.NO);
  doc.selection.deselect();
};

// Blend If of the active layer: every channel's ranges, those the case does not give at 0, 0, 255, 255.
PB.blendIf = function (bi) {
  var c2t = PB.c2t, ids = {gray: 'Gry ', red: 'Rd  ', green: 'Grn ', blue: 'Bl  '};
  var keys = ['SrcB', 'Srcl', 'SrcW', 'Srcm', 'DstB', 'Dstl', 'DstW', 'Dstt'];  // layer's 4, then the underlying's
  PB.setLayer(function (l) {
    var list = new ActionList();
    for (var i = 0; i < PB.BLEND_IF.length; i++) {
      var v = bi[PB.BLEND_IF[i]] || {}, me = v.layer || [0, 0, 255, 255], under = v.under || [0, 0, 255, 255];
      var o = new ActionDescriptor(), ch = new ActionReference(), all = me.concat(under);
      ch.putEnumerated(c2t('Chnl'), c2t('Chnl'), c2t(ids[PB.BLEND_IF[i]]));
      o.putReference(c2t('Chnl'), ch);
      for (var j = 0; j < keys.length; j++) o.putInteger(c2t(keys[j]), all[j]);
      list.putObject(c2t('Blnd'), o);
    }
    l.putList(c2t('Blnd'), list);
  });
};

// Advanced Blending's channels of the active layer: those not false stay on.
PB.channels = function (ch) {
  var ids = [['r', 'Rd  '], ['g', 'Grn '], ['b', 'Bl  ']];
  if (ch.gray === false) throw new Error('channels: an RGB document has no grey channel');
  PB.setLayer(function (l) {
    var list = new ActionList();
    for (var i = 0; i < ids.length; i++)
      if (ch[ids[i][0]] !== false) list.putEnumerated(PB.c2t('Chnl'), PB.c2t(ids[i][1]));
    l.putList(PB.s2t('channelRestrictions'), list);
  });
};

// Adjustment layers by AdjustSpec.ORDER name: [its class, its descriptor or null for none]. D32 adds the others.
PB.adjustments.Invert = function () { return [PB.c2t('Invr'), null]; };
PB.makeAdjustment = function (doc, name, vals) {
  var make = PB.adjustments[name];
  if (!make) throw new Error('no adjustment ' + name + ' in PB.adjustments');
  var t = make(vals || {}), d = new ActionDescriptor(), r = new ActionReference(), a = new ActionDescriptor();
  r.putClass(PB.c2t('AdjL'));
  d.putReference(PB.c2t('null'), r);
  if (t[1]) a.putObject(PB.c2t('Type'), t[0], t[1]);
  else a.putClass(PB.c2t('Type'), t[0]);
  d.putObject(PB.c2t('Usng'), PB.c2t('AdjL'), a);
  executeAction(PB.c2t('Mk  '), d, DialogModes.NO);
  return doc.activeLayer;
};

// Puts item at the top of a container {set: a LayerSet, null for the document, last: what it has on top}.
PB.place = function (item, box) {
  if (box.last) item.move(box.last, ElementPlacement.PLACEBEFORE);  // above it
  else item.move(box.set, ElementPlacement.INSIDE);
  box.last = item;
};

// The layers of a case, bottom first, made and stacked: [{json, layer}], groups before their children.
PB.make = function (doc, layers, box, built) {
  for (var i = 0; i < layers.length; i++) {
    var l = layers[i], item;
    if (l.kind == 'pixel') {
      var src = PB.input(l.src, l.rotate || 0);
      if (src.width.as('px') != doc.width.as('px') || src.height.as('px') != doc.height.as('px'))
        throw new Error(l.src + ' is not of the canvas size');
      item = PB.copyLayer(doc, l.src, l.rotate || 0);
    } else if (l.kind == 'group') {
      item = doc.layerSets.add();
    } else if (l.kind == 'adj') {
      doc.selection.deselect();
      item = PB.makeAdjustment(doc, l.props.adj, l.props.vals);
      PB.deleteMask();  // its white one: the case says where it has a mask
    } else {
      item = PB.kinds[l.kind](l, doc);  // a kind a later script makes
    }
    item.name = l.props.name;
    PB.place(item, box);
    built.push({json: l, layer: item});
    if (l.kind == 'group') PB.make(doc, l.children, {set: item, last: null}, built);
  }
};

// A layer's props and keys, set and read back where the DOM tells.
PB.apply = function (doc, b) {
  var l = b.json, p = l.props, layer = b.layer, group = l.kind == 'group';
  doc.activeLayer = layer;
  layer.blendMode = PB.blendMode(p.blend || 'normal');
  if (p.opacity !== undefined) layer.opacity = p.opacity * 100;
  if (p.fill !== undefined) {
    if (group) throw new Error('fill on a group');
    layer.fillOpacity = p.fill * 100;
  }
  if (p.clip) layer.grouped = true;
  if (p.visible === false) layer.visible = false;
  if (l.mask) PB.maskFrom(doc, layer, l.mask);
  if (p.maskDensity !== undefined || p.maskFeather !== undefined || p.maskOff || p.maskLinked === false) {
    if (!l.mask) throw new Error(p.name + ': mask props without a mask');
    PB.setLayer(function (d) {
      if (p.maskDensity !== undefined) d.putUnitDouble(PB.s2t('userMaskDensity'), PB.c2t('#Prc'), p.maskDensity * 100);
      if (p.maskFeather !== undefined) d.putUnitDouble(PB.s2t('userMaskFeather'), PB.c2t('#Pxl'), p.maskFeather);
      if (p.maskOff) d.putBoolean(PB.s2t('userMaskEnabled'), false);
      if (p.maskLinked === false) d.putBoolean(PB.s2t('userMaskLinked'), false);
    });
  }
  if (p.blendIf) PB.blendIf(p.blendIf);
  if (p.channels) PB.channels(p.channels);
  for (var k in p)
    if (p.hasOwnProperty(k) && PB.layerKeys[k]) PB.layerKeys[k](p[k], layer, doc, l);
  for (k in l)
    if (l.hasOwnProperty(k) && PB.layerKeys[k]) PB.layerKeys[k](l[k], layer, doc, l);
  // Read back: what the DOM says, and what Photoshop has of the rest (photoshop.txt).
  var want = PB.blendMode(p.blend || 'normal');
  if (layer.blendMode != want) throw new Error(p.name + ': blend mode ' + layer.blendMode + ', not ' + want);
  if (Math.abs(layer.opacity - (p.opacity === undefined ? 100 : p.opacity * 100)) > 0.5)
    throw new Error(p.name + ': opacity ' + layer.opacity);
  if (!group && Math.abs(layer.fillOpacity - (p.fill === undefined ? 100 : p.fill * 100)) > 0.5)
    throw new Error(p.name + ': fill ' + layer.fillOpacity);
  if (!group && layer.grouped != !!p.clip) throw new Error(p.name + ': clipped ' + layer.grouped);
  var d = PB.activeLayerDesc(), s2t = PB.s2t, keys = ['mode', 'opacity', 'fillOpacity', 'group', 'hasUserMask',
      'userMaskDensity', 'userMaskFeather', 'userMaskEnabled', 'userMaskLinked', 'blendRange', 'channelRestrictions'],
      seen = [];
  for (var i = 0; i < keys.length; i++)
    if (d.hasKey(s2t(keys[i]))) seen.push(keys[i] + '=' + PB.dumpValue(d, s2t(keys[i]), d.getType(s2t(keys[i]))));
  PB.note('  ' + p.name + ': ' + seen.join(', '));
  if (!!l.mask != (d.hasKey(s2t('hasUserMask')) && d.getBoolean(s2t('hasUserMask'))))
    throw new Error(p.name + ': ' + (l.mask ? 'no layer mask made' : 'a layer mask it should not have'));
};

// The names of a container's layers, top first, a group's as [name, [its]]: to check the stack against the case.
PB.tree = function (layers) {
  var out = [];
  for (var i = 0; i < layers.length; i++)
    out.push(layers[i].typename == 'LayerSet' ? [layers[i].name, PB.tree(layers[i].layers)] : layers[i].name);
  return out;
};
PB.wantTree = function (layers) {
  var out = [];
  for (var i = layers.length - 1; i >= 0; i--)
    out.push(layers[i].kind == 'group' ? [layers[i].props.name, PB.wantTree(layers[i].children)] : layers[i].props.name);
  return out;
};

// The case built in a new document: 16 bits, sRGB, its canvas. Closed again if it cannot be built.
PB.build = function (c) {
  var doc = app.documents.add(c.canvas[0], c.canvas[1], 72, c.name, NewDocumentMode.RGB, DocumentFill.TRANSPARENT, 1,
                              BitsPerChannelType.SIXTEEN, PB.SRGB);
  try {
    var placeholder = doc.layers[0], built = [];  // the new document's empty layer: under the case's until they are in
    PB.make(doc, c.layers, {set: null, last: placeholder}, built);
    placeholder.remove();
    for (var i = 0; i < built.length; i++) PB.apply(doc, built[i]);
    for (var k in c)
      if (c.hasOwnProperty(k) && PB.steps[k]) PB.steps[k](c[k], doc, built);
    doc.selection.deselect();
    var got = PB.json(PB.tree(doc.layers)), want = PB.json(PB.wantTree(c.layers));
    if (got != want) throw new Error('the layers came out ' + got + ', not ' + want);
  } catch (e) {
    PB.close(doc);
    throw e;
  }
  return doc;
};
PB.close = function (doc) {
  try {
    doc.close(SaveOptions.DONOTSAVECHANGES);
  } catch (e) {}
};

// ---- Saving --------------------------------------------------------------------------------------------------------

PB.pngOptions = function () {
  var o = new PNGSaveOptions();
  try {
    o.compression = 9;
    o.interlaced = false;
  } catch (e) {}
  return o;
};
// The composite as <name>.png in out: 16 bits as the document is; 8 bits as that PNG opened alone and made 8 bits
// (Use Dither off: rounded). The first of each depth is opened again to see it is.
PB.save = function (doc, name, bits) {
  var file = new File(PB.out + '/' + (bits == 8 ? '~' : '') + name + '.png');
  doc.saveAs(file, PB.pngOptions(), true, Extension.LOWERCASE);
  if (bits == 8) {
    var deep = app.open(file);
    try {
      deep.bitsPerChannel = BitsPerChannelType.EIGHT;
      deep.saveAs(new File(PB.out + '/' + name + '.png'), PB.pngOptions(), true, Extension.LOWERCASE);
    } finally {
      PB.close(deep);
      file.remove();
      app.activeDocument = doc;
    }
  }
  if (PB.checked[bits]) return;
  var check = app.open(new File(PB.out + '/' + name + '.png')), got = check.bitsPerChannel;
  check.close(SaveOptions.DONOTSAVECHANGES);
  app.activeDocument = doc;
  var want = bits == 8 ? BitsPerChannelType.EIGHT : BitsPerChannelType.SIXTEEN;
  if (got != want) throw new Error('Photoshop saved ' + name + '.png at ' + got + ', not ' + bits + ' bits');
  PB.checked[bits] = true;
};

// ---- Running -------------------------------------------------------------------------------------------------------

// Every case of a script (cases: from its cases()), its images into out/<script>/ next to it: index.json, the PNGs,
// photoshop.txt. A case that fails is noted and skipped; a failed check of the settings stops it all. options: as
// PB.options() reads them.
PB.run = function (script, cases, options) {
  var o = PB.options(options), bits = o.bits;
  var dir = new File($.fileName).parent;
  PB.inDir = new Folder(dir + '/' + o.inputs);
  if (!PB.inDir.exists) {
    PB.inDir = Folder.selectDialog('The folder with the inputs of ' + script + '.jsx (README.md)');
    if (!PB.inDir) return;
  }
  var outRoot = new Folder(dir + '/out');
  if (!outRoot.exists) outRoot.create();
  PB.out = new Folder(outRoot + '/' + script);
  if (!PB.out.exists) PB.out.create();
  PB.checked = {};
  var units = app.preferences.rulerUnits, dialogs = app.displayDialogs, fg = app.foregroundColor;
  app.preferences.rulerUnits = Units.PIXELS;  // documents at 72 ppi: points are pixels too
  app.displayDialogs = DialogModes.NO;
  var started = new Date(), entries = [], failed = [], fatal = null, restore = function () {};
  PB.note('Photoshop ' + app.version + ', ' + $.os + ', ' + started + '; ' + script + '.jsx, ' + cases.length + ' cases');
  try {
    restore = PB.colorSettings();
    PB.checkBlending();
    if (bits == 8) PB.checkDither();
    for (var m in PB.BLENDS)
      if (PB.BLENDS.hasOwnProperty(m)) PB.blendMode(m);
  } catch (e) {
    fatal = e;
  }
  for (var i = 0; i < cases.length && !fatal; i++) {
    var c = null, doc = null;
    try {
      c = PB.normalise(cases[i], bits);
      PB.note(c.name);
      doc = PB.build(c);
      PB.save(doc, c.name, c.bits);
      entries.push(c);
    } catch (e) {
      var name = c ? c.name : 'case ' + i;
      failed.push({name: name, error: String(e) + (e.line ? ' (line ' + e.line + ')' : '')});
      PB.note('FAILED ' + name + ': ' + e + (e.line ? ' (line ' + e.line + ')' : ''));
    }
    if (doc) PB.close(doc);
  }
  if (fatal) PB.note('STOPPED: ' + fatal + (fatal.line ? ' (line ' + fatal.line + ')' : ''));
  for (var k in PB.inputs)
    if (PB.inputs.hasOwnProperty(k)) PB.close(PB.inputs[k]);
  PB.inputs = {};
  restore();
  app.preferences.rulerUnits = units;
  app.displayDialogs = dialogs;
  app.foregroundColor = fg;
  var info = {script: script + '.jsx', photoshop: app.version, os: $.os, date: String(started), complete: !fatal,
              inputs: o.repoInputs};
  if (fatal) info.error = String(fatal);
  PB.note(entries.length + ' images, ' + failed.length + ' failed, ' + Math.round((new Date() - started) / 1000) + ' s');
  PB.write(PB.out + '/index.json', PB.indexText(info, entries, failed));
  PB.write(PB.out + '/photoshop.txt', PB.LOG.join('\n') + '\n');
  alert(fatal ? 'Stopped: ' + fatal + '\n\nDetails: ' + PB.out.fsName + '\\photoshop.txt'
              : 'Done: ' + entries.length + ' images' + (failed.length ? ', ' + failed.length + ' failed (photoshop.txt)' : '') +
                  '\n' + PB.out.fsName);
};

PB.write = function (path, text) {
  var f = new File(path);
  f.encoding = 'UTF-8';
  f.lineFeed = 'Unix';
  f.open('w');
  f.write(text);
  f.close();
};
