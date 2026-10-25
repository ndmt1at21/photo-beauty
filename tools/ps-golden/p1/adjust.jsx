// Photoshop's images of its adjustments (D32), for D33-D41 and D40: each of the 16 adjustment layers on cube.png (the
// 33^3 lattice), ramp.png (the ramps: tone curves at 256 levels) or photo.png, a few sets of values each; the same
// as Image > Adjustments on a layer and the commands (Desaturate, Equalize, Auto Tone, Auto Contrast, Auto Color);
// adjustment layers in Luminosity and at 50%; Auto of Levels, Curves and Brightness/Contrast (their values read back).
// README-adjust.md next to this folder has the steps. Writes out/adjust/ next to this script: <case>.png (the 16-bit
// document's composite made 8 bits, no dither), index.json (each case as photo-beauty's scene, vals by AdjustSpec's
// keys: tests/golden.h reads it), photoshop.txt, and presets.json (Photoshop's presets of Levels, Curves,
// Hue/Saturation, Black & White and Exposure, read back from the files of its Presets folder; each adjustment's values
// as Photoshop makes it; Photo Filter's colours). Inputs from in/ (tests/golden/ps/p1/in), the LUTs of Color Lookup
// from lut/ (tests/data/lut). 137 images, about 2.1 MB, 7 minutes. Not run in Photoshop yet: README-adjust.md says
// what is unsure. gradient.jsx #includes this script for what it shares (D32_GRADIENT set: the cases here do not run).
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
#include "lib.jsx"

var A = {};  // this script's (and gradient.jsx's)
A.LUTS = 'lut';  // the LUT files' folder, next to this script

// ---- Small things (psd.jsx's E.or, E.merge, E.rgb3, E.rgbc) -------------------------------------------------------

A.or = function (v, d) { return v === undefined ? d : v; };
A.merge = function (o, more) {
  if (more)
    for (var k in more)
      if (more.hasOwnProperty(k)) o[k] = more[k];
  return o;
};
A.HEX = /^#[0-9a-fA-F]{6}$/;
A.rgb3 = function (hex) {  // '#rrggbb' as [r, g, b], 0..255
  if (typeof hex != 'string' || !A.HEX.test(hex)) throw new Error('a colour ' + hex);
  var v = parseInt(hex.slice(1), 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
};
A.rgbc = function (c) {  // [r, g, b] (0..255, fractions kept) or '#rrggbb' as Photoshop's RGBC
  var v = typeof c == 'string' ? A.rgb3(c) : c, d = new ActionDescriptor();
  d.putDouble(PB.c2t('Rd  '), v[0]);
  d.putDouble(PB.c2t('Grn '), v[1]);
  d.putDouble(PB.c2t('Bl  '), v[2]);
  return d;
};
A.hex = function (rgb) {  // [r, g, b] 0..255 as '#rrggbb'
  var s = '#';
  for (var i = 0; i < 3; i++) s += ('0' + Math.max(0, Math.min(255, Math.round(rgb[i]))).toString(16)).slice(-2);
  return s;
};
A.id = function (s) { return s.length == 4 ? PB.c2t(s) : PB.s2t(s); };
A.is = function (typeId, s) { return typeId == A.id(s); };

// ---- Gradients: D30's JSON as Photoshop's Grdn (psd.jsx's E.gradient) ----------------------------------------------

// {colors: [{p, c: [r, g, b], m, type}], alphas: [{p, a, m}], smoothness, name}: Lctn is p in 4096ths, Mdpn the stop's
// own m in % (D30 reads m as the midpoint from that stop to the next; which segment Photoshop's Mdpn belongs to is one
// of the things the images settle: the midpoint cases give every stop a different m, the last one too), Intr the
// smoothness in 4096ths. A stop of type fg or bg is Photoshop's Foreground or Background Color stop.
A.gradient = function (g) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), i, o, s;
  d.putString(c('Nm  '), g.name || 'Custom');
  d.putEnumerated(c('GrdF'), c('GrdF'), c('CstS'));
  d.putDouble(c('Intr'), A.or(g.smoothness, 100) * 40.96);
  for (i = 0; i < g.colors.length; i++) {
    s = g.colors[i], o = new ActionDescriptor();
    if (s.type == 'fg' || s.type == 'bg') {
      o.putEnumerated(c('Type'), c('Clry'), c(s.type == 'fg' ? 'FrgC' : 'BckC'));
    } else {
      o.putObject(c('Clr '), c('RGBC'), A.rgbc(s.c));
      o.putEnumerated(c('Type'), c('Clry'), c('UsrS'));
    }
    o.putInteger(c('Lctn'), Math.round(s.p * 4096));
    o.putInteger(c('Mdpn'), Math.round(A.or(s.m, 0.5) * 100));
    list.putObject(c('Clrt'), o);
  }
  d.putList(c('Clrs'), list);
  var alphas = g.alphas || [{p: 0, a: 1}, {p: 1, a: 1}];
  list = new ActionList();
  for (i = 0; i < alphas.length; i++) {
    o = new ActionDescriptor();
    o.putUnitDouble(c('Opct'), c('#Prc'), alphas[i].a * 100);
    o.putInteger(c('Lctn'), Math.round(alphas[i].p * 4096));
    o.putInteger(c('Mdpn'), Math.round(A.or(alphas[i].m, 0.5) * 100));
    list.putObject(c('TrnS'), o);
  }
  d.putList(c('Trns'), list);
  return d;
};

// Photoshop 2023's gradient Method (gradientsInterpolationMethod of type gradientInterpolationMethodType): which id
// each of D30's five names has is not sure (stringIDs perceptual, linear, classic, smooth, stripes as ScriptListener
// is said to record them; psd.jsx used charIDs Perc, Lnr , Gcls, Smoo, Stri), so A.probeMethods() tries them on a
// Gradient fill layer once and keeps those Photoshop reads back as given.
A.METHODS = {perceptual: ['perceptual', 'Perc'], linear: ['linear', 'Lnr '], classic: ['classic', 'Gcls'],
             smooth: ['smooth', 'Smoo'], stripes: ['stripes', 'Stri']};
A.methodIds = null;  // method -> its id, as A.probeMethods() found; null before it ran (jscheck: never)
A.methodOf = function (name) {
  var m = String(name || 'perceptual').toLowerCase();
  if (!A.METHODS[m]) throw new Error('no gradient method ' + name);
  return m;
};
A.putMethod = function (d, name) {
  var m = A.methodOf(name), id = A.methodIds ? A.methodIds[m] : A.id(A.METHODS[m][0]);
  if (id === undefined || id === null) throw new Error('gradient method ' + m + ': no id of it worked (photoshop.txt)');
  d.putEnumerated(PB.s2t('gradientsInterpolationMethod'), PB.s2t('gradientInterpolationMethodType'), id);
};
A.probeMethods = function () {
  if (A.methodIds) return;
  A.methodIds = {};
  var doc = app.documents.add(16, 16, 72, 'method-check', NewDocumentMode.RGB, DocumentFill.TRANSPARENT, 1,
                              BitsPerChannelType.SIXTEEN, PB.SRGB);
  var g = {colors: [{p: 0, c: [0, 0, 0]}, {p: 1, c: [255, 255, 255]}]};
  try {
    for (var m in A.METHODS) {
      if (!A.METHODS.hasOwnProperty(m)) continue;
      var tried = [];
      for (var i = 0; i < A.METHODS[m].length && A.methodIds[m] === undefined; i++) {
        var id = A.id(A.METHODS[m][i]);
        try {
          var t = new ActionDescriptor();
          t.putObject(PB.c2t('Grad'), PB.c2t('Grdn'), A.gradient(g));
          t.putEnumerated(PB.s2t('gradientsInterpolationMethod'), PB.s2t('gradientInterpolationMethodType'), id);
          A.makeContent(PB.s2t('gradientLayer'), t);
          var got = A.contentOf(doc, doc.activeLayer), key = PB.s2t('gradientsInterpolationMethod');
          var back = got && got.hasKey(key) ? got.getEnumerationValue(key) : null;
          tried.push(A.METHODS[m][i] + ' read back as ' + (back === null ? 'nothing' : PB.keyName(back)));
          if (back == id) A.methodIds[m] = id;
          doc.activeLayer.remove();
        } catch (e) {
          tried.push(A.METHODS[m][i] + ': ' + e);
        }
      }
      PB.note('gradient method ' + m + ': ' + tried.join('; ') + (A.methodIds[m] === undefined ? ': none worked' : ''));
      if (A.methodIds[m] === undefined) A.methodIds[m] = null;
    }
  } finally {
    PB.close(doc);
  }
};
// A fill layer of class cls (gradientLayer, solidColorLayer) with descriptor t, made above the active layer.
A.makeContent = function (cls, t) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor(), r = new ActionReference(), use = new ActionDescriptor();
  r.putClass(s('contentLayer'));
  d.putReference(c('null'), r);
  use.putObject(c('Type'), cls, t);
  d.putObject(c('Usng'), s('contentLayer'), use);
  executeAction(c('Mk  '), d, DialogModes.NO);
};

// ---- Adjustments: PB.adjustments by AdjustSpec.ORDER name (psd.jsx's, Photo Filter and Color Lookup added to) -----

// vals as index.json has them: AdjustSpec's keys, those the case sets; where Photoshop keeps a set of values for each
// channel, range, tone or colour and AdjustSpec one set and a selector, an object for each by the selector's name
// (psd.jsx's, README-psd.md): Levels {RGB, Red, Green, Blue: {in0, gam, in1, out0, out1}}; Curves pts {RGB, ...:
// [[in, out], ...]}; Hue/Saturation {Master, Reds ... Magentas: {h, s, l, span}, colorize} (with Colorize, Master's h
// 0..360, s and l are the colour); Color Balance {Shadows, Midtones, Highlights: {cr, mg, yb}, pl}; Channel Mixer {Red,
// Green, Blue: {r, g, b, k}}, or mono and {Gray: {...}}; Selective Color {Reds ... Blacks: {c, m, y, k}, rel (1:
// Absolute)}. The rest as AdjustSpec: Brightness/Contrast b, c, legacy; Exposure e, o, g; Vibrance v, s; Black & White
// r, y, g, c, b, m, tint, tintColor; Photo Filter pf (0: the filter pf0, 1: the colour pf1), d, pl; Color Lookup lf0
// (a file of lut/: tests/data/lut), dither; Posterize lv; Threshold t; Gradient Map gradient (D30's JSON), method,
// dither, rev.
A.preset = function (d) {
  d.putEnumerated(PB.s2t('presetKind'), PB.s2t('presetKindType'), PB.s2t('presetKindCustom'));
};
A.CHANNELS = [['RGB', 'Cmps'], ['Red', 'Rd  '], ['Green', 'Grn '], ['Blue', 'Bl  ']];
A.channelRef = function (id) {
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
  A.preset(d);
  for (var i = 0; i < A.CHANNELS.length; i++) {
    var ch = v[A.CHANNELS[i][0]];
    if (!ch) continue;
    var a = new ActionDescriptor(), into = new ActionList(), out = new ActionList();
    a.putReference(c('Chnl'), A.channelRef(A.CHANNELS[i][1]));
    into.putInteger(A.or(ch.in0, 0));
    into.putInteger(A.or(ch.in1, 255));
    a.putList(c('Inpt'), into);
    a.putDouble(c('Gmm '), A.or(ch.gam, 1));
    out.putInteger(A.or(ch.out0, 0));
    out.putInteger(A.or(ch.out1, 255));
    a.putList(c('Otpt'), out);
    list.putObject(c('LvlA'), a);
  }
  d.putList(c('Adjs'), list);
  return [c('Lvls'), d];
};
PB.adjustments.Curves = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), pts = v.pts || {};
  A.preset(d);
  for (var i = 0; i < A.CHANNELS.length; i++) {
    var p = pts[A.CHANNELS[i][0]];
    if (!p) continue;
    var a = new ActionDescriptor(), curve = new ActionList();
    a.putReference(c('Chnl'), A.channelRef(A.CHANNELS[i][1]));
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
  A.preset(d);
  d.putDouble(c('Exps'), A.or(v.e, 0));
  d.putDouble(c('Ofst'), A.or(v.o, 0));
  d.putDouble(PB.s2t('gammaCorrection'), A.or(v.g, 1));
  return [c('Exps'), d];
};
PB.adjustments.Vibrance = function (v) {
  var d = new ActionDescriptor();
  d.putInteger(PB.s2t('vibrance'), A.or(v.v, 0));
  d.putInteger(PB.c2t('Strt'), A.or(v.s, 0));
  return [PB.s2t('vibrance'), d];
};
A.HUES = ['Reds', 'Yellows', 'Greens', 'Cyans', 'Blues', 'Magentas'];  // LclR 1..6
A.SPANS = {Reds: [315, 345, 15, 45], Yellows: [15, 45, 75, 105], Greens: [75, 105, 135, 165],
           Cyans: [135, 165, 195, 225], Blues: [195, 225, 255, 285], Magentas: [255, 285, 315, 345]};  // AdjustSpec's
PB.adjustments['Hue/Saturation'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), m = v.Master || {}, a, i;
  A.preset(d);
  d.putBoolean(c('Clrz'), !!v.colorize);
  a = new ActionDescriptor();  // Master; with Colorize its hue 0..360 and saturation 0..100 are the colour's
  a.putInteger(c('H   '), A.or(m.h, 0));
  a.putInteger(c('Strt'), A.or(m.s, 0));
  a.putInteger(c('Lght'), A.or(m.l, 0));
  list.putObject(c('Hst2'), a);
  for (i = 0; i < A.HUES.length; i++) {
    var r = v[A.HUES[i]];
    if (!r) continue;
    var span = r.span || A.SPANS[A.HUES[i]];
    a = new ActionDescriptor();
    a.putInteger(c('LclR'), i + 1);
    a.putInteger(c('BgnR'), span[0]);
    a.putInteger(c('BgnS'), span[1]);
    a.putInteger(c('EndS'), span[2]);
    a.putInteger(c('EndR'), span[3]);
    a.putInteger(c('H   '), A.or(r.h, 0));
    a.putInteger(c('Strt'), A.or(r.s, 0));
    a.putInteger(c('Lght'), A.or(r.l, 0));
    list.putObject(c('Hst2'), a);
  }
  d.putList(c('Adjs'), list);
  return [c('HStr'), d];
};
A.TONES = [['Shadows', 'ShdL'], ['Midtones', 'MdtL'], ['Highlights', 'HghL']];
PB.adjustments['Color Balance'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  for (var i = 0; i < A.TONES.length; i++) {
    var t = v[A.TONES[i][0]] || {}, list = new ActionList();
    list.putInteger(A.or(t.cr, 0));
    list.putInteger(A.or(t.mg, 0));
    list.putInteger(A.or(t.yb, 0));
    d.putList(c(A.TONES[i][1]), list);
  }
  d.putBoolean(c('PrsL'), v.pl !== false);
  return [c('ClrB'), d];
};
A.BW = [['r', 'Rd  ', 40], ['y', 'Yllw', 60], ['g', 'Grn ', 40], ['c', 'Cyn ', 60], ['b', 'Bl  ', 20], ['m', 'Mgnt', 80]];
A.TINT = '#c8a27c';  // AdjustSpec's TINT colour
PB.adjustments['Black & White'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  A.preset(d);
  for (var i = 0; i < A.BW.length; i++) d.putInteger(c(A.BW[i][1]), A.or(v[A.BW[i][0]], A.BW[i][2]));
  d.putBoolean(PB.s2t('useTint'), !!v.tint);
  d.putObject(PB.s2t('tintColor'), c('RGBC'), A.rgbc(v.tintColor || A.TINT));
  return [c('BanW'), d];
};
// Photo Filter's filters (AdjustSpec.LISTS.pfilter) as their colours are commonly published (sRGB), not read from
// Photoshop: scripting cannot pick a filter by name. A case with pf 0 and the default filter (Warming Filter (85))
// gives Photoshop no colour, so Photoshop's own default comes out and is read back (presets.json, photoshop.txt);
// any other filter of pf 0 is made with the colour here.
A.FILTERS = {'Warming Filter (85)': '#ec8a00', 'Warming Filter (LBA)': '#fa9600', 'Warming Filter (81)': '#ebb113',
             'Cooling Filter (80)': '#006dff', 'Cooling Filter (LBB)': '#005dff', 'Cooling Filter (82)': '#00b5ff',
             'Red': '#ea1a1a', 'Orange': '#f38417', 'Yellow': '#f9e31c', 'Green': '#19c919', 'Cyan': '#1dcbea',
             'Blue': '#1d35ea', 'Violet': '#9b1dea', 'Magenta': '#e318e3', 'Sepia': '#ac7a33', 'Underwater': '#00c1b1'};
A.FILTER = 'Warming Filter (85)';  // AdjustSpec's default filter
A.filterColour = function (v) {  // the colour the case gives Photoshop, null for Photoshop's default filter
  if (A.or(v.pf, 0) === 1) return A.or(v.pf1, '#ec8a00');
  var name = A.or(v.pf0, A.FILTER);
  if (name == A.FILTER) return null;
  if (!A.FILTERS[name]) throw new Error('Photo Filter: no filter ' + name);
  return A.FILTERS[name];
};
PB.adjustments['Photo Filter'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), colour = A.filterColour(v);
  if (colour) d.putObject(c('Clr '), c('RGBC'), A.rgbc(colour));
  d.putInteger(c('Dnst'), A.or(v.d, 25));
  d.putBoolean(c('PrsL'), v.pl !== false);
  return [PB.s2t('photoFilter'), d];
};
A.MIXER = [['Red', 'Rd  ', [100, 0, 0]], ['Green', 'Grn ', [0, 100, 0]], ['Blue', 'Bl  ', [0, 0, 100]],
           ['Gray', 'Gry ', [100, 0, 0]]];
PB.adjustments['Channel Mixer'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  A.preset(d);
  d.putBoolean(c('Mnch'), !!v.mono);
  for (var i = v.mono ? 3 : 0; i < (v.mono ? 4 : 3); i++) {
    var o = v[A.MIXER[i][0]] || {}, base = A.MIXER[i][2], m = new ActionDescriptor();
    m.putUnitDouble(c('Rd  '), c('#Prc'), A.or(o.r, base[0]));
    m.putUnitDouble(c('Grn '), c('#Prc'), A.or(o.g, base[1]));
    m.putUnitDouble(c('Bl  '), c('#Prc'), A.or(o.b, base[2]));
    m.putUnitDouble(c('Cnst'), c('#Prc'), A.or(o.k, 0));
    d.putObject(c(A.MIXER[i][1]), c('ChMx'), m);
  }
  return [c('ChnM'), d];
};
// Color Lookup: a 3D LUT file of lut/ (vals.lf0), its bytes embedded as the dialog does when it loads one, its format
// by its extension. The file is read when the layer is made, not in jscheck.
A.LUT_FORMATS = {cube: 'LUTFormatCUBE', '3dl': 'LUTFormat3DL', look: 'LUTFormatLOOK', csp: 'LUTFormatCSP'};
A.lutFile = function (name) {
  var f = new File(HERE + '/' + A.LUTS + '/' + name);
  if (!f.exists) throw new Error('no LUT ' + f.fsName + ' (copy tests/data/lut, README-adjust.md)');
  return f;
};
A.bytes = function (f) {  // a file's bytes as a string
  f.encoding = 'BINARY';
  if (!f.open('r')) throw new Error('cannot read ' + f.fsName);
  var s = f.read();
  f.close();
  return s;
};
PB.adjustments['Color Lookup'] = function (v) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor(), name = String(v.lf0 || '');
  var ext = name.replace(/^.*\./, '').toLowerCase(), f = A.lutFile(name);
  if (!A.LUT_FORMATS[ext]) throw new Error('Color Lookup: no format for ' + name);
  d.putEnumerated(s('lookupType'), s('colorLookupType'), s('3DLUT'));
  d.putString(c('Nm  '), f.fsName);
  d.putBoolean(c('Dthr'), !!v.dither);
  d.putEnumerated(s('LUTFormat'), s('LUTFormatType'), s(A.LUT_FORMATS[ext]));
  d.putData(s('LUT3DFileData'), A.bytes(f));
  d.putString(s('LUT3DFileName'), f.fsName);
  return [s('colorLookup'), d];
};
PB.adjustments.Posterize = function (v) {
  var d = new ActionDescriptor();
  d.putInteger(PB.c2t('Lvls'), A.or(v.lv, 4));
  return [PB.c2t('Pstr'), d];
};
PB.adjustments.Threshold = function (v) {
  var d = new ActionDescriptor();
  d.putInteger(PB.c2t('Lvl '), A.or(v.t, 128));
  return [PB.c2t('Thrs'), d];
};
PB.adjustments['Gradient Map'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor();
  d.putObject(c('Grad'), c('Grdn'), A.gradient(v.gradient || A.MAP));
  d.putBoolean(c('Rvrs'), !!v.rev);
  d.putBoolean(c('Dthr'), !!v.dither);
  A.putMethod(d, v.method);
  return [c('GdMp'), d];
};
A.MAP = {colors: [{p: 0, c: [27, 16, 32]}, {p: 0.5, c: [158, 47, 69]}, {p: 1, c: [233, 199, 179]}]};  // AdjustSpec's GRAD
A.SELECTIVE = [['Reds', 'Rds '], ['Yellows', 'Ylws'], ['Greens', 'Grns'], ['Cyans', 'Cyns'], ['Blues', 'Bls '],
               ['Magentas', 'Mgnt'], ['Whites', 'Whts'], ['Neutrals', 'Ntrl'], ['Blacks', 'Blks']];
PB.adjustments['Selective Color'] = function (v) {
  var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList();
  A.preset(d);
  for (var i = 0; i < A.SELECTIVE.length; i++) {
    var o = v[A.SELECTIVE[i][0]];
    if (!o) continue;
    var a = new ActionDescriptor();
    a.putEnumerated(c('Clrs'), c('Clrs'), c(A.SELECTIVE[i][1]));
    a.putUnitDouble(c('Cyn '), c('#Prc'), A.or(o.c, 0));
    a.putUnitDouble(c('Mgnt'), c('#Prc'), A.or(o.m, 0));
    a.putUnitDouble(c('Yllw'), c('#Prc'), A.or(o.y, 0));
    a.putUnitDouble(c('Blck'), c('#Prc'), A.or(o.k, 0));
    list.putObject(c('ClrC'), a);
  }
  d.putList(c('ClrC'), list);
  d.putEnumerated(c('Mthd'), c('CrcM'), c(v.rel ? 'Absl' : 'Rltv'));
  return [c('SlcC'), d];
};

// ---- Reading back (psd.jsx's E.layerDesc, E.diff and the rest) ---------------------------------------------------

// A layer's descriptor, by its id without changing the active layer; else as the active layer.
A.layerDesc = function (doc, layer) {
  try {
    var r = new ActionReference();
    r.putIdentifier(PB.c2t('Lyr '), layer.id);
    return executeActionGet(r);
  } catch (e) {
    doc.activeLayer = layer;
    return PB.activeLayerDesc();
  }
};
// An adjustment or fill layer's descriptor as Photoshop has it (its `adjustment`), null for none.
A.contentOf = function (doc, layer) {
  var d = A.layerDesc(doc, layer), key = PB.s2t('adjustment');
  if (!d.hasKey(key) || d.getType(key) != DescValueType.LISTTYPE || !d.getList(key).count) return null;
  return d.getList(key).getObjectValue(0);
};
A.value = function (from, k, t) {  // a value of a descriptor (k a key) or list (k an index) as plain data
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
A.turn = function (w, d) {  // a difference of angles (#Ang) within -180..180: Photoshop may keep -60 as 300
  return w && w.unit == PB.c2t('#Ang') ? ((d % 360) + 540) % 360 - 180 : d;
};
A.numberOf = function (v) { return typeof v == 'number' ? v : v && v.unit !== undefined ? v.v : null; };
A.show = function (v) {
  if (v === undefined || v === null || typeof v != 'object') return String(v);
  if (v.unit !== undefined) return v.v + PB.keyName(v.unit);
  if (v.enumerated !== undefined) return PB.keyName(v.enumerated);
  if (v.object) return '{' + PB.dump(v.object) + '}';
  if (v.list) return 'a list of ' + v.list.count;
  return v.other || 'a ' + (v.reference ? 'reference' : 'value');
};
A.tolerance = function (w, key, v) {
  if (w.unit !== undefined) return w.unit == PB.c2t('#Pxl') ? 0.05 : 0.5;  // % and degrees as the dialogs show them
  if (key !== null && (key == PB.c2t('Rd  ') || key == PB.c2t('Grn ') || key == PB.c2t('Bl  '))) return 0.6;  // a colour
  return 1e-3 + 1e-4 * Math.abs(v);
};
// Each value of want (what this script gave) that got (Photoshop's, read back) has otherwise: lines into out. Keys
// got lacks go to absent (noted, not wrong: Photoshop leaves out some it was given), so do strings that differ (names:
// a gradient's, the LUT file's) and raw data. A list of objects matches in any order; one of values in order; an empty
// one anything. presetKind is not compared (Photoshop may say Default where nothing changed); a midpoint (Mdpn) that
// differs is noted, not wrong: Photoshop may set the one of an end stop it does not use, which the midpoint cases ask.
A.diff = function (want, got, at, out, absent) {
  for (var i = 0; i < want.count; i++) {
    var k = want.getKey(i), name = at + PB.keyName(k), soft = [];
    if (k == PB.s2t('presetKind')) continue;
    if (!got.hasKey(k)) {
      absent.push(name);
      continue;
    }
    A.compare(A.value(want, k, want.getType(k)), A.value(got, k, got.getType(k)), name, k,
              k == PB.c2t('Mdpn') ? soft : out, absent);
    for (var j = 0; j < soft.length; j++) absent.push(soft[j]);
  }
};
A.compare = function (w, g, name, key, out, absent) {
  var wn = A.numberOf(w), gn = A.numberOf(g);
  if (wn !== null) {
    if (gn === null) out.push(name + ' ' + A.show(g) + ', not ' + A.show(w));
    else if (w.unit !== undefined && g.unit !== undefined && w.unit != g.unit) out.push(name + ' ' + A.show(g) + ', not ' + A.show(w));
    else if (Math.abs(A.turn(w, wn - gn)) > A.tolerance(w, key, wn)) out.push(name + ' ' + gn + ', not ' + wn);
  } else if (typeof w != 'object') {
    if (w !== g && typeof w == 'string' && typeof g == 'string') absent.push(name + ' read back as "' + g + '"');
    else if (w !== g) out.push(name + ' ' + A.show(g) + ', not ' + A.show(w));
  } else if (w.enumerated !== undefined) {
    if (!g || g.enumerated != w.enumerated) out.push(name + ' ' + A.show(g) + ', not ' + A.show(w));
  } else if (w.object) {
    if (!g || !g.object) out.push(name + ' ' + A.show(g) + ', not an object');
    else A.diff(w.object, g.object, name + '.', out, absent);
  } else if (w.list) {
    if (!g || !g.list) out.push(name + ' ' + A.show(g) + ', not a list');
    else A.diffList(w.list, g.list, name, out, absent);
  } else if (w.reference) {
    try {
      if (!g || !g.reference) out.push(name + ' ' + A.show(g) + ', not a reference');
      else if (w.reference.getEnumeratedValue() != g.reference.getEnumeratedValue())
        out.push(name + ' ' + PB.keyName(g.reference.getEnumeratedValue()) + ', not ' + PB.keyName(w.reference.getEnumeratedValue()));
    } catch (e) {}
  } else if (w.raw !== undefined) {
    if (!g || g.raw === undefined || g.raw != w.raw)
      absent.push(name + ' (' + w.raw.length + ' bytes given, ' + (g && g.raw !== undefined ? g.raw.length + ' read back' : 'none') + ')');
  }
};
A.diffList = function (w, g, name, out, absent) {
  var objects = w.count > 0, i, j;
  if (!w.count) return;
  for (i = 0; i < w.count; i++)
    if (w.getType(i) != DescValueType.OBJECTTYPE) objects = false;
  if (!objects) {
    if (w.count != g.count) {
      out.push(name + ' has ' + g.count + ' values, not ' + w.count);
      return;
    }
    for (i = 0; i < w.count; i++) A.compare(A.value(w, i, w.getType(i)), A.value(g, i, g.getType(i)), name + '[' + i + ']', null, out, absent);
    return;
  }
  var used = [];
  for (i = 0; i < w.count; i++) {
    var found = -1, first = null;
    for (j = 0; j < g.count && found < 0; j++) {
      if (used[j] || g.getType(j) != DescValueType.OBJECTTYPE) continue;
      var lines = [];
      A.diff(w.getObjectValue(i), g.getObjectValue(j), name + '[' + i + '].', lines, []);
      if (!lines.length) found = j;
      else if (!first) first = lines;
    }
    if (found >= 0) {
      used[found] = true;
      A.diff(w.getObjectValue(i), g.getObjectValue(found), name + '[' + i + '].', [], absent);
    } else {
      out.push(name + '[' + i + '] matches none of the ' + g.count + ' read back' + (first ? ' (' + first.join('; ') + ')' : ''));
    }
  }
};
// What this script gave (made: [class, descriptor]) against what the layer has: throws for a value Photoshop has
// otherwise (the case then fails: its image would not be of its values); notes what it did not read back.
A.check = function (doc, layer, made, name) {
  var got = A.contentOf(doc, layer), out = [], absent = [];
  if (!got) throw new Error(name + ': nothing read back');
  A.diff(made[1], got, '', out, absent);
  if (absent.length) PB.note('  ' + name + ': not read back as given: ' + absent.join('; '));
  if (out.length) throw new Error(name + ': Photoshop has ' + out.join('; '));
  return got;
};

// The red, green and blue histograms of the document as shown (a flattened copy), joined: to see that a step changed
// the image when what Photoshop reads back cannot tell (a LUT, a command, Auto).
A.histograms = function (doc) {
  var copy = doc.duplicate('histogram', true), s = [];
  try {
    for (var i = 0; i < 3; i++) s.push(copy.channels[i].histogram.join(','));
  } finally {
    PB.close(copy);
    app.activeDocument = doc;
  }
  return s.join(';');
};
// Throws unless showing `layer` changes the image.
A.changes = function (doc, layer, what) {
  var on = A.histograms(doc);
  layer.visible = false;
  var off = A.histograms(doc);
  layer.visible = true;
  doc.activeLayer = layer;
  if (on == off) throw new Error(what + ' did not change the image');
};

// ---- Each adjustment layer, checked once made (lib.jsx's PB.apply calls PB.layerKeys.adj) ------------------------

// What Photoshop has against what this script gave, then the checks of the adjustment's own (A.after).
PB.layerKeys.adj = function (name, layer, doc, l) {
  var vals = l.props.vals || {}, made = PB.adjustments[name](vals), label = l.props.name + ' (' + name + ')';
  var got = made[1] ? A.check(doc, layer, made, label) : A.contentOf(doc, layer);
  PB.note('  ' + label + ' read back: ' + (got ? PB.dump(got) : 'nothing'));
  if (A.after[name]) A.after[name](got, vals, layer, doc, l);
};
A.after = {};
// A LUT read back: its data or the profile Photoshop makes of it; if neither, set again on the layer made, as the
// Properties panel does when a file is loaded. Then, but for the identity ones, the image must change.
A.after['Color Lookup'] = function (got, vals, layer, doc, l) {
  var s = PB.s2t, has = function (d) { return d && (d.hasKey(s('LUT3DFileData')) || d.hasKey(s('profile'))); };
  if (!has(got)) {
    PB.note('  ' + l.props.name + ': no LUT read back after making the layer; setting it on the layer');
    doc.activeLayer = layer;
    var made = PB.adjustments['Color Lookup'](vals);
    A.setAdjustment(made[0], made[1]);
    got = A.contentOf(doc, layer);
    PB.note('  ' + l.props.name + ' read back: ' + (got ? PB.dump(got) : 'nothing'));
    if (!has(got)) throw new Error(l.props.name + ': Photoshop keeps no LUT (no LUT3DFileData, no profile)');
  }
  if (!/^identity/.test(vals.lf0)) A.changes(doc, layer, 'the LUT ' + vals.lf0);
};
// Photoshop's default filter, when the case gave no colour: its colour read back; vals say it if it is not AdjustSpec's.
A.defaultFilter = null;
A.after['Photo Filter'] = function (got, vals, layer, doc, l) {
  if (A.filterColour(vals) || !got) return;
  var v = A.VALS['Photo Filter'](got);
  A.defaultFilter = v;
  PB.note('  ' + l.props.name + ": Photoshop's default filter: " + PB.json(v));
  var want = A.rgb3(A.FILTERS[A.FILTER]), have = v.pf1 ? A.rgb3(v.pf1) : want;
  for (var i = 0; i < 3; i++)
    if (Math.abs(want[i] - have[i]) > 2) {
      l.props.vals = A.merge(PB.copy(vals), {pf: 1, pf1: v.pf1});  // index.json: the colour that made the image
      PB.note('  ' + l.props.name + ': not ' + A.FILTERS[A.FILTER] + ': vals give the colour read back');
      return;
    }
};

// Sets the active adjustment layer's descriptor (as the Properties panel does).
A.setAdjustment = function (cls, desc) {
  var c = PB.c2t, d = new ActionDescriptor(), r = new ActionReference();
  r.putEnumerated(c('AdjL'), c('Ordn'), c('Trgt'));
  d.putReference(c('null'), r);
  d.putObject(c('T   '), cls, desc);
  executeAction(c('setd'), d, DialogModes.NO);
};

// Layer key auto: the Auto button of the layer's Properties (Levels, Curves, Brightness/Contrast; Photoshop's Auto
// Options as they are, by default Enhance Brightness and Contrast), then the values Photoshop worked out read back into
// the layer's vals (index.json has them; offline it has none). Unsure: the descriptor of Auto (README-adjust.md).
A.AUTO = {
  'Levels': function () {
    var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), a = new ActionDescriptor();
    A.preset(d);
    a.putReference(c('Chnl'), A.channelRef('Cmps'));
    a.putBoolean(c('Auto'), true);
    list.putObject(c('LvlA'), a);
    d.putList(c('Adjs'), list);
    return [c('Lvls'), d];
  },
  'Curves': function () {
    var c = PB.c2t, d = new ActionDescriptor(), list = new ActionList(), a = new ActionDescriptor();
    A.preset(d);
    a.putReference(c('Chnl'), A.channelRef('Cmps'));
    a.putBoolean(c('Auto'), true);
    list.putObject(c('CrvA'), a);
    d.putList(c('Adjs'), list);
    return [c('Crvs'), d];
  },
  'Brightness/Contrast': function () {
    var d = new ActionDescriptor();
    d.putBoolean(PB.c2t('Auto'), true);
    d.putBoolean(PB.s2t('useLegacy'), false);
    return [PB.c2t('BrgC'), d];
  }
};
PB.layerKeys.auto = function (on, layer, doc, l) {
  if (!on) return;
  var name = l.props.adj, make = A.AUTO[name];
  if (l.kind != 'adj' || !make) throw new Error(l.props.name + ': no Auto for ' + name);
  doc.activeLayer = layer;
  var t = make();
  A.setAdjustment(t[0], t[1]);
  var got = A.contentOf(doc, layer);
  PB.note('  ' + l.props.name + ' after Auto: ' + (got ? PB.dump(got) : 'nothing'));
  var vals = got ? A.changed(name, A.VALS[name](got)) : {};
  if (PB.json(vals) == '{}') throw new Error(l.props.name + ': Auto left the defaults (README-adjust.md: the descriptor of Auto)');
  A.changes(doc, layer, 'Auto of ' + name);
  l.props.vals = vals;
};

// Case key apply: [{adj, vals} | {command}], in order, on the pixels of the case's top layer (a pixel layer): Image >
// Adjustments > adj with those values, or a command of A.COMMANDS (Image > Adjustments > Desaturate, Equalize; Image >
// Auto Tone, Auto Contrast, Auto Color, with Photoshop's Auto Options as they are). Each must change the layer.
A.COMMANDS = {
  desaturate: function (layer) { layer.desaturate(); },
  equalize: function (layer) { layer.equalize(); },
  autoTone: function (layer) { layer.autoLevels(); },
  autoContrast: function (layer) { layer.autoContrast(); },
  autoColor: function () {
    var d = new ActionDescriptor();
    d.putBoolean(PB.s2t('autoBlackWhite'), true);
    d.putBoolean(PB.s2t('autoNeutrals'), true);
    executeAction(PB.c2t('Lvls'), d, DialogModes.NO);
  }
};
PB.steps.apply = function (list, doc, built) {
  var top = built[built.length - 1];
  if (!top || top.json.kind != 'pixel') throw new Error('apply: the top layer is not a pixel layer');
  for (var i = 0; i < list.length; i++) {
    var step = list[i], before = A.histograms(doc), what;
    doc.activeLayer = top.layer;
    if (step.adj) {
      var t = PB.adjustments[step.adj](step.vals || {});
      executeAction(t[0], t[1] || undefined, DialogModes.NO);
      what = 'Image > Adjustments > ' + step.adj + ' ' + PB.json(step.vals || {});
    } else if (A.COMMANDS[step.command]) {
      A.COMMANDS[step.command](top.layer);
      what = step.command;
    } else {
      throw new Error('apply: no step ' + PB.json(step));
    }
    PB.note('  ' + what + ' on ' + top.json.props.name);
    if (A.histograms(doc) == before) throw new Error(what + ' did not change the image');
  }
};

// ---- Values from descriptors: vals by AdjustSpec's keys of what Photoshop reads back -------------------------------

A.get = function (d, key) {  // d's number, switch, string or enumeration (its name) at key; undefined when absent
  var k = A.id(key);
  if (!d || !d.hasKey(k)) return undefined;
  var t = d.getType(k);
  if (t == DescValueType.INTEGERTYPE) return d.getInteger(k);
  if (t == DescValueType.DOUBLETYPE) return d.getDouble(k);
  if (t == DescValueType.UNITDOUBLE) return d.getUnitDoubleValue(k);
  if (t == DescValueType.BOOLEANTYPE) return d.getBoolean(k);
  if (t == DescValueType.STRINGTYPE) return d.getString(k);
  if (t == DescValueType.ENUMERATEDTYPE) return PB.keyName(d.getEnumerationValue(k));
  return undefined;
};
A.list = function (d, key) {
  var k = A.id(key);
  return d && d.hasKey(k) && d.getType(k) == DescValueType.LISTTYPE ? d.getList(k) : null;
};
A.nums = function (l) {
  var out = [];
  for (var i = 0; l && i < l.count; i++) {
    var t = l.getType(i);
    out.push(t == DescValueType.INTEGERTYPE ? l.getInteger(i) : t == DescValueType.DOUBLETYPE ? l.getDouble(i) :
             t == DescValueType.UNITDOUBLE ? l.getUnitDoubleValue(i) : null);
  }
  return out;
};
A.round = function (v) { return typeof v == 'number' ? Math.round(v * 10000) / 10000 : v; };
A.channelOf = function (a) {  // 'RGB', 'Red'... of an item's Chnl reference
  var k = PB.c2t('Chnl');
  if (!a.hasKey(k) || a.getType(k) != DescValueType.REFERENCETYPE) return 'RGB';
  var e = a.getReference(k).getEnumeratedValue();
  for (var i = 0; i < A.CHANNELS.length; i++)
    if (e == PB.c2t(A.CHANNELS[i][1])) return A.CHANNELS[i][0];
  return PB.keyName(e);
};
A.colour = function (d, key) {  // an RGBC or Lab colour object at key as '#rrggbb' (Lab: Photoshop converts it)
  var k = A.id(key);
  if (!d || !d.hasKey(k) || d.getType(k) != DescValueType.OBJECTTYPE) return undefined;
  var o = d.getObjectValue(k), t = d.getObjectType(k);
  if (t == PB.c2t('LbCl')) {
    var sc = new SolidColor();
    sc.lab.l = A.get(o, 'Lmnc');
    sc.lab.a = A.get(o, 'A   ');
    sc.lab.b = A.get(o, 'B   ');
    return '#' + sc.rgb.hexValue.toLowerCase();
  }
  if (o.hasKey(PB.c2t('Rd  '))) return A.hex([A.get(o, 'Rd  '), A.get(o, 'Grn '), A.get(o, 'Bl  ')]);
  return 'a ' + PB.keyName(t);
};
A.VALS = {};  // by AdjustSpec.ORDER name: every value read back
A.VALS['Brightness/Contrast'] = function (d) { return {b: A.get(d, 'Brgh'), c: A.get(d, 'Cntr'), legacy: A.get(d, 'useLegacy')}; };
A.VALS.Levels = function (d) {
  var v = {}, l = A.list(d, 'Adjs');
  for (var i = 0; l && i < l.count; i++) {
    var a = l.getObjectValue(i), inp = A.nums(A.list(a, 'Inpt')), out = A.nums(A.list(a, 'Otpt'));
    v[A.channelOf(a)] = {in0: A.or(inp[0], 0), gam: A.round(A.or(A.get(a, 'Gmm '), 1)), in1: A.or(inp[1], 255),
                         out0: A.or(out[0], 0), out1: A.or(out[1], 255)};
  }
  return v;
};
A.VALS.Curves = function (d) {
  var v = {pts: {}}, l = A.list(d, 'Adjs');
  for (var i = 0; l && i < l.count; i++) {
    var a = l.getObjectValue(i), ch = A.channelOf(a), crv = A.list(a, 'Crv ');
    if (crv) {
      var pts = [];
      for (var j = 0; j < crv.count; j++) {
        var p = crv.getObjectValue(j);
        pts.push([A.round(A.get(p, 'Hrzn')), A.round(A.get(p, 'Vrtc'))]);
      }
      v.pts[ch] = pts;
    } else if (A.list(a, 'Mpng')) {  // a curve drawn by hand: its 256 outputs
      v.map = v.map || {};
      v.map[ch] = A.nums(A.list(a, 'Mpng'));
    }
  }
  return v;
};
A.VALS.Exposure = function (d) {
  return {e: A.round(A.get(d, 'Exps')), o: A.round(A.get(d, 'Ofst')), g: A.round(A.get(d, 'gammaCorrection'))};
};
A.VALS.Vibrance = function (d) { return {v: A.get(d, 'vibrance'), s: A.get(d, 'Strt')}; };
A.VALS['Hue/Saturation'] = function (d) {
  var v = {}, l = A.list(d, 'Adjs');
  for (var i = 0; l && i < l.count; i++) {
    var a = l.getObjectValue(i), r = A.get(a, 'LclR'), o = {h: A.get(a, 'H   '), s: A.get(a, 'Strt'), l: A.get(a, 'Lght')};
    if (r === undefined) {
      v.Master = o;
    } else {
      o.span = [A.get(a, 'BgnR'), A.get(a, 'BgnS'), A.get(a, 'EndS'), A.get(a, 'EndR')];
      v[A.HUES[r - 1] || 'range ' + r] = o;
    }
  }
  v.colorize = A.get(d, 'Clrz');
  return v;
};
A.VALS['Color Balance'] = function (d) {
  var v = {};
  for (var i = 0; i < A.TONES.length; i++) {
    var n = A.nums(A.list(d, A.TONES[i][1]));
    v[A.TONES[i][0]] = {cr: A.or(n[0], 0), mg: A.or(n[1], 0), yb: A.or(n[2], 0)};
  }
  v.pl = A.get(d, 'PrsL');
  return v;
};
A.VALS['Black & White'] = function (d) {
  var v = {};
  for (var i = 0; i < A.BW.length; i++) v[A.BW[i][0]] = A.get(d, A.BW[i][1]);
  v.tint = A.get(d, 'useTint');
  v.tintColor = A.colour(d, 'tintColor');
  return v;
};
A.VALS['Photo Filter'] = function (d) { return {pf: 1, pf1: A.colour(d, 'Clr '), d: A.get(d, 'Dnst'), pl: A.get(d, 'PrsL')}; };
A.VALS['Channel Mixer'] = function (d) {
  var v = {mono: A.get(d, 'Mnch')};
  for (var i = 0; i < A.MIXER.length; i++) {
    var k = PB.c2t(A.MIXER[i][1]);
    if (!d.hasKey(k) || d.getType(k) != DescValueType.OBJECTTYPE) continue;
    var o = d.getObjectValue(k);
    v[A.MIXER[i][0]] = {r: A.get(o, 'Rd  '), g: A.get(o, 'Grn '), b: A.get(o, 'Bl  '), k: A.get(o, 'Cnst')};
  }
  return v;
};
A.VALS['Selective Color'] = function (d) {
  var v = {}, l = A.list(d, 'ClrC');
  for (var i = 0; l && i < l.count; i++) {
    var a = l.getObjectValue(i), name = A.get(a, 'Clrs');
    for (var j = 0; j < A.SELECTIVE.length; j++)
      if (a.hasKey(PB.c2t('Clrs')) && a.getEnumerationValue(PB.c2t('Clrs')) == PB.c2t(A.SELECTIVE[j][1])) name = A.SELECTIVE[j][0];
    v[name] = {c: A.get(a, 'Cyn '), m: A.get(a, 'Mgnt'), y: A.get(a, 'Yllw'), k: A.get(a, 'Blck')};
  }
  if (d.hasKey(PB.c2t('Mthd'))) v.rel = A.is(d.getEnumerationValue(PB.c2t('Mthd')), 'Absl') ? 1 : 0;
  return v;
};
A.VALS.Posterize = function (d) { return {lv: A.get(d, 'Lvls')}; };
A.VALS.Threshold = function (d) { return {t: A.get(d, 'Lvl ')}; };

// vals without what is at AdjustSpec's default (what an adjustment layer keeps, AdjustSpec.changed()).
A.LEVEL = {in0: 0, gam: 1, in1: 255, out0: 0, out1: 255};
A.DEFAULTS = {
  'Brightness/Contrast': {b: 0, c: 0, legacy: false},
  'Levels': {RGB: A.LEVEL, Red: A.LEVEL, Green: A.LEVEL, Blue: A.LEVEL},
  'Curves': {pts: {RGB: [[0, 0], [255, 255]], Red: [[0, 0], [255, 255]], Green: [[0, 0], [255, 255]], Blue: [[0, 0], [255, 255]]}},
  'Exposure': {e: 0, o: 0, g: 1},
  'Vibrance': {v: 0, s: 0},
  'Hue/Saturation': {Master: {h: 0, s: 0, l: 0}, colorize: false},
  'Color Balance': {Shadows: {cr: 0, mg: 0, yb: 0}, Midtones: {cr: 0, mg: 0, yb: 0}, Highlights: {cr: 0, mg: 0, yb: 0}, pl: true},
  'Black & White': {r: 40, y: 60, g: 40, c: 60, b: 20, m: 80, tint: false, tintColor: A.TINT},
  'Photo Filter': {pf: 0, pf0: A.FILTER, pf1: '#ec8a00', d: 25, pl: true},
  'Channel Mixer': {mono: false, Red: {r: 100, g: 0, b: 0, k: 0}, Green: {r: 0, g: 100, b: 0, k: 0},
                    Blue: {r: 0, g: 0, b: 100, k: 0}, Gray: {r: 100, g: 0, b: 0, k: 0}},
  'Selective Color': {rel: 0},
  'Posterize': {lv: 4},
  'Threshold': {t: 128}
};
(function () {
  var hue = A.DEFAULTS['Hue/Saturation'], sc = A.DEFAULTS['Selective Color'];
  for (var i = 0; i < A.HUES.length; i++) hue[A.HUES[i]] = {h: 0, s: 0, l: 0, span: A.SPANS[A.HUES[i]]};
  for (i = 0; i < A.SELECTIVE.length; i++) sc[A.SELECTIVE[i][0]] = {c: 0, m: 0, y: 0, k: 0};
})();
A.changed = function (name, v) { return A.without(v, A.DEFAULTS[name] || {}) || {}; };
A.without = function (v, d) {  // v less what equals d, undefined when nothing is left
  var out = {}, any = false;
  for (var k in v) {
    if (!v.hasOwnProperty(k) || v[k] === undefined) continue;
    var x = v[k], y = d[k];
    if (x !== null && typeof x == 'object' && !(x instanceof Array) && y && typeof y == 'object') x = A.without(x, y);
    else if (PB.json(x) == PB.json(y)) x = undefined;
    if (x !== undefined) out[k] = x, any = true;
  }
  return any ? out : undefined;
};

// ---- Presets: presets.json -----------------------------------------------------------------------------------------

// Photoshop's presets of the adjustments AdjustSpec has presets of: each file of Presets/<kind>/ next to Photoshop
// loaded into an adjustment layer (presetKindUserDefined, Usng: the file, as the Properties panel records it), its
// values read back (vals off AdjustSpec's defaults); each adjustment as Photoshop makes it with no values (defaults,
// every value); Photo Filter's colours. Written to out/adjust/presets.json before the cases run: the UI takes its
// presets from there (AdjustSpec.PRESET_VALS), D33 its Levels and Curves presets.
A.PRESET_KINDS = [['Levels', /^levels$/i], ['Curves', /^curves$/i], ['Hue/Saturation', /^hue/i],
                  ['Black & White', /^black/i], ['Exposure', /^exposure$/i]];
A.presetFolder = function (root, re) {
  var dirs = root.getFiles(function (f) { return f instanceof Folder; });
  for (var i = 0; i < dirs.length; i++)
    if (re.test(decodeURI(dirs[i].name))) return dirs[i];
  return null;
};
// AdjustSpec.ORDER, each with its class (what PB.adjustments[name] returns first).
A.ORDER = [['Brightness/Contrast', 'BrgC'], ['Levels', 'Lvls'], ['Curves', 'Crvs'], ['Exposure', 'Exps'],
           ['Vibrance', 'vibrance'], ['Hue/Saturation', 'HStr'], ['Color Balance', 'ClrB'], ['Black & White', 'BanW'],
           ['Photo Filter', 'photoFilter'], ['Channel Mixer', 'ChnM'], ['Color Lookup', 'colorLookup'], ['Invert', 'Invr'],
           ['Posterize', 'Pstr'], ['Threshold', 'Thrs'], ['Gradient Map', 'GdMp'], ['Selective Color', 'SlcC']];
A.classOf = function (name) {
  for (var i = 0; i < A.ORDER.length; i++)
    if (A.ORDER[i][0] == name) return A.id(A.ORDER[i][1]);
  throw new Error('no adjustment ' + name);
};
A.newAdjustment = function (name) {  // an adjustment layer as Photoshop makes it, no values given: [class, layer]
  var cls = A.classOf(name), d = new ActionDescriptor(), r = new ActionReference(), a = new ActionDescriptor();
  r.putClass(PB.c2t('AdjL'));
  d.putReference(PB.c2t('null'), r);
  a.putClass(PB.c2t('Type'), cls);
  d.putObject(PB.c2t('Usng'), PB.c2t('AdjL'), a);
  executeAction(PB.c2t('Mk  '), d, DialogModes.NO);
  return [cls, app.activeDocument.activeLayer];
};
A.presets = function () {
  var out = {photoshop: app.version}, failed = [], doc = null, i, j;
  var units = app.preferences.rulerUnits, dialogs = app.displayDialogs;
  app.preferences.rulerUnits = Units.PIXELS;
  app.displayDialogs = DialogModes.NO;
  try {
    doc = app.documents.add(8, 8, 72, 'presets', NewDocumentMode.RGB, DocumentFill.WHITE, 1, BitsPerChannelType.SIXTEEN,
                            PB.SRGB);
    var root = new Folder(app.path + '/Presets');
    out.folder = root.fsName;
    if (!root.exists) failed.push({preset: 'all', error: 'no folder ' + root.fsName});
    for (i = 0; i < A.PRESET_KINDS.length && root.exists; i++) {
      var kind = A.PRESET_KINDS[i][0], folder = A.presetFolder(root, A.PRESET_KINDS[i][1]), got = {};
      if (!folder) {
        failed.push({preset: kind, error: 'no folder for it in ' + root.fsName});
        continue;
      }
      var files = folder.getFiles(function (f) { return f instanceof File && !/^\./.test(f.name); });
      files.sort(function (a, b) { return a.name < b.name ? -1 : a.name > b.name ? 1 : 0; });
      for (j = 0; j < files.length; j++) {
        var name = decodeURI(files[j].name).replace(/\.[^.]*$/, '');
        try {
          var made = A.newAdjustment(kind), t = new ActionDescriptor();
          t.putEnumerated(PB.s2t('presetKind'), PB.s2t('presetKindType'), PB.s2t('presetKindUserDefined'));
          t.putPath(PB.c2t('Usng'), files[j]);
          A.setAdjustment(made[0], t);
          var d = A.contentOf(doc, made[1]);
          PB.note('preset ' + kind + ' "' + name + '": ' + (d ? PB.dump(d) : 'nothing read back'));
          got[name] = A.changed(kind, A.VALS[kind](d));
          made[1].remove();
        } catch (e) {
          failed.push({preset: kind + ' ' + name, error: String(e)});
        }
      }
      out[kind] = got;
    }
    out.defaults = {};
    for (i = 0; i < A.ORDER.length; i++) {
      var which = A.ORDER[i][0];
      try {
        var adj = A.newAdjustment(which), back = A.contentOf(doc, adj[1]);
        PB.note('default ' + which + ': ' + (back ? PB.dump(back) : 'nothing read back'));
        out.defaults[which] = back && A.VALS[which] ? A.VALS[which](back) : null;
        adj[1].remove();
      } catch (e) {
        failed.push({preset: 'default ' + which, error: String(e)});
      }
    }
  } catch (e) {
    failed.push({preset: 'all', error: String(e) + (e.line ? ' (line ' + e.line + ')' : '')});
  } finally {
    if (doc) PB.close(doc);
    app.preferences.rulerUnits = units;
    app.displayDialogs = dialogs;
  }
  out['Photo Filter'] = A.FILTERS;  // the published colours (A.FILTERS): defaults["Photo Filter"] is Photoshop's own
  out.failed = failed;
  return out;
};
// Writes presets.json into out/adjust/ next to the script, before the cases run.
A.writePresets = function (dir) {
  var outRoot = new Folder(dir + '/out'), here = new Folder(dir + '/out/adjust');
  if (!outRoot.exists) outRoot.create();
  if (!here.exists) here.create();
  var p = A.presets();
  PB.write(here + '/presets.json', A.presetsText(p));
  PB.note('presets.json: ' + p.failed.length + ' failed' + (p.failed.length ? ', first ' + PB.json(p.failed[0]) : ''));
};
// presets.json: a key a line, presets one a line under it.
A.presetsText = function (p) {
  var lines = [];
  for (var k in p) {
    if (!p.hasOwnProperty(k)) continue;
    var v = p[k], inner = [];
    if (v && typeof v == 'object' && !(v instanceof Array)) {
      for (var n in v)
        if (v.hasOwnProperty(n)) inner.push('  ' + PB.quote(n) + ': ' + PB.json(v[n]));
      lines.push(PB.quote(k) + ': {' + (inner.length ? '\n' + inner.join(',\n') + '\n' : '') + '}');
    } else {
      lines.push(PB.quote(k) + ': ' + PB.json(v));
    }
  }
  return '{\n' + lines.join(',\n') + '\n}\n';
};

// ---- Before the cases: presets, gradient methods -------------------------------------------------------------------

// presets.json and the gradient methods' ids (A.probeMethods), in pixels and without dialogs; what fails is noted
// (photoshop.txt) and the cases still run.
A.prepare = function (presets) {
  var units = app.preferences.rulerUnits, dialogs = app.displayDialogs;
  app.preferences.rulerUnits = Units.PIXELS;
  app.displayDialogs = DialogModes.NO;
  try {
    if (presets) A.writePresets(HERE);
  } catch (e) {
    PB.note('presets: ' + e + (e.line ? ' (line ' + e.line + ')' : ''));
  }
  try {
    A.probeMethods();
  } catch (e) {
    A.methodIds = null;
    PB.note('gradient methods: cannot try them (' + e + '): the first id of each is used');
  }
  app.preferences.rulerUnits = units;
  app.displayDialogs = dialogs;
};

// ---- Cases ---------------------------------------------------------------------------------------------------------

A.INPUTS = {cube: ['cube.png', [190, 190]], ramp: ['ramp.png', [256, 256]], photo: ['photo.png', [256, 256]]};
A.base = function (input) { return {kind: 'pixel', src: A.INPUTS[input][0], props: {name: input}}; };
// A case <name>-<input>: the input under an adjustment layer named after its adjustment; vals left out for none;
// props: the layer's others (blend, opacity); more: its layer keys (auto).
A.adjCase = function (name, input, adj, vals, props, more) {
  var p = A.merge({name: adj, adj: adj}, props);
  if (vals !== undefined) p.vals = vals;
  return {name: name + '-' + input, canvas: A.INPUTS[input][1], layers: [A.base(input), A.merge({kind: 'adj', props: p}, more)]};
};
// A case of Image > Adjustments and commands on the input's pixels (case key apply).
A.applyCase = function (name, input, steps) {
  return {name: name + '-' + input, canvas: A.INPUTS[input][1], layers: [A.base(input)], apply: steps};
};

function cases() {
  var c = [], i, j, k;
  var add = function (name, inputs, adj, vals, props, more) {
    for (var n = 0; n < inputs.length; n++) c.push(A.adjCase(name, inputs[n], adj, vals, props, more));
  };
  var ramp = ['ramp'], cube = ['cube'], photo = ['photo'], rampCube = ['ramp', 'cube'], cubePhoto = ['cube', 'photo'];

  // Brightness/Contrast (D34): Brightness and Contrast alone and together on ramp.png, new and Use Legacy (whose
  // dialog goes to ±100 only: its 150s are 100); b 50, c 50 on cube.png too, and on photo.png with c 100: whether the
  // new one looks at the image.
  var bc = [['bm150', -150, 0], ['bm50', -50, 0], ['b50', 50, 0], ['b150', 150, 0], ['cm50', 0, -50], ['c50', 0, 50],
            ['c100', 0, 100], ['b50-c50', 50, 50]];
  var bcVals = function (b, cc, legacy) {
    var v = {};
    if (b) v.b = b;
    if (cc) v.c = cc;
    if (legacy) v.legacy = true;
    return v;
  };
  for (i = 0; i < bc.length; i++) add('bc-' + bc[i][0], ramp, 'Brightness/Contrast', bcVals(bc[i][1], bc[i][2], false));
  for (i = 0; i < bc.length; i++)
    add('bc-legacy-' + bc[i][0].replace('150', '100'), ramp, 'Brightness/Contrast',
        bcVals(Math.max(-100, Math.min(100, bc[i][1])), bc[i][2], true));
  add('bc-legacy-cm100', ramp, 'Brightness/Contrast', bcVals(0, -100, true));
  add('bc-b50-c50', ['cube', 'photo'], 'Brightness/Contrast', bcVals(50, 50, false));
  add('bc-legacy-b50-c50', cube, 'Brightness/Contrast', bcVals(50, 50, true));
  add('bc-c100', photo, 'Brightness/Contrast', bcVals(0, 100, false));

  // Levels (D33): gamma, input and output points, the output turned over; channels alone, and with RGB (which first).
  add('levels-gam05', ramp, 'Levels', {RGB: {gam: 0.5}});
  add('levels-gam2', ramp, 'Levels', {RGB: {gam: 2}});
  add('levels-inout', ramp, 'Levels', {RGB: {in0: 20, gam: 1.3, in1: 230, out0: 10, out1: 240}});
  add('levels-flip', ramp, 'Levels', {RGB: {out0: 255, out1: 0}});
  add('levels-channels', rampCube, 'Levels', {Red: {in0: 40}, Green: {gam: 1.8}, Blue: {in1: 180, out0: 30}});
  add('levels-order', rampCube, 'Levels', {RGB: {in0: 15, gam: 0.6}, Red: {in0: 60, gam: 1.5, out1: 230}});

  // Curves (D33): an S, points near the ends (steep: the spline clamps), ends inside (flat past them), eight points;
  // channels alone, and with RGB (which first).
  var S = {RGB: [[0, 0], [64, 40], [192, 215], [255, 255]]};
  add('curves-s', ramp, 'Curves', {pts: S});
  add('curves-edges', ramp, 'Curves', {pts: {RGB: [[0, 0], [8, 32], [247, 223], [255, 255]]}});
  add('curves-ends', ramp, 'Curves', {pts: {RGB: [[30, 15], [128, 140], [220, 245]]}});
  add('curves-many', ramp, 'Curves', {pts: {RGB: [[0, 0], [32, 20], [64, 70], [96, 90], [128, 140], [160, 150], [200, 215], [255, 255]]}});
  add('curves-channels', rampCube, 'Curves', {pts: {Red: [[0, 0], [128, 170], [255, 255]], Blue: [[0, 40], [255, 215]]}});
  add('curves-order', rampCube, 'Curves', {pts: {RGB: [[0, 0], [128, 96], [255, 255]], Green: [[0, 0], [96, 150], [255, 255]]}});

  // Exposure (D34): each of exposure, offset, gamma both ways, all three; on ramp.png (the curve), the three on cube.png.
  var ex = [['p1', {e: 1}], ['m15', {e: -1.5}], ['o01', {o: 0.1}], ['om005', {o: -0.05}], ['g05', {g: 0.5}],
            ['g2', {g: 2}], ['mix', {e: 0.7, o: -0.02, g: 1.3}]];
  for (i = 0; i < ex.length; i++) add('exposure-' + ex[i][0], i == ex.length - 1 ? rampCube : ramp, 'Exposure', ex[i][1]);

  // Vibrance (D35), on cube.png.
  var vib = [['v50', {v: 50}], ['vm50', {v: -50}], ['v100', {v: 100}], ['s50', {s: 50}], ['sm50', {s: -50}],
             ['v60-sm30', {v: 60, s: -30}]];
  for (i = 0; i < vib.length; i++) add('vibrance-' + vib[i][0], cube, 'Vibrance', vib[i][1]);

  // Hue/Saturation (D35): Master's hue, saturation, lightness; ranges (one, three, one of a span of its own);
  // Colorize. On cube.png; the ranges on photo.png too.
  var hue = [['h60', {Master: {h: 60}}], ['s50', {Master: {s: 50}}], ['sm100', {Master: {s: -100}}],
             ['l40', {Master: {l: 40}}], ['lm40', {Master: {l: -40}}], ['reds', {Reds: {h: 30, s: 40, l: -10}}],
             ['ranges', {Yellows: {h: -20, s: 30}, Cyans: {h: 40, s: -50}, Blues: {l: 30}}],
             ['span', {Greens: {h: 60, span: [60, 90, 150, 200]}}], ['colorize', {Master: {h: 200, s: 40}, colorize: true}],
             ['colorize-l', {Master: {h: 30, s: 70, l: -20}, colorize: true}]];
  for (i = 0; i < hue.length; i++) add('hue-' + hue[i][0], hue[i][0] == 'ranges' ? cubePhoto : cube, 'Hue/Saturation', hue[i][1]);

  // Color Balance (D36): each tone; all three; Preserve Luminosity on and off. On cube.png; all three on photo.png.
  var all = {Shadows: {cr: -30, yb: 40}, Midtones: {mg: 50, yb: -20}, Highlights: {cr: 30, mg: -20}};
  add('cbal-shadows', cube, 'Color Balance', {Shadows: {cr: 50, mg: -30, yb: 20}});
  add('cbal-midtones', cube, 'Color Balance', {Midtones: {cr: 100}});
  add('cbal-midtones-nopl', cube, 'Color Balance', {Midtones: {cr: 100}, pl: false});
  add('cbal-highlights', cube, 'Color Balance', {Highlights: {cr: -40, mg: 60, yb: -80}});
  add('cbal-all', cubePhoto, 'Color Balance', all);
  add('cbal-all-nopl', cube, 'Color Balance', A.merge(PB.copy(all), {pl: false}));

  // Black & White (D37): the default, the Red Filter preset's values, past the dialog's ends, Tint. On cube.png.
  add('bw-default', cube, 'Black & White');
  add('bw-red-filter', cube, 'Black & White', {r: 120, y: 110, g: -10, c: -50, b: -50, m: 120});
  add('bw-extreme', cube, 'Black & White', {r: 300, y: -200, g: 150, c: -100, b: 250, m: -50});
  add('bw-tint', cube, 'Black & White', {tint: true});
  add('bw-tint-blue', cube, 'Black & White', {r: 60, y: 30, tint: true, tintColor: '#3a6ea5'});

  // Photo Filter (D37): Photoshop's default filter (no colour given: read back), a filter by name (A.FILTERS' colour),
  // colours of its own at Densities, Preserve Luminosity on and off. On cube.png.
  add('pfilter-default', cube, 'Photo Filter');
  add('pfilter-underwater-d60', cube, 'Photo Filter', {pf0: 'Underwater', d: 60});
  add('pfilter-cooling80-d50', cube, 'Photo Filter', {pf: 1, pf1: '#006dff', d: 50});
  add('pfilter-red-d75-nopl', cube, 'Photo Filter', {pf: 1, pf1: '#ea1a1a', d: 75, pl: false});
  add('pfilter-green-d100', cube, 'Photo Filter', {pf: 1, pf1: '#19c919', d: 100});
  add('pfilter-sepia-d40-nopl', cube, 'Photo Filter', {pf: 1, pf1: '#ac7a33', d: 40, pl: false});

  // Channel Mixer (D37): swapped and mixed channels, Constant, past the ends, Monochrome. On cube.png.
  add('mixer-swap', cube, 'Channel Mixer', {Red: {r: 0, g: 100}, Green: {r: 100, g: 0}});
  add('mixer-mix', cube, 'Channel Mixer', {Red: {r: 80, g: 30, b: -10}, Blue: {r: -20, g: 40, b: 120, k: 10}});
  add('mixer-constant', cube, 'Channel Mixer', {Green: {k: 30}, Blue: {k: -40}});
  add('mixer-extreme', cube, 'Channel Mixer', {Red: {r: 200, g: -200, b: 100, k: 50}});
  add('mixer-mono', cube, 'Channel Mixer', {mono: true, Gray: {r: 40, g: 40, b: 20}});
  add('mixer-mono-k', cube, 'Channel Mixer', {mono: true, Gray: {r: 30, g: 59, b: 11, k: -20}});

  // Color Lookup (D39): tests/data/lut's LUTs (identity 17 as .cube and .3dl, invert 17, the film look of 9 points as
  // .cube and .3dl, with Dither). On cube.png; the film look on photo.png too.
  add('lookup-identity17', cube, 'Color Lookup', {lf0: 'identity-17.cube'});
  add('lookup-identity17-3dl', cube, 'Color Lookup', {lf0: 'identity-17.3dl'});
  add('lookup-invert17', cube, 'Color Lookup', {lf0: 'invert-17.cube'});
  add('lookup-film9', cubePhoto, 'Color Lookup', {lf0: 'film-9.cube'});
  add('lookup-film9-3dl', cube, 'Color Lookup', {lf0: 'film-9.3dl'});
  add('lookup-film9-dither', cube, 'Color Lookup', {lf0: 'film-9.cube', dither: true});

  // Invert, Posterize, Threshold (D38): Posterize and Threshold on ramp.png (where the steps fall) and cube.png.
  add('invert', cube, 'Invert');
  var levels = [2, 4, 7];
  for (i = 0; i < levels.length; i++) add('posterize-' + levels[i], rampCube, 'Posterize', {lv: levels[i]});
  var at = [64, 128, 200];
  for (i = 0; i < at.length; i++) add('threshold-' + at[i], rampCube, 'Threshold', {t: at[i]});

  // Gradient Map (D38, D30): AdjustSpec's three stops in the five methods on ramp.png (grey's row: the gradient at 256
  // points), on cube.png (the weights of Lum); Reverse, Dither, opacity stops, midpoints (every stop its own, the
  // last too: which segment Mdpn is) in Classic without Smoothness.
  var methods = ['Perceptual', 'Linear', 'Classic', 'Smooth', 'Stripes'];
  for (i = 0; i < methods.length; i++)
    add('gmap-' + methods[i].toLowerCase(), i ? ramp : rampCube, 'Gradient Map', {gradient: A.MAP, method: methods[i]});
  add('gmap-reverse', ramp, 'Gradient Map', {gradient: A.MAP, rev: true});
  add('gmap-dither', ramp, 'Gradient Map', {gradient: A.MAP, dither: true});
  add('gmap-alpha', ramp, 'Gradient Map', {gradient: {colors: A.MAP.colors, alphas: [{p: 0, a: 1}, {p: 1, a: 0.3}]}});
  add('gmap-midpoints', ramp, 'Gradient Map', {gradient: {colors: [{p: 0, c: [27, 16, 32], m: 0.25},
      {p: 0.5, c: [158, 47, 69], m: 0.7}, {p: 1, c: [233, 199, 179], m: 0.4}], smoothness: 0}, method: 'Classic'});

  // Selective Color (D36): Reds Relative and Absolute; Neutrals; Whites and Blacks Absolute; five colours, both ways.
  // On cube.png; the five Relative on photo.png too.
  var reds = {Reds: {c: -50, m: 30, y: 20, k: 10}};
  var five = {Yellows: {c: 40, k: -20}, Greens: {m: 50}, Cyans: {y: -60}, Blues: {c: -30, k: 30}, Magentas: {m: -40}};
  add('selective-reds', cube, 'Selective Color', reds);
  add('selective-reds-abs', cube, 'Selective Color', A.merge(PB.copy(reds), {rel: 1}));
  add('selective-neutrals', cube, 'Selective Color', {Neutrals: {c: 20, m: -20, y: 30, k: -10}});
  add('selective-whites-blacks-abs', cube, 'Selective Color', {Whites: {y: 40, k: -30}, Blacks: {c: 30, k: 50}, rel: 1});
  add('selective-five', cubePhoto, 'Selective Color', five);
  add('selective-five-abs', cube, 'Selective Color', A.merge(PB.copy(five), {rel: 1}));

  // Image > Adjustments and the commands (D41, D31): on the input's own pixels. Desaturate, Equalize; Auto Tone and
  // Auto Color on photo.png (green and blue stop short of white); Auto Contrast on photo.png made flat first (Levels'
  // output 40..200: photo.png's red spans 0..255 already); Hue/Saturation as Image > Adjustments (as hue-h60-cube).
  c.push(A.applyCase('cmd-desaturate', 'cube', [{command: 'desaturate'}]));
  c.push(A.applyCase('cmd-equalize', 'cube', [{command: 'equalize'}]));
  c.push(A.applyCase('cmd-equalize', 'photo', [{command: 'equalize'}]));
  c.push(A.applyCase('cmd-auto-tone', 'photo', [{command: 'autoTone'}]));
  c.push(A.applyCase('cmd-auto-color', 'photo', [{command: 'autoColor'}]));
  c.push(A.applyCase('cmd-auto-contrast-flat', 'photo', [{adj: 'Levels', vals: {RGB: {out0: 40, out1: 200}}},
                                                         {command: 'autoContrast'}]));
  c.push(A.applyCase('apply-hue-h60', 'cube', [{adj: 'Hue/Saturation', vals: {Master: {h: 60}}}]));

  // Adjustment layers in Luminosity (D40) and at 50%.
  add('lum-curves-s', photo, 'Curves', {pts: S}, {blend: 'luminosity'});
  add('lum-hue-h60', cube, 'Hue/Saturation', {Master: {h: 60}}, {blend: 'luminosity'});
  add('lum-levels-gam05', cube, 'Levels', {RGB: {gam: 0.5}}, {blend: 'luminosity'});
  add('opacity50-curves-s', cube, 'Curves', {pts: S}, {opacity: 0.5});

  // Auto of Levels, Curves, Brightness/Contrast (D41: Enhance Brightness and Contrast) on photo.png: the values it
  // works out come back into vals.
  add('auto-levels', photo, 'Levels', undefined, {}, {auto: true});
  add('auto-curves', photo, 'Curves', undefined, {}, {auto: true});
  add('auto-bc', photo, 'Brightness/Contrast', undefined, {}, {auto: true});
  return c;
}

// jscheck runs this without Photoshop; gradient.jsx #includes it with D32_GRADIENT set, its own cases().
if (typeof app != 'undefined' && typeof D32_GRADIENT == 'undefined') {
  A.prepare(true);
  PB.run('adjust', cases(), {bits: 8, dir: HERE});
}
