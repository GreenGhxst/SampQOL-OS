/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_BANK_H
#define FM1_BANK_H
#include <stdint.h>
#include <stddef.h>

/* 128-byte DX7 VMEM voice -> 156-byte expanded voice, as in fm1_open. */
void fm1_bank_unpack(uint8_t patch[156], const uint8_t vmem[128]);
/* Factory voice (0..127) of the FM1 V15 bank, as VMEM. */
const uint8_t *fm1_bank_factory_raw(unsigned program);
/* Load one 32-voice bank (bank 0..3 = programs 0..31, 32..63, ...).
 * Accepts a 4096-byte VMEM block or a 4104-byte DX7 bulk dump (.syx).
 * Returns 0 on success, -1 on a bad size or 8-bit data. */
int fm1_bank_load(unsigned bank, const uint8_t *data, size_t bytes);
/* Expanded voice for program 0..127: user bank if loaded, else factory. */
void fm1_bank_patch(uint8_t patch[156], unsigned program);
/* 10-character voice name of a program, NUL-terminated (11 bytes). */
void fm1_bank_name(char name[11], unsigned program);
#endif
