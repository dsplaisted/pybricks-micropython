// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

/**
 * @addtogroup FlashFs pbio/flash_fs: External flash file system
 *
 * Reads the littlefs file system that the firmware of LEGO uses on the
 * external flash, so that files such as songs can be shared with it instead
 * of overwriting it.
 *
 * Experimental.
 * @{
 */

#ifndef _PBIO_FLASH_FS_H_
#define _PBIO_FLASH_FS_H_

#include <stdbool.h>
#include <stdint.h>

#include <pbio/config.h>
#include <pbio/error.h>

#if PBIO_CONFIG_FLASH_FS

/**
 * Called for each entry when listing a directory.
 *
 * @param [in]  name        Name of the entry.
 * @param [in]  size        Size in bytes, or 0 for a directory.
 * @param [in]  is_dir      Whether the entry is a directory.
 * @param [in]  context     Caller context.
 */
typedef void (*pbio_flash_fs_list_callback_t)(const char *name, uint32_t size, bool is_dir, void *context);

pbio_error_t pbio_flash_fs_mount(void);

void pbio_flash_fs_unmount(void);

pbio_error_t pbio_flash_fs_list(const char *path, pbio_flash_fs_list_callback_t callback, void *context);

pbio_error_t pbio_flash_fs_get_usage(uint32_t *used_bytes, uint32_t *total_bytes);

pbio_error_t pbio_flash_fs_write_open(const char *path);

pbio_error_t pbio_flash_fs_write_append(const uint8_t *data, uint32_t size);

pbio_error_t pbio_flash_fs_stream_open(const char *path, uint32_t *size);

pbio_error_t pbio_flash_fs_stream_read(uint8_t *buffer, uint32_t size, uint32_t *size_read);

pbio_error_t pbio_flash_fs_stream_rewind(void);

void pbio_flash_fs_stream_close(void);

pbio_error_t pbio_flash_fs_read_file(const char *path, uint32_t offset, uint8_t *buffer, uint32_t size, uint32_t *size_read);

pbio_error_t pbio_flash_fs_write_file(const char *path, const uint8_t *data, uint32_t size);

pbio_error_t pbio_flash_fs_remove(const char *path);

#endif // PBIO_CONFIG_FLASH_FS

#endif // _PBIO_FLASH_FS_H_

/** @} */
