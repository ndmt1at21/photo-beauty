// copied from snapone 2571122 (ui/titlebar.cpp): Core's useTitleBar() and titleHit() as TitleBar's use() and hit().
#include "titlebar.h"

#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QPointer>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#endif

// The control at p (window coordinates) in item's tree, topmost first: what a press there goes to.
// Text takes mouse buttons too, for links, so only controls count.
static QQuickItem* controlAt(QQuickItem* item, QPointF p) {
  const QList<QQuickItem*> kids = item->childItems();
  for (auto k = kids.rbegin(); k != kids.rend(); ++k)
    if (QQuickItem* c = (*k)->isVisible() ? controlAt(*k, p) : nullptr) return c;
  return item->contains(item->mapFromScene(p)) && item->inherits("QQuickControl") ? item : nullptr;
}

// A popup open on win, tooltips aside: one showing; closing: one that a press outside it closes and lets through
// (CloseOnPressOutside, CloseOnPressOutsideParent, not modal: a modal one keeps that press; not the toast) from the
// moment Qt Quick puts its item on the overlay, before it shows, until the item leaves the overlay.
static bool popupOn(const QQuickWindow* win, bool closing) {
  for (const QQuickItem* top : win->contentItem()->childItems())
    if (top->inherits("QQuickOverlay"))
      for (const QQuickItem* c : top->childItems()) {  // popups' items, the toast
        const QObject* p = c->parent();
        if (p && p->inherits("QQuickPopup") && !p->inherits("QQuickToolTip") &&
            (closing ? (p->property("closePolicy").toInt() & 3) && !p->property("modal").toBool() : c->isVisible()))
          return true;
      }
  return false;
}

// The visible item named "titleBar" in win: the top bar, or the header of a space open over it.
static QQuickItem* barOf(const QQuickWindow* win) {
  if (win)
    for (QQuickItem* b : win->findChildren<QQuickItem*>(QStringLiteral("titleBar")))
      if (b->isVisible()) return b;
  return nullptr;
}

TitleBar::Hit TitleBar::hit(QQuickItem* bar, QPointF p) {
  if (!bar || !bar->isVisible() || !bar->window() || !bar->contains(bar->mapFromScene(p))) return Quick;
  // An open menu or dialog takes every press, as Qt Quick closes the one and blocks for the other.
  // Tooltips close by themselves.
  if (popupOn(bar->window(), false)) return Quick;
  const QQuickItem* c = controlAt(bar, p);
  return !c ? Move : c->objectName() == QLatin1String("maximize") ? Maximize : Quick;
}

QQuickItem* TitleBar::bar(QQuickWindow* win) const { return barOf(win); }

bool TitleBar::popupOpen(QQuickItem* item) const { return item && item->window() && popupOn(item->window(), true); }

#ifdef _WIN32
// The window's messages that make the bar its title bar. Windows keeps the frame: resize edges,
// shadow, rounded corners, snapping, the animations.
class NativeBar : public QAbstractNativeEventFilter {
 public:
  QQuickWindow* win = nullptr;
  QQuickItem* bar = nullptr;
  QQuickItem* max = nullptr;
  HWND hwnd = nullptr;

  // Sent messages (NCCALCSIZE, NCHITTEST) come with a result; posted ones, the mouse's, with null.
  bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override {
    const MSG* m = static_cast<MSG*>(message);
    if (m->hwnd != hwnd) return false;
    switch (m->message) {
      case WM_NCCALCSIZE: {  // the client area up to the window's top; the side and bottom edges stay
        auto* r = reinterpret_cast<RECT*>(m->lParam);  // NCCALCSIZE_PARAMS::rgrc[0] when wParam is TRUE
        const LONG top = r->top;
        *result = DefWindowProcW(hwnd, m->message, m->wParam, m->lParam);
        r->top = top + (IsZoomed(hwnd) ? edge() : 0);  // maximized, the edges lie off the screen
        return true;
      }
      case WM_GETMINMAXINFO: {  // the minimum with the frame the window has when not maximized: no top edge
        // Qt adds the frame of the last state: restored from maximized at its minimum height, it grew by edge().
        const QSize min = win->minimumSize() * win->devicePixelRatio();
        auto* info = reinterpret_cast<MINMAXINFO*>(m->lParam);
        info->ptMinTrackSize = {LONG(min.width() + 2 * edge()), LONG(min.height() + edge())};
        *result = 0;
        return true;
      }
      case WM_NCHITTEST: {
        *result = DefWindowProcW(hwnd, m->message, m->wParam, m->lParam);  // the side and bottom edges
        if (*result != HTCLIENT || win->visibility() == QWindow::FullScreen) return true;  // full screen moves nowhere
        POINT p{GET_X_LPARAM(m->lParam), GET_Y_LPARAM(m->lParam)};
        ScreenToClient(hwnd, &p);
        if (p.y < edge() && !IsZoomed(hwnd)) *result = HTTOP;
        else if (const TitleBar::Hit h = TitleBar::hit(shown(), QPointF(p.x, p.y) / win->devicePixelRatio()); h != TitleBar::Quick)
          *result = h == TitleBar::Move ? HTCAPTION : HTMAXBUTTON;
        return true;
      }
      // The mouse on the maximize button is the system's, for Snap Layouts: the button lights from
      // here, and clicks here, as the system's own handling would draw a button over the bar.
      case WM_NCMOUSEMOVE:
        light(m->wParam == HTMAXBUTTON);
        return false;
      case WM_NCMOUSELEAVE:
      case WM_MOUSEMOVE:
        light(false);
        return false;
      case WM_NCLBUTTONDOWN:
      case WM_NCLBUTTONDBLCLK:
      case WM_NCLBUTTONUP:
        if (m->wParam != HTMAXBUTTON) return false;
        if (m->message == WM_NCLBUTTONUP && pressed) QMetaObject::invokeMethod(max, "click");
        pressed = m->message != WM_NCLBUTTONUP;
        return true;
    }
    return false;
  }

 private:
  bool lit = false, pressed = false;
  QPointer<QQuickItem> bar_;  // the title bar last found: the top bar, or the header of a space open over it

  QQuickItem* shown() {
    if (!bar_ || !bar_->isVisible()) bar_ = barOf(win);
    return bar_;
  }

  int edge() const {  // the top resize edge, as thick as Windows makes the others
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
  }
  void light(bool on) {
    if (on == lit) return;
    lit = on;
    if (on) {  // WM_NCMOUSELEAVE when the mouse leaves for the client area or another window
      TRACKMOUSEEVENT t{sizeof t, TME_LEAVE | TME_NONCLIENT, hwnd, 0};
      TrackMouseEvent(&t);
    } else {
      pressed = false;
    }
    max->setProperty("hot", on);
  }
};
#endif

void TitleBar::use(QQuickWindow* win) {
#ifdef _WIN32
  static NativeBar t;  // the one main window
  if (t.win || !win || QGuiApplication::platformName() != QLatin1String("windows")) return;
  t.bar = win->findChild<QQuickItem*>("titleBar");
  t.max = t.bar ? t.bar->findChild<QQuickItem*>("maximize") : nullptr;
  if (!t.max) return;
  t.win = win;
  t.bar->setProperty("titleBar", true);
  t.hwnd = HWND(win->winId());
  qApp->installNativeEventFilter(&t);
  // Windows sized the window around a title bar: the client area takes its place, then the window
  // gives it up from the top, back to the place and size asked for (the place kept from the last start).
  RECT before, after, frame;
  GetClientRect(t.hwnd, &before);
  SetWindowPos(t.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  GetClientRect(t.hwnd, &after);
  GetWindowRect(t.hwnd, &frame);
  const LONG bar = after.bottom - before.bottom;
  SetWindowPos(t.hwnd, nullptr, frame.left, frame.top + bar, frame.right - frame.left, frame.bottom - frame.top - bar,
               SWP_NOZORDER | SWP_NOACTIVATE);
#else
  Q_UNUSED(win)
#endif
}
