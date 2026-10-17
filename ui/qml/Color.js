.pragma library
.import "Presets.js" as Presets
// Colour maths of the Color panel and the Color Picker (docs/ui-tasks.md, U11): sRGB <-> HSB and CIE Lab (D65), hex,
// copied from the prototype's script (build/proto/ColorPicker.dc.html); the stops of the presets' CSS gradients, for
// Qt Quick's Gradient to draw; the libraries the Swatches and Gradients panels change.

function clamp(x, a, b) { return Math.min(b, Math.max(a, x)) }
// h 0–360, s and v 0–1 → [r, g, b] 0–255
function hsv2rgb(h, s, v) { return [5, 3, 1].map((n) => { const k = (n + h / 60) % 6; return 255 * (v - v * s * Math.max(0, Math.min(k, 4 - k, 1))) }) }
// keeps the previous hue when the colour has none (grey)
function rgb2hsv(c, h0) { const mx = Math.max(c[0], c[1], c[2]), d = mx - Math.min(c[0], c[1], c[2]); return { h: d ? 60 * ((mx === c[0] ? (c[1] - c[2]) / d + 6 : mx === c[1] ? (c[2] - c[0]) / d + 2 : (c[0] - c[1]) / d + 4) % 6) : h0 || 0, s: mx ? d / mx : 0, v: mx / 255 } }
// sRGB <-> CIE Lab (D65)
const WP = [0.95047, 1, 1.08883]
function mul(m, v) { return m.map((r) => r[0] * v[0] + r[1] * v[1] + r[2] * v[2]) }
function rgb2lab(c) { const f = (t) => t > 216 / 24389 ? Math.cbrt(t) : (24389 / 27 * t + 16) / 116; const q = mul([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]], c.map((u) => { u /= 255; return u <= 0.04045 ? u / 12.92 : Math.pow((u + 0.055) / 1.055, 2.4) })).map((t, i) => f(t / WP[i])); return [116 * q[1] - 16, 500 * (q[0] - q[1]), 200 * (q[1] - q[2])] }
function lab2rgb(l) { const fy = (l[0] + 16) / 116; const xyz = [fy + l[1] / 500, fy, fy - l[2] / 200].map((t, i) => WP[i] * (t > 6 / 29 ? t * t * t : (116 * t - 16) * 27 / 24389)); return mul([[3.2404542, -1.5371385, -0.4985314], [-0.9692660, 1.8760108, 0.0415560], [0.0556434, -0.2040259, 1.0572252]], xyz).map((u) => 255 * clamp(u <= 0.0031308 ? 12.92 * u : 1.055 * Math.pow(u, 1 / 2.4) - 0.055, 0, 1)) }
// "E9C7B3", as the fields show it; css(): "#e9c7b3", as Tools keeps it
function hex(c) { return c.slice(0, 3).map((u) => Math.round(u).toString(16).padStart(2, '0')).join('').toUpperCase() }
function css(c) { return '#' + hex(c).toLowerCase() }
function parseHex(t) { const m = /^#?([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(String(t).trim()); if (!m) return null; const s = m[1].length === 3 ? m[1].replace(/./g, '$&$&') : m[1]; return [0, 2, 4].map((i) => parseInt(s.substr(i, 2), 16)) }
// [r, g, b, a] (a 0–1) of "#rgb", "#rrggbb", Qt's "#aarrggbb", "rgb(…)", "rgba(…)", "transparent"; null for anything else
// ("none" of the options bar's cells).
function parse(t) {
    t = String(t).trim().toLowerCase()
    if (t === 'transparent')
        return [0, 0, 0, 0]
    let m = /^#([0-9a-f]{2})([0-9a-f]{6})$/.exec(t)
    if (m)
        return parseHex(m[2]).concat([parseInt(m[1], 16) / 255])
    const c = parseHex(t)
    if (c)
        return c.concat([1])
    m = /^rgba?\(([^)]*)\)$/.exec(t)
    const p = m ? m[1].split(',').map(parseFloat) : []
    return p.length >= 3 && p.every((x) => !isNaN(x)) ? [p[0], p[1], p[2], p.length > 3 ? p[3] : 1] : null
}

// A gradient as Photoshop keeps it: { colors: [{ p, c: [r, g, b], m }], alphas: [{ p, a, m }] }, colour stops and opacity
// stops at p 0–1, m the midpoint to the next stop (0–1 of the way, 0.5 even), each list two stops or more; from the
// CSS of the presets ("linear-gradient(135deg, #e9c7b3, transparent)"): no positions given, evenly spaced, as CSS;
// "transparent" is no colour stop but an opacity stop of 0. The angle is not kept: tiles and bars run left to right.
function gradient(text) {
    const inner = /\((.*)\)\s*$/.exec(String(text)), parts = []
    let depth = 0, cur = ''
    for (const ch of inner ? inner[1] : '') {
        if (ch === ',' && !depth) {
            parts.push(cur.trim())
            cur = ''
            continue
        }
        depth += ch === '(' ? 1 : ch === ')' ? -1 : 0
        cur += ch
    }
    parts.push(cur.trim())
    if (/deg$|^to /.test(parts[0]))
        parts.shift()
    const st = parts.map((s) => { const m = /^(.*?)(?:\s+(-?[\d.]+)%)?$/.exec(s); return { k: parse(m[1]), p: m[2] !== undefined ? m[2] / 100 : NaN } }).filter((x) => x.k)
    if (st.length) {
        if (isNaN(st[0].p)) st[0].p = 0
        if (isNaN(st[st.length - 1].p)) st[st.length - 1].p = 1
    }
    for (let i = 1; i < st.length - 1; i++)  // a run without positions: evenly between the stops around it
        if (isNaN(st[i].p)) {
            let j = i
            while (isNaN(st[j].p)) j++
            for (let k = i; k < j; k++)
                st[k].p = st[i - 1].p + (st[j].p - st[i - 1].p) * (k - i + 1) / (j - i + 1)
            i = j
        }
    const two = (l, none) => l.length >= 2 ? l : [Object.assign({}, l[0] || none, { p: 0 }), Object.assign({}, l[0] || none, { p: 1 })]
    const alphas = st.map((x) => ({ p: x.p, a: x.k[3], m: 0.5 }))
    return { colors: two(st.filter((x) => x.k[3] > 0).map((x) => ({ p: x.p, c: x.k.slice(0, 3), m: 0.5 })), { c: [0, 0, 0], m: 0.5 }),
             alphas: alphas.some((x) => x.a < 1) ? two(alphas) : [{ p: 0, a: 1, m: 0.5 }, { p: 1, a: 1, m: 0.5 }] }
}
// The value of stops l (sorted by p) at p: f(stop) mixed by mix(a, b, u), a midpoint m halfway (linear on each side).
function at(l, p, f, mix) {
    if (p <= l[0].p) return f(l[0])
    for (let i = 0; i < l.length - 1; i++)
        if (p < l[i + 1].p) {
            const t = (p - l[i].p) / (l[i + 1].p - l[i].p), m = clamp(l[i].m, 0.05, 0.95)
            return mix(f(l[i]), f(l[i + 1]), t < m ? t / m / 2 : 0.5 + (t - m) / (1 - m) / 2)
        }
    return f(l[l.length - 1])
}
function sorted(l) { return l.slice().sort((x, y) => x.p - y.p) }
function colorAt(g, p) { return at(sorted(g.colors), p, (s) => s.c, (a, b, u) => a.map((x, i) => x + (b[i] - x) * u)) }
function alphaAt(g, p) { return at(sorted(g.alphas), p, (s) => s.a, (a, b, u) => a + (b - a) * u) }
// "#aarrggbb" of the gradient at p
function argb(g, p) { const a = Math.round(clamp(alphaAt(g, p), 0, 1) * 255); return '#' + a.toString(16).padStart(2, '0') + hex(colorAt(g, p)).toLowerCase() }
// The gradient as two-stop pieces left to right, [{ x, w (0–1), a, b ("#aarrggbb" at its ends) }]: it is linear between
// its stops and midpoints, so a Row of horizontal Gradients draws it exactly; two stops at one place make an edge.
function pieces(g) {
    const ps = [0, 1]
    for (const l of [sorted(g.colors), sorted(g.alphas)])
        l.forEach((s, i) => {
            ps.push(clamp(s.p, 0, 1))
            if (i < l.length - 1 && s.m !== 0.5)
                ps.push(s.p + (l[i + 1].p - s.p) * clamp(s.m, 0.05, 0.95))
        })
    const u = ps.sort((x, y) => x - y).filter((x, i, a) => !i || x > a[i - 1]), e = 1e-7
    return u.slice(1).map((x, i) => ({ x: u[i], w: x - u[i], a: argb(g, u[i] + e), b: argb(g, x - e) }))
}
// whether the gradient lets something through anywhere (a tile then has the transparency checker under it)
function seeThrough(g) { return g.alphas.some((s) => s.a < 1) }

// The Swatches panel's library as Prefs keeps it ("swatches"): { groups: [[name, ["#rrggbb"…]]…], open: [bool…], sel:
// [group, index] or null }, index -1 for the group itself; Presets' groups until the first change. A swatch added goes
// to the end of the group of the one picked (a new group once made; else the last), which opens.
function swatches(st) {
    st = st || {}
    const groups = st.groups || Presets.SW_GROUPS
    return { groups: groups, open: st.open || groups.map((g, i) => i < 2), sel: st.sel || null }
}
function addSwatch(st, hex) {
    st = swatches(st)
    const g = st.sel ? st.sel[0] : st.groups.length - 1, open = st.open.slice()
    open[g] = true
    return { groups: st.groups.map((x, i) => i === g ? [x[0], x[1].concat([hex])] : x), open: open, sel: [g, st.groups[g][1].length] }
}
function deleteSwatch(st) {  // the one picked; the next one (else the one before) is picked then
    st = swatches(st)
    if (!st.sel || st.sel[1] < 0)
        return st
    const g = st.sel[0], c = st.groups[g][1].filter((x, i) => i !== st.sel[1])
    return { groups: st.groups.map((x, i) => i === g ? [x[0], c] : x), open: st.open, sel: [g, Math.min(st.sel[1], c.length - 1)] }
}
function newSwatchGroup(st, name) {
    st = swatches(st)
    return { groups: st.groups.concat([[nextName(st.groups, name), []]]), open: st.open.concat([true]), sel: [st.groups.length, -1] }
}
// "Nhóm 3" after the "Nhóm 2" there is
function nextName(groups, base) {
    let n = 0
    for (const g of groups) {
        const m = String(g[0]).match(new RegExp('^' + base + ' (\\d+)$'))
        if (m) n = Math.max(n, +m[1])
    }
    return base + ' ' + (n + 1)
}

// The Gradients panel's library is Presets.GR_GROUPS itself, which Tools.gradient, its recent ones and Layer Style's
// Gradient Overlay point into by [group, index]: a gradient or a group added is appended to it, so no index moves, and
// one deleted is hidden (Prefs "gradients": { open: [bool…], hidden: ["group,index"…], group: where one is added }).
// ponytail: appended presets live as long as the app (a mock: N-tasks bring a preset store); hidden ones stay in Layer Style's list.
function gradients(st) {
    st = st || {}
    return { open: st.open || Presets.GR_GROUPS.map((g, i) => i < 2), hidden: st.hidden || [], group: st.group === undefined ? -1 : st.group }
}
function addGradient(g, name, text) {  // its index in group g
    Presets.GR_GROUPS[g][1].push([name, text])
    return Presets.GR_GROUPS[g][1].length - 1
}
function newGradientGroup(name) {
    Presets.GR_GROUPS.push([nextName(Presets.GR_GROUPS, name), []])
    return Presets.GR_GROUPS.length - 1
}
const GR0 = Presets.GR_GROUPS.map((g) => g[1].length)  // as the presets come, before any is added
function restoreGradients() {  // drops what was added
    Presets.GR_GROUPS.splice(GR0.length)
    Presets.GR_GROUPS.forEach((g, i) => g[1].splice(GR0[i]))
}
