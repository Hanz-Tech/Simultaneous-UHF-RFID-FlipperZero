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
// Number of times to retry a single word_size probe when the tag fails to
// answer (RF dropout). A timeout/partial frame does NOT mean the bank ends
// here — the tag is momentarily out of range — so we retry before deciding.
// The bank boundary is signalled ONLY by a memory-overrun (0xA3); a timeout is
// never a boundary, so we retry generously to keep the length discovery exact.
#define BANK_READ_MAX_RETRIES 10

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
        if(word_low > word_high) {
            FURI_LOG_I(UHF_WK_TAG, "Bank read complete: %u words", word_low - 1);
            return UHFWorkerEventSuccess;
        }

        word_size = (word_low + word_high) / 2;
        if(word_size == 0) {
            // Range collapsed to zero: no readable words in this bank.
            FURI_LOG_I(UHF_WK_TAG, "Bank %d: 0 readable words", (int)bank);
            return UHFWorkerEventSuccess;
        }
        iterations++;

        FURI_LOG_I(
            UHF_WK_TAG,
            "Bank %d: try word_size=%d (range %d-%d), iter=%d",
            (int)bank,
            word_size,
            word_low,
            word_high,
            iterations);

        // Probe this word_size, retrying transient RF failures. Only a definitive
        // answer — SuccessResponse (grow) or MemoryOverrun (shrink) — ends the
        // retry loop. Empty/validation/checksum failures are RF dropouts: re-select
        // and try again rather than mistaking silence for the bank boundary.
        int retries = 0;
        do {
            if(uhf_worker->state == UHFWorkerStateStop) {
                FURI_LOG_I(UHF_WK_TAG, "Bank read aborted by user");
                return UHFWorkerEventAborted;
            }

            // Re-apply Select before every probe: the M100 Gen2 session expires
            // after each Read response so subsequent reads would otherwise see an
            // empty/no-tag reply even when the tag is still in field.
            m100_set_select(uhf_worker->module, uhf_tag);

            status = m100_read_label_data_storage(
                uhf_worker->module, uhf_tag, bank, uhf_worker->DefaultAP, word_size);

            if(status == M100SuccessResponse || status == M100MemoryOverrun) {
                break; // definitive answer
            }
            if(status == M100APWrong || status == M100MemoryLocked) {
                break; // definitive access failure — no point retrying
            }

            retries++;
            FURI_LOG_W(
                UHF_WK_TAG,
                "Bank %d: transient status=%d at word_size=%d, retry %d/%d",
                (int)bank,
                (int)status,
                word_size,
                retries,
                BANK_READ_MAX_RETRIES);
        } while(retries < BANK_READ_MAX_RETRIES);

        FURI_LOG_I(UHF_WK_TAG, "Bank %d: read status=%d", (int)bank, (int)status);

        if(status == M100SuccessResponse) {
            word_low = word_size + 1;
        } else if(status == M100MemoryOverrun) {
            word_high = word_size - 1;
        } else {
            // Still failing after all retries. A timeout is NOT a memory boundary —
            // shrinking here would silently truncate the bank (report fewer words
            // than the tag actually holds). Since we cannot resolve this length,
            // abort the bank read and report it as incomplete rather than return
            // truncated data. Re-scan with the tag held closer/steadier.
            FURI_LOG_W(
                UHF_WK_TAG,
                "Bank %d: unresolved at word_size=%d after %d retries (status=%d) — "
                "reporting incomplete",
                (int)bank,
                word_size,
                BANK_READ_MAX_RETRIES,
                (int)status);
            return UHFWorkerEventFail;
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

// Deep-read a single, pre-selected tag from the multi-poll list.
// The caller populates uhf_worker->SelectedTag with the target EPC before starting
// the worker; here we select that specific EPC and read its TID, User and Reserved
// banks using the user's configured default access password.
UHFWorkerEvent deep_read_selected_card(UHFWorker* uhf_worker) {
    FURI_LOG_I(UHF_WK_TAG, "=== Starting deep_read_selected_card ===");
    UHFTag* uhf_tag = uhf_worker->SelectedTag;

    // set select on the specific EPC with stop check
    M100ResponseType set_status;
    int set_attempts = 0;
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            FURI_LOG_I(UHF_WK_TAG, "deep read set_select aborted by user");
            return UHFWorkerEventAborted;
        }
        set_attempts++;
        set_status = m100_set_select(uhf_worker->module, uhf_tag);
        if(set_attempts % 10 == 0) {
            FURI_LOG_I(
                UHF_WK_TAG, "deep set_select attempt %d, status=%d", set_attempts, (int)set_status);
        }
    } while(set_status != M100SuccessResponse);
    FURI_LOG_I(UHF_WK_TAG, "deep set_select succeeded after %d attempts", set_attempts);

    // read tid
    UHFWorkerEvent event = read_bank_till_max_length(uhf_worker, uhf_tag, TIDBank);
    if(event != UHFWorkerEventSuccess) {
        FURI_LOG_I(UHF_WK_TAG, "deep TID read failed: %d", (int)event);
        return event;
    }

    // read user
    event = read_bank_till_max_length(uhf_worker, uhf_tag, UserBank);
    if(event != UHFWorkerEventSuccess) {
        FURI_LOG_I(UHF_WK_TAG, "deep User read failed: %d", (int)event);
        return event;
    }

    // read reserved
    event = read_bank_till_max_length(uhf_worker, uhf_tag, ReservedBank);
    if(event != UHFWorkerEventSuccess) {
        FURI_LOG_I(UHF_WK_TAG, "deep Reserved read failed: %d", (int)event);
        return event;
    }

    FURI_LOG_I(UHF_WK_TAG, "=== deep_read_selected_card complete ===");
    return UHFWorkerEventSuccess;
}

// Read a single memory bank (uhf_worker->TargetBank) from the already-known
// SelectedTag. Mirrors deep_read_selected_card but for one bank on demand, so
// the per-bank memory screens can fetch TID/Reserved/User individually.
UHFWorkerEvent read_single_bank_selected(UHFWorker* uhf_worker) {
    UHFTag* uhf_tag = uhf_worker->SelectedTag;
    BankType bank = uhf_worker->TargetBank;
    FURI_LOG_I(UHF_WK_TAG, "=== Starting read_single_bank_selected bank=%d ===", (int)bank);

    // Select the specific EPC first (session expires after each read).
    M100ResponseType set_status;
    int set_attempts = 0;
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            FURI_LOG_I(UHF_WK_TAG, "single read set_select aborted by user");
            return UHFWorkerEventAborted;
        }
        set_attempts++;
        set_status = m100_set_select(uhf_worker->module, uhf_tag);
    } while(set_status != M100SuccessResponse);

    UHFWorkerEvent event = read_bank_till_max_length(uhf_worker, uhf_tag, bank);
    if(event != UHFWorkerEventSuccess) {
        FURI_LOG_I(UHF_WK_TAG, "single bank %d read failed: %d", (int)bank, (int)event);
        return event;
    }

    FURI_LOG_I(UHF_WK_TAG, "=== read_single_bank_selected complete ===");
    return UHFWorkerEventSuccess;
}

//Modified by Riley Haffner to be able to write to the reserved bank
UHFWorkerEvent write_single_card(UHFWorker* uhf_worker) {
    //Targeted (unsaved) writes address a specific tag whose current EPC/PC/CRC is
    //preloaded into SelectedTag: select by that EPC and never poll a random tag.
    //Single-poll (saved) writes fall back to the first responder in the field.
    UHFTag* uhf_tag_des;
    bool targeted = uhf_worker->Targeted;
    uint32_t deadline = targeted ? furi_get_tick() + furi_ms_to_ticks(10000) : 0;

    if(targeted) {
        //Poll repeatedly and only proceed once the tag whose current EPC matches
        //our target answers. This targets the exact scanned tag AND gives us a
        //real tag object (correct PC/CRC/size) for the write frame — a fabricated
        //tag built from on-screen strings can carry an unread ("----") PC/CRC and
        //make the EPC write silently ineffective.
        UHFTag* target = uhf_worker->SelectedTag;
        UHFTag* probe = uhf_tag_alloc();
        bool found = false;
        while(!found) {
            if(uhf_worker->state == UHFWorkerStateStop) {
                uhf_tag_free(probe);
                return UHFWorkerEventAborted;
            }
            if(furi_get_tick() >= deadline) {
                uhf_tag_free(probe);
                return UHFWorkerEventAborted;
            }
            if(m100_single_poll(uhf_worker->module, probe) != M100SuccessResponse) {
                continue;
            }
            if(probe->epc->size == target->epc->size &&
               memcmp(probe->epc->data, target->epc->data, target->epc->size) == 0) {
                found = true;
            }
        }
        //Copy the freshly polled tag's real EPC/PC/CRC into SelectedTag so the
        //select + write frames use accurate values.
        uhf_tag_set_epc(target, probe->epc->data, probe->epc->size);
        uhf_tag_set_epc_pc(target, probe->epc->pc);
        uhf_tag_set_epc_crc(target, probe->epc->crc);
        uhf_tag_free(probe);
        uhf_tag_des = target;
    } else {
        uhf_tag_des = send_polling_command(uhf_worker);
        if(uhf_tag_des == NULL) return UHFWorkerEventAborted;
    }

    UHFTag* uhf_tag_from = uhf_worker->NewTag;
    M100ResponseType rp_type;
    do {
        rp_type = m100_set_select(uhf_worker->module, uhf_tag_des);
        if(uhf_worker->state == UHFWorkerStateStop) return UHFWorkerEventAborted;
        if(targeted && furi_get_tick() >= deadline) return UHFWorkerEventAborted;
        if(rp_type == M100SuccessResponse) break;
    } while(true);
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_USER)) {
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, UserBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            return UHFWorkerEventAborted;
        }
        if(targeted && furi_get_tick() >= deadline) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100MemoryLocked) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            return UHFWorkerEventAccessDenied;
        }
        if(rp_type == M100APWrong) {
            m100_disable_write_mask(uhf_worker->module, WRITE_USER);
            return UHFWorkerEventWrongPassword;
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
        if(targeted && furi_get_tick() >= deadline) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100MemoryLocked) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            return UHFWorkerEventAccessDenied;
        }
        if(rp_type == M100APWrong) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            return UHFWorkerEventWrongPassword;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_TID);
            break;
        }
    }
    while(m100_is_write_mask_enabled(uhf_worker->module, WRITE_EPC)) {
        //The EPC bank's PC (Protocol Control) word encodes the EPC length in its
        //top 5 bits. When the new EPC differs in length from the tag's current
        //EPC we MUST write a matching PC or the tag backscatters the wrong length
        //and becomes unreadable. Take the real tag's lower PC bits (NSI/UMI/XI/
        //toggle) and overwrite the length field with the new EPC's word count.
        uint16_t base_pc = uhf_tag_get_epc_pc(uhf_tag_des);
        uint16_t new_words = uhf_tag_get_epc_size(uhf_tag_from) / 2;
        //Never commit a zero-length EPC: that clears the tag and makes it
        //undetectable. The caller validates length, but guard here too.
        if(new_words == 0) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            break;
        }
        uint16_t new_pc = (uint16_t)((base_pc & 0x07FF) | ((new_words & 0x1F) << 11));
        uhf_tag_set_epc_pc(uhf_tag_from, new_pc);
        rp_type = m100_write_label_data_storage(
            uhf_worker->module, uhf_tag_from, uhf_tag_des, EPCBank, 0, uhf_worker->DefaultAP);
        if(uhf_worker->state == UHFWorkerStateStop) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            return UHFWorkerEventAborted;
        }
        if(targeted && furi_get_tick() >= deadline) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100MemoryLocked) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            return UHFWorkerEventAccessDenied;
        }
        if(rp_type == M100APWrong) {
            m100_disable_write_mask(uhf_worker->module, WRITE_EPC);
            return UHFWorkerEventWrongPassword;
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
        if(targeted && furi_get_tick() >= deadline) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            return UHFWorkerEventAborted;
        }
        if(rp_type == M100SuccessResponse) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            break;
        }
        if(rp_type == M100MemoryLocked) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            return UHFWorkerEventAccessDenied;
        }
        if(rp_type == M100APWrong) {
            m100_disable_write_mask(uhf_worker->module, WRITE_RFU);
            return UHFWorkerEventWrongPassword;
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
    size_t tag_count = uhf_worker->uhf_tag_wrapper->tag_count;
    FURI_LOG_I(
        UHF_WK_TAG,
        "detect_multiple_cards: status=%d, tags=%d, state=%d",
        (int)status,
        (int)tag_count,
        (int)uhf_worker->state);
    // Whenever we collected at least one tag, report success so the view displays
    // the list — even if the user pressed Stop to end the scan early.
    if(tag_count > 0) {
        return UHFWorkerEventSuccess;
    }
    if(uhf_worker->state == UHFWorkerStateStop) {
        FURI_LOG_I(UHF_WK_TAG, "detect_multiple_cards: aborted with no tags");
        return UHFWorkerEventAborted;
    }
    return UHFWorkerEventNoTagDetected;
}

// Clone phase 1: poll for the first tag in field, giving up after 10 seconds.
// Stores the found tag in uhf_tag_wrapper->uhf_tag and fires CardDetected.
// Returns NoTagDetected on timeout, or Aborted if the user pressed Back.
static UHFWorkerEvent clone_scan_card(UHFWorker* uhf_worker) {
    UHFTag* uhf_tag = uhf_tag_alloc();
    M100ResponseType status;
    const uint32_t timeout_ticks = furi_ms_to_ticks(10000);
    const uint32_t start_tick = furi_get_tick();
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            uhf_tag_free(uhf_tag);
            return UHFWorkerEventAborted;
        }
        if((furi_get_tick() - start_tick) >= timeout_ticks) {
            FURI_LOG_I(UHF_WK_TAG, "Clone scan timed out (no tag in 10s)");
            uhf_tag_free(uhf_tag);
            return UHFWorkerEventNoTagDetected;
        }
        status = m100_single_poll(uhf_worker->module, uhf_tag);
    } while(status != M100SuccessResponse);
    uhf_tag_wrapper_set_tag(uhf_worker->uhf_tag_wrapper, uhf_tag);
    return UHFWorkerEventCardDetected;
}

// Clone phase 2: write selected banks from NewTag onto the first tag in field.
// Uses uhf_worker->CloneMask to determine which banks to write.
// Returns Success, Aborted, or AccessDenied (M100APWrong).
static UHFWorkerEvent clone_write_card(UHFWorker* uhf_worker) {
    // Poll for the target tag (first responder in field)
    UHFTag* target = send_polling_command(uhf_worker);
    if(target == NULL) return UHFWorkerEventAborted;

    UHFTag* source = uhf_worker->NewTag;
    uint16_t mask = uhf_worker->CloneMask;
    M100ResponseType status;

    // Select the target tag
    do {
        if(uhf_worker->state == UHFWorkerStateStop) {
            uhf_tag_free(target);
            return UHFWorkerEventAborted;
        }
        status = m100_set_select(uhf_worker->module, target);
    } while(status != M100SuccessResponse);

    // Write User bank
    if(mask & WRITE_USER) {
        if(source->user->size > 0) {
            while(true) {
                if(uhf_worker->state == UHFWorkerStateStop) {
                    uhf_tag_free(target);
                    return UHFWorkerEventAborted;
                }
                status = m100_write_label_data_storage(
                    uhf_worker->module, source, target, UserBank, 0, uhf_worker->DefaultAP);
                if(status == M100APWrong) {
                    uhf_tag_free(target);
                    return UHFWorkerEventAccessDenied;
                }
                if(status == M100SuccessResponse) break;
            }
        }
    }

    // Write TID bank
    if(mask & WRITE_TID) {
        if(source->tid->size > 0) {
            while(true) {
                if(uhf_worker->state == UHFWorkerStateStop) {
                    uhf_tag_free(target);
                    return UHFWorkerEventAborted;
                }
                status = m100_write_label_data_storage(
                    uhf_worker->module, source, target, TIDBank, 0, uhf_worker->DefaultAP);
                if(status == M100APWrong) {
                    uhf_tag_free(target);
                    return UHFWorkerEventAccessDenied;
                }
                if(status == M100SuccessResponse) break;
            }
        }
    }

    // Write EPC bank — adjust PC word count to match new EPC length
    if(mask & WRITE_EPC) {
        if(source->epc->size > 0) {
            uint16_t base_pc = uhf_tag_get_epc_pc(target);
            uint16_t new_words = (uint16_t)(uhf_tag_get_epc_size(source) / 2);
            if(new_words > 0) {
                uint16_t new_pc = (base_pc & 0x07FF) | ((new_words & 0x1F) << 11);
                uhf_tag_set_epc_pc(source, new_pc);
                while(true) {
                    if(uhf_worker->state == UHFWorkerStateStop) {
                        uhf_tag_free(target);
                        return UHFWorkerEventAborted;
                    }
                    status = m100_write_label_data_storage(
                        uhf_worker->module, source, target, EPCBank, 0, uhf_worker->DefaultAP);
                    if(status == M100APWrong) {
                        uhf_tag_free(target);
                        return UHFWorkerEventAccessDenied;
                    }
                    if(status == M100SuccessResponse) break;
                }
            }
        }
    }

    // Write Reserved bank (both kill + access passwords, source_address=1)
    if(mask & WRITE_RFU) {
        if(source->reserved->size >= 4) {
            while(true) {
                if(uhf_worker->state == UHFWorkerStateStop) {
                    uhf_tag_free(target);
                    return UHFWorkerEventAborted;
                }
                // source_address=1 writes both kill pwd and access pwd (8 bytes total)
                status = m100_write_label_data_storage(
                    uhf_worker->module, source, target, ReservedBank, 1, uhf_worker->DefaultAP);
                if(status == M100APWrong) {
                    uhf_tag_free(target);
                    return UHFWorkerEventAccessDenied;
                }
                if(status == M100SuccessResponse) break;
            }
        }
    }

    uhf_tag_free(target);
    return UHFWorkerEventSuccess;
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
    } else if(uhf_worker->state == UHFWorkerStateDeepReadSelected) {
        UHFWorkerEvent event = deep_read_selected_card(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateReadSingleBank) {
        UHFWorkerEvent event = read_single_bank_selected(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateWriteSingle) {
        UHFWorkerEvent event = write_single_card(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateCloneScan) {
        UHFWorkerEvent event = clone_scan_card(uhf_worker);
        uhf_worker->callback(event, uhf_worker->ctx);
    } else if(uhf_worker->state == UHFWorkerStateCloneWrite) {
        UHFWorkerEvent event = clone_write_card(uhf_worker);
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
    uhf_worker->SelectedTag = uhf_tag_alloc();
    uhf_worker->KillPwd = false;
    uhf_worker->AccessPwd = false;
    uhf_worker->DefaultAP = 0;
    uhf_worker->DefaultKP = 0;
    uhf_worker->Targeted = false;
    uhf_worker->CloneMask = 0;
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
    uhf_tag_free(uhf_worker->SelectedTag);
    free(uhf_worker);
}
