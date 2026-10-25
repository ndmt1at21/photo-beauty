// Photoshop's images of its Gradient fill layers (D32), for D30 (test_gradient_ps) and D40: the five styles in each
// of the five methods; the line of a fill on a box not square (angles, Scale, Offset, Reverse, Align with Layer over a
// mask); midpoints (every stop its own m, the last one too: whether Photoshop's Mdpn is the midpoint to the next stop,
// as D30 reads it, or from the one before); Smoothness on Classic and on the other methods; opacity stops; Dither in a
// 16-bit document (16-bit images) and in an 8-bit one; a fill over pixels in Multiply, a Solid Color fill.
// README-adjust.md next to this folder has the steps. Writes out/gradient/ next to this script: <case>.png (8 bits as
// adjust.jsx's, but the dither16 cases' 16), index.json (a fill layer's props.content is D30's GradientFill JSON, every
// key: gradient_fill_from() reads it), photoshop.txt. 73 images, about 0.5 MB, 3 minutes. Not run in Photoshop yet
// (README-adjust.md: what is unsure). adjust.jsx holds what the two share: its cases do not run here.
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
var D32_GRADIENT = true;  // adjust.jsx as a library: it runs no cases of its own
#include "adjust.jsx"

var G = {};

// D30's GradientFill as a fill layer's content, every key (to_json(GradientFill)'s), more over the defaults.
G.fill = function (gradient, more) {
  return A.merge({gradient: gradient, method: 'perceptual', style: 'linear', angle: 90, scale: 100, reverse: false,
                  dither: false, align: true, offset: [0, 0]}, more);
};
// Photoshop's five styles (GrdT) by D30's names.
G.STYLES = {linear: 'Lnr ', radial: 'Rdl ', angle: 'Angl', reflected: 'Rflc', diamond: 'Dmnd'};
// A fill layer's content as [class, descriptor]: {color: '#rrggbb'} a Solid Color, else a GradientFill (psd.jsx's
// E.gradientFill): Angl degrees, Scl %, Ofst % of the box, Algn Align with Layer.
G.content = function (f) {
  var c = PB.c2t, s = PB.s2t, d = new ActionDescriptor();
  if (f.color !== undefined) {
    d.putObject(c('Clr '), c('RGBC'), A.rgbc(f.color));
    return [s('solidColorLayer'), d];
  }
  var o = new ActionDescriptor(), off = f.offset || [0, 0];
  if (!G.STYLES[f.style || 'linear']) throw new Error('no gradient style ' + f.style);
  d.putObject(c('Grad'), c('Grdn'), A.gradient(f.gradient));
  A.putMethod(d, f.method);
  d.putEnumerated(c('Type'), c('GrdT'), c(G.STYLES[f.style || 'linear']));
  d.putUnitDouble(c('Angl'), c('#Ang'), A.or(f.angle, 90));
  d.putUnitDouble(c('Scl '), c('#Prc'), A.or(f.scale, 100));
  d.putBoolean(c('Rvrs'), !!f.reverse);
  d.putBoolean(c('Dthr'), !!f.dither);
  d.putBoolean(c('Algn'), f.align !== false);
  o.putUnitDouble(c('Hrzn'), c('#Prc'), off[0]);
  o.putUnitDouble(c('Vrtc'), c('#Prc'), off[1]);
  d.putObject(c('Ofst'), c('Pnt '), o);
  return [s('gradientLayer'), d];
};
// Kind fill: the layer made from props.content, without the white mask Photoshop gives it (the case says where it has
// a mask); props key content: what Photoshop has against what was given (adjust.jsx's A.check), into photoshop.txt.
PB.kinds.fill = function (l, doc) {
  doc.selection.deselect();
  var made = G.content(l.props.content);
  A.makeContent(made[0], made[1]);
  PB.deleteMask();
  return doc.activeLayer;
};
PB.layerKeys.content = function (content, layer, doc, l) {
  var got = A.check(doc, layer, G.content(content), l.props.name);
  PB.note('  ' + l.props.name + ' read back: ' + PB.dump(got));
};
// Case key depth: 8 makes the document 8 bits once built (its fill layers then dither, if they do, at 8 bits).
PB.steps.depth = function (bits, doc) {
  if (bits == 8) doc.bitsPerChannel = BitsPerChannelType.EIGHT;
  else if (bits != 16) throw new Error('depth ' + bits);
};

// ---- Cases ---------------------------------------------------------------------------------------------------------

G.BOX = [256, 160];  // not square: the line's length along an angle shows
// Three stops (red, yellow, blue: the methods part in the middle of each segment); black to white without
// Smoothness, in Classic: the value is t itself, for the line's geometry; midpoints of every stop.
G.G3 = {colors: [{p: 0, c: [230, 40, 30]}, {p: 0.5, c: [250, 220, 40]}, {p: 1, c: [20, 60, 220]}]};
G.BW = {colors: [{p: 0, c: [0, 0, 0]}, {p: 1, c: [255, 255, 255]}], smoothness: 0};
G.layer = function (content, props, more) {
  return A.merge({kind: 'fill', props: A.merge({name: 'gradient', content: content}, props)}, more);
};

function cases() {
  var c = [], i, j;
  var one = function (name, content, more) {  // a fill alone on the box; more: the case's other keys
    c.push(A.merge({name: name, canvas: G.BOX, layers: [G.layer(content)]}, more));
  };
  var geometry = function (more) { return G.fill(G.BW, A.merge({method: 'classic'}, more)); };
  var styles = ['linear', 'radial', 'angle', 'reflected', 'diamond'];
  var methods = ['perceptual', 'linear', 'classic', 'smooth', 'stripes'];

  // The five styles in each of the five methods, three stops, at the defaults (90 degrees, Scale 100%).
  for (i = 0; i < styles.length; i++)
    for (j = 0; j < methods.length; j++) one(styles[i] + '-' + methods[j], G.fill(G.G3, {style: styles[i], method: methods[j]}));

  // The line on the box (D30's fill_line()): Linear at six angles; Radial, Angle, Reflected, Diamond turned; Scale;
  // Offset; Reverse. Black to white in Classic without Smoothness.
  var angles = [['linear', 0], ['linear', 30], ['linear', 45], ['linear', 135], ['linear', 150], ['linear', -60],
                ['radial', 30], ['angle', 30], ['angle', -120], ['reflected', 45], ['diamond', 30]];
  for (i = 0; i < angles.length; i++)
    one(angles[i][0] + '-a' + String(angles[i][1]).replace('-', 'm'), geometry({style: angles[i][0], angle: angles[i][1]}));
  var scales = [['linear', 50], ['linear', 150], ['radial', 50], ['radial', 150], ['diamond', 150]];
  for (i = 0; i < scales.length; i++) one(scales[i][0] + '-s' + scales[i][1], geometry({style: scales[i][0], scale: scales[i][1]}));
  one('linear-a30-offset', geometry({angle: 30, offset: [20, -10]}));
  one('radial-offset', geometry({style: 'radial', offset: [-25, 15]}));
  one('angle-offset', geometry({style: 'angle', offset: [10, 10]}));
  one('linear-a30-reverse', geometry({angle: 30, reverse: true}));
  one('radial-reverse', geometry({style: 'radial', reverse: true}));
  one('angle-reverse', geometry({style: 'angle', reverse: true}));
  // Align with Layer on and off, a mask of shapes.png's alpha (D40: the box is the mask's bounds, or the canvas).
  for (i = 0; i < 2; i++)
    c.push({name: 'align-' + (i ? 'off' : 'on'), canvas: [256, 256],
            layers: [G.layer(geometry({style: 'radial', align: !i}), {}, {mask: 'shapes.png'})]});

  // Midpoints: every stop its own m, the last one's too (D30 uses m of a stop for the segment after it, so the last
  // one's would do nothing; read the other way, the first one's would). Classic without Smoothness (the mix is the
  // midpoint's curve alone), and Perceptual, Linear, Stripes; extremes; an opacity stop's.
  var m3 = {colors: [{p: 0, c: [230, 40, 30], m: 0.2}, {p: 0.5, c: [250, 220, 40], m: 0.75}, {p: 1, c: [20, 60, 220], m: 0.35}],
            smoothness: 0};
  one('mid-bw', G.fill({colors: [{p: 0, c: [0, 0, 0], m: 0.25}, {p: 1, c: [255, 255, 255], m: 0.8}], smoothness: 0},
                       {method: 'classic'}));
  one('mid3-classic', G.fill(m3, {method: 'classic'}));
  one('mid3-perceptual', G.fill(m3, {method: 'perceptual'}));
  one('mid3-linear', G.fill(m3, {method: 'linear'}));
  one('mid3-stripes', G.fill({colors: [{p: 0, c: [230, 40, 30], m: 0.3}, {p: 0.5, c: [250, 220, 40], m: 0.7},
                                       {p: 1, c: [20, 60, 220], m: 0.5}]}, {method: 'stripes'}));
  one('mid-extremes', G.fill({colors: [{p: 0, c: [0, 0, 0], m: 0.05}, {p: 0.5, c: [128, 128, 128], m: 0.95},
                                       {p: 1, c: [255, 255, 255], m: 0.5}], smoothness: 0}, {method: 'classic'}));
  one('mid-alpha', G.fill({colors: [{p: 0, c: [0, 0, 0]}, {p: 1, c: [255, 255, 255]}],
                           alphas: [{p: 0, a: 1, m: 0.3}, {p: 1, a: 0, m: 0.7}], smoothness: 0}, {method: 'classic'}));

  // Smoothness (D30: Classic's only): Classic at 0 and 50% (the grid has 100); Perceptual, Linear, Smooth at 0.
  var sm = [['classic', 0], ['classic', 50], ['perceptual', 0], ['linear', 0], ['smooth', 0]];
  for (i = 0; i < sm.length; i++)
    one('smoothness-' + sm[i][0] + '-' + sm[i][1], G.fill(A.merge(PB.copy(G.G3), {smoothness: sm[i][1]}), {method: sm[i][0]}));

  // Opacity stops (opaque, 20%, opaque) in each method: how opacity mixes beside colour.
  for (i = 0; i < methods.length; i++)
    one('alpha-' + methods[i], G.fill(A.merge(PB.copy(G.G3), {alphas: [{p: 0, a: 1}, {p: 0.5, a: 0.2}, {p: 1, a: 1}]}),
                                      {method: methods[i]}));

  // Dither: in the 16-bit document saved at 16 bits (the noise as it is; the same without, to tell it from the
  // rounding), Radial in three stops too; saved at 8 bits; in an 8-bit document.
  var strip = [256, 64];
  c.push({name: 'dither16', canvas: strip, bits: 16, layers: [G.layer(geometry({angle: 30, dither: true}))]});
  c.push({name: 'dither16-off', canvas: strip, bits: 16, layers: [G.layer(geometry({angle: 30}))]});
  c.push({name: 'dither16-radial', canvas: strip, bits: 16, layers: [G.layer(G.fill(G.G3, {style: 'radial', dither: true}))]});
  one('dither8', geometry({angle: 30, dither: true}));
  one('dither8-doc', geometry({angle: 30, dither: true}), {depth: 8});

  // Over pixels: three stops in Multiply at 60% over ramp.png; a Solid Color fill at 70%.
  c.push({name: 'over-ramp-multiply', canvas: [256, 256], layers: [{kind: 'pixel', src: 'ramp.png', props: {name: 'ramp'}},
          G.layer(G.fill(G.G3, {angle: 30}), {blend: 'multiply', opacity: 0.6})]});
  c.push({name: 'solid-ramp', canvas: [256, 256], layers: [{kind: 'pixel', src: 'ramp.png', props: {name: 'ramp'}},
          G.layer({color: '#3a7bd5'}, {name: 'color', opacity: 0.7})]});
  return c;
}

// jscheck runs this without Photoshop (cases() here is this script's: it comes after adjust.jsx's).
if (typeof app != 'undefined') {
  A.prepare(false);
  PB.run('gradient', cases(), {bits: 8, dir: HERE});
}
