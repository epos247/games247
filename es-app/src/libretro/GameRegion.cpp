//  SPDX-License-Identifier: MIT
//
//  games247
//  GameRegion.cpp
//
//  The region of a game from the tags in its file name, see GameRegion.h.
//

#include "libretro/GameRegion.h"

#include "utils/StringUtil.h"

#include <algorithm>
#include <map>

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
