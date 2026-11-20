/* SPDX-License-Identifier: MIT
 * Faulty "plug-ins" for the negative tests. One file, built several times
 * with a different BAD_KIND:
 *   BAD_NO_ENTRY      a shared library without any VST2 entry point
 *   BAD_WRONG_MAGIC   returns an effect whose magic is not 'VstP'
 *   BAD_CRASH_OPEN    segfaults in MVST_OP_OPEN
 *   BAD_HANG          never returns from VSTPluginMain
 *   BAD_CRASH_PROCESS segfaults in the third processReplacing call
 *   BAD_NAN_OUTPUT    writes NaN and infinity to its output
 *   BAD_HUGE_IO       claims 1000 inputs and outputs
 *   BAD_NO_REPLACING  has no processReplacing
 *   BAD_MISSING_DEP   a normal entry point, but linked against a library
 *                     that is deleted after the build
 */
#include "mock_common.h"

#include <math.h>
#include <unistd.h>

#if BAD_KIND == BAD_NO_ENTRY
MOCK_EXPORT int mock_not_a_plugin(void) { return 42; }
#else

#if BAD_KIND == BAD_MISSING_DEP
int mock_fakedep_value(void); /* from libmpcvst_fakedep.so, deleted after linking */
#endif

typedef struct {
    mvst_effect effect;
    int blocks;
} bad_t;

/* A deliberate segmentation fault the compiler cannot see through. */
static __attribute__((unused)) void deliberate_crash(void)
{
    volatile uintptr_t where = 16;
    *(volatile int *)where = 1;
}

static intptr_t dispatcher(mvst_effect *e, int32_t op, int32_t index, intptr_t value, void *ptr, float opt)
{
    (void)index; (void)value; (void)opt;
    bad_t *s = (bad_t *)e->object;
    switch (op) {
    case MVST_OP_OPEN:
#if BAD_KIND == BAD_CRASH_OPEN
        deliberate_crash();
#endif
        return 0;
    case MVST_OP_CLOSE: free(s); e->object = NULL; return 0;
    case MVST_OP_GET_EFFECT_NAME: mock_copy(ptr, "Bad Plugin", MVST_MAX_NAME_LEN); return 1;
    case MVST_OP_GET_VENDOR_STRING: mock_copy(ptr, "tests", MVST_MAX_VENDOR_LEN); return 1;
    case MVST_OP_GET_PLUG_CATEGORY: return MVST_CATEGORY_SYNTH;
    case MVST_OP_GET_VST_VERSION: return 2400;
    default: return 0;
    }
}

static __attribute__((unused)) void process_replacing(mvst_effect *e, float **in, float **out, int32_t frames)
{
    (void)in;
    bad_t *s = (bad_t *)e->object;
    s->blocks++;
#if BAD_KIND == BAD_CRASH_PROCESS
    if (s->blocks >= 3)
        deliberate_crash();
#endif
    for (int32_t i = 0; i < frames; i++) {
#if BAD_KIND == BAD_NAN_OUTPUT
        out[0][i] = (i & 1) ? NAN : INFINITY;
        out[1][i] = -INFINITY;
#else
        out[0][i] = out[1][i] = 0.1f;
#endif
    }
}

static void set_parameter(mvst_effect *e, int32_t index, float v) { (void)e; (void)index; (void)v; }
static float get_parameter(mvst_effect *e, int32_t index) { (void)e; (void)index; return 0.0f; }

MOCK_EXPORT mvst_effect *VSTPluginMain(mvst_host_fn host)
{
    (void)host;
#if BAD_KIND == BAD_HANG
    for (;;)
        pause();
#endif
#if BAD_KIND == BAD_MISSING_DEP
    if (mock_fakedep_value() == -1)
        return NULL;
#endif
    bad_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    mvst_effect *e = &s->effect;
#if BAD_KIND == BAD_WRONG_MAGIC
    e->magic = 0x12345678;
#else
    e->magic = MVST_MAGIC;
#endif
    e->dispatcher = dispatcher;
    e->set_parameter = set_parameter;
    e->get_parameter = get_parameter;
    e->num_programs = 1;
    e->num_params = 0;
#if BAD_KIND == BAD_HUGE_IO
    e->num_inputs = 1000;
    e->num_outputs = 1000;
#else
    e->num_inputs = 0;
    e->num_outputs = 2;
#endif
#if BAD_KIND == BAD_NO_REPLACING
    e->flags = MVST_FLAG_IS_SYNTH;
#else
    e->flags = MVST_FLAG_CAN_REPLACING | MVST_FLAG_IS_SYNTH;
    e->process_replacing = process_replacing;
#endif
    e->object = s;
    e->unique_id = 0x42414421 + BAD_KIND;
    e->version = 1;
    return e;
}
#endif
