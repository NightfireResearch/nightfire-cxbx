#ifndef COMMON_VERSION_H_
#define COMMON_VERSION_H_

// The build's version, from git describe or the release job's tag (cmake/version.cmake): "0.4.0" for a release,
// "0.4.0+12.gabc1234" for a build past one. The standalone executables print it at start and in each crash record;
// the launcher compares it with the releases' (docs/install-and-update.md).
extern const char NightfireVersion[];

#endif // COMMON_VERSION_H_
