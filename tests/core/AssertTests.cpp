#include <devex/core/Assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using devex::core::AssertAction;
using devex::core::AssertInfo;

namespace {

struct RecordedAssert
{
    std::string expression;
    std::string message;
    std::uint_least32_t line;
};

std::vector<RecordedAssert> recordedAsserts;

AssertAction recordAssert(const AssertInfo& info)
{
    recordedAsserts.push_back(
        {std::string(info.expression), std::string(info.message), info.location.line()});
    return AssertAction::Continue;
}

// Replaces the assert handler so failures are recorded instead of breaking into the debugger.
class AssertCapture
{
public:
    AssertCapture()
        : m_previousHandler(devex::core::setAssertHandler(&recordAssert))
    {
        recordedAsserts.clear();
    }

    ~AssertCapture()
    {
        devex::core::setAssertHandler(m_previousHandler);
    }

    AssertCapture(const AssertCapture&) = delete;
    AssertCapture& operator=(const AssertCapture&) = delete;

private:
    devex::core::AssertHandler m_previousHandler;
};

} // namespace

TEST_CASE("Passing assertions do not reach the handler", "[core][assert]")
{
    const AssertCapture capture;
    int value = 3;

    DEVEX_ASSERT(value == 3);
    DEVEX_ASSERT_MSG(value > 0, "value is {}", value);

    CHECK(recordedAsserts.empty());
}

#ifdef DEVEX_ENABLE_ASSERTS

TEST_CASE("Failing assertions report expression, message and location", "[core][assert]")
{
    const AssertCapture capture;
    int value = -1;

    const auto expectedLine = static_cast<std::uint_least32_t>(__LINE__ + 1);
    DEVEX_ASSERT_MSG(value >= 0, "value is {}", value);
    DEVEX_ASSERT(value == 0);

    REQUIRE(recordedAsserts.size() == 2);
    CHECK(recordedAsserts[0].expression == "value >= 0");
    CHECK(recordedAsserts[0].message == "value is -1");
    CHECK(recordedAsserts[0].line == expectedLine);
    CHECK(recordedAsserts[1].expression == "value == 0");
    CHECK(recordedAsserts[1].message.empty());
}

#else

TEST_CASE("Disabled assertions do not evaluate their condition", "[core][assert]")
{
    int evaluations = 0;

    DEVEX_ASSERT(++evaluations > 0);
    DEVEX_ASSERT_MSG(++evaluations > 0, "{}", ++evaluations);

    CHECK(evaluations == 0);
}

#endif
