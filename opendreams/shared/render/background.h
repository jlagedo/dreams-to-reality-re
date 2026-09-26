#pragma once

#include <sokol_gfx.h>

#include <string>

namespace od {

class Background {
public:
    bool init(std::string& error);
    void draw() const;
    void shutdown();

private:
    sg_buffer vertices_{};
    sg_shader shader_{};
    sg_pipeline pipeline_{};
};

} // namespace od
