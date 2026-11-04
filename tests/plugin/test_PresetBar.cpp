// PresetBar: bank and preset selection, loading with the unsaved-changes question, layout.
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/DubcyclingProcessor.h"
#include "plugin/presets/BankFormat.h"
#include "plugin/presets/BankStore.h"
#include "plugin/ui/Editor.h"
#include "plugin/ui/PresetBar.h"
#include "tests/plugin/TestLoops.h"

using namespace dc;
using dc::testing::LoopPair;
using dc::ui::PresetBar;

namespace {

struct BarFixture
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcbar", "", false);
    DubcyclingProcessor proc;
    std::unique_ptr<dc::ui::Editor> editor;

    BarFixture() { root.createDirectory(); }
    ~BarFixture()
    {
        editor.reset();
        root.deleteRecursively();
    }

    void open() { editor = std::make_unique<dc::ui::Editor>(proc, root); }
    PresetBar& bar() { return editor->presetBar(); }
    juce::String fingerprint() const { return presetFingerprint(proc.getPluginState()); }

    // Saves a preset whose state is a loaded loop pair with the given Loop-1 boundaries; creates
    // the bank when needed.
    void savePreset(const juce::String& bank, const juce::String& name, std::vector<double> boundaries1)
    {
        DubcyclingProcessor src;
        LoopPair loops(src);
        auto s = src.getPluginState();
        s.loop1.boundaries = std::move(boundaries1);
        src.applyEdit(s);
        BankStore store(root);
        if (!store.listBanks().contains(bank))
            REQUIRE(store.createBank(bank).ok());
        REQUIRE(store.savePreset(bank, name, src.getPluginState(), false).ok());
    }

    juce::String storedFingerprint(const juce::String& bank, const juce::String& name) const
    {
        const auto loaded = BankStore(root).loadPreset(bank, name);
        REQUIRE(loaded.result.ok());
        return presetFingerprint(loaded.state);
    }
};

// Text of every ticked item of a popup menu.
juce::StringArray ticked(const juce::PopupMenu& menu)
{
    juce::StringArray out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().isTicked)
            out.add(it.getItem().text);
    return out;
}

// No two visible children of `parent` overlap, and all lie inside it. The resize corner is window
// chrome drawn over the content on purpose and is left out.
void checkNoOverlap(const juce::Component& parent)
{
    std::vector<juce::Component*> visible;
    for (auto* c : parent.getChildren())
        if (c->isVisible() && !c->getBounds().isEmpty() && dynamic_cast<juce::ResizableCornerComponent*>(c) == nullptr)
            visible.push_back(c);
    for (std::size_t i = 0; i < visible.size(); ++i)
    {
        INFO(visible[i]->getName() << " " << visible[i]->getBounds().toString().toStdString());
        CHECK(parent.getLocalBounds().contains(visible[i]->getBounds()));
        for (std::size_t j = i + 1; j < visible.size(); ++j)
        {
            INFO("vs " << visible[j]->getBounds().toString().toStdString());
            CHECK_FALSE(visible[i]->getBounds().intersects(visible[j]->getBounds()));
        }
    }
}

void writeSnapshot(juce::Component& c, const juce::String& name)
{
    auto img = c.createComponentSnapshot(c.getLocalBounds());
    CHECK(img.isValid());
    const auto snapDir = juce::SystemStats::getEnvironmentVariable("DC_SNAPSHOT_DIR", {});
    if (snapDir.isEmpty())
        return;
    auto png = juce::File(snapDir).getChildFile(name);
    png.deleteFile();
    if (auto out = png.createOutputStream())
        juce::PNGImageFormat().writeImageToStream(img, *out);
}

} // namespace

TEST_CASE("PresetBar: lists banks alphabetically and presets of the selected bank", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("beta", "Solo", {});
    f.savePreset("Alpha", "Zwei", { 0.5 });
    f.savePreset("Alpha", "Eins", { 0.25 });
    REQUIRE(BankStore(f.root).createBank("gamma").ok());
    f.open();

    CHECK(f.bar().bankNames() == juce::StringArray { "Alpha", "beta", "gamma" });
    CHECK(f.bar().selectedBank() == "Alpha"); // no active preset: the first bank
    CHECK(f.bar().presetNames() == juce::StringArray { "Zwei", "Eins" }); // creation order
    CHECK(ticked(f.bar().bankMenu()) == juce::StringArray { "Alpha" });

    f.bar().selectBank("beta");
    CHECK(f.bar().selectedBank() == "beta");
    CHECK(f.bar().presetNames() == juce::StringArray { "Solo" });
    CHECK(ticked(f.bar().bankMenu()) == juce::StringArray { "beta" });

    f.bar().selectBank("gamma");
    CHECK(f.bar().presetNames().isEmpty());

    f.bar().selectBank("..\\Alpha"); // not a listed bank: ignored
    CHECK(f.bar().selectedBank() == "gamma");
}

TEST_CASE("PresetBar: selecting a bank loads nothing", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.savePreset("B", "Zwei", { 0.25 });
    LoopPair loops(f.proc);
    f.open();
    const auto before = f.fingerprint();

    f.bar().selectBank("B");
    CHECK(f.bar().selectedBank() == "B");
    CHECK(f.fingerprint() == before);
    CHECK(f.bar().activePresetName().isEmpty());
    CHECK(f.proc.getPluginState().activePreset.isEmpty());
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
}

TEST_CASE("PresetBar: loading a preset restores the state and marks it active", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.savePreset("A", "Zwei", { 0.25 });
    f.open();

    f.bar().requestLoad("Zwei");
    f.proc.waitForLoads();
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().isEmpty());
    CHECK(f.fingerprint() == f.storedFingerprint("A", "Zwei"));
    CHECK(f.proc.getPluginState().activeBank == "A");
    CHECK(f.proc.getPluginState().activePreset == "Zwei");
    CHECK(f.bar().activePresetName() == "Zwei");
    CHECK(ticked(f.bar().presetMenu()) == juce::StringArray { "Zwei" });
    CHECK(f.proc.getSlotStatus(0) == DubcyclingProcessor::SlotStatus::Loaded); // sample from the bank

    // Another bank shows no active preset; back in the bank the preset is marked again.
    REQUIRE(BankStore(f.root).createBank("B").ok());
    f.bar().refresh();
    f.bar().selectBank("B");
    CHECK(f.bar().activePresetName().isEmpty());
    CHECK(ticked(f.bar().presetMenu()).isEmpty());
    f.bar().selectBank("A");
    CHECK(f.bar().activePresetName() == "Zwei");
}

TEST_CASE("PresetBar: loading asks first when the current state is unsaved", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    LoopPair loops(f.proc);
    auto s = f.proc.getPluginState();
    s.loop2.boundaries = { 0.75 };
    f.proc.applyEdit(s);
    f.open();
    const auto before = f.fingerprint();

    f.bar().requestLoad("Eins");
    CHECK(f.bar().mode() == PresetBar::Mode::ConfirmDiscard);
    CHECK(f.fingerprint() == before);

    f.bar().confirmDiscard(false);
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.fingerprint() == before);
    CHECK(f.proc.getPluginState().activePreset.isEmpty());

    f.bar().requestLoad("Eins");
    CHECK(f.bar().mode() == PresetBar::Mode::ConfirmDiscard);
    f.bar().confirmDiscard(true);
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.fingerprint() == f.storedFingerprint("A", "Eins"));
    CHECK(f.proc.getPluginState().activePreset == "Eins");
}

TEST_CASE("PresetBar: loading does not ask when the state equals the active preset", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.savePreset("A", "Zwei", { 0.25 });
    f.open();
    f.bar().requestLoad("Eins");
    REQUIRE(f.proc.getPluginState().activePreset == "Eins");
    CHECK_FALSE(f.bar().hasUnsavedChanges());

    f.bar().requestLoad("Zwei");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.proc.getPluginState().activePreset == "Zwei");
    CHECK(f.fingerprint() == f.storedFingerprint("A", "Zwei"));

    // An edit after loading makes the state unsaved again.
    auto s = f.proc.getPluginState();
    s.loop1.boundaries = { 0.125 };
    f.proc.applyEdit(s);
    CHECK(f.bar().hasUnsavedChanges());
}

TEST_CASE("PresetBar: with no active preset a state with a loop counts as unsaved; the empty state does not", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", {});
    f.open();
    CHECK_FALSE(f.bar().hasUnsavedChanges());

    LoopPair loops(f.proc);
    CHECK(f.bar().hasUnsavedChanges());

    // A stale or hostile active preset from the session counts as none.
    f.proc.setActivePreset("..\\A", "Eins");
    CHECK(f.bar().hasUnsavedChanges());
    CHECK(f.bar().activePresetName().isEmpty());
    f.proc.setActivePreset("Gone", "Eins");
    CHECK(f.bar().hasUnsavedChanges());
    CHECK(f.bar().activePresetName().isEmpty());
    // The active preset no longer in its bank: compared against the empty state.
    f.proc.setActivePreset("A", "Weg");
    CHECK(f.bar().hasUnsavedChanges());
    // Loop pair without boundaries equals the stored "Eins".
    f.proc.setActivePreset("A", "Eins");
    CHECK_FALSE(f.bar().hasUnsavedChanges());
}

TEST_CASE("PresetBar: a too-new or damaged bank shows a message and changes nothing", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    LoopPair loops(f.proc);
    f.open();
    const auto before = f.fingerprint();
    const auto bankFile = f.root.getChildFile("A").getChildFile("bank.json");

    REQUIRE(bankFile.replaceWithText("{\"version\": 99, \"presets\": []}"));
    f.bar().requestLoad("Eins");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().contains("neueren Version"));
    CHECK(f.fingerprint() == before);
    CHECK(f.proc.getPluginState().activePreset.isEmpty());
    CHECK(bankFile.loadFileAsString() == "{\"version\": 99, \"presets\": []}");

    REQUIRE(bankFile.replaceWithText("not json"));
    f.bar().requestLoad("Eins");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().contains("Bank-Datei"));
    CHECK(f.fingerprint() == before);

    // Opening the damaged bank's list: no presets, a message, nothing else changes.
    f.bar().selectBank("A");
    CHECK(f.bar().presetNames().isEmpty());
    CHECK(f.bar().message().isNotEmpty());
    CHECK(f.fingerprint() == before);

    f.savePreset("B", "Zwei", {});
    f.bar().refresh();
    f.bar().selectBank("B");
    f.bar().requestLoad("Gibt es nicht");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().isNotEmpty());
    CHECK(f.fingerprint() == before);
}

TEST_CASE("PresetBar: the bar fits next to the BPM readout at the default editor size", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("Meine Bank", "Eins", { 0.5 });
    LoopPair loops(f.proc);
    f.open();
    REQUIRE(f.editor->getWidth() == 760);
    REQUIRE(f.editor->getHeight() == 480);
    f.editor->refresh();
    CHECK(f.editor->cycleText() == "Zyklus: 2 Takte");

    checkNoOverlap(*f.editor);
    checkNoOverlap(f.bar());
    writeSnapshot(*f.editor, "editor_presetbar.png");

    f.bar().requestLoad("Eins");
    REQUIRE(f.bar().mode() == PresetBar::Mode::ConfirmDiscard);
    checkNoOverlap(f.bar());
    writeSnapshot(*f.editor, "editor_presetbar_confirm.png");
    f.bar().confirmDiscard(false);

    // Wide enough, the cycle readout comes back without overlapping.
    f.editor->setSize(1100, 480);
    checkNoOverlap(*f.editor);
    bool cycleShown = false;
    for (auto* c : f.editor->getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(c); label != nullptr && label->isVisible())
            cycleShown = cycleShown || label->getText().startsWith("Zyklus");
    CHECK(cycleShown);
}
