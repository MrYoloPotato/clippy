#include "../clippy_app_i.h"
#include "../views/clippy_mass_storage_view.h"
#include "../helpers/mass_storage_usb.h"
#include "core/string.h"
#include "m-string.h"
#include <lib/toolbox/path.h>

#define TAG "ClippySceneMassStorageWork"

static bool file_read(
    void* ctx,
    uint32_t lba,
    uint16_t count,
    uint8_t* out,
    uint32_t* out_len,
    uint32_t out_cap) {
    ClippyApp* app = ctx;
    FURI_LOG_T(TAG, "file_read lba=%08lX count=%04X out_cap=%08lX", lba, count, out_cap);
    if(!storage_file_seek(app->file, lba * SCSI_BLOCK_SIZE, true)) {
        FURI_LOG_W(TAG, "seek failed");
        return false;
    }
    uint16_t clamp = MIN(out_cap, count * SCSI_BLOCK_SIZE);
    *out_len = storage_file_read(app->file, out, clamp);
    FURI_LOG_T(TAG, "%lu/%lu", *out_len, count * SCSI_BLOCK_SIZE);
    app->bytes_read += *out_len;
    return *out_len == clamp;
}

static bool file_write(void* ctx, uint32_t lba, uint16_t count, uint8_t* buf, uint32_t len) {
    ClippyApp* app = ctx;
    FURI_LOG_T(TAG, "file_write lba=%08lX count=%04X len=%08lX", lba, count, len);
    if(len != count * SCSI_BLOCK_SIZE) {
        FURI_LOG_W(TAG, "bad write params count=%u len=%lu", count, len);
        return false;
    }
    if(!storage_file_seek(app->file, lba * SCSI_BLOCK_SIZE, true)) {
        FURI_LOG_W(TAG, "seek failed");
        return false;
    }
    app->bytes_written += len;
    return storage_file_write(app->file, buf, len) == len;
}

static uint32_t file_num_blocks(void* ctx) {
    ClippyApp* app = ctx;
    return storage_file_size(app->file) / SCSI_BLOCK_SIZE;
}

static void file_eject(void* ctx) {
    ClippyApp* app = ctx;
    FURI_LOG_D(TAG, "EJECT");
    view_dispatcher_send_custom_event(app->view_dispatcher, ClippyMassStorageCustomEventEject);
}

bool clippy_scene_mass_storage_work_on_event(void* context, SceneManagerEvent event) {
    ClippyApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ClippyMassStorageCustomEventEject) {
            consumed = scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, ClippySceneCopyPasteSel);
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        clippy_mass_storage_set_stats(app->mass_storage_view, app->bytes_read, app->bytes_written);
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, ClippySceneCopyPasteSel);
    }
    return consumed;
}

void clippy_scene_mass_storage_work_on_enter(void* context) {
    ClippyApp* app = context;
    app->bytes_read = app->bytes_written = 0;

    if(!storage_file_exists(app->fs_api, furi_string_get_cstr(app->fat_image_file_path))) {
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, ClippySceneCopyPasteSel);
        return;
    }

    clippy_app_show_loading_popup(app, true);

    app->usb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    FuriString* file_name = furi_string_alloc();
    path_extract_filename(app->fat_image_file_path, file_name, true);

    clippy_mass_storage_set_file_name(app->mass_storage_view, file_name);

    app->file = storage_file_alloc(app->fs_api);
    furi_assert(storage_file_open(
        app->file,
        furi_string_get_cstr(app->fat_image_file_path),
        FSAM_READ | FSAM_WRITE,
        FSOM_OPEN_EXISTING));

    SCSIDeviceFunc fn = {
        .ctx = app,
        .read = file_read,
        .write = file_write,
        .num_blocks = file_num_blocks,
        .eject = file_eject,
    };

    app->usb = mass_storage_usb_start(furi_string_get_cstr(file_name), fn);

    furi_string_free(file_name);

    clippy_app_show_loading_popup(app, false);
    view_dispatcher_switch_to_view(app->view_dispatcher, ClippyAppViewMassStorageWork);
}

void clippy_scene_mass_storage_work_on_exit(void* context) {
    ClippyApp* app = context;
    clippy_app_show_loading_popup(app, true);

    if(app->usb_mutex) {
        furi_mutex_free(app->usb_mutex);
        app->usb_mutex = NULL;
    }
    if(app->usb) {
        mass_storage_usb_stop(app->usb);
        app->usb = NULL;
    }
    if(app->file) {
        storage_file_free(app->file);
        app->file = NULL;
    }
    clippy_app_show_loading_popup(app, false);
}
