#pragma once

#include "port/level_materials.h"
#include "render/model_preview.h"

namespace od {

// DREAMSFX output state for a level: table fog from SCENE_SetFog and the
// grGammaCorrectionValue(0.8) scan-out gamma.
constexpr float glide_output_gamma = 0.8f;

// Hands the palette rows and HNM4 pages a session changed since the previous
// call to the renderer, plus the current fog state. Call once per host frame
// after LevelMaterialSession::advance and before ModelPreview::draw.
void sync_level_materials(port::LevelMaterialSession& session, ModelPreview& preview);

} // namespace od
