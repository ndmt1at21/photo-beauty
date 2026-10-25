.pragma library
// Numbers as the UI prints and reads them (vn, sg, zt, pnum of the prototype's script), lengths at a resolution, and
// the sums a number field takes ("1240*2", "5cm"). In the UI's language (U30): 1.234,5 in Vietnamese, 1,234.5 in
// English (Qt.uiLanguage "en", as main.cpp sets it; tests leave it "", so Vietnamese, the language of the sources);
// "−" in both.

const PER_INCH = { in: 1, cm: 2.54, mm: 25.4, pt: 72 }  // px: the resolution

// The decimal mark and the thousands separator of the UI's language: [",", "."] or [".", ","].
function marks() { return Qt.uiLanguage === "en" ? [".", ","] : [",", "."] }

// 1.234,5 (1,234.5) with `dec` decimals; "−" only when the rounded number is not 0.
function vn(x, dec) {
    const s = Math.abs(x).toFixed(dec || 0).split("."), m = marks()
    return (x < 0 && Math.abs(x) >= Math.pow(10, -(dec || 0)) / 2 ? "−" : "") + s[0].replace(/\B(?=(\d{3})+(?!\d))/g, m[1]) + (s[1] ? m[0] + s[1] : "")
}
// With "+" before a positive number: sliders that go both ways.
function sg(x, dec) { return (x > 0 ? "+" : "") + vn(x, dec) }
// A zoom percentage, up to two decimals: 12.5 → "12,5%" ("12.5%"). View.js's zoom 1 is zt(100).
function zt(z) { return String(Math.round(z * 100) / 100).replace(".", marks()[0]) + "%" }
// What follows a number in unit u: "62%", "30°", "2.480 px".
function suffix(u) { return !u || u === "%" || u === "°" ? u || "" : " " + u }
function unit(x, u, dec) { return vn(x, dec) + suffix(u) }

// Digits with the language's decimal mark and thousands separator ("1.234,5", "1,234.5"); with no decimal mark, a
// separator that groups no thousands is one ("12.5", "0.125" in Vietnamese, "12,5" in English), where the prototype
// read 125.
function digits(s) {
    const m = marks()
    if (s.indexOf(m[0]) >= 0) s = s.split(m[1]).join("").replace(m[0], ".")
    else if (/^[1-9]\d{0,2}([.,]\d{3})+$/.test(s)) s = s.split(m[1]).join("")
    else s = s.replace(m[1], ".")
    return /^(\d+\.?\d*|\.\d+)$/.test(s) ? parseFloat(s) : NaN
}
// The number a text starts with ("12,5%" → 12.5, "−3" → −3), null when none.
function pnum(t) {
    const m = /^\s*([-+−]?)\s*([\d.,]+)/.exec(String(t))
    const x = m ? digits(m[2]) : NaN
    return isNaN(x) ? null : m[1] && m[1] !== "+" ? -x : x
}

// x in unit `from` as unit `to`: px, in, cm, mm, pt at `ppi`, % of `ref` px. NaN when one is no length.
function convert(x, from, to, ppi, ref) {
    if (from === to) return x
    if (PER_INCH[from] && PER_INCH[to]) return x / PER_INCH[from] * PER_INCH[to]
    const px = from === "px" ? x : from === "%" ? x / 100 * ref : x / PER_INCH[from] * ppi
    return to === "px" ? px : to === "%" ? px / ref * 100 : px / ppi * PER_INCH[to]
}

// What a field of unit u reads from `text`: numbers joined by + − * / and brackets, each in u or its
// own unit ("5cm", "2in", "300px" at `ppi`; "50%" is half of `ref`, a value in u, unless u is "%").
// NaN when it is no such sum.
function parse(text, u, ppi, ref) {
    const src = String(text).replace(/−/g, "-").replace(/×/g, "*").toLowerCase()
    u = (u || "").toLowerCase()
    let i = 0
    const peek = () => { while (src[i] === " ") i++; return src[i] }
    function expr() {
        let v = term()
        for (let c = peek(); c === "+" || c === "-"; c = peek()) { i++; const r = term(); v = c === "+" ? v + r : v - r }
        return v
    }
    function term() {
        let v = factor()
        for (let c = peek(); c === "*" || c === "/"; c = peek()) { i++; const r = factor(); v = c === "*" ? v * r : v / r }
        return v
    }
    function factor() {
        const c = peek()
        if (c === "-" || c === "+") { i++; const v = factor(); return c === "-" ? -v : v }
        let v
        if (c === "(") {
            i++
            v = expr()
            if (peek() !== ")") return NaN
            i++
        } else {
            const m = /^[\d.,]+/.exec(src.slice(i))
            if (!m) return NaN
            i += m[0].length
            v = digits(m[0])
        }
        peek()
        if (u && src.startsWith(u, i)) { i += u.length; return v }
        const w = /^(px|in|cm|mm|pt|%|°)/.exec(src.slice(i))
        if (!w) return v
        i += w[0].length
        return w[0] === "%" ? v / 100 * ref : convert(v, w[0], u, ppi)
    }
    const v = expr()
    return peek() === undefined && isFinite(v) ? v : NaN
}
