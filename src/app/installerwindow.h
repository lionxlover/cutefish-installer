// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/jobrunner.h"
#include "app/widgets.h"
#include "core/disks.h"
#include "core/installplan.h"
#include "ui/dialog_window.h"
#include "ui/toast.h"
#include "ui/window.h"

namespace cutefish::ui {
class CheckBox;
class ComboBox;
class DialogButton;
class Label;
class ProgressBar;
class TextField;
class WindowHeader;
} // namespace cutefish::ui

namespace installer {

// The installer's one window: the settings app's own shape — a sidebar of
// steps, the step's name in the header, the work in cards and rows — walked
// as a wizard. Every step's choice lands in the same plan the engine
// follows, so what the review shows is what the install does; the install
// itself is done in cutefish-installerctl, started and watched from here.
class InstallerWindow
{
public:
    InstallerWindow();
    ~InstallerWindow();

    cutefish::ui::Window& window() { return m_window; }

private:
    // The steps, in the order they are walked. The sidebar shows the first
    // eight; the last is the end itself.
    enum class Step {
        Language,
        Keyboard,
        License,
        Disk,
        User,
        Privacy,
        Review,
        Install,
        Done,
    };
    static constexpr int kSidebarSteps = 8; // the sidebar's own entries

    // A page's frame: the content that scrolls, with the page's own title
    // riding in the header.
    struct PageFrame
    {
        cutefish::ui::ColumnLayout* page = nullptr;
        cutefish::ui::ScrollView* scroll = nullptr;
        cutefish::ui::ColumnLayout* content = nullptr;
    };

    void buildWindow();
    PageFrame makePage(const std::string& title, const std::string& subtitle);
    void goTo(Step step);
    void nextClicked();
    void backClicked();
    void refreshFooter();
    void askConfirm(const std::string& title, const std::string& text, const std::string& confirmText,
                    bool destructive, std::function<void()> onAccepted, bool cancelVisible = true);

    // -- the pages, each one the step's own --------------------------------
    void buildLanguagePage();
    void buildKeyboardPage();
    void buildLicensePage();
    void buildDiskPage();
    void buildUserPage();
    void buildPrivacyPage();
    void buildReviewPage();
    void buildProgressPage();
    void buildDonePage();

    // -- the choices, gathered into the plan --------------------------------
    void scanDisks();
    void pickDisk(int row);
    void pickInstallMode(bool manual);
    void rebuildManualRows();
    void manualRowChanged();
    bool diskChoiceReady(std::string* why);
    bool userChoiceReady(std::string* why);
    void refreshUserHint();
    void collectPlan();
    void startInstall();
    void restartNow();

    // One partition of the manual layout, with the page's own controls.
    struct ManualRow
    {
        std::string kname;
        std::string fstype;
        cutefish::ui::ComboBox* role = nullptr;
        cutefish::ui::CheckBox* format = nullptr;
    };

    // -- the run, watched from here ------------------------------------------
    void jobEvent(const JobRunner::Event& event);
    void jobDone(JobRunner::Outcome outcome, const CoreJson& summary, const std::string& errorText);

    Step m_step = Step::Language;

    cutefish::ui::Window m_window;
    cutefish::ui::WindowHeader* m_header = nullptr;
    cutefish::ui::Label* m_headerTitle = nullptr;
    Sidebar* m_sidebar = nullptr;
    PageStack* m_stack = nullptr;
    std::unique_ptr<cutefish::ui::Toast> m_toast;
    cutefish::ui::RowLayout* m_footerBar = nullptr;
    cutefish::ui::DialogButton* m_backButton = nullptr;
    cutefish::ui::DialogButton* m_nextButton = nullptr;
    cutefish::ui::DialogButton* m_stopButton = nullptr;

    // The plan every step builds, and the pieces it is built from.
    cutefish::installer::InstallPlan m_plan;
    std::vector<cutefish::installer::DiskDevice> m_disks;
    int m_diskIndex = -1;
    bool m_manualMode = false;
    std::vector<cutefish::installer::PlannedPartition> m_manualPartitions;
    std::vector<ManualRow> m_manualRows;
    bool m_licenseAccepted = false;
    bool m_usernameTouched = false;
    std::string m_configPath = "/tmp/cutefish-installer-plan.json";

    // The account's own fields, read while the page is shown.
    cutefish::ui::TextField* m_nameField = nullptr;
    cutefish::ui::TextField* m_userField = nullptr;
    cutefish::ui::TextField* m_hostField = nullptr;
    cutefish::ui::TextField* m_passField = nullptr;
    cutefish::ui::TextField* m_pass2Field = nullptr;
    cutefish::ui::ComboBox* m_zoneBox = nullptr;
    cutefish::ui::Label* m_userHint = nullptr;

    // The pages' live parts, looked at while they are shown.
    cutefish::ui::Label* m_progressStep = nullptr;
    cutefish::ui::Label* m_progressStatus = nullptr;
    cutefish::ui::ProgressBar* m_progress = nullptr;
    LogView* m_log = nullptr;
    bool m_installing = false;

    JobRunner m_runner;
    std::shared_ptr<cutefish::ui::ConfirmDialog> m_confirm;
};

} // namespace installer
