// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

// The install itself, and the end. The work is done in cutefish-installerctl
// and watched from here: the step it stands on, the bar that follows it, and
// the record of what was done — then the badge of a thing done, and the two
// ways out: restart into the new system, or keep using the live session.

#include "app/installerwindow.h"

#include <string>

#include "base/i18n.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/progress_bar.h"
#include "ui/theme.h"

using namespace cutefish::ui;
using cutefish::i18n::tr;

namespace installer {

void InstallerWindow::buildProgressPage()
{
    PageFrame frame = makePage(tr("Install"),
                               tr("Cutefish OS is being installed. Keep the machine on, and give it the "
                                  "time it asks for."));

    Card* card = frame.content->addChild<Card>();
    m_progressStep = card->addChild<Label>(tr("Starting…"));
    m_progressStep->setWeight(int(600 * 4 / 3));
    m_progress = card->addChild<ProgressBar>();
    m_progress->setLayoutHints({.fillWidth = true});
    m_progressStatus = card->addChild<Label>(std::string());
    m_progressStatus->setColor(ColorId::DisabledText);
    m_progressStatus->setElide(Label::Elide::Right);

    Section* record = frame.content->addChild<Section>(tr("The work itself"));
    m_log = record->addChild<LogView>();
    m_log->setLayoutHints({.fillWidth = true, .fillHeight = true});

    refreshFooter();
}

void InstallerWindow::buildDonePage()
{
    PageFrame frame = makePage(tr("Done"), std::string());

    // The end, in the middle of the page: the badge, the words, and the way
    // on from here.
    ColumnLayout* middle = frame.content->addChild<ColumnLayout>();
    middle->setSpacing(16);
    LayoutHints centre{.fillWidth = true, .alignment = Alignment::Center};
    middle->setLayoutHints(centre);

    middle->addChild<CheckBadge>(80);

    Label* title = middle->addChild<Label>(tr("Cutefish OS is installed."));
    title->setFontSize(16 * 4 / 3);
    title->setWeight(int(600 * 4 / 3));
    LayoutHints titleHints{.alignment = Alignment::Center};
    title->setLayoutHints(titleHints);

    Label* words = middle->addChild<Label>(
        tr("Take the disk or the stick the machine started from out, and restart.\n"
           "Everything you chose — the language, the keyboard, the account — is already "
           "set up, and the desktop walks the rest of the way in with you."));
    words->setWrap(true);
    words->setLayoutHints({.fillWidth = true, .alignment = Alignment::Center});
    words->setColor(ColorId::DisabledText);

    refreshFooter();
}

} // namespace installer
