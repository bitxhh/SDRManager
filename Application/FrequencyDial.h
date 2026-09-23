#pragma once

#include <QWidget>

#include <optional>

class QLineEdit;
class QTimer;

// ---------------------------------------------------------------------------
// FrequencyDial — 10-digit frequency entry widget with a 1 Hz step (up to 9.999 GHz).
//
//   0.102.000.000 Hz
//
// Mouse:
//   • click the upper half of a digit adds 1 there, the lower half subtracts 1
//     (with carry/borrow into the higher digits)
//   • mouse wheel over a digit: ±1 in that position
//   • double click: inline text input ("145.5", "433920k", "1.2G", "145.5M";
//     no suffix means MHz)
// Keyboard (when the widget has focus):
//   • 0–9 overwrite the digit under the cursor and move right
//   • ←/→ move the cursor, ↑/↓ ±1, PgUp/PgDn ±10 in the cursor position
//   • Enter commits immediately, Esc reverts to the value at focus-in
//   • F2 / Space open the text input
//
// Signals:
//   valueChanged(hz)   — on any change (including setValue), for the UI
//   valueCommitted(hz) — only for user changes, after a ~80 ms debounce
//                        (or immediately on Enter) — for retuning the PLL
// ---------------------------------------------------------------------------
class FrequencyDial : public QWidget {
    Q_OBJECT

public:
    static constexpr int kDigits = 10;

    explicit FrequencyDial(QWidget* parent = nullptr);

    void   setRange(qint64 minHz, qint64 maxHz);
    void   setRangeMHz(double minMHz, double maxMHz);
    [[nodiscard]] qint64 minimum() const { return min_; }
    [[nodiscard]] qint64 maximum() const { return max_; }

    [[nodiscard]] qint64 value()    const { return value_; }
    [[nodiscard]] double valueMHz() const { return static_cast<double>(value_) / 1e6; }

    void setCommitDelay(int ms);

    // Parses "145.5", "145.5M", "433920k", "1.2G", "100000000Hz". Without a suffix, MHz.
    static std::optional<qint64> parseFrequency(const QString& text);

    [[nodiscard]] QSize sizeHint()        const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

public slots:
    void setValue(qint64 hz);        // programmatic: emits valueChanged only
    void setValueMHz(double mhz);

signals:
    void valueChanged(qint64 hz);
    void valueCommitted(qint64 hz);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    struct Geometry {
        int    cellW;
        int    sepW;
        int    unitW;
        int    height;
        int    x0;
    };
    [[nodiscard]] Geometry geom() const;
    [[nodiscard]] QRect    digitRect(int i) const;
    [[nodiscard]] int      digitAt(const QPoint& p) const;
    [[nodiscard]] static qint64 placeValue(int i);
    [[nodiscard]] int      digitOf(qint64 v, int i) const;
    [[nodiscard]] qint64   clamp(qint64 hz) const;

    void stepDigit(int i, int delta);
    void overwriteDigit(int i, int d);
    void userSetValue(qint64 hz, bool allowOutOfRange = false);
    void commitNow();

    void openEditor();
    void closeEditor(bool apply);

    qint64 value_{0};                // last valid value (within [min, max])
    qint64 display_{0};              // displayed value; differs from value_ only while a
                                     // typed intermediate value is outside the range
    qint64 min_{0};
    qint64 max_{9'999'999'999LL};
    qint64 focusInValue_{0};
    qint64 lastPressValue_{0};

    int    cursor_{kDigits - 4};     // focused digit (default: 1 kHz position)
    int    hoverDigit_{-1};
    bool   hoverUpper_{false};
    int    wheelAccum_{0};

    QTimer*    commitTimer_{nullptr};
    QLineEdit* editor_{nullptr};
    bool       editorClosing_{false};
};
