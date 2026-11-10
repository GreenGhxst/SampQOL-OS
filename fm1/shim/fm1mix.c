/* SPDX-License-Identifier: GPL-3.0-only
 *
 * fm1mix: an LD_PRELOAD shim for the MPC Sample app (firmware 1.3.0, started
 * by SampQOL OS from the SD card). It runs the open FM1 DX7 engine from
 * m-vave/fm1_open inside the app process and mixes its sound into the ALSA
 * playback stream that the app writes. The synth is played by:
 *
 *   - MIDI the app receives (tap on snd_rawmidi_read: MIDI IN / USB),
 *   - MIDI the app or SampQOL OS sends (tap on snd_rawmidi_write: a SampQOL
 *     MIDI track with its own MIDI Out channel plays the synth),
 *   - a text FIFO /tmp/fm1.cmd and a raw MIDI FIFO /tmp/fm1.midi.
 *
 * Configuration: $FM1MIX_DIR/fm1.conf. Log: /tmp/fm1mix.log.
 * The shim never changes the stock firmware; it lives in RAM like SampQOL OS.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "synth_dx7.h"
#include "fm1_bank.h"

#define FM1MIX_VERSION "0.1.0"
#define EXPORT __attribute__((visibility("default")))

/* ---- ALSA ABI subset. The numbers come from alsa/pcm.h and are checked
 * against the real headers by test/check_abi.c. No ALSA headers are needed
 * to build, so the shim has no build-time dependency on the device libs. */
typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
typedef struct _snd_rawmidi snd_rawmidi_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;
enum { PCM_STREAM_PLAYBACK = 0 };
enum { ACCESS_RW_INTERLEAVED = 3, ACCESS_RW_NONINTERLEAVED = 4 };
enum { FMT_S16_LE = 2, FMT_S24_LE = 6, FMT_S32_LE = 10, FMT_FLOAT_LE = 14 };

typedef snd_pcm_sframes_t (*writei_fn)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
typedef snd_pcm_sframes_t (*writen_fn)(snd_pcm_t *, void **, snd_pcm_uframes_t);
typedef int (*close_fn)(snd_pcm_t *);
typedef ssize_t (*rawmidi_read_fn)(snd_rawmidi_t *, void *, size_t);
typedef ssize_t (*rawmidi_write_fn)(snd_rawmidi_t *, const void *, size_t);
typedef int (*stream_fn)(snd_pcm_t *);
typedef const char *(*name_fn)(snd_pcm_t *);
typedef size_t (*sizeof_fn)(void);
typedef int (*current_fn)(snd_pcm_t *, snd_pcm_hw_params_t *);
typedef int (*get_rate_fn)(const snd_pcm_hw_params_t *, unsigned *, int *);
typedef int (*get_uint_fn)(const snd_pcm_hw_params_t *, unsigned *);
typedef int (*get_int_fn)(const snd_pcm_hw_params_t *, int *);

static struct {
    writei_fn writei; writen_fn writen; close_fn close;
    rawmidi_read_fn rm_read; rawmidi_write_fn rm_write;
    stream_fn stream; name_fn name; sizeof_fn hw_sizeof; current_fn hw_current;
    get_rate_fn get_rate; get_uint_fn get_channels; get_int_fn get_format; get_int_fn get_access;
} real;

/* ---- configuration ---- */
static struct {
    int enabled;          /* 0 after a fatal problem */
    unsigned program;     /* 0..127 */
    unsigned volume;      /* 0..127, CC7 changes it */
    unsigned channel;     /* 0 = omni, 1..16 */
    unsigned voices;      /* 1..16 */
    unsigned rate;        /* rate to create the synth with before the pcm is known */
    unsigned mono;        /* 1 = mono/legato */
    unsigned transpose;   /* semitones, stored +64 */
    int src_in, src_out, src_fifo;
    float gain;           /* linear master gain at volume 127 */
    char pcm_match[64];   /* substring of the pcm name to mix into; empty = first playback pcm */
    char bank_path[256];
    char dir[256];
} cfg = { 1, 0, 100, 0, 8, 44100, 0, 64, 1, 1, 1, 0.5f, "", "", "" };

/* ---- log ---- */
static int log_fd = -1;
static void logmsg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    if (log_fd < 0) return;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    buf[n++] = '\n';
    if (write(log_fd, buf, (size_t)n) < 0) { /* ignore */ }
}

/* ---- MIDI event ring: producers (MIDI threads) -> consumer (audio thread) ---- */
struct ev { uint8_t b[3]; uint8_t n; };
enum { RING = 512 };
static struct ev ring[RING];
static unsigned ring_w, ring_r;
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;

static void ring_push(const uint8_t *m, unsigned n)
{
    pthread_mutex_lock(&ring_lock);
    unsigned w = __atomic_load_n(&ring_w, __ATOMIC_RELAXED);
    unsigned r = __atomic_load_n(&ring_r, __ATOMIC_ACQUIRE);
    if (w - r < RING) {
        struct ev *e = &ring[w % RING];
        memcpy(e->b, m, 3); e->n = (uint8_t)n;
        __atomic_store_n(&ring_w, w + 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&ring_lock);
}

/* ---- MIDI byte parser (running status, skips SysEx and realtime) ---- */
struct parser { uint8_t status, need, have, insysex; uint8_t data[2]; };
typedef void (*emit_fn)(const uint8_t *m, unsigned n);

static void parse(struct parser *p, const uint8_t *buf, size_t len, emit_fn emit)
{
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = buf[i];
        if (b >= 0xF8) continue;                       /* realtime */
        if (b == 0xF0) { p->insysex = 1; continue; }
        if (b == 0xF7) { p->insysex = 0; continue; }
        if (p->insysex) continue;
        if (b >= 0x80) {
            if (b >= 0xF0) { p->status = 0; continue; } /* system common */
            p->status = b; p->have = 0;
            p->need = ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) ? 1 : 2;
            continue;
        }
        if (!p->status) continue;
        p->data[p->have++] = b;
        if (p->have == p->need) {
            uint8_t m[3] = { p->status, p->data[0], p->need == 2 ? p->data[1] : 0 };
            emit(m, p->need + 1);
            p->have = 0;
        }
    }
}

static void emit_filtered(const uint8_t *m, unsigned n)
{
    if (cfg.channel && (m[0] & 0x0F) != cfg.channel - 1) return;
    ring_push(m, n);
}

/* ---- synth state (owned by the audio thread after creation) ---- */
static synth_dx7_t *synth;
static unsigned synth_rate;
static uint8_t patch[SYNTH_DX7_EXPANDED_PATCH_BYTES];
static int32_t pend[SYNTH_DX7_BLOCK_SAMPLES];
static unsigned pend_pos = SYNTH_DX7_BLOCK_SAMPLES;
static unsigned stat_blocks, stat_events, stat_writes, stat_unknown_fmt;

static void set_program(unsigned program)
{
    char name[11];
    cfg.program = program & 127U;
    fm1_bank_patch(patch, cfg.program);
    fm1_bank_name(name, cfg.program);
    if (synth) {
        synth_dx7_set_operator_mask(synth, patch[155]);
        synth_dx7_set_poly_mode(synth, (uint8_t)cfg.mono);
        synth_dx7_set_adsr(synth, 0, 0, 0, 100, 0);
        synth_dx7_lfo_reset(synth, patch);
    }
    logmsg("program %u '%s'", cfg.program + 1, name);
}

static int synth_setup(unsigned rate)
{
    if (synth && synth_rate == rate) return 0;
    if (synth) { synth_dx7_destroy(synth); synth = NULL; }
    synth = synth_dx7_create((uint8_t)cfg.voices, rate, SYNTH_DX7_BLOCK_SAMPLES);
    if (!synth) { logmsg("synth: rate %u not supported, FM1 off", rate); return -1; }
    synth_rate = rate;
    synth_dx7_set_pitch_bend_range(synth, 2, 2);
    set_program(cfg.program);
    logmsg("synth: %u voices at %u Hz", cfg.voices, rate);
    return 0;
}

static void apply_event(const uint8_t *m, unsigned n)
{
    uint8_t cmd = m[0] & 0xF0;
    ++stat_events;
    if (cmd == 0xC0) { set_program(m[1]); return; }
    if (cmd == 0xB0 && m[1] == 7) { cfg.volume = m[2] & 127U; return; }
    if (cmd == 0xB0 && (m[1] == 0 || m[1] == 32)) return; /* bank select: one bank */
    if (!synth) return;
    uint8_t msg[3] = { cmd, m[1], m[2] };
    if ((cmd == 0x90 || cmd == 0x80) && cfg.transpose != 64) {
        int note = (int)m[1] + (int)cfg.transpose - 64;
        if (note < 0 || note > 127) return;
        msg[1] = (uint8_t)note;
    }
    (void)synth_dx7_midi(synth, patch, msg, n);
}

/* ---- FIFO control (read in the audio thread, non-blocking) ---- */
static int cmd_fd = -1, midi_fd = -1;
static struct parser fifo_parser;
static char cmd_line[128];
static unsigned cmd_len;

static void emit_direct(const uint8_t *m, unsigned n) { apply_event(m, n); }

static void run_command(char *line)
{
    char *arg = strchr(line, ' ');
    if (arg) *arg++ = 0;
    long a = arg ? strtol(arg, &arg, 0) : 0;
    long b = arg ? strtol(arg, NULL, 0) : 0;
    if (!strcmp(line, "prog") || !strcmp(line, "program")) set_program((unsigned)(a > 0 ? a - 1 : 0));
    else if (!strcmp(line, "vol") || !strcmp(line, "volume")) cfg.volume = (unsigned)(a < 0 ? 0 : a > 127 ? 127 : a);
    else if (!strcmp(line, "ch") || !strcmp(line, "channel")) cfg.channel = (unsigned)(a < 0 ? 0 : a > 16 ? 16 : a);
    else if (!strcmp(line, "mono")) { cfg.mono = a != 0; if (synth) synth_dx7_set_poly_mode(synth, (uint8_t)cfg.mono); }
    else if (!strcmp(line, "transpose")) cfg.transpose = (unsigned)(64 + (a < -48 ? -48 : a > 48 ? 48 : a));
    else if (!strcmp(line, "panic")) { if (synth) fm1_dx7_panic(synth); }
    else if (!strcmp(line, "on")) { uint8_t m[3] = { 0x90, (uint8_t)(a & 127), (uint8_t)(b ? b & 127 : 100) }; apply_event(m, 3); }
    else if (!strcmp(line, "off")) { uint8_t m[3] = { 0x80, (uint8_t)(a & 127), 0 }; apply_event(m, 3); }
    else if (!strcmp(line, "midi")) {
        uint8_t bytes[64]; unsigned n = 0;
        for (char *s = arg ? arg - 0 : NULL; s && *s && n < sizeof bytes; ) {
            while (*s == ' ') ++s;
            if (!*s) break;
            bytes[n++] = (uint8_t)strtoul(s, &s, 16);
        }
        parse(&fifo_parser, bytes, n, emit_direct);
    }
    else if (!strcmp(line, "status")) logmsg("status: prog %u vol %u ch %u blocks %u events %u writes %u active %u",
                                              cfg.program + 1, cfg.volume, cfg.channel, stat_blocks, stat_events, stat_writes,
                                              synth ? synth_dx7_active(synth) : 0);
    else logmsg("cmd: unknown '%s'", line);
}

static void poll_fifos(void)
{
    uint8_t buf[256];
    ssize_t n;
    if (midi_fd >= 0) {
        while ((n = read(midi_fd, buf, sizeof buf)) > 0) parse(&fifo_parser, buf, (size_t)n, emit_direct);
    }
    if (cmd_fd >= 0) {
        while ((n = read(cmd_fd, buf, sizeof buf)) > 0) {
            for (ssize_t i = 0; i < n; ++i) {
                char c = (char)buf[i];
                if (c == '\n' || c == '\r') {
                    if (cmd_len) { cmd_line[cmd_len] = 0; run_command(cmd_line); cmd_len = 0; }
                } else if (cmd_len < sizeof cmd_line - 1) cmd_line[cmd_len++] = c;
            }
        }
    }
}

static void drain_ring(void)
{
    unsigned r = ring_r;
    unsigned w = __atomic_load_n(&ring_w, __ATOMIC_ACQUIRE);
    while (r != w) {
        struct ev *e = &ring[r % RING];
        apply_event(e->b, e->n);
        ++r;
    }
    __atomic_store_n(&ring_r, r, __ATOMIC_RELEASE);
}

static void render_block(void)
{
    if (cfg.src_fifo) poll_fifos();
    drain_ring();
    memset(pend, 0, sizeof pend);
    if (synth) (void)synth_dx7_render(synth, pend, SYNTH_DX7_BLOCK_SAMPLES);
    pend_pos = 0;
    ++stat_blocks;
}

/* ---- pcm bookkeeping ---- */
struct pcm { snd_pcm_t *pcm; unsigned rate, channels; int format, access; int skip; };
static struct pcm pcms[8];
static snd_pcm_t *active_pcm;

static void *sym(const char *name, int next)
{
    void *p = dlsym(next ? RTLD_NEXT : RTLD_DEFAULT, name);
    if (!p) logmsg("dlsym %s failed", name);
    return p;
}

static int resolve_getters(void)
{
    static int done;
    if (done) return done > 0 ? 0 : -1;
    real.stream = (stream_fn)sym("snd_pcm_stream", 0);
    real.name = (name_fn)sym("snd_pcm_name", 0);
    real.hw_sizeof = (sizeof_fn)sym("snd_pcm_hw_params_sizeof", 0);
    real.hw_current = (current_fn)sym("snd_pcm_hw_params_current", 0);
    real.get_rate = (get_rate_fn)sym("snd_pcm_hw_params_get_rate", 0);
    real.get_channels = (get_uint_fn)sym("snd_pcm_hw_params_get_channels", 0);
    real.get_format = (get_int_fn)sym("snd_pcm_hw_params_get_format", 0);
    real.get_access = (get_int_fn)sym("snd_pcm_hw_params_get_access", 0);
    done = (real.stream && real.hw_sizeof && real.hw_current && real.get_rate &&
            real.get_channels && real.get_format && real.get_access) ? 1 : -1;
    return done > 0 ? 0 : -1;
}

/* Called in the audio thread the first time a pcm handle is written to. */
static struct pcm *lookup(snd_pcm_t *pcm)
{
    struct pcm *slot = NULL;
    for (unsigned i = 0; i < 8; ++i) {
        if (pcms[i].pcm == pcm) return &pcms[i];
        if (!slot && !pcms[i].pcm) slot = &pcms[i];
    }
    if (!slot) return NULL;
    memset(slot, 0, sizeof *slot);
    slot->pcm = pcm;
    slot->skip = 1;
    if (resolve_getters() < 0) return slot;
    if (real.stream(pcm) != PCM_STREAM_PLAYBACK) { logmsg("pcm %p: capture, ignored", (void *)pcm); return slot; }
    const char *name = real.name ? real.name(pcm) : NULL;
    if (!name) name = "?";
    size_t bytes = real.hw_sizeof();
    snd_pcm_hw_params_t *params = alloca(bytes);
    memset(params, 0, bytes);
    if (real.hw_current(pcm, params) < 0) {
        /* no hw params yet: pass through now, look again at the next write */
        logmsg("pcm %p '%s': hw_params_current failed, will retry", (void *)pcm, name);
        memset(slot, 0, sizeof *slot);
        return NULL;
    }
    int dir = 0;
    real.get_rate(params, &slot->rate, &dir);
    real.get_channels(params, &slot->channels);
    real.get_format(params, &slot->format);
    real.get_access(params, &slot->access);
    logmsg("pcm %p '%s': rate %u ch %u format %d access %d", (void *)pcm, name, slot->rate, slot->channels, slot->format, slot->access);
    if (cfg.pcm_match[0] && !strstr(name, cfg.pcm_match)) { logmsg("pcm '%s': does not match pcm=%s, ignored", name, cfg.pcm_match); return slot; }
    if (active_pcm && active_pcm != pcm) { logmsg("pcm '%s': another playback pcm is active, ignored", name); return slot; }
    if (slot->format != FMT_S16_LE && slot->format != FMT_S24_LE && slot->format != FMT_S32_LE && slot->format != FMT_FLOAT_LE) {
        logmsg("pcm '%s': format %d not supported, FM1 off on this pcm", name, slot->format); ++stat_unknown_fmt; return slot;
    }
    if (slot->channels < 1 || slot->channels > 16 || slot->rate < 8000) return slot;
    if (synth_setup(slot->rate) < 0) return slot;
    active_pcm = pcm;
    slot->skip = 0;
    logmsg("pcm '%s': FM1 mixes into this stream", name);
    return slot;
}

/* ---- mixing ---- */
static float q24_gain(void)
{
    return cfg.gain * ((float)cfg.volume / 127.0f) * (1.0f / 16777216.0f);
}

static inline int32_t sat16(float v) { return v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int32_t)v; }
static inline int32_t sat24(float v) { return v > 8388607.0f ? 8388607 : v < -8388608.0f ? -8388608 : (int32_t)v; }
static inline int32_t sat32(float v) { return v >= 2147483647.0f ? INT32_MAX : v <= -2147483648.0f ? INT32_MIN : (int32_t)v; }

/* Add `frames` synth frames to a buffer of `channels` channels with the given
 * stride (in samples) between frames and `coff` between channels. */
static void mix_into(void *base, unsigned frames, unsigned channels, int format, size_t frame_stride, size_t chan_stride)
{
    float g = q24_gain();
    unsigned i = 0;
    while (i < frames) {
        if (pend_pos >= SYNTH_DX7_BLOCK_SAMPLES) render_block();
        unsigned n = SYNTH_DX7_BLOCK_SAMPLES - pend_pos;
        if (n > frames - i) n = frames - i;
        for (unsigned k = 0; k < n; ++k, ++i) {
            float s = (float)pend[pend_pos + k] * g;
            for (unsigned c = 0; c < channels; ++c) {
                size_t idx = (size_t)i * frame_stride + (size_t)c * chan_stride;
                switch (format) {
                case FMT_S16_LE: { int16_t *p = (int16_t *)base + idx; *p = (int16_t)sat16((float)*p + s * 32767.0f); break; }
                case FMT_S24_LE: { int32_t *p = (int32_t *)base + idx; *p = sat24((float)*p + s * 8388607.0f); break; }
                case FMT_S32_LE: { int32_t *p = (int32_t *)base + idx; *p = sat32((float)*p + s * 2147483647.0f); break; }
                case FMT_FLOAT_LE: { float *p = (float *)base + idx; *p += s; break; }
                default: break;
                }
            }
        }
        pend_pos += n;
    }
}

static size_t sample_bytes(int format) { return format == FMT_S16_LE ? 2 : 4; }

/* Scratch copy so the app's own buffer is never modified. */
static void *scratch;
static size_t scratch_bytes;
static void *scratch_get(size_t bytes)
{
    if (bytes > scratch_bytes) {
        void *p = realloc(scratch, bytes);
        if (!p) return NULL;
        scratch = p; scratch_bytes = bytes;
    }
    return scratch;
}

/* ---- interposed ALSA functions ---- */
EXPORT snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
    if (!real.writei) real.writei = (writei_fn)sym("snd_pcm_writei", 1);
    if (!real.writei) return -ENOSYS;
    if (!cfg.enabled || !buffer || size == 0 || size > 1u << 20) return real.writei(pcm, buffer, size);
    struct pcm *p = lookup(pcm);
    if (!p || p->skip) return real.writei(pcm, buffer, size);
    ++stat_writes;
    size_t bytes = (size_t)size * p->channels * sample_bytes(p->format);
    void *copy = scratch_get(bytes);
    if (!copy) return real.writei(pcm, buffer, size);
    memcpy(copy, buffer, bytes);
    mix_into(copy, (unsigned)size, p->channels, p->format, p->channels, 1);
    return real.writei(pcm, copy, size);
}

EXPORT snd_pcm_sframes_t snd_pcm_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
    if (!real.writen) real.writen = (writen_fn)sym("snd_pcm_writen", 1);
    if (!real.writen) return -ENOSYS;
    if (!cfg.enabled || !bufs || size == 0 || size > 1u << 20) return real.writen(pcm, bufs, size);
    struct pcm *p = lookup(pcm);
    if (!p || p->skip) return real.writen(pcm, bufs, size);
    ++stat_writes;
    size_t per = (size_t)size * sample_bytes(p->format);
    void *copy = scratch_get(per * p->channels + sizeof(void *) * p->channels);
    if (!copy) return real.writen(pcm, bufs, size);
    void **ptrs = (void **)((char *)copy + per * p->channels);
    for (unsigned c = 0; c < p->channels; ++c) {
        ptrs[c] = (char *)copy + per * c;
        memcpy(ptrs[c], bufs[c], per);
    }
    mix_into(copy, (unsigned)size, p->channels, p->format, 1, size);
    return real.writen(pcm, ptrs, size);
}

EXPORT int snd_pcm_close(snd_pcm_t *pcm)
{
    if (!real.close) real.close = (close_fn)sym("snd_pcm_close", 1);
    for (unsigned i = 0; i < 8; ++i) if (pcms[i].pcm == pcm) memset(&pcms[i], 0, sizeof pcms[i]);
    if (active_pcm == pcm) { active_pcm = NULL; logmsg("pcm %p closed", (void *)pcm); }
    return real.close ? real.close(pcm) : -ENOSYS;
}

static struct parser in_parser, out_parser;

EXPORT ssize_t snd_rawmidi_read(snd_rawmidi_t *rmidi, void *buffer, size_t size)
{
    if (!real.rm_read) real.rm_read = (rawmidi_read_fn)sym("snd_rawmidi_read", 1);
    if (!real.rm_read) return -ENOSYS;
    ssize_t n = real.rm_read(rmidi, buffer, size);
    if (n > 0 && cfg.enabled && cfg.src_in) parse(&in_parser, buffer, (size_t)n, emit_filtered);
    return n;
}

EXPORT ssize_t snd_rawmidi_write(snd_rawmidi_t *rmidi, const void *buffer, size_t size)
{
    if (!real.rm_write) real.rm_write = (rawmidi_write_fn)sym("snd_rawmidi_write", 1);
    if (!real.rm_write) return -ENOSYS;
    if (cfg.enabled && cfg.src_out && buffer && size) parse(&out_parser, buffer, size, emit_filtered);
    return real.rm_write(rmidi, buffer, size);
}

/* ---- configuration file ---- */
static void load_config(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[320];
    if (!f) { logmsg("conf: %s not found, defaults", path); return; }
    while (fgets(line, sizeof line, f)) {
        char *s = line, *eq, *val;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '#' || *s == '\n' || !*s) continue;
        eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0; val = eq + 1;
        while (*val == ' ') ++val;
        val[strcspn(val, "\r\n")] = 0;
        for (char *e = eq - 1; e >= s && (*e == ' ' || *e == '\t'); --e) *e = 0;
        long v = strtol(val, NULL, 0);
        if (!strcmp(s, "program")) cfg.program = (unsigned)((v < 1 ? 1 : v > 128 ? 128 : v) - 1);
        else if (!strcmp(s, "volume")) cfg.volume = (unsigned)(v < 0 ? 0 : v > 127 ? 127 : v);
        else if (!strcmp(s, "channel")) cfg.channel = (unsigned)(v < 0 ? 0 : v > 16 ? 16 : v);
        else if (!strcmp(s, "voices")) cfg.voices = (unsigned)(v < 1 ? 1 : v > 16 ? 16 : v);
        else if (!strcmp(s, "rate")) cfg.rate = (unsigned)(v < 8000 ? 44100 : v);
        else if (!strcmp(s, "mono")) cfg.mono = v != 0;
        else if (!strcmp(s, "transpose")) cfg.transpose = (unsigned)(64 + (v < -48 ? -48 : v > 48 ? 48 : v));
        else if (!strcmp(s, "gain")) { float g = strtof(val, NULL); cfg.gain = g < 0.f ? 0.f : g > 4.f ? 4.f : g; }
        else if (!strcmp(s, "midi_in")) cfg.src_in = v != 0;
        else if (!strcmp(s, "midi_out")) cfg.src_out = v != 0;
        else if (!strcmp(s, "fifo")) cfg.src_fifo = v != 0;
        else if (!strcmp(s, "pcm")) { strncpy(cfg.pcm_match, val, sizeof cfg.pcm_match - 1); cfg.pcm_match[sizeof cfg.pcm_match - 1] = 0; }
        else if (!strcmp(s, "bank")) { strncpy(cfg.bank_path, val, sizeof cfg.bank_path - 1); cfg.bank_path[sizeof cfg.bank_path - 1] = 0; }
        else if (!strcmp(s, "enabled")) cfg.enabled = v != 0;
        else logmsg("conf: unknown key '%s'", s);
    }
    fclose(f);
}

static void load_bank(void)
{
    char path[512];
    uint8_t data[4104];
    if (!cfg.bank_path[0]) return;
    if (cfg.bank_path[0] == '/') snprintf(path, sizeof path, "%s", cfg.bank_path);
    else snprintf(path, sizeof path, "%s/%s", cfg.dir, cfg.bank_path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { logmsg("bank: cannot open %s", path); return; }
    ssize_t n = read(fd, data, sizeof data);
    close(fd);
    if (n <= 0 || fm1_bank_load(0, data, (size_t)n) < 0) { logmsg("bank: %s is not a 4096/4104-byte DX7 bank", path); return; }
    logmsg("bank: loaded %s into programs 1-32", path);
}

static int open_fifo(const char *path)
{
    if (mkfifo(path, 0666) < 0 && errno != EEXIST) { logmsg("fifo: mkfifo %s failed (%d)", path, errno); return -1; }
    chmod(path, 0666);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) logmsg("fifo: open %s failed (%d)", path, errno);
    return fd;
}

__attribute__((constructor)) static void fm1mix_init(void)
{
    const char *dir = getenv("FM1MIX_DIR");
    char path[512];
    log_fd = open("/tmp/fm1mix.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    logmsg("fm1mix " FM1MIX_VERSION " loaded in pid %d", (int)getpid());
    if (dir && *dir) { strncpy(cfg.dir, dir, sizeof cfg.dir - 1); cfg.dir[sizeof cfg.dir - 1] = 0; }
    else strcpy(cfg.dir, "/tmp");
    snprintf(path, sizeof path, "%s/fm1.conf", cfg.dir);
    load_config(path);
    if (!cfg.enabled) { logmsg("conf: enabled=0, FM1 off"); return; }
    load_bank();
    fm1_bank_patch(patch, cfg.program);
    if (cfg.src_fifo) { cmd_fd = open_fifo("/tmp/fm1.cmd"); midi_fd = open_fifo("/tmp/fm1.midi"); }
    /* Create the synth now, outside the audio thread, with the expected rate.
     * It is rebuilt only if the pcm turns out to run at another rate. */
    (void)synth_setup(cfg.rate);
    logmsg("conf: program %u volume %u channel %u voices %u midi_in %d midi_out %d fifo %d gain %.2f",
           cfg.program + 1, cfg.volume, cfg.channel, cfg.voices, cfg.src_in, cfg.src_out, cfg.src_fifo, (double)cfg.gain);
}
