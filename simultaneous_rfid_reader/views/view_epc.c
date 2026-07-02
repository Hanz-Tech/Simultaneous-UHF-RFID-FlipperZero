#include "view_epc.h"
#include "view_read.h"
#include "../helpers/saved_epc_functions.h"

// ─── 5-second deep-read timeout ──────────────────────────────────────────────
// Called from the FuriTimer thread: just flags the expiry and changes the
// worker state to Stop. The worker callback then fires with Aborted and
// the event handler promotes that to a DeepReadDone (show whatever was read).
static void uhf_deep_read_timeout_callback(void* ctx) {
    UHFReaderApp* App = (UHFReaderApp*)ctx;
    App->DeepReadTimerExpired = true;
    uhf_worker_change_state(App->YRM100XWorker, UHFWorkerStateStop);
}

// ─── Draw callback ────────────────────────────────────────────────────────────
void uhf_reader_view_epc_draw_callback(Canvas* canvas, void* model) {
    UHFRFIDTagModel* MyModel = (UHFRFIDTagModel*)model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 11, "            EPC Info:");
    canvas_set_font(canvas, FontSecondary);

    // EPC value — wrap at 20 chars per line (120px fits 128px display)
    const char* EpcStr = furi_string_get_cstr(MyModel->Epc);
    size_t EpcLen = strlen(EpcStr);
    const size_t CharsPerLine = 20;

    char Line1[21];
    memset(Line1, 0, sizeof(Line1));
    size_t L1 = EpcLen < CharsPerLine ? EpcLen : CharsPerLine;
    memcpy(Line1, EpcStr, L1);
    canvas_draw_str(canvas, 0, 22, Line1);

    if(EpcLen > CharsPerLine) {
        char Line2[21];
        memset(Line2, 0, sizeof(Line2));
        size_t L2 = (EpcLen - CharsPerLine) < CharsPerLine ?
                        (EpcLen - CharsPerLine) : CharsPerLine;
        memcpy(Line2, EpcStr + CharsPerLine, L2);
        canvas_draw_str(canvas, 0, 33, Line2);
    }

    // CRC and PC on one row
    canvas_draw_str(canvas, 4, 44, "CRC:");
    canvas_draw_str(canvas, 28, 44, furi_string_get_cstr(MyModel->Crc));
    canvas_draw_str(canvas, 70, 44, "PC:");
    canvas_draw_str(canvas, 88, 44, furi_string_get_cstr(MyModel->Pc));

    // Dynamic button hints
    if(MyModel->IsDeepReading) {
        canvas_draw_str(canvas, 38, 55, "Reading...");
        elements_button_right(canvas, "Actions");
    } else if(MyModel->DeepReadDone) {
        elements_button_left(canvas, "\x19 Banks");  // \x19 = down-arrow glyph in Flipper font
        elements_button_center(canvas, "Rescan");
        elements_button_right(canvas, "Actions");
    } else {
        elements_button_center(canvas, "Deep Read");
        elements_button_right(canvas, "Actions");
    }
}

// ─── Navigation callbacks ─────────────────────────────────────────────────────
uint32_t uhf_reader_navigation_exit_view_epc_callback(void* context) {
    UNUSED(context);
    return UHFReaderViewRead;
}

uint32_t uhf_reader_navigation_banks_to_epc_dump_callback(void* context) {
    UNUSED(context);
    return UHFReaderViewEpcDump;
}

// ─── Input handler ────────────────────────────────────────────────────────────
bool uhf_reader_view_epc_input_callback(InputEvent* event, void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;

    if(event->type != InputTypeShort) return false;

    // Back during deep-read: cancel (don't navigate away)
    if(event->key == InputKeyBack && App->DeepReading) {
        uhf_worker_change_state(App->YRM100XWorker, UHFWorkerStateStop);
        // Worker callback fires → UHFCustomEventDeepReadAborted → cleans up
        return true;
    }

    // Center: start deep-read (only for YRM100 with a tag selected, not already reading)
    if(event->key == InputKeyOk && !App->DeepReading &&
       App->UHFModuleType == YRM100X_MODULE && App->NumberOfEpcsToRead > 0) {
        UHFTagWrapper* wrapper = App->YRM100XWorker->uhf_tag_wrapper;
        uint32_t idx = App->CurEpcIndex;
        if(idx >= 1 && idx <= wrapper->tag_count) {
            // Copy selected EPC onto SelectedTag
            UHFTag* selected = wrapper->tags[idx - 1];
            UHFTag* target = App->YRM100XWorker->SelectedTag;
            uhf_tag_reset(target);
            uhf_tag_set_epc(target, selected->epc->data, selected->epc->size);
            uhf_tag_set_epc_pc(target, selected->epc->pc);
            uhf_tag_set_epc_crc(target, selected->epc->crc);

            App->DeepReading = true;
            App->DeepReadDone = false;
            App->DeepReadTimerExpired = false;

            with_view_model(
                App->ViewEpc,
                UHFRFIDTagModel * model,
                { model->IsDeepReading = true; model->DeepReadDone = false; },
                true);

            notification_message(App->Notifications, &uhf_sequence_blink_start_cyan);

            // Ensure any previous worker run has fully exited before restarting
            uhf_worker_stop(App->YRM100XWorker);

            // Start worker
            uhf_worker_start(
                App->YRM100XWorker,
                UHFWorkerStateDeepReadSelected,
                uhf_deep_read_worker_callback,
                App);

            // Start 5-second one-shot timeout (defensive: free any stale timer)
            if(App->Timer) {
                furi_timer_stop(App->Timer);
                furi_timer_free(App->Timer);
                App->Timer = NULL;
            }
            App->Timer = furi_timer_alloc(
                uhf_deep_read_timeout_callback, FuriTimerTypeOnce, App);
            furi_timer_start(App->Timer, furi_ms_to_ticks(5000));
        }
        return true;
    }

    // Right: navigate to TagAction (always available when a tag is selected)
    if(event->key == InputKeyRight && !App->DeepReading &&
       App->NumberOfEpcsToRead > 0) {
        // Set Back on TagAction to return to EPC dump
        view_set_previous_callback(
            submenu_get_view(App->SubmenuTagActions),
            uhf_reader_navigation_exit_view_epc_callback);
        view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewTagAction);
        return true;
    }

    // Down: navigate to Banks screen (only after deep-read is done)
    if(event->key == InputKeyDown && App->DeepReadDone) {
        view_set_previous_callback(
            App->ViewEpcInfo, uhf_reader_navigation_banks_to_epc_dump_callback);
        view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewEpcInfo);
        return true;
    }

    return false;
}

// ─── Custom event handler ─────────────────────────────────────────────────────
bool uhf_reader_view_epc_custom_event_callback(uint32_t event, void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;

    switch(event) {
    case UHFReaderEventIdRedrawScreen: {
        with_view_model(App->ViewEpc, UHFRFIDTagModel * _m, { UNUSED(_m); }, true);
        return true;
    }

    case UHFCustomEventDeepReadDone: {
        // Stop and free the 5-second timeout timer
        if(App->Timer) {
            furi_timer_stop(App->Timer);
            furi_timer_free(App->Timer);
            App->Timer = NULL;
        }
        uhf_worker_stop(App->YRM100XWorker);

        App->DeepReading = false;
        App->DeepReadDone = true;

        // Populate the Banks screen (ViewEpcInfo) with the freshly read data
        UHFTag* tag = App->YRM100XWorker->SelectedTag;
        uint8_t reserved_buf[8];
        memcpy(reserved_buf, tag->reserved->kill_password, 4);
        memcpy(reserved_buf + 4, tag->reserved->access_password, 4);

        char* TempEpc = convertToHexString(tag->epc->data, tag->epc->size);
        char* TempTid = convertToHexString(tag->tid->data, tag->tid->size);
        char* TempUser = convertToHexString(tag->user->data, tag->user->size);
        char* TempRes = convertToHexString(reserved_buf, 8);
        char* TempCrc = uint16_to_hex_string(tag->epc->crc);
        char* TempPc = uint16_to_hex_string(tag->epc->pc);

        App->NumberOfTidsToRead = 1;
        App->NumberOfResToRead = 1;
        App->NumberOfMemToRead = 1;

        with_view_model(
            App->ViewEpcInfo,
            UHFRFIDTagModel * _model,
            {
                furi_string_set_str(_model->Epc, TempEpc);
                furi_string_set_str(_model->Tid, TempTid);
                furi_string_set_str(_model->User, TempUser);
                furi_string_set_str(_model->Reserved, TempRes);
                furi_string_set_str(_model->Crc, TempCrc);
                furi_string_set_str(_model->Pc, TempPc);
            },
            false);

        free(TempEpc);
        free(TempTid);
        free(TempUser);
        free(TempRes);
        free(TempCrc);
        free(TempPc);

        // Update EPC dump model to show ↓Banks hint
        with_view_model(
            App->ViewEpc,
            UHFRFIDTagModel * _model,
            { _model->IsDeepReading = false; _model->DeepReadDone = true; },
            true);

        notification_message(App->Notifications, &uhf_sequence_blink_stop);
        return true;
    }

    case UHFCustomEventDeepReadAborted: {
        // Stop and free the 5-second timeout timer
        if(App->Timer) {
            furi_timer_stop(App->Timer);
            furi_timer_free(App->Timer);
            App->Timer = NULL;
        }
        App->DeepReading = false;

        with_view_model(
            App->ViewEpc,
            UHFRFIDTagModel * _model,
            { _model->IsDeepReading = false; },
            true);

        notification_message(App->Notifications, &uhf_sequence_blink_stop);
        return true;
    }

    default:
        return false;
    }
}

// ─── Enter / Exit callbacks ───────────────────────────────────────────────────
void uhf_reader_view_epc_enter_callback(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;
    // Sync deep-read state into the view model on every entry
    with_view_model(
        App->ViewEpc,
        UHFRFIDTagModel * model,
        {
            model->IsDeepReading = App->DeepReading;
            model->DeepReadDone = App->DeepReadDone;
        },
        true);
}

void uhf_reader_view_epc_exit_callback(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;
    // Safety: if a deep-read is somehow in progress, clean up the timer
    if(App->Timer) {
        furi_timer_stop(App->Timer);
        furi_timer_free(App->Timer);
        App->Timer = NULL;
    }
}

// ─── Allocation / Free ────────────────────────────────────────────────────────
void view_epc_alloc(UHFReaderApp* App) {
    App->ViewEpc = view_alloc();
    view_set_draw_callback(App->ViewEpc, uhf_reader_view_epc_draw_callback);
    view_set_input_callback(App->ViewEpc, uhf_reader_view_epc_input_callback);
    view_set_custom_callback(App->ViewEpc, uhf_reader_view_epc_custom_event_callback);
    view_set_previous_callback(App->ViewEpc, uhf_reader_navigation_exit_view_epc_callback);
    view_set_enter_callback(App->ViewEpc, uhf_reader_view_epc_enter_callback);
    view_set_exit_callback(App->ViewEpc, uhf_reader_view_epc_exit_callback);
    view_set_context(App->ViewEpc, App);

    view_allocate_model(App->ViewEpc, ViewModelTypeLockFree, sizeof(UHFRFIDTagModel));
    UHFRFIDTagModel* ModelEpc = view_get_model(App->ViewEpc);
    ModelEpc->Epc = furi_string_alloc_set("Press Read!");
    ModelEpc->Tid = furi_string_alloc_set("---");
    ModelEpc->User = furi_string_alloc_set("---");
    ModelEpc->Reserved = furi_string_alloc_set("---");
    ModelEpc->Crc = furi_string_alloc_set("----");
    ModelEpc->Pc = furi_string_alloc_set("----");
    ModelEpc->IsDeepReading = false;
    ModelEpc->DeepReadDone = false;

    view_dispatcher_add_view(App->ViewDispatcher, UHFReaderViewEpcDump, App->ViewEpc);
}

void view_epc_free(UHFReaderApp* App) {
    view_dispatcher_remove_view(App->ViewDispatcher, UHFReaderViewEpcDump);
    view_free(App->ViewEpc);
}

// ─── Legacy timer callback ─────────────────────────────────────────────────
// Used by view_delete.c and view_delete_success.c to scroll their EPC strings.
// The EPC Dump view no longer uses a periodic timer, but this symbol must stay
// for the delete views which are allocated with this callback.
void uhf_reader_view_epc_timer_callback(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;

    with_view_model(
        App->ViewDelete,
        UHFReaderDeleteModel * model,
        {
            uint32_t LenEpc = strlen(model->ScrollingText);
            model->ScrollOffset++;
            if(model->ScrollOffset >= LenEpc) {
                model->ScrollOffset = 0;
            }
        },
        true);
    with_view_model(
        App->ViewDeleteSuccess,
        UHFReaderDeleteModel * model,
        {
            uint32_t LenEpc = strlen(model->ScrollingText);
            model->ScrollOffset++;
            if(model->ScrollOffset >= LenEpc) {
                model->ScrollOffset = 0;
            }
        },
        true);
    view_dispatcher_send_custom_event(App->ViewDispatcher, UHFReaderEventIdRedrawScreen);
}
