// Photoshop's composites for P1's blending (B30): B31's blend modes and Fill, B34's masks, clipping, Blend If and
// channels, groups, an adjustment layer in each place it can be; README.md has the steps. Writes out/blend/ next to
// this script: <case>.png, index.json (each case as photo-beauty's scene and the bits of its PNG, tests/golden.h reads
// it), photoshop.txt. 16 bits as B4 for the 16 modes B4 has not (at opacity 100 and 50%) and the inputs; every other
// case 8 bits, the 16-bit composite rounded without dither, so that tests/golden/ps/p1 stays within 30 MB (README.md,
// P1). Inputs from in/ (tests/golden/ps/p1/in: pb-bench ps-inputs): base.png, top.png (256 x 384, B4's ramps: every
// pair of levels), and the 256 x 256 ramp.png, shapes.png, alpha.png; photo.png and cube.png only come back as
// Photoshop read them (in-*). 174 images, about 8 minutes.
#target photoshop
#include "lib.jsx"

// The 27 modes of a layer (kBlendNames but passThrough), in Photoshop's menu order.
var MODES = ['normal', 'dissolve', 'darken', 'multiply', 'colorBurn', 'linearBurn', 'darkerColor', 'lighten', 'screen',
             'colorDodge', 'linearDodge', 'lighterColor', 'overlay', 'softLight', 'hardLight', 'vividLight',
             'linearLight', 'pinLight', 'hardMix', 'difference', 'exclusion', 'subtract', 'divide', 'hue', 'saturation',
             'color', 'luminosity'];
// B4's 11 (tools/ps-golden/blend.jsx: tests/golden/ps/<mode>-<opacity>.png at 16 bits), here at 8 bits.
var P0 = ['normal', 'darken', 'multiply', 'colorBurn', 'lighten', 'screen', 'colorDodge', 'overlay', 'softLight',
          'linearLight', 'difference'];
// The 8 whose Fill is not their opacity (B31).
var SPECIAL = ['colorBurn', 'linearBurn', 'colorDodge', 'linearDodge', 'vividLight', 'linearLight', 'hardMix',
               'difference'];
var TALL = [256, 384], SQUARE = [256, 256];

function pixel(src, props, more) {  // a pixel layer of an input; more: rotate, mask
  var l = {kind: 'pixel', src: src, props: props || {}};
  for (var k in more)
    if (more.hasOwnProperty(k)) l[k] = more[k];
  return l;
}
function group(props, children, mask) {
  var g = {kind: 'group', props: props, children: children};
  if (mask) g.mask = mask;
  return g;
}
function invert(props) {
  props.adj = 'Invert';
  return {kind: 'adj', props: props};
}
function over(props) {  // top.png over base.png
  props.name = 'top';
  return [pixel('base.png', {name: 'base'}), pixel('top.png', props)];
}

function cases() {
  var c = [], i, j, k, f;
  // Each mode at 100 and 50% (B4's names: <mode>-<opacity>): 16 bits but B4's modes.
  for (i = 0; i < MODES.length; i++)
    for (f = 100; f >= 50; f -= 50)
      c.push({name: MODES[i] + '-' + f, canvas: TALL, bits: PB.has(P0, MODES[i]) ? 8 : 16,
              layers: over({blend: MODES[i], opacity: f / 100})});
  // Fill of the 8 special modes, alone and with opacity 50%; three modes where Fill is opacity, and Dissolve.
  for (i = 0; i < SPECIAL.length; i++) {
    for (f = 0; f <= 75; f += 25) c.push({name: SPECIAL[i] + '-fill' + f, canvas: TALL, layers: over({blend: SPECIAL[i], fill: f / 100})});
    c.push({name: SPECIAL[i] + '-50-fill50', canvas: TALL, layers: over({blend: SPECIAL[i], opacity: 0.5, fill: 0.5})});
  }
  var plain = ['normal', 'multiply', 'overlay', 'dissolve'];
  for (i = 0; i < plain.length; i++) c.push({name: plain[i] + '-fill50', canvas: TALL, layers: over({blend: plain[i], fill: 0.5})});
  // Transparent over transparent: alpha.png, its alpha across, under itself turned (alpha down), each mode.
  for (i = 0; i < MODES.length; i++)
    c.push({name: 'alpha-' + MODES[i], canvas: SQUARE, layers: [pixel('alpha.png', {name: 'under'}),
            pixel('alpha.png', {name: 'over', blend: MODES[i]}, {rotate: 90})]});
  // Groups over ramp.png: isolated ones (Normal, Multiply with a mask), Pass Through (alone, at 50% with a mask),
  // one in another.
  var inner = function (mode1, mode2) {
    return [pixel('shapes.png', {name: 'shapes', blend: mode1}), pixel('alpha.png', {name: 'alpha', blend: mode2})];
  };
  c.push({name: 'group-normal', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          group({name: 'group', blend: 'normal'}, inner('normal', 'multiply'))]});
  c.push({name: 'group-multiply-mask', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          group({name: 'group', blend: 'multiply'}, inner('normal', 'screen'), 'alpha.png')]});
  c.push({name: 'group-passthrough', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          group({name: 'group', blend: 'passThrough'}, inner('overlay', 'multiply'))]});
  c.push({name: 'group-passthrough-50-mask', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          group({name: 'group', blend: 'passThrough', opacity: 0.5}, inner('overlay', 'multiply'), 'shapes.png')]});
  c.push({name: 'group-nested', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          group({name: 'outer', blend: 'passThrough', opacity: 0.75}, [pixel('shapes.png', {name: 'shapes'}),
                group({name: 'inner', blend: 'screen', opacity: 0.5}, [pixel('alpha.png', {name: 'alpha', blend: 'difference'})])])]});
  // Layer masks: of shapes.png's alpha, at Density 100 and 50%; of alpha.png's on a layer with alpha of its own.
  c.push({name: 'mask', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          pixel('alpha.png', {name: 'masked'}, {rotate: 90, mask: 'shapes.png'})]});
  c.push({name: 'mask-density50', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          pixel('alpha.png', {name: 'masked', maskDensity: 0.5}, {rotate: 90, mask: 'shapes.png'})]});
  c.push({name: 'mask-alpha', canvas: SQUARE, layers: [pixel('ramp.png', {name: 'ramp'}),
          pixel('alpha.png', {name: 'masked'}, {rotate: 90, mask: 'alpha.png'})]});
  // Clipping onto shapes.png: plain; the base at opacity 50%, at Fill 0, in Multiply; the clipped layer in Multiply
  // with a mask; two clipped; a group as the base.
  var clip = function (name, base, clipped, more) {
    var layers = [pixel('ramp.png', {name: 'ramp'}), base, clipped];
    if (more) layers.push(more);
    c.push({name: name, canvas: SQUARE, layers: layers});
  };
  var turned = {rotate: 90};
  clip('clip', pixel('shapes.png', {name: 'base'}), pixel('alpha.png', {name: 'clipped', clip: true}, turned));
  clip('clip-base-opacity50', pixel('shapes.png', {name: 'base', opacity: 0.5}),
       pixel('alpha.png', {name: 'clipped', clip: true}, turned));
  clip('clip-base-fill0', pixel('shapes.png', {name: 'base', fill: 0}),
       pixel('alpha.png', {name: 'clipped', clip: true}, turned));
  clip('clip-base-multiply', pixel('shapes.png', {name: 'base', blend: 'multiply'}),
       pixel('alpha.png', {name: 'clipped', clip: true}, turned));
  clip('clip-multiply-mask', pixel('shapes.png', {name: 'base'}),
       pixel('alpha.png', {name: 'clipped', clip: true, blend: 'multiply'}, {rotate: 90, mask: 'alpha.png'}));
  clip('clip-two', pixel('shapes.png', {name: 'base'}), pixel('alpha.png', {name: 'clipped', clip: true}, turned),
       pixel('alpha.png', {name: 'clipped 2', clip: true, blend: 'screen', opacity: 0.5}));
  clip('clip-base-group', group({name: 'base', blend: 'normal'}, [pixel('shapes.png', {name: 'shapes'}),
                                pixel('alpha.png', {name: 'alpha', blend: 'multiply'})]),
       pixel('alpha.png', {name: 'clipped', clip: true}, turned));
  // Blend If of top.png, each channel, This Layer and Underlying Layer, hard (64 / 192) and split (32-96 / 160-224);
  // two together; one on a Multiply layer.
  var chans = ['gray', 'red', 'green', 'blue'], which = ['layer', 'under'];
  var ranges = {hard: [64, 64, 192, 192], split: [32, 96, 160, 224]}, none = [0, 0, 255, 255];
  for (i = 0; i < chans.length; i++)
    for (j = 0; j < which.length; j++)
      for (k in ranges)
        if (ranges.hasOwnProperty(k)) {
          var bi = {}, range = {};
          range.layer = which[j] == 'layer' ? ranges[k] : none;
          range.under = which[j] == 'under' ? ranges[k] : none;
          bi[chans[i]] = range;
          c.push({name: 'blendif-' + chans[i] + '-' + which[j] + '-' + k, canvas: TALL, layers: over({blendIf: bi})});
        }
  c.push({name: 'blendif-mixed', canvas: TALL, layers: over({blendIf: {gray: {layer: [0, 0, 128, 255], under: [32, 96, 255, 255]},
                                                                      red: {under: [0, 0, 200, 200]}}})});
  c.push({name: 'blendif-multiply', canvas: TALL, layers: over({blend: 'multiply', blendIf: {gray: {layer: none, under: ranges.split}}})});
  // Advanced Blending's channels: the boxes off.
  c.push({name: 'channels-r-off', canvas: TALL, layers: over({channels: {r: false}})});
  c.push({name: 'channels-gb-off-multiply', canvas: TALL, layers: over({blend: 'multiply', channels: {g: false, b: false}})});
  // An Invert adjustment layer: over ramp.png, at 50%, in Luminosity; in an isolated group and a Pass Through one
  // with shapes.png; clipped to shapes.png.
  var ramp = function () { return pixel('ramp.png', {name: 'ramp'}); };
  c.push({name: 'invert', canvas: SQUARE, layers: [ramp(), invert({name: 'invert'})]});
  c.push({name: 'invert-50', canvas: SQUARE, layers: [ramp(), invert({name: 'invert', opacity: 0.5})]});
  c.push({name: 'invert-luminosity', canvas: SQUARE, layers: [ramp(), invert({name: 'invert', blend: 'luminosity'})]});
  c.push({name: 'invert-group', canvas: SQUARE, layers: [ramp(), group({name: 'group', blend: 'normal'},
          [pixel('shapes.png', {name: 'shapes'}), invert({name: 'invert'})])]});
  c.push({name: 'invert-passthrough', canvas: SQUARE, layers: [ramp(), group({name: 'group', blend: 'passThrough'},
          [pixel('shapes.png', {name: 'shapes'}), invert({name: 'invert'})])]});
  c.push({name: 'invert-clip', canvas: SQUARE, layers: [ramp(), pixel('shapes.png', {name: 'shapes'}),
          invert({name: 'invert', clip: true})]});
  // Each input as Photoshop read it, 16 bits: test_golden_ps checks it holds the file's levels.
  var inputs = [['base', TALL], ['top', TALL], ['photo', SQUARE], ['cube', [190, 190]], ['ramp', SQUARE],
                ['impulse', SQUARE], ['shapes', SQUARE], ['alpha', SQUARE]];
  for (i = 0; i < inputs.length; i++)
    c.push({name: 'in-' + inputs[i][0], canvas: inputs[i][1], bits: 16,
            layers: [pixel(inputs[i][0] + '.png', {name: inputs[i][0]})]});
  return c;
}

if (typeof app != 'undefined') PB.run('blend', cases(), {bits: 8});  // jscheck runs it without Photoshop
