// SPDX-FileCopyrightText: 2026 The Cutefish Installer Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "core/tables.h"

#include <cstring>
#include <sstream>

namespace cutefish::installer {

const std::vector<Language>& languages()
{
    static const std::vector<Language> table = {
        {"en_US", "English", "United States", "English"},
        {"en_GB", "English", "United Kingdom", "English (UK)"},
        {"zh_CN", "Chinese (Simplified)", "China", "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87"},
        {"zh_TW", "Chinese (Traditional)", "Taiwan", "\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87"},
        {"ja_JP", "Japanese", "Japan", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},
        {"ko_KR", "Korean", "Korea", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"},
        {"de_DE", "German", "Germany", "Deutsch"},
        {"fr_FR", "French", "France", "Fran\xe7""ais"},
        {"es_ES", "Spanish", "Spain", "Espa\xc3\xb1""ol"},
        {"pt_BR", "Portuguese", "Brazil", "Portugu\xc3\xa""s (Brasil)"},
        {"ru_RU", "Russian", "Russia", "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"},
        {"it_IT", "Italian", "Italy", "Italiano"},
        {"nl_NL", "Dutch", "Netherlands", "Nederlands"},
        {"pl_PL", "Polish", "Poland", "Polski"},
        {"tr_TR", "Turkish", "Turkey", "T\xc3\xbcrk\xc3\xa7""e"},
        {"ar_EG", "Arabic", "Egypt", "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9"},
        {"hi_IN", "Hindi", "India", "\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80"},
        {"id_ID", "Indonesian", "Indonesia", "Bahasa Indonesia"},
        {"th_TH", "Thai", "Thailand", "\xe0\xb9\x84\xe0\xb8\x97\xe0\xb8\xa2"},
        {"vi_VN", "Vietnamese", "Vietnam", "Ti\xe1\xba\xbfng Vi\xe1\xbb\x87""t"},
    };
    return table;
}

const std::vector<KeyboardLayout>& keyboardLayouts()
{
    static const std::vector<KeyboardLayout> table = {
        {"us", "English (US)", "QWERTY"},
        {"gb", "English (UK)", "QWERTY"},
        {"de", "German", "QWERTZ"},
        {"de-ch", "German (Switzerland)", "QWERTZ"},
        {"fr", "French", "AZERTY"},
        {"be", "Belgian", "AZERTY"},
        {"es", "Spanish", "QWERTY"},
        {"latam", "Spanish (Latin America)", "QWERTY"},
        {"it", "Italian", "QWERTY"},
        {"pt-br", "Portuguese (Brazil)", "QWERTY, ABNT2"},
        {"ru", "Russian", "\xd0\x99\xd0\xa6\xd0\xa3\xd0\x9a\xd0\x95\xd0\x9d"},
        {"ua", "Ukrainian", "\xd0\x99\xd0\xa6\xd0\xa3\xd0\x9a\xd0\x95\xd0\x9d"},
        {"jp", "Japanese", "JIS"},
        {"cn", "Chinese", "Pinyin"},
        {"kr", "Korean", "Dubeolsik"},
        {"tr", "Turkish", "QWERTY, T"},
        {"ara", "Arabic", "AZERTY-like"},
        {"in", "Hindi", "InScript"},
        {"th", "Thai", "Kedmanee"},
        {"vn", "Vietnamese", "Telex"},
    };
    return table;
}

const std::vector<Zone>& timeZones()
{
    static const std::vector<Zone> table = {
        {"Africa", "Cairo"}, {"Africa", "Johannesburg"}, {"Africa", "Lagos"}, {"Africa", "Nairobi"},
        {"America", "Anchorage"}, {"America", "Argentina/Buenos_Aires"}, {"America", "Bogota"},
        {"America", "Caracas"}, {"America", "Chicago"}, {"America", "Denver"}, {"America", "Halifax"},
        {"America", "Lima"}, {"America", "Los_Angeles"}, {"America", "Mexico_City"}, {"America", "New_York"},
        {"America", "Phoenix"}, {"America", "Sao_Paulo"}, {"America", "Santiago"}, {"America", "Toronto"},
        {"America", "Vancouver"}, {"Asia", "Bangkok"}, {"Asia", "Dubai"}, {"Asia", "Hong_Kong"},
        {"Asia", "Jakarta"}, {"Asia", "Jerusalem"}, {"Asia", "Kolkata"}, {"Asia", "Kuala_Lumpur"},
        {"Asia", "Manila"}, {"Asia", "Riyadh"}, {"Asia", "Seoul"}, {"Asia", "Shanghai"}, {"Asia", "Singapore"},
        {"Asia", "Taipei"}, {"Asia", "Tehran"}, {"Asia", "Tokyo"}, {"Australia", "Adelaide"},
        {"Australia", "Brisbane"}, {"Australia", "Melbourne"}, {"Australia", "Perth"}, {"Australia", "Sydney"},
        {"Europe", "Amsterdam"}, {"Europe", "Athens"}, {"Europe", "Berlin"}, {"Europe", "Brussels"},
        {"Europe", "Copenhagen"}, {"Europe", "Helsinki"}, {"Europe", "Istanbul"}, {"Europe", "Lisbon"},
        {"Europe", "London"}, {"Europe", "Madrid"}, {"Europe", "Moscow"}, {"Europe", "Oslo"}, {"Europe", "Paris"},
        {"Europe", "Prague"}, {"Europe", "Rome"}, {"Europe", "Stockholm"}, {"Europe", "Vienna"},
        {"Europe", "Warsaw"}, {"Europe", "Zurich"}, {"Pacific", "Auckland"}, {"UTC", "UTC"},
    };
    return table;
}

std::string zonePath(const Zone& zone)
{
    std::string path = zone.area;
    if (!std::strcmp(zone.area, "UTC"))
        return path;
    path += '/';
    path += zone.region;
    return path;
}

bool knownZone(const std::string& path)
{
    for (const Zone& zone : timeZones()) {
        if (zonePath(zone) == path)
            return true;
    }
    return false;
}

bool knownLanguage(const std::string& code)
{
    return findLanguage(code) != nullptr;
}

const Language* findLanguage(const std::string& code)
{
    for (const Language& language : languages()) {
        if (code == language.code)
            return &language;
    }
    return nullptr;
}

bool knownKeyboard(const std::string& code)
{
    return findKeyboard(code) != nullptr;
}

const KeyboardLayout* findKeyboard(const std::string& code)
{
    for (const KeyboardLayout& layout : keyboardLayouts()) {
        if (code == layout.code)
            return &layout;
    }
    return nullptr;
}

std::string languageLabel(const std::string& code)
{
    const Language* language = findLanguage(code);
    return language ? language->name : code;
}

std::string keyboardLabel(const std::string& code)
{
    const KeyboardLayout* layout = findKeyboard(code);
    return layout ? layout->name : code;
}

} // namespace cutefish::installer
