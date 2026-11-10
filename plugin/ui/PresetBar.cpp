#include "plugin/ui/PresetBar.h"
#include <algorithm>
#include <utility>
#include "plugin/presets/BankArchive.h"
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
constexpr int kButtonGap = 4;
constexpr int kSaveW = 46;
constexpr int kSaveAsW = 62;
constexpr int kMoreW = 26;
constexpr int kButtonRoomW = 6 + kSaveW + kButtonGap + kSaveAsW + kButtonGap + kMoreW; // right of the PRESET field
constexpr int kFixedW = kBankCaptionW + kCaptionGap + kGroupGap + kPresetCaptionW + kCaptionGap + kButtonRoomW;
static_assert(PresetBar::kPreferredWidth == kFixedW + 2 * kFieldMaxW, "kPreferredWidth must follow the layout");
constexpr int kConfirmLoadW = 56;
constexpr int kCancelW = 84; // CANCEL, in the confirm question and the name prompt
constexpr int kPromptOkW = 36;
constexpr int kPromptMessageMinW = 110; // room for an error next to the name field

juce::String utf8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

void styleCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(mono(10.0f));
    label.setColour(juce::Label::textColourId, kDim);
    label.setJustificationType(juce::Justification::centredRight);
    label.setBorderSize(juce::BorderSize<int>(0));
}

void styleNameEditor(juce::TextEditor& editor)
{
    editor.setFont(mono(11.0f));
    editor.setIndents(6, 0);
    editor.setJustification(juce::Justification::centredLeft);
    editor.setColour(juce::TextEditor::backgroundColourId, kLcd);
    editor.setColour(juce::TextEditor::textColourId, kAmber);
    editor.setColour(juce::TextEditor::outlineColourId, kEdge);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, kEdgeLight);
    editor.setColour(juce::TextEditor::highlightColourId, kAccent.withAlpha(0.4f));
    editor.setColour(juce::CaretComponent::caretColourId, kAmber);
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

PresetBar::PresetBar(DubcyclingProcessor& proc, BankStore& store) : proc_(proc), store_(store), messageColour_(kWarn)
{
    styleCaption(bankCaption_, "BANK");
    styleCaption(presetCaption_, "PRESET");
    addAndMakeVisible(bankCaption_);
    addAndMakeVisible(presetCaption_);

    bankField_.setTooltip("Choose bank (loads nothing)");
    bankField_.onClick = [this] { openBankMenu(); };
    presetField_.onClick = [this] { openPresetMenu(); };
    addAndMakeVisible(bankField_);
    addAndMakeVisible(presetField_);

    save_.setButtonText("SAVE");
    save_.setTooltip("Overwrite the active preset (without an active preset: enter a name)");
    save_.setColour(juce::TextButton::buttonColourId, kAccent);
    save_.setColour(juce::TextButton::textColourOffId, kWindow);
    save_.onClick = [this] { requestSave(); };
    saveAs_.setButtonText("SAVE AS");
    saveAs_.setTooltip("Save as a new preset");
    saveAs_.onClick = [this] { requestSaveAs(); };
    more_.setButtonText(utf8("\xe2\x8b\xaf"));
    more_.setTooltip("Banks: new, export, import, open folder");
    more_.onClick = [this] { openMoreMenu(); };
    addAndMakeVisible(save_);
    addAndMakeVisible(saveAs_);
    addAndMakeVisible(more_);

    confirmText_.setText("Discard unsaved changes?", juce::dontSendNotification);
    confirmText_.setFont(mono(10.0f));
    confirmText_.setMinimumHorizontalScale(0.6f);
    confirmText_.setColour(juce::Label::textColourId, kWarn);
    confirmText_.setJustificationType(juce::Justification::centredLeft);
    confirmLoad_.setButtonText("LOAD");
    confirmLoad_.onClick = [this] { confirmDiscard(true); };
    confirmCancel_.setButtonText("CANCEL");
    confirmCancel_.onClick = [this] { confirmDiscard(false); };
    addChildComponent(confirmText_);
    addChildComponent(confirmLoad_);
    addChildComponent(confirmCancel_);

    for (auto* editor : { &bankNameEditor_, &nameEditor_ })
    {
        styleNameEditor(*editor);
        editor->onReturnKey = [this] { confirmPrompt(); };
        editor->onEscapeKey = [this] { cancel(); };
        addChildComponent(*editor);
    }
    bankNameEditor_.setInputRestrictions(kMaxBankNameLength);
    bankNameEditor_.setTextToShowWhenEmpty("Bank name", kDim);
    promptMessage_.setFont(mono(10.0f));
    promptMessage_.setMinimumHorizontalScale(0.6f);
    promptMessage_.setJustificationType(juce::Justification::centredLeft);
    promptMessage_.setBorderSize(juce::BorderSize<int>(0));
    promptOk_.setButtonText("OK");
    promptOk_.onClick = [this] { confirmPrompt(); };
    promptCancel_.setButtonText("CANCEL");
    promptCancel_.onClick = [this] { cancel(); };
    addChildComponent(promptMessage_);
    addChildComponent(promptOk_);
    addChildComponent(promptCancel_);

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
    if (const auto r = BankStore::checkParsed(store_.bankDir(selectedBank_).getChildFile(BankStore::kBankFileName), parsed);
        !r.ok())
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
        refuse(loaded.result.message);
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
        refuse("Not loaded: the state changed in the meantime");
        return;
    }
    const auto loaded = store_.loadPreset(asked.bank, asked.preset); // the bank may have changed meanwhile
    if (!loaded.result.ok())
    {
        refuse(loaded.result.message);
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

juce::String PresetBar::slotProblem() const
{
    const auto st = proc_.getPluginState();
    for (int i = 0; i < 2; ++i)
    {
        const auto& name = (i == 0 ? st.loop1 : st.loop2).fileName;
        const auto loop = "Not saved: Loop " + juce::String(i + 1);
        switch (proc_.getSlotStatus(i))
        {
            case DubcyclingProcessor::SlotStatus::Missing: return loop + " missing (" + name + ")";
            case DubcyclingProcessor::SlotStatus::Failed:  return loop + " unreadable (" + name + ")";
            case DubcyclingProcessor::SlotStatus::Empty:
            case DubcyclingProcessor::SlotStatus::Loading:
            case DubcyclingProcessor::SlotStatus::Loaded:  break;
        }
    }
    return {};
}

void PresetBar::requestSave()
{
    if (mode_ != Mode::Normal)
        return;
    reloadLists();
    if (const auto problem = slotProblem(); problem.isNotEmpty())
    {
        refuse(problem);
        return;
    }
    // Overwrite only a preset that is still stored in the selected bank (Spec 6); else like SAVE AS.
    if (const auto active = activePresetName(); active.isNotEmpty())
    {
        const auto parsed = store_.readBank(selectedBank_);
        if (const auto r = BankStore::checkParsed(store_.bankDir(selectedBank_).getChildFile(BankStore::kBankFileName), parsed);
            !r.ok())
        {
            refuse(r.message);
            return;
        }
        for (const auto& p : parsed.bank->presets)
            if (p.name.equalsIgnoreCase(active))
            {
                save(selectedBank_, p.name, true);
                return;
            }
    }
    openPrompt(banks_.isEmpty() ? Prompt::PresetAndBank : Prompt::Preset);
}

void PresetBar::requestSaveAs()
{
    if (mode_ != Mode::Normal)
        return;
    reloadLists();
    if (const auto problem = slotProblem(); problem.isNotEmpty())
    {
        refuse(problem);
        return;
    }
    openPrompt(banks_.isEmpty() ? Prompt::PresetAndBank : Prompt::Preset);
}

void PresetBar::requestNewBank()
{
    if (mode_ != Mode::Normal)
        return;
    openPrompt(Prompt::NewBank);
}

void PresetBar::openPrompt(Prompt prompt)
{
    mode_ = Mode::NamePrompt;
    prompt_ = prompt;
    setMessage({});
    bankNameEditor_.clear();
    nameEditor_.clear();
    const bool forBank = prompt == Prompt::NewBank;
    nameEditor_.setInputRestrictions(forBank ? kMaxBankNameLength : kMaxPresetNameLength);
    nameEditor_.setTextToShowWhenEmpty(forBank ? "New bank" : "Preset name", kDim);
    updateView();
    auto& first = prompt == Prompt::PresetAndBank ? bankNameEditor_ : nameEditor_;
    if (first.isShowing())
        first.grabKeyboardFocus();
}

void PresetBar::confirmPrompt()
{
    if (mode_ != Mode::NamePrompt)
        return;
    if (prompt_ == Prompt::NewBank)
        createBank(nameEditor_.getText().trim());
    else
        submitName(nameEditor_.getText(), bankNameEditor_.getText().trim());
}

void PresetBar::submitName(const juce::String& presetName, const juce::String& bankName)
{
    if (mode_ != Mode::NamePrompt || prompt_ == Prompt::NewBank)
        return;
    const bool newBank = prompt_ == Prompt::PresetAndBank;
    const auto bank = newBank ? bankName : selectedBank_;
    if (newBank)
        if (const auto problem = bankNameProblem(bankName, store_.listBanks()); problem.isNotEmpty())
        {
            refuse(problem);
            return;
        }
    Bank stored; // for the taken-name check; a damaged bank is reported by savePreset
    if (!newBank)
        if (auto parsed = store_.readBank(bank); parsed.bank)
            stored = std::move(*parsed.bank);
    if (const auto problem = presetNameProblem(presetName, stored); problem.isNotEmpty())
    {
        refuse(problem);
        return;
    }
    if (const auto problem = slotProblem(); problem.isNotEmpty())
    {
        refuse(problem);
        return;
    }
    if (newBank)
    {
        if (const auto r = store_.createBank(bank); !r.ok())
        {
            refuse(r.message);
            return;
        }
        prompt_ = Prompt::Preset; // the bank exists now; a failed save below asks only for the name again
        selectedBank_ = bank;
        reloadLists();
    }
    save(bank, presetName, false);
}

void PresetBar::save(const juce::String& bank, const juce::String& name, bool overwrite)
{
    if (const auto r = store_.savePreset(bank, name, proc_.getPluginState(), overwrite); !r.ok())
    {
        refuse(r.message);
        return;
    }
    proc_.setActivePreset(bank, sanitizeName(name, kMaxPresetNameLength));
    mode_ = Mode::Normal;
    selectedBank_ = bank;
    setMessage({});
    reloadLists();
    updateView();
}

void PresetBar::cancel()
{
    if (mode_ != Mode::NamePrompt)
        return;
    mode_ = Mode::Normal;
    setMessage({});
    updateView();
}

void PresetBar::createBank(const juce::String& name)
{
    if (mode_ == Mode::ConfirmDiscard || (mode_ == Mode::NamePrompt && prompt_ != Prompt::NewBank))
        return;
    if (const auto r = store_.createBank(name); !r.ok())
    {
        refuse(r.message);
        return;
    }
    mode_ = Mode::Normal;
    selectBank(name); // re-reads the banks, clears the message
}

void PresetBar::exportBankTo(const juce::File& zip)
{
    if (selectedBank_.isEmpty())
    {
        refuse("No bank selected");
        return;
    }
    if (const auto r = exportBank(store_, selectedBank_, zip); !r.ok())
    {
        refuse(r.message);
        return;
    }
    setHint("Exported: " + zip.getFileName());
    updateView();
}

void PresetBar::importBankFrom(const juce::File& zip)
{
    juce::String imported;
    if (const auto r = importBank(store_, zip, imported); !r.ok())
    {
        refuse(r.message);
        return;
    }
    selectBank(imported);
    setHint("Imported: " + imported);
    updateView();
}

void PresetBar::setMessage(const juce::String& text)
{
    message_ = text;
    messageColour_ = kWarn;
    messageUntil_ = text.isEmpty() ? 0 : juce::Time::getMillisecondCounter() + kMessageShowMs;
}

void PresetBar::setHint(const juce::String& text)
{
    setMessage(text);
    messageColour_ = kGreen;
}

void PresetBar::refuse(const juce::String& text)
{
    setMessage(text);
    updateView();
}

juce::PopupMenu PresetBar::bankMenu() const
{
    juce::PopupMenu menu;
    if (banks_.isEmpty())
        menu.addItem(1, "No bank", false);
    for (int i = 0; i < banks_.size(); ++i)
        menu.addItem(i + 1, banks_[i], true, banks_[i] == selectedBank_);
    return menu;
}

juce::PopupMenu PresetBar::presetMenu() const
{
    juce::PopupMenu menu;
    if (presets_.isEmpty())
        menu.addItem(1, "No presets", false);
    const auto active = activePresetName();
    for (int i = 0; i < presets_.size(); ++i)
        menu.addItem(i + 1, presets_[i], true, active.isNotEmpty() && presets_[i].equalsIgnoreCase(active));
    return menu;
}

juce::PopupMenu PresetBar::moreMenu() const
{
    juce::PopupMenu menu;
    menu.addItem(1, utf8("New bank\xe2\x80\xa6"));
    menu.addItem(2, utf8("Export bank\xe2\x80\xa6"), selectedBank_.isNotEmpty());
    menu.addItem(3, utf8("Import bank\xe2\x80\xa6"));
    menu.addItem(4, "Open presets folder");
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
                               [safe, bank = selectedBank_, names = presets_](int id) {
                                   // The list belongs to the bank selected when it opened.
                                   if (safe != nullptr && id > 0 && id <= names.size() && safe->selectedBank_ == bank)
                                       safe->requestLoad(names[id - 1]);
                               });
}

void PresetBar::openMoreMenu()
{
    reloadLists();
    updateView();
    juce::Component::SafePointer<PresetBar> safe(this);
    moreMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&more_), [safe](int id) {
        if (safe == nullptr)
            return;
        switch (id)
        {
            case 1: safe->requestNewBank(); break;
            case 2: safe->chooseExportFile(); break;
            case 3: safe->chooseImportFile(); break;
            case 4: safe->openPresetFolder(); break;
            default: break;
        }
    });
}

void PresetBar::chooseExportFile()
{
    if (selectedBank_.isEmpty())
        return;
    const auto suggested = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                               .getChildFile(selectedBank_ + ".zip");
    chooser_ = std::make_unique<juce::FileChooser>("Export bank", suggested, "*.zip");
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (safe != nullptr && file != juce::File())
                                  safe->exportBankTo(file.hasFileExtension("zip") ? file : file.withFileExtension("zip"));
                          });
}

void PresetBar::chooseImportFile()
{
    chooser_ = std::make_unique<juce::FileChooser>("Import bank", juce::File(), "*.zip");
    juce::Component::SafePointer<PresetBar> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (safe != nullptr && file != juce::File())
                                  safe->importBankFrom(file);
                          });
}

void PresetBar::openPresetFolder()
{
    const auto root = store_.root();
    if (!root.createDirectory().wasOk() || !root.startAsProcess())
        refuse("The preset folder could not be opened.");
}

void PresetBar::updateView()
{
    bankField_.setDisplay(selectedBank_.isEmpty() ? juce::String("No bank") : selectedBank_,
                          selectedBank_.isEmpty() ? kDim : kAmber);

    const bool showMessage = messageUntil_ != 0 && juce::Time::getMillisecondCounter() < messageUntil_;
    if (!showMessage)
        messageUntil_ = 0;
    const auto active = activePresetName();
    if (showMessage)
        presetField_.setDisplay(message_, messageColour_);
    else
        presetField_.setDisplay(active.isEmpty() ? juce::String("No preset") : active, active.isEmpty() ? kDim : kAmber);
    presetField_.setTooltip(showMessage ? message_ : juce::String("Load preset"));

    const bool normal = mode_ == Mode::Normal;
    const bool confirm = mode_ == Mode::ConfirmDiscard;
    const bool prompt = mode_ == Mode::NamePrompt;
    const bool askBank = prompt && prompt_ == Prompt::PresetAndBank;
    presetCaption_.setVisible(normal);
    presetField_.setVisible(normal);
    save_.setVisible(normal);
    saveAs_.setVisible(normal);
    more_.setVisible(normal);
    confirmText_.setVisible(confirm);
    confirmLoad_.setVisible(confirm);
    confirmCancel_.setVisible(confirm);

    // NamePrompt: the BANK field stays (it shows where the preset goes) unless a new bank is named.
    bankField_.setVisible(!askBank);
    bankNameEditor_.setVisible(askBank);
    nameEditor_.setVisible(prompt);
    promptOk_.setVisible(prompt);
    promptCancel_.setVisible(prompt);
    promptMessage_.setText(showMessage ? message_ : juce::String(), juce::dontSendNotification);
    promptMessage_.setColour(juce::Label::textColourId, messageColour_);
    promptMessage_.setTooltip(showMessage ? message_ : juce::String());
    promptMessage_.setVisible(prompt && showMessage);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    const int fieldW = std::clamp((area.getWidth() - kFixedW) / 2, kFieldMinW, kFieldMaxW);

    bankCaption_.setBounds(area.removeFromLeft(kBankCaptionW));
    area.removeFromLeft(kCaptionGap);
    const auto bankBounds = area.removeFromLeft(fieldW);
    bankField_.setBounds(bankBounds);
    bankNameEditor_.setBounds(bankBounds);
    area.removeFromLeft(kGroupGap);

    // Everything right of the BANK field: PRESET field and buttons, or in their place the
    // ConfirmDiscard question or the name prompt.
    area = area.removeFromLeft(std::min(area.getWidth(), kPresetCaptionW + kCaptionGap + fieldW + kButtonRoomW));
    auto take = [](juce::Rectangle<int>& r, int w) { return r.removeFromRight(std::clamp(w, 0, std::max(r.getWidth(), 0))); };

    auto confirm = area;
    confirmCancel_.setBounds(take(confirm, kCancelW));
    take(confirm, 4);
    confirmLoad_.setBounds(take(confirm, kConfirmLoadW));
    take(confirm, 6);
    confirmText_.setBounds(confirm);

    auto prompt = area;
    promptCancel_.setBounds(take(prompt, kCancelW));
    take(prompt, kButtonGap);
    promptOk_.setBounds(take(prompt, kPromptOkW));
    take(prompt, 6);
    const int nameW = std::clamp(prompt.getWidth() - kPromptMessageMinW - 6, kFieldMinW, fieldW);
    nameEditor_.setBounds(prompt.removeFromLeft(std::min(nameW, prompt.getWidth())));
    prompt.removeFromLeft(std::min(6, prompt.getWidth()));
    promptMessage_.setBounds(prompt);

    auto normal = area;
    presetCaption_.setBounds(normal.removeFromLeft(kPresetCaptionW));
    normal.removeFromLeft(kCaptionGap);
    presetField_.setBounds(normal.removeFromLeft(std::min(fieldW, normal.getWidth())));
    normal.removeFromLeft(std::min(6, normal.getWidth()));
    save_.setBounds(normal.removeFromLeft(std::min(kSaveW, normal.getWidth())));
    normal.removeFromLeft(std::min(kButtonGap, normal.getWidth()));
    saveAs_.setBounds(normal.removeFromLeft(std::min(kSaveAsW, normal.getWidth())));
    normal.removeFromLeft(std::min(kButtonGap, normal.getWidth()));
    more_.setBounds(normal.removeFromLeft(std::min(kMoreW, normal.getWidth())));
}

} // namespace dc::ui
