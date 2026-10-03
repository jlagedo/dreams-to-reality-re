/*
 * WINDREAM recompilation - development control channel (WD_CTL).
 *
 * A live way into a running host for tests and for a person at a shell:
 * press a game key, wait for a condition, save a frame, read guest memory,
 * pause and step, tail the file opens, record the mixer's output. The client
 * is recomp/windream/debug/wdctl.py; the commands are listed in
 * recomp/README.md, "Development control channel", and in run_command below.
 *
 * Development only. It is compiled when the CMake option WD_DEVTOOLS is on
 * (never in a WD_RELEASE build; release.py checks the executable for MARKER),
 * the host reaches it through the seven hooks of devtools.h and nothing else,
 * and it does nothing unless WD_CTL is set when the host starts:
 *
 *   WD_CTL=<port>   listen on 127.0.0.1:<port> (0: any free port). The port
 *                   is written to ctl.port in the current directory and
 *                   logged as "[ctl] listening on 127.0.0.1:<port>".
 *
 * Protocol: one JSON object per line each way. A request is
 * {"id":N,"cmd":"...", ...}; its answer {"id":N,"ok":true, ...} or
 * {"id":N,"ok":false,"error":"..."}. Every answer with ok:true also carries
 * "frame" (frames presented so far), "ms" (host_elapsed_ms) and "seq" (the
 * number of the last recorded event). One client at a time.
 *
 * Threads: the listener thread (ctl_net.c) only queues request lines. They
 * are run here on the host's main thread, in wd_devtools_pump, which host_pump
 * calls where it drains SDL events: between guest instructions of the frame
 * loop, never in the middle of a bridge. Guest memory and host state are
 * touched from that thread alone. Two hooks arrive on other threads and take
 * a lock: file opens (any guest thread) and the mixer's output.
 *
 * Game knowledge does not belong here: this file knows keys, frames, memory,
 * files and sound. What an address means is the client's business.
 */
#include <stdarg.h>
#include "host.h"
#include "input_map.h"
#include "recomp_trace.h"
#include "render_live.h"
#include "devtools.h"
#include "ctl_json.h"
#include "ctl_net.h"

#define MARKER "wd-devtools"     /* release.py refuses an executable that holds this text */
#define TAP_MS 150u              /* a tap's default hold: KEY_HOLD_MS of the WD_KEYS script (user.c) */
#define DEFAULT_TIMEOUT_MS 60000u
#define MAX_READ 65536u
#define EVENTS 512               /* the ring `log` and `status` read */
#define PENDING 8                /* answers that wait for something */

static int g_on;                 /* WD_CTL is set and the listener runs */
static SDL_ThreadID g_main;      /* the thread that called wd_devtools_init: the host's main thread */
static uint32_t g_client;        /* the connection seen at the last pump, 0: none */
static uint32_t g_frames;        /* frames presented */
static int g_frame_w, g_frame_h; /* the last one's size */
static int g_paused;
static uint32_t g_step_left;     /* frames to let through before pausing again */

/* ---- keys ---- */
static uint8_t g_key[256];                              /* held by a `key` command */
static struct { int on, by_frame; uint32_t at; } g_tap[256];   /* when a tapped key comes up: that frame, or that ms */

int wd_devtools_key_down(int vk) { return g_on && g_key[vk & 0xFF]; }

/* ---- events: file opens, CD and disc changes ----
 * Numbered from 1 in the order they happened; event n is g_events[n % EVENTS]
 * until EVENTS newer ones have replaced it. */
typedef struct {
    uint32_t seq, ms, frame;
    char kind;                    /* 'o' open, 'c' CD, 'd' active disc */
    uint8_t write, ok;            /* 'o' */
    char guest[W32_MAX_PATH];     /* 'o': the guest's path */
    char text[512];               /* 'o': where it resolved on the host; else the message */
} Event;
static Event g_events[EVENTS];
static uint32_t g_seq;
static SDL_Mutex* g_events_lock;  /* the ring, the CD state and the pending answers' open matches */
static int g_disc_seen;           /* the active disc at the last open */
static struct { int track, disc; uint32_t mode; } g_cd = { 0, 0, W32_MCI_MODE_STOP };

/* ---- answers that wait ---- */
enum { PEND_WAIT = 1, PEND_UNTIL, PEND_STEP, PEND_SHOT };
enum { COND_MEM = 1, COND_OPENED, COND_DISC, COND_CD_TRACK, COND_FRAME };
typedef struct {
    int kind;                     /* 0: free */
    uint32_t connection;
    int64_t id;
    uint32_t start_frame, start_ms;
    int has_frames, has_ms;       /* PEND_WAIT: how long; PEND_UNTIL: the timeouts */
    uint32_t frames, ms;
    int cond;                     /* PEND_UNTIL */
    uint32_t addr, size, value;
    char op;                      /* '=', '!', '<', '>', '&' */
    char needle[W32_MAX_PATH];    /* COND_OPENED: the substring, */
    int want_ok;                  /*   whether the open must have succeeded (1), failed (0) or either (-1), */
    uint32_t hit;                 /*   and the event that matched, 0 until one does */
} Pending;
static Pending g_pending[PENDING];

/* ---- screenshot: the path waits for the next present ---- */
enum { SHOT_IDLE, SHOT_WANTED, SHOT_TAKEN };
static int g_shot;
static char g_shot_path[1024];

/* ---- audio dump: the mixer's output as a WAV file ---- */
static SDL_AtomicInt g_wav_on;
static SDL_Mutex* g_wav_lock;
static FILE* g_wav;
static char g_wav_path[1024];
static uint64_t g_wav_frames, g_wav_headed;   /* written; and how many the header on disk declares */
static int g_wav_rate = 44100;

/* ================= events ================= */

static int contains(const char* text, const char* needle) {   /* without regard to case, as guest names are */
    size_t n = strlen(needle);
    for (; *text; text++)
        if (!SDL_strncasecmp(text, needle, n)) return 1;
    return !n;
}
static int open_matches(const Event* e, const Pending* p) {
    return e->kind == 'o' && (p->want_ok < 0 || p->want_ok == e->ok) && contains(e->guest, p->needle);
}
/* Holding g_events_lock. */
static void record(char kind, const char* guest, const char* text, int write, int ok) {
    Event* e = &g_events[++g_seq % EVENTS];
    e->seq = g_seq; e->ms = host_elapsed_ms(); e->frame = g_frames;
    e->kind = kind; e->write = (uint8_t)write; e->ok = (uint8_t)ok;
    SDL_strlcpy(e->guest, guest, sizeof e->guest);
    SDL_strlcpy(e->text, text, sizeof e->text);
    for (int i = 0; kind == 'o' && i < PENDING; i++) {
        Pending* p = &g_pending[i];
        if (p->kind == PEND_UNTIL && p->cond == COND_OPENED && !p->hit && open_matches(e, p)) p->hit = e->seq;
    }
}
/* Event n if the ring still has it. Holding g_events_lock. */
static const Event* event_at(uint32_t n) {
    return n && n <= g_seq && g_seq - n < EVENTS ? &g_events[n % EVENTS] : NULL;
}

void wd_devtools_file_open(const char* guest, const char* host, int write, int ok) {
    if (!g_on) return;
    int disc = 0;
    files_active_disc(&disc, NULL);
    SDL_LockMutex(g_events_lock);
    if (disc != g_disc_seen) {   /* files.c changes the disc inside the open that asked for the other one */
        char text[64];
        SDL_snprintf(text, sizeof text, "active disc %d -> %d", g_disc_seen, disc);
        record('d', "", text, 0, 1);
        g_disc_seen = disc;
    }
    record('o', guest, host, write, ok);
    SDL_UnlockMutex(g_events_lock);
}

void wd_devtools_cd(int track, int disc, uint32_t mode) {
    if (!g_on) return;
    SDL_LockMutex(g_events_lock);
    if (track != g_cd.track || mode != g_cd.mode) {
        char text[64], on[16] = "";
        if (disc) SDL_snprintf(on, sizeof on, " (disc %d)", disc);
        if (mode == W32_MCI_MODE_PLAY) SDL_snprintf(text, sizeof text, "play track %d%s", track, on);
        else if (mode == W32_MCI_MODE_PAUSE) SDL_snprintf(text, sizeof text, "pause track %d%s", track, on);
        else SDL_snprintf(text, sizeof text, "stop");
        record('c', "", text, 0, 1);
    }
    g_cd.track = track; g_cd.disc = disc; g_cd.mode = mode;
    SDL_UnlockMutex(g_events_lock);
}

/* What winmm.c's cd_mode would say now: a track that ran out is stopped
 * although no MCI command has reported it. Holding g_events_lock. */
static const char* cd_state(void) {
    if (g_cd.mode == W32_MCI_MODE_PAUSE) return "paused";
    return g_cd.mode == W32_MCI_MODE_PLAY && mixer_cd_playing() ? "playing" : "stopped";
}

static void event_json(JsonOut* o, const Event* e) {
    jo_open(o, NULL, '{');
    jo_int(o, "seq", e->seq);
    jo_int(o, "ms", e->ms);
    jo_int(o, "frame", e->frame);
    jo_str(o, "kind", e->kind == 'o' ? "open" : e->kind == 'c' ? "cd" : "disc", 0);
    if (e->kind == 'o') {
        jo_str(o, "path", e->guest, 1);
        jo_str(o, "host", e->text, 0);
        jo_bool(o, "write", e->write);
        jo_bool(o, "ok", e->ok);
    } else {
        jo_str(o, "text", e->text, 0);
    }
    jo_close(o, '}');
}

/* ================= audio dump ================= */

static void wav_header(FILE* f, int rate, uint64_t frames) {
    uint32_t bytes = frames * 4 > 0xFFFFFFFFu - 36 ? 0xFFFFFFFFu - 36 : (uint32_t)(frames * 4);
    uint8_t h[44] = "RIFF";
    uint32_t fields[] = { 36 + bytes, 0, 0, 16, 0x00020001u /* PCM, 2 channels */, (uint32_t)rate,
                          (uint32_t)rate * 4, 0x00100004u /* 4-byte frames, 16 bits */, 0, bytes };
    for (int i = 0; i < 10; i++)
        for (int k = 0; k < 4; k++) h[4 + i * 4 + k] = (uint8_t)(fields[i] >> (8 * k));
    memcpy(h + 8, "WAVEfmt ", 8);
    memcpy(h + 36, "data", 4);
    long at = ftell(f);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, f);
    if (at > 0) fseek(f, at, SEEK_SET);
}

void wd_devtools_audio(const int16_t* pcm, int frames, int rate) {
    if (!SDL_GetAtomicInt(&g_wav_on)) return;
    SDL_LockMutex(g_wav_lock);
    if (g_wav) {
        g_wav_rate = rate;
        fwrite(pcm, 4, (size_t)frames, g_wav);
        g_wav_frames += (uint64_t)frames;
        if (g_wav_frames - g_wav_headed >= (uint64_t)rate) {   /* once a second: a killed host leaves a valid file */
            wav_header(g_wav, rate, g_wav_frames);
            fflush(g_wav);
            g_wav_headed = g_wav_frames;
        }
    }
    SDL_UnlockMutex(g_wav_lock);
}

/* Returns the frames written, or -1 when nothing was being dumped. */
static int64_t wav_stop(void) {
    int64_t frames = -1;
    SDL_SetAtomicInt(&g_wav_on, 0);
    SDL_LockMutex(g_wav_lock);
    if (g_wav) {
        wav_header(g_wav, g_wav_rate, g_wav_frames);
        fclose(g_wav);
        g_wav = NULL;
        frames = (int64_t)g_wav_frames;
    }
    SDL_UnlockMutex(g_wav_lock);
    return frames;
}

/* ================= guest memory ================= */

/* Is [va, va + n) committed guest memory? vm_state is how the host asks. */
static int mapped(uint32_t va, uint32_t n) {
    uint64_t at = va, end = (uint64_t)va + n;
    if (!n || end > 0x100000000ull) return 0;
    while (at < end) {
        uint32_t base = 0, bytes = 0;
        if (vm_state((uint32_t)at, &base, &bytes) != W32_MEM_COMMIT || !bytes) return 0;
        at = (uint64_t)base + bytes;
    }
    return 1;
}
/* An observer's view of the arena: not a guest access, so the render audit's
 * ownership check (wd_host_range) is not asked. */
static uint8_t* guest_bytes(uint32_t va) { return (uint8_t*)PTR(va); }

static int read_value(uint32_t va, uint32_t size, uint32_t* out) {
    if (!mapped(va, size)) return 0;
    const uint8_t* p = guest_bytes(va);
    *out = size == 1 ? p[0] : size == 2 ? (uint32_t)(p[0] | p[1] << 8)
         : (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return 1;
}

/* ================= answers ================= */

typedef struct { uint32_t connection; int64_t id; const JsonField* fields; int n; } Request;

static void reply_begin(JsonOut* o, int64_t id) {
    memset(o, 0, sizeof *o);
    jo_open(o, NULL, '{');
    jo_int(o, "id", id);
    jo_bool(o, "ok", 1);
    jo_int(o, "frame", g_frames);
    jo_int(o, "ms", host_elapsed_ms());
    SDL_LockMutex(g_events_lock);
    jo_int(o, "seq", g_seq);
    SDL_UnlockMutex(g_events_lock);
}
static void reply_end(JsonOut* o, uint32_t connection) {
    jo_close(o, '}');
    ctl_net_send(connection, o->text, o->length);
    free(o->text);
}
static void reply_ok(uint32_t connection, int64_t id) {
    JsonOut o;
    reply_begin(&o, id);
    reply_end(&o, connection);
}
static void reply_error(uint32_t connection, int64_t id, const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(text, sizeof text, format, args);
    va_end(args);
    JsonOut o = { 0 };
    jo_open(&o, NULL, '{');
    jo_int(&o, "id", id);
    jo_bool(&o, "ok", 0);
    jo_str(&o, "error", text, 0);
    reply_end(&o, connection);
}

/* A numeric argument: 1 and *out when given and valid, 0 when absent, -1 (and
 * the error is sent) when it is not an unsigned 32-bit number. */
static int arg_u32(const Request* r, const char* key, uint32_t* out) {
    const JsonField* f = json_get(r->fields, r->n, key);
    uint64_t v;
    if (!f) return 0;
    if (!json_u64(f, &v) || v > 0xFFFFFFFFu) {
        reply_error(r->connection, r->id, "\"%s\" must be a number from 0 to 0xFFFFFFFF", key);
        return -1;
    }
    *out = (uint32_t)v;
    return 1;
}
/* The same for an argument the command cannot do without: 1, or 0 with the error sent. */
static int need_u32(const Request* r, const char* key, uint32_t* out) {
    int got = arg_u32(r, key, out);
    if (!got) reply_error(r->connection, r->id, "\"%s\" is missing", key);
    return got > 0;
}
static const char* arg_text(const Request* r, const char* key) {
    return json_text(json_get(r->fields, r->n, key));
}

/* A free slot, filled in for this request; NULL (and the error is sent) when
 * all are taken. The caller sets the rest and then kind, under g_events_lock
 * when it is an open wait. */
static Pending* pending_new(const Request* r) {
    for (int i = 0; i < PENDING; i++) {
        Pending* p = &g_pending[i];
        if (p->kind) continue;
        memset(p, 0, sizeof *p);
        p->connection = r->connection; p->id = r->id;
        p->start_frame = g_frames; p->start_ms = host_elapsed_ms();
        return p;
    }
    reply_error(r->connection, r->id, "too many commands are waiting (%d)", PENDING);
    return NULL;
}
static int pending_has(int kind) {
    for (int i = 0; i < PENDING; i++) if (g_pending[i].kind == kind) return 1;
    return 0;
}

/* ================= commands ================= */

static void cmd_ping(const Request* r) {
    JsonOut o;
    reply_begin(&o, r->id);
    jo_str(&o, "build", MARKER " 1, host built " __DATE__ " " __TIME__, 0);
#ifdef WD_RENDER_AUDIT
    jo_bool(&o, "render_audit", 1);
#else
    jo_bool(&o, "render_audit", 0);
#endif
    reply_end(&o, r->connection);
}

static void cmd_status(const Request* r) {
    uint32_t want = 8;
    if (arg_u32(r, "opens", &want) < 0) return;
    if (want > 64) want = 64;
    int disc = 0;
    int disc_mode = files_active_disc(&disc, NULL) != NULL;
    JsonOut o;
    reply_begin(&o, r->id);
    jo_bool(&o, "headless", host_env("WD_HEADLESS") != NULL);
    jo_str(&o, "renderer", wd_render_requested() ? "direct" : "software", 0);
    jo_bool(&o, "disc_mode", disc_mode);
    jo_int(&o, "disc", disc);
    jo_bool(&o, "paused", g_paused);
    jo_bool(&o, "audio_dump", SDL_GetAtomicInt(&g_wav_on));
    SDL_LockMutex(g_events_lock);
    jo_int(&o, "cd_track", g_cd.track);
    jo_int(&o, "cd_disc", g_cd.disc);
    jo_str(&o, "cd_state", cd_state(), 0);
    uint32_t first = g_seq + 1;   /* the oldest of the last `want` opens */
    for (uint32_t n = g_seq, found = 0; found < want && event_at(n); n--)
        if (event_at(n)->kind == 'o') { first = n; found++; }
    jo_open(&o, "opens", '[');
    for (uint32_t n = first; n <= g_seq; n++)
        if (event_at(n)->kind == 'o') event_json(&o, event_at(n));
    jo_close(&o, ']');
    SDL_UnlockMutex(g_events_lock);
    reply_end(&o, r->connection);
}

static void cmd_key(const Request* r) {
    const char* name = arg_text(r, "name");
    const char* action = arg_text(r, "action");
    uint32_t frames = 0, ms = TAP_MS;
    int has_frames = arg_u32(r, "frames", &frames), has_ms = arg_u32(r, "ms", &ms);
    if (has_frames < 0 || has_ms < 0) return;
    uint8_t vk = name ? wd_vk_from_name(name, strlen(name)) : 0;
    if (!vk) { reply_error(r->connection, r->id, "unknown key name \"%s\" (the names of WD_KEYS)", name ? name : ""); return; }
    if (!action || !strcmp(action, "tap")) {
        g_key[vk] = 1;
        g_tap[vk].on = 1;
        g_tap[vk].by_frame = has_frames;
        g_tap[vk].at = has_frames ? g_frames + frames : host_elapsed_ms() + ms;
    } else if (!strcmp(action, "down") || !strcmp(action, "up")) {
        g_key[vk] = action[0] == 'd';
        g_tap[vk].on = 0;
    } else {
        reply_error(r->connection, r->id, "\"action\" must be down, up or tap");
        return;
    }
    JsonOut o;
    reply_begin(&o, r->id);
    jo_int(&o, "vk", vk);
    reply_end(&o, r->connection);
}

static void cmd_wait(const Request* r) {
    uint32_t frames = 0, ms = 0;
    int has_frames = arg_u32(r, "frames", &frames), has_ms = arg_u32(r, "ms", &ms);
    if (has_frames < 0 || has_ms < 0) return;
    if (!has_frames && !has_ms) { reply_error(r->connection, r->id, "wait needs \"frames\" or \"ms\""); return; }
    if (g_paused && !has_ms) { reply_error(r->connection, r->id, "paused: no frame will pass; step, or wait for ms"); return; }
    Pending* p = pending_new(r);
    if (!p) return;
    p->has_frames = has_frames; p->frames = frames;
    p->has_ms = has_ms; p->ms = ms;
    p->kind = PEND_WAIT;
}

static void cmd_wait_until(const Request* r) {
    static const struct { const char* name; int cond; } conds[] = {
        {"mem", COND_MEM}, {"opened", COND_OPENED}, {"disc", COND_DISC}, {"cd_track", COND_CD_TRACK}, {"frame", COND_FRAME},
    };
    const char* name = arg_text(r, "cond");
    int cond = 0;
    for (size_t i = 0; name && i < sizeof conds / sizeof conds[0]; i++)
        if (!strcmp(name, conds[i].name)) cond = conds[i].cond;
    if (!cond) { reply_error(r->connection, r->id, "\"cond\" must be mem, opened, disc, cd_track or frame"); return; }

    uint32_t addr = 0, size = 4, value = 0, timeout_frames = 0, timeout_ms = DEFAULT_TIMEOUT_MS, since = 0;
    int has_addr = arg_u32(r, "addr", &addr), has_value = arg_u32(r, "value", &value);
    int has_frames = arg_u32(r, "timeout_frames", &timeout_frames), has_ms = arg_u32(r, "timeout_ms", &timeout_ms);
    int has_since = arg_u32(r, "since", &since);
    if (has_addr < 0 || has_value < 0 || has_frames < 0 || has_ms < 0 || has_since < 0 || arg_u32(r, "size", &size) < 0) return;
    const char* op = arg_text(r, "op");
    const char* path = arg_text(r, "path");
    const JsonField* ok = json_get(r->fields, r->n, "ok");
    if (cond == COND_MEM) {
        if (!has_addr || (size != 1 && size != 2 && size != 4)) { reply_error(r->connection, r->id, "mem needs \"addr\" and a \"size\" of 1, 2 or 4"); return; }
        if (!op || !op[0] || !strchr("=!<>&", op[0]) || (op[1] && strcmp(op, "==") && strcmp(op, "!="))) {
            reply_error(r->connection, r->id, "\"op\" must be ==, !=, <, > or &");
            return;
        }
    } else if (cond == COND_OPENED) {
        if (!path || !path[0] || strlen(path) >= W32_MAX_PATH) { reply_error(r->connection, r->id, "opened needs \"path\", part of a guest path"); return; }
    } else if (!has_value) {
        reply_error(r->connection, r->id, "%s needs \"value\"", name);
        return;
    }

    Pending* p = pending_new(r);
    if (!p) return;
    p->has_frames = has_frames; p->frames = timeout_frames;
    p->has_ms = has_ms || !has_frames; p->ms = timeout_ms;   /* never without a timeout */
    p->cond = cond; p->addr = addr; p->size = size; p->value = value; p->op = op ? op[0] : 0;
    p->want_ok = ok && ok->type == 'b' ? ok->num != 0 : -1;
    SDL_LockMutex(g_events_lock);
    if (cond == COND_OPENED) {
        /* Opens after this command, or after event `since` for a client that
         * noted "seq" before the key that causes the open. */
        SDL_strlcpy(p->needle, path, sizeof p->needle);
        for (uint32_t n = has_since ? since + 1 : g_seq + 1; !p->hit && n && n <= g_seq; n++)
            if (event_at(n) && open_matches(event_at(n), p)) p->hit = n;
    }
    p->kind = PEND_UNTIL;
    SDL_UnlockMutex(g_events_lock);
}

static void cmd_read(const Request* r) {
    uint32_t addr = 0, size = 0;
    if (!need_u32(r, "addr", &addr) || !need_u32(r, "size", &size)) return;
    if (size > MAX_READ) { reply_error(r->connection, r->id, "at most %u bytes per read", MAX_READ); return; }
    if (!mapped(addr, size)) { reply_error(r->connection, r->id, "0x%08X..+%u is not committed guest memory", addr, size); return; }
    JsonOut o;
    reply_begin(&o, r->id);
    jo_hex(&o, "hex", guest_bytes(addr), size);
    reply_end(&o, r->connection);
}

static void cmd_read_cstr(const Request* r) {
    uint32_t addr = 0, max = 256, n = 0;
    if (!need_u32(r, "addr", &addr) || arg_u32(r, "max", &max) < 0) return;
    if (max > 4096) max = 4096;
    char text[4097];
    while (n < max) {
        if (!mapped(addr + n, 1)) { reply_error(r->connection, r->id, "0x%08X is not committed guest memory", addr + n); return; }
        if (!(text[n] = (char)*guest_bytes(addr + n))) break;
        n++;
    }
    text[n] = 0;
    JsonOut o;
    reply_begin(&o, r->id);
    jo_str(&o, "text", text, 1);
    jo_int(&o, "length", n);
    reply_end(&o, r->connection);
}

static int hex_digit(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static void cmd_write(const Request* r) {
    uint32_t addr = 0;
    const char* hex = arg_text(r, "hex");
    if (!need_u32(r, "addr", &addr)) return;
    size_t digits = hex ? strlen(hex) : 0;
    if (!digits || digits % 2) { reply_error(r->connection, r->id, "write needs \"hex\", two digits per byte"); return; }
    for (size_t i = 0; i < digits; i++)
        if (hex_digit(hex[i]) < 0) { reply_error(r->connection, r->id, "\"hex\" holds something that is not a hex digit"); return; }
    uint32_t size = (uint32_t)(digits / 2);
    if (!mapped(addr, size)) { reply_error(r->connection, r->id, "0x%08X..+%u is not committed guest memory", addr, size); return; }
    uint8_t* to = guest_bytes(addr);
    for (uint32_t i = 0; i < size; i++) to[i] = (uint8_t)(hex_digit(hex[2 * i]) << 4 | hex_digit(hex[2 * i + 1]));
    JsonOut o;
    reply_begin(&o, r->id);
    jo_int(&o, "size", size);
    reply_end(&o, r->connection);
}

static void cmd_screenshot(const Request* r) {
    const char* path = arg_text(r, "path");
    if (!path || !path[0] || strlen(path) >= sizeof g_shot_path) { reply_error(r->connection, r->id, "screenshot needs \"path\""); return; }
    if (g_shot != SHOT_IDLE) { reply_error(r->connection, r->id, "a screenshot is already waiting for a frame"); return; }
    Pending* p = pending_new(r);
    if (!p) return;
    SDL_strlcpy(g_shot_path, path, sizeof g_shot_path);
    g_shot = SHOT_WANTED;
    p->kind = PEND_SHOT;
    if (g_paused) { g_step_left = 1; g_paused = 0; }   /* nothing is presented while paused: step one frame for it */
}

/* Render parity captures (recomp/windream/verify/render_parity.py): the scene
 * inputs of the next display 3D frame as a .wds file, and the guest's committed
 * memory: "WDM2", the render root of the last scene capture, then runs of
 * { va, bytes, data }. Both are written by the guest thread, so with the game
 * paused they describe the same frame. */
static void cmd_scene_capture(const Request* r) {
    const char* path = arg_text(r, "path");
    if (!path || !path[0]) { reply_error(r->connection, r->id, "scene_capture needs \"path\""); return; }
    if (!wd_render_requested()) { reply_error(r->connection, r->id, "scene_capture needs the direct renderer"); return; }
    wd_render_scene_capture_next(path);
    reply_ok(r->connection, r->id);
}
static void cmd_memory_dump(const Request* r) {
    const char* path = arg_text(r, "path");
    if (!path || !path[0]) { reply_error(r->connection, r->id, "memory_dump needs \"path\""); return; }
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (!io) { reply_error(r->connection, r->id, "cannot create the dump file"); return; }
    uint32_t runs = 0, total = 0, va = 0x10000u;
    uint32_t root = wd_render_scene_capture_root();
    int ok = SDL_WriteIO(io, "WDM2", 4) == 4 && SDL_WriteIO(io, &root, 4) == 4;
    while (ok && va < WD_ARENA_SIZE) {
        uint32_t base = 0, bytes = 0;
        uint32_t state = vm_state(va, &base, &bytes);
        if (!bytes) break;
        uint32_t end = base + bytes;
        if (state == W32_MEM_COMMIT) {
            uint32_t head[2] = { va, end - va };
            ok = SDL_WriteIO(io, head, sizeof head) == sizeof head &&
                 SDL_WriteIO(io, wd_host_range(va, end - va, 0), end - va) == end - va;
            runs++;
            total += end - va;
        }
        if (end <= va) break;
        va = end;
    }
    ok = SDL_CloseIO(io) && ok;
    if (!ok) { reply_error(r->connection, r->id, "writing the dump failed"); return; }
    JsonOut o;
    reply_begin(&o, r->id);
    jo_int(&o, "runs", runs);
    jo_int(&o, "bytes", total);
    jo_int(&o, "scene_pending", wd_render_scene_capture_pending());
    reply_end(&o, r->connection);
}

static void cmd_step(const Request* r) {
    uint32_t frames = 1;
    if (arg_u32(r, "frames", &frames) < 0) return;
    if (!frames) { reply_error(r->connection, r->id, "\"frames\" must be at least 1"); return; }
    if (pending_has(PEND_STEP)) { reply_error(r->connection, r->id, "a step is already running"); return; }
    Pending* p = pending_new(r);
    if (!p) return;
    g_step_left = frames;
    g_paused = 0;
    p->kind = PEND_STEP;
}

static void cmd_log(const Request* r) {
    uint32_t since = 0, max = 100, sent = 0;
    if (arg_u32(r, "since", &since) < 0 || arg_u32(r, "max", &max) < 0) return;
    JsonOut o;
    reply_begin(&o, r->id);
    SDL_LockMutex(g_events_lock);
    uint32_t oldest = g_seq >= EVENTS ? g_seq - EVENTS + 1 : 1;
    jo_bool(&o, "dropped", since + 1 < oldest);   /* events after `since` that the ring no longer has */
    jo_open(&o, "events", '[');
    for (uint32_t n = since + 1 < oldest ? oldest : since + 1; n && n <= g_seq && sent < max; n++, sent++)
        event_json(&o, event_at(n));
    jo_close(&o, ']');
    SDL_UnlockMutex(g_events_lock);
    reply_end(&o, r->connection);
}

static void cmd_audio_dump(const Request* r) {
    const char* path = arg_text(r, "path");
    if (!path || !path[0] || strlen(path) >= sizeof g_wav_path) { reply_error(r->connection, r->id, "audio_dump needs \"path\""); return; }
    SDL_LockMutex(g_wav_lock);
    int busy = g_wav != NULL;
    if (!busy && (g_wav = fopen(path, "wb")) != NULL) {
        SDL_strlcpy(g_wav_path, path, sizeof g_wav_path);
        g_wav_frames = g_wav_headed = 0;
        wav_header(g_wav, g_wav_rate, 0);
    }
    int opened = !busy && g_wav != NULL;
    SDL_UnlockMutex(g_wav_lock);
    if (busy) { reply_error(r->connection, r->id, "already dumping to %s", g_wav_path); return; }
    if (!opened) { reply_error(r->connection, r->id, "cannot create %s", path); return; }
    SDL_SetAtomicInt(&g_wav_on, 1);
    JsonOut o;
    reply_begin(&o, r->id);
    jo_str(&o, "path", path, 0);
    jo_str(&o, "format", "wav, 16-bit stereo: the mixer's output", 0);
    reply_end(&o, r->connection);
}

static void cmd_audio_dump_stop(const Request* r) {
    int64_t frames = wav_stop();
    if (frames < 0) { reply_error(r->connection, r->id, "no audio dump is running"); return; }
    JsonOut o;
    reply_begin(&o, r->id);
    jo_str(&o, "path", g_wav_path, 0);
    jo_int(&o, "frames", frames);
    jo_int(&o, "rate", g_wav_rate);
    reply_end(&o, r->connection);
}

/* Leave the way main does when the game returns: flush the trace, close the
 * renderer and exit() (stdio is flushed and the atexit reports are written,
 * the virtual-memory log's exit state among them). */
static void cmd_quit(const Request* r) {
    uint32_t code = 0;
    if (arg_u32(r, "code", &code) < 0) return;
    reply_ok(r->connection, r->id);
    fprintf(stderr, "[ctl] quit %u\n", code);
    wav_stop();
    SDL_RemovePath("ctl.port");
    ctl_net_finish();
    recomp_trace_flush();
    wd_render_close();
    exit((int)code);
}

static void run_command(const Request* r, const char* cmd) {
    if (!strcmp(cmd, "ping")) cmd_ping(r);
    else if (!strcmp(cmd, "status")) cmd_status(r);
    else if (!strcmp(cmd, "key")) cmd_key(r);
    else if (!strcmp(cmd, "wait")) cmd_wait(r);
    else if (!strcmp(cmd, "wait_until")) cmd_wait_until(r);
    else if (!strcmp(cmd, "read")) cmd_read(r);
    else if (!strcmp(cmd, "read_cstr")) cmd_read_cstr(r);
    else if (!strcmp(cmd, "write")) cmd_write(r);
    else if (!strcmp(cmd, "screenshot")) cmd_screenshot(r);
    else if (!strcmp(cmd, "pause")) { g_paused = 1; g_step_left = 0; reply_ok(r->connection, r->id); }
    else if (!strcmp(cmd, "resume")) { g_paused = 0; g_step_left = 0; reply_ok(r->connection, r->id); }
    else if (!strcmp(cmd, "step")) cmd_step(r);
    else if (!strcmp(cmd, "scene_capture")) cmd_scene_capture(r);
    else if (!strcmp(cmd, "memory_dump")) cmd_memory_dump(r);
    else if (!strcmp(cmd, "log")) cmd_log(r);
    else if (!strcmp(cmd, "audio_dump")) cmd_audio_dump(r);
    else if (!strcmp(cmd, "audio_dump_stop")) cmd_audio_dump_stop(r);
    else if (!strcmp(cmd, "quit")) cmd_quit(r);
    else reply_error(r->connection, r->id, "unknown command \"%s\"", cmd);
}

static void run_line(char* line, uint32_t connection) {
    JsonField fields[16];
    Request r = { connection, 0, fields, json_parse(line, fields, 16) };
    if (r.n < 0) { reply_error(connection, 0, "not a flat JSON object of at most 16 fields"); return; }
    const JsonField* id = json_get(fields, r.n, "id");
    if (id && id->type == 'n') r.id = (int64_t)id->num;
    const char* cmd = arg_text(&r, "cmd");
    if (!cmd) { reply_error(connection, r.id, "no \"cmd\""); return; }
    run_command(&r, cmd);
}

/* ================= the main thread's side ================= */

static int condition(const Pending* p, uint32_t* seen) {
    int disc = 0;
    switch (p->cond) {
    case COND_MEM:
        if (!read_value(p->addr, p->size, seen)) return 0;   /* not mapped (yet): not true */
        return p->op == '=' ? *seen == p->value : p->op == '!' ? *seen != p->value
             : p->op == '<' ? *seen < p->value : p->op == '>' ? *seen > p->value : (*seen & p->value) != 0;
    case COND_OPENED: return p->hit != 0;
    case COND_DISC: files_active_disc(&disc, NULL); return (uint32_t)disc == p->value;
    case COND_CD_TRACK: return (uint32_t)g_cd.track == p->value;
    case COND_FRAME: return g_frames >= p->value;
    default: return 0;
    }
}

/* Answer what has finished waiting. */
static void finish_pending(void) {
    uint32_t now = host_elapsed_ms();
    for (int i = 0; i < PENDING; i++) {
        Pending* p = &g_pending[i];
        if (!p->kind) continue;
        uint32_t frames = g_frames - p->start_frame, ms = now - p->start_ms, seen = 0;
        int limit = (p->has_frames && frames >= p->frames) || (p->has_ms && ms >= p->ms);
        int done = 0, failed = 0;
        JsonOut o;
        SDL_LockMutex(g_events_lock);   /* p->hit, the CD state and the ring */
        if (p->kind == PEND_WAIT) done = limit;
        else if (p->kind == PEND_STEP) done = !g_step_left;
        else if (p->kind == PEND_SHOT) done = g_shot == SHOT_TAKEN;
        else if (!(done = condition(p, &seen))) failed = limit;
        if (done) {
            reply_begin(&o, p->id);   /* takes the same lock again: SDL mutexes are recursive */
            jo_int(&o, "waited_frames", frames);
            jo_int(&o, "waited_ms", ms);
            if (p->kind == PEND_UNTIL && p->cond == COND_MEM) jo_int(&o, "value", seen);
            if (p->kind == PEND_UNTIL && p->cond == COND_OPENED) {
                const Event* e = event_at(p->hit);
                jo_int(&o, "event", p->hit);
                if (e) { jo_str(&o, "path", e->guest, 1); jo_str(&o, "host", e->text, 0); jo_bool(&o, "open_ok", e->ok); }
            }
        }
        SDL_UnlockMutex(g_events_lock);
        if (done && p->kind == PEND_SHOT) {
            /* The host's snapshot code has run (gdi.c snap): a 24-bit BMP of
             * the game's frame, or with the direct renderer a PNG of the window. */
            int direct = wd_render_requested(), w = g_frame_w, h = g_frame_h;
            if (direct && host_window()) SDL_GetWindowSizeInPixels(host_window(), &w, &h);
            g_shot = SHOT_IDLE;
            jo_str(&o, "path", g_shot_path, 0);
            jo_int(&o, "width", w);
            jo_int(&o, "height", h);
            jo_str(&o, "format", direct ? "png" : "bmp", 0);
            if (!SDL_GetPathInfo(g_shot_path, NULL)) { free(o.text); done = 0; failed = 2; }
        }
        if (done) reply_end(&o, p->connection);
        if (failed == 1) reply_error(p->connection, p->id, "timeout after %u frames (%u ms)", frames, ms);
        if (failed == 2) reply_error(p->connection, p->id, "the host's snapshot did not write %s", g_shot_path);
        if (done || failed) {
            SDL_LockMutex(g_events_lock);
            p->kind = 0;
            SDL_UnlockMutex(g_events_lock);
        }
    }
}

/* The client left: what it was waiting for has nobody to go to. What it set
 * stays (a pause, a held key, a tap still to be released, an audio dump), so
 * that one command per connection works, as the wdctl.py command line does. */
static void client_gone(void) {
    fprintf(stderr, "[ctl] client disconnected\n");
    g_shot = SHOT_IDLE;
    SDL_LockMutex(g_events_lock);
    memset(g_pending, 0, sizeof g_pending);
    SDL_UnlockMutex(g_events_lock);
}

static void service(void) {
    uint32_t connection = ctl_net_connection(), from;
    if (connection != g_client) {
        if (g_client) client_gone();
        if (connection) fprintf(stderr, "[ctl] client connected\n");
        g_client = connection;
    }
    for (char* line; (line = ctl_net_next(&from)) != NULL; free(line))
        if (from == connection) run_line(line, from);   /* a line from a client that has left is dropped */
    uint32_t now = host_elapsed_ms();
    for (int vk = 0; vk < 256; vk++)
        if (g_tap[vk].on && (int32_t)((g_tap[vk].by_frame ? g_frames : now) - g_tap[vk].at) >= 0)
            g_tap[vk].on = g_key[vk] = 0;
    finish_pending();
}

void wd_devtools_pump(void) {
    if (!g_on || SDL_GetCurrentThreadID() != g_main) return;
    for (;;) {
        service();
        if (!g_paused) return;
        /* Paused: the guest stays in this call. Keep the window alive without
         * taking its events from the host, and let a close request through. */
        if (SDL_WasInit(SDL_INIT_EVENTS)) {
            SDL_PumpEvents();
            if (SDL_HasEvent(SDL_EVENT_QUIT) || SDL_HasEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED)) {
                fprintf(stderr, "[ctl] window closed while paused: resuming\n");
                g_paused = 0;
                g_step_left = 0;
                return;
            }
        }
        SDL_Delay(2);
    }
}

const char* wd_devtools_frame(int width, int height) {
    if (!g_on) return NULL;
    g_frames++;
    g_frame_w = width; g_frame_h = height;
    if (g_step_left && !--g_step_left) g_paused = 1;
    if (g_shot != SHOT_WANTED) return NULL;
    g_shot = SHOT_TAKEN;
    return g_shot_path;
}

void wd_devtools_init(void) {
    const char* spec = host_env("WD_CTL");
    char error[128];
    if (!spec) return;
    int port = ctl_net_start(atoi(spec), error, sizeof error);
    if (port < 0) { fprintf(stderr, "[ctl] WD_CTL=%s: %s; the control channel is off\n", spec, error); return; }
    g_main = SDL_GetCurrentThreadID();
    g_events_lock = SDL_CreateMutex();
    g_wav_lock = SDL_CreateMutex();
    files_active_disc(&g_disc_seen, NULL);
    FILE* f = fopen("ctl.port", "w");
    if (f) { fprintf(f, "%d\n", port); fclose(f); }
    g_on = 1;
    fprintf(stderr, "[ctl] listening on 127.0.0.1:%d\n", port);
}
