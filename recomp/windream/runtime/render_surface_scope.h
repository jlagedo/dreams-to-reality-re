#pragma once

namespace wd {
// Renderer-thread operation boundary. Nested operations may insert resources,
// but must not sweep and erase surface objects borrowed by their caller.
class SurfaceScope {
  public:
    SurfaceScope(unsigned &depth, void (*retire)()) : depth_(depth) {
        if (depth_++ == 0)
            retire();
    }
    ~SurfaceScope() { --depth_; }
    SurfaceScope(const SurfaceScope &) = delete;
    SurfaceScope &operator=(const SurfaceScope &) = delete;

  private:
    unsigned &depth_;
};
} // namespace wd
