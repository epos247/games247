//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  MameNames.h
//
//  Provides expanded game names based on short MAME name arguments. Also contains
//  functions to check whether a passed argument is a MAME BIOS or a MAME device.
//  The data sources are stored as the files mamebioses.xml, mamedevices.xml and mamenames.xml.
//

#ifndef ES_CORE_MAMENAMES_H
#define ES_CORE_MAMENAMES_H

#include "Settings.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

// Expand MAME names to full game names and lookup device and BIOS entries.
class MameNames
{
public:
    static MameNames& getInstance();

    std::string getRealName(const std::string& mameName)
    {
        std::string name {mNamePairs[mameName]};
        if (name == "")
            return mameName;
        else
            return name;
    }

    std::string getCleanName(const std::string& mameName)
    {
        static const bool stripInfo {Settings::getInstance()->getBool("MAMENameStripExtraInfo")};
        if (stripInfo)
            return Utils::String::removeParenthesis(getRealName(mameName));
        else
            return getRealName(mameName);
    }

    // games247: the parent set of a clone ("sf2" for "sf2ua"), or an empty string for a
    // parent or a name that isn't in the list.
    std::string getParent(const std::string& mameName) const
    {
        const auto it = mParents.find(mameName);
        return it == mParents.cend() ? std::string {} : it->second;
    }

    const bool isBios(const std::string& biosName)
    {
        return std::find(mMameBioses.cbegin(), mMameBioses.cend(), biosName) != mMameBioses.cend();
    }

    const bool isDevice(const std::string& deviceName)
    {
        return std::find(mMameDevices.cbegin(), mMameDevices.cend(), deviceName) !=
               mMameDevices.cend();
    }

private:
    MameNames();

    std::unordered_map<std::string, std::string> mNamePairs;
    std::unordered_map<std::string, std::string> mParents;
    std::vector<std::string> mMameBioses;
    std::vector<std::string> mMameDevices;
};

#endif // ES_CORE_MAMENAMES_H
