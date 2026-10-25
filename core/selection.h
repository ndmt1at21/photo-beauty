#pragma once
// Selections (S31): the shapes of the Rectangular and Elliptical Marquee, Lasso and Polygonal Lasso rasterized into a
// grid of the selection's format at level 0 (DocState::selection, A40), and combined with the selection there is in
// Photoshop's four ways: one function, combine_selection(), for every tool and command that selects (S33's Magic
// Wand, S34's Color Range, S37's Load Selection, O31's Select Subject, H34's Patch). Select All, Deselect, Reselect,
// Inverse, Move Selection, Feather (S30's blur); where the selection lies and whether its edges show. The UI works the
// shapes out (Style Normal, Fixed Ratio, Fixed Size; Shift for a square or a circle, Alt from the centre, Space moving
// the shape while dragging: U5) and gives them here in document pixels, any real values. Each command is a
// SetSelection for Document::run() under the name of Photoshop's History step (SelectionStep).
//
// What Photoshop does is not published for three things, set here as named hypotheses that S39's images settle at
// B33 (tests/test_selection.cpp prints how far each is from Photoshop's): the Lasso's fill rule (kLassoFillRule), how
// soft selections combine (kSoftCombine), the Elliptical Marquee's anti-aliasing (kEllipseAntiAliasWidth); and the
// pixels a shape takes without anti-aliasing: those whose centre lies inside it.
//
// A grid here is a selection's: one channel at the document's depth, fill 0, 0 (not selected) up to the depth's
// maximum (selected), no tile wholly past the canvas. What an edge tile holds past the canvas means nothing (A40):
// Select All's tiles are solid, and the functions here read only pixels on the canvas.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "doc.h"
#include "regrid.h"
#include "tile.h"

namespace pb {

// How a new selection meets the one there is: the options bar's four buttons, or Shift (Add), Alt (Subtract), Shift
// Alt (Intersect) as the tool starts. JSON (S39's index.json, Actions in P2) by kSelectModeNames.
enum class SelectMode : uint8_t { New, Add, Subtract, Intersect };
inline constexpr const char* kSelectModeNames[] = {"new", "add", "subtract", "intersect"};

// How two soft selections combine, a for the selection there is, b for the new one, as 0..1: MinMax, Add max(a, b),
// Subtract min(a, 1 - b), Intersect min(a, b) (the hypothesis); Product, Add a + b - ab, Subtract a (1 - b), Intersect
// ab. Hard selections (0 or 1) come out alike either way. S39's add-ramps, subtract-ramps and intersect-ramps (every
// pair of 256 levels) settle it.
enum class SoftCombine : uint8_t { MinMax, Product };
inline constexpr SoftCombine kSoftCombine = SoftCombine::MinMax;

// Which points a self-crossing Lasso takes: EvenOdd, those inside an odd number of times (the hypothesis: a pentagram
// drawn point to point has a hole in the middle); NonZero, those the outline winds round at all (the middle filled).
// S39's lasso-star and lasso-star-noaa settle it.
enum class FillRule : uint8_t { EvenOdd, NonZero };
inline constexpr FillRule kLassoFillRule = FillRule::EvenOdd;

// The Elliptical Marquee's Anti-alias: each pixel takes the share of a box of this width (px) about its centre that
// lies inside the ellipse: 1 is the share of its own area (the hypothesis), wider ones make a softer edge. S39's
// ellipse settles it (the test fits the width to Photoshop's image).
inline constexpr double kEllipseAntiAliasWidth = 1;

// A point of document pixels: pixel (i, j) covers [i, i + 1) x [j, j + 1), its centre (i + 0.5, j + 0.5); as
// View::to_doc() gives them.
using SelectionPoint = std::array<double, 2>;

// A rectangle of document pixels between the corners (x0, y0) and (x1, y1), in either order (where the drag began and
// where it is): a marquee's, the ellipse's bounding box.
struct SelectionBox {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// ---- Shapes as grids ------------------------------------------------------------------------------------------------
// Each of the selection's format at `depth` over the canvas of width x height pixels from (0, 0), cut at its edges:
// tiles wholly inside the shape solid (one tile shared, no pixel allocated), wholly outside it empty, the others
// data. Tile rows in parallel (parallel_for). Throw std::invalid_argument for a coordinate not finite or a canvas
// without pixels. Any thread.

// The Rectangular Marquee's: the pixels whose centre lies in the box, x0 <= i + 0.5 < x1 (an edge on a pixel's
// centre takes it on the left and top, leaves it on the right and bottom), never anti-aliased, as Photoshop's (its
// Anti-alias greyed); a box of whole pixels takes exactly those.
TileGrid rect_selection(const SelectionBox&, int width, int height, Depth);

// The Elliptical Marquee's: the ellipse inscribed in the box. Anti-aliased, each pixel the share of a box of
// `aa_width` px about its centre inside the ellipse, worked out exactly (the area of a disc within a rectangle), so
// that the selection sums to pi a b; else the pixels whose centre lies inside it or on it.
TileGrid ellipse_selection(const SelectionBox&, bool anti_alias, int width, int height, Depth,
                           double aa_width = kEllipseAntiAliasWidth);

// The Lasso's and the Polygonal Lasso's: the polygon through `points`, the last joined to the first, by `rule`.
// Anti-aliased, each pixel the share of its area inside, exact where no more than two of the outline's windings meet
// within it (all but the pixels where it crosses itself); else the pixels whose centre is inside. Fewer than three
// points select nothing.
TileGrid polygon_selection(const std::vector<SelectionPoint>& points, bool anti_alias, int width, int height, Depth,
                           FillRule rule = kLassoFillRule);

// ---- Grids of selections --------------------------------------------------------------------------------------------
// width x height is the canvas; null grids select nothing. Each throws std::invalid_argument for a grid not of one
// channel with fill 0, two of different depths, a canvas without pixels. Tiles in parallel; any thread, while the grids
// stay as they are (published copies).

// The selection `selection` with `shape` (a tool's, a command's) by `mode`: New, the shape; Add, Subtract, Intersect by
// `how` (kSoftCombine), to the nearest level. Exact; tiles alike one side shared (Add of a solid selected tile: that
// tile), none past the canvas. Null where nothing on the canvas would be selected: Photoshop deselects then ("No pixels
// are selected." after Intersect).
std::shared_ptr<const TileGrid> combine_selection(const std::shared_ptr<const TileGrid>& selection,
                                                  const std::shared_ptr<const TileGrid>& shape, SelectMode mode,
                                                  int width, int height, SoftCombine how = kSoftCombine);

// Select All's grid: a solid tile of the depth's maximum over every tile of the canvas, one tile shared, no pixel.
std::shared_ptr<const TileGrid> all_selected(int width, int height, Depth);

// Select > Inverse: each pixel max - v; null when that selects nothing on the canvas (it was all selected). Twice gives
// the selection back, pixel for pixel.
std::shared_ptr<const TileGrid> inverse_selection(const TileGrid& selection, int width, int height);

// The selection moved by (dx, dy) whole pixels (T30's place()), what leaves the canvas gone, what it uncovers not
// selected (none of what an edge tile held past the canvas comes in); null when nothing is left on it. By whole tiles
// the tiles that stay inside are the same tiles.
std::shared_ptr<const TileGrid> moved_selection(const TileGrid& selection, int dx, int dy, int width, int height);

// The smallest rectangle of canvas pixels holding every pixel selected at all (above 0), what a command on the
// selected pixels covers; {} when there is none.
PixelRect selection_bounds(const TileGrid& selection, int width, int height);

// Whether some pixel on the canvas is selected 50% or more (2 v >= max: 128 of 255, 32768 of 65535): where the marching
// ants run (V30). Not: Photoshop warns "No pixels are more than 50% selected. The selection edges will not be
// visible." (the UI, N44), the selection kept.
bool selection_visible(const TileGrid& selection, int width, int height);

// Whether any pixel on the canvas is selected at all.
bool selects_anything(const TileGrid& selection, int width, int height);

// ---- Commands -------------------------------------------------------------------------------------------------------
// Each on a document state: its canvas and depth, its selection and Reselect's. A step is a SetSelection for
// Document::run() under `name`, Photoshop's History step (the UI shows it, translated): doc.run(s->command, s->name).
// nullopt where Photoshop greys the command out or nothing would change: no step then. Any thread; they throw as the
// functions above.
struct SelectionStep {
  SetSelection command;
  std::string name;
};

// The options bar of the Marquee and Lasso tools: the mode, Feather 0..1,000 px (kFeatherMax; S30's
// feather_selection(), the shape blurred before it meets the selection, its edges at the canvas extended as Feather's
// without Apply effect at canvas bounds), Anti-alias (the Elliptical Marquee's and the Lassos'; the Rectangular
// Marquee has none).
struct ShapeOptions {
  SelectMode mode = SelectMode::New;
  double feather = 0;
  bool anti_alias = true;
};

// History steps' names, as Photoshop's.
inline constexpr const char* kRectangularMarquee = "Rectangular Marquee";
inline constexpr const char* kEllipticalMarquee = "Elliptical Marquee";
inline constexpr const char* kLasso = "Lasso";
inline constexpr const char* kPolygonalLasso = "Polygonal Lasso";
inline constexpr const char* kSelectAll = "Select All";
inline constexpr const char* kDeselect = "Deselect";
inline constexpr const char* kReselect = "Reselect";
inline constexpr const char* kSelectInverse = "Select Inverse";
inline constexpr const char* kMoveSelection = "Move Selection";
inline constexpr const char* kFeather = "Feather";

// `shape` (a grid over d's canvas, as the tools and commands make one) combined with d's selection by `mode`, a step
// named `name`; nullopt when the selection would stay as it is. What every tool and command that selects ends with:
// S33's "Magic Wand", S34's "Color Range", S37's "Load Selection", O31's "Select Subject", H34's Patch.
std::optional<SelectionStep> select_shape(const DocState& d, const std::shared_ptr<const TileGrid>& shape,
                                          SelectMode mode, std::string name);

// The tools, at mouse up: "Rectangular Marquee", "Elliptical Marquee", "Lasso", "Polygonal Lasso". A shape that takes
// no pixel (a click without a drag, fewer than three points) in New deselects, as Photoshop's click: a "Deselect" step
// while something is selected, else nullopt; in the other modes it changes nothing.
std::optional<SelectionStep> rectangular_marquee(const DocState& d, const SelectionBox&, const ShapeOptions& = {});
std::optional<SelectionStep> elliptical_marquee(const DocState& d, const SelectionBox&, const ShapeOptions& = {});
std::optional<SelectionStep> lasso(const DocState& d, const std::vector<SelectionPoint>& points,
                                   const ShapeOptions& = {});
std::optional<SelectionStep> polygonal_lasso(const DocState& d, const std::vector<SelectionPoint>& points,
                                             const ShapeOptions& = {});

// Select > All (Ctrl A): always a step, all_selected().
SelectionStep select_all(const DocState& d);
// Select > Deselect (Ctrl D): the selection goes to Reselect's (SetSelection{}, A40); nullopt when nothing is selected.
std::optional<SelectionStep> deselect(const DocState& d);
// Select > Reselect (Ctrl Shift D): the selection the last Deselect dropped; nullopt without one, or when it is the
// selection already.
std::optional<SelectionStep> reselect(const DocState& d);
// Select > Inverse (Ctrl Shift I): inverse_selection(); nullopt when nothing is selected. All selected gives nothing
// selected, as Deselect (Reselect then gives the selection back).
std::optional<SelectionStep> select_inverse(const DocState& d);
// A selection tool dragged inside the selection, the arrow keys (1 px, Shift 10 px): moved_selection(); nullopt when
// nothing is selected or for (0, 0).
std::optional<SelectionStep> move_selection(const DocState& d, int dx, int dy);
// Select > Modify > Feather (Shift F6): S30's feather_selection() of the selection; nullopt when nothing is selected
// or for a radius of 0. Throws std::invalid_argument for a radius outside 0..1,000 px.
std::optional<SelectionStep> modify_feather(const DocState& d, double radius, bool at_canvas_bounds);

// d's selection_bounds(), selection_visible(): {} and false when nothing is selected.
PixelRect selection_bounds(const DocState& d);
bool selection_visible(const DocState& d);

}  // namespace pb
