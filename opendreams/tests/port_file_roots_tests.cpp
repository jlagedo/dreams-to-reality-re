#include "port/file_roots.h"

#include <cstring>
#include <iostream>

int main() {
    od::port::FileRootState roots;
    roots.disc_root = "D:\\";
    roots.install_root = "C:\\CRYO\\DREAMS\\";

    if (std::strcmp(od::port::FILE_GetInstallRoot(roots), "C:\\CRYO\\DREAMS\\") != 0 ||
        std::strcmp(od::port::FILE_GetDataRoot(roots), "D:\\") != 0) {
        std::cerr << "retail CD and install roots differ before FULL.ID\n";
        return 1;
    }
    roots.full_install = true;
    if (od::port::FILE_GetDataRoot(roots) != od::port::FILE_GetInstallRoot(roots)) {
        std::cerr << "FULL.ID must select the same install-root buffer\n";
        return 1;
    }
    roots.install_root = "E:\\CRYO\\DREAMS\\";
    if (std::strcmp(od::port::FILE_GetDataRoot(roots), "E:\\CRYO\\DREAMS\\") != 0) {
        std::cerr << "the getters must read the current install-root state\n";
        return 1;
    }
    return 0;
}
