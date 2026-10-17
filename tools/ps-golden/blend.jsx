// Photoshop's composites for photo-beauty's tests (tests/test_ps_golden.cpp), README.md has the steps. Reads base.png
// and top.png (pb-bench ps-inputs) next to this script, writes into out/ next to them:
//   <mode>-<opacity>.png  top over base, top in each P0 blend mode at 100 and 50% opacity: RGBA, 16 bits
//   dabs.png              single dabs of the round brush, as dabs() lays them out
//   photoshop.txt         Photoshop's version, the colour settings and brush it ran with, what it checked
// Written for Photoshop 2025 (26) on Windows; plain ExtendScript, so CC 2019 (20) on should run it too.
#target photoshop

var MODES = [['normal', BlendMode.NORMAL], ['darken', BlendMode.DARKEN], ['multiply', BlendMode.MULTIPLY],
             ['colorBurn', BlendMode.COLORBURN], ['lighten', BlendMode.LIGHTEN], ['screen', BlendMode.SCREEN],
             ['colorDodge', BlendMode.COLORDODGE], ['overlay', BlendMode.OVERLAY], ['softLight', BlendMode.SOFTLIGHT],
             ['linearLight', BlendMode.LINEARLIGHT], ['difference', BlendMode.DIFFERENCE]];  // kBlendNames' names
var SRGB = 'sRGB IEC61966-2.1';
var LOG = [];

function c2t(s) { return charIDToTypeID(s); }
function s2t(s) { return stringIDToTypeID(s); }
function note(s) { LOG.push(s); }
function keyName(k) { return typeIDToStringID(k) || typeIDToCharID(k); }

// A descriptor's values as name=value, objects nested: for photoshop.txt.
function dump(d) {
  var s = [];
  for (var i = 0; i < d.count; i++) {
    var k = d.getKey(i), t = d.getType(k), v;
    if (t == DescValueType.BOOLEANTYPE) v = d.getBoolean(k);
    else if (t == DescValueType.INTEGERTYPE) v = d.getInteger(k);
    else if (t == DescValueType.DOUBLETYPE) v = d.getDouble(k);
    else if (t == DescValueType.UNITDOUBLE) v = d.getUnitDoubleValue(k);
    else if (t == DescValueType.STRINGTYPE) v = '"' + d.getString(k) + '"';
    else if (t == DescValueType.ENUMERATEDTYPE) v = keyName(d.getEnumerationValue(k));
    else if (t == DescValueType.OBJECTTYPE) v = '{' + dump(d.getObjectValue(k)) + '}';
    else v = String(t);
    s.push(keyName(k) + '=' + v);
  }
  return s.join(', ');
}

function solid(v) {  // grey v, 0..1
  var c = new SolidColor();
  c.rgb.red = c.rgb.green = c.rgb.blue = v * 255;
  return c;
}

function savePng(doc, out, name) {  // a copy, at the document's depth
  doc.saveAs(new File(out + '/' + name + '.png'), new PNGSaveOptions(), true, Extension.LOWERCASE);
}

// Edit > Color Settings as the composites need them: no dither, RGB blended as encoded (not at gamma 1), no asking
// about profiles. Their keys are matched by name, as versions name them as they please; photoshop.txt gets them all,
// before and after. Returns what puts them back.
function colorSettings() {
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
    before = get(), want = get();
  } catch (e) {
    note('colour settings: cannot read them (' + e + '); set them by hand (README.md)');
    return nothing;
  }
  note('colour settings before: ' + dump(before));
  var flipped = [];
  for (var i = 0; i < want.count; i++) {
    var k = want.getKey(i), name = keyName(k);
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
    note('colour settings: turned off ' + flipped.join(', ') + '; after: ' + dump(get()));
  } catch (e) {
    note('colour settings: cannot turn off ' + flipped.join(', ') + ' (' + e + ')');
    return nothing;
  }
  return function () {
    try {
      set(before);
    } catch (e) {
      note('colour settings: cannot put them back (' + e + '); turn ' + flipped.join(', ') + ' on again by hand');
    }
  };
}

// That Photoshop blends RGB as encoded, as photo-beauty does: grey at 100% and at 50% over black, side by side, read
// from the red channel's histogram (colour samplers would average as the eyedropper's Sample Size says). Blending at
// gamma 1 ("Blend RGB Colors Using Gamma") makes the 50% about 0.36 of white, not half the 100%.
function checkBlending() {
  var doc = app.documents.add(16, 4, 72, 'blend-check', NewDocumentMode.RGB, DocumentFill.WHITE, 1,
                              BitsPerChannelType.SIXTEEN, SRGB);
  doc.selection.selectAll();
  doc.selection.fill(solid(0));
  doc.artLayers.add();
  doc.selection.select([[0, 0], [8, 0], [8, 4], [0, 4]]);
  doc.selection.fill(solid(0.5));
  doc.artLayers.add().opacity = 50;
  doc.selection.select([[8, 0], [16, 0], [16, 4], [8, 4]]);
  doc.selection.fill(solid(0.5));
  doc.selection.deselect();
  doc.flatten();
  var h = doc.channels[0].histogram, levels = [];  // of 255: the 50%'s, the 100%'s
  for (var i = 0; i < 256; i++)
    if (h[i]) levels.push(i);
  doc.close(SaveOptions.DONOTSAVECHANGES);
  note('blending: grey over black at 50% and 100% comes out ' + levels.join(' and ') + ' of 255');
  if (levels.length != 2 || Math.abs(levels[0] - levels[1] / 2) > 2)
    throw new Error('Photoshop blends RGB at gamma 1: uncheck Blend RGB Colors Using Gamma in Edit > Color Settings');
}

// An input as Photoshop must have it: RGB, 16 bits, its pixels as in the file (it has no profile, so opening converts
// nothing; normal-100.png would show it), sRGB assigned.
function open16(file) {
  var doc = app.open(file);
  if (doc.mode != DocumentMode.RGB || doc.bitsPerChannel != BitsPerChannelType.SIXTEEN)
    throw new Error(file.name + ' opened as ' + doc.mode + ', ' + doc.bitsPerChannel + ', not RGB at 16 bits');
  doc.colorProfileName = SRGB;  // assigned, not converted
  var layer = doc.artLayers[0];
  if (layer.isBackgroundLayer) layer.isBackgroundLayer = false;
  note(file.name + ': ' + doc.width.as('px') + ' x ' + doc.height.as('px') + ', ' + doc.bitsPerChannel + ', ' +
       doc.colorProfileName);
  return doc;
}

// Top over base, top in each mode at 100 and 50% (its fill 100%): <mode>-<opacity>.png.
function blends(dir, out) {
  var base = open16(new File(dir + '/base.png')), top = open16(new File(dir + '/top.png'));
  app.activeDocument = top;
  var layer = top.artLayers[0].duplicate(base, ElementPlacement.PLACEATBEGINNING);
  top.close(SaveOptions.DONOTSAVECHANGES);
  app.activeDocument = base;
  if (base.layers.length != 2) throw new Error('base.png with top.png has ' + base.layers.length + ' layers, not 2');
  layer.fillOpacity = 100;
  for (var i = 0; i < MODES.length; i++)
    for (var o = 100; o >= 50; o -= 50) {
      layer.blendMode = MODES[i][1];
      layer.opacity = o;
      savePng(base, out, MODES[i][0] + '-' + o);
    }
  base.close(SaveOptions.DONOTSAVECHANGES);
  var check = app.open(new File(out + '/normal-100.png')), bits = check.bitsPerChannel;
  check.close(SaveOptions.DONOTSAVECHANGES);
  if (bits != BitsPerChannelType.SIXTEEN) throw new Error('Photoshop saved the PNGs at ' + bits + ', not 16 bits');
  note(MODES.length * 2 + ' composites, 16 bits');
}

// Single dabs of the round brush, black on white at 100% opacity and flow, no dynamics: a row per size, a column per
// hardness, each centred in its 128 px cell (test_ps_golden_dabs reads them so): dabs.png, RGB, 16 bits. A dab is
// a path of one point stroked with the brush.
function dabs(out) {
  var sizes = [5, 19, 100], hardness = [0, 50, 90, 100], cell = 128;
  var doc = app.documents.add(cell * hardness.length, cell * sizes.length, 72, 'dabs', NewDocumentMode.RGB,
                              DocumentFill.WHITE, 1, BitsPerChannelType.SIXTEEN, SRGB);
  var toolOptions = function () {
    var r = new ActionReference();
    r.putProperty(c2t('Prpr'), c2t('CrnT'));
    r.putEnumerated(c2t('capp'), c2t('Ordn'), c2t('Trgt'));
    return r;
  };
  var d = new ActionDescriptor(), r = new ActionReference();
  r.putClass(s2t('paintbrushTool'));
  d.putReference(c2t('null'), r);
  executeAction(c2t('slct'), d, DialogModes.NO);
  try {
    d = new ActionDescriptor();
    d.putReference(c2t('null'), toolOptions());
    executeAction(c2t('Rset'), d, DialogModes.NO);  // Reset Tool
  } catch (e) {
    note('brush: no Reset Tool (' + e + ')');
  }
  // What Reset Tool should have left, made sure of: 100% opacity and flow, Normal, the brush's dynamics off.
  var opts = executeActionGet(toolOptions()).getObjectValue(c2t('CrnT')), set = [];
  var dynamics = new RegExp('^(use(TipDynamics|Scatter|Texture|DualBrush|ColorDynamics|PaintDynamics|BrushPose)|' +
                            'dualBrush|noise|wetEdges|repeat)$');
  for (var i = 0; i < opts.count; i++) {
    var k = opts.getKey(i), name = keyName(k), t = opts.getType(k), full = /^(opacity|flow)$/.test(name);
    if (t == DescValueType.BOOLEANTYPE && opts.getBoolean(k) && dynamics.test(name)) opts.putBoolean(k, false);
    else if (full && t == DescValueType.INTEGERTYPE && opts.getInteger(k) != 100) opts.putInteger(k, 100);
    else if (full && t == DescValueType.UNITDOUBLE && opts.getUnitDoubleValue(k) != 100)
      opts.putUnitDouble(k, opts.getUnitDoubleType(k), 100);
    else if (name == 'mode' && t == DescValueType.ENUMERATEDTYPE && keyName(opts.getEnumerationValue(k)) != 'normal')
      opts.putEnumerated(k, opts.getEnumerationType(k), s2t('normal'));
    else continue;
    set.push(name);
  }
  if (set.length) {
    d = new ActionDescriptor(), r = new ActionReference();
    r.putClass(s2t('paintbrushTool'));
    d.putReference(c2t('null'), r);
    d.putObject(c2t('T   '), c2t('null'), opts);
    executeAction(c2t('setd'), d, DialogModes.NO);
    note('brush: set ' + set.join(', '));
  }
  app.foregroundColor = solid(0);
  for (var j = 0; j < sizes.length; j++)
    for (i = 0; i < hardness.length; i++) {
      var b = new ActionDescriptor();
      b.putUnitDouble(c2t('Dmtr'), c2t('#Pxl'), sizes[j]);
      b.putUnitDouble(c2t('Hrdn'), c2t('#Prc'), hardness[i]);
      b.putUnitDouble(c2t('Angl'), c2t('#Ang'), 0);
      b.putUnitDouble(c2t('Rndn'), c2t('#Prc'), 100);
      b.putUnitDouble(c2t('Spcn'), c2t('#Prc'), 25);
      b.putBoolean(c2t('Intr'), true);
      b.putBoolean(s2t('flipX'), false);
      b.putBoolean(s2t('flipY'), false);
      d = new ActionDescriptor(), r = new ActionReference();
      r.putEnumerated(c2t('Brsh'), c2t('Ordn'), c2t('Trgt'));
      d.putReference(c2t('null'), r);
      d.putObject(c2t('T   '), s2t('computedBrush'), b);
      executeAction(c2t('setd'), d, DialogModes.NO);
      var x = cell * i + cell / 2, y = cell * j + cell / 2, p = new PathPointInfo(), s = new SubPathInfo();
      p.kind = PointKind.CORNERPOINT;
      p.anchor = [x, y], p.leftDirection = [x, y], p.rightDirection = [x, y];
      s.closed = false, s.operation = ShapeOperation.SHAPEADD, s.entireSubPath = [p];
      var path = doc.pathItems.add('dab', [s]), white = doc.histogram[255];
      path.strokePath(ToolType.BRUSH, false);
      path.remove();
      if (doc.histogram[255] == white) throw new Error('no dab at ' + sizes[j] + ' px, hardness ' + hardness[i] + '%');
    }
  note('brush: ' + dump(executeActionGet(toolOptions()).getObjectValue(c2t('CrnT'))));
  savePng(doc, out, 'dabs');
  doc.close(SaveOptions.DONOTSAVECHANGES);
  note(sizes.length * hardness.length + ' dabs');
}

function main() {
  var dir = new File($.fileName).parent;
  if (!new File(dir + '/base.png').exists || !new File(dir + '/top.png').exists) {
    dir = Folder.selectDialog('The folder with base.png and top.png (pb-bench ps-inputs)');
    if (!dir) return;
  }
  var out = new Folder(dir + '/out');
  if (!out.exists) out.create();
  var units = app.preferences.rulerUnits, dialogs = app.displayDialogs, fg = app.foregroundColor, error = null;
  app.preferences.rulerUnits = Units.PIXELS;  // and documents at 72 ppi: path points are pixels too
  app.displayDialogs = DialogModes.NO;
  note('Photoshop ' + app.version + ', ' + $.os + ', ' + new Date());
  var restore = function () {};
  try {
    restore = colorSettings();
    checkBlending();
    blends(dir, out);
    dabs(out);
  } catch (e) {
    error = e;
    note('STOPPED: ' + e + (e.line ? ' (blend.jsx line ' + e.line + ')' : ''));
  }
  restore();
  app.preferences.rulerUnits = units;
  app.displayDialogs = dialogs;
  app.foregroundColor = fg;
  var f = new File(out + '/photoshop.txt');
  f.encoding = 'UTF-8';
  f.lineFeed = 'Unix';
  f.open('w');
  f.write(LOG.join('\n') + '\n');
  f.close();
  alert(error ? 'Stopped: ' + error + '\n\nDetails: ' + f.fsName : 'Done: ' + out.fsName);
}

main();
