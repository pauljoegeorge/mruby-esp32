#include <stdio.h>
#include <dirent.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"

#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/compile.h"
#include "mruby/error.h"
#include "mruby/string.h"
#include "mruby/dump.h"

#define TAG "mruby_task"

typedef mrb_value (*mrb_load_func)(mrb_state*, FILE*, mrbc_context*);

void load_rb_file(mrb_state *mrb, mrbc_context *context, const char *path) {
    mrb_load_func load = mrb_load_detect_file_cxt;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        ESP_LOGW(TAG, "File not found: %s", path);
        return;
    }

    load(mrb, fp, context);

    if (mrb->exc) {
        ESP_LOGE(TAG, "Exception occurred in %s", path);
        mrb_print_error(mrb);
        mrb->exc = 0;
    } else {
        ESP_LOGI(TAG, "Loaded: %s", path);
    }
    fclose(fp);
}

void load_mrblib(mrb_state *mrb, mrbc_context *context) {
    const char *dir_path = "/storage/mrblib";
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".rb") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            load_rb_file(mrb, context, full_path);
        }
    }
    closedir(dir);
}

void load_all_rb_files(mrb_state *mrb, mrbc_context *context) {
    const char *dir_path = "/storage";
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /storage");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".rb") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            load_rb_file(mrb, context, full_path);
        }
    }

    closedir(dir);
}

void mruby_task(void *pvParameter)
{
    mrb_state *mrb = mrb_open();
    mrbc_context *context = mrbc_context_new(mrb);
    int ai = mrb_gc_arena_save(mrb);
    ESP_LOGI(TAG, "Loading Ruby scripts...");

    // Load all .rb files first
    load_mrblib(mrb, context);

    // Then load main.rb (if it exists)
    load_rb_file(mrb, context, "/storage/main.rb");

    mrb_gc_arena_restore(mrb, ai);
    mrbc_context_free(mrb, context);
    mrb_close(mrb);

    // Task should never end
    while (1) {
        vTaskDelay(1);
    }
}

void app_main()
{
    nvs_flash_init();

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .format_if_mount_failed = false,
    };
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

    xTaskCreate(&mruby_task, "mruby_task", 16384, NULL, 5, NULL);
}
