/* SPDX-License-Identifier: MIT
 * mock_instrument: a small polyphonic VST2 instrument for tests and
 * benchmarks. 8 voices, three waveforms, an amplitude envelope, pitch bend,
 * sustain pedal and velocity. It has 4 parameters, 4 programs and a state
 * chunk, so every host path (parameters, programs, chunks, MIDI, transport)
 * can be exercised with known results.
 *
 * Deterministic: the same MIDI at the same rate renders the same samples,
 * whatever the block sizes (the tests rely on that).
 */
#include "mock_common.h"

#include <math.h>
#include <stdio.h>

#define VOICES 8
#define PARAMS 4
#define PROGRAMS 4
enum { P_VOLUME, P_WAVE, P_DECAY, P_DETUNE };

typedef struct {
    int active, note, channel;
    float velocity, phase, phase2, env, env_step;
    int released;
    uint32_t age;
} voice_t;

typedef struct {
    mvst_effect effect;
    mvst_host_fn host;
    float rate;
    int block;
    float params[PARAMS];
    int program;
    char program_names[PROGRAMS][MVST_MAX_PROGRAM_NAME_LEN];
    float program_params[PROGRAMS][PARAMS];
    voice_t voices[VOICES];
    float bend[16];     /* semitones */
    int sustain[16];
    uint32_t clock;
    unsigned char chunk[64];
    int blocks_processed;
    int last_num_events;
    struct { int delta; unsigned char d[3]; } pending[64];
    int num_pending;
    int transport_playing;  /* read through MVST_HOST_GET_TIME each block */
    double last_tempo;
} synth_t;

static const float DEFAULTS[PROGRAMS][PARAMS] = {
    {0.8f, 0.0f, 0.5f, 0.0f},  /* Sine */
    {0.6f, 0.5f, 0.3f, 0.1f},  /* Saw */
    {0.5f, 1.0f, 0.8f, 0.0f},  /* Square */
    {0.7f, 0.5f, 0.1f, 0.3f},  /* Pluck */
};
static const char *const NAMES[PROGRAMS] = {"Sine", "Saw", "Square", "Pluck"};

static synth_t *self(mvst_effect *e) { return (synth_t *)e->object; }

static void note_on(synth_t *s, int ch, int note, int vel)
{
    voice_t *v = NULL;
    uint32_t oldest = 0;
    for (int i = 0; i < VOICES; i++) {
        if (!s->voices[i].active) { v = &s->voices[i]; break; }
        if (!v || s->voices[i].age < oldest) { v = &s->voices[i]; oldest = s->voices[i].age; }
    }
    v->active = 1;
    v->note = note;
    v->channel = ch;
    v->velocity = (float)vel / 127.0f;
    v->phase = v->phase2 = 0.0f;
    v->env = 1.0f;
    v->released = 0;
    v->age = s->clock++;
    float decay_s = 0.05f + 2.0f * s->params[P_DECAY];
    v->env_step = 1.0f / (decay_s * s->rate);
}

static void note_off(synth_t *s, int ch, int note)
{
    for (int i = 0; i < VOICES; i++)
        if (s->voices[i].active && s->voices[i].note == note && s->voices[i].channel == ch && !s->voices[i].released)
            s->voices[i].released = 1;
}

static void all_off(synth_t *s, int ch)
{
    for (int i = 0; i < VOICES; i++)
        if (s->voices[i].active && (ch < 0 || s->voices[i].channel == ch))
            s->voices[i].released = 1;
}

static void handle_midi(synth_t *s, const unsigned char *d)
{
    int st = d[0] & 0xF0, ch = d[0] & 0x0F;
    switch (st) {
    case 0x90: if (d[2]) note_on(s, ch, d[1], d[2]); else note_off(s, ch, d[1]); break;
    case 0x80: note_off(s, ch, d[1]); break;
    case 0xB0:
        if (d[1] == 64) s->sustain[ch] = d[2] >= 64;
        else if (d[1] == 123 || d[1] == 120) all_off(s, ch);
        break;
    case 0xE0: s->bend[ch] = ((float)((d[2] << 7) | d[1]) - 8192.0f) / 8192.0f * 2.0f; break;
    case 0xC0: if (d[1] < PROGRAMS) { s->program = d[1]; memcpy(s->params, s->program_params[d[1]], sizeof s->params); } break;
    default: break;
    }
}

static void render(synth_t *s, float *out, int frames)
{
    const float vol = s->params[P_VOLUME];
    const float wave = s->params[P_WAVE];
    const float detune = s->params[P_DETUNE] * 0.02f;
    if (frames <= 0)
        return;
    memset(out, 0, sizeof(float) * (size_t)frames);
    for (int i = 0; i < VOICES; i++) {
        voice_t *v = &s->voices[i];
        if (!v->active)
            continue;
        float hz = 440.0f * powf(2.0f, ((float)v->note - 69.0f + s->bend[v->channel]) / 12.0f);
        float inc = hz / s->rate, inc2 = inc * (1.0f + detune);
        int sustained = s->sustain[v->channel];
        for (int n = 0; n < frames; n++) {
            float x;
            if (wave < 0.33f) x = sinf(v->phase * 6.2831853f);
            else if (wave < 0.66f) x = 2.0f * v->phase - 1.0f;
            else x = v->phase < 0.5f ? 1.0f : -1.0f;
            if (detune > 0.0f)
                x = 0.5f * (x + (2.0f * v->phase2 - 1.0f));
            out[n] += x * v->env * v->velocity * vol * 0.25f;
            v->phase += inc; if (v->phase >= 1.0f) v->phase -= 1.0f;
            v->phase2 += inc2; if (v->phase2 >= 1.0f) v->phase2 -= 1.0f;
            if (v->released && !sustained) {
                v->env -= v->env_step * 4.0f;
            } else {
                v->env -= v->env_step;
            }
            if (v->env <= 0.0f) { v->env = 0.0f; v->active = 0; break; }
        }
    }
}

static intptr_t dispatcher(mvst_effect *e, int32_t op, int32_t index, intptr_t value, void *ptr, float opt)
{
    synth_t *s = self(e);
    switch (op) {
    case MVST_OP_OPEN: return 0;
    case MVST_OP_CLOSE: free(s); e->object = NULL; return 0;
    case MVST_OP_SET_PROGRAM:
        if (value >= 0 && value < PROGRAMS) { s->program = (int)value; memcpy(s->params, s->program_params[value], sizeof s->params); }
        return 0;
    case MVST_OP_GET_PROGRAM: return s->program;
    case MVST_OP_SET_PROGRAM_NAME: mock_copy(s->program_names[s->program], (const char *)ptr, MVST_MAX_PROGRAM_NAME_LEN); return 0;
    case MVST_OP_GET_PROGRAM_NAME: mock_copy(ptr, s->program_names[s->program], MVST_MAX_PROGRAM_NAME_LEN); return 0;
    case MVST_OP_GET_PROGRAM_NAME_INDEXED:
        if (index < 0 || index >= PROGRAMS) return 0;
        mock_copy(ptr, s->program_names[index], MVST_MAX_PROGRAM_NAME_LEN);
        return 1;
    case MVST_OP_GET_PARAM_LABEL: mock_copy(ptr, index == P_DECAY ? "s" : "", MVST_MAX_LABEL_LEN); return 0;
    case MVST_OP_GET_PARAM_DISPLAY: {
        char b[32];
        if (index == P_WAVE) snprintf(b, sizeof b, "%s", s->params[P_WAVE] < 0.33f ? "sine" : s->params[P_WAVE] < 0.66f ? "saw" : "square");
        else if (index == P_DECAY) snprintf(b, sizeof b, "%.2f", 0.05f + 2.0f * s->params[P_DECAY]);
        else snprintf(b, sizeof b, "%.0f", s->params[index] * 100.0f);
        mock_copy(ptr, b, MVST_MAX_PARAM_STR_LEN * 4);
        return 0;
    }
    case MVST_OP_GET_PARAM_NAME: {
        static const char *const names[PARAMS] = {"Volume", "Wave", "Decay", "Detune"};
        mock_copy(ptr, index >= 0 && index < PARAMS ? names[index] : "", MVST_MAX_NAME_LEN);
        return 0;
    }
    case MVST_OP_SET_SAMPLE_RATE: s->rate = opt > 0 ? opt : 44100.0f; return 0;
    case MVST_OP_SET_BLOCK_SIZE: s->block = (int)value; return 0;
    case MVST_OP_MAINS_CHANGED:
        if (value == 0) { for (int i = 0; i < VOICES; i++) s->voices[i].active = 0; }
        return 0;
    case MVST_OP_GET_CHUNK: {
        /* "MCK1" + program + 4 params as floats. */
        memcpy(s->chunk, "MCK1", 4);
        int32_t p = s->program;
        memcpy(s->chunk + 4, &p, 4);
        memcpy(s->chunk + 8, s->params, sizeof s->params);
        *(void **)ptr = s->chunk;
        return 8 + (intptr_t)sizeof s->params;
    }
    case MVST_OP_SET_CHUNK: {
        const unsigned char *d = (const unsigned char *)ptr;
        if (value < 8 + (intptr_t)sizeof s->params || memcmp(d, "MCK1", 4) != 0) return 0;
        int32_t p; memcpy(&p, d + 4, 4);
        if (p >= 0 && p < PROGRAMS) s->program = p;
        memcpy(s->params, d + 8, sizeof s->params);
        for (int i = 0; i < PARAMS; i++) s->params[i] = mock_clamp01(s->params[i]);
        return 1;
    }
    case MVST_OP_PROCESS_EVENTS: {
        mvst_events *ev = (mvst_events *)ptr;
        s->last_num_events = ev->num_events;
        /* Kept until the next process call, applied at their delta_frames
         * (sample-accurate), as a real instrument would. */
        s->num_pending = 0;
        for (int32_t i = 0; i < ev->num_events && s->num_pending < 64; i++) {
            mvst_event *x = ev->events[i];
            if (!x || x->type != MVST_EVENT_MIDI)
                continue;
            mvst_midi_event *m = (mvst_midi_event *)x;
            s->pending[s->num_pending].delta = m->delta_frames < 0 ? 0 : m->delta_frames;
            memcpy(s->pending[s->num_pending].d, m->midi_data, 3);
            s->num_pending++;
        }
        return 1;
    }
    case MVST_OP_CAN_BE_AUTOMATED: return 1;
    case MVST_OP_GET_PLUG_CATEGORY: return MVST_CATEGORY_SYNTH;
    case MVST_OP_GET_EFFECT_NAME: mock_copy(ptr, "Mock Instrument", MVST_MAX_NAME_LEN); return 1;
    case MVST_OP_GET_VENDOR_STRING: mock_copy(ptr, "MPC Sample VST Host project", MVST_MAX_VENDOR_LEN); return 1;
    case MVST_OP_GET_PRODUCT_STRING: mock_copy(ptr, "mock_instrument", MVST_MAX_PRODUCT_LEN); return 1;
    case MVST_OP_GET_VENDOR_VERSION: return 100;
    case MVST_OP_CAN_DO: {
        const char *q = (const char *)ptr;
        if (!q) return 0;
        if (!strcmp(q, "receiveVstEvents") || !strcmp(q, "receiveVstMidiEvent") || !strcmp(q, "receiveVstTimeInfo")) return 1;
        return -1;
    }
    case MVST_OP_GET_TAIL_SIZE: return (intptr_t)(s->rate * 2);
    case MVST_OP_GET_VST_VERSION: return 2400;
    case MVST_OP_START_PROCESS: case MVST_OP_STOP_PROCESS: return 0;
    case MVST_OP_GET_NUM_MIDI_INPUT_CHANNELS: return 16;
    default: return 0;
    }
}

static void process_replacing(mvst_effect *e, float **in, float **out, int32_t frames)
{
    (void)in;
    synth_t *s = self(e);
    s->blocks_processed++;
    if (s->host) {
        mvst_time_info *t = (mvst_time_info *)s->host(e, MVST_HOST_GET_TIME, 0, MVST_TIME_TEMPO_VALID, NULL, 0.0f);
        if (t) {
            s->transport_playing = (t->flags & MVST_TIME_TRANSPORT_PLAYING) != 0;
            s->last_tempo = t->tempo;
        }
    }
    /* Render up to each event, apply it, continue: the output does not
     * depend on where the host's block boundaries fall. */
    int pos = 0;
    for (int i = 0; i < s->num_pending; i++) {
        int at = s->pending[i].delta;
        if (at > frames) at = frames;
        if (at > pos) {
            render(s, out[0] + pos, at - pos);
            pos = at;
        }
        handle_midi(s, s->pending[i].d);
    }
    render(s, out[0] + pos, frames - pos);
    s->num_pending = 0;
    memcpy(out[1], out[0], sizeof(float) * (size_t)frames);
}

static void set_parameter(mvst_effect *e, int32_t index, float v)
{
    synth_t *s = self(e);
    if (index >= 0 && index < PARAMS) s->params[index] = mock_clamp01(v);
}

static float get_parameter(mvst_effect *e, int32_t index)
{
    synth_t *s = self(e);
    return index >= 0 && index < PARAMS ? s->params[index] : 0.0f;
}

MOCK_EXPORT mvst_effect *VSTPluginMain(mvst_host_fn host)
{
    if (host && host(NULL, MVST_HOST_VERSION, 0, 0, NULL, 0.0f) < 2000)
        return NULL;
    synth_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->host = host;
    s->rate = 44100.0f;
    for (int p = 0; p < PROGRAMS; p++) {
        memcpy(s->program_params[p], DEFAULTS[p], sizeof DEFAULTS[p]);
        mock_copy(s->program_names[p], NAMES[p], MVST_MAX_PROGRAM_NAME_LEN);
    }
    memcpy(s->params, DEFAULTS[0], sizeof s->params);
    mvst_effect *e = &s->effect;
    e->magic = MVST_MAGIC;
    e->dispatcher = dispatcher;
    e->set_parameter = set_parameter;
    e->get_parameter = get_parameter;
    e->num_programs = PROGRAMS;
    e->num_params = PARAMS;
    e->num_inputs = 0;
    e->num_outputs = 2;
    e->flags = MVST_FLAG_CAN_REPLACING | MVST_FLAG_PROGRAM_CHUNKS | MVST_FLAG_IS_SYNTH;
    e->initial_delay = 0;
    e->object = s;
    e->unique_id = (int32_t)(('m' << 24) | ('k' << 16) | ('I' << 8) | '1');
    e->version = 100;
    e->process_replacing = process_replacing;
    return e;
}
