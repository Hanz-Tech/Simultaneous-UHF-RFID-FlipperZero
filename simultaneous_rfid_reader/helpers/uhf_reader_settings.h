#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <storage/storage.h>

#define UHF_READER_MIN_POWER_DBM 15U
#define UHF_READER_MAX_POWER_DBM 26U

typedef struct {
    bool reader_profile_initialized;
    uint8_t save_on_write_index;
    uint8_t region_index;
    uint8_t power_dbm;
    uint8_t session_index;
    uint8_t target_index;
    uint32_t default_access_password;
} UHFReaderSettings;

void uhf_reader_settings_set_defaults(UHFReaderSettings* settings);
bool uhf_reader_settings_load(Storage* storage, UHFReaderSettings* settings);
bool uhf_reader_settings_save(Storage* storage, const UHFReaderSettings* settings);
