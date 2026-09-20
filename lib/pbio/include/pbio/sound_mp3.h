// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

/**
 * @addtogroup SoundMp3 pbio/sound_mp3: Experimental MP3 player
 *
 * Decodes an MP3 stream in a background process and streams it to the
 * speaker. Collects timing statistics to evaluate the CPU cost.
 * @{
 */

#ifndef _PBIO_SOUND_MP3_H_
#define _PBIO_SOUND_MP3_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pbio/config.h>
#include <pbio/error.h>

#if PBIO_CONFIG_SOUND_MP3

/**
 * Playback and timing statistics.
 */
typedef struct {
    /** Whether audio is currently playing. */
    bool playing;
    /** Whether playback is paused and can be resumed. */
    bool paused;
    /** Sample rate of the stream in Hz. */
    uint32_t sample_rate;
    /** Number of channels in the stream (stereo is mixed to mono). */
    uint32_t channels;
    /** Bitrate of the most recent frame in kbit/s. */
    uint32_t bitrate_kbps;
    /** Number of MP3 frames decoded. The timing statistics below are since
     * the start of playback or since they were last reset. */
    uint32_t frames;
    /** Total time spent decoding frames. */
    uint32_t decode_us_total;
    /** Shortest time to decode one frame. */
    uint32_t decode_us_min;
    /** Longest time to decode one frame. */
    uint32_t decode_us_max;
    /** Time since playback started, until it ended if it has ended. */
    uint32_t elapsed_us;
    /** Duration of one buffer half. A half must be refilled within this time. */
    uint32_t half_period_us;
    /** Longest time from a half becoming free until it was refilled. */
    uint32_t fill_latency_us_max;
    /** Number of buffer halves played before they were refilled. */
    uint32_t underruns;
    /** Longest time spent in one run of the background process. */
    uint32_t step_us_max;
    /** Number of buffer halves played. */
    uint32_t halves_played;
    /** Size of the memory used by the decoder and buffers. */
    uint32_t context_size;
} pbio_sound_mp3_stats_t;

size_t pbio_sound_mp3_get_context_size(void);

pbio_error_t pbio_sound_mp3_start(void *context, const uint8_t *data, uint32_t size, bool loop, bool persist);

pbio_error_t pbio_sound_mp3_start_file(void *context, const char *path, bool loop, bool persist);

void pbio_sound_mp3_stop(void);

bool pbio_sound_mp3_is_playing(void);

bool pbio_sound_mp3_is_paused(void);

void pbio_sound_mp3_pause(void);

void pbio_sound_mp3_resume(void);

bool pbio_sound_mp3_stop_for_program_end(void);

void pbio_sound_mp3_get_stats(pbio_sound_mp3_stats_t *stats, bool reset);

#else // PBIO_CONFIG_SOUND_MP3

static inline void pbio_sound_mp3_stop(void) {
}

static inline bool pbio_sound_mp3_is_playing(void) {
    return false;
}

static inline bool pbio_sound_mp3_is_paused(void) {
    return false;
}

static inline bool pbio_sound_mp3_stop_for_program_end(void) {
    return false;
}

#endif // PBIO_CONFIG_SOUND_MP3

#endif // _PBIO_SOUND_MP3_H_

/** @} */
