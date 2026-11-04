#include "plugin/ui/PresetBar.h"
#include <algorithm>
#include <utility>
#include "plugin/presets/BankFormat.h"
#include "plugin/ui/PanelLook.h"

namespace dc::ui {

namespace {
using namespace panel;

constexpr juce::uint32 kListMaxAgeMs = 3000; // refresh() re-reads the lists at most this often
constexpr juce::uint32 kMessageShowMs = 5000;
constexpr int kBankCaptionW = 34;
constexpr int kPresetCaptionW = 48;
constexpr int kCaptionGap = 4;
constexpr int kGroupGap = 10;
constexpr int kFieldMinW = 70;
constexpr int kFieldMaxW = 150;
constexpr int kButtonRoomW = 150; // right of the PRESET field: room for the save buttons
constexpr int kConfirmLoadW = 56;
constexpr int kConfirmCancelW = 84;

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(mono(10.0f));
    label.setColour(juce::Label::textColourId, kDim);
    label.setJustificationType(juce::Justification::centredRight);
    label.setBorderSize(juce::BorderSize<int>(0));
}
} // namespace

PresetBar::Field::Field() : juce::Button({}) {}

void PresetBar::Field::setDisplay(const juce::String& shown, juce::Colour colour)
{
    if (shown == text_ && colour == colour_)
        return;
    text_ = shown;
    colour_ = colour;
    repaint();
}

void PresetBar::Field::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(kLcd);
    g.fillRect(r);
    g.setColour(highlighted || down ? kEdgeLight : kEdge);
    g.drawRect(r, 1.0f);

    auto area = getLocalBounds().reduced(6, 0);
    const auto arrow = area.removeFromRight(10).toFloat();
    const float cx = arrow.getCentreX();
    const float cy = arrow.getCentreY();
    juce::Path tri;
    tri.addTriangle(cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 2.5f);
    g.setColour(highlighted ? kText : kDim);
    g.fillPath(tri);

    g.setColour(colour_);
    g.setFont(mono(11.0f));
    g.drawText(text_, area.withTrimmedRight(4), juce::Justification::centredLeft, true);
}

PresetBar::PresetBar(DubcyclingProcessor& proc, BankStore& store) : proc_(proc), store_(store)
{
    styleCaption(bankCaption_, "BANK");
    styleCaption(presetCaption_, "PRESET");
    addAndMakeVisible(bankCaption_);
    addAndMakeVisible(presetCaption_);

    bankField_.setTooltip("Bank w" + utf8("\xc3\xa4") + "hlen (l" + utf8("\xc3\xa4") + "dt nichts)");
    bankField_.onClick = [this] { openBankMenu(); };
    presetField_.onClick = [this] { openPresetMenu(); };
    addAndMakeVisible(bankField_);
    addAndMakeVisible(presetField_);

    confirmText_.setText(utf8("Ungespeicherte \xc3\x84nderungen verwerfen?"), juce::dontSendNotification);
    confirmText_.setFont(mono(10.0f));
    confirmText_.setMinimumHorizontalScale(0.6f);
    confirmText_.setColour(juce::Label::textColourId, kWarn);
    confirmText_.setJustificationType(juce::Justification::centredLeft);
    confirmLoad_.setButtonText("LADEN");
    confirmLoad_.onClick = [this] { confirmDiscard(true); };
    confirmCancel_.setButtonText("ABBRECHEN");
    confirmCancel_.onClick = [this] { confirmDiscard(false); };
    addChildComponent(confirmText_);
    addChildComponent(confirmLoad_);
    addChildComponent(confirmCancel_);

    reloadLists();
    updateView();
}

void PresetBar::refresh()
{
    if (juce::Time::getMillisecondCounter() - listsReadAt_ >= kListMaxAgeMs)
        reloadLists();
    updateView();
}

void PresetBar::reloadLists()
{
    banks_ = store_.listBanks();
    if (const int i = banks_.indexOf(selectedBank_, true); i >= 0)
        selectedBank_ = banks_[i];
    else if (const auto active = validActive(banks_); active.bank.isNotEmpty())
        selectedBank_ = active.bank;
    else
        selectedBank_ = banks_.isEmpty() ? juce::String() : banks_[0];
    reloadPresets();
    listsReadAt_ = juce::Time::getMillisecondCounter();
}

void PresetBar::reloadPresets()
{
    presets_.clear();
    if (selectedBank_.isEmpty())
        return;
    const auto parsed = store_.readBank(selectedBank_);
    if (const auto r = BankStore::checkParsed(store_.bankDir(selectedBank_).getChildFile("bank.json"), parsed); !r.ok())
    {
        setMessage(r.message);
        return;
    }
    for (const auto& p : parsed.bank->presets)
        presets_.add(p.name);
}

void PresetBar::selectBank(const juce::String& bank)
{
    banks_ = store_.listBanks();
    const int i = banks_.indexOf(bank, true);
    if (i < 0)
        return;
    selectedBank_ = banks_[i];
    setMessage({});
    reloadPresets();
    listsReadAt_ = juce::Time::getMillisecondCounter();
    updateView();
}

PresetBar::ActivePreset PresetBar::validActive(const juce::StringArray& banks) const
{
    const auto st = proc_.getPluginState();
    if (st.activeBank.isEmpty() || st.activePreset.isEmpty() || bankNameProblem(st.activeBank, {}).isNotEmpty())
        return {};
    const int i = banks.indexOf(st.activeBank, true);
    if (i < 0)
        return {};
    return { banks[i], st.activePreset };
}

juce::String PresetBar::activePresetName() const
{
    const auto active = validActive(banks_);
    return active.bank.isNotEmpty() && active.bank == selectedBank_ ? active.preset : juce::String();
}

bool PresetBar::hasUnsavedChanges() const
{
    PluginState reference;
    if (const auto active = validActive(store_.listBanks()); active.bank.isNotEmpty())
        if (const auto parsed = store_.readBank(active.bank); parsed.bank)
            for (const auto& p : parsed.bank->presets)
                if (p.name.equalsIgnoreCase(active.preset))
                {
                    reference = p.state;
                    break;
                }
    return presetFingerprint(proc_.getPluginState()) != presetFingerprint(reference);
}

void PresetBar::requestLoad(const juce::String& preset)
{
    if (mode_ != Mode::Normal)
        return;
    const auto loaded = store_.loadPreset(selectedBank_, preset);
    if (!loaded.result.ok())
    {
        setMessage(loaded.result.message);
        updateView();
        return;
    }
    if (hasUnsavedChanges())
    {
        pending_ = { selectedBank_, preset, presetFingerprint(proc_.getPluginState()) };
        mode_ = Mode::ConfirmDiscard;
        setMessage({});
        updateView();
        return;
    }
    apply(loaded);
}

void PresetBar::confirmDiscard(bool load)
{
    if (mode_ != Mode::ConfirmDiscard)
        return;
    mode_ = Mode::Normal;
    const auto asked = std::exchange(pending_, {});
    if (!load)
    {
        updateView();
        return;
    }
    // The answer only covers the state the user was asked about.
    if (presetFingerprint(proc_.getPluginState()) != asked.fingerprint)
    {
        setMessage("Nicht geladen: der Zustand wurde inzwischen ge" + utf8("\xc3\xa4") + "ndert");
        updateView();
        return;
    }
    const auto loaded = store_.loadPreset(asked.bank, asked.preset); // the bank may have changed meanwhile
    if (!loaded.result.ok())
    {
        setMessage(loaded.result.message);
        updateView();
        return;
    }
    apply(loaded);
}

void PresetBar::apply(const BankStore::LoadedPreset& loaded)
{
    proc_.restoreState(loaded.state); // sets activeBank / activePreset too
    selectedBank_ = loaded.state.activeBank;
    setMessage({});
    reloadLists();
    updateView();
}

void PresetBar::setMessage(const juce::String& text)
{
    message_ = text;
    messageUntil_ = text.isEmpty() ? 0 : juce::Time::getMillisecondCounter() + kMessageShowMs;
}

juce::PopupMenu PresetBar::bankMenu() const
{
    juce::PopupMenu menu;
    if (banks_.isEmpty())
        menu.addItem(1, "Keine Bank", false);
    for (int i = 0; i < banks_.size(); ++i)
        menu.addItem(i + 1, banks_[i], true, banks_[i] == selectedBank_);
    return menu;
}

juce::PopupMenu PresetBar::presetMenu() const
{
    juce::PopupMenu menu;
    if (presets_.isEmpty())
        menu.addItem(1, "Keine Presets", false);
    const auto active = activePresetName();
    for (int i = 0; i < presets_.size(); ++i)
        menu.addItem(i + 1, presets_[i], true, active.isNotEmpty() && presets_[i].equalsIgnoreCase(active));
    return menu;
}

void PresetBar::openBankMenu()
{
    reloadLists();
    updateView();
    juce::Component::SafePointer<PresetBar> safe(this);
    bankMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&bankField_),
                             [safe, names = banks_](int id) {
                                 if (safe != nullptr && id > 0 && id <= names.size())
                                     safe->selectBank(names[id - 1]);
                             });
}

void PresetBar::openPresetMenu()
{
    reloadLists();
    updateView();
    juce::Component::SafePointer<PresetBar> safe(this);
    presetMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetField_),
                               [safe, names = presets_](int id) {
                                   if (safe != nullptr && id > 0 && id <= names.size())
                                       safe->requestLoad(names[id - 1]);
                               });
}

void PresetBar::updateView()
{
    bankField_.setDisplay(selectedBank_.isEmpty() ? juce::String("Keine Bank") : selectedBank_,
                          selectedBank_.isEmpty() ? kDim : kAmber);

    const bool showMessage = messageUntil_ != 0 && juce::Time::getMillisecondCounter() < messageUntil_;
    if (!showMessage)
        messageUntil_ = 0;
    const auto active = activePresetName();
    if (showMessage)
        presetField_.setDisplay(message_, kWarn);
    else
        presetField_.setDisplay(active.isEmpty() ? juce::String("Kein Preset") : active, active.isEmpty() ? kDim : kAmber);
    presetField_.setTooltip(showMessage ? message_ : juce::String("Preset laden"));

    const bool confirm = mode_ == Mode::ConfirmDiscard;
    presetCaption_.setVisible(!confirm);
    presetField_.setVisible(!confirm);
    confirmText_.setVisible(confirm);
    confirmLoad_.setVisible(confirm);
    confirmCancel_.setVisible(confirm);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    const int fixed = kBankCaptionW + kCaptionGap + kGroupGap + kPresetCaptionW + kCaptionGap + kButtonRoomW;
    const int fieldW = std::clamp((area.getWidth() - fixed) / 2, kFieldMinW, kFieldMaxW);

    bankCaption_.setBounds(area.removeFromLeft(kBankCaptionW));
    area.removeFromLeft(kCaptionGap);
    bankField_.setBounds(area.removeFromLeft(fieldW));
    area.removeFromLeft(kGroupGap);

    // ConfirmDiscard: the question and its buttons take the place of the PRESET field.
    auto confirm = area;
    confirmCancel_.setBounds(confirm.removeFromRight(std::min(kConfirmCancelW, confirm.getWidth())));
    confirm.removeFromRight(4);
    confirmLoad_.setBounds(confirm.removeFromRight(std::min(kConfirmLoadW, std::max(confirm.getWidth(), 0))));
    confirm.removeFromRight(6);
    confirmText_.setBounds(confirm.withWidth(std::max(confirm.getWidth(), 0)));

    presetCaption_.setBounds(area.removeFromLeft(kPresetCaptionW));
    area.removeFromLeft(kCaptionGap);
    presetField_.setBounds(area.removeFromLeft(std::min(fieldW, area.getWidth())));
}

} // namespace dc::ui
