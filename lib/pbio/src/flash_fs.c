// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

// Reads the littlefs file system of the firmware of LEGO on the external
// flash. Sharing that file system means songs and their data can live on the
// same hub, instead of one overwriting the other.
//
// Experimental. Writing is limited to the file system area by the block
// device driver, so the storage of the system and the area used by firmware
// updates cannot be touched from here.

#include <pbio/config.h>

#if PBIO_CONFIG_FLASH_FS

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pbdrv/block_device.h>
#include <pbdrv/watchdog.h>

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
static uint8_t file_buffer[FS_CACHE_SIZE];

static lfs2_t lfs;
static uint32_t mount_count;

// One file can be kept open for reading a song while it plays.
static struct lfs2_file_config stream_config = { .buffer = file_buffer };
static lfs2_file_t stream_file;
static bool stream_open;

// One directory can be open for listing, which is done a piece at a time so
// that each entry can be sent to the computer before reading the next.
static lfs2_dir_t list_dir;
static bool list_open;

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
        // The read completes on interrupts, so keep driving it. Background
        // processes don't run while waiting, so feed the watchdog here.
        pbdrv_watchdog_update();
    }

    return err == PBIO_SUCCESS ? LFS2_ERR_OK : LFS2_ERR_IO;
}

static int flash_fs_prog(const struct lfs2_config *c, lfs2_block_t block, lfs2_off_t off, const void *buffer, lfs2_size_t size) {

    uint32_t address = FS_START_ADDRESS + block * c->block_size + off;

    pbio_os_state_t state = 0;
    pbio_error_t err;
    while ((err = pbdrv_block_device_write_raw(&state, address, buffer, size)) == PBIO_ERROR_AGAIN) {
        pbdrv_watchdog_update();
    }

    return err == PBIO_SUCCESS ? LFS2_ERR_OK : LFS2_ERR_IO;
}

static int flash_fs_erase(const struct lfs2_config *c, lfs2_block_t block) {

    uint32_t address = FS_START_ADDRESS + block * c->block_size;

    pbio_os_state_t state = 0;
    pbio_error_t err;
    while ((err = pbdrv_block_device_erase_raw(&state, address)) == PBIO_ERROR_AGAIN) {
        // Erasing takes tens of milliseconds each, and writing a song erases
        // many blocks, which is far longer than the watchdog allows.
        pbdrv_watchdog_update();
    }

    return err == PBIO_SUCCESS ? LFS2_ERR_OK : LFS2_ERR_IO;
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

    if (mount_count > 0) {
        mount_count++;
        return PBIO_SUCCESS;
    }

    if (lfs2_mount(&lfs, &config) != LFS2_ERR_OK) {
        return PBIO_ERROR_FAILED;
    }

    mount_count = 1;
    return PBIO_SUCCESS;
}

/**
 * Unmounts the file system.
 */
void pbio_flash_fs_unmount(void) {
    if (mount_count > 1) {
        mount_count--;
        return;
    }
    if (mount_count == 1) {
        lfs2_unmount(&lfs);
        mount_count = 0;
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

    if (mount_count == 0) {
        return PBIO_ERROR_INVALID_OP;
    }

    lfs2_dir_t dir;
    int result = lfs2_dir_open(&lfs, &dir, path);
    if (result != LFS2_ERR_OK) {
        return result == LFS2_ERR_NOENT ? PBIO_ERROR_NO_DEV : PBIO_ERROR_FAILED;
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
 * Opens a directory for listing it one entry at a time.
 *
 * @param [in]  path    Directory to list, such as "/".
 * @return              ::PBIO_ERROR_FAILED if it can't be opened, otherwise
 *                      ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_list_open(const char *path) {

    pbio_flash_fs_list_close();

    pbio_error_t err = pbio_flash_fs_mount();
    if (err != PBIO_SUCCESS) {
        return err;
    }

    int result = lfs2_dir_open(&lfs, &list_dir, path);
    if (result != LFS2_ERR_OK) {
        pbio_flash_fs_unmount();
        return result == LFS2_ERR_NOENT ? PBIO_ERROR_NO_DEV : PBIO_ERROR_FAILED;
    }

    list_open = true;
    return PBIO_SUCCESS;
}

/**
 * Gets the next entry of the directory that is open for listing.
 *
 * @param [out] name        Name of the entry.
 * @param [in]  name_size   Size of @p name.
 * @param [out] size        Size of the entry in bytes.
 * @param [out] is_dir      Whether the entry is a directory.
 * @param [out] done        Whether the end of the directory was reached, in
 *                          which case the other values are not set.
 * @return                  ::PBIO_ERROR_INVALID_OP if no directory is open,
 *                          ::PBIO_ERROR_FAILED on a read error, otherwise
 *                          ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_list_next(char *name, uint32_t name_size, uint32_t *size, bool *is_dir, bool *done) {

    if (!list_open) {
        return PBIO_ERROR_INVALID_OP;
    }

    struct lfs2_info info;
    for (;;) {
        int result = lfs2_dir_read(&lfs, &list_dir, &info);
        if (result < 0) {
            return PBIO_ERROR_FAILED;
        }
        if (result == 0) {
            *done = true;
            return PBIO_SUCCESS;
        }
        // Skip the entries for this and the parent directory.
        if (info.name[0] == '.' && (info.name[1] == 0 || (info.name[1] == '.' && info.name[2] == 0))) {
            continue;
        }
        break;
    }

    strncpy(name, info.name, name_size - 1);
    name[name_size - 1] = 0;
    *size = info.size;
    *is_dir = info.type == LFS2_TYPE_DIR;
    *done = false;
    return PBIO_SUCCESS;
}

/**
 * Closes the directory that was open for listing.
 */
void pbio_flash_fs_list_close(void) {
    if (list_open) {
        lfs2_dir_close(&lfs, &list_dir);
        list_open = false;
        pbio_flash_fs_unmount();
    }
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

    if (mount_count == 0) {
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

/**
 * Opens a file to write it a piece at a time, replacing it if it exists.
 *
 * @param [in]  path    File to write.
 * @return              ::PBIO_ERROR_FAILED if it can't be opened, otherwise
 *                      ::PBIO_SUCCESS.
 */
/**
 * Creates the directories that a path leads through, so that a file can be
 * written into a folder that is not there yet. The file system must be
 * mounted.
 *
 * @param [in]  path    The path of the file.
 * @return              ::PBIO_ERROR_INVALID_ARG if the path is too long,
 *                      ::PBIO_ERROR_FAILED if a directory can't be made,
 *                      otherwise ::PBIO_SUCCESS.
 */
static pbio_error_t pbio_flash_fs_make_parents(const char *path) {

    char dir[PBIO_FLASH_FS_PATH_SIZE];

    if (strlen(path) >= sizeof(dir)) {
        return PBIO_ERROR_INVALID_ARG;
    }

    for (uint32_t i = 0; path[i]; i++) {

        // The leading slash is the root, which is always there.
        if (path[i] != '/' || i == 0) {
            continue;
        }

        memcpy(dir, path, i);
        dir[i] = '\0';

        int result = lfs2_mkdir(&lfs, dir);
        if (result != LFS2_ERR_OK && result != LFS2_ERR_EXIST) {
            return PBIO_ERROR_FAILED;
        }
    }

    return PBIO_SUCCESS;
}

pbio_error_t pbio_flash_fs_write_open(const char *path) {

    pbio_flash_fs_stream_close();

    pbio_error_t err = pbio_flash_fs_mount();
    if (err != PBIO_SUCCESS) {
        return err;
    }

    err = pbio_flash_fs_make_parents(path);
    if (err != PBIO_SUCCESS) {
        pbio_flash_fs_unmount();
        return err;
    }

    if (lfs2_file_opencfg(&lfs, &stream_file, path,
        LFS2_O_WRONLY | LFS2_O_CREAT | LFS2_O_TRUNC, &stream_config) != LFS2_ERR_OK) {
        pbio_flash_fs_unmount();
        return PBIO_ERROR_FAILED;
    }

    stream_open = true;
    return PBIO_SUCCESS;
}

/**
 * Adds data to the end of the file that is open for writing.
 *
 * @param [in]  data    The data to add.
 * @param [in]  size    Number of bytes to add.
 * @return              ::PBIO_ERROR_INVALID_OP if no file is open,
 *                      ::PBIO_ERROR_FAILED if it can't be written, otherwise
 *                      ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_write_append(const uint8_t *data, uint32_t size) {

    if (!stream_open) {
        return PBIO_ERROR_INVALID_OP;
    }

    return lfs2_file_write(&lfs, &stream_file, data, size) == (lfs2_ssize_t)size ?
           PBIO_SUCCESS : PBIO_ERROR_FAILED;
}

/**
 * Opens a file to read it a piece at a time, such as while playing a song.
 *
 * Only one file can be open this way, and the file system stays mounted
 * until it is closed.
 *
 * @param [in]  path    File to open.
 * @param [out] size    Size of the file in bytes.
 * @return              ::PBIO_ERROR_FAILED if it can't be opened, otherwise
 *                      ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_stream_open(const char *path, uint32_t *size) {

    pbio_flash_fs_stream_close();

    pbio_error_t err = pbio_flash_fs_mount();
    if (err != PBIO_SUCCESS) {
        return err;
    }

    if (lfs2_file_opencfg(&lfs, &stream_file, path, LFS2_O_RDONLY, &stream_config) != LFS2_ERR_OK) {
        pbio_flash_fs_unmount();
        return PBIO_ERROR_FAILED;
    }

    stream_open = true;
    *size = lfs2_file_size(&lfs, &stream_file);
    return PBIO_SUCCESS;
}

/**
 * Reads the next piece of the open file.
 *
 * @param [out] buffer      Buffer to receive the data.
 * @param [in]  size        Number of bytes to read.
 * @param [out] size_read   Number of bytes actually read, which is less than
 *                          @p size at the end of the file.
 * @return                  ::PBIO_ERROR_INVALID_OP if no file is open,
 *                          ::PBIO_ERROR_FAILED on a read error, otherwise
 *                          ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_stream_read(uint8_t *buffer, uint32_t size, uint32_t *size_read) {

    if (!stream_open) {
        return PBIO_ERROR_INVALID_OP;
    }

    lfs2_ssize_t result = lfs2_file_read(&lfs, &stream_file, buffer, size);
    if (result < 0) {
        return PBIO_ERROR_FAILED;
    }

    *size_read = result;
    return PBIO_SUCCESS;
}

/**
 * Starts reading the open file from the beginning again.
 *
 * @return  ::PBIO_ERROR_INVALID_OP if no file is open, ::PBIO_ERROR_FAILED
 *          if it can't be rewound, otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_stream_rewind(void) {

    if (!stream_open) {
        return PBIO_ERROR_INVALID_OP;
    }

    return lfs2_file_seek(&lfs, &stream_file, 0, LFS2_SEEK_SET) < 0 ? PBIO_ERROR_FAILED : PBIO_SUCCESS;
}

/**
 * Closes the file that was opened for reading a piece at a time.
 */
void pbio_flash_fs_stream_close(void) {
    if (stream_open) {
        lfs2_file_close(&lfs, &stream_file);
        stream_open = false;
        pbio_flash_fs_unmount();
    }
}

/**
 * Reads part of a file.
 *
 * @param [in]  path        File to read.
 * @param [in]  offset      Where to start reading.
 * @param [out] buffer      Buffer to receive the data.
 * @param [in]  size        Number of bytes to read.
 * @param [out] size_read   Number of bytes actually read, which is less than
 *                          @p size at the end of the file.
 * @return                  ::PBIO_ERROR_INVALID_OP if not mounted,
 *                          ::PBIO_ERROR_FAILED if the file can't be read,
 *                          otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_read_file(const char *path, uint32_t offset, uint8_t *buffer, uint32_t size, uint32_t *size_read) {

    if (mount_count == 0) {
        return PBIO_ERROR_INVALID_OP;
    }

    struct lfs2_file_config file_config = { .buffer = file_buffer };
    lfs2_file_t file;

    if (lfs2_file_opencfg(&lfs, &file, path, LFS2_O_RDONLY, &file_config) != LFS2_ERR_OK) {
        return PBIO_ERROR_FAILED;
    }

    pbio_error_t err = PBIO_SUCCESS;
    if (lfs2_file_seek(&lfs, &file, offset, LFS2_SEEK_SET) < 0) {
        err = PBIO_ERROR_FAILED;
    } else {
        lfs2_ssize_t result = lfs2_file_read(&lfs, &file, buffer, size);
        if (result < 0) {
            err = PBIO_ERROR_FAILED;
        } else {
            *size_read = result;
        }
    }

    lfs2_file_close(&lfs, &file);
    return err;
}

/**
 * Writes a whole file, replacing it if it exists.
 *
 * This erases and programs the flash, so it takes a while and must not be
 * done while sound is playing from the same chip.
 *
 * @param [in]  path        File to write.
 * @param [in]  data        The data to write.
 * @param [in]  size        Number of bytes to write.
 * @return                  ::PBIO_ERROR_INVALID_OP if not mounted,
 *                          ::PBIO_ERROR_FAILED if it can't be written,
 *                          otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_write_file(const char *path, const uint8_t *data, uint32_t size) {

    if (mount_count == 0) {
        return PBIO_ERROR_INVALID_OP;
    }

    pbio_error_t parents = pbio_flash_fs_make_parents(path);
    if (parents != PBIO_SUCCESS) {
        return parents;
    }

    struct lfs2_file_config file_config = { .buffer = file_buffer };
    lfs2_file_t file;

    if (lfs2_file_opencfg(&lfs, &file, path, LFS2_O_WRONLY | LFS2_O_CREAT | LFS2_O_TRUNC, &file_config) != LFS2_ERR_OK) {
        return PBIO_ERROR_FAILED;
    }

    pbio_error_t err = PBIO_SUCCESS;
    if (lfs2_file_write(&lfs, &file, data, size) != (lfs2_ssize_t)size) {
        err = PBIO_ERROR_FAILED;
    }

    if (lfs2_file_close(&lfs, &file) != LFS2_ERR_OK) {
        err = PBIO_ERROR_FAILED;
    }

    return err;
}

/**
 * Deletes a file or an empty directory.
 *
 * @param [in]  path        What to delete.
 * @return                  ::PBIO_ERROR_INVALID_OP if not mounted,
 *                          ::PBIO_ERROR_FAILED if it can't be deleted,
 *                          otherwise ::PBIO_SUCCESS.
 */
pbio_error_t pbio_flash_fs_remove(const char *path) {

    if (mount_count == 0) {
        return PBIO_ERROR_INVALID_OP;
    }

    return lfs2_remove(&lfs, path) == LFS2_ERR_OK ? PBIO_SUCCESS : PBIO_ERROR_FAILED;
}

#endif // PBIO_CONFIG_FLASH_FS
