// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

// Volume shared by all sounds, kept outside of the application so that it
// survives user programs starting and stopping.

#include <stdint.h>

#include <pbdrv/config.h>

#include <pbio/sound_volume.h>

#ifndef PBDRV_CONFIG_SOUND_DEFAULT_VOLUME
#define PBDRV_CONFIG_SOUND_DEFAULT_VOLUME (100)
#endif

static uint8_t pbio_sound_volume_percent = PBDRV_CONFIG_SOUND_DEFAULT_VOLUME;

// Read while sound is playing, so changes take effect right away.
static volatile uint16_t pbio_sound_volume_sample_attenuator = INT16_MAX;

/**
 * Sets the volume for all sounds.
 *
 * @param [in]  percent             The volume in percent, for reading back.
 * @param [in]  sample_attenuator   The matching sample scale factor.
 */
void pbio_sound_volume_set(uint8_t percent, uint16_t sample_attenuator) {
    pbio_sound_volume_percent = percent;
    pbio_sound_volume_sample_attenuator = sample_attenuator;
}

/**
 * Gets the volume in percent.
 *
 * @return  The volume in percent.
 */
uint8_t pbio_sound_volume_get_percent(void) {
    return pbio_sound_volume_percent;
}

/**
 * Gets the scale factor to apply to sound samples.
 *
 * @return  The scale factor (0 to INT16_MAX).
 */
uint16_t pbio_sound_volume_get_sample_attenuator(void) {
    return pbio_sound_volume_sample_attenuator;
}
