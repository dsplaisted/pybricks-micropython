// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2022 The Pybricks Authors

#include "py/mpconfig.h"

#if PYBRICKS_PY_COMMON && PYBRICKS_PY_COMMON_SYSTEM

#include <string.h>

#include <pbdrv/block_device.h>
#include <pbdrv/clock.h>
#include <pbio/flash_fs.h>
#include <pbio/os.h>
#include <pbsys/host.h>
#include <pbdrv/bluetooth.h>
#include <pbdrv/reset.h>
#include <pbsys/main.h>
#include <pbsys/program_stop.h>
#include <pbsys/status.h>
#include <pbsys/storage.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include <pybricks/common.h>
#include <pybricks/parameters.h>
#include <pybricks/util_pb/pb_error.h>
#include <pybricks/util_mp/pb_kwarg_helper.h>
#include <pybricks/util_mp/pb_obj_helper.h>

static mp_obj_t pb_type_System_name(void) {
    const char *hub_name = pbdrv_bluetooth_get_hub_name();
    return mp_obj_new_str(hub_name, strlen(hub_name));
}
static MP_DEFINE_CONST_FUN_OBJ_0(pb_type_System_name_obj, pb_type_System_name);

#if PBDRV_CONFIG_RESET

static mp_obj_t pb_type_System_reset_reason(void) {
    pbdrv_reset_reason_t reason = pbdrv_reset_get_reason();
    return MP_OBJ_NEW_SMALL_INT(reason);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pb_type_System_reset_reason_obj, pb_type_System_reset_reason);

#endif // PBDRV_CONFIG_RESET

static mp_obj_t pb_type_System_info(void) {
    const char *hub_name = pbdrv_bluetooth_get_hub_name();

    pbio_pybricks_user_program_id_t program_id;
    pbsys_main_program_start_request_type_t program_start_type;
    pbsys_main_program_get_info(&program_id, &program_start_type);

    mp_map_elem_t info[] = {
        {MP_OBJ_NEW_QSTR(MP_QSTR_name), mp_obj_new_str(hub_name, strlen(hub_name))},
        #if PBDRV_CONFIG_RESET
        {MP_OBJ_NEW_QSTR(MP_QSTR_reset_reason), mp_obj_new_int(pbdrv_reset_get_reason())},
        #endif // PBDRV_CONFIG_RESET
        #if PBDRV_CONFIG_BLUETOOTH
        {MP_OBJ_NEW_QSTR(MP_QSTR_host_connected_ble), mp_obj_new_bool(pbsys_status_test(PBIO_PYBRICKS_STATUS_BLE_HOST_CONNECTED))},
        #endif // PBDRV_CONFIG_BLUETOOTH
        #if PBDRV_CONFIG_USB
        {MP_OBJ_NEW_QSTR(MP_QSTR_host_connected_usb), mp_obj_new_bool(pbsys_status_test(PBIO_PYBRICKS_STATUS_USB_HOST_CONNECTED))},
        #endif // PBDRV_CONFIG_USB
        {MP_OBJ_NEW_QSTR(MP_QSTR_program_id), mp_obj_new_int(program_id)},
        {MP_OBJ_NEW_QSTR(MP_QSTR_program_start_type), mp_obj_new_int(program_start_type)},
    };
    mp_obj_t info_dict = mp_obj_new_dict(MP_ARRAY_SIZE(info));

    for (size_t i = 0; i < MP_ARRAY_SIZE(info); i++) {
        mp_map_elem_t *elem = &info[i];
        mp_obj_dict_store(info_dict, elem->key, elem->value);
    }

    return info_dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pb_type_System_info_obj, pb_type_System_info);

#if PBIO_CONFIG_ENABLE_SYS

static mp_obj_t pb_type_System_set_stop_button(mp_obj_t buttons_in) {
    pbio_button_flags_t buttons = 0;

    if (mp_obj_is_true(buttons_in)) {
        #if PYBRICKS_PY_COMMON_KEYPAD_HUB_BUTTONS > 1
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            // try an iterator first in case there are multiple buttons
            mp_obj_t iter = mp_getiter(buttons_in, NULL);
            mp_obj_t item;
            while ((item = mp_iternext(iter)) != MP_OBJ_STOP_ITERATION) {
                buttons |= pb_type_button_get_button_flag(item);
            }
            nlr_pop();
        } else {
            // mp_getiter() will raise an exception if it is not an iter, so we
            // will end up here in that case, where we are expecting a single
            // button enum value. Technically there could be other error that
            // get us here, but they should be rare and will likely be a ValueError
            // which is the same error that will be raised here.
            buttons = pb_type_button_get_button_flag(buttons_in);
        }
        #else // PYBRICKS_PY_COMMON_KEYPAD_HUB_BUTTONS > 1
        buttons = PBIO_BUTTON_CENTER;
        #endif // PYBRICKS_PY_COMMON_KEYPAD_HUB_BUTTONS > 1
    }

    pbsys_program_stop_set_buttons(buttons);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_System_set_stop_button_obj, pb_type_System_set_stop_button);

static mp_obj_t pb_type_System_shutdown(void) {

    // Request shutdown. This will make the program_stop module call
    // pbsys_main_stop_program, which schedules mp_sched_vm_abort to stop
    // the MicroPython application (gracefully as far as pbsys is concerned).
    // Then, pbsys will proceed to shutdown instead of running another program.
    pbsys_status_set(PBIO_PYBRICKS_STATUS_SHUTDOWN_REQUEST);

    // Keep running MicroPython until mp_sched_vm_abort is eventually handled.
    for (;;) {
        mp_event_wait_indefinite();
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pb_type_System_shutdown_obj, pb_type_System_shutdown);

static mp_obj_t pb_type_System_storage(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_FUNCTION(n_args, pos_args, kw_args,
        PB_ARG_REQUIRED(offset),
        PB_ARG_DEFAULT_NONE(read),
        PB_ARG_DEFAULT_NONE(write));

    // Get offset and confirm integer type.
    mp_int_t offset = mp_obj_get_int(offset_in);

    // Handle read.
    if (read_in != mp_const_none && write_in == mp_const_none) {
        byte *data;
        mp_uint_t size = mp_obj_get_int(read_in);
        pb_assert(pbsys_storage_get_user_data(offset, &data, size));
        return mp_obj_new_bytes(data, size);
    }

    // Handle write.
    if (write_in != mp_const_none && read_in == mp_const_none) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(write_in, &bufinfo, MP_BUFFER_READ);

        pb_assert(pbsys_storage_set_user_data(offset, bufinfo.buf, bufinfo.len));

        return mp_const_none;
    }

    mp_raise_TypeError(MP_ERROR_TEXT("Must set either read (int) or write (bytes)."));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pb_type_System_storage_obj, 0, pb_type_System_storage);

static mp_obj_t pb_type_System_reset_storage(void) {
    pbsys_storage_reset_storage();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pb_type_System_reset_storage_obj, pb_type_System_reset_storage);

#endif // PBIO_CONFIG_ENABLE_SYS

#if PYBRICKS_PY_COMMON_SYSTEM_UMM_INFO

// Not in library header for some reason.
extern void *umm_info(void *ptr, bool force);

// Prints out umm usage similar to micropython.mem_info().
static mp_obj_t pb_type_System_umm_info(void) {
    umm_info(NULL, false);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pb_type_System_umm_info_obj, pb_type_System_umm_info);

#endif // PYBRICKS_PY_COMMON_SYSTEM_UMM_INFO

// dir(pybricks.common.System)
#if PBDRV_CONFIG_BLOCK_DEVICE_RAW_ACCESS
// Experimental: reads the external flash, including the regions used by
// other firmwares, to see what is stored there. Read only.
static mp_obj_t pb_type_System_flash_read(mp_obj_t address_in, mp_obj_t size_in) {

    uint32_t address = mp_obj_get_int(address_in);
    uint32_t size = mp_obj_get_int(size_in);

    vstr_t vstr;
    vstr_init_len(&vstr, size);

    pbio_error_t err;
    pbio_os_state_t state = 0;
    while ((err = pbdrv_block_device_read_raw(&state, address, (uint8_t *)vstr.buf, size)) == PBIO_ERROR_AGAIN) {
        pbio_os_run_processes_and_wait_for_event();
    }
    pb_assert(err);

    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_2(pb_type_System_flash_read_obj, pb_type_System_flash_read);
#endif // PBDRV_CONFIG_BLOCK_DEVICE_RAW_ACCESS

#if PBIO_CONFIG_FLASH_FS
static void pb_type_System_flash_file_callback(const char *name, uint32_t size, bool is_dir, void *context) {
    mp_obj_t entry[] = {
        mp_obj_new_str(name, strlen(name)),
        mp_obj_new_int_from_uint(size),
        mp_obj_new_bool(is_dir),
    };
    mp_obj_list_append(*(mp_obj_t *)context, mp_obj_new_tuple(3, entry));
}

// Experimental: lists the files that other firmwares keep on the external
// flash. Read only.
static mp_obj_t pb_type_System_flash_files(size_t n_args, const mp_obj_t *args) {

    const char *path = n_args > 0 ? mp_obj_str_get_str(args[0]) : "/";

    pb_assert(pbio_flash_fs_mount());

    mp_obj_t list = mp_obj_new_list(0, NULL);
    pbio_error_t err = pbio_flash_fs_list(path, pb_type_System_flash_file_callback, &list);
    pbio_flash_fs_unmount();
    pb_assert(err);

    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pb_type_System_flash_files_obj, 0, 1, pb_type_System_flash_files);

// Experimental: reads part of a file on the external flash.
static mp_obj_t pb_type_System_flash_read_file(size_t n_args, const mp_obj_t *args) {

    const char *path = mp_obj_str_get_str(args[0]);
    uint32_t offset = n_args > 1 ? mp_obj_get_int(args[1]) : 0;
    uint32_t size = n_args > 2 ? mp_obj_get_int(args[2]) : 1024;

    pb_assert(pbio_flash_fs_mount());

    vstr_t vstr;
    vstr_init_len(&vstr, size);

    uint32_t size_read = 0;
    pbio_error_t err = pbio_flash_fs_read_file(path, offset, (uint8_t *)vstr.buf, size, &size_read);
    pbio_flash_fs_unmount();
    pb_assert(err);

    vstr.len = size_read;
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pb_type_System_flash_read_file_obj, 1, 3, pb_type_System_flash_read_file);

// Experimental: writes a whole file on the external flash.
static mp_obj_t pb_type_System_flash_write_file(mp_obj_t path_in, mp_obj_t data_in) {

    const char *path = mp_obj_str_get_str(path_in);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);

    pb_assert(pbio_flash_fs_mount());
    pbio_error_t err = pbio_flash_fs_write_file(path, bufinfo.buf, bufinfo.len);
    pbio_flash_fs_unmount();
    pb_assert(err);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(pb_type_System_flash_write_file_obj, pb_type_System_flash_write_file);

// Experimental: deletes a file on the external flash.
static mp_obj_t pb_type_System_flash_remove(mp_obj_t path_in) {

    const char *path = mp_obj_str_get_str(path_in);

    pb_assert(pbio_flash_fs_mount());
    pbio_error_t err = pbio_flash_fs_remove(path);
    pbio_flash_fs_unmount();
    pb_assert(err);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pb_type_System_flash_remove_obj, pb_type_System_flash_remove);

// Experimental: receives a file over the connection with the computer and
// writes it to the external flash. Prints READY when it is waiting for data
// and K after each chunk, so that the sender knows when to continue.
static mp_obj_t pb_type_System_flash_receive_file(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    PB_PARSE_ARGS_FUNCTION(n_args, pos_args, kw_args,
        PB_ARG_REQUIRED(path),
        PB_ARG_REQUIRED(size),
        PB_ARG_DEFAULT_INT(chunk, 1024),
        PB_ARG_DEFAULT_INT(timeout, 10000));

    const char *path = mp_obj_str_get_str(path_in);
    uint32_t total = mp_obj_get_int(size_in);
    uint32_t chunk = mp_obj_get_int(chunk_in);
    uint32_t timeout = mp_obj_get_int(timeout_in);

    uint8_t *buffer = m_new(uint8_t, chunk);

    pb_assert(pbio_flash_fs_mount());

    pbio_error_t err = pbio_flash_fs_write_open(path);
    if (err != PBIO_SUCCESS) {
        pbio_flash_fs_unmount();
        pb_assert(err);
    }

    mp_printf(&mp_plat_print, "READY\n");

    uint32_t received = 0;
    while (received < total) {

        // Collect one chunk, or what is left of the file.
        uint32_t want = total - received < chunk ? total - received : chunk;
        uint32_t have = 0;
        uint32_t last_data = pbdrv_clock_get_ms();

        while (have < want) {
            uint32_t size_now = want - have;
            if (pbsys_host_stdin_read(buffer + have, &size_now) == PBIO_SUCCESS) {
                have += size_now;
                last_data = pbdrv_clock_get_ms();
                continue;
            }

            if (pbdrv_clock_get_ms() - last_data > timeout) {
                pbio_flash_fs_stream_close();
                pbio_flash_fs_unmount();
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("timed out waiting for data"));
            }

            // Let the connection and everything else run while waiting.
            pbio_os_run_processes_and_wait_for_event();
        }

        err = pbio_flash_fs_write_append(buffer, have);
        if (err != PBIO_SUCCESS) {
            pbio_flash_fs_stream_close();
            pbio_flash_fs_unmount();
            pb_assert(err);
        }

        received += have;
        mp_printf(&mp_plat_print, "K\n");
    }

    pbio_flash_fs_stream_close();
    pbio_flash_fs_unmount();

    return mp_obj_new_int_from_uint(received);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pb_type_System_flash_receive_file_obj, 0, pb_type_System_flash_receive_file);

static mp_obj_t pb_type_System_flash_usage(void) {

    pb_assert(pbio_flash_fs_mount());

    uint32_t used, total;
    pbio_error_t err = pbio_flash_fs_get_usage(&used, &total);
    pbio_flash_fs_unmount();
    pb_assert(err);

    mp_obj_t result[] = {
        mp_obj_new_int_from_uint(used),
        mp_obj_new_int_from_uint(total),
    };
    return mp_obj_new_tuple(2, result);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pb_type_System_flash_usage_obj, pb_type_System_flash_usage);
#endif // PBIO_CONFIG_FLASH_FS

static const mp_rom_map_elem_t common_System_locals_dict_table[] = {
    #if PBIO_CONFIG_FLASH_FS
    { MP_ROM_QSTR(MP_QSTR_flash_files), MP_ROM_PTR(&pb_type_System_flash_files_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_usage), MP_ROM_PTR(&pb_type_System_flash_usage_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_read_file), MP_ROM_PTR(&pb_type_System_flash_read_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_write_file), MP_ROM_PTR(&pb_type_System_flash_write_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_remove), MP_ROM_PTR(&pb_type_System_flash_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_receive_file), MP_ROM_PTR(&pb_type_System_flash_receive_file_obj) },
    #endif
    #if PBDRV_CONFIG_BLOCK_DEVICE_RAW_ACCESS
    { MP_ROM_QSTR(MP_QSTR_flash_read), MP_ROM_PTR(&pb_type_System_flash_read_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_name), MP_ROM_PTR(&pb_type_System_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&pb_type_System_info_obj) },
    #if PBDRV_CONFIG_RESET
    { MP_ROM_QSTR(MP_QSTR_reset_reason), MP_ROM_PTR(&pb_type_System_reset_reason_obj) },
    #endif // PBDRV_CONFIG_RESET
    #if PBIO_CONFIG_ENABLE_SYS
    { MP_ROM_QSTR(MP_QSTR_set_stop_button), MP_ROM_PTR(&pb_type_System_set_stop_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_storage), MP_ROM_PTR(&pb_type_System_reset_storage_obj) },
    { MP_ROM_QSTR(MP_QSTR_shutdown), MP_ROM_PTR(&pb_type_System_shutdown_obj) },
    { MP_ROM_QSTR(MP_QSTR_storage), MP_ROM_PTR(&pb_type_System_storage_obj) },
    #endif
    #if PYBRICKS_PY_COMMON_SYSTEM_UMM_INFO
    { MP_ROM_QSTR(MP_QSTR_umm_info), MP_ROM_PTR(&pb_type_System_umm_info_obj) },
    #endif
};
static MP_DEFINE_CONST_DICT(common_System_locals_dict, common_System_locals_dict_table);

// type(pybricks.common.System) but implemented as module for reduced build size.
// REVISIT: Make implementation consistent across modules/singletons: https://github.com/pybricks/support/issues/840
const mp_obj_module_t pb_type_System = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&common_System_locals_dict,
};

#endif // PYBRICKS_PY_COMMON && PYBRICKS_PY_COMMON_SYSTEM
