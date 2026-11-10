/* SPDX-License-Identifier: GPL-3.0-only
 * Confirms the ALSA constants and types that fm1mix.c declares by hand. */
#include <alsa/asoundlib.h>
#include <stdio.h>
_Static_assert(SND_PCM_STREAM_PLAYBACK == 0, "stream");
_Static_assert(SND_PCM_ACCESS_RW_INTERLEAVED == 3, "access i");
_Static_assert(SND_PCM_ACCESS_RW_NONINTERLEAVED == 4, "access n");
_Static_assert(SND_PCM_FORMAT_S16_LE == 2, "s16");
_Static_assert(SND_PCM_FORMAT_S24_LE == 6, "s24");
_Static_assert(SND_PCM_FORMAT_S32_LE == 10, "s32");
_Static_assert(SND_PCM_FORMAT_FLOAT_LE == 14, "float");
_Static_assert(sizeof(snd_pcm_sframes_t) == sizeof(long), "sframes");
_Static_assert(sizeof(snd_pcm_uframes_t) == sizeof(unsigned long), "uframes");
int main(void) { puts("ALSA ABI constants match alsa/pcm.h"); return 0; }
