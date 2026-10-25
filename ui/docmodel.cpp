// A document opened for real (docmodel.h): commands as the mock document runs them, made of T1's; the JS values the
// panels read, made again only for the layers whose node is new; thumbnails of layers and snapshots for image://pb.
#include "docmodel.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValueIterator>
#include <QLoggingCategory>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <variant>

#include "session.h"

Q_LOGGING_CATEGORY(lcDoc, "pb.doc")

namespace {

using pb::LayerId;
using pb::LayerPtr;

std::invalid_argument refused(const std::string& why) { return std::invalid_argument("DocModel: " + why); }

// A step's name as the History panel shows it: Vietnamese, with an icon, as the mock names its steps; a name not here
// (a snapshot's, a P1 command's) as it is.
struct Look {
  const char* name;
  const char* label;
  const char* icon;
};
constexpr Look kLooks[] = {
    {"Open", QT_TRANSLATE_NOOP("DocModel", "Mở"), "folder_open"},
    {"Duplicate", QT_TRANSLATE_NOOP("DocModel", "Nhân bản"), "content_copy"},
    {"New Layer", QT_TRANSLATE_NOOP("DocModel", "Layer mới"), "add_box"},
    {"New Group", QT_TRANSLATE_NOOP("DocModel", "Nhóm mới"), "folder"},
    {"Delete Layer", QT_TRANSLATE_NOOP("DocModel", "Xóa layer"), "delete"},
    {"Delete Hidden Layers", QT_TRANSLATE_NOOP("DocModel", "Xóa layer đang ẩn"), "delete"},
    {"Duplicate Layer", QT_TRANSLATE_NOOP("DocModel", "Nhân bản layer"), "content_copy"},
    {"Layer Order", QT_TRANSLATE_NOOP("DocModel", "Sắp xếp layer"), "drag_indicator"},
    {"Group Layers", QT_TRANSLATE_NOOP("DocModel", "Nhóm layer"), "folder"},
    {"Ungroup Layers", QT_TRANSLATE_NOOP("DocModel", "Bỏ nhóm"), "folder"},
    {"Rename Layer", QT_TRANSLATE_NOOP("DocModel", "Đổi tên layer"), "edit"},
    {"Hide Layer", QT_TRANSLATE_NOOP("DocModel", "Ẩn layer"), "visibility"},
    {"Show Layer", QT_TRANSLATE_NOOP("DocModel", "Hiện layer"), "visibility"},
    {"Blending Change", QT_TRANSLATE_NOOP("DocModel", "Blend mode"), "tune"},
    {"Master Opacity Change", QT_TRANSLATE_NOOP("DocModel", "Opacity"), "opacity"},
    {"Fill Opacity Change", QT_TRANSLATE_NOOP("DocModel", "Fill"), "opacity"},
    {"Lock Layer", QT_TRANSLATE_NOOP("DocModel", "Khóa layer"), "lock"},
    {"Layer Properties", QT_TRANSLATE_NOOP("DocModel", "Thuộc tính layer"), "tune"},
    {"Brush", "Brush", "brush"},
    {"Eraser", "Eraser", "ink_eraser"},
};
std::pair<QString, QString> look(const std::string& name) {
  for (const Look& l : kLooks)
    if (name == l.name) return {QCoreApplication::translate("DocModel", l.label), QString::fromLatin1(l.icon)};
  return {QString::fromStdString(name), QStringLiteral("history")};
}

// The mock's keys of a layer's props past T1's (Mock.PROPS): what the panels read of P1 layers, at their defaults; a
// layer's `extra` (unknown keys, kept by the core) goes over them.
constexpr const char* kP1Props =
    R"("background":false,"clip":false,"mask":null,"maskOff":false,"maskDensity":1,"maskFeather":0,"photo":null,)"
    R"("thumb":null,"look":null,"adj":null,"vals":null,"color":null,"shape":null,"fx":null)";
// The keys setProps takes on a real document: T1's, and data the core keeps for the UI (the colour label).
const char* const kSettable[] = {"name",     "visible",          "opacity",    "fill",         "blend",
                                 "lockTransparency", "lockPixels", "lockPosition", "lockAll", "tag"};
// What the core does: run()'s ops, and the menus' actions made of them (MenuSpec.js, Docs.exec()); those of the view,
// the windows and the app, also while the file is read.
const char* const kOps[] = {"selectLayers", "addLayer", "newLayer",   "newGroup", "deleteLayers", "duplicateLayers",
                            "moveLayer",    "arrange",  "groupLayers", "ungroup", "setProps",     "deleteHidden"};
const char* const kActions[] = {"undo",      "redo",     "toggleLast", "revert",     "dupDoc",  "newLayer", "newGroup",
                                "dupLayer",  "delLayer", "delHidden",  "groupLayer", "ungroup", "hideLayer", "arr",
                                "lockAll",   "rename",   "save"};
const char* const kViewActions[] = {"close", "closeAll", "open",   "zoomIn", "zoomOut", "zoomFit", "zoomFill",
                                    "zoom100", "tog",    "scr",    "snapTo", "win",     "side",    "aa",
                                    "fp",    "openFile", "quit",   "clearRecent", "noop"};
// The mock's menu requirements (Mock.CX): the mock judges them.
const char* const kNeeds[] = {"doc",  "sel",    "prev", "undo", "redo", "hist", "layer", "multi", "hidden",
                              "grp",  "text",   "shape", "so",  "mask", "bgl",  "below", "faces"};
template <size_t N>
bool among(const char* const (&list)[N], const QString& s) {
  return std::any_of(std::begin(list), std::end(list), [&](const char* x) { return s == QLatin1String(x); });
}

// JSON text of a string, a float as the shortest that reads back the same (0.3, not 0.30000001).
void quoted(std::string& out, const std::string& s) {
  out += '"';
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\', out += c;
    } else if (uint8_t(c) < 0x20) {
      char u[8];
      std::snprintf(u, sizeof u, "\\u%04x", unsigned(uint8_t(c)));
      out += u;
    } else {
      out += c;
    }
  }
  out += '"';
}
void number(std::string& out, float f) {
  char b[32];
  const auto r = std::to_chars(b, b + sizeof b, f);
  out.append(b, r.ptr);
}

// "6,48 MB", "1,02 GB", "410 KB": as the mock's sizes (Mock.RECENT), Vietnamese decimals.
QString size(double bytes) {
  const char* units[] = {"KB", "MB", "GB", "TB"};
  double v = bytes / 1e3;
  int u = 0;
  while (v >= 1000 && u < 3) v /= 1e3, u++;
  const int decimals = v >= 100 ? 0 : v >= 10 ? 1 : 2;
  QString s = QString::number(v, 'f', decimals);
  if (s.contains(u'.')) {
    while (s.endsWith(u'0')) s.chop(1);
    if (s.endsWith(u'.')) s.chop(1);
  }
  return s.replace(u'.', u',') + u' ' + QLatin1String(units[u]);
}

// Where layer id is: its node, its group and its index there (top: the root's children).
struct Place {
  LayerPtr layer;
  const pb::Layer* parent = nullptr;
  int index = -1;
};
Place locate(const LayerPtr& g, LayerId id) {
  for (size_t i = 0; i < g->children.size(); i++) {
    const LayerPtr& c = g->children[i];
    if (c->id == id) return {c, g.get(), int(i)};
    if (c->kind == pb::Kind::Group)
      if (Place p = locate(c, id); p.layer) return p;
  }
  return {};
}
// Every layer top first, as the Layers panel lists them (Mock.flatten()).
void flatten(const pb::Layer& g, std::vector<const pb::Layer*>& out) {
  for (size_t i = g.children.size(); i-- > 0;) {
    out.push_back(g.children[i].get());
    flatten(*g.children[i], out);
  }
}
bool background(const pb::Layer& l) { return l.props.background; }

// "base N" for the next N no layer has (Mock.nextName()).
std::string nextName(const pb::DocState& d, const std::string& base) {
  std::vector<const pb::Layer*> all;
  flatten(*d.root, all);
  long n = 0;
  for (const pb::Layer* l : all) {
    const std::string& s = l->props.name;
    if (s.size() > base.size() + 1 && s.compare(0, base.size(), base) == 0 && s[base.size()] == ' ' &&
        std::all_of(s.begin() + long(base.size()) + 1, s.end(), [](char c) { return c >= '0' && c <= '9'; }))
      n = std::max(n, std::strtol(s.c_str() + base.size() + 1, nullptr, 10));
  }
  return base + ' ' + std::to_string(n + 1);
}

// The layers of ids, once each, but those inside another of them, in the tree's order (bottom first, a group before
// what it holds): doc.cpp's top_level(). Throws for a layer not there.
std::vector<LayerPtr> topLevel(const pb::DocState& d, std::vector<LayerId> ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  std::vector<LayerPtr> out;
  size_t found = 0;
  const std::function<void(const pb::Layer&, bool)> walk = [&](const pb::Layer& g, bool inside) {
    for (const LayerPtr& c : g.children) {
      const bool listed = std::binary_search(ids.begin(), ids.end(), c->id);
      found += listed;
      if (listed && !inside) out.push_back(c);
      walk(*c, inside || listed);
    }
  };
  walk(*d.root, false);
  if (found != ids.size()) throw refused("a layer not there");
  return out;
}

// The props JSON patch `p` (setProps, addLayer) over `base`: T1's keys checked as the mock does, the colour label into
// extra; a key the core does not keep yet (mask, fx…) refused.
pb::LayerProps patched(pb::LayerProps base, const QJsonObject& p) {
  QJsonObject extra = base.extra.empty() ? QJsonObject()
                                         : QJsonDocument::fromJson(QByteArray::fromStdString(base.extra)).object();
  for (auto it = p.begin(); it != p.end(); ++it) {
    const QString k = it.key();
    const QJsonValue v = it.value();
    if (!among(kSettable, k)) throw refused("no prop " + k.toStdString() + " in the core yet");
    if (k == u"name") {
      if (!v.isString()) throw refused("a name is text");
      base.name = v.toString().toStdString();
    } else if (k == u"opacity" || k == u"fill") {
      if (!v.isDouble() || !(v.toDouble() >= 0 && v.toDouble() <= 1)) throw refused("opacity and fill go 0..1");
      (k == u"opacity" ? base.opacity : base.fill) = float(v.toDouble());
    } else if (k == u"blend") {
      const std::string b = v.toString().toStdString();
      const auto n = std::find(std::begin(pb::kBlendNames), std::end(pb::kBlendNames), b);
      if (n == std::end(pb::kBlendNames)) throw refused("no blend mode " + b);
      base.blend = pb::Blend(n - std::begin(pb::kBlendNames));
    } else if (k == u"tag") {
      if (v.isNull() || v.isUndefined())
        extra.remove(k);
      else if (v.isString())
        extra[k] = v;
      else
        throw refused("a colour label is text");
    } else {
      if (!v.isBool()) throw refused(k.toStdString() + " is true or false");
      const bool b = v.toBool();
      if (k == u"visible") base.visible = b;
      if (k == u"lockTransparency") base.lock_transparency = b;
      if (k == u"lockPixels") base.lock_pixels = b;
      if (k == u"lockPosition") base.lock_position = b;
      if (k == u"lockAll") base.lock_all = b;
    }
  }
  // as the core reads extra back: members sorted (QJsonObject's order), no spaces
  base.extra = extra.isEmpty() ? std::string() : QJsonDocument(extra).toJson(QJsonDocument::Compact).toStdString();
  return base;
}

std::vector<LayerId> ids(const QJsonValue& v) {
  if (!v.isArray()) throw refused("layers: a list of ids");
  std::vector<LayerId> out;
  for (const QJsonValue& x : v.toArray()) {
    const double d = x.toDouble(-1);
    if (!(d >= 1 && d == std::floor(d) && d <= 4294967295.)) throw refused("no layer id");
    out.push_back(LayerId(d));
  }
  return out;
}
LayerId id(const QJsonValue& v) {
  const double d = v.toDouble(-1);
  if (!(d >= 0 && d == std::floor(d) && d <= 4294967295.)) throw refused("no layer id");
  return LayerId(d);
}
int integer(const QJsonValue& v) {
  const double d = v.toDouble(std::nan(""));
  if (!(d == std::floor(d) && std::abs(d) < 1e9)) throw refused("not a whole number");
  return int(d);
}

}  // namespace

DocModel::DocModel(QJSEngine* js, const QString& path, const QString& file, qint64 bytes, bool pbd, int w, int h,
                   int viewW, int viewH, QObject* parent)
    : QObject(parent), js_(js), path_(path), file_(file), bytes_(bytes), pbd_(pbd), viewW_(viewW), viewH_(viewH), w_(w),
      h_(h) {
  parse_ = js_->globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("parse"));
  alphas_ = js_->newArray(0);
  hiddenChannels_ = js_->newObject();
  collapsed_ = js_->newObject();
  for (QJSValue* v : {&layers_, &rows_, &selected_, &history_, &snapshots_}) *v = js_->newArray(0);
  active_ = QJSValue(QJSValue::NullValue);
  tree_ = js_->newObject();
  tree_.setProperty(QStringLiteral("id"), 0);
  tree_.setProperty(QStringLiteral("kind"), QStringLiteral("group"));
  tree_.setProperty(QStringLiteral("children"), js_->newArray(0));
  state_ = js_->newObject();
  state_.setProperty(QStringLiteral("root"), tree_);
  state_.setProperty(QStringLiteral("selected"), selected_);
  fit(viewW, viewH);
  // ponytail: a guess from the pixels (open_image() says nothing as it goes) that stops at 95 %, until M1 reports
  task_ = tr("Đang mở %1").arg(file_);
  progress_ = 0;
  reading_.start();
  ticker_.setInterval(100);
  connect(&ticker_, &QTimer::timeout, this, &DocModel::estimate);
  ticker_.start();
}

DocModel::~DocModel() { emit closing(); }

void DocModel::estimate() {
  const double seconds = std::max(0.2, double(w_) * h_ / 40e6);  // ~40 MP/s decoded and cut into tiles
  progress_ = std::min(0.95, reading_.elapsed() / 1e3 / seconds);
  bump();
}

void DocModel::fit(int viewW, int viewH) {
  const double w = std::max(1, w_), h = std::max(1, h_);
  // Mock.fitZoom(): % of a frame of viewW x viewH less 40 px, two decimals
  const double pct = std::round(std::min((viewW - 40) / w, (viewH - 40) / h) * 10000) / 100;
  QJSValue v = js_->newObject();
  v.setProperty(QStringLiteral("x"), w / 2);
  v.setProperty(QStringLiteral("y"), h / 2);
  v.setProperty(QStringLiteral("zoom"), std::clamp(pct, 1., 12800.) / 100);
  v.setProperty(QStringLiteral("angle"), 0);
  view_ = v;
}

void DocModel::opened(pb::DocPtr state, const char* step, bool background, std::shared_ptr<const pb::Preview> preview) {
  ticker_.stop();
  preview_ = std::move(preview);
  task_.clear();
  progress_ = -1;
  if (background && state->root->children.size() == 1 && state->root->children[0]->kind == pb::Kind::Pixel) {
    // as Photoshop opens a JPEG: the Background, locked, at the bottom (the mock's `background`, the core's since A40)
    const pb::Layer& l = *state->root->children[0];
    pb::LayerProps p = l.props;
    p.lock_all = true;
    p.background = true;
    state = pb::run(*state, pb::SetProps{l.id, p});
  }
  doc_ = std::make_unique<pb::Document>(std::move(state), step);
  saved_ = doc_->step(0).serial;
  doc_->snapshot(file_.toStdString());  // the file as it opened, as Photoshop keeps it
  const pb::DocState& d = *doc_->state();
  const bool resized = d.width != w_ || d.height != h_;
  w_ = d.width, h_ = d.height;
  if (resized) fit(viewW_, viewH_);
  derive();
  bump();
}

void DocModel::failed(const QString& why) {
  ticker_.stop();
  task_.clear();
  progress_ = -1;
  error_ = why.isEmpty() ? tr("không đọc được file") : why;
  bump();
}

void DocModel::touched() {
  snapshot_ = -1;
  derive();
  bump();
}

void DocModel::bump() {
  version_++;
  emit changed();
}

bool DocModel::dirty() const { return doc_ && doc_->step(doc_->current()).serial != saved_; }

int DocModel::unsaved() const {
  if (!dirty()) return 0;
  const long now = long(doc_->current());
  for (size_t i = 0; i < doc_->steps(); i++)
    if (doc_->step(i).serial == saved_) return int(std::labs(now - long(i)));
  return int(std::max(1L, now));
}

void DocModel::saved(uint64_t serial, const QString& path) {
  saved_ = serial;
  if (path != path_) {
    path_ = path;
    file_ = QFileInfo(path).fileName();
  }
  pbd_ = true;
  bytes_ = QFileInfo(path).size();
  bump();
}

// ---- commands ----

bool DocModel::can(const QString& name) const {
  const bool action = name.startsWith(u'@') || name.contains(u':');  // "@merge:down"; "mask" is an op of run()
  const QString n = (name.startsWith(u'@') ? name.mid(1) : name).section(u':', 0, 0);
  if (name == u"dialog:SaveOptions") return !!doc_;  // Lưu thành, Lưu bản sao: a .pbd, where Main's FileDialog says (N2)
  if (name.startsWith(QLatin1String("dialog:")) || name.startsWith(QLatin1String("space:")) ||
      name.startsWith(QLatin1String("panel:")))
    return false;  // none yet
  if (n.isEmpty() || (!action && n != u"mask" && among(kNeeds, n.startsWith(u'!') ? n.mid(1) : n))) return true;
  return among(kViewActions, n) || (doc_ && (among(kOps, n) || among(kActions, n)));  // reading: the view only
}

bool DocModel::run(const QJSValue& cmd) {
  if (!doc_ || !cmd.isObject()) return false;
  const QJsonObject c = QJsonObject::fromVariantMap(cmd.toVariant().toMap());
  emit aboutToChange();
  const uint64_t was = doc_->step(doc_->current()).serial;
  const size_t steps = doc_->steps();
  try {
    if (!apply(c)) return false;
  } catch (const std::exception& e) {
    qCDebug(lcDoc, "%s: %s", qPrintable(c.value(QLatin1String("op")).toString()), e.what());
    return false;
  }
  if (doc_->steps() != steps || doc_->step(doc_->current()).serial != was) snapshot_ = -1;  // not for selectLayers
  derive();
  bump();
  return true;
}

void DocModel::step(const pb::Command& c, const char* name) { doc_->run(c, name); }

bool DocModel::apply(const QJsonObject& c) {
  const QString op = c.value(QLatin1String("op")).toString();
  const pb::DocState& d = *doc_->state();
  const LayerId activeId = d.selected.empty() ? 0 : d.selected.back();
  const auto has = [&](const char* k) { return c.contains(QLatin1String(k)) && !c.value(QLatin1String(k)).isNull(); };
  const auto field = [&](const char* k) { return c.value(QLatin1String(k)); };
  // the layer c names, else the active one
  const auto target = [&]() -> Place {
    const LayerId l = has("layer") ? id(field("layer")) : activeId;
    Place p = locate(d.root, l);
    if (!p.layer) throw refused("no layer " + std::to_string(l));
    return p;
  };
  const auto listed = [&]() { return has("layers") ? ids(field("layers")) : d.selected; };
  // a new layer above the active one, in its group; at the top without one
  const auto addAbove = [&](pb::Kind kind, pb::LayerProps p, const char* name) {
    const Place a = activeId ? locate(d.root, activeId) : Place{};
    pb::AddLayer add;
    add.parent = a.layer ? a.parent->id : 0;
    add.index = a.layer ? a.index + 1 : int(d.root->children.size());
    add.kind = kind;
    add.props = std::move(p);
    step(add, name);
  };
  // the layers gone, the one in the place of the topmost of them selected, as the mock (T1 leaves that to the UI)
  const auto remove = [&](const std::vector<LayerPtr>& top, const char* name, bool keepOne) {
    if (top.empty()) throw refused("no layers to delete");
    std::vector<const pb::Layer*> before;
    flatten(*d.root, before);
    std::vector<LayerId> gone;
    const std::function<void(const pb::Layer&)> mark = [&](const pb::Layer& l) {
      gone.push_back(l.id);
      for (const LayerPtr& k : l.children) mark(*k);
    };
    for (const LayerPtr& l : top) mark(*l);
    std::sort(gone.begin(), gone.end());
    const auto isGone = [&](const pb::Layer* l) { return std::binary_search(gone.begin(), gone.end(), l->id); };
    const size_t i = size_t(std::find_if(before.begin(), before.end(), isGone) - before.begin());
    std::vector<const pb::Layer*> rest;
    std::copy_if(before.begin(), before.end(), std::back_inserter(rest), [&](const pb::Layer* l) { return !isGone(l); });
    if (keepOne && std::none_of(d.root->children.begin(), d.root->children.end(),
                                [&](const LayerPtr& l) { return !isGone(l.get()); }))
      throw refused("every layer hidden");
    std::vector<LayerId> ids;
    for (const LayerPtr& l : top) ids.push_back(l->id);
    doc_->begin_gesture();
    try {
      step(pb::DeleteLayers{ids}, name);
      step(pb::SelectLayers{rest.empty() ? std::vector<LayerId>{} : std::vector<LayerId>{rest[std::min(i, rest.size() - 1)]->id}}, "");
    } catch (...) {
      doc_->end_gesture();
      throw;
    }
    doc_->end_gesture();
  };
  // `layer` into `parent` at `index` (counted without it), as the mock: the Background stays, nothing goes below it
  const auto move = [&](const Place& f, LayerId parent, int index) {
    if (background(*f.layer)) throw refused("the Background stays where it is");
    const Place g = parent ? locate(d.root, parent) : Place{d.root, nullptr, 0};
    if (!g.layer || g.layer->kind != pb::Kind::Group) throw refused("no group " + std::to_string(parent));
    std::vector<LayerPtr> rest;
    for (const LayerPtr& k : g.layer->children)
      if (k->id != f.layer->id) rest.push_back(k);
    if (index < 0 || size_t(index) > rest.size()) throw refused("no index " + std::to_string(index));
    if (index == 0 && !rest.empty() && background(*rest[0])) throw refused("nothing goes below the Background");
    if (f.parent->id == parent && f.index == index) throw refused("no change");
    step(pb::MoveLayer{f.layer->id, parent, index}, "Layer Order");
  };

  if (op == u"selectLayers") {
    std::vector<LayerId> sel;
    for (const LayerId l : ids(field("layers"))) {
      if (!locate(d.root, l).layer) throw refused("no layer " + std::to_string(l));
      if (std::find(sel.begin(), sel.end(), l) == sel.end()) sel.push_back(l);
    }
    if (sel == d.selected) return false;
    doc_->run(pb::SelectLayers{sel}, "");
  } else if (op == u"addLayer") {
    const QString kind = field("kind").toString();
    if (kind != u"pixel" && kind != u"group") throw refused("no " + kind.toStdString() + " layers in the core yet");
    pb::AddLayer add;
    add.parent = id(field("parent"));
    add.index = integer(field("index"));
    add.kind = kind == u"group" ? pb::Kind::Group : pb::Kind::Pixel;
    add.props = patched({}, field("props").toObject());
    step(add, add.kind == pb::Kind::Group ? "New Group" : "New Layer");
  } else if (op == u"newLayer") {
    pb::LayerProps p;
    p.name = nextName(d, tr("Layer").toStdString());
    addAbove(pb::Kind::Pixel, p, "New Layer");
  } else if (op == u"newGroup") {
    pb::LayerProps p;
    p.name = nextName(d, tr("Group").toStdString());
    p.blend = pb::Blend::PassThrough;
    addAbove(pb::Kind::Group, p, "New Group");
  } else if (op == u"deleteLayers") {
    remove(topLevel(d, listed()), "Delete Layer", false);
  } else if (op == u"deleteHidden") {
    std::vector<const pb::Layer*> all;
    flatten(*d.root, all);
    std::vector<LayerId> hidden;
    for (const pb::Layer* l : all)
      if (!l->props.visible) hidden.push_back(l->id);
    if (hidden.empty()) throw refused("no hidden layers");
    remove(topLevel(d, hidden), "Delete Hidden Layers", true);
  } else if (op == u"duplicateLayers") {
    // each copy right above its layer, named "… bản sao", unlocked, not the Background: one step, as the mock
    const std::vector<LayerPtr> top = topLevel(d, listed());
    if (top.empty()) throw refused("no layers to duplicate");
    std::vector<LayerId> ids;
    for (const LayerPtr& l : top) ids.push_back(l->id);
    doc_->begin_gesture();
    try {
      step(pb::DuplicateLayers{ids}, "Duplicate Layer");
      const std::vector<LayerId> copies = doc_->state()->selected;  // in the order of `top`
      for (size_t k = 0; k < copies.size() && k < top.size(); k++) {
        pb::LayerProps p = top[k]->props;
        p.name += tr(" bản sao").toStdString();
        p.lock_transparency = p.lock_pixels = p.lock_position = p.lock_all = false;
        p.background = false;  // the core's copy is none already; these props are the original's
        step(pb::SetProps{copies[k], p}, "Duplicate Layer");
      }
    } catch (...) {
      doc_->end_gesture();
      throw;
    }
    doc_->end_gesture();
  } else if (op == u"moveLayer") {
    const Place f = locate(d.root, id(field("layer")));
    if (!f.layer) throw refused("no layer to move");
    const LayerId parent = id(field("parent"));
    if (parent == f.layer->id || (f.layer->kind == pb::Kind::Group && locate(f.layer, parent).layer))
      throw refused("a group inside itself");
    move(f, parent, integer(field("index")));
  } else if (op == u"arrange") {  // Layer › Sắp xếp: top, up, down, bottom among its group's layers
    const Place f = target();
    const int n = int(f.parent->children.size());
    const int floor = background(*f.parent->children[0]) ? 1 : 0;
    const QString dir = field("dir").toString();
    const int to = dir == u"top" ? n - 1 : dir == u"up" ? std::min(f.index + 1, n - 1)
                 : dir == u"down" ? std::max(f.index - 1, floor) : dir == u"bottom" ? floor : -1;
    if (to < 0) throw refused("no direction " + dir.toStdString());
    move(f, f.parent->id, to);
  } else if (op == u"groupLayers") {
    const std::vector<LayerPtr> top = topLevel(d, listed());
    if (top.empty()) throw refused("no layers to group");
    if (std::any_of(top.begin(), top.end(), [](const LayerPtr& l) { return background(*l); }))
      throw refused("the Background stays where it is");
    pb::GroupLayers g;
    for (const LayerPtr& l : top) g.layers.push_back(l->id);
    if (has("props")) {
      g.props = patched({}, field("props").toObject());
    } else {
      g.props.name = nextName(d, tr("Group").toStdString());
      g.props.blend = pb::Blend::PassThrough;
    }
    step(g, "Group Layers");
  } else if (op == u"ungroup") {
    const LayerId g = has("group") ? id(field("group")) : activeId;
    const Place f = locate(d.root, g);
    if (!f.layer || f.layer->kind != pb::Kind::Group) throw refused("layer " + std::to_string(g) + " is not a group");
    step(pb::Ungroup{g}, "Ungroup Layers");
  } else if (op == u"setProps") {
    const Place f = target();
    const QJsonObject p = field("props").toObject();
    const pb::LayerProps now = patched(f.layer->props, p);
    if (pb::to_json(now) == pb::to_json(f.layer->props)) throw refused("no change");
    // named by the keys it was given, as the mock names them
    const QStringList given = p.keys();
    const auto only = [&](std::initializer_list<const char*> keys) {
      return !given.isEmpty() && std::all_of(given.begin(), given.end(), [&](const QString& k) {
        return std::any_of(keys.begin(), keys.end(), [&](const char* x) { return k == QLatin1String(x); });
      });
    };
    const char* name = only({"name"})         ? "Rename Layer"
                       : only({"visible"})    ? (now.visible ? "Show Layer" : "Hide Layer")
                       : only({"blend"})      ? "Blending Change"
                       : only({"opacity"})    ? "Master Opacity Change"
                       : only({"fill"})       ? "Fill Opacity Change"
                       : only({"lockTransparency", "lockPixels", "lockPosition", "lockAll"}) ? "Lock Layer"
                                                                                             : "Layer Properties";
    step(pb::SetProps{f.layer->id, now}, name);
  } else {
    // a command of the core by its JSON (command_from_json()): what P1's tasks add
    const std::optional<pb::Command> cmd =
        pb::command_from_json(QJsonDocument(c).toJson(QJsonDocument::Compact).toStdString());
    if (!cmd) throw refused("no command " + op.toStdString());
    step(*cmd, op.toUtf8().constData());
  }
  return true;
}

// ---- history ----

void DocModel::undo() {
  if (!doc_) return;
  emit aboutToChange();
  if (doc_->undo()) touched();
}

void DocModel::redo() {
  if (!doc_) return;
  emit aboutToChange();
  if (doc_->redo()) touched();
}

void DocModel::jumpTo(int i) {
  if (!doc_) return;
  emit aboutToChange();
  doc_->jump(size_t(std::clamp(i, 0, int(doc_->steps()) - 1)));
  touched();
}

void DocModel::toggleLast() {
  if (!doc_ || doc_->steps() < 2) return;
  const size_t last = doc_->steps() - 1;
  jumpTo(int(doc_->current() == last ? last - 1 : last));
}

void DocModel::revert() { jumpTo(0); }

void DocModel::jumpToSnapshot(int i) {
  if (!doc_ || i < 0 || size_t(i) >= doc_->snapshots().size()) return;
  emit aboutToChange();
  doc_->restore(size_t(i));
  derive();
  snapshot_ = i;
  bump();
}

void DocModel::newSnapshot() {
  if (!doc_) return;
  emit aboutToChange();
  doc_->snapshot(tr("Snapshot %1").arg(doc_->snapshots().size()).toStdString());
  derive();
  bump();
}

void DocModel::deleteStep() {
  if (!doc_ || !doc_->current()) return;
  emit aboutToChange();
  doc_->delete_step();
  touched();
}

void DocModel::beginGesture() {
  if (!doc_) return;
  emit aboutToChange();  // a stroke merging now would join the gesture's step
  doc_->begin_gesture();
}

void DocModel::endGesture() {
  if (doc_) doc_->end_gesture();
}

void DocModel::markSaved() {
  if (!doc_) return;
  saved_ = doc_->step(doc_->current()).serial;
  bump();
}

// ---- what History does not keep ----

void DocModel::setView(const QJSValue& patch) {
  QJSValue v = js_->newObject();
  for (const char* k : {"x", "y", "zoom", "angle"}) {
    const QString key = QLatin1String(k);
    const QJSValue p = patch.property(key);
    v.setProperty(key, p.isNumber() ? p : view_.property(key));
  }
  view_ = v;
  bump();
}

void DocModel::zoomTo(double pct) {
  QJSValue p = js_->newObject();
  p.setProperty(QStringLiteral("zoom"), std::clamp(std::round(pct * 100) / 100, 1., 12800.) / 100);
  setView(p);
}

void DocModel::toggleOpen(int group) {
  const auto it = std::find(collapsedIds_.begin(), collapsedIds_.end(), LayerId(group));
  if (it != collapsedIds_.end())
    collapsedIds_.erase(it);
  else
    collapsedIds_.push_back(LayerId(group));
  QJSValue c = js_->newObject();
  for (const LayerId l : collapsedIds_) c.setProperty(QString::number(l), true);
  collapsed_ = c;
  shown_ = nullptr;  // the rows again
  derive();
  bump();
}

void DocModel::setChannel(const QString& name) {
  const bool colour = name == u"Red" || name == u"Green" || name == u"Blue";
  channel_ = colour && mode_ == u"RGB" ? name : QStringLiteral("RGB");  // no alpha channels yet
  bump();
}

void DocModel::toggleChannel(const QString& name) {
  QJSValue c = js_->newObject();
  bool was = false;
  QJSValueIterator it(hiddenChannels_);
  while (it.hasNext()) {
    it.next();
    if (it.name() == name)
      was = true;
    else
      c.setProperty(it.name(), true);
  }
  if (!was) c.setProperty(name, true);
  hiddenChannels_ = c;
  bump();
}

void DocModel::setNotice(const QString& text) {
  notice_ = text;
  bump();
}

void DocModel::setTask(const QString& task, double progress) {
  task_ = task;
  progress_ = progress;
  bump();
}

QString DocModel::dump() const {
  if (!doc_) return {};
  const pb::DocState& d = *doc_->state();
  const std::function<QJsonArray(const pb::Layer&)> layers = [&](const pb::Layer& g) {
    QJsonArray out;
    for (const LayerPtr& l : g.children) {
      QJsonObject o{{"id", qint64(l->id)},
                    {"kind", l->kind == pb::Kind::Group ? "group" : "pixel"},
                    {"props", QJsonDocument::fromJson(QByteArray::fromStdString(pb::to_json(l->props))).object()}};
      if (l->pixels) {
        QJsonArray tiles;
        for (const auto& [xy, t] : l->pixels->tiles()) tiles.append(QString::number(t->id()));
        o[QStringLiteral("tiles")] = tiles;
      } else {
        o[QStringLiteral("layers")] = layers(*l);
      }
      out.append(o);
    }
    return out;
  };
  QJsonArray selected;
  for (const LayerId l : d.selected) selected.append(qint64(l));
  const QJsonObject o{{"width", d.width}, {"height", d.height}, {"selected", selected}, {"nextId", qint64(d.next_id)},
                      {"layers", layers(*d.root)}};
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// ---- the JS values ----

QString DocModel::digest() const {
  if (!doc_) return {};
  const pb::DocState& d = *doc_->state();
  QCryptographicHash hash(QCryptographicHash::Sha1);
  std::vector<uint8_t> rows;
  const std::function<void(const pb::Layer&)> walk = [&](const pb::Layer& g) {
    for (const LayerPtr& c : g.children) {
      const char kind = c->kind == pb::Kind::Group ? 'g' : c->pixels ? 'p' : 'o';
      hash.addData(QByteArrayView(&kind, 1));
      if (c->pixels) {
        const pb::Format f = c->pixels->format();
        const char format[2] = {char(f.channels), char(f.bytes())};
        hash.addData(QByteArrayView(format, 2));
        const size_t stride = size_t(d.width) * size_t(f.bytes());
        for (int y = 0; y < d.height; y += pb::kTileSize) {
          const int n = std::min(pb::kTileSize, d.height - y);
          rows.resize(stride * size_t(n));
          c->pixels->read_rect(0, y, d.width, n, rows.data(), stride);
          hash.addData(QByteArrayView(reinterpret_cast<const char*>(rows.data()), qsizetype(rows.size())));
        }
      }
      if (c->kind == pb::Kind::Group) walk(*c);
    }
  };
  walk(*d.root);
  return QString::fromLatin1(hash.result().toHex());
}

QString DocModel::thumbUrl(const pb::Layer& l) const {
  if (!l.pixels) return {};
  return QStringLiteral("image://pb/layer/%1").arg(pb::thumbs::layer(l.pixels, w_, h_));
}

QJSValue DocModel::props(const pb::Layer& l) {
  const pb::LayerProps& p = l.props;
  std::string s = "{";
  s += kP1Props;
  s += ",\"name\":", quoted(s, p.name);
  s += ",\"visible\":", s += p.visible ? "true" : "false";
  s += ",\"opacity\":", number(s, p.opacity);
  s += ",\"fill\":", number(s, p.fill);
  s += ",\"blend\":\"", s += pb::kBlendNames[size_t(p.blend)], s += '"';
  s += ",\"lockTransparency\":", s += p.lock_transparency ? "true" : "false";
  s += ",\"lockPixels\":", s += p.lock_pixels ? "true" : "false";
  s += ",\"lockPosition\":", s += p.lock_position ? "true" : "false";
  s += ",\"lockAll\":", s += p.lock_all ? "true" : "false";
  if (p.background) s += ",\"background\":true";  // over kP1Props' false
  if (p.extra.size() > 2) s += ',', s.append(p.extra, 1, p.extra.size() - 2);  // over the defaults: JSON's last wins
  // the thumbnail of a pixel layer, as the mock's sample photo (LayerRow shows `photo` through Doc.thumbUrl())
  if (l.kind == pb::Kind::Pixel) s += ",\"photo\":", quoted(s, thumbUrl(l).toStdString());
  s += '}';
  return parse_.call({QJSValue(QString::fromStdString(s))});
}

QJSValue DocModel::node(const LayerPtr& l) {
  Made& m = made_[l.get()];
  if (!m.layer) m.layer = l, m.props = props(*l);
  m.used = true;
  if (m.node.isUndefined()) {
    QJSValue n = js_->newObject();
    n.setProperty(QStringLiteral("id"), uint(l->id));
    n.setProperty(QStringLiteral("kind"), l->kind == pb::Kind::Group ? QStringLiteral("group") : QStringLiteral("pixel"));
    n.setProperty(QStringLiteral("props"), m.props);
    if (l->kind == pb::Kind::Group) {
      QJSValue kids = js_->newArray(uint(l->children.size()));
      for (size_t i = 0; i < l->children.size(); i++) kids.setProperty(uint(i), node(l->children[i]));
      n.setProperty(QStringLiteral("children"), kids);
    }
    m.node = n;  // m still there: an unordered_map's elements stay where they are as it grows
  }
  return m.node;
}

QJSValue DocModel::layerEntry(const LayerPtr& l, int depth, LayerId parent, int index) {
  Made& m = made_[l.get()];
  if (!m.layer) m.layer = l, m.props = props(*l);
  m.used = true;
  if (m.entry.isUndefined() || m.depth != depth || m.parent != parent || m.index != index) {
    QJSValue e = js_->newObject();
    e.setProperty(QStringLiteral("id"), uint(l->id));
    e.setProperty(QStringLiteral("kind"), l->kind == pb::Kind::Group ? QStringLiteral("group") : QStringLiteral("pixel"));
    e.setProperty(QStringLiteral("props"), m.props);
    e.setProperty(QStringLiteral("depth"), depth);
    e.setProperty(QStringLiteral("parent"), uint(parent));
    e.setProperty(QStringLiteral("index"), index);
    e.setProperty(QStringLiteral("count"), uint(l->children.size()));
    m.entry = e, m.depth = depth, m.parent = parent, m.index = index;
  }
  return m.entry;
}

void DocModel::derive() {
  if (!doc_) return;
  const pb::DocPtr s = doc_->state();
  if (s != shown_) {
    const bool tree = !shown_ || s->root != shown_->root;
    const bool sel = !shown_ || s->selected != shown_->selected;
    if (tree) {
      for (auto& [k, m] : made_) m.used = false;
      // layers top first; rows but those inside collapsed groups
      std::vector<QJSValue> entries, rows;
      std::vector<std::pair<LayerId, QJSValue>> byId;
      const std::function<void(const pb::Layer&, int, bool)> walk = [&](const pb::Layer& g, int depth, bool hidden) {
        for (size_t i = g.children.size(); i-- > 0;) {
          const LayerPtr& c = g.children[i];
          QJSValue e = layerEntry(c, depth, g.id, int(i));
          entries.push_back(e);
          if (!hidden) rows.push_back(e);
          byId.emplace_back(c->id, e);
          if (c->kind == pb::Kind::Group)
            walk(*c, depth + 1,
                 hidden || std::find(collapsedIds_.begin(), collapsedIds_.end(), c->id) != collapsedIds_.end());
        }
      };
      walk(*s->root, 0, false);
      QJSValue layers = js_->newArray(uint(entries.size())), rowsJs = js_->newArray(uint(rows.size()));
      for (size_t i = 0; i < entries.size(); i++) layers.setProperty(uint(i), entries[i]);
      for (size_t i = 0; i < rows.size(); i++) rowsJs.setProperty(uint(i), rows[i]);
      QJSValue root = js_->newObject();
      root.setProperty(QStringLiteral("id"), 0);
      root.setProperty(QStringLiteral("kind"), QStringLiteral("group"));
      QJSValue kids = js_->newArray(uint(s->root->children.size()));
      for (size_t i = 0; i < s->root->children.size(); i++) kids.setProperty(uint(i), node(s->root->children[i]));
      root.setProperty(QStringLiteral("children"), kids);
      for (auto it = made_.begin(); it != made_.end();) it = it->second.used ? std::next(it) : made_.erase(it);
      tree_ = root, layers_ = layers, rows_ = rowsJs;
      // the active layer: an entry of layers, the same object
      active_ = QJSValue(QJSValue::NullValue);
      if (!s->selected.empty())
        for (const auto& [lid, e] : byId)
          if (lid == s->selected.back()) active_ = e;
      // what Mock.cx() and the status bar read
      std::vector<const pb::Layer*> all;
      flatten(*s->root, all);
      double held = 0;  // tiles' bytes: the document with its layers
      for (const pb::Layer* l : all)
        if (l->pixels) held += double(l->pixels->tiles().size()) * pb::kTileSize * pb::kTileSize * l->pixels->format().bytes();
      const double flat = double(s->width) * s->height * (s->mode == pb::Mode::Rgb ? 3 : 1) * (s->depth == pb::Depth::U16 ? 2 : 1);
      mem_ = size(flat) + QStringLiteral(" / ") + size(std::max(flat, held));
    }
    if (sel || tree) {
      QJSValue ids = js_->newArray(uint(s->selected.size()));
      for (size_t i = 0; i < s->selected.size(); i++) ids.setProperty(uint(i), uint(s->selected[i]));
      selected_ = ids;
      if (!tree) {  // the active entry of the same layers
        active_ = QJSValue(QJSValue::NullValue);
        const int n = layers_.property(QStringLiteral("length")).toInt();
        for (int i = 0; i < n && !s->selected.empty(); i++)
          if (layers_.property(uint(i)).property(QStringLiteral("id")).toUInt() == s->selected.back())
            active_ = layers_.property(uint(i));
      }
    }
    if (!shown_ || s->mode != shown_->mode || s->depth != shown_->depth || s->icc != shown_->icc) {
      mode_ = s->mode == pb::Mode::Rgb ? QStringLiteral("RGB") : QStringLiteral("Gray");
      bits_ = s->depth == pb::Depth::U16 ? 16 : 8;
      profile_.clear();
      if (s->icc && !s->icc->empty())
        profile_ = QColorSpace::fromIccProfile(QByteArray(reinterpret_cast<const char*>(s->icc->data()), qsizetype(s->icc->size())))
                       .description();
      if (profile_.isEmpty()) profile_ = s->icc ? tr("Hồ sơ không đọc được") : tr("Không có hồ sơ");
    }
    w_ = s->width, h_ = s->height;
    QJSValue st = js_->newObject();
    st.setProperty(QStringLiteral("w"), w_);
    st.setProperty(QStringLiteral("h"), h_);
    st.setProperty(QStringLiteral("ppi"), 72);
    st.setProperty(QStringLiteral("mode"), mode_);
    st.setProperty(QStringLiteral("bits"), bits_);
    st.setProperty(QStringLiteral("profile"), profile_);
    st.setProperty(QStringLiteral("root"), tree_);
    st.setProperty(QStringLiteral("selected"), selected_);
    st.setProperty(QStringLiteral("nextId"), uint(s->next_id));
    st.setProperty(QStringLiteral("alphas"), alphas_);
    state_ = st;
    shown_ = s;
  }
  // History's steps, by serial: a new list when any is another
  std::vector<uint64_t> serials(doc_->steps());
  for (size_t i = 0; i < serials.size(); i++) serials[i] = doc_->step(i).serial;
  if (serials != stepsShown_) {  // each step's entry made once, by serial
    QJSValue list = js_->newArray(uint(serials.size()));
    std::unordered_map<uint64_t, QJSValue> kept;
    for (size_t i = 0; i < serials.size(); i++) {
      auto it = stepEntries_.find(serials[i]);
      if (it == stepEntries_.end()) {
        const pb::Document::Step& st = doc_->step(i);
        const auto [label, icon] = look(st.name);
        QJSValue e = js_->newObject();
        e.setProperty(QStringLiteral("name"), label);
        e.setProperty(QStringLiteral("icon"), icon);
        e.setProperty(QStringLiteral("json"), QString::fromStdString(st.json));
        it = stepEntries_.emplace(serials[i], e).first;
      }
      list.setProperty(uint(i), it->second);
      kept.emplace(serials[i], it->second);
    }
    stepEntries_ = std::move(kept);
    history_ = list;
    stepsShown_ = std::move(serials);
  }
  if (doc_->snapshots().size() != snapshotsShown_) {
    const auto& all = doc_->snapshots();
    QJSValue list = js_->newArray(uint(all.size()));
    for (size_t i = 0; i < all.size(); i++) {
      QJSValue e = js_->newObject();
      e.setProperty(QStringLiteral("name"), QString::fromStdString(all[i].name));
      e.setProperty(QStringLiteral("look"), QStringLiteral("none"));
      e.setProperty(QStringLiteral("url"), QStringLiteral("image://pb/doc/%1").arg(pb::thumbs::document(all[i].state)));
      list.setProperty(uint(i), e);
    }
    snapshots_ = list;
    snapshotsShown_ = all.size();
  }
}
