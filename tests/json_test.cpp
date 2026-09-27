// SPDX-License-Identifier: GPL-3.0-only
#include "json.hpp"

#include <gtest/gtest.h>

namespace priomix::json {
namespace {

TEST(Json, ParsesNestedDocument) {
    auto v = Parse(R"({"a": [1, 2.5, -3], "b": {"c": "x\"yä"}, "d": true, "e": null})");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->Find("a")->AsArray().size(), 3u);
    EXPECT_DOUBLE_EQ(v->Find("a")->AsArray()[1].AsNumber(), 2.5);
    EXPECT_EQ(v->Find("b")->Find("c")->AsString(), "x\"y\xc3\xa4");
    EXPECT_TRUE(v->Find("d")->AsBool());
    EXPECT_TRUE(v->Find("e")->IsNull());
    EXPECT_EQ(v->Find("missing"), nullptr);
}

TEST(Json, RejectsMalformedInput) {
    std::string error;
    EXPECT_FALSE(Parse("{\"a\": }", &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(Parse("[1, 2"));
    EXPECT_FALSE(Parse("{} trailing"));
    EXPECT_FALSE(Parse("\"unterminated"));
}

TEST(Json, RoundTrip) {
    Value original(Object{{"list", Array{Value(Object{{"id", "usb:1:2"}}), Value(1)}},
                          {"name", "tab\tquote\""},
                          {"flag", false}});
    auto parsed = Parse(original.Serialize());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->Serialize(), original.Serialize());
    EXPECT_EQ(parsed->Find("name")->AsString(), "tab\tquote\"");
}

}  // namespace
}  // namespace priomix::json
