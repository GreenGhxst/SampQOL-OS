/* SPDX-License-Identifier: MIT
 * mock_effect: a stereo VST2 effect for tests: gain, a one-pole low-pass
 * and a reported latency of 0. Two parameters, no chunk (parameters are the
 * state), one program. */
#include "mock_common.h"

#include <math.h>
#include <stdio.h>

#define PARAMS 2
enum { P_GAIN, P_CUTOFF };

typedef struct {
    mvst_effect effect;
    float rate;
    float params[PARAMS];
    float z[2];
    char program_name[MVST_MAX_PROGRAM_NAME_LEN];
} fx_t;

static fx_t *self(mvst_effect *e) { return (fx_t *)e->object; }

static intptr_t dispatcher(mvst_effect *e, int32_t op, int32_t index, intptr_t value, void *ptr, float opt)
{
    fx_t *s = self(e);
    (void)value;
    switch (op) {
    case MVST_OP_OPEN: return 0;
    case MVST_OP_CLOSE: free(s); e->object = NULL; return 0;
    case MVST_OP_GET_PROGRAM: return 0;
    case MVST_OP_GET_PROGRAM_NAME: mock_copy(ptr, s->program_name, MVST_MAX_PROGRAM_NAME_LEN); return 0;
    case MVST_OP_GET_PROGRAM_NAME_INDEXED: if (index != 0) return 0; mock_copy(ptr, s->program_name, MVST_MAX_PROGRAM_NAME_LEN); return 1;
    case MVST_OP_GET_PARAM_NAME: mock_copy(ptr, index == P_GAIN ? "Gain" : index == P_CUTOFF ? "Cutoff" : "", MVST_MAX_NAME_LEN); return 0;
    case MVST_OP_GET_PARAM_LABEL: mock_copy(ptr, index == P_GAIN ? "dB" : "Hz", MVST_MAX_LABEL_LEN); return 0;
    case MVST_OP_GET_PARAM_DISPLAY: {
        char b[32];
        if (index == P_GAIN) snprintf(b, sizeof b, "%.1f", -24.0f + 36.0f * s->params[P_GAIN]);
        else snprintf(b, sizeof b, "%.0f", 50.0f * powf(400.0f, s->params[P_CUTOFF]));
        mock_copy(ptr, b, 32);
        return 0;
    }
    case MVST_OP_SET_SAMPLE_RATE: s->rate = opt > 0 ? opt : 44100.0f; return 0;
    case MVST_OP_SET_BLOCK_SIZE: return 0;
    case MVST_OP_MAINS_CHANGED: s->z[0] = s->z[1] = 0.0f; return 0;
    case MVST_OP_CAN_BE_AUTOMATED: return 1;
    case MVST_OP_GET_PLUG_CATEGORY: return MVST_CATEGORY_EFFECT;
    case MVST_OP_GET_EFFECT_NAME: mock_copy(ptr, "Mock Effect", MVST_MAX_NAME_LEN); return 1;
    case MVST_OP_GET_VENDOR_STRING: mock_copy(ptr, "MPC Sample VST Host project", MVST_MAX_VENDOR_LEN); return 1;
    case MVST_OP_GET_PRODUCT_STRING: mock_copy(ptr, "mock_effect", MVST_MAX_PRODUCT_LEN); return 1;
    case MVST_OP_GET_VENDOR_VERSION: return 100;
    case MVST_OP_CAN_DO: return -1;
    case MVST_OP_GET_TAIL_SIZE: return 0;
    case MVST_OP_GET_VST_VERSION: return 2400;
    default: return 0;
    }
}

static void process_replacing(mvst_effect *e, float **in, float **out, int32_t frames)
{
    fx_t *s = self(e);
    float gain = powf(10.0f, (-24.0f + 36.0f * s->params[P_GAIN]) / 20.0f);
    float hz = 50.0f * powf(400.0f, s->params[P_CUTOFF]);
    float a = 1.0f - expf(-6.2831853f * hz / s->rate);
    for (int c = 0; c < 2; c++) {
        float z = s->z[c];
        for (int32_t i = 0; i < frames; i++) {
            z += a * (in[c][i] * gain - z);
            out[c][i] = z;
        }
        s->z[c] = z;
    }
}

static void set_parameter(mvst_effect *e, int32_t index, float v)
{
    fx_t *s = self(e);
    if (index >= 0 && index < PARAMS) s->params[index] = mock_clamp01(v);
}

static float get_parameter(mvst_effect *e, int32_t index)
{
    fx_t *s = self(e);
    return index >= 0 && index < PARAMS ? s->params[index] : 0.0f;
}

MOCK_EXPORT mvst_effect *VSTPluginMain(mvst_host_fn host)
{
    (void)host;
    fx_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->rate = 44100.0f;
    s->params[P_GAIN] = 24.0f / 36.0f; /* 0 dB */
    s->params[P_CUTOFF] = 1.0f;        /* 20 kHz: nearly flat */
    mock_copy(s->program_name, "Default", MVST_MAX_PROGRAM_NAME_LEN);
    mvst_effect *e = &s->effect;
    e->magic = MVST_MAGIC;
    e->dispatcher = dispatcher;
    e->set_parameter = set_parameter;
    e->get_parameter = get_parameter;
    e->num_programs = 1;
    e->num_params = PARAMS;
    e->num_inputs = 2;
    e->num_outputs = 2;
    e->flags = MVST_FLAG_CAN_REPLACING;
    e->object = s;
    e->unique_id = (int32_t)(('m' << 24) | ('k' << 16) | ('F' << 8) | '1');
    e->version = 100;
    e->process_replacing = process_replacing;
    return e;
}
