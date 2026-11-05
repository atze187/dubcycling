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
    juce::File outDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("dcbarout", "", false);
    DubcyclingProcessor proc;
    std::unique_ptr<dc::ui::Editor> editor;

    BarFixture()
    {
        root.createDirectory();
        outDir.createDirectory();
    }
    ~BarFixture()
    {
        editor.reset();
        root.deleteRecursively();
        outDir.deleteRecursively();
    }

    void open() { editor = std::make_unique<dc::ui::Editor>(proc, root); }
    PresetBar& bar() { return editor->presetBar(); }
    juce::String fingerprint() const { return presetFingerprint(proc.getPluginState()); }
    juce::String bankText(const juce::String& bank) const
    {
        return root.getChildFile(bank).getChildFile(BankStore::kBankFileName).loadFileAsString();
    }

    void editLoop1(std::vector<double> boundaries)
    {
        auto s = proc.getPluginState();
        s.loop1.boundaries = std::move(boundaries);
        proc.applyEdit(s);
    }

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

// Every visible text button of `parent` is wide enough for its text.
void checkButtonTextFits(const juce::Component& parent)
{
    for (auto* c : parent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->isVisible())
        {
            const auto font = b->getLookAndFeel().getTextButtonFont(*b, b->getHeight());
            INFO(b->getButtonText().toStdString() << " in " << b->getWidth());
            CHECK(juce::GlyphArrangement::getStringWidthInt(font, b->getButtonText()) + 6 <= b->getWidth());
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
    REQUIRE(f.editor->getWidth() == 900); // grown for the effects panel (was 760x480)
    REQUIRE(f.editor->getHeight() == 600);
    f.editor->refresh();
    CHECK(f.editor->cycleText() == "Zyklus: 2 Takte");

    checkNoOverlap(*f.editor);
    checkNoOverlap(f.bar());
    checkButtonTextFits(f.bar());
    writeSnapshot(*f.editor, "editor_presetbar.png");

    f.bar().requestLoad("Eins");
    REQUIRE(f.bar().mode() == PresetBar::Mode::ConfirmDiscard);
    checkNoOverlap(f.bar());
    checkButtonTextFits(f.bar());
    writeSnapshot(*f.editor, "editor_presetbar_confirm.png");
    f.bar().confirmDiscard(false);

    f.bar().requestSaveAs();
    REQUIRE(f.bar().mode() == PresetBar::Mode::NamePrompt);
    checkNoOverlap(f.bar());
    checkButtonTextFits(f.bar());
    writeSnapshot(*f.editor, "editor_presetbar_prompt.png");
    f.bar().submitName("Eins"); // taken: the message shows next to the name field
    REQUIRE(f.bar().mode() == PresetBar::Mode::NamePrompt);
    REQUIRE(f.bar().message().isNotEmpty());
    checkNoOverlap(f.bar());
    writeSnapshot(*f.editor, "editor_presetbar_prompt_error.png");
    f.bar().cancel();
    checkNoOverlap(f.bar());

    // Wide enough, the cycle readout comes back without overlapping.
    f.editor->setSize(1100, 480);
    checkNoOverlap(*f.editor);
    bool cycleShown = false;
    for (auto* c : f.editor->getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(c); label != nullptr && label->isVisible())
            cycleShown = cycleShown || label->getText().startsWith("Zyklus");
    CHECK(cycleShown);
}

namespace {
juce::StringArray itemTexts(const juce::PopupMenu& menu)
{
    juce::StringArray out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        out.add(it.getItem().text);
    return out;
}
} // namespace

TEST_CASE("PresetBar: the more menu offers the bank actions", "[PresetBar]")
{
    BarFixture f;
    f.open();
    CHECK(itemTexts(f.bar().moreMenu())
          == juce::StringArray { juce::String::fromUTF8("Neue Bank\xe2\x80\xa6"), juce::String::fromUTF8("Bank exportieren\xe2\x80\xa6"),
                                 juce::String::fromUTF8("Bank importieren\xe2\x80\xa6"),
                                 juce::String::fromUTF8("Preset-Ordner \xc3\xb6" "ffnen") });
}

TEST_CASE("PresetBar: SAVE overwrites the active preset of the selected bank", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.savePreset("A", "Zwei", { 0.75 });
    f.open();
    f.bar().requestLoad("Eins");
    f.proc.waitForLoads();
    REQUIRE(f.proc.getPluginState().activePreset == "Eins");
    const auto zwei = f.storedFingerprint("A", "Zwei");

    f.editLoop1({ 0.25 });
    REQUIRE(f.bar().hasUnsavedChanges());
    f.bar().requestSave();
    CHECK(f.bar().mode() == PresetBar::Mode::Normal); // no name field
    CHECK(f.bar().message().isEmpty());
    CHECK(f.bar().presetNames() == juce::StringArray { "Eins", "Zwei" });
    CHECK(f.storedFingerprint("A", "Eins") == f.fingerprint());
    CHECK(f.storedFingerprint("A", "Zwei") == zwei);
    CHECK(f.proc.getPluginState().activePreset == "Eins");
    CHECK_FALSE(f.bar().hasUnsavedChanges());

    // A second save changes the preset again; the count stays.
    f.editLoop1({ 0.125, 0.5 });
    f.bar().requestSave();
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().presetNames() == juce::StringArray { "Eins", "Zwei" });
    CHECK(f.storedFingerprint("A", "Eins") == f.fingerprint());
}

TEST_CASE("PresetBar: SAVE AS asks for a name and stores a variant, which becomes active", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.open();
    f.bar().requestLoad("Eins");
    f.proc.waitForLoads();
    const auto eins = f.storedFingerprint("A", "Eins");
    f.editLoop1({ 0.25 });

    f.bar().requestSaveAs();
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(f.bar().presetNames() == juce::StringArray { "Eins" }); // nothing written yet

    f.bar().submitName("  Variante  ");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().isEmpty());
    CHECK(f.bar().presetNames() == juce::StringArray { "Eins", "Variante" });
    CHECK(f.storedFingerprint("A", "Variante") == f.fingerprint());
    CHECK(f.storedFingerprint("A", "Eins") == eins);
    CHECK(f.proc.getPluginState().activeBank == "A");
    CHECK(f.proc.getPluginState().activePreset == "Variante");
    CHECK(f.bar().activePresetName() == "Variante");
    CHECK(ticked(f.bar().presetMenu()) == juce::StringArray { "Variante" });
    CHECK_FALSE(f.bar().hasUnsavedChanges());
}

TEST_CASE("PresetBar: right after SAVE AS or SAVE with hand-placed boundaries nothing is unsaved", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.open();
    f.bar().requestLoad("Eins");
    f.proc.waitForLoads();

    // x / width as the lane computes it; these need all 17 significant digits.
    const std::vector<double> awkward { 0.012345678901234568, 137.0 / 700.0, 0.30000000000000004, 2.0 / 3.0 };
    f.editLoop1(awkward);
    REQUIRE(f.proc.getPluginState().loop1.boundaries.size() == awkward.size()); // applyEdit may round the last bit

    f.bar().requestSaveAs();
    f.bar().submitName("Krumm");
    REQUIRE(f.bar().mode() == PresetBar::Mode::Normal);
    REQUIRE(f.bar().message().isEmpty());
    CHECK_FALSE(f.bar().hasUnsavedChanges());

    f.editLoop1({ 1.0 / 700.0 * 9.0, 691.0 / 700.0 });
    REQUIRE(f.bar().hasUnsavedChanges());
    f.bar().requestSave();
    REQUIRE(f.bar().mode() == PresetBar::Mode::Normal);
    REQUIRE(f.bar().message().isEmpty());
    CHECK(f.proc.getPluginState().activePreset == "Krumm");
    CHECK_FALSE(f.bar().hasUnsavedChanges());

    // Picking another preset loads it right away, without the discard question.
    f.bar().requestLoad("Eins");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.proc.getPluginState().activePreset == "Eins");
}

TEST_CASE("PresetBar: SAVE without an active preset asks for a name", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.savePreset("B", "Zwei", {});
    LoopPair loops(f.proc);
    f.open();
    REQUIRE(f.bar().selectedBank() == "A");

    f.bar().requestSave();
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    f.bar().cancel();

    // The active preset lies in another bank than the selected one.
    f.bar().requestLoad("Eins");
    f.bar().confirmDiscard(true);
    f.proc.waitForLoads();
    REQUIRE(f.proc.getPluginState().activePreset == "Eins");
    const auto eins = f.storedFingerprint("A", "Eins");
    f.bar().selectBank("B");
    f.bar().requestSave();
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    f.bar().submitName("Neu");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().presetNames() == juce::StringArray { "Zwei", "Neu" });
    CHECK(f.storedFingerprint("A", "Eins") == eins);
    CHECK(f.proc.getPluginState().activeBank == "B");
    CHECK(f.proc.getPluginState().activePreset == "Neu");
}

TEST_CASE("PresetBar: SAVE asks for a name when the active preset is gone from its bank", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    f.savePreset("A", "Zwei", { 0.25 });
    f.open();
    f.bar().requestLoad("Eins");
    f.proc.waitForLoads();
    REQUIRE(f.proc.getPluginState().activePreset == "Eins");

    // Someone removes "Eins" from bank.json on disk.
    const auto zwei = BankStore(f.root).readBank("A").bank->presets[1];
    Bank edited;
    edited.presets.push_back(zwei);
    const auto bankFile = f.root.getChildFile("A").getChildFile(BankStore::kBankFileName);
    REQUIRE(bankFile.replaceWithText(bankToJson(edited)));
    const auto before = bankFile.loadFileAsString();

    f.bar().requestSave();
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(bankFile.loadFileAsString() == before);
    f.bar().submitName("Eins");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().presetNames() == juce::StringArray { "Zwei", "Eins" });
}

TEST_CASE("PresetBar: a taken name keeps the prompt open with a message and does not overwrite", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    LoopPair loops(f.proc);
    f.editLoop1({ 0.25 });
    f.open();
    const auto before = f.bankText("A");

    f.bar().requestSaveAs();
    f.bar().submitName("eins");
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(f.bar().message().contains("gibt es schon"));
    CHECK(f.bankText("A") == before);
    CHECK(f.proc.getPluginState().activePreset.isEmpty());

    f.bar().submitName("   ");
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(f.bar().message().contains("leer"));
    CHECK(f.bankText("A") == before);

    f.bar().submitName("Zwei");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().isEmpty());
    CHECK(f.bar().presetNames() == juce::StringArray { "Eins", "Zwei" });
}

TEST_CASE("PresetBar: cancel leaves the bank and the state untouched", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    LoopPair loops(f.proc);
    f.open();
    const auto bank = f.bankText("A");
    const auto state = f.fingerprint();

    f.bar().requestSaveAs();
    REQUIRE(f.bar().mode() == PresetBar::Mode::NamePrompt);
    f.bar().cancel();
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().isEmpty());
    CHECK(f.bankText("A") == bank);
    CHECK(f.fingerprint() == state);
    CHECK(f.proc.getPluginState().activePreset.isEmpty());

    f.bar().submitName("Spaet"); // no prompt open: ignored
    CHECK(f.bankText("A") == bank);
    CHECK(f.bar().presetNames() == juce::StringArray { "Eins" });
}

TEST_CASE("PresetBar: on a fresh install SAVE also asks for a bank name and creates the bank", "[PresetBar]")
{
    BarFixture f;
    LoopPair loops(f.proc);
    f.open();
    REQUIRE(f.bar().bankNames().isEmpty());

    f.bar().requestSave();
    REQUIRE(f.bar().mode() == PresetBar::Mode::NamePrompt);
    checkNoOverlap(f.bar());
    checkButtonTextFits(f.bar());
    writeSnapshot(*f.editor, "editor_presetbar_prompt_bank.png");

    f.bar().submitName("Erstes", "con"); // reserved Windows name
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(f.bar().message().contains("reserviert"));
    CHECK(BankStore(f.root).listBanks().isEmpty());

    f.bar().submitName("", "Live"); // a bad preset name creates no bank either
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(f.bar().message().contains("leer"));
    CHECK(BankStore(f.root).listBanks().isEmpty());

    f.bar().submitName("Erstes", "Live");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().isEmpty());
    CHECK(f.bar().bankNames() == juce::StringArray { "Live" });
    CHECK(f.bar().selectedBank() == "Live");
    CHECK(f.bar().presetNames() == juce::StringArray { "Erstes" });
    CHECK(f.proc.getPluginState().activeBank == "Live");
    CHECK(f.proc.getPluginState().activePreset == "Erstes");
    CHECK(f.storedFingerprint("Live", "Erstes") == f.fingerprint());
}

TEST_CASE("PresetBar: saving with a Missing or Failed slot is refused with a message", "[PresetBar]")
{
    using Status = DubcyclingProcessor::SlotStatus;
    {
        // An empty slot is saved as empty.
        BarFixture f;
        f.open();
        f.bar().requestSaveAs();
        f.bar().submitName("Leer", "Bank");
        CHECK(f.bar().presetNames() == juce::StringArray { "Leer" });
    }

    BarFixture f;
    f.savePreset("A", "Eins", {});
    LoopPair loops(f.proc);
    f.open();
    const auto bank = f.bankText("A");

    // Failed: the file exists but does not decode (BankStore alone would save it).
    const auto bad = loops.dir.getChildFile("bad.wav");
    REQUIRE(bad.replaceWithText("not audio"));
    f.bar().requestSaveAs();
    REQUIRE(f.bar().mode() == PresetBar::Mode::NamePrompt);
    REQUIRE(f.proc.loadLoop(1, bad));
    f.proc.waitForLoads();
    REQUIRE(f.proc.getSlotStatus(1) == Status::Failed);
    f.bar().submitName("Kaputt"); // the slot failed while the prompt was open
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(f.bar().message().contains("Loop 2"));
    CHECK(f.bankText("A") == bank);
    f.bar().cancel();

    f.bar().requestSaveAs(); // refused before asking for a name
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().contains("Loop 2"));
    f.proc.setActivePreset("A", "Eins");
    f.bar().requestSave();
    CHECK(f.bar().message().contains("Loop 2"));
    CHECK(f.bankText("A") == bank);

    // Missing: the file of Loop 1 is gone.
    auto s = f.proc.getPluginState();
    s.loop1.path = loops.dir.getChildFile("gone.wav").getFullPathName();
    s.loop1.fileName = "gone.wav";
    s.loop2 = f.proc.getPluginState().loop1;
    f.proc.restoreState(s);
    f.proc.waitForLoads();
    REQUIRE(f.proc.getSlotStatus(0) == Status::Missing);
    REQUIRE(f.proc.getSlotStatus(1) == Status::Loaded);
    f.bar().requestSaveAs();
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().contains("Loop 1"));
    CHECK(f.bankText("A") == bank);
}

TEST_CASE("PresetBar: createBank adds a bank and a bad name shows a message", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", {});
    f.open();

    f.bar().createBank("Neu");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().message().isEmpty());
    CHECK(f.bar().bankNames() == juce::StringArray { "A", "Neu" });
    CHECK(f.bar().selectedBank() == "Neu");
    CHECK(f.bar().presetNames().isEmpty());

    f.bar().createBank("a"); // taken, case-insensitive
    CHECK(f.bar().message().contains("gibt es schon"));
    CHECK(f.bar().bankNames() == juce::StringArray { "A", "Neu" });
    f.bar().createBank("x/y");
    CHECK(f.bar().message().contains("Zeichen"));
    CHECK(BankStore(f.root).listBanks() == juce::StringArray { "A", "Neu" });

    // Through the menu entry: the name field stays open on a bad name.
    f.bar().requestNewBank();
    REQUIRE(f.bar().mode() == PresetBar::Mode::NamePrompt);
    f.bar().createBank("");
    CHECK(f.bar().mode() == PresetBar::Mode::NamePrompt);
    CHECK(f.bar().message().contains("leer"));
    f.bar().createBank("Dritte");
    CHECK(f.bar().mode() == PresetBar::Mode::Normal);
    CHECK(f.bar().selectedBank() == "Dritte");
}

TEST_CASE("PresetBar: exportBankTo and importBankFrom round trip through the bar", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("Live", "Eins", { 0.5 });
    f.open();
    REQUIRE(f.bar().selectedBank() == "Live");
    const auto eins = f.storedFingerprint("Live", "Eins");

    const auto zip = f.outDir.getChildFile("Live.zip");
    f.bar().exportBankTo(zip);
    CHECK(zip.existsAsFile());
    CHECK(f.bar().message().contains("Live.zip"));

    f.bar().importBankFrom(zip);
    CHECK(f.bar().bankNames() == juce::StringArray { "Live", "Live (2)" });
    CHECK(f.bar().selectedBank() == "Live (2)");
    CHECK(f.bar().presetNames() == juce::StringArray { "Eins" });
    CHECK(f.bar().message().contains("Live (2)"));
    CHECK(f.storedFingerprint("Live (2)", "Eins") == eins);

    // Failures: a message, nothing added.
    const auto junk = f.outDir.getChildFile("junk.zip");
    REQUIRE(junk.replaceWithText("not a zip"));
    f.bar().importBankFrom(junk);
    CHECK(f.bar().message().isNotEmpty());
    CHECK_FALSE(f.bar().message().contains("Importiert"));
    CHECK(f.bar().bankNames() == juce::StringArray { "Live", "Live (2)" });

    f.bar().selectBank("Live");
    for (auto& sample : f.root.getChildFile("Live").getChildFile("samples").findChildFiles(juce::File::findFiles, false))
        REQUIRE(sample.deleteFile());
    const auto broken = f.outDir.getChildFile("Broken.zip");
    f.bar().exportBankTo(broken);
    CHECK(f.bar().message().contains("loop.wav"));
    CHECK_FALSE(broken.exists());
}

TEST_CASE("PresetBar: save and load keep the host program list unchanged", "[PresetBar]")
{
    BarFixture f;
    f.savePreset("A", "Eins", { 0.5 });
    LoopPair loops(f.proc);
    f.open();
    auto check = [&] {
        CHECK(f.proc.getNumPrograms() == 1);
        CHECK(f.proc.getCurrentProgram() == 0);
        CHECK(f.proc.getProgramName(0) == "Default");
    };
    check();
    f.bar().requestSaveAs();
    f.bar().submitName("Zwei");
    REQUIRE(f.proc.getPluginState().activePreset == "Zwei");
    check();
    f.bar().requestLoad("Eins");
    f.proc.waitForLoads();
    REQUIRE(f.proc.getPluginState().activePreset == "Eins");
    check();
}
