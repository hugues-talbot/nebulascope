#pragma once
//
// BuildInfo — build-time provenance, distinct from the user-maintained
// AppInfo.h. gitDescribe() returns the exact `git describe` of the tree the
// binary was built from ("v0.92-7-ga6a8118", "-dirty" when uncommitted;
// "unknown" outside a git checkout).
//
namespace appbuild {
const char* gitDescribe();
}
