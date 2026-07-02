#include "yrm100x_module.h"
#include "yrm100x_worker.h"
#include "yrm100x_module_cmd.h"
#include "saved_epc_functions.h"
#include <furi.h>

static const char* UHF_MOD_TAG = "UHF_MOD";

/**
 * File that handles the YRM100 module
 * @author frux-c
 * @author modified by haffnerriley
*/

#define DELAY_MS  100
#define WAIT_TICK 4000 // max wait time in between each byte

static M100ResponseType setup_and_send_rx(M100Module* module, uint8_t* cmd, size_t cmd_length) {
    UHFUart* uart = module->uart;
    Buffer* buffer = uart->buffer;
    // clear buffer
    uhf_buffer_reset(buffer);
    // send cmd
    uhf_uart_send_wait(uart, cmd, cmd_length);
    // wait for response by polling
    int tick_count = 0;
    while(!uhf_is_buffer_closed(buffer) && !uhf_uart_tick(uart)) {
        tick_count++;
    }
    // reset tick
    uhf_uart_tick_reset(uart);
    // Validation Checks
    uint8_t* data = uhf_buffer_get_data(buffer);
    size_t length = uhf_buffer_get_size(buffer);
    // check if size > 0
    if(!length) {
        FURI_LOG_D(UHF_MOD_TAG, "Response: empty (waited %d ticks)", tick_count);
        return M100EmptyResponse;
    }
    // check if data is valid
    if(data[0] != FRAME_START || data[length - 1] != FRAME_END) {
        FURI_LOG_W(
            UHF_MOD_TAG,
            "Response: validation fail, len=%d, first=0x%02X, last=0x%02X (waited %d ticks)",
            length,
            data[0],
            data[length - 1],
            tick_count);
        return M100ValidationFail;
    }
    // check if checksum is correct
    if(checksum(data + 1, length - 3) != data[length - 2]) {
        FURI_LOG_W(
            UHF_MOD_TAG,
            "Response: checksum fail, len=%d, expected=0x%02X, got=0x%02X",
            length,
            data[length - 2],
            checksum(data + 1, length - 3));
        return M100ChecksumFail;
    }
    FURI_LOG_D(UHF_MOD_TAG, "Response: OK, len=%d", length);
    return M100SuccessResponse;
}

M100ModuleInfo* m100_module_info_alloc() {
    M100ModuleInfo* module_info = (M100ModuleInfo*)malloc(sizeof(M100ModuleInfo));
    return module_info;
}

void m100_module_info_free(M100ModuleInfo* module_info) {
    if(module_info->hw_version != NULL) free(module_info->hw_version);
    if(module_info->sw_version != NULL) free(module_info->sw_version);
    if(module_info->manufacturer != NULL) free(module_info->manufacturer);
    free(module_info);
}

M100Module* m100_module_alloc() {
    M100Module* module = (M100Module*)malloc(sizeof(M100Module));
    module->transmitting_power = DEFAULT_TRANSMITTING_POWER;
    module->region = DEFAULT_WORKING_REGION;
    module->info = m100_module_info_alloc();
    module->uart = uhf_uart_alloc();
    module->write_mask = WRITE_EPC;
    return module;
}

void m100_module_free(M100Module* module) {
    m100_module_info_free(module->info);
    uhf_uart_free(module->uart);
    free(module);
}

uint8_t checksum(const uint8_t* data, size_t length) {
    // CheckSum8 Modulo 256
    // Sum of Bytes % 256
    uint64_t sum_val = 0x00;
    for(size_t i = 0; i < length; i++) {
        sum_val += data[i];
    }
    return (uint8_t)(sum_val % 0x100);
}

uint16_t crc16_genibus(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF; // Initial value
    uint16_t polynomial = 0x1021; // CRC-16/GENIBUS polynomial

    for(size_t i = 0; i < length; i++) {
        crc ^= (data[i] << 8); // Move byte into MSB of 16bit CRC
        for(int j = 0; j < 8; j++) {
            if(crc & 0x8000) {
                crc = (crc << 1) ^ polynomial;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc ^ 0xFFFF; // Post-inversion
}

char* _m100_info_helper(M100Module* module, char** info) {
    if(!uhf_buffer_get_size(module->uart->buffer)) return NULL;
    uint8_t* data = uhf_buffer_get_data(module->uart->buffer);
    uint16_t payload_len = data[3];
    payload_len = (payload_len << 8) + data[4];
    FuriString* temp_str = furi_string_alloc();
    for(int i = 0; i < payload_len; i++) {
        furi_string_cat_printf(temp_str, "%c", data[6 + i]);
    }
    if(*info == NULL) {
        *info = (char*)malloc(sizeof(char) * payload_len);
    } else {
        for(size_t i = 0; i < strlen(*info); i++) {
            (*info)[i] = 0;
        }
    }
    memcpy(*info, furi_string_get_cstr(temp_str), payload_len);
    furi_string_free(temp_str);
    return *info;
}

char* m100_get_hardware_version(M100Module* module) {
    setup_and_send_rx(module, (uint8_t*)&CMD_HW_VERSION.cmd[0], CMD_HW_VERSION.length);
    return _m100_info_helper(module, &module->info->hw_version);
}
char* m100_get_software_version(M100Module* module) {
    setup_and_send_rx(module, (uint8_t*)&CMD_SW_VERSION.cmd[0], CMD_SW_VERSION.length);
    return _m100_info_helper(module, &module->info->sw_version);
}
char* m100_get_manufacturers(M100Module* module) {
    setup_and_send_rx(module, (uint8_t*)&CMD_MANUFACTURERS.cmd[0], CMD_MANUFACTURERS.length);
    return _m100_info_helper(module, &module->info->manufacturer);
}

M100ResponseType m100_single_poll(M100Module* module, UHFTag* uhf_tag) {
    M100ResponseType rp_type =
        setup_and_send_rx(module, (uint8_t*)&CMD_SINGLE_POLLING.cmd[0], CMD_SINGLE_POLLING.length);
    if(rp_type != M100SuccessResponse) return rp_type;
    uint8_t* data = uhf_buffer_get_data(module->uart->buffer);
    uint16_t pc = data[6];
    uint16_t crc = 0;
    // mask out epc length from protocol control
    size_t epc_len = pc;
    epc_len >>= 3;
    epc_len *= 2;
    // get protocol control
    pc <<= 8;
    pc += data[7];
    // get cyclic redundency check
    crc = data[8 + epc_len];
    crc <<= 8;
    crc += data[8 + epc_len + 1];
    // validate crc
    if(crc16_genibus(data + 6, epc_len + 2) != crc) return M100ValidationFail;
    uhf_tag_set_epc_pc(uhf_tag, pc);
    uhf_tag_set_epc_crc(uhf_tag, crc);
    uhf_tag_set_epc(uhf_tag, data + 8, epc_len);
    return M100SuccessResponse;
}

M100ResponseType m100_multi_poll(M100Module* module, UHFTagWrapper* wrapper, UHFWorker* worker) {
    UHFUart* uart = module->uart;
    Buffer* buffer = uart->buffer;
    M100ResponseType result = M100EmptyResponse;

    // Send CMD_MULTIPLE_POLLING once to start the inventory round
    uhf_buffer_reset(buffer);
    uhf_uart_send_wait(
        uart,
        (uint8_t*)&CMD_MULTIPLE_POLLING.cmd[0],
        CMD_MULTIPLE_POLLING.length);

    // Collect one EPC notification frame per tag until stopped or list full
    while(wrapper->tag_count < UHF_TAG_WRAPPER_MAX_TAGS) {
        if(worker->state == UHFWorkerStateStop) {
            FURI_LOG_I(UHF_MOD_TAG, "multi_poll: aborted by worker stop");
            break;
        }

        // Reset buffer to receive next frame
        uhf_buffer_reset(buffer);

        // CMD_MULTIPLE_POLLING triggers a Gen2 inventory round that takes 10-100 ms
        // before the first EPC frame arrives. A single 1000-tick window (~0.1 ms at
        // 64 MHz) is too short. Use up to 500 tick-reset rounds (~50-500 ms total)
        // so slow inventory rounds and inter-tag gaps are covered.
        bool frame_ready = false;
        for(int round = 0; round < 500; round++) {
            uhf_uart_tick_reset(uart);
            while(!uhf_is_buffer_closed(buffer) && !uhf_uart_tick(uart)) {
                if(worker->state == UHFWorkerStateStop) break;
            }
            if(uhf_is_buffer_closed(buffer)) {
                frame_ready = true;
                break;
            }
            if(worker->state == UHFWorkerStateStop) break;
        }
        uhf_uart_tick_reset(uart);

        uint8_t* data = uhf_buffer_get_data(buffer);
        size_t length = uhf_buffer_get_size(buffer);

        // All rounds exhausted without a frame — inventory round complete
        if(!frame_ready) {
            FURI_LOG_I(
                UHF_MOD_TAG,
                "multi_poll: no more frames, total tags=%d",
                (int)wrapper->tag_count);
            break;
        }

        // Validate frame structure and checksum
        if(length < 13 || data[0] != FRAME_START || data[length - 1] != FRAME_END) {
            FURI_LOG_W(UHF_MOD_TAG, "multi_poll: invalid frame, len=%d", (int)length);
            continue;
        }
        if(checksum(data + 1, length - 3) != data[length - 2]) {
            FURI_LOG_W(UHF_MOD_TAG, "multi_poll: frame checksum fail");
            continue;
        }

        // Parse EPC — same layout as m100_single_poll (data[6]=PC_hi, data[7]=PC_lo)
        uint16_t pc = data[6];
        size_t epc_len = (pc >> 3) * 2;
        pc = (uint16_t)((pc << 8) | data[7]);

        if(length < (size_t)(8 + epc_len + 4)) {
            FURI_LOG_W(
                UHF_MOD_TAG,
                "multi_poll: epc_len %d exceeds frame len %d",
                (int)epc_len,
                (int)length);
            continue;
        }

        uint16_t crc =
            (uint16_t)(((uint16_t)data[8 + epc_len] << 8) | data[8 + epc_len + 1]);
        if(crc16_genibus(data + 6, epc_len + 2) != crc) {
            FURI_LOG_W(UHF_MOD_TAG, "multi_poll: EPC CRC fail");
            continue;
        }

        uint8_t* epc_data = data + 8;

        // Deduplicate — skip if EPC already present in the list
        bool duplicate = false;
        for(size_t i = 0; i < wrapper->tag_count; i++) {
            UHFTag* existing = wrapper->tags[i];
            if(existing->epc != NULL && existing->epc->size == epc_len &&
               memcmp(existing->epc->data, epc_data, epc_len) == 0) {
                duplicate = true;
                break;
            }
        }
        if(duplicate) {
            FURI_LOG_D(UHF_MOD_TAG, "multi_poll: duplicate EPC, skipping");
            continue;
        }

        // Allocate new tag, populate EPC fields, add to wrapper list
        UHFTag* new_tag = uhf_tag_alloc();
        uhf_tag_reset(new_tag);
        uhf_tag_set_epc_pc(new_tag, pc);
        uhf_tag_set_epc_crc(new_tag, crc);
        uhf_tag_set_epc(new_tag, epc_data, epc_len);

        if(!uhf_tag_wrapper_add_tag(wrapper, new_tag)) {
            // Guard: shouldn't happen since loop condition checks tag_count
            FURI_LOG_W(UHF_MOD_TAG, "multi_poll: wrapper add failed");
            uhf_tag_free(new_tag);
            break;
        }
        FURI_LOG_I(UHF_MOD_TAG, "multi_poll: added unique tag %d", (int)wrapper->tag_count);
        result = M100SuccessResponse;
    }

    // Always stop the inventory round regardless of exit path
    uhf_buffer_reset(buffer);
    uhf_uart_send_wait(
        uart,
        (uint8_t*)&CMD_STOP_MULTIPLE_POLLING.cmd[0],
        CMD_STOP_MULTIPLE_POLLING.length);
    // Drain the stop-ack frame (module responds quickly; 20 rounds is sufficient)
    for(int round = 0; round < 20; round++) {
        uhf_uart_tick_reset(uart);
        while(!uhf_is_buffer_closed(buffer) && !uhf_uart_tick(uart)) {}
        if(uhf_is_buffer_closed(buffer)) break;
    }
    uhf_uart_tick_reset(uart);

    FURI_LOG_I(
        UHF_MOD_TAG,
        "multi_poll: done, total tags=%d, result=%d",
        (int)wrapper->tag_count,
        (int)result);
    return result;
}

M100ResponseType m100_set_select(M100Module* module, UHFTag* uhf_tag) {
    // Set select
    uint8_t cmd[MAX_BUFFER_SIZE];
    size_t cmd_length = CMD_SET_SELECT_PARAMETER.length;
    size_t mask_length_bytes = uhf_tag->epc->size;
    size_t mask_length_bits = mask_length_bytes * 8;
    // payload len == sel param len + ptr len + mask len + epc len
    size_t payload_len = 7 + mask_length_bytes;
    memcpy(cmd, CMD_SET_SELECT_PARAMETER.cmd, cmd_length);
    // set new length
    cmd_length = 12 + mask_length_bytes + 2;
    // set payload length
    cmd[3] = (payload_len >> 8) & 0xFF;
    cmd[4] = payload_len & 0xFF;
    // set select param
    cmd[5] = 0x01; // 0x00=rfu, 0x01=epc, 0x10=tid, 0x11=user
    // set ptr
    cmd[9] = 0x20; // epc data begins after 0x20
    // set mask length
    cmd[10] = mask_length_bits;
    // truncate
    cmd[11] = false;
    // set mask
    memcpy((void*)&cmd[12], uhf_tag->epc->data, mask_length_bytes);

    // set checksum
    cmd[cmd_length - 2] = checksum(cmd + 1, 11 + mask_length_bytes);
    // end frame
    cmd[cmd_length - 1] = FRAME_END;

    M100ResponseType rp_type = setup_and_send_rx(module, cmd, 12 + mask_length_bytes + 3);

    if(rp_type != M100SuccessResponse) return rp_type;

    uint8_t* data = uhf_buffer_get_data(module->uart->buffer);
    if(data[5] != 0x00) return M100ValidationFail; // error if not 0

    return M100SuccessResponse;
}

void m100_enable_write_mask(M100Module* module, WriteMask mask) {
    module->write_mask |= mask;
}

void m100_disable_write_mask(M100Module* module, WriteMask mask) {
    module->write_mask &= ~mask;
}

bool m100_is_write_mask_enabled(M100Module* module, WriteMask mask) {
    return (module->write_mask & mask) == mask;
}

UHFTag* m100_get_select_param(M100Module* module) {
    uhf_buffer_reset(module->uart->buffer);
    // furi_hal_uart_set_irq_cb(FuriHalUartIdLPUART1, rx_callback, module->uart->buffer);
    // furi_hal_uart_tx(
    //     FuriHalUartIdUSART1,
    //     (uint8_t*)&CMD_GET_SELECT_PARAMETER.cmd,
    //     CMD_GET_SELECT_PARAMETER.length);
    // furi_delay_ms(DELAY_MS);
    // UHFTag* uhf_tag = uhf_tag_alloc();
    // uint8_t* data = buffer_get_data(module->uart->buffer);
    // size_t mask_length =
    // uhf_tag_set_epc(uhf_tag, data + 12, )
    // TODO : implement
    return NULL;
}

//Modified by William Riley Haffner (haffnerriley)
M100ResponseType m100_read_label_data_storage(
    M100Module* module,
    UHFTag* uhf_tag,
    BankType bank,
    uint32_t access_pwd,
    uint16_t word_count) {
    /*
        Will probably remove UHFTag as param and get it from get selected tag
    */
    if(bank == EPCBank) return M100SuccessResponse;
    uint8_t cmd[MAX_BUFFER_SIZE];
    size_t cmd_length = CMD_READ_LABEL_DATA_STORAGE_AREA.length;
    memcpy(cmd, CMD_READ_LABEL_DATA_STORAGE_AREA.cmd, cmd_length);
    // set access password
    cmd[5] = (access_pwd >> 24) & 0xFF;
    cmd[6] = (access_pwd >> 16) & 0xFF;
    cmd[7] = (access_pwd >> 8) & 0xFF;
    cmd[8] = access_pwd & 0xFF;
    // set mem bank
    cmd[9] = (uint8_t)bank;
    // set word counter

    if(bank == ReservedBank) {
        word_count = 4;
    }
    cmd[12] = (word_count >> 8) & 0xFF;
    cmd[13] = word_count & 0xFF;
    // calc checksum
    cmd[cmd_length - 2] = checksum(cmd + 1, cmd_length - 3);

    M100ResponseType rp_type = setup_and_send_rx(module, cmd, cmd_length);
    if(rp_type != M100SuccessResponse) return rp_type;

    uint8_t* data = uhf_buffer_get_data(module->uart->buffer);

    uint8_t rtn_command = data[2];
    uint16_t payload_len = data[3];
    payload_len = (payload_len << 8) + data[4];

    if(rtn_command == 0xFF) {
        if(payload_len == 0x01) return M100NoTagResponse;
        return M100MemoryOverrun;
    }

    size_t ptr_offset = 5 /*<-ptr offset*/ + uhf_tag_get_epc_size(uhf_tag) + 3 /*<-pc + ul*/;
    size_t bank_data_length = payload_len - (ptr_offset - 5 /*dont include the offset*/);

    if(bank == TIDBank) {
        uhf_tag_set_tid(uhf_tag, data + ptr_offset, bank_data_length);
    } else if(bank == UserBank) {
        uhf_tag_set_user(uhf_tag, data + ptr_offset, bank_data_length);
    } else if(bank == ReservedBank) {
        uhf_tag_set_kill_pwd(uhf_tag, data + ptr_offset, bank_data_length);
        uhf_tag_set_access_pwd(uhf_tag, data + ptr_offset, bank_data_length);
    }

    return M100SuccessResponse;
}

//Created by William Riley Haffner (haffnerriley)
//Function that handles creating the lock parameter bytes in the lock payload
//Follows the Gen2 Standards: https://www.gs1.org/sites/default/files/docs/epc/Gen2_Protocol_Standard.pdf
uint32_t get_lock_param(uint32_t lock_param, BankType bank, LockType lockfunction) {
    if(lockfunction == Lock) {
        switch(bank) {
        case KillPwd:
            lock_param = 0x80200;
            break;
        case AccessPwd:
            lock_param = 0x20080;
            break;
        case EPCBank:
            lock_param = 0x08020;
            break;
        case TIDBank:
            lock_param = 0x02008;
            break;
        case FileZero:
            lock_param = 0x00802;
            break;
        default:
            return 0x02008;
        }
    } else if(lockfunction == PermaLock) {
        switch(bank) {
        case KillPwd:
            lock_param = 0xC0300;
            break;
        case AccessPwd:
            lock_param = 0x300C0;
            break;
        case EPCBank:
            lock_param = 0x0C030;
            break;
        case TIDBank:
            lock_param = 0x0300C;
            break;
        case FileZero:
            lock_param = 0x00C03;
            break;
        default:
            return 0x300C0;
        }
    } else if(lockfunction == PermaUnlock) {
        switch(bank) {
        case KillPwd:
            lock_param = 0x40100;
            break;
        case AccessPwd:
            lock_param = 0x10040;
            break;
        case EPCBank:
            lock_param = 0x04010;
            break;
        case TIDBank:
            lock_param = 0x01004;
            break;
        case FileZero:
            lock_param = 0x00401;
            break;
        default:
            return 0x10040;
        }
    } else {
        switch(bank) {
        case KillPwd:
            lock_param = 0xC0000;
            break;
        case AccessPwd:
            lock_param = 0x30000;
            break;
        case EPCBank:
            lock_param = 0x0C000;
            break;
        case TIDBank:
            lock_param = 0x03000;
            break;
        case FileZero:
            lock_param = 0x00C00;
            break;
        default:
            return 0x30000;
        }
    }

    return lock_param;
}
//Created by William Riley Haffner
//Handles locking the uhf tag
M100ResponseType m100_lock_label_data(
    M100Module* module,
    BankType bank,
    uint32_t access_pwd,
    LockType lockfunction) {
    uint8_t cmd[MAX_BUFFER_SIZE];
    size_t cmd_length = CMD_LOCK_LABEL_DATA_STORE.length;
    uint32_t lock_param = 0;
    memcpy(cmd, CMD_LOCK_LABEL_DATA_STORE.cmd, cmd_length);

    cmd[5] = (access_pwd >> 24) & 0xFF;
    cmd[6] = (access_pwd >> 16) & 0xFF;
    cmd[7] = (access_pwd >> 8) & 0xFF;
    cmd[8] = access_pwd & 0xFF;

    lock_param = get_lock_param(lock_param, bank, lockfunction);

    cmd[9] = (lock_param >> 16) & 0xFF;
    cmd[10] = (lock_param >> 8) & 0xFF;
    cmd[11] = lock_param & 0xFF;

    // Calculate checksum
    cmd[cmd_length - 2] = checksum(cmd + 1, cmd_length - 3);

    // Set end frame
    cmd[cmd_length - 1] = FRAME_END;

    // Send command and receive response
    M100ResponseType rp_type = setup_and_send_rx(module, cmd, cmd_length);
    if(rp_type != M100SuccessResponse) return rp_type;

    uint8_t* data = uhf_buffer_get_data(module->uart->buffer);

    // Validate response
    uint8_t rtn_command = data[2];
    uint16_t payload_len = data[3];
    payload_len = (payload_len << 8) + data[4];

    //Need to add a check here for an incorrect password. Look for an error code of some sort
    if(rtn_command == 0xFF) {
        if(payload_len == 0x01)
            return M100NoTagResponse;

        else if(data[2] == 0xFF && (payload_len == 16 && data[5] == 0x16))
            return M100APWrong;

        return M100MemoryOverrun;
    }

    return M100SuccessResponse;
}

//Created by William Riley Haffner (haffnerriley)
//Kills the tag
M100ResponseType m100_kill_tag(M100Module* module, uint32_t kill_pwd) {
    uint8_t cmd[MAX_BUFFER_SIZE];
    size_t cmd_length = CMD_INACTIVATE_KILL_TAG.length;
    memcpy(cmd, CMD_INACTIVATE_KILL_TAG.cmd, cmd_length);

    cmd[5] = (kill_pwd >> 24) & 0xFF;
    cmd[6] = (kill_pwd >> 16) & 0xFF;
    cmd[7] = (kill_pwd >> 8) & 0xFF;
    cmd[8] = kill_pwd & 0xFF;

    // Calculate checksum
    cmd[cmd_length - 2] = checksum(cmd + 1, cmd_length - 3);

    // Set end frame
    cmd[cmd_length - 1] = FRAME_END;

    // Send command and receive response
    M100ResponseType rp_type = setup_and_send_rx(module, cmd, cmd_length);
    if(rp_type != M100SuccessResponse) return rp_type;

    uint8_t* data = uhf_buffer_get_data(module->uart->buffer);

    // Validate response
    uint8_t rtn_command = data[2];
    uint16_t payload_len = data[3];
    payload_len = (payload_len << 8) + data[4];

    if(rtn_command == 0xFF) {
        if(payload_len == 0x01)
            return M100NoTagResponse;
        else if(data[2] == 0xFF && (payload_len == 16 && data[5] == 0x12))
            return M100APWrong;
        return M100MemoryOverrun;
    }

    return M100SuccessResponse;
}

//Modified by William Riley Haffner to add TID and reserved write support
M100ResponseType m100_write_label_data_storage(
    M100Module* module,
    UHFTag* saved_tag,
    UHFTag* selected_tag,
    BankType bank,
    uint16_t source_address,
    uint32_t access_pwd) {
    uint8_t cmd[MAX_BUFFER_SIZE];
    size_t cmd_length = CMD_WRITE_LABEL_DATA_STORE.length;
    memcpy(cmd, CMD_WRITE_LABEL_DATA_STORE.cmd, cmd_length);
    uint16_t payload_len = 9;
    uint16_t data_length = 0;
    if(bank == ReservedBank) {
        //Handles writing the access password and uses the saved password
        if(source_address == 32) {
            source_address = 0;
            payload_len += 8;
            data_length = 8;
            memcpy(cmd + 14, uhf_tag_get_kill_pwd(saved_tag), 4);
            memcpy(cmd + 18, uhf_tag_get_access_pwd(saved_tag), 4);

        } else if(source_address == 1) {
            //For both
            source_address = 0;
            payload_len += 8;
            data_length = 8;
            memcpy(cmd + 14, uhf_tag_get_kill_pwd(saved_tag), 4);
            memcpy(cmd + 18, uhf_tag_get_access_pwd(saved_tag), 4);
        } else {
            payload_len += 4;
            data_length = 4;
            memcpy(cmd + 14, uhf_tag_get_kill_pwd(saved_tag), 4);
        }
    } else if(bank == EPCBank) {
        // epc len + pc len
        payload_len += 4 + uhf_tag_get_epc_size(saved_tag);
        data_length = 4 + uhf_tag_get_epc_size(saved_tag);
        // set data
        uint8_t tmp_arr[4];
        tmp_arr[0] = (uint8_t)((uhf_tag_get_epc_crc(selected_tag) >> 8) & 0xFF);
        tmp_arr[1] = (uint8_t)(uhf_tag_get_epc_crc(selected_tag) & 0xFF);
        tmp_arr[2] = (uint8_t)((uhf_tag_get_epc_pc(saved_tag) >> 8) & 0xFF);
        tmp_arr[3] = (uint8_t)(uhf_tag_get_epc_pc(saved_tag) & 0xFF);
        memcpy(cmd + 14, tmp_arr, 4);
        memcpy(cmd + 18, uhf_tag_get_epc(saved_tag), uhf_tag_get_epc_size(saved_tag));
    } else if(bank == TIDBank) {
        payload_len += uhf_tag_get_tid_size(saved_tag);
        data_length = uhf_tag_get_tid_size(saved_tag);
        // set data
        memcpy(cmd + 14, uhf_tag_get_tid(saved_tag), uhf_tag_get_tid_size(saved_tag));
    } else if(bank == UserBank) {
        payload_len += uhf_tag_get_user_size(saved_tag);
        data_length = uhf_tag_get_user_size(saved_tag);
        // set data
        memcpy(cmd + 14, uhf_tag_get_user(saved_tag), uhf_tag_get_user_size(saved_tag));
    }

    // set payload length
    cmd[3] = (payload_len >> 8) & 0xFF;
    cmd[4] = payload_len & 0xFF;
    // set access password
    cmd[5] = (access_pwd >> 24) & 0xFF;
    cmd[6] = (access_pwd >> 16) & 0xFF;
    cmd[7] = (access_pwd >> 8) & 0xFF;
    cmd[8] = access_pwd & 0xFF;
    // set membank
    cmd[9] = (uint8_t)bank;
    // set source address
    cmd[10] = (source_address >> 8) & 0xFF;
    cmd[11] = source_address & 0xFF;
    // set data length
    size_t data_length_words = data_length / 2;
    cmd[12] = (data_length_words >> 8) & 0xFF;
    cmd[13] = data_length_words & 0xFF;
    // update cmd len
    cmd_length = 7 + payload_len;
    // calculate checksum
    cmd[cmd_length - 2] = checksum(cmd + 1, cmd_length - 3);
    cmd[cmd_length - 1] = FRAME_END;
    // send cmd
    M100ResponseType rp_type = setup_and_send_rx(module, cmd, cmd_length);
    if(rp_type != M100SuccessResponse) return rp_type;
    uint8_t* buff_data = uhf_buffer_get_data(module->uart->buffer);
    size_t buff_length = uhf_buffer_get_size(module->uart->buffer);
    if(buff_data[2] == 0xFF && buff_length == 8)
        return M100NoTagResponse;
    else if(buff_data[2] == 0xFF && (buff_length == 23 || buff_data[5] == 0x16))
        return M100APWrong;
    else if(buff_data[2] == 0xFF)
        return M100ValidationFail;
    return M100SuccessResponse;
}

void m100_set_baudrate(M100Module* module, uint32_t baudrate) {
    size_t length = CMD_SET_COMMUNICATION_BAUD_RATE.length;
    uint8_t cmd[length];
    memcpy(cmd, CMD_SET_COMMUNICATION_BAUD_RATE.cmd, length);
    uint16_t br_mod = baudrate / 100; // module format
    cmd[6] = 0xFF & br_mod; // pow LSB
    cmd[5] = 0xFF & (br_mod >> 8); // pow MSB
    cmd[length - 2] = checksum(cmd + 1, length - 3);
    // setup_and_send_rx(module, cmd, length);
    uhf_uart_send_wait(module->uart, cmd, length);
    uhf_uart_set_baudrate(module->uart, baudrate);
    module->uart->baudrate = baudrate;
}

bool m100_set_working_region(M100Module* module, WorkingRegion region) {
    size_t length = CMD_SET_WORK_AREA.length;
    uint8_t cmd[length];
    memcpy(cmd, CMD_SET_WORK_AREA.cmd, length);
    cmd[5] = (uint8_t)region;
    cmd[length - 2] = checksum(cmd + 1, length - 3);
    setup_and_send_rx(module, cmd, length);
    module->region = region;
    return true;
}

bool m100_set_transmitting_power(M100Module* module, uint16_t power) {
    size_t length = CMD_SET_TRANSMITTING_POWER.length;
    uint8_t cmd[length];
    memcpy(cmd, CMD_SET_TRANSMITTING_POWER.cmd, length);
    cmd[5] = (power >> 8) & 0xFF;
    cmd[6] = power & 0xFF;
    cmd[length - 2] = checksum(cmd + 1, length - 3);
    setup_and_send_rx(module, cmd, length);
    module->transmitting_power = power;
    return true;
}

bool m100_set_freq_hopping(M100Module* module, bool hopping) {
    UNUSED(module);
    UNUSED(hopping);
    return true;
}

bool m100_set_power(M100Module* module, uint8_t* power) {
    UNUSED(module);
    UNUSED(power);
    return true;
}

uint32_t m100_get_baudrate(M100Module* module) {
    return module->uart->baudrate;
}
