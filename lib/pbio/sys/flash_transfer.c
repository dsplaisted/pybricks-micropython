// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The Pybricks Authors

// Answers requests from the computer to list the files on the external flash
// and to read them.
//
// The command that arrives can only answer with an error code, so the data is
// sent back as events instead. Those are sent from a process here, because
// sending has to wait for the connection and a command must not.

#include <pbio/config.h>

#if PBIO_CONFIG_FLASH_FS

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pbio/error.h>
#include <pbio/flash_fs.h>
#include <pbio/os.h>
#include <pbio/protocol.h>
#include <pbio/util.h>

#include <pbdrv/bluetooth.h>

#include <pbsys/host.h>

#include "./flash_transfer.h"

// Notifications hold very little, so answers are sent as a stream of small
// pieces that the computer puts back together.
#define DATA_SIZE (PBDRV_BLUETOOTH_MAX_CHAR_SIZE - 2)

// What fits right now, which is less on a connection that agreed to less.
static uint32_t pbsys_flash_transfer_data_size(void) {
    uint32_t size = pbdrv_bluetooth_get_max_message_size();
    // One byte for the event, one for the kind of answer.
    size = size > 2 ? size - 2 : 1;
    return size < DATA_SIZE ? size : DATA_SIZE;
}

// Room for one line of a listing or one piece of a file.
#define WORK_SIZE (96)

static struct {
    /** Whether a request is waiting to be answered. */
    bool pending;
    /** What was asked for. */
    uint8_t operation;
    char path[64];
    uint32_t offset;
    uint32_t size;
} request;

static uint8_t payload[DATA_SIZE + 1];
static uint8_t work[WORK_SIZE];

static pbio_os_process_t pbsys_flash_transfer_process;

/**
 * Accepts a request from the computer, to be answered by the process.
 *
 * @param [in]  operation   ::PBSYS_FLASH_TRANSFER_LIST or
 *                          ::PBSYS_FLASH_TRANSFER_READ.
 * @param [in]  data        The rest of the command.
 * @param [in]  size        Size of @p data.
 * @return                  Error to answer the command with.
 */
pbio_pybricks_error_t pbsys_flash_transfer_request(uint8_t operation, const uint8_t *data, uint32_t size) {

    if (request.pending) {
        return PBIO_PYBRICKS_ERROR_BUSY;
    }

    uint32_t path_offset = operation == PBSYS_FLASH_TRANSFER_READ ? 6 : 0;
    if (size < path_offset) {
        return PBIO_PYBRICKS_ERROR_VALUE_NOT_ALLOWED;
    }

    if (operation == PBSYS_FLASH_TRANSFER_READ) {
        request.offset = pbio_get_uint32_le(&data[0]);
        request.size = pbio_get_uint16_le(&data[4]);
        if (request.size > WORK_SIZE) {
            request.size = WORK_SIZE;
        }
    }

    // The name is not null terminated on the wire.
    uint32_t path_size = size - path_offset;
    if (path_size == 0 || path_size >= sizeof(request.path)) {
        return PBIO_PYBRICKS_ERROR_VALUE_NOT_ALLOWED;
    }
    memcpy(request.path, &data[path_offset], path_size);
    request.path[path_size] = 0;

    request.operation = operation;
    request.pending = true;
    pbio_os_request_poll();

    return PBIO_PYBRICKS_ERROR_OK;
}

/**
 * Sends one notification, waiting if the connection is busy with something
 * else, such as a status report.
 */
static pbio_error_t pbsys_flash_transfer_send(pbio_os_state_t *state, uint32_t size) {

    static pbio_os_state_t sub;
    static pbio_os_timer_t timer;
    static uint32_t tries;
    pbio_error_t err;

    PBIO_OS_ASYNC_BEGIN(state);

    for (tries = 0; tries < 100; tries++) {
        PBIO_OS_AWAIT(state, &sub, err = pbsys_host_send_event(&sub,
            PBIO_PYBRICKS_EVENT_FLASH_FILE_DATA, payload, size));

        if (err != PBIO_ERROR_BUSY) {
            return err;
        }

        // Something else is being sent, so try again shortly.
        PBIO_OS_AWAIT_MS(state, &timer, 5);
    }

    return PBIO_ERROR_BUSY;

    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

/**
 * Sends part of the answer, in pieces small enough for a notification.
 */
static pbio_error_t pbsys_flash_transfer_send_data(pbio_os_state_t *state, uint32_t size) {

    static pbio_os_state_t sub;
    static uint32_t sent;
    static uint32_t size_now;
    pbio_error_t err;

    PBIO_OS_ASYNC_BEGIN(state);

    for (sent = 0; sent < size; sent += size_now) {
        size_now = pbsys_flash_transfer_data_size();
        if (size - sent < size_now) {
            size_now = size - sent;
        }

        payload[0] = PBSYS_FLASH_TRANSFER_REPLY_DATA;
        memcpy(&payload[1], &work[sent], size_now);

        PBIO_OS_AWAIT(state, &sub, err = pbsys_flash_transfer_send(&sub, size_now + 1));
        if (err != PBIO_SUCCESS) {
            return err;
        }
    }

    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
}

/**
 * Writes one entry of a listing into the work buffer, as a line of text:
 * the kind, the size and the name.
 *
 * @return  Length of the line.
 */
static uint32_t pbsys_flash_transfer_format_entry(const char *name, uint32_t size, bool is_dir) {

    uint32_t length = 0;
    work[length++] = is_dir ? 'd' : 'f';
    work[length++] = ' ';

    // The size in decimal, without needing string formatting.
    char digits[10];
    uint32_t count = 0;
    do {
        digits[count++] = '0' + size % 10;
        size /= 10;
    } while (size > 0);
    while (count > 0) {
        work[length++] = digits[--count];
    }

    work[length++] = ' ';
    while (*name && length < WORK_SIZE - 1) {
        work[length++] = *name++;
    }
    work[length++] = '\n';

    return length;
}

static pbio_error_t pbsys_flash_transfer_process_thread(pbio_os_state_t *state, void *context) {

    static pbio_os_state_t sub;
    static uint32_t payload_size;
    static bool done;
    static uint32_t size_read;
    static uint32_t entry_size;
    static bool is_dir;
    static pbio_error_t err;
    // Which step failed, to help find out why.
    static uint8_t stage;
    static char name[64];

    PBIO_OS_ASYNC_BEGIN(state);

    for (;;) {

        PBIO_OS_AWAIT_UNTIL(state, request.pending);

        if (request.operation == PBSYS_FLASH_TRANSFER_LIST) {

            stage = 1;
            err = pbio_flash_fs_list_open(request.path);

            while (err == PBIO_SUCCESS) {

                stage = 2;
                err = pbio_flash_fs_list_next(name, sizeof(name), &entry_size, &is_dir, &done);
                if (err != PBIO_SUCCESS || done) {
                    break;
                }

                payload_size = pbsys_flash_transfer_format_entry(name, entry_size, is_dir);

                stage = 3;
                PBIO_OS_AWAIT(state, &sub, err = pbsys_flash_transfer_send_data(&sub, payload_size));
            }

            pbio_flash_fs_list_close();

        } else {

            stage = 4;
            err = pbio_flash_fs_mount();
            if (err == PBIO_SUCCESS) {
                err = pbio_flash_fs_read_file(request.path, request.offset, work, request.size, &size_read);
                pbio_flash_fs_unmount();
            }

            if (err == PBIO_SUCCESS) {
                stage = 5;
                PBIO_OS_AWAIT(state, &sub, err = pbsys_flash_transfer_send_data(&sub, size_read));
            }
        }

        // Tell the computer that the answer is complete, or that it failed.
        payload[0] = err == PBIO_SUCCESS ?
            PBSYS_FLASH_TRANSFER_REPLY_END : PBSYS_FLASH_TRANSFER_REPLY_ERROR;
        payload[1] = stage;
        payload[2] = (uint8_t)err;
        PBIO_OS_AWAIT(state, &sub, pbsys_flash_transfer_send(&sub, 3));

        request.pending = false;
    }

    // Unreachable.
    PBIO_OS_ASYNC_END(PBIO_ERROR_FAILED);
}

void pbsys_flash_transfer_init(void) {
    pbio_os_process_start(&pbsys_flash_transfer_process, pbsys_flash_transfer_process_thread, NULL);
}

#endif // PBIO_CONFIG_FLASH_FS
