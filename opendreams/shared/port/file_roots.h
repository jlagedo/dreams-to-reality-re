#pragma once

#include <string>

namespace od::port {

// Retail keeps these paths and the FULL.ID result in mutable globals. Keeping
// them in one caller-owned state lets each selected disc have its own roots.
struct FileRootState {
    std::string disc_root = "X:\\";
    std::string install_root = "X:\\CRYO\\DREAMS\\";
    bool full_install = false;
};

// Adapted from WINDREAM.EXE 0x004285be and 0x004285e9. Returned pointers stay
// valid until the corresponding root string is modified or FileRootState dies.
const char* FILE_GetInstallRoot(const FileRootState& state);
const char* FILE_GetDataRoot(const FileRootState& state);

} // namespace od::port
