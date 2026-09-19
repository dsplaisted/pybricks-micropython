// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

/**
 * @addtogroup SoundVolume pbio/sound_volume: Sound volume
 *
 * The volume shared by all sounds. It is kept here rather than in the
 * application so that it is not reset when a user program starts, which
 * matters when sound keeps playing between programs.
 * @{
 */

#ifndef _PBIO_SOUND_VOLUME_H_
#define _PBIO_SOUND_VOLUME_H_

#include <stdint.h>

void pbio_sound_volume_set(uint8_t percent, uint16_t sample_attenuator);

uint8_t pbio_sound_volume_get_percent(void);

uint16_t pbio_sound_volume_get_sample_attenuator(void);

#endif // _PBIO_SOUND_VOLUME_H_

/** @} */
