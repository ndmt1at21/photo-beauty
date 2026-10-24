// Photoshop's images of P1's filters (F31): every filter of plan.md's P0-P1 column of the Filter menu (Blur, Sharpen,
// Noise, Other) with two to five sets of values, for F32-F38's tests; README-filters.md has the steps. Writes
// out/filters/ next to this script: <case>.png, index.json (each case as photo-beauty's scene, the filters as
// "filters" of the layer they ran on, tests/golden.h reads it), photoshop.txt. Inputs from in/ (tests/golden/ps/p1/in,
// B30's): photo.png (a photograph: what the filters do to real pictures), impulse.png (single white pixels on black and
// on 50% grey: the kernels; also the grey that Add Noise's statistics are measured on), ramp.png (smooth ramps, bands
// with hard edges), shapes.png and alpha.png (partly transparent: what a filter does at the edge of a layer's content).
// The kernels' images (of impulse.png, but Add Noise's) are 16 bits: a kernel's tails are below one level of 8 bits,
// and they are small files; the others 8 bits, the 16-bit result rounded, no dither (decision 6 of docs/p1-tasks.md).
// 81 cases, 82 images, about 2.3 MB, about 6 minutes. Not run in Photoshop yet: what it is unsure of is in
// README-filters.md.
//
// A filtered layer: {kind: 'pixel', src, props, filters: [step, ...]}, the steps run in order on that layer after its
// props are set; a step {name, vals, selection, keep}: name as F.SPEC (FilterSpec.js' keys), vals every key of the
// filter (F.vals: FilterSpec's keys, units and ranges, its defaults for those a case leaves out), selection (optional:
// [x, y, w, h] a rectangle, or an input whose alpha is loaded as a soft selection) for that step alone, keep (optional:
// a name: the composite after that step is saved as <keep>.png too, 8 bits, the input of the next step: Reduce Noise
// on noise made by Photoshop). move [dx, dy] (optional): the layer is moved before its filters (pixels moved past the
// canvas are kept, as Photoshop does).
#target photoshop
var HERE = typeof app != 'undefined' ? File($.fileName).parent : null;  // this script's folder (none in jscheck)
#include "lib.jsx"

var F = {};

// ---- The filters ---------------------------------------------------------------------------------------------------

// By photo-beauty's names: FilterSpec.FILTERS' keys for the filters with a dialog, the menu item's name in camelCase
// for the others (MenuSpec.js' "@step:" items). Each value lists the filter's values as FilterSpec's rows: [key,
// default, min, max, decimals] a number (a kernel: 25 whole numbers, row by row), [key, default, [options]] a choice,
// [key, default] a check box.
F.KERNEL = [0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, -1, 5, -1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0];  // Custom's default
F.SPEC = {
  average: [],
  blur: [],
  blurMore: [],
  boxBlur: [['radius', 10, 1, 2000, 0]],
  gaussianBlur: [['radius', 4, 0.1, 1000, 1]],
  motionBlur: [['angle', 0, -360, 360, 0], ['distance', 10, 1, 2000, 0]],
  surfaceBlur: [['radius', 5, 1, 100, 0], ['threshold', 15, 2, 255, 0]],
  sharpen: [],
  sharpenMore: [],
  sharpenEdges: [],
  unsharpMask: [['amount', 120, 1, 500, 0], ['radius', 1.2, 0.1, 1000, 1], ['threshold', 3, 0, 255, 0]],
  addNoise: [['amount', 4, 0.1, 400, 1], ['distribution', 'gaussian', ['uniform', 'gaussian']], ['monochromatic', true]],
  despeckle: [],
  dustScratches: [['radius', 1, 1, 500, 0], ['threshold', 0, 0, 255, 0]],
  median: [['radius', 1, 1, 500, 0]],
  reduceNoise: [['strength', 6, 0, 10, 0], ['preserveDetails', 60, 0, 100, 0], ['reduceColorNoise', 45, 0, 100, 0],
                ['sharpenDetails', 25, 0, 100, 0], ['removeJpegArtifact', false]],
  custom: [['kernel', F.KERNEL, -999, 999, 0], ['scale', 1, 1, 9999, 0], ['offset', 0, -9999, 9999, 0]],
  highPass: [['radius', 10, 0.1, 1000, 1]],
  maximum: [['radius', 1, 1, 500, 0], ['preserve', 'squareness', ['squareness', 'roundness']]],
  minimum: [['radius', 1, 1, 500, 0], ['preserve', 'squareness', ['squareness', 'roundness']]],
  offset: [['horizontal', 0, -30000, 30000, 0], ['vertical', 0, -30000, 30000, 0],
           ['undefinedAreas', 'wrap', ['transparent', 'repeat', 'wrap']]]
};
F.STEP_KEYS = ['name', 'vals', 'selection', 'keep'];

F.isNumber = function (v, min, max, decimals) {
  var k = Math.pow(10, decimals);
  return typeof v == 'number' && v >= min && v <= max && Math.round(v * k) / k == v;
};
// Every value of filter `name`, in FilterSpec's order: those given, checked, and the defaults of the others.
F.vals = function (name, given) {
  var spec = F.SPEC[name], out = {}, i, k;
  if (!spec) throw new Error('no filter ' + name);
  given = given || {};
  for (k in given)
    if (given.hasOwnProperty(k)) {
      var known = false;
      for (i = 0; i < spec.length; i++)
        if (spec[i][0] == k) known = true;
      if (!known) throw new Error(name + ' has no value ' + k);
    }
  for (i = 0; i < spec.length; i++) {
    var s = spec[i], v = given[s[0]] === undefined ? PB.copy(s[1]) : given[s[0]], ok;
    if (s.length == 2) {
      ok = typeof v == 'boolean';
    } else if (s.length == 3) {
      ok = PB.has(s[2], v);
    } else if (s[1] instanceof Array) {
      ok = v instanceof Array && v.length == s[1].length;
      for (var j = 0; ok && j < v.length; j++) ok = F.isNumber(v[j], s[2], s[3], 0);
    } else {
      ok = F.isNumber(v, s[2], s[3], s[4]);
    }
    if (!ok) throw new Error(name + ': ' + s[0] + ' ' + PB.json(v));
    out[s[0]] = v;
  }
  return out;
};
// A step of a layer's filters, checked.
F.step = function (name, vals, more) {
  var st = {name: name, vals: F.vals(name, vals)};
  if (more)
    for (var k in more)
      if (more.hasOwnProperty(k)) {
        if (!PB.has(F.STEP_KEYS, k)) throw new Error(name + ': step key ' + k);
        st[k] = more[k];
      }
  var s = st.selection;
  if (s !== undefined && typeof s != 'string' && !(s instanceof Array && s.length == 4))
    throw new Error(name + ': selection ' + PB.json(s));
  return st;
};

// ---- Running them in Photoshop -------------------------------------------------------------------------------------

F.c = function (s) { return charIDToTypeID(s); };
F.s = function (s) { return stringIDToTypeID(s); };
// An Action Manager event on the active layer: fill(descriptor) puts the values in.
F.event = function (id, fill) {
  var d = new ActionDescriptor();
  if (fill) fill(d);
  executeAction(id, d, DialogModes.NO);
};
// Tries the ways of running a filter in turn until one goes ([[name, function], ...]): what Action Manager takes is not
// sure for some (README-filters.md); photoshop.txt gets the one that ran and why the others did not.
F.tryEach = function (filter, ways) {
  var errors = [];
  for (var i = 0; i < ways.length; i++) {
    try {
      ways[i][1]();
      PB.note('    ' + filter + ': ran as ' + ways[i][0] + (errors.length ? ' (' + errors.join('; ') + ')' : ''));
      return;
    } catch (e) {
      errors.push(ways[i][0] + ': ' + e);
    }
  }
  throw new Error(filter + ' did not run: ' + errors.join('; '));
};
F.radius = function (v) {
  return function (d) { d.putUnitDouble(F.c('Rds '), F.c('#Pxl'), v.radius); };
};
// Maximum and Minimum: Preserve (Photoshop 2019, 20), or the older dialog without it for Squareness.
F.minmax = function (id, name, v) {
  var ways = [['preserveShape', function () {
    F.event(F.c(id), function (d) {
      F.radius(v)(d);
      d.putEnumerated(F.s('preserveShape'), F.s('preserveShape'), F.s(v.preserve));
    });
  }]];
  if (v.preserve == 'squareness') ways.push(['radius only', function () { F.event(F.c(id), F.radius(v)); }]);
  F.tryEach(name, ways);
};
// Reduce Noise in Basic mode: Strength and Preserve Details of the composite channel, the rest of the dialog.
F.reduceNoise = function (v) {
  var desc = function (preserve, preset) {
    return function (d) {
      var list = new ActionList(), ch = new ActionDescriptor(), r = new ActionReference();
      d.putUnitDouble(F.c('ClNs'), F.c('#Prc'), v.reduceColorNoise);
      d.putUnitDouble(F.c('Shrp'), F.c('#Prc'), v.sharpenDetails);
      d.putBoolean(F.s('removeJPEGArtifact'), v.removeJpegArtifact);
      r.putEnumerated(F.c('Chnl'), F.c('Chnl'), F.c('Cmps'));
      ch.putReference(F.c('Chnl'), r);
      ch.putInteger(F.c('Amnt'), v.strength);
      ch.putInteger(preserve, v.preserveDetails);
      list.putObject(F.s('channelDenoiseParams'), ch);
      d.putList(F.s('channelDenoise'), list);
      if (preset) d.putString(F.s('preset'), 'Default');
    };
  };
  F.tryEach('reduceNoise', [
    ['denoise, EdgF', function () { F.event(F.s('denoise'), desc(F.c('EdgF'), false)); }],
    ['denoise, edgeFidelity', function () { F.event(F.s('denoise'), desc(F.s('edgeFidelity'), false)); }],
    ['denoise, EdgF, preset', function () { F.event(F.s('denoise'), desc(F.c('EdgF'), true)); }]
  ]);
};
// Each filter of F.SPEC: function (vals, layer, doc), on the active layer (the DOM's method where it has one).
F.RUN = {
  average: function (v, l) { l.applyAverage(); },
  blur: function (v, l) { l.applyBlur(); },
  blurMore: function (v, l) { l.applyBlurMore(); },
  boxBlur: function (v) { F.event(F.s('boxblur'), F.radius(v)); },
  gaussianBlur: function (v) { F.event(F.c('GsnB'), F.radius(v)); },  // the DOM's applyGaussianBlur stops at 250
  motionBlur: function (v, l) { l.applyMotionBlur(v.angle, v.distance); },
  surfaceBlur: function (v) {
    F.event(F.s('surfaceBlur'), function (d) {
      F.radius(v)(d);
      d.putInteger(F.c('Thsh'), v.threshold);
    });
  },
  sharpen: function (v, l) { l.applySharpen(); },
  sharpenMore: function (v, l) { l.applySharpenMore(); },
  sharpenEdges: function (v, l) { l.applySharpenEdges(); },
  unsharpMask: function (v, l) { l.applyUnSharpMask(v.amount, v.radius, v.threshold); },
  addNoise: function (v, l) {
    l.applyAddNoise(v.amount, v.distribution == 'uniform' ? NoiseDistribution.UNIFORM : NoiseDistribution.GAUSSIAN,
                    v.monochromatic);
  },
  despeckle: function (v, l) { l.applyDespeckle(); },
  dustScratches: function (v, l) { l.applyDustAndScratches(v.radius, v.threshold); },
  median: function (v, l) { l.applyMedianNoise(v.radius); },
  reduceNoise: function (v) { F.reduceNoise(v); },
  custom: function (v, l) { l.applyCustomFilter(v.kernel, v.scale, v.offset); },
  highPass: function (v, l) { l.applyHighPass(v.radius); },
  maximum: function (v) { F.minmax('Mxm ', 'maximum', v); },
  minimum: function (v) { F.minmax('Mnm ', 'minimum', v); },
  offset: function (v, l) {
    var areas = {transparent: OffsetUndefinedAreas.SETTOBACKGROUND, repeat: OffsetUndefinedAreas.REPEATEDGEPIXELS,
                 wrap: OffsetUndefinedAreas.WRAPAROUND};
    l.applyOffset(v.horizontal, v.vertical, areas[v.undefinedAreas]);
  }
};

// The selection of a step: a rectangle, or an input's transparency (lib.jsx's PB.maskFrom's way); then the layer active
// again, its pixels (not a mask) the target.
F.select = function (doc, layer, s) {
  if (s instanceof Array) {
    doc.selection.select([[s[0], s[1]], [s[0] + s[2], s[1]], [s[0] + s[2], s[1] + s[3]], [s[0], s[1] + s[3]]]);
  } else if (s !== undefined) {
    var tmp = PB.copyLayer(doc, s, 0), d = new ActionDescriptor(), r = new ActionReference(), t = new ActionReference();
    doc.activeLayer = tmp;
    r.putProperty(F.c('Chnl'), F.c('fsel'));
    d.putReference(F.c('null'), r);
    t.putEnumerated(F.c('Chnl'), F.c('Chnl'), F.c('Trsp'));
    d.putReference(F.c('T   '), t);
    executeAction(F.c('setd'), d, DialogModes.NO);
    tmp.remove();
  }
  doc.activeLayer = layer;
  try {
    doc.activeChannels = doc.componentChannels;
  } catch (e) {}
};
// A layer's filters (PB.layerKeys.filters): its move first, then each step on it.
F.apply = function (steps, layer, doc, l) {
  if (l.move) layer.translate(l.move[0], l.move[1]);
  for (var i = 0; i < steps.length; i++) {
    var st = steps[i];
    if (!F.RUN[st.name]) throw new Error('no filter ' + st.name);
    F.select(doc, layer, st.selection);
    PB.note('    ' + st.name + ' ' + PB.json(st.vals) + (st.selection !== undefined ? ' in ' + PB.json(st.selection) : ''));
    F.RUN[st.name](st.vals, layer, doc);
    doc.selection.deselect();
    if (st.keep) {
      PB.save(doc, st.keep, 8);
      doc.activeLayer = layer;
    }
  }
};
PB.layerKeys.filters = F.apply;
PB.layerKeys.move = function () {};  // F.apply moves the layer, before its filters

// ---- The cases -----------------------------------------------------------------------------------------------------

F.SQUARE = [256, 256];
F.GREY = [0, 145, 256, 31];  // impulse.png's 50% grey between two rows of its dots (rows 144 and 176): no dot in it
// A case: one layer of input src and its filters (a step or a list); more: bits (16 for the kernels), move.
F.one = function (name, src, steps, more) {
  var l = {kind: 'pixel', src: src, props: {name: src.replace(/\.png$/, '')}};
  if (more && more.move) l.move = more.move;
  l.filters = steps instanceof Array ? steps : [steps];
  var c = {name: name, canvas: F.SQUARE, layers: [l]};
  if (more && more.bits) c.bits = more.bits;
  return c;
};

function cases() {
  var c = [], s = F.step, deep = {bits: 16}, i;
  var add = function (name, src, steps, more) { c.push(F.one(name, src, steps, more)); };

  // Blur. Average: the whole layer, a rectangle of it, a partly transparent one (its alpha in the average).
  add('average-photo', 'photo.png', s('average'));
  add('average-ramp-rect', 'ramp.png', s('average', {}, {selection: [48, 40, 160, 96]}));
  add('average-shapes', 'shapes.png', s('average'));
  // The fixed kernels of Blur, Blur More, Sharpen, Sharpen More (F33 reads them off the impulses), and at the edges of
  // shapes.png's content.
  var fixed = ['blur', 'blurMore', 'sharpen', 'sharpenMore'];
  for (i = 0; i < fixed.length; i++) {
    add(fixed[i] + '-impulse', 'impulse.png', s(fixed[i]), deep);
    add(fixed[i] + '-shapes', 'shapes.png', s(fixed[i]));
  }
  add('boxBlur-impulse-r1', 'impulse.png', s('boxBlur', {radius: 1}), deep);
  add('boxBlur-impulse-r4', 'impulse.png', s('boxBlur', {radius: 4}), deep);
  add('boxBlur-photo-r10', 'photo.png', s('boxBlur', {radius: 10}));
  add('boxBlur-shapes-r6', 'shapes.png', s('boxBlur', {radius: 6}));
  // Gaussian Blur: F32's radii, the kernels of 0.5 to 5 (k of sigma_of_radius) and photographs at 10, 50, 250 (the
  // canvas's edge); content edges, an alpha ramp, a soft selection (F30: the old and the filtered mixed by it).
  var small = [[0.5, '0_5'], [1, '1'], [2, '2'], [5, '5']];
  for (i = 0; i < small.length; i++)
    add('gaussianBlur-impulse-r' + small[i][1], 'impulse.png', s('gaussianBlur', {radius: small[i][0]}), deep);
  var large = [10, 50, 250];
  for (i = 0; i < large.length; i++) add('gaussianBlur-photo-r' + large[i], 'photo.png', s('gaussianBlur', {radius: large[i]}));
  add('gaussianBlur-shapes-r4', 'shapes.png', s('gaussianBlur', {radius: 4}));
  add('gaussianBlur-alpha-r8', 'alpha.png', s('gaussianBlur', {radius: 8}));
  add('gaussianBlur-shapes-r3-softsel', 'shapes.png', s('gaussianBlur', {radius: 3}, {selection: 'alpha.png'}));
  // Motion Blur: the line's angle and length (an odd and an even one), sampled off the axes.
  var motion = [[0, 10], [0, 9], [30, 20], [-45, 15], [90, 12]];
  for (i = 0; i < motion.length; i++)
    add('motionBlur-impulse-a' + motion[i][0] + '-d' + motion[i][1], 'impulse.png',
        s('motionBlur', {angle: motion[i][0], distance: motion[i][1]}), deep);
  add('motionBlur-photo-a30-d25', 'photo.png', s('motionBlur', {angle: 30, distance: 25}));
  add('motionBlur-shapes-a0-d20', 'shapes.png', s('motionBlur', {angle: 0, distance: 20}));
  // Surface Blur: two photographs; threshold 255 on the impulses shows the window's shape and weights (F34).
  add('surfaceBlur-photo-r5-t15', 'photo.png', s('surfaceBlur', {radius: 5, threshold: 15}));
  add('surfaceBlur-photo-r12-t40', 'photo.png', s('surfaceBlur', {radius: 12, threshold: 40}));
  add('surfaceBlur-impulse-r6-t255', 'impulse.png', s('surfaceBlur', {radius: 6, threshold: 255}), deep);
  add('surfaceBlur-shapes-r8-t60', 'shapes.png', s('surfaceBlur', {radius: 8, threshold: 60}));
  add('surfaceBlur-ramp-r10-t20', 'ramp.png', s('surfaceBlur', {radius: 10, threshold: 20}));

  // Sharpen: Sharpen Edges (a fixed Unsharp Mask with a threshold: F32 finds it), Unsharp Mask (the defaults, strong,
  // a threshold on smooth ramps, the kernel, content edges).
  add('sharpenEdges-impulse', 'impulse.png', s('sharpenEdges'), deep);
  add('sharpenEdges-photo', 'photo.png', s('sharpenEdges'));
  add('sharpenEdges-ramp', 'ramp.png', s('sharpenEdges'));
  add('unsharpMask-photo-a120-r1_2-t3', 'photo.png', s('unsharpMask', {amount: 120, radius: 1.2, threshold: 3}));
  add('unsharpMask-photo-a300-r3-t0', 'photo.png', s('unsharpMask', {amount: 300, radius: 3, threshold: 0}));
  add('unsharpMask-ramp-a200-r4-t8', 'ramp.png', s('unsharpMask', {amount: 200, radius: 4, threshold: 8}));
  add('unsharpMask-impulse-a100-r2-t0', 'impulse.png', s('unsharpMask', {amount: 100, radius: 2, threshold: 0}), deep);
  add('unsharpMask-shapes-a150-r4-t0', 'shapes.png', s('unsharpMask', {amount: 150, radius: 4, threshold: 0}));

  // Noise. Add Noise on impulse.png's 50% grey between two rows of dots (F.GREY), for its statistics (F37: the mean
  // kept, the spread against Amount, Monochromatic one noise for the three channels); Photoshop's numbers are random.
  var noise = [['gaussian', 10, true], ['gaussian', 10, false], ['uniform', 10, true], ['gaussian', 25, true]];
  for (i = 0; i < noise.length; i++)
    add('addNoise-impulse-' + noise[i][0] + noise[i][1] + (noise[i][2] ? '-mono' : ''), 'impulse.png',
        s('addNoise', {amount: noise[i][1], distribution: noise[i][0], monochromatic: noise[i][2]}, {selection: F.GREY}));
  // Despeckle: a photograph, isolated dots, smooth ramps (unchanged); Dust & Scratches, Median: their radius and
  // threshold, the window's shape at shapes.png's corners.
  add('despeckle-photo', 'photo.png', s('despeckle'));
  add('despeckle-impulse', 'impulse.png', s('despeckle'), deep);
  add('despeckle-ramp', 'ramp.png', s('despeckle'));
  add('dustScratches-photo-r3-t20', 'photo.png', s('dustScratches', {radius: 3, threshold: 20}));
  add('dustScratches-impulse-r2-t0', 'impulse.png', s('dustScratches', {radius: 2, threshold: 0}), deep);
  add('dustScratches-ramp-r4-t10', 'ramp.png', s('dustScratches', {radius: 4, threshold: 10}));
  add('median-photo-r3', 'photo.png', s('median', {radius: 3}));
  add('median-photo-r10', 'photo.png', s('median', {radius: 10}));
  add('median-shapes-r6', 'shapes.png', s('median', {radius: 6}));
  // Reduce Noise: the defaults and the other end of each slider on the photograph; and on noise of Photoshop's own,
  // colour noise added to the middle of ramp.png first, that noisy image kept (reduceNoise-ramp-noisy-in.png) as
  // the input F37 compares from.
  add('reduceNoise-photo', 'photo.png', s('reduceNoise'));
  add('reduceNoise-photo-s10-p0-c100-d0-jpeg', 'photo.png', s('reduceNoise', {strength: 10, preserveDetails: 0,
      reduceColorNoise: 100, sharpenDetails: 0, removeJpegArtifact: true}));
  add('reduceNoise-ramp-noisy', 'ramp.png', [
      s('addNoise', {amount: 8, distribution: 'gaussian', monochromatic: false},
        {selection: [0, 64, 256, 128], keep: 'reduceNoise-ramp-noisy-in'}),
      s('reduceNoise')]);

  // Other. High Pass: v - G(v) + 0.5 (F32), its kernel, content edges.
  add('highPass-photo-r10', 'photo.png', s('highPass', {radius: 10}));
  add('highPass-impulse-r2', 'impulse.png', s('highPass', {radius: 2}), deep);
  add('highPass-shapes-r4', 'shapes.png', s('highPass', {radius: 4}));
  // Maximum, Minimum: the square and the round window (F36) on dots, a photograph, shapes.png (its alpha too) and the
  // alpha ramp.
  add('maximum-impulse-r4-square', 'impulse.png', s('maximum', {radius: 4, preserve: 'squareness'}), deep);
  add('maximum-impulse-r4-round', 'impulse.png', s('maximum', {radius: 4, preserve: 'roundness'}), deep);
  add('maximum-photo-r3-round', 'photo.png', s('maximum', {radius: 3, preserve: 'roundness'}));
  add('maximum-shapes-r6-square', 'shapes.png', s('maximum', {radius: 6, preserve: 'squareness'}));
  add('maximum-alpha-r8-square', 'alpha.png', s('maximum', {radius: 8, preserve: 'squareness'}));
  add('minimum-photo-r3-square', 'photo.png', s('minimum', {radius: 3, preserve: 'squareness'}));
  add('minimum-shapes-r6-square', 'shapes.png', s('minimum', {radius: 6, preserve: 'squareness'}));
  add('minimum-shapes-r6-round', 'shapes.png', s('minimum', {radius: 6, preserve: 'roundness'}));
  // Offset: the three Undefined Areas, negative and past the canvas; a layer moved partly off the canvas first (F38:
  // what Wrap Around does with the pixels past it).
  add('offset-ramp-64-32-wrap', 'ramp.png', s('offset', {horizontal: 64, vertical: 32, undefinedAreas: 'wrap'}));
  add('offset-ramp-m100-50-repeat', 'ramp.png', s('offset', {horizontal: -100, vertical: 50, undefinedAreas: 'repeat'}));
  add('offset-ramp-300-0-wrap', 'ramp.png', s('offset', {horizontal: 300, vertical: 0, undefinedAreas: 'wrap'}));
  add('offset-shapes-40-m30-transparent', 'shapes.png',
      s('offset', {horizontal: 40, vertical: -30, undefinedAreas: 'transparent'}));
  add('offset-shapes-moved-m64-0-wrap', 'shapes.png', s('offset', {horizontal: -64, vertical: 0, undefinedAreas: 'wrap'}),
      {move: [96, 0]});
  // Custom: one 1 at the top right of the 5 x 5 (which way the kernel is applied: correlation moves a dot down left),
  // the default kernel on dots, a horizontal derivative with Offset 128, a 3 x 3 box with Scale 9 (its rounding).
  var shift = [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  var derivative = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  var box = [0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0];
  add('custom-impulse-shift', 'impulse.png', s('custom', {kernel: shift, scale: 1, offset: 0}), deep);
  add('custom-impulse-default', 'impulse.png', s('custom'), deep);
  add('custom-ramp-derivative-o128', 'ramp.png', s('custom', {kernel: derivative, scale: 1, offset: 128}));
  add('custom-shapes-box-s9', 'shapes.png', s('custom', {kernel: box, scale: 9, offset: 0}));
  return c;
}

if (typeof app != 'undefined') PB.run('filters', cases(), {bits: 8, dir: HERE});  // jscheck runs it without Photoshop
