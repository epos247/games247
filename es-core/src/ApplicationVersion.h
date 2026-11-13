//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  ApplicationVersion.h
//

#ifndef ES_CORE_APPLICATION_VERSION_H
#define ES_CORE_APPLICATION_VERSION_H

// These numbers and strings need to be manually updated for a new version.
// Do this version number update as the very last commit for the new release version.
// clang-format off
#define PROGRAM_VERSION_MAJOR        4
#define PROGRAM_VERSION_MINOR        0
#define PROGRAM_VERSION_MAINTENANCE  0
// clang-format on

// games247: release builds made by the GitHub workflow pass their own build number and version
// (from the git tag). The updater offers a release whose build number is higher than this one.
#if defined(GAMES247_BUILD_NUMBER)
#define PROGRAM_RELEASE_NUMBER GAMES247_BUILD_NUMBER
#else
#define PROGRAM_RELEASE_NUMBER 53
#endif

#if defined(GAMES247_VERSION_STRING)
#define PROGRAM_VERSION_STRING GAMES247_VERSION_STRING
#else
#define PROGRAM_VERSION_STRING "4.0.0-alpha"
#endif

#define PROGRAM_BUILT_STRING __DATE__ " - " __TIME__

#define RESOURCE_VERSION_STRING "4,0,0\0"
#define RESOURCE_VERSION PROGRAM_VERSION_MAJOR, PROGRAM_VERSION_MINOR, PROGRAM_VERSION_MAINTENANCE

#endif // ES_CORE_APPLICATION_VERSION_H
