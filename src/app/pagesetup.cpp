// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

// The first three steps: the language, the keyboard, and the agreement.
// Each is a card of rows the settings app would know, with the choice
// itself carried beside every row's own name.

#include "app/installerwindow.h"

#include <string>
#include <vector>

#include "base/i18n.h"
#include "core/tables.h"
#include "ui/check_box.h"
#include "ui/label.h"
#include "ui/layout.h"

using namespace cutefish::ui;
using cutefish::i18n::tr;

namespace installer {

// -- the language ---------------------------------------------------------------

void InstallerWindow::buildLanguagePage()
{
    PageFrame frame = makePage(tr("Language"),
                               tr("Choose the language the installed system uses. It can be changed later in the settings."));

    Card* card = frame.content->addChild<Card>(false);
    std::vector<IconTextRow*> rows;
    for (const cutefish::installer::Language& language : cutefish::installer::languages()) {
        IconTextRow* row = card->addChild<IconTextRow>("");
        row->setTitle(language.name);
        row->setSubtitle(std::string(language.territory) + " \xc2\xb7 " + language.native);
        row->setCheckable(true);
        row->setChecked(language.code == m_plan.language());
        row->onClicked = [this, rows, row, code = std::string(language.code)] {
            m_plan.setLanguage(code);
            for (IconTextRow* other : rows)
                other->setChecked(false);
            row->setChecked(true);
        };
        rows.push_back(row);
    }
}

// -- the keyboard -----------------------------------------------------------------

void InstallerWindow::buildKeyboardPage()
{
    PageFrame frame = makePage(tr("Keyboard"),
                               tr("Choose the layout of this keyboard. It can be changed later in the settings."));

    Card* card = frame.content->addChild<Card>(false);
    std::vector<IconTextRow*> rows;
    for (const cutefish::installer::KeyboardLayout& layout : cutefish::installer::keyboardLayouts()) {
        IconTextRow* row = card->addChild<IconTextRow>("input-keyboard", 20);
        row->setTitle(layout.name);
        row->setSubtitle(layout.hint);
        row->setCheckable(true);
        row->setChecked(layout.code == m_plan.keyboard());
        row->onClicked = [this, rows, row, code = std::string(layout.code)] {
            m_plan.setKeyboard(code);
            for (IconTextRow* other : rows)
                other->setChecked(false);
            row->setChecked(true);
        };
        rows.push_back(row);
    }
}

// -- the agreement ------------------------------------------------------------------

// The words the agreement asks about, in the plain language a person reads.
static std::vector<std::string> agreementParagraphs()
{
    return {
        tr("Cutefish OS is free software: most of it is licensed under the GNU General Public "
           "License and other free licenses, with the source there for anyone to read, change "
           "and share, under the terms those licenses set out."),
        tr("The system comes as it is, with no warranty of any kind. The people who make it "
           "cannot be held responsible for what happens to files, machines or anything else "
           "while it is used."),
        tr("A few services of the desktop can reach the internet: the weather, the maps, the "
           "time, the updates. The privacy choices two steps ahead decide which of them the "
           "installed system starts with, and every one of them can be turned on or off at any "
           "time, in the settings."),
        tr("Installing erases and writes disks. Nothing the installer does is hidden: the page "
           "before the work itself shows exactly what will be done, and the work only starts "
           "after the agreement below."),
        tr("By choosing Continue you accept all of the above."),
    };
}

void InstallerWindow::buildLicensePage()
{
    PageFrame frame = makePage(tr("Agreement"),
                               tr("Please read the agreement. The install goes on only after it is accepted."));

    Card* card = frame.content->addChild<Card>();
    card->setSpacing(10);
    for (const std::string& paragraph : agreementParagraphs()) {
        Label* label = card->addChild<Label>(paragraph);
        label->setWrap(true);
        label->setLayoutHints({.fillWidth = true});
        label->setColor(ColorId::DisabledText);
    }

    // The agreement's own answer, under the words it answers.
    auto* acceptRow = frame.content->addChild<Card>(false);
    FormRow* row = acceptRow->addChild<FormRow>(tr("I have read and agree"));
    CheckBox* accept = row->setTrailing<CheckBox>();
    accept->setChecked(m_licenseAccepted);
    accept->onToggled = [this](bool checked) {
        m_licenseAccepted = checked;
        refreshFooter();
    };
}

} // namespace installer
