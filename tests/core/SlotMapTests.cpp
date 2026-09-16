#include <devex/core/SlotMap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using devex::core::SlotMap;

TEST_CASE("Inserted values are found through their handle", "[core][slotmap]")
{
    SlotMap<std::string> names;

    const auto first = names.insert("first");
    const auto second = names.insert("second");

    REQUIRE(names.find(first) != nullptr);
    CHECK(*names.find(first) == "first");
    CHECK(*names.find(second) == "second");
    CHECK(names.size() == 2);
    CHECK_FALSE(SlotMap<std::string>::HandleType{}.isValid());
}

TEST_CASE("Removed values leave their handles stale", "[core][slotmap]")
{
    SlotMap<std::string> names;
    const auto handle = names.insert("value");

    const std::optional<std::string> removed = names.remove(handle);

    REQUIRE(removed.has_value());
    CHECK(*removed == "value");
    CHECK_FALSE(names.contains(handle));
    CHECK(names.find(handle) == nullptr);
    CHECK_FALSE(names.remove(handle).has_value());
    CHECK(names.empty());
}

TEST_CASE("Reused slots do not revive old handles", "[core][slotmap]")
{
    SlotMap<std::string> names;
    const auto old = names.insert("old");
    static_cast<void>(names.remove(old));

    const auto reused = names.insert("new");

    CHECK(reused.index == old.index);
    CHECK(reused.generation != old.generation);
    CHECK(names.find(old) == nullptr);
    CHECK(*names.find(reused) == "new");
}

TEST_CASE("Move-only values are supported", "[core][slotmap]")
{
    SlotMap<std::unique_ptr<int>> values;
    const auto handle = values.insert(std::make_unique<int>(42));

    CHECK(**values.find(handle) == 42);
    const std::optional<std::unique_ptr<int>> removed = values.remove(handle);
    REQUIRE(removed.has_value());
    CHECK(**removed == 42);
}
