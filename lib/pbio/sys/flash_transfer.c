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

#include <pbsys/host.h>

#include "./flash_transfer.h"

// Enough for a name of an entry or a piece of a file, within the size that
// can be sent in one event on any transport.
#define PAYLOAD_SIZE (128)

static struct {
    /** Whether a request is waiting to be answered. */
    bool pending;
    /** What was asked for. */
    uint8_t operation;
    char path[64];
    uint32_t offset;
    uint32_t size;
} request;

static uint8_t payload[PAYLOAD_SIZE + 6];

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
        if (request.size > PAYLOAD_SIZE) {
            request.size = PAYLOAD_SIZE;
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
 * Sends one answer, waiting if the connection is busy with something else,
 * such as a status report.
 */
static pbio_error_t pbsys_flash_transfer_send(pbio_os_state_t *state, const uint8_t *data, uint32_t size) {

    static pbio_os_state_t sub;
    static pbio_os_timer_t timer;
    static uint32_t tries;
    pbio_error_t err;

    PBIO_OS_ASYNC_BEGIN(state);

    for (tries = 0; tries < 100; tries++) {
        PBIO_OS_AWAIT(state, &sub, err = pbsys_host_send_event(&sub,
            PBIO_PYBRICKS_EVENT_FLASH_FILE_DATA, data, size));

        if (err != PBIO_ERROR_BUSY) {
            return err;
        }

        // Something else is being sent, so try again shortly.
        PBIO_OS_AWAIT_MS(state, &timer, 5);
    }

    return PBIO_ERROR_BUSY;

    PBIO_OS_ASYNC_END(PBIO_SUCCESS);
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

    PBIO_OS_ASYNC_BEGIN(state);

    for (;;) {

        PBIO_OS_AWAIT_UNTIL(state, request.pending);

        if (request.operation == PBSYS_FLASH_TRANSFER_LIST) {

            stage = 1;
            err = pbio_flash_fs_list_open(request.path);

            while (err == PBIO_SUCCESS) {

                stage = 2;
                err = pbio_flash_fs_list_next(&((char *)payload)[6], PAYLOAD_SIZE, &entry_size, &is_dir, &done);
                if (err != PBIO_SUCCESS || done) {
                    break;
                }

                payload[0] = PBSYS_FLASH_TRANSFER_REPLY_ENTRY;
                payload[1] = is_dir;
                pbio_set_uint32_le(&payload[2], entry_size);
                payload_size = 6 + strlen(&((char *)payload)[6]);

                stage = 3;
                PBIO_OS_AWAIT(state, &sub, err = pbsys_flash_transfer_send(&sub, payload, payload_size));
            }

            pbio_flash_fs_list_close();

        } else {

            stage = 4;
            err = pbio_flash_fs_mount();
            if (err == PBIO_SUCCESS) {
                err = pbio_flash_fs_read_file(request.path, request.offset, &payload[2], request.size, &size_read);
                pbio_flash_fs_unmount();
            }

            if (err == PBIO_SUCCESS) {
                payload[0] = PBSYS_FLASH_TRANSFER_REPLY_DATA;
                payload[1] = 0;
                payload_size = 2 + size_read;
                PBIO_OS_AWAIT(state, &sub, err = pbsys_flash_transfer_send(&sub, payload, payload_size));
            }
        }

        // Tell the computer that the answer is complete, or that it failed.
        payload[0] = err == PBIO_SUCCESS ?
            PBSYS_FLASH_TRANSFER_REPLY_END : PBSYS_FLASH_TRANSFER_REPLY_ERROR;
        payload[1] = stage;
        payload[2] = (uint8_t)err;
        PBIO_OS_AWAIT(state, &sub, pbsys_flash_transfer_send(&sub, payload, 3));

        request.pending = false;
    }

    // Unreachable.
    PBIO_OS_ASYNC_END(PBIO_ERROR_FAILED);
}

void pbsys_flash_transfer_init(void) {
    pbio_os_process_start(&pbsys_flash_transfer_process, pbsys_flash_transfer_process_thread, NULL);
}

#endif // PBIO_CONFIG_FLASH_FS
