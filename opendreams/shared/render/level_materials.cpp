#include "render/level_materials.h"

namespace od {

void sync_level_materials(port::LevelMaterialSession& session, ModelPreview& preview) {
    if (!session.active()) return;
    auto& materials = session.materials();
    for (size_t material = 0; material < materials.size(); ++material) {
        const uint32_t rows = session.take_dirty_rows(material);
        if (rows) preview.update_palette_rows(material, rows, materials[material].bank);
        if (session.take_dirty_pixels(material))
            preview.update_material_pixels(material, materials[material].bank);
    }
    preview.set_fog(session.fog());
    preview.set_output_gamma(glide_output_gamma);
}

} // namespace od
