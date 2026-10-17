#pragma once
// The start screen of the design (build/proto/Splash.dc.html): its 560 × 340 px scaled to `size`, the bar filled to
// `done`. Painted with QPainter as snapone's (ui/main.cpp there): a raster window shows it before Qt Quick and the
// GPU are up. Colours of the dark theme, the accent the default one (Theme.accents[0]).
#include <QCoreApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>

inline void paintStart(QPainter& p, QSizeF size, qreal done) {
  const QColor accent(0x2FB5B0), dot(0x1d0f0b), t1(0xececed), t3(0x86868b);
  const auto font = [](const char* family, qreal px, QFont::Weight weight) {
    QFont f(QString::fromLatin1(family));
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
  };
  p.save();
  p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  p.scale(size.width() / 560, size.height() / 340);
  QPainterPath box;
  box.addRoundedRect(QRectF(0.5, 0.5, 559, 339), 9.5, 9.5);
  p.fillPath(box, QColor(0x161617));
  p.strokePath(box, QPen(QColor(0x2b2b2e), 1));
  // The tile 64 px and the wordmark (a 30 px line at 1.35), 18 px apart, centred together.
  const qreal top = (340 - (64 + 18 + 30 * 1.35)) / 2;
  p.setPen(Qt::NoPen);
  p.setBrush(accent);
  p.drawRoundedRect(QRectF(248, top, 64, 64), 16, 16);
  p.setBrush(dot);
  p.drawEllipse(QRectF(248 + 14, top + 14, 24, 24));
  QFont word = font("Manrope", 30, QFont::ExtraBold);
  word.setLetterSpacing(QFont::AbsoluteSpacing, -0.3);
  p.setFont(word);
  p.setPen(t1);
  p.drawText(QRectF(0, top + 82, 560, 30 * 1.35), Qt::AlignCenter, QStringLiteral("photo-beauty"));
  // 40 px in from the sides, 34 px up from the bottom: the 2 px bar, 8 px, a line of 11 px text.
  const qreal bar = 340 - 34 - (2 + 8 + 11 * 1.35);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0x3a3a3e));
  p.drawRoundedRect(QRectF(40, bar, 480, 2), 1, 1);
  p.setBrush(accent);
  p.drawRoundedRect(QRectF(40, bar, 480 * done, 2), 1, 1);
  const QRectF line(40, bar + 10, 480, 11 * 1.35);
  p.setPen(t3);
  p.setFont(font("Space Grotesk", 11, QFont::Normal));
  p.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("Splash", "Đang nạp bộ lọc và font…"));
  p.setFont(font("JetBrains Mono", 11, QFont::Normal));
  p.drawText(line, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1%").arg(qRound(done * 100)));
  p.restore();
}
