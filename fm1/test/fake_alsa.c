/* SPDX-License-Identifier: GPL-3.0-only
 * A stand-in for libasound: records what the "app" writes and feeds MIDI
 * bytes to snd_rawmidi_read. Only the entry points fm1mix touches exist. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

typedef struct _snd_pcm { int stream; } snd_pcm_t;
static struct { unsigned rate, channels; int format, access; } params;
static FILE *out;
static uint8_t midi_q[256]; static size_t midi_n;

static size_t sample_bytes(void) { return params.format == 2 ? 2 : 4; }

void fake_setup(const char *path, unsigned rate, unsigned channels, int format, int access)
{
    if (out) fclose(out);
    out = fopen(path, "wb");
    params.rate = rate; params.channels = channels; params.format = format; params.access = access;
}
void fake_midi_in(const uint8_t *bytes, size_t n) { if (n <= sizeof midi_q) { memcpy(midi_q, bytes, n); midi_n = n; } }
void fake_flush(void) { if (out) fflush(out); }

int snd_pcm_stream(snd_pcm_t *pcm) { return pcm->stream; }
const char *snd_pcm_name(snd_pcm_t *pcm) { return pcm->stream ? "hw:0,0 capture (fake)" : "hw:0,0 playback (fake)"; }
size_t snd_pcm_hw_params_sizeof(void) { return 64; }
int snd_pcm_hw_params_current(snd_pcm_t *pcm, void *p) { return 0; }
int snd_pcm_hw_params_get_rate(const void *p, unsigned *v, int *dir) { *v = params.rate; if (dir) *dir = 0; return 0; }
int snd_pcm_hw_params_get_channels(const void *p, unsigned *v) { *v = params.channels; return 0; }
int snd_pcm_hw_params_get_format(const void *p, int *v) { *v = params.format; return 0; }
int snd_pcm_hw_params_get_access(const void *p, int *v) { *v = params.access; return 0; }
long snd_pcm_writei(snd_pcm_t *pcm, const void *buf, unsigned long frames)
{
    if (out) fwrite(buf, sample_bytes() * params.channels, frames, out);
    return (long)frames;
}
long snd_pcm_writen(snd_pcm_t *pcm, void **bufs, unsigned long frames)
{
    size_t sb = sample_bytes();
    for (unsigned long i = 0; i < frames && out; ++i)
        for (unsigned c = 0; c < params.channels; ++c) fwrite((char *)bufs[c] + i * sb, sb, 1, out);
    return (long)frames;
}
int snd_pcm_close(snd_pcm_t *pcm) { return 0; }
ssize_t snd_rawmidi_read(void *r, void *buf, size_t n)
{
    size_t k = midi_n < n ? midi_n : n;
    memcpy(buf, midi_q, k); midi_n = 0;
    return (ssize_t)k;
}
ssize_t snd_rawmidi_write(void *r, const void *buf, size_t n) { return (ssize_t)n; }
