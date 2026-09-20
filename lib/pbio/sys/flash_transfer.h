// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

// Answers requests from the computer about the files on the external flash.

#ifndef _PBSYS_SYS_FLASH_TRANSFER_H_
#define _PBSYS_SYS_FLASH_TRANSFER_H_

#include <stdint.h>

#include <pbio/config.h>
#include <pbio/protocol.h>

/**
 * What the computer asks for.
 */
enum {
    /** Lists a directory, with the path as the payload. */
    PBSYS_FLASH_TRANSFER_LIST = 0,
    /** Reads a file, with offset, size and path as the payload. */
    PBSYS_FLASH_TRANSFER_READ = 1,
};

/**
 * First byte of the events that answer such a request.
 */
enum {
    /** A piece of the answer, which follows. */
    PBSYS_FLASH_TRANSFER_REPLY_DATA = 0,
    /** The answer is complete. */
    PBSYS_FLASH_TRANSFER_REPLY_END = 1,
    /** The request failed, with the step and the error following. */
    PBSYS_FLASH_TRANSFER_REPLY_ERROR = 2,
};

#if PBIO_CONFIG_FLASH_FS

void pbsys_flash_transfer_init(void);

pbio_pybricks_error_t pbsys_flash_transfer_request(uint8_t operation, const uint8_t *data, uint32_t size);

#else

static inline void pbsys_flash_transfer_init(void) {
}

#endif // PBIO_CONFIG_FLASH_FS

#endif // _PBSYS_SYS_FLASH_TRANSFER_H_
