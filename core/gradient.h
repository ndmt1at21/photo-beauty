#pragma once
// Gradients (D30): one definition, Photoshop's, for Gradient Map (D38), the Gradient fill layer (D40), Gradient Overlay
// (L34), the Gradient tool (C36, C37) and the Gradients panel's presets. A Gradient is colour and opacity stops (JSON
// as the UI keeps it); a method mixes them into a table along t, 0 at the gradient's start and 1 at its end
// (GradientLut, made on the CPU, where the transcendental functions are); a shape lays t over the document's pixels
// (GradientShape); render_gradient() draws the result into tiles, dithered or not. shaders/lib/gradient.slang has the
// shape, the lookup and the dither line by line, float operation for float operation, so a GPU composite gives
// render_gradient()'s samples to the bit (tests/test_gradient.cpp, through shaders/gradient_check.slang).
//
// Photoshop publishes its Classic mix without Smoothness and the five shapes; how midpoints, Smoothness, Perceptual,
// Smooth and Stripes mix, the 16-bit dither and how a fill's angle and scale span its box are guesses, named at the top
// of gradient.cpp until D32's Photoshop images settle them (B38).
#include <array>
#include <cstdint>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

#include "color.h"
#include "tile.h"

namespace pb {

// ---- The gradient --------------------------------------------------------------------------------------------------

// A colour stop: at p (0..1 along the gradient) colour c, the document's encoded values as the UI's numbers (0..255 a
// channel, fractions allowed; a grey document's R = G = B), or the foreground or background colour of when the
// gradient is used (Photoshop's stop types: with_colors() puts them in c). m: where between this stop and the next their
// mix is half and half, 0.05..0.95 of the way (the Gradient Editor's diamond).
struct ColorStop {
  enum class Type : uint8_t { User, Foreground, Background };
  double p = 0;
  std::array<double, 3> c{0, 0, 0};
  double m = 0.5;
  Type type = Type::User;
};
// An opacity stop: at p, opacity a (0..1), midpoint m to the next.
struct AlphaStop {
  double p = 0, a = 1, m = 0.5;
};

// A gradient as Photoshop's Gradient Editor keeps it. Each list holds a stop or more in any order: they are taken by p,
// stops at one p in list order (an edge). Photoshop keeps p in 4096ths (PSD's Lctn), and so does the table: a stop at
// p = k / 4096 is exactly its colour there. Default: Photoshop's Foreground to Background, opaque.
struct Gradient {
  std::vector<ColorStop> colors{{0, {0, 0, 0}, 0.5, ColorStop::Type::Foreground},
                                {1, {255, 255, 255}, 0.5, ColorStop::Type::Background}};
  std::vector<AlphaStop> alphas{{0, 1, 0.5}, {1, 1, 0.5}};
  double smoothness = 100;  // %: Classic's easing at the stops (100 % on Photoshop's presets and new gradients)
  std::string name;         // a preset's, PSD's Nm; "" for none
};

// JSON, the UI's GradientBar value (ui/qml/Color.js gradient()) and a little more:
//   {"colors": [{"p": 0, "c": [r, g, b], "m": 0.5, "type": "fg"}, ...], "alphas": [{"p": 0, "a": 1, "m": 0.5}, ...],
//    "smoothness": 100, "name": "..."}
// "type" "fg" or "bg" for the dynamic stops (left out for a user colour; such a stop's "c" is optional: the colour it
// showed). Every stop has "p", a colour stop "c" unless it is dynamic, an opacity stop "a"; "m" defaults to 0.5,
// "alphas" to opaque, "smoothness" to 100. Numbers out of range are clamped (p, a 0..1, m 0.05..0.95, c 0..255,
// smoothness 0..100), keys it does not know skipped. nullopt for JSON that is malformed, not an object, without a colour
// stop, missing a key a stop needs, or with a key of the wrong type. Any thread.
std::optional<Gradient> gradient_from(const nlohmann::json&);
std::optional<Gradient> gradient_from_json(const std::string&);
// Members sorted, no spaces (as LayerProps' JSON keeps objects): gradient_from_json() reads back the same gradient and
// to_json() of that writes the same bytes. Stops in their list order.
std::string to_json(const Gradient&);
// g with its Foreground and Background stops in colours fg, bg (0..255 a channel, as c), as user colours: what the
// Gradient tool draws with, and what a fill layer keeps when made (Photoshop's do not follow the colours later).
Gradient with_colors(Gradient g, const std::array<double, 3>& fg, const std::array<double, 3>& bg);

// ---- Mixing: the table along t ------------------------------------------------------------------------------------

// How a stop mixes with the next (Photoshop 2023's Method; Perceptual is its default): Classic their encoded values, as
// Photoshop always did, eased by Smoothness; Linear their light (the document profile's curve); Perceptual in Oklab;
// Smooth as Perceptual, eased at each stop; Stripes not at all, a stop's colour holding up to the midpoint. Opacity
// mixes linearly but in Stripes (held), Smooth (eased) and Classic (Smoothness).
enum class GradientMethod : uint8_t { Perceptual, Linear, Classic, Smooth, Stripes };
inline constexpr const char* kGradientMethodNames[] = {"perceptual", "linear", "classic", "smooth", "stripes"};
std::optional<GradientMethod> gradient_method(const std::string& name);  // any case: also ToolSpec's "Perceptual"

// What a stop's numbers mean: the document's colours.
struct GradientSpace {
  // The document's profile, source_profile() of its ICC (color.h): its curves to linear light (Linear) and its
  // primaries (Perceptual, Smooth). Null: sRGB's.
  std::shared_ptr<const Source> profile;
  bool grey = false;  // a grey document: each colour stop becomes its grey first, and the table R = G = B
};

// A gradient's colours along t, as render_gradient() and the GPU (gradient.slang's gradient_lookup()) read them.
struct GradientLut {
  static constexpr int kSteps = 4096;  // entry i is t = i / kSteps
  // R, G, B, alpha, the document's encoded values 0..1, not premultiplied. Entry kSteps + 1 repeats entry kSteps, so
  // that a lookup reads entries i and i + 1 for any t without a bound. 64 KB, laid out as the GPU's float4.
  std::array<std::array<float, 4>, kSteps + 2> rgba;
  // At t (clamped to 0..1, NaN 0): linear between the entries around it.
  std::array<float, 4> at(float t) const;
};
// About a millisecond (4097 mixes): cache it by what made it (the JSON and the document's profile), as tables of
// adjustments are. Any thread.
std::shared_ptr<const GradientLut> gradient_lut(const Gradient&, GradientMethod, const GradientSpace& = {});

// ---- Shapes: t over the document ----------------------------------------------------------------------------------

// Photoshop's five, by a line from start (x0, y0) to end (x1, y1): Linear t along it, 0 before the start and 1 past the
// end; Radial the distance from the start over the line's length; Angle the turn about the start counterclockwise on
// screen from the line, 0 to 1; Reflected Linear both ways from the start; Diamond a square about the start with a
// corner at the end. t past 1 is 1. A line of no length gives t = 0 everywhere.
enum class GradientStyle : uint8_t { Linear, Radial, Angle, Reflected, Diamond };
inline constexpr const char* kGradientStyleNames[] = {"linear", "radial", "angle", "reflected", "diamond"};
std::optional<GradientStyle> gradient_style(const std::string& name);  // any case

// Document pixels: pixel (x, y) is the square from (x, y) to (x + 1, y + 1), and t at its centre is its value. The
// Gradient tool's drag (C36), or fill_line()'s for a fill layer or overlay.
struct GradientLine {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct GradientShape {
  GradientStyle style = GradientStyle::Linear;
  GradientLine line;
  bool reverse = false;  // t becomes 1 - t: the colours the other way
};
// The shape at mip level `level`, whose pixels cover 2^level document pixels across: its line scaled down.
GradientShape at_level(GradientShape, int level);

// A shape as the CPU and the GPU work it out pixel by pixel, in single precision (gradient.slang's GradientPlane, 32
// bytes: a GPU buffer or push constants take it as it is).
struct GradientPlane {
  uint32_t style = 0;    // GradientStyle's value
  uint32_t reverse = 0;  // 1: 1 - t
  float sx = 0, sy = 0;  // the start
  float ax = 0, ay = 0;  // (end - start) / |end - start|²: t along the line is (p - start) · a
  float inv_len = 0;     // 1 / |end - start|; 0 with a for a line of no length
  uint32_t pad = 0;
};
GradientPlane plane(const GradientShape&);
// t at pixel (x, y)'s centre, 0..1, reversed if the shape is: gradient.slang's gradient_t() to the bit. Angle's turn
// comes from a polynomial (Abramowitz and Stegun 4.4.49, 2e-8 rad), sums, products and a quotient only.
float gradient_t(const GradientPlane&, int x, int y);

// ---- Fill layers and Gradient Overlay -----------------------------------------------------------------------------

// What a Gradient fill layer (D40's `content`) and Gradient Overlay (L34, L30's `gradientOverlay`) share, keys as
// LayerStyleDialog's gradientOverlay: {"gradient": {...}, "method": "perceptual", "style": "linear", "angle": 90,
// "scale": 100, "reverse": false, "dither": false, "align": true, "offset": [0, 0]}. The box the gradient spans is the
// caller's: the layer's (Align with Layer: D40 the mask's bounds, L34 the content's) or the canvas.
struct GradientFill {
  Gradient gradient;
  GradientMethod method = GradientMethod::Perceptual;
  GradientStyle style = GradientStyle::Linear;
  double angle = 90;   // degrees, counterclockwise from 3 o'clock: the way from the start colour to the end's
  double scale = 100;  // %, 1..1000 (Photoshop's dialogs: 10..150)
  bool reverse = false, dither = false;
  bool align = true;                    // Align with Layer
  std::array<double, 2> offset{0, 0};  // the centre's shift, % of the box's width and height, right and down
};
// Those keys of an object, defaults for what it leaves out, others skipped (on, blend, opacity: the caller's). nullopt
// for a key of the wrong type, an unknown method or style, a gradient gradient_from() does not take. Any thread.
std::optional<GradientFill> gradient_fill_from(const nlohmann::json&);
std::string to_json(const GradientFill&);  // every key, sorted, no spaces
// The line over box (x, y, w, h), document pixels: through the box's centre (shifted by offset) along angle, long
// enough to span the box (gradient.cpp says how), times scale; Linear from one end to the other, the other styles from
// the centre to one end.
GradientLine fill_line(const GradientFill&, double x, double y, double w, double h);
inline GradientShape fill_shape(const GradientFill& f, double x, double y, double w, double h) {
  return {f.style, fill_line(f, x, y, w, h), f.reverse};
}

// ---- Pixels ---------------------------------------------------------------------------------------------------------

// The dither's seed for every gradient: the tool, a fill layer and an overlay of one gradient give the same pixels.
inline constexpr uint32_t kGradientSeed = 0x9e3779b9;
// The dither's noise at pixel (x, y), uniform in [-0.5, 0.5), for R, G, B and alpha (grey: R and alpha; one channel:
// R): each its own hash (hash.h). gradient.slang's dither_noise().
std::array<float, 4> dither_noise(int x, int y, uint32_t seed = kGradientSeed);
// v (0..1) as a sample of depth d: tile.h's from_unit(), and so when `noise` is 0. Dithered, the noise times one 8-bit
// level (257 at 16 bits) is added before rounding, so that bands break up and a mean keeps: up to ±0.5 of an 8-bit
// level, less within that of 0 and the maximum, which therefore stay, and the mean with them. gradient.slang's
// to_level().
uint16_t to_level(float v, Depth d, float noise = 0);

// What render_gradient() draws.
struct GradientRender {
  std::shared_ptr<const GradientLut> lut;
  GradientShape shape;
  bool dither = false;
  uint32_t seed = kGradientSeed;
};
// Pixels x .. x + w - 1, y .. y + h - 1 of the gradient as samples of format f into dst, rows `stride` bytes apart: 4
// channels R, G, B, alpha; 2 grey (the table's R) and alpha; 1 the grey alone (a mask, a selection). Each sample
// to_level() of the table at gradient_t(), with dither_noise() when dithered: gradient_check.slang's to the bit.
void gradient_pixels(const GradientRender&, Format f, int x, int y, int w, int h, uint8_t* dst, size_t stride);
// The gradient over every tile of `tiles` in grid, all their pixels (past the canvas too), in the grid's format as
// gradient_pixels(): what was there is replaced; each tile compacted (compact(): solid when uniform, empty when it is
// the grid's fill). In parallel, a tile a task; the calling thread writes the grid. The Gradient tool's Classic mode
// (C36) and a fill layer made pixels (D40).
void render_gradient(const GradientRender&, TileGrid& grid, TileRect tiles);

}  // namespace pb
