#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/ui/FxStrip.h"

using namespace dc;
using dc::ui::FxStrip;

namespace {

RegionMap regions(std::vector<double> b)
{
    RegionMap m;
    m.boundaries = std::move(b);
    return m;
}

// A 1000 px wide strip, so x / 1000 is the loop phase (like the lane above it).
struct StripFixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    FxStrip strip;
    std::vector<std::pair<int, FxInsert>> toggled;

    explicit StripFixture(std::vector<double> b = { 0.25, 0.5 }, int width = 1000)
    {
        strip.setSize(width, 16);
        strip.setRegions(regions(std::move(b)));
        strip.onToggled = [this](int r, FxInsert i) { toggled.emplace_back(r, i); };
    }
};

} // namespace

TEST_CASE("FxStrip: every region gets five markers inside its own x range")
{
    StripFixture f;
    const auto width = f.strip.getWidth();
    for (int r = 0; r < 3; ++r)
    {
        const auto [lo, hi] = regions({ 0.25, 0.5 }).bounds(r);
        for (int k = 0; k < kFxInserts; ++k)
        {
            const auto b = f.strip.markerBounds(r, static_cast<FxInsert>(k));
            REQUIRE_FALSE(b.isEmpty());
            CHECK(b.getX() >= static_cast<float>(lo * width));
            CHECK(b.getRight() <= static_cast<float>(hi * width));
        }
    }
}

TEST_CASE("FxStrip: a click on a marker reports region and insert, a click outside the strip does nothing")
{
    StripFixture f;
    const auto b = f.strip.markerBounds(1, FxInsert::Flanger);
    f.strip.toggleAt(b.getCentreX(), b.getCentreY());
    REQUIRE(f.toggled.size() == 1);
    CHECK(f.toggled[0].first == 1);
    CHECK(f.toggled[0].second == FxInsert::Flanger);

    f.strip.toggleAt(-5.0f, 8.0f);
    f.strip.toggleAt(500.0f, -1.0f);
    f.strip.toggleAt(500.0f, 16.0f);
    f.strip.toggleAt(2000.0f, 8.0f);
    CHECK(f.toggled.size() == 1);
}

TEST_CASE("FxStrip: a strip that is not live has no markers and ignores clicks")
{
    StripFixture f;
    const auto b = f.strip.markerBounds(1, FxInsert::Flanger);
    f.strip.setLive(false);
    CHECK(f.strip.markerBounds(1, FxInsert::Flanger).isEmpty());
    f.strip.toggleAt(b.getCentreX(), b.getCentreY());
    CHECK(f.toggled.empty());
    f.strip.setLive(true);
    f.strip.toggleAt(b.getCentreX(), b.getCentreY());
    CHECK(f.toggled.size() == 1);
}

TEST_CASE("FxStrip: markers show the switch state")
{
    StripFixture f;
    f.strip.setSwitches({ 0, 5 });
    CHECK(f.strip.markerOn(1, FxInsert::Bitcrush));
    CHECK_FALSE(f.strip.markerOn(1, FxInsert::Filter));
    CHECK(f.strip.markerOn(1, FxInsert::Flanger));
    CHECK_FALSE(f.strip.markerOn(1, FxInsert::Reverb));
    CHECK_FALSE(f.strip.markerOn(0, FxInsert::Bitcrush));
    CHECK_FALSE(f.strip.markerOn(2, FxInsert::Bitcrush)); // beyond the masks
}

TEST_CASE("FxStrip: markers of different inserts do not overlap")
{
    StripFixture f;
    for (int r = 0; r < 3; ++r)
        for (int a = 0; a < kFxInserts; ++a)
            for (int b = a + 1; b < kFxInserts; ++b)
                CHECK_FALSE(f.strip.markerBounds(r, static_cast<FxInsert>(a))
                                .intersects(f.strip.markerBounds(r, static_cast<FxInsert>(b))));
}

TEST_CASE("FxStrip: a very narrow region shows a collective marker and ignores clicks")
{
    StripFixture f({ 0.01 }); // region 0 is 10 px wide
    CHECK(f.strip.isCollective(0));
    CHECK_FALSE(f.strip.isCollective(1));
    CHECK(f.strip.markerBounds(0, FxInsert::Bitcrush).isEmpty());
    const auto c = f.strip.collectiveBounds(0);
    REQUIRE_FALSE(c.isEmpty());
    f.strip.toggleAt(c.getCentreX(), c.getCentreY());
    CHECK(f.toggled.empty());
}

TEST_CASE("FxStrip: with 30 regions in 600 px nothing overflows the strip")
{
    std::vector<double> b;
    for (int i = 1; i < 30; ++i)
        b.push_back(i / 30.0);
    StripFixture f(b, 600);
    const auto strip = f.strip.getLocalBounds().toFloat();
    for (int r = 0; r < 30; ++r)
    {
        if (f.strip.isCollective(r))
            CHECK(strip.contains(f.strip.collectiveBounds(r)));
        else
            for (int k = 0; k < kFxInserts; ++k)
                CHECK(strip.contains(f.strip.markerBounds(r, static_cast<FxInsert>(k))));
    }
}

TEST_CASE("FxStrip: a click in the gap or above the marker rect hits the marker's column")
{
    StripFixture f;
    const auto b = f.strip.markerBounds(1, FxInsert::Filter);
    f.strip.toggleAt(b.getCentreX(), 0.5f);                // above the rect, inside the strip
    f.strip.toggleAt(b.getCentreX(), 15.5f);               // below the rect
    f.strip.toggleAt(b.getRight() + 0.2f, b.getCentreY()); // gap, closer to this marker
    REQUIRE(f.toggled.size() == 3);
    for (const auto& t : f.toggled)
    {
        CHECK(t.first == 1);
        CHECK(t.second == FxInsert::Filter);
    }
}

TEST_CASE("FxStrip: a column edge belongs to exactly one marker")
{
    StripFixture f;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < kFxInserts; ++k)
        {
            const auto col = f.strip.columnRange(r, static_cast<FxInsert>(k));
            int region = -1;
            FxInsert insert = FxInsert::Bitcrush;
            REQUIRE(f.strip.markerAt(col.getStart(), 8.0f, region, insert));
            CHECK(region == r);
            CHECK(insert == static_cast<FxInsert>(k));
            REQUIRE(f.strip.markerAt(col.getEnd() - 0.01f, 8.0f, region, insert));
            CHECK(region == r);
            CHECK(insert == static_cast<FxInsert>(k));
            if (col.getEnd() < 1000.0f) // the end is the start of the next column
            {
                REQUIRE(f.strip.markerAt(col.getEnd(), 8.0f, region, insert));
                CHECK((region != r || insert != static_cast<FxInsert>(k)));
            }
        }
}

TEST_CASE("FxStrip: a right-click does not report, a left-click does")
{
    StripFixture f;
    const auto b = f.strip.markerBounds(0, FxInsert::Reverb);
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto click = [&](juce::ModifierKeys mods) {
        return juce::MouseEvent(source, b.getCentre(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &f.strip, &f.strip,
                                juce::Time::getCurrentTime(), b.getCentre(), juce::Time::getCurrentTime(), 1, false);
    };
    f.strip.mouseDown(click(juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    CHECK(f.toggled.empty());
    f.strip.mouseDown(click(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    CHECK(f.toggled.size() == 1);
}

TEST_CASE("FxStrip: markers follow the chain order")
{
    StripFixture f;
    FxOrder rev = { FxInsert::Delay, FxInsert::Reverb, FxInsert::Flanger, FxInsert::Filter, FxInsert::Bitcrush };
    f.strip.setOrder(rev);
    CHECK(f.strip.markerBounds(1, FxInsert::Reverb).getX() < f.strip.markerBounds(1, FxInsert::Bitcrush).getX());
    for (int c = 0; c + 1 < kFxInserts; ++c)
        CHECK(f.strip.markerBounds(1, rev[static_cast<std::size_t>(c)]).getX()
              < f.strip.markerBounds(1, rev[static_cast<std::size_t>(c + 1)]).getX());
}

TEST_CASE("FxStrip: a click toggles the effect under the marker, not the column index")
{
    StripFixture f;
    f.strip.setOrder({ FxInsert::Delay, FxInsert::Reverb, FxInsert::Flanger, FxInsert::Filter, FxInsert::Bitcrush });
    const auto b = f.strip.markerBounds(1, FxInsert::Reverb);
    f.strip.toggleAt(b.getCentreX(), b.getCentreY());
    REQUIRE(f.toggled.size() == 1);
    CHECK(f.toggled[0].first == 1);
    CHECK(f.toggled[0].second == FxInsert::Reverb);
}

TEST_CASE("FxStrip: five markers are distinct and non-overlapping in any order")
{
    StripFixture f;
    f.strip.setOrder({ FxInsert::Filter, FxInsert::Delay, FxInsert::Bitcrush, FxInsert::Reverb, FxInsert::Flanger });
    for (int a = 0; a < kFxInserts; ++a)
        for (int b = a + 1; b < kFxInserts; ++b)
        {
            const auto ra = f.strip.markerBounds(1, static_cast<FxInsert>(a));
            const auto rb = f.strip.markerBounds(1, static_cast<FxInsert>(b));
            CHECK_FALSE(ra.intersects(rb));
            const auto ca = f.strip.columnRange(1, static_cast<FxInsert>(a));
            const auto cb = f.strip.columnRange(1, static_cast<FxInsert>(b));
            CHECK((ca.getEnd() <= cb.getStart() || cb.getEnd() <= ca.getStart()));
        }
}

TEST_CASE("FxStrip: colours of all five inserts are pairwise distinct")
{
    for (int a = 0; a < kFxInserts; ++a)
        for (int b = a + 1; b < kFxInserts; ++b)
        {
            const auto ca = dc::ui::fxInsertColour(static_cast<FxInsert>(a));
            const auto cb = dc::ui::fxInsertColour(static_cast<FxInsert>(b));
            const int dr = ca.getRed() - cb.getRed(), dg = ca.getGreen() - cb.getGreen(), db = ca.getBlue() - cb.getBlue();
            CHECK(dr * dr + dg * dg + db * db > 60 * 60);
        }
}

TEST_CASE("FxStrip: switch bits stay with their effect after setOrder")
{
    StripFixture f;
    f.strip.setSwitches({ static_cast<std::uint8_t>(1 << static_cast<int>(FxInsert::Delay)), 0, 0 });
    CHECK(f.strip.markerOn(0, FxInsert::Delay));
    f.strip.setOrder({ FxInsert::Delay, FxInsert::Reverb, FxInsert::Flanger, FxInsert::Filter, FxInsert::Bitcrush });
    CHECK(f.strip.markerOn(0, FxInsert::Delay));
    CHECK_FALSE(f.strip.markerOn(0, FxInsert::Bitcrush));
}

TEST_CASE("FxStrip: an invalid order falls back to the default")
{
    StripFixture f;
    f.strip.setOrder({ FxInsert::Delay, FxInsert::Delay, FxInsert::Delay, FxInsert::Delay, FxInsert::Delay });
    const auto d = defaultFxOrder();
    for (int c = 0; c + 1 < kFxInserts; ++c)
        CHECK(f.strip.markerBounds(1, d[static_cast<std::size_t>(c)]).getX()
              < f.strip.markerBounds(1, d[static_cast<std::size_t>(c + 1)]).getX());
}
