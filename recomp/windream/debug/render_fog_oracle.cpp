#include "port/fog.h"
#include <algorithm>
extern "C" void wd_fog_plan(const uint8_t *record, double delta, int32_t mode, float *phase,
                            uint32_t *colour, float *density, uint8_t *table) {
    od::port::GlideFogState fog;
    od::port::FogWaterPhase water{*phase};
    od::port::SCENE_SetFog(fog, water, record, delta, mode);
    *phase = water.phase;
    *colour = fog.color;
    *density = fog.density;
    std::copy(fog.table.begin(), fog.table.end(), table);
}
