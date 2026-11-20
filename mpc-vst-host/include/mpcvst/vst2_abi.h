/* SPDX-License-Identifier: MIT
 * MPC Sample VST Host Expansion: the VST 2.x plug-in binary interface as a
 * headless host needs it.
 *
 * This header is an independent description of the long-public VST 2.4
 * calling convention and structure layout (the same interface that free
 * reimplementations such as FST and VeSTige describe). It contains no file
 * or text from the Steinberg VST2 SDK, which is withdrawn and may not be
 * redistributed. All identifiers here are this project's own.
 *
 * It is plain C so that the mock plug-ins in plugins/ can use it too.
 */
#ifndef MPCVST_VST2_ABI_H
#define MPCVST_VST2_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mvst_effect;
typedef struct mvst_effect mvst_effect;

/* Host callback: plug-in -> host. */
typedef intptr_t (*mvst_host_fn)(mvst_effect *e, int32_t opcode, int32_t index, intptr_t value,
                                 void *ptr, float opt);
/* Dispatcher: host -> plug-in. */
typedef intptr_t (*mvst_dispatch_fn)(mvst_effect *e, int32_t opcode, int32_t index, intptr_t value,
                                     void *ptr, float opt);
typedef void (*mvst_process_fn)(mvst_effect *e, float **in, float **out, int32_t frames);
typedef void (*mvst_process_double_fn)(mvst_effect *e, double **in, double **out, int32_t frames);
typedef void (*mvst_set_param_fn)(mvst_effect *e, int32_t index, float value);
typedef float (*mvst_get_param_fn)(mvst_effect *e, int32_t index);
/* The entry point a plug-in library exports as "VSTPluginMain" (or the
 * legacy "main"). */
typedef mvst_effect *(*mvst_entry_fn)(mvst_host_fn host);

#define MVST_MAGIC ((int32_t)(((int32_t)'V' << 24) | ((int32_t)'s' << 16) | ((int32_t)'t' << 8) | (int32_t)'P'))
#define MVST_HOST_API_VERSION 2400 /* what MVST_HOST_VERSION (the opcode) answers */
#define MVST_MAX_NAME_LEN 64
#define MVST_MAX_LABEL_LEN 64
#define MVST_MAX_VENDOR_LEN 64
#define MVST_MAX_PRODUCT_LEN 64
#define MVST_MAX_PROGRAM_NAME_LEN 24
#define MVST_MAX_PARAM_STR_LEN 8 /* the minimum a plug-in may assume; hosts pass 256 */

struct mvst_effect {
    int32_t magic;                   /* MVST_MAGIC */
    mvst_dispatch_fn dispatcher;
    mvst_process_fn process_accumulating; /* deprecated in 2.4; may be NULL */
    mvst_set_param_fn set_parameter;
    mvst_get_param_fn get_parameter;
    int32_t num_programs;
    int32_t num_params;
    int32_t num_inputs;
    int32_t num_outputs;
    int32_t flags;                   /* MVST_FLAG_* */
    intptr_t resvd1;                 /* reserved for the host */
    intptr_t resvd2;                 /* reserved for the host */
    int32_t initial_delay;           /* latency in frames */
    int32_t real_qualities;          /* unused */
    int32_t off_qualities;           /* unused */
    float io_ratio;                  /* unused */
    void *object;                    /* the plug-in's own object */
    void *user;                      /* user data */
    int32_t unique_id;
    int32_t version;
    mvst_process_fn process_replacing;
    mvst_process_double_fn process_double_replacing;
    char future[56];
};

/* mvst_effect::flags */
enum {
    MVST_FLAG_HAS_EDITOR = 1 << 0,
    MVST_FLAG_CAN_REPLACING = 1 << 4,
    MVST_FLAG_PROGRAM_CHUNKS = 1 << 5,
    MVST_FLAG_IS_SYNTH = 1 << 8,
    MVST_FLAG_NO_SOUND_IN_STOP = 1 << 9,
    MVST_FLAG_CAN_DOUBLE_REPLACING = 1 << 12
};

/* Host -> plug-in opcodes (mvst_effect::dispatcher). */
enum {
    MVST_OP_OPEN = 0,
    MVST_OP_CLOSE = 1,
    MVST_OP_SET_PROGRAM = 2,
    MVST_OP_GET_PROGRAM = 3,
    MVST_OP_SET_PROGRAM_NAME = 4,
    MVST_OP_GET_PROGRAM_NAME = 5,
    MVST_OP_GET_PARAM_LABEL = 6,
    MVST_OP_GET_PARAM_DISPLAY = 7,
    MVST_OP_GET_PARAM_NAME = 8,
    MVST_OP_SET_SAMPLE_RATE = 10,
    MVST_OP_SET_BLOCK_SIZE = 11,
    MVST_OP_MAINS_CHANGED = 12,
    MVST_OP_EDIT_GET_RECT = 13,
    MVST_OP_EDIT_OPEN = 14,
    MVST_OP_EDIT_CLOSE = 15,
    MVST_OP_EDIT_IDLE = 19,
    MVST_OP_GET_CHUNK = 23,
    MVST_OP_SET_CHUNK = 24,
    MVST_OP_PROCESS_EVENTS = 25,
    MVST_OP_CAN_BE_AUTOMATED = 26,
    MVST_OP_STRING_TO_PARAMETER = 27,
    MVST_OP_GET_PROGRAM_NAME_INDEXED = 29,
    MVST_OP_GET_INPUT_PROPERTIES = 33,
    MVST_OP_GET_OUTPUT_PROPERTIES = 34,
    MVST_OP_GET_PLUG_CATEGORY = 35,
    MVST_OP_SET_SPEAKER_ARRANGEMENT = 42,
    MVST_OP_GET_EFFECT_NAME = 45,
    MVST_OP_GET_VENDOR_STRING = 47,
    MVST_OP_GET_PRODUCT_STRING = 48,
    MVST_OP_GET_VENDOR_VERSION = 49,
    MVST_OP_VENDOR_SPECIFIC = 50,
    MVST_OP_CAN_DO = 51,
    MVST_OP_GET_TAIL_SIZE = 52,
    MVST_OP_GET_PARAMETER_PROPERTIES = 56,
    MVST_OP_GET_VST_VERSION = 58,
    MVST_OP_BEGIN_SET_PROGRAM = 67,
    MVST_OP_END_SET_PROGRAM = 68,
    MVST_OP_START_PROCESS = 71,
    MVST_OP_STOP_PROCESS = 72,
    MVST_OP_SET_PROCESS_PRECISION = 77,
    MVST_OP_GET_NUM_MIDI_INPUT_CHANNELS = 78,
    MVST_OP_GET_NUM_MIDI_OUTPUT_CHANNELS = 79
};

/* Plug-in -> host opcodes (mvst_host_fn). */
enum {
    MVST_HOST_AUTOMATE = 0,
    MVST_HOST_VERSION = 1,
    MVST_HOST_CURRENT_ID = 2,
    MVST_HOST_IDLE = 3,
    MVST_HOST_PIN_CONNECTED = 4, /* deprecated */
    MVST_HOST_WANT_MIDI = 6,     /* deprecated */
    MVST_HOST_GET_TIME = 7,
    MVST_HOST_PROCESS_EVENTS = 8,
    MVST_HOST_TEMPO_AT = 10,     /* deprecated */
    MVST_HOST_IO_CHANGED = 13,
    MVST_HOST_SIZE_WINDOW = 15,
    MVST_HOST_GET_SAMPLE_RATE = 16,
    MVST_HOST_GET_BLOCK_SIZE = 17,
    MVST_HOST_GET_INPUT_LATENCY = 18,
    MVST_HOST_GET_OUTPUT_LATENCY = 19,
    MVST_HOST_GET_CURRENT_PROCESS_LEVEL = 23,
    MVST_HOST_GET_AUTOMATION_STATE = 24,
    MVST_HOST_GET_VENDOR_STRING = 32,
    MVST_HOST_GET_PRODUCT_STRING = 33,
    MVST_HOST_GET_VENDOR_VERSION = 34,
    MVST_HOST_VENDOR_SPECIFIC = 35,
    MVST_HOST_CAN_DO = 37,
    MVST_HOST_GET_LANGUAGE = 38,
    MVST_HOST_GET_DIRECTORY = 41,
    MVST_HOST_UPDATE_DISPLAY = 42,
    MVST_HOST_BEGIN_EDIT = 43,
    MVST_HOST_END_EDIT = 44
};

/* MVST_OP_GET_PLUG_CATEGORY results. */
enum {
    MVST_CATEGORY_UNKNOWN = 0,
    MVST_CATEGORY_EFFECT = 1,
    MVST_CATEGORY_SYNTH = 2,
    MVST_CATEGORY_ANALYSIS = 3,
    MVST_CATEGORY_MASTERING = 4,
    MVST_CATEGORY_SPACIALIZER = 5,
    MVST_CATEGORY_ROOM_FX = 6,
    MVST_CATEGORY_SURROUND_FX = 7,
    MVST_CATEGORY_RESTORATION = 8,
    MVST_CATEGORY_OFFLINE_PROCESS = 9,
    MVST_CATEGORY_SHELL = 10,
    MVST_CATEGORY_GENERATOR = 11
};

/* MVST_HOST_GET_CURRENT_PROCESS_LEVEL results. */
enum {
    MVST_PROCESS_LEVEL_UNKNOWN = 0,
    MVST_PROCESS_LEVEL_USER = 1,
    MVST_PROCESS_LEVEL_REALTIME = 2,
    MVST_PROCESS_LEVEL_PREFETCH = 3,
    MVST_PROCESS_LEVEL_OFFLINE = 4
};

/* Events (MVST_OP_PROCESS_EVENTS). */
enum { MVST_EVENT_MIDI = 1, MVST_EVENT_SYSEX = 6 };

typedef struct mvst_event {
    int32_t type;
    int32_t byte_size;
    int32_t delta_frames;
    int32_t flags;
    char data[16];
} mvst_event;

typedef struct mvst_midi_event {
    int32_t type;        /* MVST_EVENT_MIDI */
    int32_t byte_size;   /* sizeof(mvst_midi_event) */
    int32_t delta_frames;/* offset into the block */
    int32_t flags;       /* MVST_MIDI_EVENT_IS_REALTIME or 0 */
    int32_t note_length; /* 0: unknown */
    int32_t note_offset; /* 0 */
    unsigned char midi_data[4]; /* status, data1, data2, 0 */
    char detune;
    char note_off_velocity;
    char reserved1;
    char reserved2;
} mvst_midi_event;

enum { MVST_MIDI_EVENT_IS_REALTIME = 1 };

typedef struct mvst_sysex_event {
    int32_t type;      /* MVST_EVENT_SYSEX */
    int32_t byte_size;
    int32_t delta_frames;
    int32_t flags;
    int32_t dump_bytes;
    intptr_t resvd1;
    char *sysex_dump;
    intptr_t resvd2;
} mvst_sysex_event;

/* A list of events: the count, a reserved word, then `num_events` pointers.
 * Hosts allocate it with room for as many pointers as they send. */
typedef struct mvst_events {
    int32_t num_events;
    intptr_t reserved;
    mvst_event *events[2];
} mvst_events;

/* MVST_HOST_GET_TIME result. */
typedef struct mvst_time_info {
    double sample_pos;
    double sample_rate;
    double nano_seconds;
    double ppq_pos;
    double tempo;
    double bar_start_pos;
    double cycle_start_pos;
    double cycle_end_pos;
    int32_t time_sig_numerator;
    int32_t time_sig_denominator;
    int32_t smpte_offset;
    int32_t smpte_frame_rate;
    int32_t samples_to_next_clock;
    int32_t flags; /* MVST_TIME_* */
} mvst_time_info;

enum {
    MVST_TIME_TRANSPORT_CHANGED = 1 << 0,
    MVST_TIME_TRANSPORT_PLAYING = 1 << 1,
    MVST_TIME_TRANSPORT_CYCLE_ACTIVE = 1 << 2,
    MVST_TIME_TRANSPORT_RECORDING = 1 << 3,
    MVST_TIME_AUTOMATION_WRITING = 1 << 6,
    MVST_TIME_AUTOMATION_READING = 1 << 7,
    MVST_TIME_NANOS_VALID = 1 << 8,
    MVST_TIME_PPQ_POS_VALID = 1 << 9,
    MVST_TIME_TEMPO_VALID = 1 << 10,
    MVST_TIME_BARS_VALID = 1 << 11,
    MVST_TIME_CYCLE_POS_VALID = 1 << 12,
    MVST_TIME_SIG_VALID = 1 << 13,
    MVST_TIME_SMPTE_VALID = 1 << 14,
    MVST_TIME_CLOCK_VALID = 1 << 15
};

/* MVST_OP_EDIT_GET_RECT result (unused by a headless host; here so the
 * layout is complete). */
typedef struct mvst_rect {
    int16_t top, left, bottom, right;
} mvst_rect;

/* MVST_OP_GET_INPUT_PROPERTIES / MVST_OP_GET_OUTPUT_PROPERTIES result. */
typedef struct mvst_pin_properties {
    char label[64];
    int32_t flags;
    int32_t arrangement_type;
    char short_label[8];
    char future[48];
} mvst_pin_properties;

enum { MVST_PIN_IS_ACTIVE = 1 << 0, MVST_PIN_IS_STEREO = 1 << 1, MVST_PIN_USE_SPEAKER = 1 << 2 };

#ifdef __cplusplus
} /* extern "C" */

#include <type_traits>
namespace mpcvst {
/* On every LP64 target this layout is 192 bytes; a different size means
 * the compiler laid the struct out in a way plug-ins will not expect. */
static_assert(sizeof(void *) == 8 ? sizeof(mvst_effect) == 192 : true, "mvst_effect layout");
static_assert(sizeof(mvst_midi_event) == 32, "mvst_midi_event layout");
static_assert(sizeof(void *) == 8 ? sizeof(mvst_time_info) == 88 : true, "mvst_time_info layout");
} // namespace mpcvst
#endif

#endif /* MPCVST_VST2_ABI_H */
