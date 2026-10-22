// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include <cstdio>
#include <unistd.h>

#include "app/installerwindow.h"
#include "base/i18n.h"
#include "base/process.h"
#include "core/util.h"
#include "ui/application.h"

int main(int argc, char** argv)
{
    cutefish::ui::Application application(argc, argv);
    application.setApplicationId("cutefish-installer");
    cutefish::i18n::setDomain("cutefish-installer");

    // Installing a system is the administrator's own work. On a live session
    // the window already holds those rights; started from an installed
    // desktop, it asks for them once and starts itself again with them.
    if (!cutefish::installer::runningAsRoot()) {
        char self[512];
        const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (length <= 0) {
            std::fprintf(stderr, "cutefish-installer: cannot find itself\n");
            return 1;
        }
        self[length] = '\0';
        const pid_t started = cutefish::base::Process::startDetached(
            "pkexec", {self}, {}, {"XDG_SESSION_TYPE=wayland", "WAYLAND_DISPLAY"});
        if (started <= 0) {
            std::fprintf(stderr,
                         "cutefish-installer: the administrator's helper, pkexec, could not be started\n");
            return 1;
        }
        return 0;
    }

    installer::InstallerWindow window;
    window.window().show();

    return application.run();
}
