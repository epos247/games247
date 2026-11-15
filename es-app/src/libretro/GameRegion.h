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

#endif // GAMES247_GAME_REGION_H
