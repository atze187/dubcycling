// FxList: effect rows in chain order, per-region quick switches, click to select, drag to reorder.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/ui/FxList.h"

using namespace dc;
using dc::ui::FxList;

namespace {

struct ListFixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    FxList list;
    std::vector<FxOrder> orders;
    std::vector<FxInsert> selected;
    std::vector<std::pair<FxInsert, bool>> switched;

    ListFixture()
    {
        list.setSize(130, 150);
        list.onOrderChanged = [this](const FxOrder& o) { orders.push_back(o); };
        list.onSelected = [this](FxInsert i) { selected.push_back(i); };
        list.onSwitched = [this](FxInsert i, bool on) { switched.emplace_back(i, on); };
    }
};

const FxOrder kMixed { FxInsert::Reverb, FxInsert::Delay, FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Flanger };

bool isPermutation(const FxOrder& o) { return sanitizeFxOrder(o) == o; }

juce::Point<float> centre(juce::Rectangle<int> r) { return r.toFloat().getCentre(); }

// A row's body: inside the row, away from handle and switch.
juce::Point<float> bodyOf(const FxList& l, int position)
{
    const auto row = l.rowBounds(position);
    const auto handle = l.handleBounds(position);
    const auto sw = l.switchBounds(l.insertAt(position));
    return { static_cast<float>(handle.getRight() + sw.getX()) * 0.5f, static_cast<float>(row.getCentreY()) };
}

} // namespace

TEST_CASE("FxList: rows follow the order", "[FxList]")
{
    ListFixture f;
    f.list.setContext(kMixed, static_cast<int>(FxInsert::Reverb), 0, true);
    CHECK(f.list.order() == kMixed);
    for (int k = 0; k < kFxInserts; ++k)
    {
        CAPTURE(k);
        CHECK(f.list.insertAt(k) == kMixed[static_cast<std::size_t>(k)]);
        const auto row = f.list.rowBounds(k);
        REQUIRE_FALSE(row.isEmpty());
        CHECK(f.list.getLocalBounds().contains(row));
        CHECK(row.contains(f.list.handleBounds(k)));
        CHECK(row.contains(f.list.switchBounds(f.list.insertAt(k))));
        CHECK_FALSE(f.list.handleBounds(k).intersects(f.list.switchBounds(f.list.insertAt(k))));
        if (k > 0)
            CHECK(row.getY() >= f.list.rowBounds(k - 1).getBottom());
    }
    CHECK(juce::String(dc::ui::fxInsertName(FxInsert::Bitcrush)) == "BITCRUSH");
    CHECK(juce::String(dc::ui::fxInsertName(FxInsert::Delay)) == "DELAY");
    // An invalid order falls back to the default.
    f.list.setContext({ FxInsert::Reverb, FxInsert::Reverb, FxInsert::Reverb, FxInsert::Reverb, FxInsert::Reverb }, 0, 0, true);
    CHECK(f.list.order() == defaultFxOrder());
    CHECK(f.orders.empty());
}

TEST_CASE("FxList: dragRow moves an effect and publishes the new full permutation once", "[FxList]")
{
    ListFixture f;
    f.list.setContext(defaultFxOrder(), 0, 0, true); // Bitcrush Filter Flanger Delay Reverb
    f.list.dragRow(4, 0);                             // Reverb to the front
    REQUIRE(f.orders.size() == 1);
    const FxOrder expected { FxInsert::Reverb, FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay };
    CHECK(f.orders[0] == expected);
    CHECK(f.list.order() == expected);
    f.list.dragRow(1, 1); // no move: nothing published
    CHECK(f.orders.size() == 1);
    f.list.dragRow(0, 2); // Reverb down to position 2
    REQUIRE(f.orders.size() == 2);
    CHECK(f.orders[1] == FxOrder { FxInsert::Bitcrush, FxInsert::Filter, FxInsert::Reverb, FxInsert::Flanger, FxInsert::Delay });
    f.list.dragRow(-1, 3); // out of range
    f.list.dragRow(0, 7);
    CHECK(f.orders.size() == 2);
}

TEST_CASE("FxList: a mouse drag publishes once on release, with a lifted row and an insertion line", "[FxList]")
{
    ListFixture f;
    f.list.setContext(defaultFxOrder(), 0, 0, true);
    f.list.pressAt(centre(f.list.handleBounds(0)));
    CHECK(f.list.isDragging());
    CHECK(f.list.draggedPosition() == 0);
    // Through all rows to below the last one: never published on the way.
    for (int k = 0; k < kFxInserts; ++k)
    {
        f.list.dragTo(centre(f.list.rowBounds(k)).translated(0.0f, 3.0f));
        CHECK(f.list.isDragging());
        CHECK(f.list.insertionSlot() >= 0);
        CHECK(f.list.insertionSlot() <= kFxInserts);
    }
    f.list.dragTo({ 60.0f, static_cast<float>(f.list.rowBounds(kFxInserts - 1).getBottom() + 20) });
    CHECK(f.list.insertionSlot() == kFxInserts);
    CHECK(f.orders.empty());
    CHECK(f.list.order() == defaultFxOrder()); // the order changes only on the drop
    f.list.release();
    CHECK_FALSE(f.list.isDragging());
    CHECK(f.list.insertionSlot() == -1);
    REQUIRE(f.orders.size() == 1);
    CHECK(f.orders[0] == FxOrder { FxInsert::Filter, FxInsert::Flanger, FxInsert::Delay, FxInsert::Reverb, FxInsert::Bitcrush });
    CHECK(f.selected.empty());
    CHECK(f.switched.empty());

    // A drag dropped where it started publishes nothing.
    f.list.pressAt(centre(f.list.handleBounds(2)));
    f.list.dragTo(centre(f.list.handleBounds(2)).translated(0.0f, 2.0f));
    f.list.release();
    CHECK(f.orders.size() == 1);
}

TEST_CASE("FxList: setContext during a drag keeps the dragged order", "[FxList]")
{
    ListFixture f;
    f.list.setContext(defaultFxOrder(), 0, 0, true);
    f.list.pressAt(centre(f.list.handleBounds(0)));
    f.list.dragTo(centre(f.list.rowBounds(2)).translated(0.0f, 4.0f));
    f.list.setContext(kMixed, 0, 0, true); // the editor's poll
    CHECK(f.list.isDragging());
    CHECK(f.list.order() == defaultFxOrder());
    f.list.release();
    REQUIRE(f.orders.size() == 1);
    CHECK(isPermutation(f.orders[0]));
    CHECK(f.orders[0][0] == FxInsert::Filter);
}

TEST_CASE("FxList: a drag works without a selected region", "[FxList]")
{
    ListFixture f;
    f.list.setContext(defaultFxOrder(), 0, 0, false);
    CHECK_FALSE(f.list.switchesEnabled());
    f.list.dragRow(0, 4);
    REQUIRE(f.orders.size() == 1);
    CHECK(f.orders[0][4] == FxInsert::Bitcrush);
    f.list.pressAt(centre(f.list.handleBounds(4)));
    f.list.dragTo({ 60.0f, static_cast<float>(f.list.rowBounds(0).getY()) });
    f.list.release();
    REQUIRE(f.orders.size() == 2);
    CHECK(f.orders[1] == defaultFxOrder());
}

TEST_CASE("FxList: switch click reports the effect id (not the position)", "[FxList]")
{
    ListFixture f;
    const std::uint8_t mask = 1 << static_cast<int>(FxInsert::Delay);
    f.list.setContext(kMixed, 0, mask, true);
    CHECK(f.list.switchOn(FxInsert::Delay));
    CHECK_FALSE(f.list.switchOn(FxInsert::Reverb));
    // Position 1 holds Delay: its switch goes off.
    REQUIRE(f.list.switchBounds(FxInsert::Delay) == f.list.switchBounds(f.list.insertAt(1)));
    f.list.pressAt(centre(f.list.switchBounds(FxInsert::Delay)));
    f.list.release();
    REQUIRE(f.switched.size() == 1);
    CHECK(f.switched[0].first == FxInsert::Delay);
    CHECK_FALSE(f.switched[0].second);
    CHECK_FALSE(f.list.switchOn(FxInsert::Delay));
    // Position 0 holds Reverb: on.
    f.list.pressAt(centre(f.list.switchBounds(FxInsert::Reverb)));
    f.list.release();
    REQUIRE(f.switched.size() == 2);
    CHECK(f.switched[1].first == FxInsert::Reverb);
    CHECK(f.switched[1].second);
    CHECK(f.orders.empty());
    CHECK(f.selected.empty()); // the switch does not select
}

TEST_CASE("FxList: switches are disabled without a region", "[FxList]")
{
    ListFixture f;
    f.list.setContext(defaultFxOrder(), 0, 0xff, false);
    for (int i = 0; i < kFxInserts; ++i)
        CHECK_FALSE(f.list.switchOn(static_cast<FxInsert>(i))); // greyed, shown off
    f.list.pressAt(centre(f.list.switchBounds(FxInsert::Filter)));
    f.list.release();
    CHECK(f.switched.empty());
    f.list.setContext(defaultFxOrder(), 0, 0, true);
    CHECK(f.list.switchesEnabled());
}

TEST_CASE("FxList: click on a row selects", "[FxList]")
{
    ListFixture f;
    f.list.setContext(kMixed, static_cast<int>(FxInsert::Reverb), 0, false);
    CHECK(f.list.selected() == FxInsert::Reverb);
    f.list.pressAt(bodyOf(f.list, 3)); // Filter
    f.list.release();
    REQUIRE(f.selected.size() == 1);
    CHECK(f.selected[0] == FxInsert::Filter);
    CHECK(f.list.selected() == FxInsert::Filter);
    CHECK(f.orders.empty());
    // Clicking the selected row again reports nothing new.
    f.list.pressAt(bodyOf(f.list, 3));
    f.list.release();
    CHECK(f.selected.size() == 1);
    // Below the rows: nothing.
    f.list.pressAt({ 60.0f, static_cast<float>(f.list.getHeight() - 1) });
    f.list.release();
    CHECK(f.selected.size() == 1);
}

TEST_CASE("FxList: handle drag across all positions yields valid permutations", "[FxList]")
{
    ListFixture f;
    for (int from = 0; from < kFxInserts; ++from)
    {
        for (int slot = 0; slot <= kFxInserts; ++slot)
        {
            CAPTURE(from, slot);
            f.list.setContext(kMixed, 0, 0, true);
            f.orders.clear();
            const auto moved = f.list.insertAt(from);
            f.list.pressAt(centre(f.list.handleBounds(from)));
            const float y = slot < kFxInserts ? static_cast<float>(f.list.rowBounds(slot).getY()) + 1.0f
                                              : static_cast<float>(f.list.rowBounds(kFxInserts - 1).getBottom()) + 8.0f;
            f.list.dragTo({ 60.0f, y });
            CHECK(f.list.insertionSlot() == slot);
            f.list.release();
            const int to = slot > from ? slot - 1 : slot;
            if (to == from)
            {
                CHECK(f.orders.empty());
                continue;
            }
            REQUIRE(f.orders.size() == 1);
            CHECK(isPermutation(f.orders[0]));
            CHECK(f.orders[0][static_cast<std::size_t>(to)] == moved);
        }
    }
}
