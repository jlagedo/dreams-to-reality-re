#pragma once
#include "render/direct.h"
#include <sokol_gfx.h>

// Borrowed views expire on release or the next draw to their target.
sg_view od_renderer_view(od_renderer *, od_render_id);
sg_image od_renderer_image(od_renderer *, od_render_id);
// Caller owns begin/end pass, commit and present. Gamma is final-output only.
int od_renderer_output(od_renderer *, od_render_id, float gamma);
