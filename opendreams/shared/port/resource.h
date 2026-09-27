#pragma once

#include "port/model.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

class DanArchive;
class VfsContext;
struct DsnTexturePage;

// The first adapted RES source is an active DAN archive. DSN and loose-file
// routes remain explicit future branches of the same retail entry points.
bool RES_ReadFile(DanArchive& archive, std::string_view logical_name,
                  std::vector<uint8_t>& bytes, std::string& error);
// Retail's no-container physical-file branch: skip two u32 header words and
// return the remaining body. The selected VFS context supplies the source.
bool RES_ReadFile(VfsContext& vfs, std::string_view physical_path,
                  std::vector<uint8_t>& bytes, std::string& error);
bool MDL_BindFaceMaterials(ModelGraph& graph, std::string& error);
bool MDL_BindTreeMaterials(ModelGraph& graph, std::string& error);
bool MDL_LoadMaterials(DanArchive& archive, ModelGraph& graph, std::string& error);
bool MDL_LoadSceneMaterials(const std::vector<DsnTexturePage>& pages,
                            ModelGraph& graph, std::vector<std::string>& missing,
                            std::string& error);
bool RES_Load(DanArchive& archive, std::string_view logical_name,
              ModelGraph& graph, std::string& error);
bool RES_Load(VfsContext& vfs, std::string_view physical_path,
              ModelGraph& graph, std::string& error);

} // namespace od::port
