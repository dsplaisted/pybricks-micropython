// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2023 The Pybricks Authors

// Speaker class for playing sounds.

// TODO: functions implementations need to be moved to lib/pbio/src/sound/sound.c
// so that they can be used by pbsys as well.

// TODO: share code with ev3dev Speaker type

#include "py/mpconfig.h"

#if PYBRICKS_PY_COMMON_SPEAKER && MICROPY_PY_BUILTINS_FLOAT

#include <math.h>
#include <pbdrv/sound.h>
#include <pbio/flash_fs.h>
#include <pbio/sound_mp3.h>
#include <pbio/sound_volume.h>
#include <pbsys/storage.h>

#include "py/mphal.h"
#include "py/obj.h"

#include <pybricks/common.h>
#include <pybricks/tools.h>
#include <pybricks/tools/pb_type_async.h>
#include <pybricks/util_mp/pb_kwarg_helper.h>
#include <pybricks/util_mp/pb_obj_helper.h>
#include <pybricks/util_pb/pb_error.h>

typedef struct {
    mp_obj_base_t base;

    // State of awaitable sound
    pb_type_async_t *iter;
    pbio_os_timer_t timer;
    mp_obj_t notes_generator;
    uint32_t note_duration;
    uint32_t scaled_duration;

    // volume in 0..100 range
    uint8_t volume;

    // The number to multiply the sample amplitude by, to attenuate the amplitude based on the defined speaker volume.
    // The original sample amplitude must be in the -1..1 range.
    uint16_t sample_attenuator;
} pb_type_Speaker_obj_t;

static mp_obj_t pb_type_Speaker_volume(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        pb_type_Speaker_obj_t, self,
        PB_ARG_DEFAULT_NONE(volume));

    if (volume_in == mp_const_none) {
        return MP_OBJ_NEW_SMALL_INT(self->volume);
    }

    self->volume = pb_obj_get_pct(volume_in);

    // exponential amplification (human ear perceives sample amplitude in a logarithmic way)
    self->sample_attenuator = (powf(10, self->volume / 100.0F) - 1) / 9 * INT16_MAX;

    // Shared with other sounds and kept when this program ends. Sound that
    // is already playing picks up the new volume right away.
    pbio_sound_volume_set(self->volume, self->sample_attenuator);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pb_type_Speaker_volume_obj, 1, pb_type_Speaker_volume);

static mp_obj_t pb_type_Speaker_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {

    pb_type_Speaker_obj_t *self = mp_obj_malloc(pb_type_Speaker_obj_t, type);

    // The volume is kept outside of the application, so it continues where
    // the previous program left off instead of resetting to the default.
    self->volume = pbio_sound_volume_get_percent();
    self->sample_attenuator = pbio_sound_volume_get_sample_attenuator();

    self->iter = NULL;

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t pb_type_Speaker_close(mp_obj_t self_in) {
    pbdrv_sound_stop();
    return mp_const_none;
}

static pbio_error_t pb_type_Speaker_beep_iterate_once(pbio_os_state_t *state, mp_obj_t parent_obj) {
    pb_type_Speaker_obj_t *self = MP_OBJ_TO_PTR(parent_obj);
    // The beep has already been started. We just need to await the duration
    // and then stop.
    PBIO_OS_ASYNC_BEGIN(state);
    PBIO_OS_AWAIT_UNTIL(state, pbio_os_timer_is_expired(&self->timer));
    pbdrv_sound_stop();
    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

static mp_obj_t pb_type_Speaker_beep(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        pb_type_Speaker_obj_t, self,
        PB_ARG_DEFAULT_INT(frequency, 500),
        PB_ARG_DEFAULT_INT(duration, 100));

    mp_int_t frequency = pb_obj_get_int(frequency_in);
    mp_int_t duration = pb_obj_get_int(duration_in);

    pbdrv_beep_start(frequency, self->sample_attenuator);

    if (duration < 0) {
        return mp_const_none;
    }

    pbio_os_timer_set(&self->timer, pb_obj_get_int(duration_in));

    pb_type_async_t config = {
        .parent_obj = MP_OBJ_FROM_PTR(self),
        .iter_once = pb_type_Speaker_beep_iterate_once,
        .close = pb_type_Speaker_close,
    };
    // New operation always wins; ongoing sound awaitable is cancelled.
    return pb_type_async_wait_or_await(&config, &self->iter, true);

}
static MP_DEFINE_CONST_FUN_OBJ_KW(pb_type_Speaker_beep_obj, 1, pb_type_Speaker_beep);

static void pb_type_Speaker_get_note(mp_obj_t obj, uint32_t note_ms, uint32_t *frequency, uint32_t *total_ms, uint32_t *on_ms) {
    const char *note = mp_obj_str_get_str(obj);
    int pos = 0;
    mp_float_t freq;
    bool release = true;

    // Note names can be A-G followed by optional # (sharp) or b (flat) or R for rest
    switch (note[pos++]) {
        case 'C':
            switch (note[pos++]) {
                case 'b':
                    mp_raise_ValueError(MP_ERROR_TEXT("Cb is not allowed"));
                    break;
                case '#':
                    freq = MICROPY_FLOAT_CONST(17.32);
                    break;
                default:
                    pos--;
                    freq = MICROPY_FLOAT_CONST(16.35);
                    break;
            }
            break;
        case 'D':
            switch (note[pos++]) {
                case 'b':
                    freq = MICROPY_FLOAT_CONST(17.32);
                    break;
                case '#':
                    freq = MICROPY_FLOAT_CONST(19.45);
                    break;
                default:
                    pos--;
                    freq = MICROPY_FLOAT_CONST(18.35);
                    break;
            }
            break;
        case 'E':
            switch (note[pos++]) {
                case 'b':
                    freq = MICROPY_FLOAT_CONST(19.45);
                    break;
                case '#':
                    mp_raise_ValueError(MP_ERROR_TEXT("E# is not allowed"));
                    break;
                default:
                    pos--;
                    freq = MICROPY_FLOAT_CONST(20.60);
                    break;
            }
            break;
        case 'F':
            switch (note[pos++]) {
                case 'b':
                    mp_raise_ValueError(MP_ERROR_TEXT("Fb is not allowed"));
                    break;
                case '#':
                    freq = MICROPY_FLOAT_CONST(23.12);
                    break;
                default:
                    pos--;
                    freq = MICROPY_FLOAT_CONST(21.83);
                    break;
            }
            break;
        case 'G':
            switch (note[pos++]) {
                case 'b':
                    freq = MICROPY_FLOAT_CONST(23.12);
                    break;
                case '#':
                    freq = MICROPY_FLOAT_CONST(25.96);
                    break;
                default:
                    pos--;
                    freq = MICROPY_FLOAT_CONST(24.50);
                    break;
            }
            break;
        case 'A':
            switch (note[pos++]) {
                case 'b':
                    freq = MICROPY_FLOAT_CONST(25.96);
                    break;
                case '#':
                    freq = MICROPY_FLOAT_CONST(29.14);
                    break;
                default:
                    pos--;
                    freq = MICROPY_FLOAT_CONST(27.50);
                    break;
            }
            break;
        case 'B':
            switch (note[pos++]) {
                case 'b':
                    freq = MICROPY_FLOAT_CONST(29.14);
                    break;
                case '#':
                    mp_raise_ValueError(MP_ERROR_TEXT("B# is not allowed"));
                    break;
                default:
                    pos--;
                    freq = MICROPY_FLOAT_CONST(30.87);
                    break;
            }
            break;
        case 'R':
            freq = MICROPY_FLOAT_CONST(0.0);
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("Missing note name A-G or R"));
            break;
    }

    // Note name must be followed by the octave number
    if (freq != 0) {
        int octave = note[pos++] - '0';
        if (octave < 2 || octave > 8) {
            mp_raise_ValueError(MP_ERROR_TEXT("Missing octave number 2-8"));
        }
        freq *= 2 << (octave - 1);
    }

    // '/' delimiter is required between octave and fraction
    if (note[pos++] != '/') {
        mp_raise_ValueError(MP_ERROR_TEXT("Missing /"));
    }

    // The fractional size of the note, e.g. 4 = quarter note
    int fraction = note[pos++] - '0';
    if (fraction < 0 || fraction > 9) {
        mp_raise_ValueError(MP_ERROR_TEXT("Missing fractional value 1, 2, 4, 8, etc."));
    }

    // optional second digit
    int fraction2 = note[pos++] - '0';
    if (fraction2 < 0 || fraction2 > 9) {
        pos--;
    } else {
        fraction = fraction * 10 + fraction2;
    }

    *total_ms = note_ms / fraction;

    // optional decorations

    if (note[pos++] == '.') {
        // dotted note has length extended by 1/2
        *total_ms = 3 * *total_ms / 2;
    } else {
        pos--;
    }

    if (note[pos++] == '_') {
        // note with tie/slur is not released
        release = false;
    } else {
        pos--;
    }

    *frequency = (uint32_t)freq;
    *on_ms = release ? 7 * (*total_ms) / 8 : *total_ms;
}

static pbio_error_t pb_type_Speaker_play_notes_iterate_once(pbio_os_state_t *state, mp_obj_t parent_obj) {
    pb_type_Speaker_obj_t *self = MP_OBJ_TO_PTR(parent_obj);
    mp_obj_t item;

    PBIO_OS_ASYNC_BEGIN(state);

    while ((item = mp_iternext(self->notes_generator)) != MP_OBJ_STOP_ITERATION) {

        // Parse next note.
        uint32_t frequency;
        uint32_t beep_time;
        pb_type_Speaker_get_note(item, self->note_duration, &frequency, &self->scaled_duration, &beep_time);

        // On portion of the note.
        pbdrv_beep_start(frequency, self->sample_attenuator);
        pbio_os_timer_set(&self->timer, beep_time);
        PBIO_OS_AWAIT_UNTIL(state, pbio_os_timer_is_expired(&self->timer));

        // Off portion of the note.
        pbdrv_sound_stop();
        self->timer.duration = self->scaled_duration;
        PBIO_OS_AWAIT_UNTIL(state, pbio_os_timer_is_expired(&self->timer));
    }

    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

static mp_obj_t pb_type_Speaker_play_notes(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        pb_type_Speaker_obj_t, self,
        PB_ARG_REQUIRED(notes),
        PB_ARG_DEFAULT_INT(tempo, 120));

    self->notes_generator = mp_getiter(notes_in, NULL);
    self->note_duration = 4 * 60 * 1000 / pb_obj_get_int(tempo_in);

    pb_type_async_t config = {
        .parent_obj = MP_OBJ_FROM_PTR(self),
        .iter_once = pb_type_Speaker_play_notes_iterate_once,
        .close = pb_type_Speaker_close,
    };
    // New operation always wins; ongoing sound awaitable is cancelled.
    return pb_type_async_wait_or_await(&config, &self->iter, true);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pb_type_Speaker_play_notes_obj, 1, pb_type_Speaker_play_notes);

#if PBIO_CONFIG_SOUND_MP3

// Experimental: plays an MP3 file embedded in the firmware at build time.

// Generated from the files given by PB_MP3 when building.
extern const uint8_t *const pb_mp3_song_data[];
extern const uint32_t pb_mp3_song_size[];
extern const uint32_t pb_mp3_song_count;

// Decoder memory, kept here so the garbage collector won't free it while
// playing. Playback is always stopped when the program ends.
MP_REGISTER_ROOT_POINTER(void *pb_mp3_context);

// Gets the memory for the decoder to work in.
//
// Where it comes from depends on whether playback is meant to outlive the
// program, so this must be called after stopping and just before starting.
static void *pb_type_Speaker_mp3_memory(void) {

    if (pbio_sound_mp3_get_persist()) {
        // Reserved memory that is not reused when this program ends, so that
        // the sound can keep playing during and after the next programs.
        MP_STATE_VM(pb_mp3_context) = NULL;
        return pbsys_storage_get_sound_memory();
    }

    MP_STATE_VM(pb_mp3_context) = m_malloc(pbio_sound_mp3_get_context_size());
    return MP_STATE_VM(pb_mp3_context);
}

// Starts the chosen song from the beginning.
static void pb_type_Speaker_start_mp3(void) {

    if (!pbio_sound_mp3_is_selected()) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("no song selected"));
    }

    // Stop first, so the old memory is no longer used when replaced.
    pbio_sound_mp3_stop();

    pb_assert(pbio_sound_mp3_play(pb_type_Speaker_mp3_memory()));
}

// Experimental: chooses the song that the play methods will play.
//
// A whole number chooses a song built into the firmware. A string chooses a
// file on the external flash.
static mp_obj_t pb_type_Speaker_select_mp3(mp_obj_t self_in, mp_obj_t song_in) {

    if (mp_obj_is_str(song_in)) {
        pb_assert(pbio_sound_mp3_select_file(mp_obj_str_get_str(song_in)));
        return mp_const_none;
    }

    mp_int_t index = pb_obj_get_int(song_in);
    if (index < 0 || (uint32_t)index >= pb_mp3_song_count) {
        mp_raise_ValueError(MP_ERROR_TEXT("no such song"));
    }

    pbio_sound_mp3_select(pb_mp3_song_data[index], pb_mp3_song_size[index], index);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(pb_type_Speaker_select_mp3_obj, pb_type_Speaker_select_mp3);

// Experimental: gets the song that was chosen, or None if there is none.
static mp_obj_t pb_type_Speaker_mp3_selected(mp_obj_t self_in) {

    if (!pbio_sound_mp3_is_selected()) {
        return mp_const_none;
    }

    const char *path = pbio_sound_mp3_get_selected_path();
    if (path) {
        return mp_obj_new_str(path, strlen(path));
    }

    return mp_obj_new_int_from_uint(pbio_sound_mp3_get_selected_index());
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_mp3_selected_obj, pb_type_Speaker_mp3_selected);

#if PBIO_CONFIG_FLASH_FS
// Collects the songs on the external flash while listing a directory.
typedef struct {
    mp_obj_t list;
    const char *dir;
} pb_type_Speaker_mp3_songs_t;

static void pb_type_Speaker_mp3_songs_callback(const char *name, uint32_t size, bool is_dir, void *context) {

    pb_type_Speaker_mp3_songs_t *songs = context;

    uint32_t length = strlen(name);
    if (is_dir || length < 4 || strcmp(&name[length - 4], ".mp3")) {
        return;
    }

    // The path is what select_mp3() takes, so give it back ready to use.
    vstr_t vstr;
    vstr_init(&vstr, strlen(songs->dir) + length + 1);
    vstr_add_str(&vstr, songs->dir);
    if (vstr.len == 0 || vstr_str(&vstr)[vstr.len - 1] != '/') {
        vstr_add_char(&vstr, '/');
    }
    vstr_add_str(&vstr, name);

    mp_obj_list_append(songs->list, mp_obj_new_str_from_vstr(&vstr));
}
#endif // PBIO_CONFIG_FLASH_FS

// Experimental: lists the songs that can be chosen with select_mp3().
//
// The songs built into the firmware come first, as whole numbers, followed by
// the paths of the MP3 files on the external flash.
static mp_obj_t pb_type_Speaker_mp3_songs(size_t n_args, const mp_obj_t *args) {

    mp_obj_t list = mp_obj_new_list(0, NULL);

    for (uint32_t i = 0; i < pb_mp3_song_count; i++) {
        mp_obj_list_append(list, mp_obj_new_int_from_uint(i));
    }

    #if PBIO_CONFIG_FLASH_FS
    const char *dir = n_args > 1 ? mp_obj_str_get_str(args[1]) : PBIO_SOUND_MP3_DIR;

    pb_assert(pbio_flash_fs_mount());

    pb_type_Speaker_mp3_songs_t songs = { .list = list, .dir = dir };
    pbio_error_t err = pbio_flash_fs_list(dir, pb_type_Speaker_mp3_songs_callback, &songs);
    pbio_flash_fs_unmount();

    // A hub that has never had a song put on it has no folder to list, which
    // means no songs rather than something being wrong.
    if (err != PBIO_ERROR_NO_DEV) {
        pb_assert(err);
    }
    #endif // PBIO_CONFIG_FLASH_FS

    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pb_type_Speaker_mp3_songs_obj, 1, 2, pb_type_Speaker_mp3_songs);

// Experimental: gets or sets whether the song starts again when it ends.
static mp_obj_t pb_type_Speaker_mp3_repeat(size_t n_args, const mp_obj_t *args) {

    if (n_args > 1) {
        pbio_sound_mp3_set_repeat(mp_obj_is_true(args[1]));
        return mp_const_none;
    }

    return mp_obj_new_bool(pbio_sound_mp3_get_repeat());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pb_type_Speaker_mp3_repeat_obj, 1, 2, pb_type_Speaker_mp3_repeat);

// Experimental: gets or sets whether playback continues after the program
// that started it ends. Takes effect the next time a song starts.
static mp_obj_t pb_type_Speaker_mp3_persist(size_t n_args, const mp_obj_t *args) {

    if (n_args > 1) {
        pbio_sound_mp3_set_persist(mp_obj_is_true(args[1]));
        return mp_const_none;
    }

    return mp_obj_new_bool(pbio_sound_mp3_get_persist());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pb_type_Speaker_mp3_persist_obj, 1, 2, pb_type_Speaker_mp3_persist);

// Experimental: plays the chosen song from the beginning.
static mp_obj_t pb_type_Speaker_play_mp3(mp_obj_t self_in) {
    pb_type_Speaker_start_mp3();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_play_mp3_obj, pb_type_Speaker_play_mp3);

static mp_obj_t pb_type_Speaker_pause_mp3(mp_obj_t self_in) {
    pbio_sound_mp3_pause();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_pause_mp3_obj, pb_type_Speaker_pause_mp3);

static mp_obj_t pb_type_Speaker_resume_mp3(mp_obj_t self_in) {
    pbio_sound_mp3_resume();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_resume_mp3_obj, pb_type_Speaker_resume_mp3);

// Experimental: copies a song embedded in the firmware to the external
// flash, so that playing from a file can be tried before there is a way to
// upload songs to the hub.
static mp_obj_t pb_type_Speaker_mp3_playing(mp_obj_t self_in) {
    return mp_obj_new_bool(pbio_sound_mp3_is_playing());
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_mp3_playing_obj, pb_type_Speaker_mp3_playing);

static mp_obj_t pb_type_Speaker_mp3_paused(mp_obj_t self_in) {
    return mp_obj_new_bool(pbio_sound_mp3_is_paused());
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_mp3_paused_obj, pb_type_Speaker_mp3_paused);

static mp_obj_t pb_type_Speaker_mp3_stopped(mp_obj_t self_in) {
    return mp_obj_new_bool(!pbio_sound_mp3_is_playing() && !pbio_sound_mp3_is_paused());
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_mp3_stopped_obj, pb_type_Speaker_mp3_stopped);

static mp_obj_t pb_type_Speaker_play_pause_mp3(mp_obj_t self_in) {

    // Acts like a play/pause button: pauses while playing, continues when
    // paused, and starts from the beginning when stopped.
    if (pbio_sound_mp3_is_playing()) {
        pbio_sound_mp3_pause();
    } else if (pbio_sound_mp3_is_paused()) {
        pbio_sound_mp3_resume();
    } else {
        pb_type_Speaker_start_mp3();
    }

    // Tells the caller which way it went, for showing it on the display.
    return mp_obj_new_bool(pbio_sound_mp3_is_playing());
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_play_pause_mp3_obj, pb_type_Speaker_play_pause_mp3);

static mp_obj_t pb_type_Speaker_stop_mp3(mp_obj_t self_in) {
    pbio_sound_mp3_stop();
    MP_STATE_VM(pb_mp3_context) = NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_Speaker_stop_mp3_obj, pb_type_Speaker_stop_mp3);

static void pb_type_Speaker_stats_store(mp_obj_t dict, qstr key, uint32_t value) {
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(key), mp_obj_new_int_from_uint(value));
}

static mp_obj_t pb_type_Speaker_mp3_stats(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_METHOD(n_args, pos_args, kw_args,
        pb_type_Speaker_obj_t, self,
        PB_ARG_DEFAULT_FALSE(reset));
    (void)self;

    pbio_sound_mp3_stats_t stats;
    pbio_sound_mp3_get_stats(&stats, mp_obj_is_true(reset_in));

    mp_obj_t dict = mp_obj_new_dict(16);
    pb_type_Speaker_stats_store(dict, MP_QSTR_sample_rate, stats.sample_rate);
    pb_type_Speaker_stats_store(dict, MP_QSTR_channels, stats.channels);
    pb_type_Speaker_stats_store(dict, MP_QSTR_bitrate_kbps, stats.bitrate_kbps);
    pb_type_Speaker_stats_store(dict, MP_QSTR_frames, stats.frames);
    pb_type_Speaker_stats_store(dict, MP_QSTR_decode_us_min, stats.decode_us_min);
    pb_type_Speaker_stats_store(dict, MP_QSTR_decode_us_max, stats.decode_us_max);
    pb_type_Speaker_stats_store(dict, MP_QSTR_decode_us_avg, stats.frames ? stats.decode_us_total / stats.frames : 0);
    pb_type_Speaker_stats_store(dict, MP_QSTR_elapsed_ms, stats.elapsed_us / 1000);
    pb_type_Speaker_stats_store(dict, MP_QSTR_half_period_us, stats.half_period_us);
    pb_type_Speaker_stats_store(dict, MP_QSTR_fill_latency_us_max, stats.fill_latency_us_max);
    pb_type_Speaker_stats_store(dict, MP_QSTR_underruns, stats.underruns);
    pb_type_Speaker_stats_store(dict, MP_QSTR_step_us_max, stats.step_us_max);
    pb_type_Speaker_stats_store(dict, MP_QSTR_halves_played, stats.halves_played);
    pb_type_Speaker_stats_store(dict, MP_QSTR_memory_bytes, stats.context_size);

    // Share of processing time spent decoding.
    mp_float_t cpu_pct = stats.elapsed_us ?
        (mp_float_t)stats.decode_us_total * 100 / (mp_float_t)stats.elapsed_us : 0;
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_cpu_pct), mp_obj_new_float(cpu_pct));

    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pb_type_Speaker_mp3_stats_obj, 1, pb_type_Speaker_mp3_stats);

#endif // PBIO_CONFIG_SOUND_MP3

static const mp_rom_map_elem_t pb_type_Speaker_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_volume), MP_ROM_PTR(&pb_type_Speaker_volume_obj) },
    { MP_ROM_QSTR(MP_QSTR_beep), MP_ROM_PTR(&pb_type_Speaker_beep_obj) },
    { MP_ROM_QSTR(MP_QSTR_play_notes), MP_ROM_PTR(&pb_type_Speaker_play_notes_obj) },
    #if PBIO_CONFIG_SOUND_MP3
    { MP_ROM_QSTR(MP_QSTR_select_mp3), MP_ROM_PTR(&pb_type_Speaker_select_mp3_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_selected), MP_ROM_PTR(&pb_type_Speaker_mp3_selected_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_songs), MP_ROM_PTR(&pb_type_Speaker_mp3_songs_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_repeat), MP_ROM_PTR(&pb_type_Speaker_mp3_repeat_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_persist), MP_ROM_PTR(&pb_type_Speaker_mp3_persist_obj) },
    { MP_ROM_QSTR(MP_QSTR_play_mp3), MP_ROM_PTR(&pb_type_Speaker_play_mp3_obj) },
    { MP_ROM_QSTR(MP_QSTR_play_pause_mp3), MP_ROM_PTR(&pb_type_Speaker_play_pause_mp3_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_playing), MP_ROM_PTR(&pb_type_Speaker_mp3_playing_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_paused), MP_ROM_PTR(&pb_type_Speaker_mp3_paused_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_stopped), MP_ROM_PTR(&pb_type_Speaker_mp3_stopped_obj) },
    { MP_ROM_QSTR(MP_QSTR_pause_mp3), MP_ROM_PTR(&pb_type_Speaker_pause_mp3_obj) },
    { MP_ROM_QSTR(MP_QSTR_resume_mp3), MP_ROM_PTR(&pb_type_Speaker_resume_mp3_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop_mp3), MP_ROM_PTR(&pb_type_Speaker_stop_mp3_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_stats), MP_ROM_PTR(&pb_type_Speaker_mp3_stats_obj) },
    #endif
};
static MP_DEFINE_CONST_DICT(pb_type_Speaker_locals_dict, pb_type_Speaker_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(pb_type_Speaker,
    MP_QSTR_Speaker,
    MP_TYPE_FLAG_NONE,
    make_new, pb_type_Speaker_make_new,
    locals_dict, &pb_type_Speaker_locals_dict);

#endif // PYBRICKS_PY_COMMON_SPEAKER && MICROPY_PY_BUILTINS_FLOAT
