#pragma once

#include <QString>

// ---------------------------------------------------------------------------
// ClassificationVote — debounces classifier results for auto mode.
//
// feed() returns true once the same type has arrived kRequired times in a row,
// each with confidence >= kThreshold. A different type or a low-confidence
// result restarts the count. After firing, the count restarts too, so a
// steady signal fires again only after another kRequired results.
// ---------------------------------------------------------------------------
class ClassificationVote {
public:
    static constexpr double kThreshold = 0.8;
    static constexpr int    kRequired  = 3;

    bool feed(const QString& type, double confidence) {
        if (confidence < kThreshold || type.isEmpty()) { reset(); return false; }
        if (type != type_) { type_ = type; count_ = 0; }
        if (++count_ < kRequired) return false;
        count_ = 0;
        return true;
    }

    void reset() { type_.clear(); count_ = 0; }

    [[nodiscard]] const QString& type() const { return type_; }

private:
    QString type_;
    int     count_{0};
};
