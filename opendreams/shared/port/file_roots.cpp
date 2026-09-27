#include "port/file_roots.h"

namespace od::port {

const char* FILE_GetInstallRoot(const FileRootState& state) {
    return state.install_root.c_str();
}

const char* FILE_GetDataRoot(const FileRootState& state) {
    return state.full_install ? FILE_GetInstallRoot(state) : state.disc_root.c_str();
}

} // namespace od::port
