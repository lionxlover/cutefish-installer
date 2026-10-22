// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>
#include <vector>

namespace cutefish::installer {

// The tables the pages and the engine share: a language is a locale that
// /etc/locale.conf takes; a keyboard is a layout the console and the session
// both take; a zone is a region/area name the zoneinfo tree knows. They are
// built in, so the pages can offer their choices before anything is mounted
// and the engine can write what was picked without guessing.

struct Language
{
    const char* code;      // "en_US"
    const char* name;      // "English"
    const char* territory; // "United States"
    const char* native;    // "English (United States)"
};

struct KeyboardLayout
{
    const char* code; // "us"
    const char* name; // "English (US)"
    const char* hint; // "QWERTY"
};

struct Zone
{
    const char* area;    // "America"
    const char* region;  // "New_York"
};

const std::vector<Language>& languages();
const std::vector<KeyboardLayout>& keyboardLayouts();
// The whole zoneinfo tree's areas, in the order the tree keeps them.
const std::vector<Zone>& timeZones();

// "America/New_York" from a Zone; "" when the pair is not of the tree.
std::string zonePath(const Zone& zone);
// True when `path` names a zone the tree knows, as "America/New_York".
bool knownZone(const std::string& path);

// True when the locale code is one of the table's.
bool knownLanguage(const std::string& code);
const Language* findLanguage(const std::string& code);
// True when the layout code is one of the table's.
bool knownKeyboard(const std::string& code);
const KeyboardLayout* findKeyboard(const std::string& code);

// The words the summary shows for a locale code: "English (United States)",
// or the code itself when the table does not know it.
std::string languageLabel(const std::string& code);
std::string keyboardLabel(const std::string& code);

} // namespace cutefish::installer
