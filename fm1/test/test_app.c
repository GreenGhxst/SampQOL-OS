/* SPDX-License-Identifier: GPL-3.0-only
 * Plays the role of the MPC Sample app: writes silence (and one marker
 * section with a DC offset) to ALSA, receives MIDI, sends MIDI, and uses the
 * fm1mix FIFO. check.py judges the recorded output. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

typedef struct _snd_pcm { int stream; } snd_pcm_t;
void fake_setup(const char *path, unsigned rate, unsigned channels, int format, int access);
void fake_midi_in(const uint8_t *bytes, size_t n);
void fake_flush(void);
long snd_pcm_writei(snd_pcm_t *, const void *, unsigned long);
long snd_pcm_writen(snd_pcm_t *, void **, unsigned long);
int snd_pcm_close(snd_pcm_t *);
ssize_t snd_rawmidi_read(void *, void *, size_t);
ssize_t snd_rawmidi_write(void *, const void *, size_t);

enum { FRAMES = 128 };

static void fifo(const char *text)
{
    int fd = open("/tmp/fm1.cmd", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open /tmp/fm1.cmd (is fm1mix loaded?)"); exit(2); }
    if (write(fd, text, strlen(text)) < 0) perror("write fifo");
    close(fd);
}

static void midi_in(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t m[3] = { a, b, c }, buf[16];
    fake_midi_in(m, 3);
    (void)snd_rawmidi_read(NULL, buf, sizeof buf);
}

static int32_t buf32[FRAMES * 2];
static int16_t buf16l[FRAMES], buf16r[FRAMES];

static void write32(snd_pcm_t *pcm, unsigned blocks, int32_t dc)
{
    for (unsigned i = 0; i < FRAMES * 2; ++i) buf32[i] = dc;
    for (unsigned b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < FRAMES * 2; ++i) buf32[i] = dc;
        if (snd_pcm_writei(pcm, buf32, FRAMES) != FRAMES) { fputs("writei failed\n", stderr); exit(1); }
    }
}

int main(void)
{
    snd_pcm_t play = { 0 }, cap = { 1 };
    FILE *plan = fopen("plan.txt", "w");
    unsigned pos = 0;
    if (!plan) return 1;
    fprintf(plan, "stream out1.raw i32\n");

    /* ---- stream 1: S32_LE interleaved, 44.1 kHz ---- */
    fake_setup("out1.raw", 44100, 2, 10, 3);
    /* a capture handle must be ignored */
    (void)snd_pcm_writei(&cap, buf32, 0);

    fprintf(plan, "silence %u %u\n", pos, pos + 50 * FRAMES); write32(&play, 50, 0); pos += 50 * FRAMES;

    midi_in(0x90, 60, 100);                                   /* MIDI IN note on, C4 */
    fprintf(plan, "loud %u %u\n", pos, pos + 200 * FRAMES); write32(&play, 200, 0); pos += 200 * FRAMES;
    midi_in(0x80, 60, 0);
    write32(&play, 300, 0); pos += 300 * FRAMES;                /* release tail */
    fprintf(plan, "quiet %u %u\n", pos, pos + 50 * FRAMES); write32(&play, 50, 0); pos += 50 * FRAMES;

    fifo("prog 10\nvol 110\non 67 110\n");                   /* FIFO control */
    fprintf(plan, "loud %u %u\n", pos, pos + 200 * FRAMES); write32(&play, 200, 0); pos += 200 * FRAMES;
    fifo("off 67\npanic\n");
    write32(&play, 20, 0); pos += 20 * FRAMES;

    (void)snd_rawmidi_write(NULL, "\x91\x40\x70", 3);         /* MIDI OUT tap, channel 2 */
    fprintf(plan, "loud %u %u\n", pos, pos + 200 * FRAMES); write32(&play, 200, 0); pos += 200 * FRAMES;
    (void)snd_rawmidi_write(NULL, "\x81\x40\x00", 3);
    fifo("panic\n");
    write32(&play, 5, 0); pos += 5 * FRAMES;

    fifo("ch 5\n");                                           /* channel filter: ch 2 ignored */
    write32(&play, 5, 0); pos += 5 * FRAMES;                    /* FIFO commands apply at the next block */
    (void)snd_rawmidi_write(NULL, "\x91\x40\x70", 3);
    fprintf(plan, "silence %u %u\n", pos, pos + 100 * FRAMES); write32(&play, 100, 0); pos += 100 * FRAMES;
    (void)snd_rawmidi_write(NULL, "\x81\x40\x00", 3);
    (void)snd_rawmidi_write(NULL, "\x94\x40\x70", 3);         /* channel 5 passes */
    fprintf(plan, "loud %u %u\n", pos, pos + 200 * FRAMES); write32(&play, 200, 0); pos += 200 * FRAMES;
    fifo("panic\nch 0\n");
    write32(&play, 5, 0); pos += 5 * FRAMES;

    /* the app's own signal must pass unchanged when the synth is silent */
    fprintf(plan, "dc %u %u 1000\n", pos, pos + 50 * FRAMES); write32(&play, 50, 1000); pos += 50 * FRAMES;
    fifo("status\n");
    write32(&play, 1, 0);
    fake_flush();
    snd_pcm_close(&play);

    /* ---- stream 2: S16_LE non-interleaved, 48 kHz (synth is rebuilt) ---- */
    snd_pcm_t play2 = { 0 };
    void *bufs[2] = { buf16l, buf16r };
    fake_setup("out2.raw", 48000, 2, 2, 4);
    fprintf(plan, "stream out2.raw i16\n");
    fprintf(plan, "silence %u %u\n", 0, 50 * FRAMES);
    for (unsigned b = 0; b < 50; ++b) { memset(buf16l, 0, sizeof buf16l); memset(buf16r, 0, sizeof buf16r); snd_pcm_writen(&play2, bufs, FRAMES); }
    fifo("on 48 120\n");
    fprintf(plan, "loud %u %u\n", 50 * FRAMES, 250 * FRAMES);
    for (unsigned b = 0; b < 200; ++b) { memset(buf16l, 0, sizeof buf16l); memset(buf16r, 0, sizeof buf16r); snd_pcm_writen(&play2, bufs, FRAMES); }
    fifo("panic\n");
    fake_flush();
    fclose(plan);
    puts("test_app: done");
    return 0;
}
