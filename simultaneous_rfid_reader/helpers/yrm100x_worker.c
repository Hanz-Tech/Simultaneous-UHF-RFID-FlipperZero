#include "yrm100x_tag.h"
#include "yrm100x_worker.h"
#include <furi.h>

static const char* UHF_WK_TAG = "UHF_WK";

/**
 * File that handles the worker for the YRM100
 * @author frux-c
 * @author modified by haffnerriley
*/

// yrm100 module commands
UHFWorkerEvent verify_module_connected(UHFWorker* uhf_worker) {
    FURI_LOG_I(UHF_WK_TAG, "Verifying module connection...");
    char* hw_version = m100_get_hardware_version(uhf_worker->module);
    char* sw_version = m100_get_software_version(uhf_worker->module);
    char* manufacturer = m100_get_manufacturers(uhf_worker->module);
    // verify all data exists
    if(hw_version == NULL || sw_version == NULL || manufacturer == NULL) {
        FURI_LOG_E(UHF_WK_TAG, "Module verification failed - missing info");
        FURI_LOG_E(
            UHF_WK_TAG,
            "HW: %s, SW: %s, Mfg: %s",
            hw_version ? hw_version : "NULL",
            sw_version ? sw_version : "NULL",
            manufacturer ? manufacturer : "NULL");
        return UHFWorkerEventFail;
    }
    FURI_LOG_I(UHF_WK_TAG, "Module OK - HW:%s SW:%s Mfg:%s", hw_version, sw_version, manufacturer);
    return UHFWorkerEventSuccess;
}

UHFTag* send_polling_command(UHFWorker* uhf_worker) {
    // read epc bank
    UHFTag* uhf_tag = uhf_tag_alloc();
    M100ResponseType status;
    int poll_attempts = 0;
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            FURI_LOG_I(UHF_WK_TAG, "Polling aborted by user");
            uhf_tag_free(uhf_tag);
            return NULL;
        }
        poll_attempts++;
        status = m100_single_poll(uhf_worker->module, uhf_tag);
        if(poll_attempts % 10 == 0) {
            FURI_LOG_I(UHF_WK_TAG, "Polling attempt %d, status=%d", poll_attempts, (int)status);
        }
    } while(status != M100SuccessResponse);
    FURI_LOG_I(UHF_WK_TAG, "Tag polled successfully after %d attempts", poll_attempts);
    return uhf_tag;
}

//Modified by William Riley Haffner to use a default access password that is set through the ap UI
UHFWorkerEvent read_bank_till_max_length(UHFWorker* uhf_worker, UHFTag* uhf_tag, BankType bank) {
    unsigned int word_low = 0, word_high = 64;
    unsigned int word_size;
    M100ResponseType status;
    int iterations = 0;
    FURI_LOG_I(UHF_WK_TAG, "Reading bank %d, binary search: 0-64 words", (int)bank);
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            FURI_LOG_I(UHF_WK_TAG, "Bank read aborted by user");
            return UHFWorkerEventAborted;
        }
        if(word_low >= word_high) {
            FURI_LOG_I(UHF_WK_TAG, "Bank read complete: %d words", word_low);
            return UHFWorkerEventSuccess;
        }
        word_size = (word_low + word_high) / 2;
        iterations++;

        FURI_LOG_I(
            UHF_WK_TAG,
            "Bank %d: try word_size=%d (range %d-%d), iter=%d",
            (int)bank,
            word_size,
            word_low,
            word_high,
            iterations);

        status = m100_read_label_data_storage(
            uhf_worker->module, uhf_tag, bank, uhf_worker->DefaultAP, word_size);
        FURI_LOG_I(UHF_WK_TAG, "Bank %d: read status=%d", (int)bank, (int)status);

        if(status == M100SuccessResponse) {
            word_low = word_size + 1;
        } else if(status == M100MemoryOverrun) {
            word_high = word_size - 1;
        } else {
            // Any other response (EmptyResponse, ValidationFail, ChecksumFail, etc.)
            // is treated as the limit, exit the binary search
            FURI_LOG_I(
                UHF_WK_TAG,
                "Bank %d: unexpected status=%d, treating as limit",
                (int)bank,
                (int)status);
            word_high = word_size - 1;
        }
    } while(true);
    return UHFWorkerEventSuccess;
}

//Modified by Riley Haffner to read the reserved bank using the default AP set by the user
UHFWorkerEvent read_single_card(UHFWorker* uhf_worker) {
    FURI_LOG_I(UHF_WK_TAG, "=== Starting read_single_card ===");
    UHFTag* uhf_tag = send_polling_command(uhf_worker);
    if(uhf_tag == NULL) {
        FURI_LOG_I(UHF_WK_TAG, "read_single_card: poll returned NULL");
        return UHFWorkerEventAborted;
    }
    uhf_tag_wrapper_set_tag(uhf_worker->uhf_tag_wrapper, uhf_tag);

    // set select with stop check
    FURI_LOG_I(UHF_WK_TAG, "Calling m100_set_select...");
    M100ResponseType set_status;
    int set_attempts = 0;
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            FURI_LOG_I(UHF_WK_TAG, "set_select aborted by user");
            return UHFWorkerEventAborted;
        }
        set_attempts++;
        set_status = m100_set_select(uhf_worker->module, uhf_tag);
        if(set_attempts % 10 == 0) {
            FURI_LOG_I(
                UHF_WK_TAG, "set_select attempt %d, status=%d", set_attempts, (int)set_status);
        }
    } while(set_status != M100SuccessResponse);
    FURI_LOG_I(UHF_WK_TAG, "set_select succeeded after %d attempts", set_attempts);

    // read tid
    FURI_LOG_I(UHF_WK_TAG, "Reading TID bank...");
    UHFWorkerEvent event;
    event = read_bank_till_max_length(uhf_worker, uhf_tag, TIDBank);
    if(event != UHFWorkerEventSuccess) {
        FURI_LOG_I(UHF_WK_TAG, "TID read failed: %d", (int)event);
        return event;
    }

    // read user
    FURI_LOG_I(UHF_WK_TAG, "Reading User bank...");
    event = read_bank_till_max_length(uhf_worker, uhf_tag, UserBank);
    if(event != UHFWorkerEventSuccess) {
        FURI_LOG_I(UHF_WK_TAG, "User read failed: %d", (int)event);
        return event;
    }

    // read reserved
    FURI_LOG_I(UHF_WK_TAG, "Reading Reserved bank...");
    event = read_bank_till_max_length(uhf_worker, uhf_tag, ReservedBank);
    if(event != UHFWorkerEventSuccess) {
        FURI_LOG_I(UHF_WK_TAG, "Reserved read failed: %d", (int)event);
        return event;
    }

    FURI_LOG_I(UHF_WK_TAG, "=== read_single_card complete ===");
    return UHFWorkerEventSuccess;
}

//Modified by Riley Haffner to be able to write to the reserved bank
UHFWorkerEvent write_single_card(UHFWorker* uhf_worker) {
    //uhf_worker->TagToWrite
    UHFTag* uhf_tag_des = send_polling_command(uhf_worker);
    if(uhf_tag_des == NULL) return UHFWorkerEventAborted;

    UHFTag* uhf_tag_from = uhf_worker->NewTag;
    M100ResponseType rp_type;
    do {
        rp_type = m100_set_select(uhf_worker->module, uhf_tag_des);
        if(uhf_worker->state == UHFWorkerStateStop) return UHFWorkerEventAborted;
        if(rp_type == M100SuccessResponse) break;
    } while(true);
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_USER)) {
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, UserBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            break;
        }
    }
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_TID)) {
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, TIDBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            break;
        }
    }
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_EPC)) {
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, EPCBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            break;
        }
    }
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_RFU)) {
        if(uhf_worker->KillPwd && uhf_worker->AccessPwd) {
            rp_type = m100_write_label_data_storage(
                uhf_worker->module,
                uhf_tag_from,
                uhf_tag_des,
                ReservedBank,
                1,
                uhf_worker->DefaultAP);
        } else if(uhf_worker->KillPwd) {
            rp_type = m100_write_label_data_storage(
                uhf_worker->module,
                uhf_tag_from,
                uhf_tag_des,
                ReservedBank,
                0,
                uhf_worker->DefaultAP);
        } else if(uhf_worker->AccessPwd) {
            rp_type = m100_write_label_data_storage(
                uhf_worker->module,
                uhf_tag_from,
                uhf_tag_des,
                ReservedBank,
                32,
                uhf_worker->DefaultAP);
        }

        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            break;
        }
        if(rp_type == M100APWrong) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            return UHFWorkerEventAborted;
        }
    }
    return UHFWorkerEventSuccess;
}

static UHFWorkerEvent detect_multiple_cards(UHFWorker* uhf_worker) {
    FURI_LOG_I(UHF_WK_TAG, "=== Starting detect_multiple_cards ===");
    // Clear any tags from a previous multi-poll round
    uhf_tag_wrapper_reset_list(uhf_worker->uhf_tag_wrapper);
    M100ResponseType status =
        m100_multi_poll(uhf_worker->module, uhf_worker->uhf_tag_wrapper, uhf_worker);
    if(uhf_worker->state == UHFWorkerStateStop) {
        FURI_LOG_I(UHF_WK_TAG, "detect_multiple_cards: aborted");
        return UHFWorkerEventAborted;
    }
    FURI_LOG_I(
        UHF_WK_TAG,
        "detect_multiple_cards: status=%d, tags=%d",
        (int)status,
        (int)uhf_worker->uhf_tag_wrapper->tag_count);
    if(status == M100SuccessResponse && uhf_worker->uhf_tag_wrapper->tag_count > 0) {
        return UHFWorkerEventSuccess;
    }
    return UHFWorkerEventNoTagDetected;
}

int32_t uhf_worker_task(void* ctx) {
    UHFWorker* uhf_worker = ctx;
    if(uhf_worker->state == UHFWorkerStateVerify) {
        UHFWorkerEvent event = verify_module_connected(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateDetectSingle) {
        UHFWorkerEvent event = read_single_card(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateDetectMultiple) {
        UHFWorkerEvent event = detect_multiple_cards(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateWriteSingle) {
        UHFWorkerEvent event = write_single_card(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    }
    return 0;
}

UHFWorker* uhf_worker_alloc() {
    UHFWorker* uhf_worker = (UHFWorker*)malloc(sizeof(UHFWorker));
    uhf_worker->thread =
        furi_thread_alloc_ex("UHFWorker", UHF_WORKER_STACK_SIZE, uhf_worker_task, uhf_worker);
    uhf_worker->module = m100_module_alloc();
    uhf_worker->callback = NULL;
    uhf_worker->ctx = NULL;
    uhf_worker->NewTag = uhf_tag_alloc();
    uhf_worker->KillPwd = false;
    uhf_worker->AccessPwd = false;
    uhf_worker->DefaultAP = 0;
    return uhf_worker;
}

void uhf_worker_change_state(UHFWorker* worker, UHFWorkerState state) {
    worker->state = state;
}

void uhf_worker_start(
    UHFWorker* uhf_worker,
    UHFWorkerState state,
    UHFWorkerCallback callback,
    void* ctx) {
    uhf_worker->state = state;
    uhf_worker->callback = callback;
    uhf_worker->ctx = ctx;
    furi_thread_start(uhf_worker->thread);
}

void uhf_worker_stop(UHFWorker* uhf_worker) {
    furi_assert(uhf_worker);
    furi_assert(uhf_worker->thread);

    if(furi_thread_get_state(uhf_worker->thread) != FuriThreadStateStopped) {
        uhf_worker_change_state(uhf_worker, UHFWorkerStateStop);
        furi_thread_join(uhf_worker->thread);
    }
}

void uhf_worker_free(UHFWorker* uhf_worker) {
    furi_assert(uhf_worker);
    furi_thread_free(uhf_worker->thread);
    m100_module_free(uhf_worker->module);
    uhf_tag_free(uhf_worker->NewTag);
    free(uhf_worker);
}
