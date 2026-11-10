// PadLook: the pad palette (distinct, readable when playing) and the Loop-2 strip of a pass.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include "plugin/ui/PadLook.h"
#include "plugin/ui/PanelLook.h"

using namespace dc;
using namespace dc::ui::pads;
using Catch::Approx;

namespace {
PassEvent start(double phase, int region) { return PassEvent { phase, false, region, PlayMode::Gate }; }
PassEvent stop(double phase) { return PassEvent { phase, true, -1, PlayMode::Gate }; }
Pass pass(std::vector<PassEvent> events) { return Pass { std::move(events) }; }

float hueDistanceDegrees(juce::Colour a, juce::Colour b)
{
    const float d = std::abs(a.getHue() - b.getHue()) * 360.0f;
    return std::min(d, 360.0f - d);
}
} // namespace

TEST_CASE("contrastRatio follows WCAG", "[padlook]")
{
    CHECK(relativeLuminance(juce::Colours::black) == Approx(0.0f));
    CHECK(relativeLuminance(juce::Colours::white) == Approx(1.0f));
    CHECK(contrastRatio(juce::Colours::black, juce::Colours::white) == Approx(21.0f));
    CHECK(contrastRatio(juce::Colours::white, juce::Colours::black) == Approx(21.0f)); // order does not matter
    CHECK(contrastRatio(juce::Colour(0xff777777), juce::Colour(0xff777777)) == Approx(1.0f));
    // #767676 on white is the classic 4.54:1.
    CHECK(contrastRatio(juce::Colour(0xff767676), juce::Colours::white) == Approx(4.54f).margin(0.01f));
}

TEST_CASE("Pad palette: eight distinct hues, readable text on a playing pad", "[padlook]")
{
    for (int i = 0; i < kPassSlots; ++i)
    {
        CAPTURE(i);
        const auto c = padColour(i);
        CHECK(c.isOpaque());
        CHECK(c.getSaturation() >= 0.35f); // a colour, not a grey
        CHECK(contrastRatio(padPlayingText(i), c) >= 4.5f);
        // The resting fill is darker than the full colour; its number (kText) stays readable.
        CHECK(relativeLuminance(padRestingFill(i)) < relativeLuminance(c));
        CHECK(contrastRatio(dc::ui::panel::kText, padRestingFill(i)) >= 4.5f);
        for (int j = i + 1; j < kPassSlots; ++j)
        {
            CAPTURE(j);
            CHECK(padColour(i) != padColour(j));
            CHECK(hueDistanceDegrees(padColour(i), padColour(j)) >= 14.0f);
        }
    }
    CHECK(padColour(8) == padColour(0)); // wraps
    CHECK(padColour(-1) == padColour(7));
}

TEST_CASE("passStripSegments: an empty pass has no segments", "[padlook][strip]")
{
    CHECK(passStripSegments(Pass {}).empty());
    CHECK(passStripSegments(pass({ stop(0.2), stop(0.6) })).empty()); // only Stops: silence
}

TEST_CASE("passStripSegments: a single Start sounds to the pass end", "[padlook][strip]")
{
    const auto s = passStripSegments(pass({ start(0.25, 3) }));
    REQUIRE(s.size() == 1);
    CHECK(s[0].start == Approx(0.25f));
    CHECK(s[0].end == Approx(1.0f));
    CHECK(s[0].region2 == 3);
}

TEST_CASE("passStripSegments: a Stop ends the segment, silence is a gap", "[padlook][strip]")
{
    const auto s = passStripSegments(pass({ start(0.0, 0), stop(0.5), start(0.75, 2) }));
    REQUIRE(s.size() == 2);
    CHECK(s[0] == StripSegment { 0.0f, 0.5f, 0 });
    CHECK(s[1] == StripSegment { 0.75f, 1.0f, 2 });
}

TEST_CASE("passStripSegments: consecutive Starts each sound until the next event", "[padlook][strip]")
{
    const auto s = passStripSegments(pass({ start(0.0, 1), start(0.25, 0), start(0.5, 1), stop(0.875) }));
    REQUIRE(s.size() == 3);
    CHECK(s[0] == StripSegment { 0.0f, 0.25f, 1 });
    CHECK(s[1] == StripSegment { 0.25f, 0.5f, 0 });
    CHECK(s[2] == StripSegment { 0.5f, 0.875f, 1 });
}

TEST_CASE("passStripSegments: events at the same phase leave no zero-width segment; input is sorted", "[padlook][strip]")
{
    // Unsorted input with two Starts at 0.5: the later one in the (stable) order wins.
    const auto s = passStripSegments(pass({ start(0.5, 1), start(0.5, 2), start(0.1, 0) }));
    REQUIRE(s.size() == 2);
    CHECK(s[0] == StripSegment { 0.1f, 0.5f, 0 });
    CHECK(s[1] == StripSegment { 0.5f, 1.0f, 2 });

    const auto t = passStripSegments(pass({ start(0.3, 0), stop(0.3) }));
    CHECK(t.empty());
}

TEST_CASE("passStripSegments: a full pass of 256 events stays sorted and inside the pass", "[padlook][strip]")
{
    std::vector<PassEvent> events;
    for (int i = 0; i < kMaxPassEvents; ++i) // reversed phases, a Stop every fourth event
        events.push_back(i % 4 == 3 ? stop(1.0 - i / 256.0) : start(1.0 - i / 256.0, i % 7));
    const auto s = passStripSegments(pass(events));
    CHECK(!s.empty());
    CHECK(s.size() <= static_cast<std::size_t>(kMaxPassEvents));
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        CAPTURE(i);
        CHECK(s[i].start >= 0.0f);
        CHECK(s[i].end <= 1.0f);
        CHECK(s[i].end > s[i].start);
        if (i > 0)
            CHECK(s[i].start >= s[i - 1].end);
    }
}

TEST_CASE("passStripSegments: phases are clamped to the pass and non-finite ones dropped", "[padlook][strip]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto s = passStripSegments(pass({ start(-0.5, 0), start(nan, 4), stop(0.5), start(1.5, 1) }));
    REQUIRE(s.size() == 1);
    CHECK(s[0] == StripSegment { 0.0f, 0.5f, 0 }); // the Start at 1.5 is clamped to 1.0: no width
    for (const auto& seg : passStripSegments(pass({ start(0.9, 0), start(2.0, 1) })))
    {
        CHECK(seg.start >= 0.0f);
        CHECK(seg.end <= 1.0f);
        CHECK(seg.end > seg.start);
    }
}
