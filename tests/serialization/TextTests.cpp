#include <devex/serialization/Text.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace devex::serialization;

TEST_CASE("Documents parse sections, attributes and properties", "[serialization][text]")
{
    const auto document = parseText(R"(# A comment
[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Player"]  # trailing comment
parent = "b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"

[component type="Transform"]
position = vec3(0, 1.5, -2)
visible = true
count = -12
)");
    REQUIRE(document.has_value());
    REQUIRE(document->sections.size() == 2);

    const TextSection& entity = document->sections[0];
    CHECK(entity.type == "entity");
    CHECK(entity.line == 2);
    REQUIRE(entity.findAttribute("name") != nullptr);
    CHECK(*asString(*entity.findAttribute("name")) == "Player");
    CHECK(*asString(*entity.findProperty("parent")) == "b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44");

    const TextSection& component = document->sections[1];
    const TextCall* const position = asCall(*component.findProperty("position"), "vec3");
    REQUIRE(position != nullptr);
    REQUIRE(position->arguments.size() == 3);
    CHECK(asNumber(position->arguments[1]) == 1.5);
    CHECK(asInteger(position->arguments[2]) == -2);
    CHECK(asBool(*component.findProperty("visible")) == true);
    CHECK(asInteger(*component.findProperty("count")) == -12);
    CHECK(component.properties[0].line == 6);
}

TEST_CASE("Strings keep escaped characters", "[serialization][text]")
{
    const auto document = parseText("[s]\ntext = \"a \\\"quote\\\"\\n\\\\ and é\"\n");
    REQUIRE(document.has_value());

    CHECK(*asString(*document->sections[0].findProperty("text")) == "a \"quote\"\n\\ and é");
}

TEST_CASE("Parse errors report their line and column", "[serialization][text]")
{
    const auto check = [](std::string_view source, std::string_view message) {
        const auto document = parseText(source);
        REQUIRE_FALSE(document.has_value());
        CHECK(document.error().code == devex::core::ErrorCode::Parse);
        CHECK(document.error().message == message);
    };

    check("key = 1", "line 1, column 1: a property must follow a section header");
    check("[s]\nkey = vec3(1, 2", "line 2, column 16: expected ',' or ')' in vec3(...)");
    check("[s]\n\nkey = \"open", "line 3, column 12: unterminated string");
    check("[s]\nkey = nope", "line 2, column 7: unknown value 'nope'");
    check("[s]\nkey = 1.2.3", "line 2, column 7: invalid number '1.2.3'");
    check("[s name = 1]", "line 1, column 8: expected '=' directly after the attribute name");
}

TEST_CASE("Written documents read back identically", "[serialization][text]")
{
    TextDocument original;
    TextSection section{.type = "component"};
    section.attributes.push_back({"type", TextValue(std::string("Transform"))});
    section.properties.push_back(
        {"position", makeCall("vec3", {TextValue(0.1), TextValue(-2.5), TextValue(1e-7)})});
    section.properties.push_back({"name", TextValue(std::string("line\nbreak \"quoted\""))});
    section.properties.push_back({"enabled", TextValue(false)});
    section.properties.push_back({"count", TextValue(std::int64_t{42})});
    original.sections.push_back(section);
    original.sections.push_back(TextSection{.type = "empty"});

    const std::string text = writeText(original);
    const auto reread = parseText(text);
    REQUIRE(reread.has_value());

    CHECK(text.starts_with("[component type=\"Transform\"]\nposition = vec3(0.1, -2.5, 1e-07)\n"));
    REQUIRE(reread->sections.size() == 2);
    CHECK(reread->sections[1].type == "empty");
    for (std::size_t index = 0; index < section.properties.size(); ++index)
    {
        CHECK(reread->sections[0].properties[index].value == section.properties[index].value);
    }
}
