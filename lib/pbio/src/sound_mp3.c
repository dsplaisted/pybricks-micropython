// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

// Experimental MP3 player. A background process decodes one MP3 frame at a
// time and copies the samples into a double buffer that the sound driver
// streams to the speaker. Timing statistics are collected along the way to
// evaluate how much processing time this takes.

#include <pbio/config.h>

#if PBIO_CONFIG_SOUND_MP3

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pbdrv/clock.h>
#include <pbdrv/sound.h>
#include <pbio/os.h>
#include <pbio/sound_mp3.h>

// The decoder normally puts about 16 KB of scratch memory on the stack for
// each frame. This is more than the whole stack on some hubs, so our copy of
// minimp3 is patched to use this pointer instead.
static void *pbio_sound_mp3_scratch;
#define MINIMP3_SCRATCH_PTR ((mp3dec_scratch_t *)pbio_sound_mp3_scratch)

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#include <minimp3.h>
#pragma GCC diagnostic pop

// Number of samples in each half of the double buffer. This is how much
// audio is buffered ahead, so it sets how long the background process may
// be delayed before the audio glitches. At 22050 Hz, this is 93 ms.
#define HALF_SAMPLES (2048)

/**
 * Memory for the decoder and buffers. This is allocated by the caller, so it
 * only uses memory while in use.
 */
typedef struct {
    mp3dec_t decoder;
    mp3dec_scratch_t scratch;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint16_t buffer[2 * HALF_SAMPLES];
} pbio_sound_mp3_context_t;

static struct {
    pbio_sound_mp3_context_t *ctx;
    const uint8_t *data;
    uint32_t size;
    /** Position of the next frame in the data. */
    uint32_t pos;
    bool loop;
    /** Whether playback continues when a user program ends. */
    bool persist;
    /** Volume scale, which may be changed while playing. */
    volatile uint16_t sample_attenuator;
    /** The same volume as a percentage, for reporting it back. */
    uint8_t volume_percent;
    /** Number of decoded samples in the pcm buffer. */
    uint32_t pcm_count;
    /** Number of samples from the pcm buffer copied to the double buffer. */
    uint32_t pcm_pos;
    /** Number of halves filled since the start, including the prefill. */
    uint32_t halves_filled;
    /** Number of samples written to the half being filled. */
    uint32_t fill_pos;
    /** All data has been decoded. */
    bool end_of_data;
    /** Playback ends once this many halves have been played. */
    uint32_t end_halves;
    /** The driver is streaming the double buffer. */
    bool streaming;
    /** Start of the statistics period. */
    uint32_t start_us;
    pbio_sound_mp3_stats_t stats;
} player;

static pbio_os_process_t pbio_sound_mp3_process;

/**
 * Gets the size of the memory that must be passed to ::pbio_sound_mp3_start.
 *
 * @return  Size in bytes.
 */
size_t pbio_sound_mp3_get_context_size(void) {
    return sizeof(pbio_sound_mp3_context_t);
}

/**
 * Decodes the next frame into the pcm buffer.
 *
 * @return  @c false if there is no more audio, @c true otherwise.
 */
static bool pbio_sound_mp3_decode_frame(void) {

    if (player.pos >= player.size) {
        if (!player.loop) {
            return false;
        }
        player.pos = 0;
    }

    mp3dec_frame_info_t info;
    uint32_t start_us = pbdrv_clock_get_us();
    int samples = mp3dec_decode_frame(&player.ctx->decoder, player.data + player.pos,
        player.size - player.pos, player.ctx->pcm, &info);
    uint32_t decode_us = pbdrv_clock_get_us() - start_us;

    // No frames in the remaining data.
    if (info.frame_bytes == 0) {
        bool was_at_start = player.pos == 0;
        player.pos = player.size;
        // Try again from the start if looping, unless there are no frames at all.
        return player.loop && !was_at_start;
    }

    // Skip the frame and any data before it.
    player.pos += info.frame_bytes;

    // Skipped data such as tags, so no samples to add.
    if (samples == 0) {
        return true;
    }

    pbio_sound_mp3_stats_t *stats = &player.stats;
    stats->frames++;
    stats->decode_us_total += decode_us;
    if (decode_us < stats->decode_us_min) {
        stats->decode_us_min = decode_us;
    }
    if (decode_us > stats->decode_us_max) {
        stats->decode_us_max = decode_us;
    }
    stats->bitrate_kbps = info.bitrate_kbps;
    stats->channels = info.channels;
    if (stats->sample_rate == 0) {
        stats->sample_rate = info.hz;
    }

    // Mix stereo to mono in place.
    mp3d_sample_t *pcm = player.ctx->pcm;
    if (info.channels == 2) {
        for (int i = 0; i < samples; i++) {
            pcm[i] = (pcm[2 * i] + pcm[2 * i + 1]) / 2;
        }
    }

    player.pcm_count = samples;
    player.pcm_pos = 0;
    return true;
}

/**
 * Copies decoded samples into the half being filled, scaled for the volume
 * and converted to the format of the sound driver.
 */
static void pbio_sound_mp3_copy_samples(void) {
    uint16_t *dst = player.ctx->buffer + (player.halves_filled % 2) * HALF_SAMPLES;
    int32_t attenuator = player.sample_attenuator;
    while (player.fill_pos < HALF_SAMPLES && player.pcm_pos < player.pcm_count) {
        int32_t sample = player.ctx->pcm[player.pcm_pos++];
        dst[player.fill_pos++] = INT16_MAX + ((sample * attenuator) >> 15);
    }
}

/**
 * Fills the remainder of the half being filled with silence.
 */
static void pbio_sound_mp3_fill_silence(void) {
    uint16_t *dst = player.ctx->buffer + (player.halves_filled % 2) * HALF_SAMPLES;
    while (player.fill_pos < HALF_SAMPLES) {
        dst[player.fill_pos++] = INT16_MAX;
    }
}

static void pbio_sound_mp3_stop_streaming(void) {
    if (player.streaming) {
        pbdrv_sound_stop();
        player.stats.elapsed_us = pbdrv_clock_get_us() - player.start_us;
    }
    player.streaming = false;
    player.stats.playing = false;
}

/**
 * Does one small unit of work: decoding one frame, or copying samples into
 * the buffer. This keeps each run of the background process short.
 *
 * @return  @c true if there is more work to do right away.
 */
static bool pbio_sound_mp3_step(void) {

    uint32_t irq_us = 0;
    uint32_t done = player.streaming ? pbdrv_sound_stream_get_halves_done(&irq_us) : 0;
    player.stats.halves_played = done;

    // All audio has been played.
    if (player.streaming && player.end_of_data && done >= player.end_halves) {
        pbio_sound_mp3_stop_streaming();
        return false;
    }

    // Both halves are full. Wait until one has been played.
    if (player.halves_filled >= done + 2) {
        return false;
    }

    // The half we are filling has already started playing, so old data is
    // playing instead. Continue the audio in the half after the one that is
    // playing now.
    bool was_late = player.streaming && done >= player.halves_filled;
    if (was_late) {
        player.stats.underruns += done - player.halves_filled + 1;
        player.halves_filled = done + 1;
        player.fill_pos = 0;
    }

    pbio_sound_mp3_copy_samples();

    // Need more samples to complete this half.
    if (player.fill_pos < HALF_SAMPLES) {
        if (!player.end_of_data && pbio_sound_mp3_decode_frame()) {
            // Copy the new samples on the next step.
            return true;
        }
        if (!player.end_of_data) {
            // This is the last half with audio.
            player.end_of_data = true;
            player.end_halves = player.halves_filled + 1;
        }
        pbio_sound_mp3_fill_silence();
    }

    // This half is complete. It became free when the previous half started
    // playing, which is the most recent completion. After being late, the
    // most recent completion is not the one that freed this half, so the
    // measurement would be meaningless.
    if (player.streaming && !was_late) {
        uint32_t latency_us = pbdrv_clock_get_us() - irq_us;
        if (latency_us > player.stats.fill_latency_us_max) {
            player.stats.fill_latency_us_max = latency_us;
        }
    }
    player.halves_filled++;
    player.fill_pos = 0;
    return true;
}

static pbio_error_t pbio_sound_mp3_process_thread(pbio_os_state_t *state, void *context) {

    if (!player.streaming) {
        return PBIO_SUCCESS;
    }

    uint32_t start_us = pbdrv_clock_get_us();
    bool more = pbio_sound_mp3_step();
    uint32_t step_us = pbdrv_clock_get_us() - start_us;
    if (step_us > player.stats.step_us_max) {
        player.stats.step_us_max = step_us;
    }

    // Come back soon if there is more to do, but let other processes run
    // first. Otherwise, the driver requests a poll when a half is free.
    if (more) {
        pbio_os_request_poll();
    }

    return player.streaming ? PBIO_ERROR_AGAIN : PBIO_SUCCESS;
}

/**
 * Starts playing MP3 data in the background.
 *
 * @param [in]  context             Memory of ::pbio_sound_mp3_get_context_size
 *                                  bytes. Must remain valid until playback
 *                                  ends or ::pbio_sound_mp3_stop is called.
 * @param [in]  data                The MP3 data.
 * @param [in]  size                Size of @p data.
 * @param [in]  sample_attenuator   Volume scale factor (0 to INT16_MAX).
 * @param [in]  loop                Whether to repeat from the start at the end.
 * @return                          ::PBIO_ERROR_INVALID_ARG if there is no
 *                                  audio in @p data, otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_sound_mp3_start(void *context, const uint8_t *data, uint32_t size, uint16_t sample_attenuator, bool loop, bool persist) {

    pbio_sound_mp3_stop();

    memset(&player, 0, sizeof(player));
    player.ctx = context;
    player.data = data;
    player.size = size;
    player.loop = loop;
    player.persist = persist;
    player.sample_attenuator = sample_attenuator;
    player.stats.decode_us_min = UINT32_MAX;
    player.stats.context_size = sizeof(pbio_sound_mp3_context_t);

    pbio_sound_mp3_scratch = &player.ctx->scratch;
    mp3dec_init(&player.ctx->decoder);

    // Fill both halves before starting, which also finds the sample rate.
    while (player.halves_filled < 2) {
        pbio_sound_mp3_step();
    }

    uint32_t sample_rate = player.stats.sample_rate;
    if (sample_rate == 0) {
        return PBIO_ERROR_INVALID_ARG;
    }
    player.stats.half_period_us = HALF_SAMPLES * 1000000 / sample_rate;

    player.streaming = true;
    player.stats.playing = true;
    player.start_us = pbdrv_clock_get_us();
    pbdrv_sound_stream_start(player.ctx->buffer, 2 * HALF_SAMPLES, sample_rate);
    pbio_os_process_start(&pbio_sound_mp3_process, pbio_sound_mp3_process_thread, NULL);

    return PBIO_SUCCESS;
}

/**
 * Stops playing. After this, the context memory is no longer used.
 */
void pbio_sound_mp3_stop(void) {
    pbio_sound_mp3_stop_streaming();
}

/**
 * Sets the volume, also while playing.
 *
 * Samples that are already buffered keep the previous volume, so the change
 * takes effect after up to two buffer halves.
 *
 * @param [in]  sample_attenuator   Volume scale factor (0 to INT16_MAX).
 * @param [in]  percent             The same volume in percent.
 */
void pbio_sound_mp3_set_volume(uint16_t sample_attenuator, uint8_t percent) {
    player.sample_attenuator = sample_attenuator;
    player.volume_percent = percent;
}

/**
 * Gets the volume of the sound that is currently playing.
 *
 * Lets a new program pick up where the previous one left off, instead of
 * changing the volume of music that is already playing.
 *
 * @param [out] sample_attenuator   Volume scale factor.
 * @param [out] percent             The same volume in percent.
 * @return                          @c false if nothing is playing, in which
 *                                  case the values are not set.
 */
bool pbio_sound_mp3_get_volume(uint16_t *sample_attenuator, uint8_t *percent) {
    if (!player.streaming) {
        return false;
    }
    *sample_attenuator = player.sample_attenuator;
    *percent = player.volume_percent;
    return true;
}

/**
 * Stops playing when a user program ends, unless playback was started with
 * @c persist so that it continues across program runs.
 *
 * @return  @c true if playback continues, @c false otherwise.
 */
bool pbio_sound_mp3_stop_for_program_end(void) {
    if (player.streaming && player.persist) {
        return true;
    }
    pbio_sound_mp3_stop_streaming();
    return false;
}

/**
 * Gets playback and timing statistics of the current or most recent playback.
 *
 * @param [out] stats   The statistics.
 * @param [in]  reset   Whether to restart the timing statistics after this,
 *                      to measure the next period separately.
 */
void pbio_sound_mp3_get_stats(pbio_sound_mp3_stats_t *stats, bool reset) {
    *stats = player.stats;
    if (player.streaming) {
        stats->elapsed_us = pbdrv_clock_get_us() - player.start_us;
    }
    if (stats->decode_us_min == UINT32_MAX) {
        stats->decode_us_min = 0;
    }

    if (reset) {
        player.start_us = pbdrv_clock_get_us();
        player.stats.frames = 0;
        player.stats.decode_us_total = 0;
        player.stats.decode_us_min = UINT32_MAX;
        player.stats.decode_us_max = 0;
        player.stats.elapsed_us = 0;
        player.stats.fill_latency_us_max = 0;
        player.stats.underruns = 0;
        player.stats.step_us_max = 0;
    }
}

#endif // PBIO_CONFIG_SOUND_MP3
