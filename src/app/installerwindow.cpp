// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "app/installerwindow.h"

#include <cstdio>
#include <functional>

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/process.h"
#include "core/util.h"
#include "ui/button.h"
#include "ui/progress_bar.h"
#include "ui/dialog_window.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/theme.h"
#include "ui/window_header.h"

using namespace cutefish::ui;
using namespace cutefish::installer;
using cutefish::i18n::tr;

namespace installer {

// The settings app's page rhythm: the margin a page keeps, and the air
// under its header.
static constexpr double kPageMargin = 24;
static constexpr double kPageTopMargin = 16;
static constexpr double kSidebarWidth = 230;

InstallerWindow::InstallerWindow()
{
    m_plan.setSource(InstallPlan::liveSource());
    buildWindow();
    buildLanguagePage();
}

InstallerWindow::~InstallerWindow() = default;

void InstallerWindow::buildWindow()
{
    auto root = std::make_unique<Column>();
    root->setSpacing(0);

    // The header: the window's buttons at its end, the step's name from the
    // sidebar's edge, as the settings app's header holds its page's.
    m_header = root->addChild<WindowHeader>(40);
    m_headerTitle = m_header->setContent<Label>();
    m_headerTitle->setFontSize(11 * 4 / 3);
    LayoutHints titleHints{.fillWidth = true};
    titleHints.margins.left = kSidebarWidth + 9;
    titleHints.verticalAlignment = Alignment::Center;
    m_headerTitle->setLayoutHints(titleHints);

    // The body: the sidebar of steps, the steps themselves.
    auto body = std::make_unique<RowLayout>();
    body->setSpacing(0);
    m_sidebar = body->addChild<Sidebar>();
    m_sidebar->setLayoutHints({.fillHeight = true});
    m_sidebar->setTitle(tr("Install Cutefish OS"));
    m_sidebar->setEntries({
        {tr("Language"), "preferences-desktop-locale", accent::blue},
        {tr("Keyboard"), "input-keyboard", accent::grey},
        {tr("Agreement"), "text-x-generic", accent::orange},
        {tr("Disk"), "drive-harddisk", accent::green},
        {tr("Users"), "system-users", accent::purple},
        {tr("Privacy"), "preferences-system-privacy", accent::pink},
        {tr("Review"), "edit-find", accent::grey},
        {tr("Install"), "system-software-install", accent::blue},
    });
    // A step already walked may be gone back to; the walk itself only goes
    // forward from where it stands.
    m_sidebar->onActivated = [this](int index) {
        if (m_installing || index >= int(m_step))
            return;
        goTo(static_cast<Step>(index));
    };

    m_stack = body->addChild<PageStack>();
    LayoutHints stackHints{.fillWidth = true, .fillHeight = true};
    m_stack->setLayoutHints(stackHints);
    root->addChild(std::move(body));

    // The footer: the dialog's own buttons, at the end of the line. What
    // they say and do belongs to the step the window stands on.
    m_footerBar = root->addChild<RowLayout>();
    m_footerBar->setSpacing(8);
    m_footerBar->setPadding({kPageMargin, 12, kPageMargin, 16});
    LayoutHints barHints{.fillWidth = true, .alignment = Alignment::End};
    m_footerBar->setLayoutHints(barHints);
    m_backButton = m_footerBar->addChild<DialogButton>(tr("Back"), false);
    m_nextButton = m_footerBar->addChild<DialogButton>(tr("Continue"), true);
    m_stopButton = m_footerBar->addChild<DialogButton>(tr("Stop"), false);
    m_backButton->onClicked = [this] { backClicked(); };
    m_nextButton->onClicked = [this] { nextClicked(); };
    m_stopButton->onClicked = [this] {
        askConfirm(tr("Stop the install?"),
                   tr("The machine is left as it was. Nothing half-done stays on the disk."),
                   tr("Stop"), true, [this] { m_runner.cancel(); });
    };
    m_stopButton->setVisible(false);

    m_window.setTitle(tr("Install Cutefish OS"));
    m_window.resize({900, 640});
    m_window.setMinimumSize({760, 540});
    m_window.setRestoresSize("installer");
    m_window.setContentView(std::move(root));

    m_toast = std::make_unique<Toast>(&m_window);
}

InstallerWindow::PageFrame InstallerWindow::makePage(const std::string& title, const std::string& subtitle)
{
    PageFrame frame;
    // The step's name rides in the header, as the settings app's pages'
    // names do; the page below holds only its own work.
    m_headerTitle->setText(title);

    frame.page = new ColumnLayout();
    frame.page->setSpacing(16);
    frame.page->setPadding({kPageMargin, kPageTopMargin, kPageMargin, kPageTopMargin});

    if (!subtitle.empty())
        frame.page->addChild<SectionLabel>(subtitle, true);

    frame.scroll = frame.page->addChild<ScrollView>();
    LayoutHints scrollHints{.fillWidth = true, .fillHeight = true};
    frame.scroll->setLayoutHints(scrollHints);
    frame.content = frame.scroll->setContent<ColumnLayout>();
    frame.content->setSpacing(kPageMargin);

    m_stack->replace(std::unique_ptr<View>(frame.page));
    return frame;
}

void InstallerWindow::goTo(Step step)
{
    m_step = step;
    // The sidebar's own steps: where the window stands, and what is done.
    m_sidebar->setCurrentIndex(std::min(int(step), kSidebarSteps - 1));
    m_sidebar->setDoneUpTo(std::min(int(step), kSidebarSteps - 1) - 1);

    switch (step) {
    case Step::Language:
        buildLanguagePage();
        break;
    case Step::Keyboard:
        buildKeyboardPage();
        break;
    case Step::License:
        buildLicensePage();
        break;
    case Step::Disk:
        scanDisks();
        buildDiskPage();
        break;
    case Step::User:
        buildUserPage();
        break;
    case Step::Privacy:
        buildPrivacyPage();
        break;
    case Step::Review:
        buildReviewPage();
        break;
    case Step::Install:
        buildProgressPage();
        break;
    case Step::Done:
        buildDonePage();
        break;
    }
    refreshFooter();
}

void InstallerWindow::refreshFooter()
{
    // The footer says and does what the step it stands on needs. While the
    // engine works, only Stop stays.
    m_stopButton->setVisible(m_step == Step::Install && m_installing);
    m_backButton->setVisible(m_step != Step::Language && m_step != Step::Install);
    m_nextButton->setVisible(m_step != Step::Install);

    switch (m_step) {
    case Step::Language:
    case Step::Keyboard:
        m_nextButton->setText(tr("Continue"));
        m_nextButton->setEnabled(true);
        break;
    case Step::License:
        m_nextButton->setText(tr("Continue"));
        m_nextButton->setEnabled(m_licenseAccepted);
        break;
    case Step::Disk: {
        std::string why;
        m_nextButton->setText(tr("Continue"));
        m_nextButton->setEnabled(diskChoiceReady(&why));
        break;
    }
    case Step::User: {
        std::string why;
        m_nextButton->setText(tr("Continue"));
        m_nextButton->setEnabled(userChoiceReady(&why));
        break;
    }
    case Step::Privacy:
        m_nextButton->setText(tr("Continue"));
        m_nextButton->setEnabled(true);
        break;
    case Step::Review:
        m_nextButton->setText(tr("Install"));
        m_nextButton->setEnabled(true);
        break;
    case Step::Install:
        break;
    case Step::Done:
        m_backButton->setText(tr("Keep using the live session"));
        m_backButton->setEnabled(true);
        m_backButton->setVisible(true);
        m_nextButton->setText(tr("Restart now"));
        m_nextButton->setPrimary(true);
        m_nextButton->setEnabled(true);
        m_nextButton->setVisible(true);
        break;
    }
}

void InstallerWindow::nextClicked()
{
    switch (m_step) {
    case Step::Language:
    case Step::Keyboard:
        goTo(static_cast<Step>(int(m_step) + 1));
        break;
    case Step::License:
        if (m_licenseAccepted)
            goTo(static_cast<Step>(int(m_step) + 1));
        break;
    case Step::Disk: {
        std::string why;
        if (!diskChoiceReady(&why)) {
            m_toast->show(why);
            return;
        }
        goTo(static_cast<Step>(int(m_step) + 1));
        break;
    }
    case Step::User: {
        std::string why;
        if (!userChoiceReady(&why)) {
            m_toast->show(why);
            return;
        }
        goTo(static_cast<Step>(int(m_step) + 1));
        break;
    }
    case Step::Privacy:
        goTo(static_cast<Step>(int(m_step) + 1));
        break;
    case Step::Review:
        startInstall();
        break;
    case Step::Install:
        break;
    case Step::Done:
        restartNow();
        break;
    }
}

void InstallerWindow::backClicked()
{
    if (m_installing)
        return;
    if (m_step == Step::Done) {
        // The live session goes on without the window.
        m_window.hide();
        return;
    }
    if (int(m_step) > int(Step::Language))
        goTo(static_cast<Step>(int(m_step) - 1));
}

void InstallerWindow::askConfirm(const std::string& title, const std::string& text,
                                 const std::string& confirmText, bool destructive,
                                 std::function<void()> onAccepted, bool cancelVisible)
{
    m_confirm = std::make_shared<ConfirmDialog>(&m_window, title, text);
    m_confirm->setConfirmText(confirmText.empty() ? tr("Confirm") : confirmText);
    m_confirm->setDestructive(destructive);
    m_confirm->setCancelVisible(cancelVisible);

    // The dialog lives until the event that answered it is over, freed by
    // the loop after; the file manager keeps its dialogs the same way.
    const auto dismiss = [this]() {
        std::shared_ptr<ConfirmDialog> alive = std::move(m_confirm);
        cutefish::base::EventLoop::current()->post([alive] {});
    };
    m_confirm->onAccepted = [this, dismiss, onAccepted = std::move(onAccepted)] {
        dismiss();
        if (onAccepted)
            onAccepted();
    };
    m_confirm->onRejected = [this, dismiss] { dismiss(); };
    m_confirm->show();
}

// -- the run -----------------------------------------------------------------

void InstallerWindow::startInstall()
{
    collectPlan();
    std::string why;
    if (!m_plan.validate(&why)) {
        m_toast->show(why);
        return;
    }

    // The last honest warning: the disk is about to be changed in ways that
    // cannot be undone.
    const std::string question =
        m_manualMode
            ? tr("The chosen partitions are mounted and made into a new system. Files on the ones to format are lost. Install?")
            : tr("The whole disk is erased and Cutefish OS is installed on it. Everything on it is lost. Install?");
    askConfirm(tr("Install now"), question, tr("Install"), true, [this] {
        m_installing = true;
        goTo(Step::Install);
        m_runner.onEvent = [this](const JobRunner::Event& event) { jobEvent(event); };
        m_runner.onDone = [this](JobRunner::Outcome outcome, const CoreJson& summary,
                                 const std::string& errorText) {
            jobDone(outcome, summary, errorText);
        };
        // The window holds the administrator's own rights on a live
        // session; a started-from-the-desktop window re-execs through pkexec
        // before it ever gets here.
        const bool privileged = !cutefish::installer::runningAsRoot();
        m_runner.start(privileged ? "/usr/bin/cutefish-installerctl" : "cutefish-installerctl",
                       {"install", "--config", m_configPath, "--progress-events"}, privileged);
    });
}

void InstallerWindow::restartNow()
{
    // The new system is what the machine starts into.
    const bool privileged = !cutefish::installer::runningAsRoot();
    cutefish::base::Process::startDetached(
        privileged ? "pkexec" : "cutefish-installerctl",
        privileged ? std::vector<std::string>{"/usr/bin/cutefish-installerctl", "reboot"}
                   : std::vector<std::string>{"reboot"});
}

void InstallerWindow::jobEvent(const JobRunner::Event& event)
{
    const CoreJson& data = event.data;
    if (event.name == "step") {
        if (m_progressStep) {
            const std::string id = data.get("id").toString();
            static const std::pair<const char*, const char*> names[] = {
                {"prepare", "Checking the machine and the plan"},
                {"partition", "Laying out the disk"},
                {"format", "Making the file systems"},
                {"mount", "Mounting the new system"},
                {"copy", "Copying the system's files"},
                {"fstab", "Writing the mounts table"},
                {"configure", "Configuring the new system"},
                {"bootloader", "Installing the bootloader"},
                {"firstboot", "Setting the first boot up"},
                {"cleanup", "Finishing up"},
            };
            std::string label = tr("Working…");
            for (const auto& name : names) {
                if (id == name.first)
                    label = tr(name.second);
            }
            m_progressStep->setText(label);
            if (m_progressStatus)
                m_progressStatus->setText(std::string());
            if (m_log)
                m_log->addLine("== " + label + " ==");
        }
    } else if (event.name == "progress") {
        if (m_progress)
            m_progress->setValue(data.get("percent").toNumber() / 100.0);
        if (m_progressStatus)
            m_progressStatus->setText(data.get("message").toString());
    } else if (event.name == "log") {
        if (m_log)
            m_log->addLine(data.get("message").toString());
    }
}

void InstallerWindow::jobDone(JobRunner::Outcome outcome, const CoreJson& summary,
                              const std::string& errorText)
{
    m_installing = false;
    if (outcome == JobRunner::Outcome::Finished) {
        goTo(Step::Done);
        return;
    }
    if (outcome == JobRunner::Outcome::Cancelled) {
        goTo(Step::Review);
        if (m_log)
            m_log->addLine(tr("Stopped. The machine was left as it was."));
        m_toast->show(tr("The install was stopped before anything was finished."));
        return;
    }
    const std::string message = errorText.empty()
        ? summary.get("message").toString()
        : errorText;
    askConfirm(tr("Something went wrong"), message.empty() ? tr("The install failed.") : message,
               tr("OK"), false, nullptr, false);
    goTo(Step::Review);
}

} // namespace installer
