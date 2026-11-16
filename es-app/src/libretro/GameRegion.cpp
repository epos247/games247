//  SPDX-License-Identifier: MIT
//
//  games247
//  GameRegion.cpp
//
//  The region of a game from the tags in its file name, see GameRegion.h.
//

#include "libretro/GameRegion.h"

#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

std::vector<GameRegion> regionsInFileName(const std::string& fileName)
{
    static const std::map<std::string, GameRegion> tags {
        {"usa", GameRegion::USA},          {"us", GameRegion::USA},
        {"u", GameRegion::USA},            {"ntsc-u", GameRegion::USA},
        {"america", GameRegion::USA},      {"canada", GameRegion::USA},
        {"europe", GameRegion::EUROPE},    {"e", GameRegion::EUROPE},
        {"eu", GameRegion::EUROPE},        {"pal", GameRegion::EUROPE},
        {"uk", GameRegion::EUROPE},        {"germany", GameRegion::EUROPE},
        {"france", GameRegion::EUROPE},    {"spain", GameRegion::EUROPE},
        {"italy", GameRegion::EUROPE},     {"netherlands", GameRegion::EUROPE},
        {"sweden", GameRegion::EUROPE},    {"scandinavia", GameRegion::EUROPE},
        {"australia", GameRegion::EUROPE}, {"japan", GameRegion::JAPAN},
        {"j", GameRegion::JAPAN},          {"jp", GameRegion::JAPAN},
        {"ntsc-j", GameRegion::JAPAN},     {"asia", GameRegion::JAPAN}};

    std::vector<GameRegion> regions;
    const std::string lower {Utils::String::toLower(fileName)};
    size_t open {lower.find('(')};
    while (open != std::string::npos) {
        const size_t close {lower.find(')', open)};
        if (close == std::string::npos)
            break;
        for (const std::string& part : Utils::String::delimitedStringToVector(
                 lower.substr(open + 1, close - open - 1), ",")) {
            const auto it = tags.find(Utils::String::trim(part));
            if (it != tags.end() &&
                std::find(regions.begin(), regions.end(), it->second) == regions.end())
                regions.emplace_back(it->second);
        }
        open = lower.find('(', close);
    }
    return regions;
}

namespace
{
    // Whether a round-bracket tag (lower case, without the brackets) only names regions,
    // languages or a revision: "e", "usa, europe", "en,fr,de", "m5", "rev 1", "v1.1".
    bool isVersionTag(const std::string& tag)
    {
        static const std::set<std::string> names {
            // Regions, by name and by GoodTools/No-Intro code.
            "usa", "us", "u", "america", "canada", "europe", "eu", "e", "uk", "pal", "ntsc",
            "ntsc-u", "ntsc-j", "japan", "jp", "j", "world", "w", "ue", "ju", "jue", "eu",
            "asia", "korea", "k", "australia", "a", "brazil", "b", "china", "c", "taiwan",
            "hong kong", "hk", "russia", "germany", "g", "france", "f", "spain", "s", "italy",
            "i", "netherlands", "nl", "sweden", "sw", "scandinavia", "unknown",
            // Languages.
            "en", "fr", "de", "es", "it", "pt", "sv", "no", "da", "fi", "ja", "zh", "ko",
            "pl", "ru", "ca", "el", "tr", "cs", "hu"};
        const std::string trimmed {Utils::String::trim(tag)};
        if (trimmed.empty())
            return false;
        // A revision or version number.
        for (const std::string& prefix : {std::string {"rev "}, std::string {"rev"},
                                          std::string {"version "}, std::string {"v"}}) {
            if (trimmed.rfind(prefix, 0) == 0 && trimmed.size() > prefix.size()) {
                const std::string rest {trimmed.substr(prefix.size())};
                if (prefix == "v" || prefix == "rev") {
                    // "v1.1", "rev1": a digit straight after.
                    if (std::isdigit(static_cast<unsigned char>(rest.front())))
                        return true;
                }
                else if (rest.find(' ') == std::string::npos) {
                    return true;
                }
            }
        }
        // Multi-language: "m3".
        if (trimmed.size() >= 2 && trimmed.front() == 'm' &&
            std::all_of(trimmed.begin() + 1, trimmed.end(),
                        [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            return true;
        for (const std::string& part : Utils::String::delimitedStringToVector(trimmed, ",")) {
            std::string name {Utils::String::trim(part)};
            // "En+Fr"
            for (const std::string& piece : Utils::String::delimitedStringToVector(name, "+")) {
                if (names.find(Utils::String::trim(piece)) == names.end())
                    return false;
            }
        }
        return true;
    }

    // The regions the person playing most likely wants, best first.
    const std::vector<GameRegion>& preferredRegions()
    {
        static const std::vector<GameRegion> regions {[] {
            const std::string locale {Utils::Localization::sCurrentLocale};
            if (locale == "en_US" || locale == "en_CA" || locale == "es_MX" ||
                locale == "pt_BR")
                return std::vector<GameRegion> {GameRegion::USA, GameRegion::EUROPE,
                                                GameRegion::JAPAN};
            if (locale.rfind("ja", 0) == 0)
                return std::vector<GameRegion> {GameRegion::JAPAN, GameRegion::USA,
                                                GameRegion::EUROPE};
            return std::vector<GameRegion> {GameRegion::EUROPE, GameRegion::USA,
                                            GameRegion::JAPAN};
        }()};
        return regions;
    }
} // namespace

std::string gameWithoutVersionTags(const std::string& fileName)
{
    const std::string lower {Utils::String::toLower(fileName)};
    std::string game;
    size_t pos {0};
    while (pos < lower.size()) {
        const char c {lower[pos]};
        if (c == '(' || c == '[') {
            const size_t close {lower.find(c == '(' ? ')' : ']', pos)};
            if (close == std::string::npos) {
                game += lower.substr(pos);
                break;
            }
            if (c == '(' && !isVersionTag(lower.substr(pos + 1, close - pos - 1)))
                game += lower.substr(pos, close - pos + 1);
            pos = close + 1;
            continue;
        }
        game += c;
        ++pos;
    }
    // One space between words, none at the ends.
    std::string tidy;
    for (const char c : game) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!tidy.empty() && tidy.back() != ' ')
                tidy += ' ';
        }
        else {
            tidy += c;
        }
    }
    return Utils::String::trim(tidy);
}

int versionPreference(const std::string& fileName)
{
    const std::string lower {Utils::String::toLower(fileName)};
    const std::vector<GameRegion> regions {regionsInFileName(fileName)};
    const std::vector<GameRegion>& preferred {preferredRegions()};
    const bool world {lower.find("(world") != std::string::npos ||
                      lower.find("(w)") != std::string::npos};

    int score {0};
    if (!regions.empty() &&
        std::find(regions.begin(), regions.end(), preferred[0]) != regions.end())
        score = 0;
    else if (world)
        score = 1;
    else if (!regions.empty() &&
             std::find(regions.begin(), regions.end(), preferred[1]) != regions.end())
        score = 2;
    else if (!regions.empty())
        score = 3;
    else
        score = 4;

    // Square-bracket tags other than [!]: bad dumps, hacks, trainers, alternates...
    size_t open {lower.find('[')};
    while (open != std::string::npos) {
        if (lower.compare(open, 3, "[!]") != 0) {
            score += 10;
            break;
        }
        open = lower.find('[', open + 1);
    }
    return score;
}
