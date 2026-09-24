#include <catch2/catch_test_macros.hpp>

#include "ClassificationVote.h"

TEST_CASE("ClassificationVote fires after N confident identical results", "[classifier]") {
    ClassificationVote v;
    CHECK_FALSE(v.feed("NFM", 0.9));
    CHECK_FALSE(v.feed("NFM", 0.85));
    CHECK(v.feed("NFM", 0.95));
    CHECK(v.type() == "NFM");
}

TEST_CASE("ClassificationVote restarts on a different type", "[classifier]") {
    ClassificationVote v;
    CHECK_FALSE(v.feed("NFM", 0.9));
    CHECK_FALSE(v.feed("NFM", 0.9));
    CHECK_FALSE(v.feed("AM", 0.9));
    CHECK_FALSE(v.feed("AM", 0.9));
    CHECK(v.feed("AM", 0.9));
}

TEST_CASE("ClassificationVote restarts on low confidence", "[classifier]") {
    ClassificationVote v;
    CHECK_FALSE(v.feed("FM", 0.9));
    CHECK_FALSE(v.feed("FM", 0.9));
    CHECK_FALSE(v.feed("FM", 0.5));
    CHECK_FALSE(v.feed("FM", 0.9));
    CHECK_FALSE(v.feed("FM", 0.9));
    CHECK(v.feed("FM", 0.8));
}

TEST_CASE("ClassificationVote re-arms after firing and on reset", "[classifier]") {
    ClassificationVote v;
    for (int i = 0; i < 2; ++i) v.feed("USB", 0.9);
    CHECK(v.feed("USB", 0.9));
    CHECK_FALSE(v.feed("USB", 0.9));
    CHECK_FALSE(v.feed("USB", 0.9));
    CHECK(v.feed("USB", 0.9));

    v.feed("USB", 0.9);
    v.feed("USB", 0.9);
    v.reset();
    CHECK_FALSE(v.feed("USB", 0.9));
}
