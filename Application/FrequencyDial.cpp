#include "FrequencyDial.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

namespace {
constexpr int kMargin         = 4;
constexpr int kCommitDelayMs  = 80;

// Separator after digit i (thousands groups): places 9, 6, 3 → i = 0, 3, 6.
bool separatorAfter(int i) {
    const int place = FrequencyDial::kDigits - 1 - i;
    return place > 0 && place % 3 == 0;
}

int separatorsBefore(int i) {
    int n = 0;
    for (int j = 0; j < i; ++j)
        if (separatorAfter(j)) ++n;
    return n;
}
} // namespace

// ---------------------------------------------------------------------------
FrequencyDial::FrequencyDial(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSizeF(std::max(f.pointSizeF(), font().pointSizeF()) * 1.3);
    f.setBold(true);
    setFont(f);

    setToolTip(tr("Double click / F2: text input (145.5, 433920k, 1.2G)"));

    commitTimer_ = new QTimer(this);
    commitTimer_->setSingleShot(true);
    commitTimer_->setInterval(kCommitDelayMs);
    connect(commitTimer_, &QTimer::timeout, this, [this] {
        if (display_ == value_) emit valueCommitted(value_);
    });

    editor_ = new QLineEdit(this);
    editor_->hide();
    editor_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editor_->setPlaceholderText("MHz / k / G");
    editor_->installEventFilter(this);
    connect(editor_, &QLineEdit::editingFinished, this, [this] { closeEditor(true); });
}

// ---------------------------------------------------------------------------
void FrequencyDial::setRange(qint64 minHz, qint64 maxHz) {
    if (minHz > maxHz) std::swap(minHz, maxHz);
    min_ = std::max<qint64>(0, minHz);
    max_ = std::min<qint64>(placeValue(0) * 10 - 1, maxHz);
    if (display_ != value_ || clamp(value_) != value_) {
        const qint64 old = value_;
        value_ = display_ = clamp(value_);
        update();
        if (value_ != old) emit valueChanged(value_);
    }
}

void FrequencyDial::setRangeMHz(double minMHz, double maxMHz) {
    setRange(qRound64(minMHz * 1e6), qRound64(maxMHz * 1e6));
}

void FrequencyDial::setCommitDelay(int ms) {
    commitTimer_->setInterval(std::max(0, ms));
}

void FrequencyDial::setValue(qint64 hz) {
    hz = clamp(hz);
    const bool changed = (hz != value_);
    value_ = display_ = hz;
    update();
    if (changed) emit valueChanged(value_);
}

void FrequencyDial::setValueMHz(double mhz) {
    setValue(qRound64(mhz * 1e6));
}

// ---------------------------------------------------------------------------
std::optional<qint64> FrequencyDial::parseFrequency(const QString& text) {
    QString t = text.trimmed().toLower();
    t.remove(' ');
    t.remove('_');
    t.replace(',', '.');

    bool hadHz = false;
    if (t.endsWith("hz")) { t.chop(2); hadHz = true; }

    double mult = hadHz ? 1.0 : 1e6;
    if (!t.isEmpty()) {
        const QChar last = t.back();
        if      (last == 'g') { mult = 1e9; t.chop(1); }
        else if (last == 'm') { mult = 1e6; t.chop(1); }
        else if (last == 'k') { mult = 1e3; t.chop(1); }
    }

    // "0.102.000.000" — the dial's own format (grouped Hz)
    if (t.count('.') > 1) {
        t.remove('.');
        mult = 1.0;
    }

    bool ok = false;
    const double v = QLocale::c().toDouble(t, &ok);
    if (!ok || v < 0.0) return std::nullopt;
    return qRound64(v * mult);
}

// ---------------------------------------------------------------------------
qint64 FrequencyDial::placeValue(int i) {
    qint64 p = 1;
    for (int k = 0; k < kDigits - 1 - i; ++k) p *= 10;
    return p;
}

int FrequencyDial::digitOf(qint64 v, int i) const {
    return static_cast<int>((v / placeValue(i)) % 10);
}

qint64 FrequencyDial::clamp(qint64 hz) const {
    return std::clamp(hz, min_, max_);
}

FrequencyDial::Geometry FrequencyDial::geom() const {
    const QFontMetrics fm(font());
    Geometry g{};
    g.cellW  = fm.horizontalAdvance('0') + 4;
    g.sepW   = std::max(5, fm.horizontalAdvance('.'));
    g.unitW  = fm.horizontalAdvance(" Hz") + kMargin;
    g.height = fm.height() + 8;
    g.x0     = kMargin;
    return g;
}

QRect FrequencyDial::digitRect(int i) const {
    const Geometry g = geom();
    const int x = g.x0 + i * g.cellW + separatorsBefore(i) * g.sepW;
    return {x, 1, g.cellW, height() - 2};
}

int FrequencyDial::digitAt(const QPoint& p) const {
    for (int i = 0; i < kDigits; ++i)
        if (digitRect(i).contains(p)) return i;
    return -1;
}

QSize FrequencyDial::sizeHint() const {
    const Geometry g = geom();
    return {2 * g.x0 + kDigits * g.cellW + separatorsBefore(kDigits) * g.sepW + g.unitW,
            g.height};
}

// ---------------------------------------------------------------------------
void FrequencyDial::userSetValue(qint64 hz, bool allowOutOfRange) {
    hz = std::clamp<qint64>(hz, 0, placeValue(0) * 10 - 1);
    if (!allowOutOfRange) hz = clamp(hz);
    if (hz == display_) return;
    display_ = hz;
    update();
    if (clamp(display_) != display_) return;   // pending, not applied yet
    value_ = display_;
    emit valueChanged(value_);
    commitTimer_->start();
}

void FrequencyDial::commitNow() {
    commitTimer_->stop();
    if (display_ != value_) userSetValue(clamp(display_));
    commitTimer_->stop();
    emit valueCommitted(value_);
}

void FrequencyDial::stepDigit(int i, int delta) {
    if (i < 0 || i >= kDigits || delta == 0) return;
    userSetValue(clamp(display_ + delta * placeValue(i)));
}

void FrequencyDial::overwriteDigit(int i, int d) {
    if (i < 0 || i >= kDigits) return;
    const qint64 p = placeValue(i);
    userSetValue(display_ - digitOf(display_, i) * p + d * p, /*allowOutOfRange=*/true);
}

// ---------------------------------------------------------------------------
void FrequencyDial::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QPalette& pal = palette();
    const bool enabled  = isEnabled();

    // Frame
    p.fillRect(rect(), pal.color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::Base));
    p.setPen(hasFocus() ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    QColor textColor = pal.color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::Text);
    if (display_ != value_) textColor = QColor(220, 50, 50);   // typed value out of range
    QColor dimColor = textColor;
    dimColor.setAlpha(80);

    QColor hoverColor = pal.color(QPalette::Highlight);
    hoverColor.setAlpha(70);

    const Geometry g = geom();
    const QFontMetrics fm(font());

    // First significant digit (everything to the left of it is dimmed)
    int firstSig = kDigits - 1;
    for (int i = 0; i < kDigits; ++i)
        if (digitOf(display_, i) != 0) { firstSig = i; break; }

    for (int i = 0; i < kDigits; ++i) {
        const QRect r = digitRect(i);

        if (enabled && i == hoverDigit_) {
            const QRect half = hoverUpper_
                ? QRect(r.left(), r.top(), r.width(), r.height() / 2)
                : QRect(r.left(), r.top() + r.height() / 2, r.width(), r.height() - r.height() / 2);
            p.fillRect(half, hoverColor);
        }

        p.setPen(i < firstSig ? dimColor : textColor);
        p.drawText(r, Qt::AlignCenter, QString::number(digitOf(display_, i)));

        if (enabled && hasFocus() && i == cursor_ && !editor_->isVisible())
            p.fillRect(QRect(r.left() + 2, r.bottom() - 2, r.width() - 4, 2),
                       pal.color(QPalette::Highlight));

        if (separatorAfter(i)) {
            const QRect sr(r.right() + 1, r.top(), g.sepW, r.height());
            p.setPen(i < firstSig ? dimColor : textColor);
            p.drawText(sr, Qt::AlignCenter, ".");
        }
    }

    // Unit label
    QFont uf = font();
    uf.setBold(false);
    p.setFont(uf);
    p.setPen(dimColor.alpha() ? pal.color(QPalette::Disabled, QPalette::Text) : textColor);
    const QRect last = digitRect(kDigits - 1);
    p.drawText(QRect(last.right() + 1, 0, g.unitW, height()), Qt::AlignCenter, "Hz");
    Q_UNUSED(fm);
}

// ---------------------------------------------------------------------------
void FrequencyDial::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }
    const int i = digitAt(e->position().toPoint());
    if (i < 0) return;
    lastPressValue_ = display_;
    cursor_ = i;
    const QRect r = digitRect(i);
    stepDigit(i, e->position().y() < r.center().y() + 0.5 ? +1 : -1);
    e->accept();
}

void FrequencyDial::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QWidget::mouseDoubleClickEvent(e); return; }
    // The first click of the double click has already stepped a digit — undo it.
    userSetValue(lastPressValue_);
    openEditor();
    e->accept();
}

void FrequencyDial::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const int i = digitAt(pos);
    const bool upper = (i >= 0) && pos.y() < digitRect(i).center().y() + 1;
    if (i != hoverDigit_ || upper != hoverUpper_) {
        hoverDigit_ = i;
        hoverUpper_ = upper;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void FrequencyDial::leaveEvent(QEvent* e) {
    hoverDigit_ = -1;
    update();
    QWidget::leaveEvent(e);
}

void FrequencyDial::wheelEvent(QWheelEvent* e) {
    wheelAccum_ += e->angleDelta().y();
    const int steps = wheelAccum_ / 120;
    wheelAccum_ %= 120;
    const int i = digitAt(e->position().toPoint());
    const int digit = (i >= 0) ? i : cursor_;
    if (steps != 0) {
        cursor_ = digit;
        stepDigit(digit, steps);
    }
    e->accept();
}

// ---------------------------------------------------------------------------
void FrequencyDial::keyPressEvent(QKeyEvent* e) {
    const int key = e->key();

    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        overwriteDigit(cursor_, key - Qt::Key_0);
        cursor_ = std::min(cursor_ + 1, kDigits - 1);
        update();
        return;
    }

    switch (key) {
    case Qt::Key_Left:     cursor_ = std::max(cursor_ - 1, 0);           update(); return;
    case Qt::Key_Right:    cursor_ = std::min(cursor_ + 1, kDigits - 1); update(); return;
    case Qt::Key_Home:     cursor_ = 0;                                  update(); return;
    case Qt::Key_End:      cursor_ = kDigits - 1;                        update(); return;
    case Qt::Key_Up:       stepDigit(cursor_, +1);  return;
    case Qt::Key_Down:     stepDigit(cursor_, -1);  return;
    case Qt::Key_PageUp:   stepDigit(cursor_, +10); return;
    case Qt::Key_PageDown: stepDigit(cursor_, -10); return;
    case Qt::Key_Backspace:
        cursor_ = std::max(cursor_ - 1, 0);
        overwriteDigit(cursor_, 0);
        update();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        commitNow();
        return;
    case Qt::Key_Escape:
        userSetValue(focusInValue_);
        commitNow();
        return;
    case Qt::Key_F2:
    case Qt::Key_Space:
        openEditor();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void FrequencyDial::focusInEvent(QFocusEvent* e) {
    if (!editor_->isVisible()) focusInValue_ = value_;
    update();
    QWidget::focusInEvent(e);
}

void FrequencyDial::focusOutEvent(QFocusEvent* e) {
    if (display_ != value_) commitNow();   // clamp a pending out-of-range value
    update();
    QWidget::focusOutEvent(e);
}

// ---------------------------------------------------------------------------
void FrequencyDial::openEditor() {
    if (!isEnabled() || editor_->isVisible()) return;
    QString s = QString::number(static_cast<double>(value_) / 1e6, 'f', 6);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);

    QFont ef = font();
    ef.setBold(false);
    editor_->setFont(ef);
    editor_->setGeometry(rect().adjusted(1, 1, -1, -1));
    editor_->setText(s);
    editor_->selectAll();
    editor_->show();
    editor_->setFocus();
    update();
}

void FrequencyDial::closeEditor(bool apply) {
    if (editorClosing_ || !editor_->isVisible()) return;
    editorClosing_ = true;

    if (apply) {
        if (const auto hz = parseFrequency(editor_->text())) {
            userSetValue(*hz);
            commitNow();
        }
    }
    const bool hadFocus = editor_->hasFocus();
    editor_->hide();
    if (hadFocus) setFocus();
    editorClosing_ = false;
    update();
}

bool FrequencyDial::eventFilter(QObject* obj, QEvent* e) {
    if (obj == editor_ && e->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            closeEditor(false);
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}
