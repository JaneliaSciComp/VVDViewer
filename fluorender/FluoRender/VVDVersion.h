#ifndef VVD_VERSION_H
#define VVD_VERSION_H

// Release version of VVDViewer, the one number to bump for a release (the
// release tag is the same number): window titles and About boxes of the wx and
// Qt apps, the macOS bundle version (CMakeLists.txt reads this line) and the
// packages (package.sh reads it too). The project file format keeps its own
// version, VERSION_MAJOR / VERSION_MINOR in CMakeLists.txt.
#define VVD_VERSION "2.0.0"

#endif // VVD_VERSION_H
