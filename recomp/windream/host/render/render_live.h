#ifndef WD_RENDER_LIVE_H
#define WD_RENDER_LIVE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
int wd_render_requested(void);
void wd_render_install(void);
SDL_WindowFlags wd_render_window_flags(void);
int wd_render_open(SDL_Window *);
void wd_render_close(void);
void wd_render_bind_surface(uint32_t, uint32_t, int, int, int, int, int);
void wd_render_forget_surface(uint32_t);
void wd_render_begin_present(uint32_t);
void wd_render_end_present(void);
void wd_render_capture(const char *);
void wd_render_mouse(SDL_Event *);
int wd_render_surface_owned(uint32_t address);
void wd_render_line(uint32_t destination, int x0, int y0, int x1, int y1, uint32_t colour);
// Source points at the first decoded pixel of the rectangle; pitch is bytes.
void wd_render_movie_upload(uint32_t source, uint32_t destination, int x, int y,
                            int width, int height, int pitch);
bool wd_render_read_arena(void *, uint32_t, void *, size_t);
void wd_render_read_scope_begin(void);
void wd_render_read_scope_end(void);
void wd_render_write_arena(uint32_t, const void *, size_t);
void wd_render_write_arena_at(uint32_t entry, uint32_t address, const void *, size_t);
int wd_render_copy(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int);
int wd_render_fill(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int);
void wd_render_ui(const uint32_t registers[8], uint32_t entry);
void wd_render_scene(uint32_t root, uint32_t destination, int main_frame, uint32_t caller,
                     uint32_t frame_callback);
void wd_render_prepare_callback(uint32_t root, uint32_t destination, int main_frame, uint32_t caller);
void wd_render_collect_scene(uint32_t root);
void wd_render_dim_background(uint32_t);
void wd_render_text_band(int);
void wd_render_caption_band(void);
void wd_render_reset_scene(void);
void wd_render_fog_update(void);
void wd_render_caption_scope_begin(void);
void wd_render_caption_scope_end(void);
uint32_t wd_render_copy_caller(uint32_t instruction);
#ifdef __cplusplus
}
#endif
#endif
