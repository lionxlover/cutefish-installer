// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

// The engine's own tests, run with ctest: the tables, the username and
// hostname rules, the disk scan from a canned lsblk, the plan's JSON and its
// rules, the layouts the planner lays out, the mounts table it writes, and
// the whole installation walked through as a dry run — plus the command-line
// tool itself, run as the window runs it. Nothing here touches a disk: the
// dry run is the proof, and the commands the plan would run are checked, not
// run.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/disks.h"
#include "core/installengine.h"
#include "core/installplan.h"
#include "core/json.h"
#include "core/partitionplan.h"
#include "core/tables.h"
#include "core/util.h"

using namespace cutefish::installer;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        if (!(cond)) {                                                                               \
            ++g_failed;                                                                              \
            std::fprintf(stderr, "FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
        }                                                                                            \
    } while (0)

// Both sides of a check, in the failure's message: the strings stay
// themselves, the numbers become their own text.
static std::string asText(const std::string& text) { return text; }
static std::string asText(const char* text) { return text; }
template <typename T>
static std::string asText(const T& value) { return std::to_string(value); }

#define CHECK_EQUAL(a, b)                                                                            \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        if (!((a) == (b))) {                                                                         \
            ++g_failed;                                                                              \
            std::fprintf(stderr, "FAILED: %s:%d: %s == %s\n    left:  %s\n    right: %s\n",          \
                         __FILE__, __LINE__, #a, #b, asText(a).c_str(), asText(b).c_str());           \
        }                                                                                            \
    } while (0)

// A place for the fixtures the tests write. Everything the tests make is
// under /tmp/cutefish-installer-test and removed when they are done.
static std::string testRoot()
{
    return "/tmp/cutefish-installer-test";
}

// The canned lsblk: a desktop's NVMe with the running system on it, an empty
// SSD ready for an install, a USB stick the live session runs from, and a
// loop device the image keeps its squashfs in. The disk the installer is
// pointed at in the tests is /dev/sda.
static std::string cannedLsblk()
{
    return R"({
   "blockdevices" : [
      {"name":"/dev/loop0","kname":"/dev/loop0","maj:min":"7:0","size":1825361100,
       "type":"loop","rm":false,"model":null,"vendor":null,"fstype":"squashfs",
       "children":[{"name":"/dev/loop1","kname":"/dev/loop1","maj:min":"7:1","size":1825361100,
       "type":"loop","fstype":"squashfs","mountpoints":["/run/archiso/airootfs"]}]},
      {"name":"/dev/sda","kname":"/dev/sda","maj:min":"8:0","size":1000204886016,
       "type":"disk","rm":false,"model":"Samsung SSD 980","vendor":"","fstype":null,
       "children":[
          {"name":"/dev/sda1","kname":"/dev/sda1","maj:min":"8:1","size":536870912,
           "type":"part","fstype":"vfat","label":"EFI","uuid":"AAAA-1111","partuuid":"part-aaaa-1",
           "mountpoints":[null]},
          {"name":"/dev/sda2","kname":"/dev/sda2","maj:min":"8:2","size":4294967296,
           "type":"part","fstype":"swap","uuid":"2222-swap","partuuid":"part-aaaa-2",
           "mountpoints":[null]},
          {"name":"/dev/sda3","kname":"/dev/sda3","maj:min":"8:3","size":995371173888,
           "type":"part","fstype":"ext4","uuid":"3333-root","partuuid":"part-aaaa-3",
           "mountpoints":[null]}
       ]},
      {"name":"/dev/sdb","kname":"/dev/sdb","maj:min":"8:16","size":31000123456,
       "type":"disk","rm":true,"model":"Ultra USB 3.0","vendor":"SanDisk","fstype":null,
       "children":[
          {"name":"/dev/sdb1","kname":"/dev/sdb1","maj:min":"8:17","size":31000123456,
           "type":"part","fstype":"iso9660","uuid":"iso-uuid","partuuid":"part-bbbb-1",
           "mountpoints":["/run/archiso/bootmnt"]}
       ]},
      {"name":"/dev/nvme0n1","kname":"/dev/nvme0n1","maj:min":"259:0","size":512110190592,
       "type":"disk","rm":false,"model":"WD Blue SN550","vendor":"","fstype":null,
       "children":[
          {"name":"/dev/nvme0n1p1","kname":"/dev/nvme0n1p1","maj:min":"259:1","size":1073741824,
           "type":"part","fstype":"ext4","uuid":"4444-nvme","partuuid":"part-cccc-1",
           "mountpoints":["/home"]}
       ]}
   ]
}
)";
}

static void writeFixture(const std::string& path, const std::string& text)
{
    writeFile(path, text);
}

// -- the tables -------------------------------------------------------------------

static void testTables()
{
    CHECK(knownLanguage("en_US"));
    CHECK(knownLanguage("zh_CN"));
    CHECK(!knownLanguage("xx_XX"));
    CHECK_EQUAL(std::string(languageLabel("de_DE")), std::string("German"));
    CHECK_EQUAL(std::string(languageLabel("not_a_locale")), std::string("not_a_locale"));

    CHECK(knownKeyboard("us"));
    CHECK(knownKeyboard("pt-br"));
    CHECK(!knownKeyboard("dvorak"));
    CHECK_EQUAL(std::string(keyboardLabel("fr")), std::string("French"));

    CHECK(knownZone("America/New_York"));
    CHECK(knownZone("Asia/Shanghai"));
    CHECK(knownZone("UTC"));
    CHECK(!knownZone("Middle Earth"));
}

// -- the names ---------------------------------------------------------------------

static void testNameRules()
{
    CHECK_EQUAL(suggestUsername("John Doe"), std::string("john-doe"));
    CHECK_EQUAL(suggestUsername("  "), std::string(""));
    CHECK_EQUAL(suggestUsername("\xc3\x9c""nal"), std::string("nal"));
    CHECK_EQUAL(suggestHostname("Cutefish PC!"), std::string("cutefish-pc"));
}

// -- the disk scan -------------------------------------------------------------------

static void testDiskScan()
{
    const std::string canned = testRoot() + "/lsblk.json";
    writeFixture(canned, cannedLsblk());

    std::vector<DiskDevice> disks;
    CHECK(DiskScanner::parseLsblkJson(cannedLsblk(), disks));
    // The loop is not a disk; the USB the live session runs from is never a
    // target; the two real disks stay.
    CHECK_EQUAL(disks.size(), size_t(2));
    if (disks.size() == 2) {
        CHECK_EQUAL(disks[0].kname, std::string("/dev/sda"));
        CHECK_EQUAL(disks[0].partitions.size(), size_t(3));
        CHECK_EQUAL(disks[0].partitions[0].kname, std::string("/dev/sda1"));
        CHECK_EQUAL(disks[0].partitions[0].uuid, std::string("AAAA-1111"));
        CHECK_EQUAL(disks[0].partitions[2].fstype, std::string("ext4"));
        CHECK_EQUAL(disks[0].description(), std::string("Samsung SSD 980 \xc2\xb7 1.0 TB"));
        CHECK_EQUAL(disks[1].kname, std::string("/dev/nvme0n1"));
        // The kernel's p between an nvme disk and its partition number.
        CHECK_EQUAL(disks[1].partitions[0].kname, std::string("/dev/nvme0n1p1"));
        CHECK_EQUAL(disks[1].partitions[0].number, 1);
        CHECK(disks[1].partitions[0].mounted());
    }

    // The same scan through the environment the scripted installs and these
    // tests share.
    setenv("CUTEFISH_INSTALLER_FAKE_LSBLK", canned.c_str(), 1);
    std::string error;
    const std::vector<DiskDevice> scanned = DiskScanner::scan(&error);
    CHECK(error.empty());
    CHECK_EQUAL(scanned.size(), size_t(2));
    unsetenv("CUTEFISH_INSTALLER_FAKE_LSBLK");

    // Not lsblk's own JSON at all.
    std::vector<DiskDevice> none;
    CHECK(!DiskScanner::parseLsblkJson("{ not json", none));
}

// -- the plan ------------------------------------------------------------------------

static InstallPlan goodPlan()
{
    InstallPlan plan;
    plan.setDevice("/dev/sda");
    plan.setMode(InstallPlan::Mode::Erase);
    plan.setSwap("auto");
    plan.setLanguage("en_US");
    plan.setKeyboard("us");
    plan.setTimeZone("America/New_York");
    plan.setFullName("John Doe");
    plan.setUserName("john");
    plan.setPassword("correct horse battery staple");
    plan.setHostName("cutefish-pc");
    plan.setSource(InstallPlan::directorySource(testRoot() + "/source"));
    return plan;
}

static void testPlan()
{
    std::string error;
    InstallPlan plan = goodPlan();
    CHECK(plan.validate(&error));
    CHECK_EQUAL(error, std::string());

    // The plan's JSON round trip: what is written is what is read back.
    const Json json = plan.toJson();
    InstallPlan read;
    CHECK(read.fromJson(json, &error));
    CHECK(read.validate(&error));
    CHECK_EQUAL(read.device(), std::string("/dev/sda"));
    CHECK_EQUAL(read.userName(), std::string("john"));
    CHECK_EQUAL(read.timeZone(), std::string("America/New_York"));
    CHECK_EQUAL(read.source(), std::string("dir:") + testRoot() + "/source");

    // The rules, one by one.
    InstallPlan broken = goodPlan();
    broken.setUserName("Bad User!");
    CHECK(!broken.validate(&error));
    CHECK_EQUAL(error, std::string("the username is not one the system takes"));

    broken = goodPlan();
    broken.setUserName("1john");
    CHECK(!broken.validate(&error));
    CHECK_EQUAL(error, std::string("the username is not one the system takes"));

    broken = goodPlan();
    broken.setHostName("cutefish_pc");
    CHECK(!broken.validate(&error));
    CHECK_EQUAL(error, std::string("the hostname is not one the network takes"));

    broken = goodPlan();
    broken.setPassword("");
    CHECK(!broken.validate(&error));
    CHECK_EQUAL(error, std::string("the account needs a password"));

    broken = goodPlan();
    broken.setLanguage("xx_XX");
    CHECK(!broken.validate(&error));
    CHECK_EQUAL(error, std::string("the language is not one the installer knows"));

    broken = goodPlan();
    broken.setSwap("banana");
    CHECK(!broken.validate(&error));

    broken = goodPlan();
    broken.setSource("ftp://example.com/cutefish");
    CHECK(!broken.validate(&error));
    CHECK_EQUAL(error, std::string("the source of the system's files is not one the installer knows"));

    // The manual layout's own rules, checked against the UEFI way so the
    // rules are the same whatever machine runs the tests.
    InstallPlan manual = goodPlan();
    manual.setMode(InstallPlan::Mode::Manual);
    manual.setFirmwareOverride(true);
    manual.setPartitions({{"/dev/sda3", "/", "ext4", false}});
    CHECK(!manual.validate(&error));
    CHECK_EQUAL(error, std::string("this machine boots the UEFI way: the layout needs a partition at /boot/efi"));
    manual.setPartitions({
        {"/dev/sda1", "/boot/efi", "vfat", false},
        {"/dev/sda3", "/", "ext4", false},
        {"/dev/sda2", "swap", "swap", false},
    });
    CHECK(manual.validate(&error));
    // An ESP that is not FAT, and a mount point the installer never sets up.
    manual.setPartitions({
        {"/dev/sda1", "/boot/efi", "ext4", false},
        {"/dev/sda3", "/", "ext4", false},
    });
    CHECK(!manual.validate(&error));
    manual.setPartitions({
        {"/dev/sda3", "/", "ext4", false},
        {"/dev/sda2", "/var", "ext4", false},
        {"/dev/sda1", "/boot/efi", "vfat", false},
    });
    CHECK(!manual.validate(&error));
    CHECK_EQUAL(error, std::string("the mount point /var is not one the installer sets up"));
}

// -- the layouts ----------------------------------------------------------------------

static void testPartitionPlanner()
{
    // The swap a machine is suggested: a third of its memory, between 2 and
    // 8 GB. A machine of unknown memory is taken for 4 GB.
    CHECK_EQUAL(PartitionPlanner::suggestedSwapBytes(0), uint64_t(2) * 1024 * 1024 * 1024);
    CHECK_EQUAL(PartitionPlanner::suggestedSwapBytes(6ull << 30), uint64_t(2) * 1024 * 1024 * 1024);
    CHECK_EQUAL(PartitionPlanner::suggestedSwapBytes(12ull << 30), uint64_t(4) * 1024 * 1024 * 1024);
    CHECK_EQUAL(PartitionPlanner::suggestedSwapBytes(48ull << 30), uint64_t(8) * 1024 * 1024 * 1024);

    CHECK_EQUAL(PartitionPlanner::partitionPath("/dev/sda", 3), std::string("/dev/sda3"));
    CHECK_EQUAL(PartitionPlanner::partitionPath("/dev/nvme0n1", 12), std::string("/dev/nvme0n1p12"));

    InstallPlan plan = goodPlan();
    plan.setDevice("/dev/nvme0n1");

    // The UEFI layout: the EFI system partition first, the swap after, the
    // root the rest. A machine of 6 GB memory is suggested exactly 2 GB.
    PartitionPlanner::Layout efiLayout = PartitionPlanner::eraseLayout(plan, true, 6ull << 30);
    CHECK_EQUAL(efiLayout.firmwarePartition, std::string("/dev/nvme0n1p1"));
    CHECK(efiLayout.firmwareIsEsp);
    CHECK_EQUAL(efiLayout.firmwareBytes, uint64_t(512) * 1024 * 1024);
    CHECK_EQUAL(efiLayout.swapPartition, std::string("/dev/nvme0n1p2"));
    CHECK_EQUAL(efiLayout.swapBytes, uint64_t(2) * 1024 * 1024 * 1024);
    CHECK_EQUAL(efiLayout.rootPartition, std::string("/dev/nvme0n1p3"));

    std::vector<Command> commands = PartitionPlanner::partitionCommands(plan, efiLayout, true);
    std::string text;
    for (const Command& command : commands)
        text += command.describe() + "\n";
    CHECK(text.find("sgdisk --zap-all /dev/nvme0n1") != std::string::npos);
    CHECK(text.find("-n1:0:+512M") != std::string::npos);
    CHECK(text.find("-t1:ef00") != std::string::npos);
    CHECK(text.find("-t2:8200") != std::string::npos);
    CHECK(text.find("-n3:0:0") != std::string::npos);
    CHECK(text.find("-t3:8300") != std::string::npos);
    CHECK(text.find("partprobe /dev/nvme0n1") != std::string::npos);

    std::vector<Command> formats = PartitionPlanner::formatCommands(plan, efiLayout, true);
    CHECK_EQUAL(formats.size(), size_t(3));
    CHECK_EQUAL(formats[0].program, std::string("mkfs.vfat"));
    CHECK_EQUAL(formats[1].program, std::string("mkswap"));
    CHECK_EQUAL(formats[2].program, std::string("mkfs.ext4"));

    // The BIOS layout: a small BIOS boot partition instead of the ESP.
    PartitionPlanner::Layout biosLayout = PartitionPlanner::eraseLayout(plan, false, 6ull << 30);
    CHECK(!biosLayout.firmwareIsEsp);
    CHECK_EQUAL(biosLayout.firmwareBytes, uint64_t(2) * 1024 * 1024);
    commands = PartitionPlanner::partitionCommands(plan, biosLayout, false);
    text.clear();
    for (const Command& command : commands)
        text += command.describe() + "\n";
    CHECK(text.find("-n1:0:+2M") != std::string::npos);
    CHECK(text.find("-t1:ef02") != std::string::npos);
    formats = PartitionPlanner::formatCommands(plan, biosLayout, false);
    CHECK_EQUAL(formats.size(), size_t(2)); // the BIOS boot partition is never formatted

    // No swap at all.
    InstallPlan noSwap = goodPlan();
    noSwap.setSwap("none");
    PartitionPlanner::Layout none = PartitionPlanner::eraseLayout(noSwap, true, 6ull << 30);
    CHECK(none.swapPartition.empty());
    CHECK_EQUAL(none.rootPartition, std::string("/dev/sda2"));

    // The manual mode's own file systems, made only where asked.
    InstallPlan manual = goodPlan();
    manual.setMode(InstallPlan::Mode::Manual);
    manual.setPartitions({
        {"/dev/sda1", "/boot/efi", "vfat", true},
        {"/dev/sda3", "/", "ext4", false},
        {"/dev/sda2", "swap", "swap", true},
    });
    std::vector<Command> manualFormats = PartitionPlanner::manualFormatCommands(manual);
    CHECK_EQUAL(manualFormats.size(), size_t(2));
    CHECK_EQUAL(manualFormats[0].program, std::string("mkfs.vfat"));
    CHECK_EQUAL(manualFormats[1].program, std::string("mkswap"));
}

// -- the mounts table ------------------------------------------------------------------

static void testFstab()
{
    std::vector<DiskDevice> disks;
    CHECK(DiskScanner::parseLsblkJson(cannedLsblk(), disks));

    PartitionPlanner::Layout layout;
    layout.rootPartition = "/dev/sda3";
    layout.swapPartition = "/dev/sda2";
    const std::string fstab = InstallEngine::generateFstab(layout, true, disks);
    CHECK(fstab.find("UUID=3333-root  /  ext4  rw,noatime  0  1") != std::string::npos);
    CHECK(fstab.find("UUID=2222-swap  none  swap  sw  0  0") != std::string::npos);
    CHECK(fstab.find("/boot/efi") == std::string::npos); // the layout has no ESP

    PartitionPlanner::Layout withEsp = layout;
    withEsp.firmwarePartition = "/dev/sda1";
    withEsp.firmwareIsEsp = true;
    const std::string uefiFstab = InstallEngine::generateFstab(withEsp, true, disks);
    CHECK(uefiFstab.find("UUID=AAAA-1111  /boot/efi  vfat  rw,umask=0077  0  2") != std::string::npos);

    // A partition the scan does not know falls back to its own name.
    PartitionPlanner::Layout unknown = layout;
    unknown.rootPartition = "/dev/vdz9";
    const std::string fallback = InstallEngine::generateFstab(unknown, false, disks);
    CHECK(fallback.find("/dev/vdz9  /  ext4") != std::string::npos);

    const std::string manual = InstallEngine::generateManualFstab(
        {
            {"/dev/sda1", "/boot/efi", "vfat", false},
            {"/dev/sda3", "/", "ext4", false},
            {"/dev/sda2", "swap", "swap", false},
        },
        disks);
    CHECK(manual.find("UUID=AAAA-1111  /boot/efi") != std::string::npos);
    CHECK(manual.find("UUID=3333-root  /") != std::string::npos);
    CHECK(manual.find("UUID=2222-swap  none  swap") != std::string::npos);
}

// -- the dry run ------------------------------------------------------------------------

static void testDryRun(bool dryRun, InstallPlan& plan, std::vector<Json>& events)
{
    InstallEngine::Hooks hooks;
    hooks.event = [&events](const Json& event) { events.push_back(event); };
    InstallEngine engine(std::move(hooks));
    const InstallEngine::Outcome outcome = engine.run(plan, dryRun);
    CHECK_EQUAL(int(outcome), int(InstallEngine::Outcome::Finished));
    if (outcome != InstallEngine::Outcome::Finished) {
        std::fprintf(stderr, "    the dry run failed at '%s': %s\n", engine.failedStep().c_str(),
                     engine.error().c_str());
        return;
    }
    CHECK(engine.error().empty());

    // The steps, in the order the installation walks them.
    std::vector<std::string> steps;
    double lastPercent = -1;
    bool sawFinished = false;
    for (const Json& event : events) {
        const std::string name = event.get("event").toString();
        if (name == "step")
            steps.push_back(event.get("id").toString());
        else if (name == "progress") {
            const double percent = event.get("percent").toNumber();
            CHECK(percent >= lastPercent);
            lastPercent = percent;
        } else if (name == "finished")
            sawFinished = true;
    }
    const std::vector<std::string> wanted = InstallEngine::stepOrder();
    CHECK_EQUAL(steps.size(), wanted.size());
    for (size_t i = 0; i < steps.size() && i < wanted.size(); ++i)
        CHECK_EQUAL(steps[i], wanted[i]);
    CHECK(sawFinished);
    CHECK(lastPercent >= 99.9);

    // What the run would have done, checked as a whole: every command named
    // in the log, none of them run — the tools are not even installed here,
    // so a command run by mistake would have failed the run above.
    std::string log;
    for (const std::string& line : engine.commandLog())
        log += line + "\n";
    CHECK(log.find("wipefs -a /dev/sda") != std::string::npos);
    CHECK(log.find("sgdisk --zap-all /dev/sda") != std::string::npos);
    CHECK(log.find("partprobe /dev/sda") != std::string::npos);
    CHECK(log.find("mkfs.ext4 -F -L cutefish /dev/sda3") != std::string::npos);
    CHECK(log.find("mount /dev/sda3 /mnt/target") != std::string::npos);
    CHECK(log.find("rsync -aHAXx") != std::string::npos);
    CHECK(log.find("write /mnt/target/etc/fstab") != std::string::npos);
    CHECK(log.find("write /mnt/target/etc/locale.conf") != std::string::npos);
    CHECK(log.find("write /mnt/target/etc/vconsole.conf") != std::string::npos);
    CHECK(log.find("write /mnt/target/etc/hostname") != std::string::npos);
    CHECK(log.find("write /mnt/target/etc/cutefish/privacy.conf") != std::string::npos);
    CHECK(log.find("chroot /mnt/target useradd -m") != std::string::npos);
    CHECK(log.find("chroot /mnt/target chpasswd < input") != std::string::npos);
    CHECK(log.find("grub-install") != std::string::npos);
    CHECK(log.find("grub-mkconfig -o /boot/grub/grub.cfg") != std::string::npos);
    CHECK(log.find("ln -sf /usr/share/zoneinfo/America/New_York /mnt/target/etc/localtime")
          != std::string::npos);
    // The user's password never reaches the log.
    CHECK(log.find("correct horse") == std::string::npos);
    // Nothing was mounted: the dry run leaves the machine as it was.
    CHECK(log.find("umount -R /mnt/target") == std::string::npos);
}

static void testManualDryRun(InstallPlan& plan, std::vector<Json>& events)
{
    InstallEngine::Hooks hooks;
    hooks.event = [&events](const Json& event) { events.push_back(event); };
    InstallEngine engine(std::move(hooks));
    const InstallEngine::Outcome outcome = engine.run(plan, true);
    CHECK_EQUAL(int(outcome), int(InstallEngine::Outcome::Finished));
    std::string log;
    for (const std::string& line : engine.commandLog())
        log += line + "\n";
    // The manual layout touches no partition table.
    CHECK(log.find("sgdisk") == std::string::npos);
    CHECK(log.find("wipefs") == std::string::npos);
    // The chosen partitions are mounted where they were told.
    CHECK(log.find("mount /dev/sda3 /mnt/target") != std::string::npos);
    CHECK(log.find("mount /dev/sda1 /mnt/target/boot/efi") != std::string::npos);
    CHECK(log.find("swapon /dev/sda2") != std::string::npos);
    // Only the partitions asked to format are formatted.
    CHECK(log.find("mkfs.vfat") != std::string::npos);
    CHECK(log.find("mkfs.ext4") == std::string::npos);
    CHECK(log.find("mkswap") == std::string::npos);
}

// -- the command-line tool ----------------------------------------------------------------

// Runs the command-line tool with the canned disk scan, and returns its
// exit code with its output.
static int runCli(const char* cliPath, const std::vector<std::string>& arguments, std::string* output)
{
    std::string line = "env CUTEFISH_INSTALLER_FAKE_LSBLK=";
    line += testRoot() + "/lsblk.json ";
    line += cliPath;
    for (const std::string& argument : arguments)
        line += " '" + argument + "'";
    line += " > " + testRoot() + "/cli.out 2> " + testRoot() + "/cli.err";
    const int code = std::system(line.c_str());
    if (output) {
        readFile(testRoot() + "/cli.out", *output);
        std::string errors;
        readFile(testRoot() + "/cli.err", errors);
        if (!errors.empty())
            *output += errors;
    }
    return WEXITSTATUS(code);
}

static void testCli(const char* cliPath)
{
    if (!cliPath) {
        std::fprintf(stderr, "note: no command-line tool given; its own tests are skipped\n");
        return;
    }

    // The disks, as JSON.
    std::string output;
    int code = runCli(cliPath, {"disks", "--json"}, &output);
    CHECK_EQUAL(code, 0);
    Json disks;
    CHECK(Json::parse(output, disks));
    CHECK(disks.isArray());
    CHECK_EQUAL(disks.size(), size_t(2));
    CHECK_EQUAL(disks.at(0).get("device").toString(), std::string("/dev/sda"));

    // The tables.
    code = runCli(cliPath, {"locales"}, &output);
    CHECK_EQUAL(code, 0);
    CHECK(output.find("en_US") != std::string::npos);
    code = runCli(cliPath, {"layouts", "--json"}, &output);
    CHECK_EQUAL(code, 0);
    CHECK(output.find("\"us\"") != std::string::npos);

    // The plan's JSON, checked.
    code = runCli(cliPath, {"validate", "--config", testRoot() + "/plan.json"}, &output);
    CHECK_EQUAL(code, 0);
    Json plan;
    CHECK(Json::parse(output, plan));
    CHECK_EQUAL(plan.get("device").toString(), std::string("/dev/sda"));

    InstallPlan broken = goodPlan();
    broken.setUserName("not valid");
    writeFile(testRoot() + "/broken-plan.json", broken.toJson().dump(true));
    code = runCli(cliPath, {"validate", "--config", testRoot() + "/broken-plan.json"}, &output);
    CHECK_EQUAL(code, 1);

    // The installation, walked through as a dry run with the events the
    // window reads: one JSON object a line, ending in finished.
    code = runCli(cliPath,
                  {"install", "--config", testRoot() + "/plan.json", "--dry-run",
                   "--progress-events"},
                  &output);
    CHECK_EQUAL(code, 0);
    std::vector<Json> events;
    size_t at = 0;
    int lines = 0;
    while (at < output.size()) {
        size_t end = output.find('\n', at);
        std::string line = output.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? output.size() : end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty())
            continue;
        Json event;
        CHECK(Json::parse(line, event));
        CHECK(event.contains("event"));
        events.push_back(event);
        ++lines;
    }
    CHECK(lines > 30);
    bool sawStart = false, sawFinished = false;
    std::vector<std::string> steps;
    for (const Json& event : events) {
        const std::string name = event.get("event").toString();
        if (name == "start")
            sawStart = true;
        else if (name == "step")
            steps.push_back(event.get("id").toString());
        else if (name == "finished")
            sawFinished = true;
    }
    CHECK(sawStart);
    CHECK(sawFinished);
    CHECK_EQUAL(steps.size(), InstallEngine::stepOrder().size());
    for (size_t i = 0; i < steps.size() && i < InstallEngine::stepOrder().size(); ++i)
        CHECK_EQUAL(steps[i], InstallEngine::stepOrder()[i]);

    // An install without a plan is not started at all.
    code = runCli(cliPath, {"install"}, &output);
    CHECK_EQUAL(code, 3);
}

// -- main -----------------------------------------------------------------------------

int main(int argc, char** argv)
{
    const char* cliPath = argc > 1 ? argv[1] : nullptr;

    makeDirectories(testRoot());
    makeDirectories(testRoot() + "/source/etc");
    writeFile(testRoot() + "/source/etc/os-release", "NAME=\"Cutefish OS\"\n");

    testTables();
    testNameRules();
    testDiskScan();
    // The canned scan stays set for the dry runs below: the engine reads the
    // disks the same way, wherever its work is started from.
    setenv("CUTEFISH_INSTALLER_FAKE_LSBLK", (testRoot() + "/lsblk.json").c_str(), 1);
    testPlan();
    testPartitionPlanner();
    testFstab();

    // The plan the dry runs and the command-line tests share.
    InstallPlan plan = goodPlan();
    writeFile(testRoot() + "/plan.json", plan.toJson().dump(true));

    std::vector<Json> events;
    testDryRun(true, plan, events);

    InstallPlan manual = goodPlan();
    manual.setMode(InstallPlan::Mode::Manual);
    manual.setPartitions({
        {"/dev/sda1", "/boot/efi", "vfat", true},
        {"/dev/sda3", "/", "ext4", false},
        {"/dev/sda2", "swap", "swap", false},
    });
    std::vector<Json> manualEvents;
    testManualDryRun(manual, manualEvents);

    testCli(cliPath);

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed == 0)
        std::printf("all engine tests passed\n");
    return g_failed == 0 ? 0 : 1;
}
