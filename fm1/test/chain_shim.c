/* SPDX-License-Identifier: GPL-3.0-only
 * Stands in for mpcqol.so: another LD_PRELOAD library that interposes the
 * same ALSA calls and forwards with RTLD_NEXT. Loaded in front of fm1mix
 * to prove the two chain correctly. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <sys/types.h>
static int writes, reads, mwrites;
long snd_pcm_writei(void *pcm, const void *buf, unsigned long n)
{
    static long (*next)(void *, const void *, unsigned long);
    if (!next) next = dlsym(RTLD_NEXT, "snd_pcm_writei");
    ++writes;
    return next(pcm, buf, n);
}
ssize_t snd_rawmidi_read(void *r, void *buf, size_t n)
{
    static ssize_t (*next)(void *, void *, size_t);
    if (!next) next = dlsym(RTLD_NEXT, "snd_rawmidi_read");
    ++reads;
    return next(r, buf, n);
}
ssize_t snd_rawmidi_write(void *r, const void *buf, size_t n)
{
    static ssize_t (*next)(void *, const void *, size_t);
    if (!next) next = dlsym(RTLD_NEXT, "snd_rawmidi_write");
    ++mwrites;
    return next(r, buf, n);
}
__attribute__((destructor)) static void done(void)
{
    /* not reached before the output is checked; counts are for debugging */
    (void)writes; (void)reads; (void)mwrites;
}
