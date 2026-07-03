#pragma once

#include <furi.h>
#include <furi_hal.h>
#include "yrm100x_module.h"

/**
 * File that handles the worker for the YRM100
 * @author frux-c
 * @author modified by haffnerriley
*/

#define UHF_WORKER_STACK_SIZE 2 * 1024

typedef enum {
    // Init states
    UHFWorkerStateNone,
    UHFWorkerStateBroken,
    UHFWorkerStateReady,
    UHFWorkerStateVerify,
    // Main worker states
    UHFWorkerStateDetectSingle,
    UHFWorkerStateDetectMultiple,
    UHFWorkerStateDeepReadSelected,
    UHFWorkerStateReadSingleBank,
    UHFWorkerStateWriteSingle,
    UHFWorkerStateWriteKey,
    //UHFWorkerStateKillTag,
    // Transition
    UHFWorkerStateStop,
} UHFWorkerState;

typedef enum {
    UHFWorkerEventSuccess,
    UHFWorkerEventFail,
    UHFWorkerEventNoTagDetected,
    UHFWorkerEventAborted,
    UHFWorkerEventCardDetected,
} UHFWorkerEvent;

typedef void (*UHFWorkerCallback)(UHFWorkerEvent event, void* ctx);
//Modified by William Riley Haffner
typedef struct UHFWorker {
    FuriThread* thread;
    M100Module* module;
    UHFWorkerCallback callback;
    UHFWorkerState state;
    UHFTagWrapper* uhf_tag_wrapper;
    //Adding tags for writing
    bool KillPwd;
    bool AccessPwd;
    UHFTag* NewTag;
    UHFTag* SelectedTag;
    uint32_t DefaultAP;
    // Which bank a single-bank read (UHFWorkerStateReadSingleBank) should fetch.
    BankType TargetBank;
    // When true, a WriteSingle operation targets the specific tag whose EPC is
    // preloaded into SelectedTag (no first-responder poll) and aborts after a
    // 10-second deadline if that tag never answers. Used by the unsaved path.
    bool Targeted;
    //uint32_t write_ap;
    void* ctx;
} UHFWorker;

int32_t uhf_worker_task(void* ctx);
UHFWorker* uhf_worker_alloc();
void uhf_worker_change_state(UHFWorker* worker, UHFWorkerState state);
void uhf_worker_start(
    UHFWorker* uhf_worker,
    UHFWorkerState state,
    UHFWorkerCallback callback,
    void* ctx);
void uhf_worker_stop(UHFWorker* uhf_worker);
void uhf_worker_free(UHFWorker* uhf_worker);