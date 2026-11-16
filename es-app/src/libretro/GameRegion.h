//  SPDX-License-Identifier: MIT
//
//  games247
//  GameRegion.h
//
//  The region of a game from the tags in its file name, for cores that need a BIOS of the
//  game's region (PlayStation, Saturn).
//

#ifndef GAMES247_GAME_REGION_H
#define GAMES247_GAME_REGION_H

#include <string>
#include <vector>

enum class GameRegion { USA, EUROPE, JAPAN };

// Regions from the bracketed tags of a file name, in the order they appear:
// "Final Fantasy VII (Europe) (Disc 1).chd" gives Europe, "Ridge Racer (USA, Europe).cue"
// USA then Europe. Empty when the name doesn't say.
std::vector<GameRegion> regionsInFileName(const std::string& fileName);

// The game a file name is a version of, for grouping the regional versions of a game:
// region, language and revision tags in round brackets and every tag in square brackets are
// left out, other tags kept, and it's lower case. "Sonic the Hedgehog (E) [!]" and
// "Sonic The Hedgehog (USA, Europe) (Rev 1)" both give "sonic the hedgehog", while
// "Final Fantasy VII (Europe) (Disc 1)" gives "final fantasy vii (disc 1)".
std::string gameWithoutVersionTags(const std::string& fileName);

// How well a version suits the person playing, lower first: their own region (from the
// language games247 runs in: Europe, unless it's American English or Japanese), then
// world-wide releases, then the other regions, then untagged ones. Bad dumps, hacks and
// other square-bracket tags (apart from [!]) go after the rest.
int versionPreference(const std::string& fileName);

#endif // GAMES247_GAME_REGION_H
