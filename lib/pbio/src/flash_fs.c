// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

// Reads the littlefs file system of the firmware of LEGO on the external
// flash. Sharing that file system means songs and their data can live on the
// same hub, instead of one overwriting the other.
//
// Experimental, and currently read only: programming and erasing are not
// implemented, so nothing here can modify the file system.

#include <pbio/config.h>

#if PBIO_CONFIG_FLASH_FS

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pbdrv/block_device.h>

#include <pbio/error.h>
#include <pbio/flash_fs.h>
#include <pbio/os.h>

#include "lfs2.h"

// Where the file system starts on the chip, as used by the firmware of LEGO
// and by upstream MicroPython for this hub.
#define FS_START_ADDRESS (1024 * 1024)

// From the superblock of a hub: 4 KB blocks covering the rest of the chip.
#define FS_BLOCK_SIZE (4096)
#define FS_BLOCK_COUNT (7936)

// Matching the defaults that MicroPython uses for this file system.
#define FS_READ_SIZE (32)
#define FS_PROG_SIZE (32)
#define FS_CACHE_SIZE (128)
#define FS_LOOKAHEAD_SIZE (32)

static uint8_t read_buffer[FS_CACHE_SIZE];
static uint8_t prog_buffer[FS_CACHE_SIZE];
static uint8_t lookahead_buffer[FS_LOOKAHEAD_SIZE];

static lfs2_t lfs;
static bool mounted;

/**
 * Reads from the chip, waiting for the transfer to complete.
 *
 * The file system calls this and expects the data to be there when it
 * returns, so this waits rather than yielding. Reads are kept small by the
 * cache size, so this takes well under a millisecond.
 */
static int flash_fs_read(const struct lfs2_config *c, lfs2_block_t block, lfs2_off_t off, void *buffer, lfs2_size_t size) {

    uint32_t address = FS_START_ADDRESS + block * c->block_size + off;

    pbio_os_state_t state = 0;
    pbio_error_t err;
    while ((err = pbdrv_block_device_read_raw(&state, address, buffer, size)) == PBIO_ERROR_AGAIN) {
        // The read completes on interrupts, so keep driving it.
    }

    return err == PBIO_SUCCESS ? LFS2_ERR_OK : LFS2_ERR_IO;
}

static int flash_fs_prog(const struct lfs2_config *c, lfs2_block_t block, lfs2_off_t off, const void *buffer, lfs2_size_t size) {
    // Read only for now.
    return LFS2_ERR_IO;
}

static int flash_fs_erase(const struct lfs2_config *c, lfs2_block_t block) {
    // Read only for now.
    return LFS2_ERR_IO;
}

static int flash_fs_sync(const struct lfs2_config *c) {
    return LFS2_ERR_OK;
}

static const struct lfs2_config config = {
    .read = flash_fs_read,
    .prog = flash_fs_prog,
    .erase = flash_fs_erase,
    .sync = flash_fs_sync,
    .read_size = FS_READ_SIZE,
    .prog_size = FS_PROG_SIZE,
    .block_size = FS_BLOCK_SIZE,
    .block_count = FS_BLOCK_COUNT,
    .block_cycles = 100,
    .cache_size = FS_CACHE_SIZE,
    .lookahead_size = FS_LOOKAHEAD_SIZE,
    .read_buffer = read_buffer,
    .prog_buffer = prog_buffer,
    .lookahead_buffer = lookahead_buffer,
};

/**
 * Mounts the file system.
 *
 * @return  ::PBIO_ERROR_FAILED if there is no file system to mount,
 *          otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_mount(void) {

    if (mounted) {
        return PBIO_SUCCESS;
    }

    if (lfs2_mount(&lfs, &config) != LFS2_ERR_OK) {
        return PBIO_ERROR_FAILED;
    }

    mounted = true;
    return PBIO_SUCCESS;
}

/**
 * Unmounts the file system.
 */
void pbio_flash_fs_unmount(void) {
    if (mounted) {
        lfs2_unmount(&lfs);
        mounted = false;
    }
}

/**
 * Lists the entries in a directory.
 *
 * @param [in]  path        Directory to list, such as "/".
 * @param [in]  callback    Called for each entry.
 * @param [in]  context     Passed to @p callback.
 * @return                  ::PBIO_ERROR_INVALID_OP if not mounted,
 *                          ::PBIO_ERROR_FAILED if the directory can't be
 *                          read, otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_list(const char *path, pbio_flash_fs_list_callback_t callback, void *context) {

    if (!mounted) {
        return PBIO_ERROR_INVALID_OP;
    }

    lfs2_dir_t dir;
    if (lfs2_dir_open(&lfs, &dir, path) != LFS2_ERR_OK) {
        return PBIO_ERROR_FAILED;
    }

    struct lfs2_info info;
    while (lfs2_dir_read(&lfs, &dir, &info) > 0) {
        // Skip the entries for this and the parent directory.
        if (info.name[0] == '.' && (info.name[1] == '\0' || (info.name[1] == '.' && info.name[2] == '\0'))) {
            continue;
        }
        callback(info.name, info.size, info.type == LFS2_TYPE_DIR, context);
    }

    lfs2_dir_close(&lfs, &dir);
    return PBIO_SUCCESS;
}

/**
 * Gets how much of the file system is in use.
 *
 * @param [out] used_bytes  Bytes in use, rounded up to whole blocks.
 * @param [out] total_bytes Size of the file system.
 * @return                  ::PBIO_ERROR_INVALID_OP if not mounted,
 *                          ::PBIO_ERROR_FAILED if it can't be read,
 *                          otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_get_usage(uint32_t *used_bytes, uint32_t *total_bytes) {

    if (!mounted) {
        return PBIO_ERROR_INVALID_OP;
    }

    lfs2_ssize_t used_blocks = lfs2_fs_size(&lfs);
    if (used_blocks < 0) {
        return PBIO_ERROR_FAILED;
    }

    *used_bytes = (uint32_t)used_blocks * FS_BLOCK_SIZE;
    *total_bytes = (uint32_t)FS_BLOCK_COUNT * FS_BLOCK_SIZE;
    return PBIO_SUCCESS;
}

#endif // PBIO_CONFIG_FLASH_FS
