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
#include <pbio/flash_fs.h>
#include <pbio/sound_mp3.h>
#include <pbio/sound_volume.h>

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

// Bytes of song held in memory when playing from a file. Must be well over
// the largest MP3 frame, which is about 1.4 KB.
#define WINDOW_SIZE (8192)

// How much is read per step. Small enough that a read step costs less than
// a decode step, which keeps the longest step short.
#define WINDOW_READ_SIZE (1024)

// Decoding is only attempted with at least this much data, unless the end of
// the song has been reached.
#define WINDOW_MIN_DECODE (2048)

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
    // Only used when playing from a file.
    uint8_t window[WINDOW_SIZE];
} pbio_sound_mp3_context_t;

static struct {
    pbio_sound_mp3_context_t *ctx;
    /** Size of the file being played. */
    uint32_t size;
    /** Whether a file is open, so that it gets closed when playing stops. */
    bool file_open;
    /** Bytes of the file held in the window, and how much is used. */
    uint32_t window_size;
    uint32_t window_pos;
    /** Whether the end of the file has been read into the window. */
    bool end_of_file;
    bool loop;
    /** Whether playback continues when a user program ends. */
    bool persist;
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
    /** Playback is paused and can be resumed where it left off. */
    bool paused;
    /** Start of the statistics period. */
    uint32_t start_us;
    /** How far into the song playback began, for reporting the position. */
    uint32_t start_ms;
    pbio_sound_mp3_stats_t stats;
} player;

static pbio_os_process_t pbio_sound_mp3_process;
/**
 * Which song to play and how, kept apart from the player so that it outlives
 * both a song and the program that chose it.
 */
static struct {
    /** Whether a song has been chosen. */
    bool valid;
    /** The file on the external flash. */
    char path[PBIO_SOUND_MP3_PATH_SIZE];
    /** Whether the song starts again when it ends. */
    bool repeat;
    /** Whether playback continues when the program that started it ends. */
    bool persist;
} selection;

/**
 * Works out where a song lives.
 *
 * A name on its own is a song in ::PBIO_SOUND_MP3_DIR. Anything with a slash
 * in it is a path of its own, so that songs elsewhere can still be played.
 *
 * @param [in]  path    The name or path of the song.
 * @param [out] buffer  Where to write the full path.
 * @param [in]  size    Size of @p buffer.
 * @return              ::PBIO_ERROR_INVALID_ARG if the result doesn't fit,
 *                      otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_sound_mp3_resolve_path(const char *path, char *buffer, size_t size) {

    if (path[0] == '\0') {
        return PBIO_ERROR_INVALID_ARG;
    }

    if (strchr(path, '/')) {
        if (strlen(path) >= size) {
            return PBIO_ERROR_INVALID_ARG;
        }
        strcpy(buffer, path);
        return PBIO_SUCCESS;
    }

    if (strlen(PBIO_SOUND_MP3_DIR) + 1 + strlen(path) >= size) {
        return PBIO_ERROR_INVALID_ARG;
    }

    strcpy(buffer, PBIO_SOUND_MP3_DIR "/");
    strcat(buffer, path);
    return PBIO_SUCCESS;
}

/**
 * Chooses a song from a file on the external flash.
 *
 * @param [in]  path    The name of the song, or a path to it.
 * @return              ::PBIO_ERROR_INVALID_ARG if the path is too long,
 *                      the error from reading the file if it cannot be read,
 *                      otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_sound_mp3_select_file(const char *path) {

    char full[PBIO_SOUND_MP3_PATH_SIZE];
    pbio_error_t err = pbio_sound_mp3_resolve_path(path, full, sizeof(full));
    if (err != PBIO_SUCCESS) {
        return err;
    }

    // Reading the start of the file says whether it can be played at all,
    // which is friendlier to find out now than when it is due to play.
    err = pbio_flash_fs_mount();
    if (err != PBIO_SUCCESS) {
        return err;
    }

    uint8_t probe;
    uint32_t size_read;
    err = pbio_flash_fs_read_file(full, 0, &probe, sizeof(probe), &size_read);
    pbio_flash_fs_unmount();

    if (err != PBIO_SUCCESS) {
        return err;
    }

    strcpy(selection.path, full);
    selection.valid = true;
    return PBIO_SUCCESS;
}

/**
 * Gets whether a song has been chosen.
 */
bool pbio_sound_mp3_is_selected(void) {
    return selection.valid;
}

/**
 * Gets the file that was chosen, or @c NULL when nothing is chosen.
 */
const char *pbio_sound_mp3_get_selected_path(void) {
    return selection.valid ? selection.path : NULL;
}

/**
 * Sets whether the song starts again when it ends. Takes effect right away,
 * so it can be turned off while the last time through is playing.
 */
void pbio_sound_mp3_set_repeat(bool repeat) {
    selection.repeat = repeat;
    player.loop = repeat;
}

/**
 * Gets whether the song starts again when it ends.
 */
bool pbio_sound_mp3_get_repeat(void) {
    return selection.repeat;
}

/**
 * Sets whether playback continues when the program that started it ends.
 * Takes effect the next time a song starts, since where the decoder keeps
 * its memory depends on it.
 */
void pbio_sound_mp3_set_persist(bool persist) {
    selection.persist = persist;
}

/**
 * Gets whether playback continues when the program that started it ends.
 */
bool pbio_sound_mp3_get_persist(void) {
    return selection.persist;
}

/**
 * Plays the chosen song.
 *
 * @param [in]  context     Memory of ::pbio_sound_mp3_get_context_size bytes.
 * @param [in]  start_ms    How far into the song to begin, or 0 for the start.
 * @return                  ::PBIO_ERROR_INVALID_OP if no song is chosen,
 *                          otherwise what starting it gives.
 */
pbio_error_t pbio_sound_mp3_play(void *context, uint32_t start_ms) {

    if (!selection.valid) {
        return PBIO_ERROR_INVALID_OP;
    }

    return pbio_sound_mp3_start_file(context, selection.path, start_ms);
}


/**
 * Gets the size of the memory that must be passed to ::pbio_sound_mp3_start.
 *
 * @return  Size in bytes.
 */
size_t pbio_sound_mp3_get_context_size(void) {
    return sizeof(pbio_sound_mp3_context_t);
}

/**
 * Gets the song data at the current position.
 *
 * @param [out] available   Number of bytes available from there.
 * @return                  The data.
 */
static const uint8_t *pbio_sound_mp3_source(uint32_t *available) {
    *available = player.window_size - player.window_pos;
    return player.ctx->window + player.window_pos;
}

/**
 * Advances past data that has been decoded.
 */
static void pbio_sound_mp3_consume(uint32_t size) {
    player.window_pos += size;
}

/**
 * Tests whether more of the file should be read before decoding again.
 */
static bool pbio_sound_mp3_needs_data(void) {
    uint32_t available;
    pbio_sound_mp3_source(&available);
    return !player.end_of_file && available < WINDOW_MIN_DECODE;
}

/**
 * Reads the next piece of the file into the window.
 */
static void pbio_sound_mp3_read_data(void) {

    // Move what is left to the start to make room.
    uint32_t remaining = player.window_size - player.window_pos;
    if (player.window_pos > 0) {
        memmove(player.ctx->window, player.ctx->window + player.window_pos, remaining);
        player.window_pos = 0;
        player.window_size = remaining;
    }

    uint32_t space = WINDOW_SIZE - player.window_size;
    uint32_t size = space < WINDOW_READ_SIZE ? space : WINDOW_READ_SIZE;

    uint32_t size_read = 0;
    if (pbio_flash_fs_stream_read(player.ctx->window + player.window_size, size, &size_read) != PBIO_SUCCESS) {
        player.end_of_file = true;
        return;
    }

    player.window_size += size_read;
    if (size_read < size) {
        player.end_of_file = true;
    }
}

/**
 * Starts the file again from the beginning, for playing in a loop.
 */
static bool pbio_sound_mp3_rewind(void) {
    if (pbio_flash_fs_stream_rewind() != PBIO_SUCCESS) {
        return false;
    }
    player.window_size = 0;
    player.window_pos = 0;
    player.end_of_file = false;
    player.start_ms = 0;
    return true;
}

/**
 * Decodes the next frame into the pcm buffer.
 *
 * @return  @c false if there is no more audio, @c true otherwise.
 */
static bool pbio_sound_mp3_decode_frame(void) {

    uint32_t available;
    const uint8_t *data = pbio_sound_mp3_source(&available);

    if (available == 0) {
        if (!player.loop) {
            return false;
        }
        return pbio_sound_mp3_rewind();
    }

    mp3dec_frame_info_t info;
    uint32_t start_us = pbdrv_clock_get_us();
    int samples = mp3dec_decode_frame(&player.ctx->decoder, data, available, player.ctx->pcm, &info);
    uint32_t decode_us = pbdrv_clock_get_us() - start_us;

    // No frames in the remaining data.
    if (info.frame_bytes == 0) {
        pbio_sound_mp3_consume(available);
        // Try again from the start if looping.
        if (!player.loop) {
            return false;
        }
        return pbio_sound_mp3_rewind();
    }

    // Skip the frame and any data before it.
    pbio_sound_mp3_consume(info.frame_bytes);

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
    int32_t attenuator = pbio_sound_volume_get_sample_attenuator();
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

static void pbio_sound_mp3_close_file(void) {
    if (player.file_open) {
        pbio_flash_fs_stream_close();
        player.file_open = false;
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
        pbio_sound_mp3_close_file();
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
        // Reading from the file is done in a step of its own, so that it is
        // never added to the time taken by decoding a frame.
        if (!player.end_of_data && pbio_sound_mp3_needs_data()) {
            pbio_sound_mp3_read_data();
            return true;
        }
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
 * Starts the driver streaming the double buffer, which must be filled.
 */
static void pbio_sound_mp3_start_streaming(void) {
    player.streaming = true;
    player.stats.playing = true;
    player.stats.paused = false;
    // Keep the statistics running across a pause.
    player.start_us = pbdrv_clock_get_us() - player.stats.elapsed_us;
    pbdrv_sound_stream_start(player.ctx->buffer, 2 * HALF_SAMPLES, player.stats.sample_rate);
    pbio_os_process_start(&pbio_sound_mp3_process, pbio_sound_mp3_process_thread, NULL);
}

/**
 * Starts playing MP3 data in the background.
 *
 * @param [in]  context             Memory of ::pbio_sound_mp3_get_context_size
 *                                  bytes. Must remain valid until playback
 *                                  ends or ::pbio_sound_mp3_stop is called.
 * @param [in]  data                The MP3 data.
 * @param [in]  size                Size of @p data.
 * @param [in]  loop                Whether to repeat from the start at the end.
 * @return                          ::PBIO_ERROR_INVALID_ARG if there is no
 *                                  audio in @p data, otherwise ::PBIO_SUCCESS.
 */
static pbio_error_t pbio_sound_mp3_start_common(void *context, bool loop, bool persist) {

    player.ctx = context;
    player.loop = loop;
    player.persist = persist;
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

    pbio_sound_mp3_start_streaming();

    return PBIO_SUCCESS;
}

/**
 * Tests whether a frame carries a tag saying how the file was made, rather
 * than any music. Encoders put one at the start of a file, and it is not
 * always at the rate the music is, so it must not be measured by.
 *
 * @param [in]  frame   The start of the frame.
 * @param [in]  size    How much of the frame is there.
 */
static bool pbio_sound_mp3_frame_has_tag(const uint8_t *frame, uint32_t size) {

    // The tag sits a little way into the frame, past the header.
    for (uint32_t i = 0; i + 4 <= size && i < 64; i++) {
        if (memcmp(&frame[i], "Xing", 4) == 0 || memcmp(&frame[i], "Info", 4) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * Works out how far into the data a moment in the song is.
 *
 * The frames of a song encoded at a constant rate are all the same size, so
 * how dense one of them is says where any moment lives. A song encoded at a
 * varying rate would only be found approximately this way.
 *
 * @param [in]  context     The decoder memory, used for the scratch space.
 * @param [in]  data        The start of the song.
 * @param [in]  size        How much of the song @p data holds.
 * @param [in]  start_ms    The moment to look for.
 * @return                  Offset in bytes, or 0 if the rate can't be read.
 */
static uint32_t pbio_sound_mp3_offset_of(void *context, const uint8_t *data, uint32_t size, uint32_t start_ms) {

    pbio_sound_mp3_context_t *ctx = context;

    pbio_sound_mp3_scratch = &ctx->scratch;
    mp3dec_init(&ctx->decoder);

    // Passing no output asks for the frame to be read but not decoded.
    mp3dec_frame_info_t info = { 0 };
    mp3dec_decode_frame(&ctx->decoder, data, size, NULL, &info);

    if (info.frame_bytes <= 0) {
        return 0;
    }

    // Where the music starts, which is not the start of the file when there
    // is anything in front of it.
    uint32_t music_start = info.frame_offset;

    if (pbio_sound_mp3_frame_has_tag(data + info.frame_offset, info.frame_bytes - info.frame_offset)) {
        // That frame was the tag, so the music and its rate are in the next.
        music_start = info.frame_bytes;

        mp3dec_frame_info_t next = { 0 };
        mp3dec_decode_frame(&ctx->decoder, data + music_start, size - music_start, NULL, &next);

        if (next.frame_bytes <= 0) {
            return 0;
        }

        music_start += next.frame_offset;
        info = next;
    }

    if (info.bitrate_kbps == 0) {
        return 0;
    }

    // Bits per second over eight is bytes per millisecond.
    return music_start + start_ms * info.bitrate_kbps / 8;
}

/**
 * Starts playing an MP3 file on the external flash.
 *
 * The file stays open while it plays, so nothing else may write to the
 * flash until playback stops.
 *
 * @param [in]  context     Memory of ::pbio_sound_mp3_get_context_size bytes.
 * @param [in]  path        The file to play.
 * @param [in]  start_ms    How far into the song to begin, or 0 for the start.
 *                          A moment past the end starts from the beginning.
 * @return                  ::PBIO_ERROR_FAILED if the file can't be opened,
 *                          ::PBIO_ERROR_INVALID_ARG if it has no audio,
 *                          otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_sound_mp3_start_file(void *context, const char *path, uint32_t start_ms) {

    pbio_sound_mp3_stop();

    memset(&player, 0, sizeof(player));
    player.file_open = true;

    uint32_t size;
    pbio_error_t err = pbio_flash_fs_stream_open(path, &size);
    if (err != PBIO_SUCCESS) {
        return err;
    }
    player.size = size;

    if (start_ms > 0) {
        // How dense the song is has to be read from the song itself, so the
        // start of the file is read before deciding where to carry on from.
        pbio_sound_mp3_context_t *ctx = context;
        uint32_t size_read = 0;
        uint32_t offset = 0;

        if (pbio_flash_fs_stream_read(ctx->window, WINDOW_READ_SIZE, &size_read) == PBIO_SUCCESS) {
            offset = pbio_sound_mp3_offset_of(context, ctx->window, size_read, start_ms);
        }

        if (offset > 0 && offset < size) {
            player.start_ms = start_ms;
        } else {
            offset = 0;
        }

        // Reading to find that out moved through the file, so go where the
        // song is meant to carry on from, which is the start if it is not.
        err = pbio_flash_fs_stream_seek(offset);
        if (err != PBIO_SUCCESS) {
            pbio_flash_fs_stream_close();
            return err;
        }
    }

    err = pbio_sound_mp3_start_common(context, selection.repeat, selection.persist);
    if (err != PBIO_SUCCESS) {
        pbio_flash_fs_stream_close();
    }
    return err;
}

/**
 * Stops playing. After this, the context memory is no longer used.
 */
void pbio_sound_mp3_stop(void) {
    pbio_sound_mp3_stop_streaming();
    pbio_sound_mp3_close_file();
    player.paused = false;
    player.stats.paused = false;
}

/**
 * Tests if sound is currently playing.
 *
 * @return  @c true if playing, @c false when stopped or paused.
 */
bool pbio_sound_mp3_is_playing(void) {
    return player.streaming;
}

/**
 * Tests if playback is paused and can be resumed.
 *
 * @return  @c true if paused.
 */
bool pbio_sound_mp3_is_paused(void) {
    return player.paused;
}

/**
 * Pauses playback, keeping the position in the song.
 */
void pbio_sound_mp3_pause(void) {
    if (!player.streaming) {
        return;
    }
    pbio_sound_mp3_stop_streaming();
    player.paused = true;
    player.stats.paused = true;
}

/**
 * Resumes playback from where it was paused.
 *
 * Playback restarts at the beginning of the buffer, which still holds the
 * audio from when it was paused. Up to two buffer halves are therefore
 * played again, rather than skipped.
 */
void pbio_sound_mp3_resume(void) {
    if (!player.paused || player.streaming) {
        return;
    }
    player.paused = false;

    // Both halves still hold audio, and the driver starts at the first one.
    player.halves_filled = 2;
    player.fill_pos = 0;

    // The end is counted in halves played, which restarts at zero.
    if (player.end_of_data) {
        player.end_halves = 2;
    }

    pbio_sound_mp3_start_streaming();
}

/**
 * Stops playing when a user program ends, unless playback was started with
 * @c persist so that it continues across program runs.
 *
 * @return  @c true if playback continues, @c false otherwise.
 */
bool pbio_sound_mp3_stop_for_program_end(void) {
    if ((player.streaming || player.paused) && player.persist) {
        return true;
    }
    pbio_sound_mp3_stop_streaming();
    pbio_sound_mp3_close_file();
    return false;
}

/**
 * Gets how far into the song playback has got, counting from the start of
 * the song rather than from where this playback began.
 *
 * @return  Position in milliseconds.
 */
uint32_t pbio_sound_mp3_get_position(void) {

    uint32_t elapsed_us = player.streaming ?
        pbdrv_clock_get_us() - player.start_us : player.stats.elapsed_us;

    return player.start_ms + elapsed_us / 1000;
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
