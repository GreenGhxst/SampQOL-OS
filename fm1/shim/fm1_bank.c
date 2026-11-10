/* SPDX-License-Identifier: GPL-3.0-only
 * DX7 voice bank for fm1mix. The factory bank decoder and the VMEM unpack
 * follow app/fm_factory.c and app/fm_library.c of m-vave/fm1_open
 * (GPL-3.0-only WITH GPL-3.0-linking-exception); the packed bank data is
 * app/fm_factory.inc (Apache-2.0). */
#include "fm1_bank.h"
#include <string.h>

#include "fm_factory.inc" /* static const uint8_t factory_packed[] */

enum { PROGRAMS = 128, VMEM = 128, EXPANDED = 156 };

static uint8_t factory_raw[PROGRAMS * VMEM];
static unsigned factory_ready;
static uint8_t user_raw[PROGRAMS * VMEM];
static uint8_t user_loaded[4];

static const uint8_t op_max[21] = {99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14};
static const uint8_t global_max[19] = {99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7,48};

static void factory_init(void)
{
    if (factory_ready) return;
    uint8_t *out = factory_raw;
    const uint8_t *end = factory_raw + sizeof factory_raw;
    for (size_t at = 0; at < sizeof factory_packed && out < end;) {
        unsigned token = factory_packed[at++];
        if (token < 128) { *out++ = (uint8_t)token; continue; }
        if (at >= sizeof factory_packed) break;
        unsigned low = factory_packed[at++], distance = low & 127U;
        if (low & 128U) {
            if (at >= sizeof factory_packed) break;
            distance |= (unsigned)factory_packed[at++] << 7;
        }
        if (distance == 0 || out - factory_raw < (long)distance) break;
        for (unsigned n = token - 128 + 3; n && out < end; --n) { *out = out[-(int)distance]; ++out; }
    }
    factory_ready = 1;
}

const uint8_t *fm1_bank_factory_raw(unsigned program)
{
    factory_init();
    return factory_raw + (program % PROGRAMS) * VMEM;
}

void fm1_bank_unpack(uint8_t p[156], const uint8_t b[128])
{
    for (unsigned op = 0; op < 6; ++op) {
        const uint8_t *v = b + op * 17; uint8_t *q = p + op * 21;
        memcpy(q, v, 11); q[11] = v[11] & 3U; q[12] = (v[11] >> 2) & 3U;
        q[13] = v[12] & 7U; q[20] = (v[12] >> 3) & 15U;
        q[14] = v[13] & 3U; q[15] = (v[13] >> 2) & 7U; q[16] = v[14];
        q[17] = v[15] & 1U; q[18] = (v[15] >> 1) & 31U; q[19] = v[16];
    }
    memcpy(p + 126, b + 102, 8); p[134] = b[110] & 31U;
    p[135] = b[111] & 7U; p[136] = (b[111] >> 3) & 1U;
    memcpy(p + 137, b + 112, 4); p[141] = b[116] & 1U;
    p[142] = (b[116] >> 1) & 7U; p[143] = (b[116] >> 4) & 7U;
    memcpy(p + 144, b + 117, 11); p[155] = 63;
    for (unsigned i = 0; i < 145; ++i) {
        unsigned limit = i < 126 ? op_max[i % 21] : global_max[i - 126];
        if (p[i] > limit) p[i] = (uint8_t)limit;
    }
}

int fm1_bank_load(unsigned bank, const uint8_t *data, size_t bytes)
{
    if (bank > 3 || !data) return -1;
    if (bytes == 4104 && data[0] == 0xF0 && data[1] == 0x43 && data[3] == 0x09) data += 6;
    else if (bytes != 4096) return -1;
    for (unsigned i = 0; i < 4096; ++i) if (data[i] > 127) return -1;
    memcpy(user_raw + bank * 32 * VMEM, data, 4096);
    user_loaded[bank] = 1;
    return 0;
}

void fm1_bank_patch(uint8_t patch[156], unsigned program)
{
    program %= PROGRAMS;
    const uint8_t *raw = user_loaded[program / 32] ? user_raw + program * VMEM
                                                   : fm1_bank_factory_raw(program);
    fm1_bank_unpack(patch, raw);
}

void fm1_bank_name(char name[11], unsigned program)
{
    program %= PROGRAMS;
    const uint8_t *raw = user_loaded[program / 32] ? user_raw + program * VMEM
                                                   : fm1_bank_factory_raw(program);
    for (unsigned i = 0; i < 10; ++i) {
        uint8_t c = raw[118 + i];
        name[i] = (c >= 32 && c < 127) ? (char)c : ' ';
    }
    name[10] = 0;
}
