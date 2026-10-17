.pragma library
.import "Mock.js" as Mock
// Where the canvas shows the document (docs/ui-tasks.md, Một hàm đổi tọa độ): the View of core/render.h, so the mock
// canvas, V1's Canvas and every overlay map points one way.
//   view { x, y: the document point at the frame's centre (pan); zoom: device pixels per document pixel, 1 is 100 %;
//          angle: the view's rotation, degrees clockwise }: Doc.view, Viewport.view
//   size { w, h: the frame in logical pixels (Qt's item coordinates); dpr: device pixels per logical pixel, 1 if left
//          out }: Viewport.size
// Canvas points are logical pixels from the frame's top left; document points are pixels from the canvas's top left,
// pixel (i, j) covering [i, i + 1) × [j, j + 1). Points are { x, y }; each function returns a new object.

var MIN_ZOOM = 0.01;  // 1 %
var MAX_ZOOM = 128;  // 12.800 %

function clampZoom(z) { return Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, z)); }
// Logical pixels per document pixel.
function scale(view, size) { return view.zoom / (size.dpr || 1); }

function toCanvas(view, size, p) {
  const s = scale(view, size), a = view.angle * Math.PI / 180, c = Math.cos(a), n = Math.sin(a);
  const dx = (p.x - view.x) * s, dy = (p.y - view.y) * s;
  return { x: size.w / 2 + dx * c - dy * n, y: size.h / 2 + dx * n + dy * c };
}
function toDoc(view, size, p) {
  const s = scale(view, size), a = view.angle * Math.PI / 180, c = Math.cos(a), n = Math.sin(a);
  const dx = p.x - size.w / 2, dy = p.y - size.h / 2;
  return { x: view.x + (dx * c + dy * n) / s, y: view.y + (dy * c - dx * n) / s };
}

// Zoomed to `zoom` (1 to 12.800 %, to 1/100 %) about canvas point p: the document point under p stays there.
function zoomAt(view, size, p, zoom) {
  const z = Math.round(clampZoom(zoom) * 1e4) / 1e4;
  const d = toDoc(view, size, p), q = toDoc({ x: view.x, y: view.y, zoom: z, angle: view.angle }, size, p);
  return { x: view.x + d.x - q.x, y: view.y + d.y - q.y, zoom: z, angle: view.angle };
}
// Turned to `angle` degrees clockwise, in (−180, 180], about the frame's centre: the point there stays.
function rotateTo(view, angle) {
  let a = angle % 360;
  if (a > 180) a -= 360;
  if (a <= -180) a += 360;
  return { x: view.x, y: view.y, zoom: view.zoom, angle: a };
}
// The picture moved dx, dy logical pixels across the frame.
function pan(view, size, dx, dy) {
  const c = toDoc(view, size, { x: size.w / 2 - dx, y: size.h / 2 - dy });
  return { x: c.x, y: c.y, zoom: view.zoom, angle: view.angle };
}
// Vừa màn hình: the zoom that shows a w × h document whole, turned by `angle`, 20 px inside the frame (Mock.fitZoom()).
function fit(w, h, size, angle) {
  const a = (angle || 0) * Math.PI / 180, c = Math.abs(Math.cos(a)), n = Math.abs(Math.sin(a));
  const z = Math.min((size.w - 40) / (w * c + h * n), (size.h - 40) / (w * n + h * c)) * (size.dpr || 1);
  return Math.round(clampZoom(z) * 1e4) / 1e4;
}
// Moved as little as it takes for the w × h document's box on the canvas to cover the frame, or to sit in its middle
// along an axis where the box is smaller: Photoshop's pan, no overscroll. The view itself when it already does.
function constrain(view, size, w, h) {
  const xs = [], ys = [];
  for (const p of [{ x: 0, y: 0 }, { x: w, y: 0 }, { x: 0, y: h }, { x: w, y: h }]) {
    const q = toCanvas(view, size, p);
    xs.push(q.x);
    ys.push(q.y);
  }
  const dx = shift(Math.min.apply(null, xs), Math.max.apply(null, xs), size.w);
  const dy = shift(Math.min.apply(null, ys), Math.max.apply(null, ys), size.h);
  return Math.abs(dx) < 1e-6 && Math.abs(dy) < 1e-6 ? view : pan(view, size, dx, dy);
}
function shift(lo, hi, len) {  // what moves [lo, hi] over [0, len], or into its middle
  if (hi - lo <= len) return (len - lo - hi) / 2;
  return lo > 0 ? -lo : hi < len ? len - hi : 0;
}

// The zoom of the next step of Mock.ZOOMS up (dir > 0) or down; zoom itself past the last.
function step(zoom, dir) {
  const z = zoom * 100;
  const L = Mock.ZOOMS.filter((x) => (dir > 0 ? x > z + 0.001 : x < z - 0.001));
  return (L.length ? (dir > 0 ? L[0] : L[L.length - 1]) : z) / 100;
}
