// SPDX-License-Identifier: MIT
// Copyright (c) 2020 The Pybricks Authors

/**
 * @addtogroup SoundDriver Driver: Sound
 * @{
 */

#ifndef _PBDRV_SOUND_H_
#define _PBDRV_SOUND_H_

#include <stdint.h>

#include <pbdrv/config.h>
#include <pbio/error.h>


#if PBDRV_CONFIG_SOUND

/**
 * Starts playing a square wave until pbdrv_sound_stop() is called.
 *
 * @param [in]  frequency           The frequency of the wave in Hz.
 * @param [in]  sample_attenuator   The normalized attenuation to apply to get the requested volume.
 */
void pbdrv_beep_start(uint32_t frequency, uint16_t sample_attenuator);

#if PBDRV_CONFIG_SOUND_SAMPLED
/**
 * Starts playing a sound repeatedly until pbdrv_sound_stop() is called.
 *
 * @param [in]  data        The PCM data of the sound to play.
 * @param [in]  length      The number of samples in @p data.
 * @param [in]  sample_rate The sample rate of @p data in Hz.
 */
void pbdrv_sound_start(const uint16_t *data, uint32_t length, uint32_t sample_rate);

#if PBDRV_CONFIG_SOUND_STREAM
/**
 * Starts streaming samples from a double buffer until pbdrv_sound_stop() is
 * called.
 *
 * The hardware plays @p data repeatedly. Each time it finishes one half,
 * the half counter increments and a poll is requested, so the caller can
 * refill that half while the other half plays.
 *
 * @param [in]  data        Buffer of @p length samples, both halves filled.
 * @param [in]  length      The number of samples in @p data. Must be even.
 * @param [in]  sample_rate The sample rate of @p data in Hz.
 */
void pbdrv_sound_stream_start(uint16_t *data, uint32_t length, uint32_t sample_rate);

/**
 * Gets the number of buffer halves completed since streaming started.
 *
 * @param [out] time_us     Time of the most recent completion in microseconds.
 * @return                  Number of halves completed.
 */
uint32_t pbdrv_sound_stream_get_halves_done(uint32_t *time_us);
#endif // PBDRV_CONFIG_SOUND_STREAM
#endif // PBDRV_CONFIG_SOUND_SAMPLED

/**
 * Stops any currently playing sound.
 */
void pbdrv_sound_stop(void);


#else // PBDRV_CONFIG_SOUND

static inline void pbdrv_beep_start(uint32_t frequency, uint16_t sample_attenuator) {
}

static inline void pbdrv_sound_start(const uint16_t *data, uint32_t length, uint32_t sample_rate) {
}

static inline void pbdrv_sound_stop(void) {
}

#endif // PBDRV_CONFIG_SOUND

#endif // _PBDRV_SOUND_H_

/** @} */
