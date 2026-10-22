// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

// The account, the privacy, and the review: the person the machine belongs
// to, what the system may reach on its behalf, and everything the install
// will do, shown together before anything is done.

#include "app/installerwindow.h"

#include <string>
#include <vector>

#include "base/i18n.h"
#include "core/tables.h"
#include "core/util.h"
#include "ui/combo_box.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/switch.h"
#include "ui/text_field.h"
#include "ui/theme.h"

using namespace cutefish::ui;
using namespace cutefish::installer;
using cutefish::i18n::tr;

namespace installer {

// -- the account ----------------------------------------------------------------

void InstallerWindow::buildUserPage()
{
    PageFrame frame = makePage(tr("Users"),
                               tr("The account this machine starts with, and its own name on the network."));

    Section* account = frame.content->addChild<Section>(tr("Your account"));
    Card* card = account->addChild<Card>();

    auto addField = [&](const char* label, TextField* field) {
        Label* name = card->addChild<Label>(tr(label));
        name->setColor(ColorId::DisabledText);
        name->setFontSize(Theme::current().smallFontSize());
        card->addChild(std::unique_ptr<TextField>(field));
    };

    m_nameField = new TextField();
    m_nameField->setPlaceholder(tr("ex. John Doe"));
    addField("Full name", m_nameField);
    m_nameField->onTextEdited = [this] {
        // The username follows the name, until it is typed over.
        if (!m_usernameTouched && m_userField)
            m_userField->setText(cutefish::installer::suggestUsername(m_nameField->text()));
        refreshUserHint();
        refreshFooter();
    };

    m_userField = new TextField();
    m_userField->setPlaceholder(tr("john"));
    addField("Username", m_userField);
    m_userField->onTextEdited = [this] {
        m_usernameTouched = true;
        refreshUserHint();
        refreshFooter();
    };

    m_hostField = new TextField();
    m_hostField->setText(m_plan.hostName().empty() ? std::string("cutefish-pc") : m_plan.hostName());
    addField("The computer's name", m_hostField);
    m_hostField->onTextEdited = [this] {
        refreshUserHint();
        refreshFooter();
    };

    Section* password = frame.content->addChild<Section>(tr("The password"));
    Card* passCard = password->addChild<Card>();

    auto addPassField = [&](const char* label, TextField* field) {
        Label* name = passCard->addChild<Label>(tr(label));
        name->setColor(ColorId::DisabledText);
        name->setFontSize(Theme::current().smallFontSize());
        passCard->addChild(std::unique_ptr<TextField>(field));
    };

    m_passField = new TextField();
    m_passField->setEchoMode(TextField::EchoMode::Password);
    m_passField->setPlaceholder("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2");
    addPassField("Password", m_passField);
    m_passField->onTextEdited = [this] {
        refreshUserHint();
        refreshFooter();
    };

    m_pass2Field = new TextField();
    m_pass2Field->setEchoMode(TextField::EchoMode::Password);
    m_pass2Field->setPlaceholder(tr("The same password again"));
    addPassField("Confirm the password", m_pass2Field);
    m_pass2Field->onTextEdited = [this] {
        refreshUserHint();
        refreshFooter();
    };

    m_userHint = passCard->addChild<Label>(std::string());
    m_userHint->setColor(ColorId::DisabledText);
    m_userHint->setFontSize(Theme::current().smallFontSize());
    m_userHint->setWrap(true);
    m_userHint->setLayoutHints({.fillWidth = true});
    refreshUserHint();

    Section* zone = frame.content->addChild<Section>(tr("Where this machine is"));
    Card* zoneCard = zone->addChild<Card>(false);
    FormRow* zoneRow = zoneCard->addChild<FormRow>(tr("Time zone"),
                                                   tr("The clock follows it, and the calendar with the sun."));
    std::vector<std::string> zones;
    std::string current = m_plan.timeZone();
    if (current.empty())
        current = cutefish::installer::systemTimeZone();
    if (!cutefish::installer::knownZone(current))
        current = "UTC";
    int at = 0;
    for (const cutefish::installer::Zone& entry : cutefish::installer::timeZones()) {
        const std::string path = cutefish::installer::zonePath(entry);
        if (path == current)
            at = int(zones.size());
        zones.push_back(path);
    }
    m_zoneBox = zoneRow->setTrailing<ComboBox>(std::move(zones));
    m_zoneBox->setCurrentIndex(at);
    m_zoneBox->onActivated = [this](int index) {
        const std::vector<cutefish::installer::Zone>& table = cutefish::installer::timeZones();
        if (index >= 0 && index < int(table.size()))
            m_plan.setTimeZone(cutefish::installer::zonePath(table[size_t(index)]));
    };
}

bool InstallerWindow::userChoiceReady(std::string* why)
{
    if (!m_nameField || !m_userField || !m_hostField || !m_passField || !m_pass2Field) {
        if (why)
            *why = tr("The page is not ready.");
        return false;
    }
    const std::string name = m_nameField->text();
    const std::string user = m_userField->text();
    const std::string host = m_hostField->text();
    const std::string pass = m_passField->text();
    const std::string again = m_pass2Field->text();
    if (name.empty() || user.empty()) {
        if (why)
            *why = tr("The account needs a name and a username.");
        return false;
    }
    if (!cutefish::installer::validUsernameName(user)) {
        if (why)
            *why = tr("The username takes lower case letters, digits, hyphens and underscores, "
                      "and starts with a letter.");
        return false;
    }
    if (!cutefish::installer::validHostName(cutefish::installer::suggestHostname(host))) {
        if (why)
            *why = tr("The computer's name takes lower case letters, digits and hyphens.");
        return false;
    }
    if (pass.empty()) {
        if (why)
            *why = tr("The account needs a password.");
        return false;
    }
    if (pass != again) {
        if (why)
            *why = tr("The two passwords differ.");
        return false;
    }
    return true;
}

void InstallerWindow::refreshUserHint()
{
    if (!m_userHint || !m_passField || !m_pass2Field)
        return;
    const std::string pass = m_passField->text();
    const std::string again = m_pass2Field->text();
    if (!pass.empty() && !again.empty() && pass != again) {
        m_userHint->setText(tr("The two passwords differ."));
        m_userHint->setColor(accent::red);
        return;
    }
    if (!pass.empty() && pass.size() < 8) {
        m_userHint->setText(tr("A longer password keeps the machine safer."));
        m_userHint->setColor(ColorId::DisabledText);
        return;
    }
    m_userHint->setText(std::string());
}

// -- the privacy -----------------------------------------------------------------

void InstallerWindow::buildPrivacyPage()
{
    PageFrame frame = makePage(tr("Privacy"),
                               tr("What the installed system may reach, to begin with. Every one of these "
                                  "can be changed later, in the settings."));

    cutefish::installer::PrivacySettings privacy = m_plan.privacy();

    Card* card = frame.content->addChild<Card>(false);
    FormRow* location = card->addChild<FormRow>(tr("Location services"),
                                                tr("Apps may ask where this machine is, for the weather and the maps."));
    Switch* locationSwitch = location->setTrailing<Switch>();
    locationSwitch->setChecked(privacy.location);
    locationSwitch->onToggled = [this](bool checked) {
        cutefish::installer::PrivacySettings settings = m_plan.privacy();
        settings.location = checked;
        m_plan.setPrivacy(settings);
    };

    FormRow* diagnostics = card->addChild<FormRow>(tr("Diagnostic reports"),
                                                   tr("The desktop's makers learn how it runs, with nothing "
                                                      "of your files or your work in it."));
    Switch* diagnosticsSwitch = diagnostics->setTrailing<Switch>();
    diagnosticsSwitch->setChecked(privacy.diagnostics);
    diagnosticsSwitch->onToggled = [this](bool checked) {
        cutefish::installer::PrivacySettings settings = m_plan.privacy();
        settings.diagnostics = checked;
        m_plan.setPrivacy(settings);
    };

    FormRow* tailored = card->addChild<FormRow>(tr("Tailored suggestions"),
                                                tr("The stores and the start page may suggest by what this "
                                                   "machine is used for."));
    Switch* tailoredSwitch = tailored->setTrailing<Switch>();
    tailoredSwitch->setChecked(privacy.tailored);
    tailoredSwitch->onToggled = [this](bool checked) {
        cutefish::installer::PrivacySettings settings = m_plan.privacy();
        settings.tailored = checked;
        m_plan.setPrivacy(settings);
    };
}

// -- the review --------------------------------------------------------------------

void InstallerWindow::collectPlan()
{
    if (m_diskIndex >= 0 && m_diskIndex < int(m_disks.size()))
        m_plan.setDevice(m_disks[size_t(m_diskIndex)].kname);
    m_plan.setMode(m_manualMode ? cutefish::installer::InstallPlan::Mode::Manual
                                : cutefish::installer::InstallPlan::Mode::Erase);
    m_plan.setSwap("auto");
    if (m_manualMode) {
        manualRowChanged();
        m_plan.setPartitions(m_manualPartitions);
    }
    if (m_nameField)
        m_plan.setFullName(m_nameField->text());
    if (m_userField)
        m_plan.setUserName(m_userField->text());
    if (m_hostField)
        m_plan.setHostName(cutefish::installer::suggestHostname(m_hostField->text()));
    if (m_passField)
        m_plan.setPassword(m_passField->text());
    if (m_zoneBox && m_zoneBox->currentIndex() >= 0
        && m_zoneBox->currentIndex() < int(cutefish::installer::timeZones().size()))
        m_plan.setTimeZone(
            cutefish::installer::zonePath(cutefish::installer::timeZones()[size_t(m_zoneBox->currentIndex())]));

    // The plan the engine follows, written where the CLI reads it.
    cutefish::installer::writeFile(m_configPath, m_plan.toJson().dump(true));
}

void InstallerWindow::buildReviewPage()
{
    collectPlan();
    PageFrame frame = makePage(tr("Review"),
                               tr("Everything the install will do, in one place. The work starts only from "
                                  "here."));

    Card* card = frame.content->addChild<Card>(false);
    const auto addValue = [this](Card* where, const char* name, std::string value) {
        FormRow* row = where->addChild<FormRow>(tr(name));
        row->setTrailing<Label>(std::move(value));
    };

    addValue(card, "Language", cutefish::installer::languageLabel(m_plan.language()));
    addValue(card, "Keyboard", cutefish::installer::keyboardLabel(m_plan.keyboard()));
    addValue(card, "Time zone", m_plan.timeZone());
    addValue(card, "Disk",
             m_diskIndex >= 0 && m_diskIndex < int(m_disks.size())
                 ? m_disks[size_t(m_diskIndex)].description()
                 : tr("none chosen"));
    if (m_manualMode) {
        addValue(card, "Layout",
                 std::string(tr("The partitions are used as they are")) + " ("
                     + std::to_string(m_manualPartitions.size()) + ")");
    } else {
        addValue(card, "Layout", tr("The whole disk is erased and laid out anew"));
    }
    addValue(card, "Account", m_plan.fullName() + " (" + m_plan.userName() + ")");
    addValue(card, "The computer's name", m_plan.hostName());

    frame.content->addChild<SectionLabel>(
        m_manualMode
            ? tr("The partitions chosen to format are made into empty new file systems. "
                 "Make sure anything needed from them is saved elsewhere.")
            : tr("Everything on the chosen disk is erased. Make sure anything needed from it "
                 "is saved elsewhere."),
        true);
}

} // namespace installer
