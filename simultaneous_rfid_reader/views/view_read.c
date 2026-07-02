#include "view_read.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief      Converts hex bytearray to string 
 * @details    This function is used to convert a bytearray to a char* for use with views.
 * @param      array    The bytearray -Byte array of hexadecimal values 
 * @param      length  The length of the array - size_t length value for the byte array 
 * @return     char* - the string representation of the hexadecimal array.
*/
char* convert_to_hex_string(uint8_t* array, size_t length) {
    if(array == NULL || length == 0) {
        return " ";
    }
    FuriString* temp_str = furi_string_alloc();

    for(size_t i = 0; i < length; i++) {
        furi_string_cat_printf(temp_str, "%02X ", array[i]);
    }
    const char* furi_str = furi_string_get_cstr(temp_str);

    size_t str_len = strlen(furi_str);
    char* str = (char*)malloc(sizeof(char) * str_len);

    memcpy(str, furi_str, str_len);
    furi_string_free(temp_str);
    return str;
}

/**
 * @brief      Read Draw Callback.
 * @details    This function is called when the user selects read on the main submenu.
 * @param      canvas    The canvas - Canvas object for drawing the screen.
 * @param      model  The view model - model for the view with variables required for drawing.
*/
void uhf_reader_view_read_draw_callback(Canvas* canvas, void* model) {
    UHFReaderConfigModel* MyModel = (UHFReaderConfigModel*)model;
    FuriString* XStr = furi_string_alloc();

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 11, "           Read Menu:");
    canvas_set_font(canvas, FontSecondary);

    // Row 1: # EPCs and Cur Tag
    furi_string_printf(XStr, "%ld", MyModel->NumEpcsRead);
    canvas_draw_str(canvas, 4, 22, "# EPCs:");
    canvas_draw_str(canvas, 45, 22, furi_string_get_cstr(XStr));
    furi_string_printf(XStr, "%ld", MyModel->CurEpcIndex);
    canvas_draw_str(canvas, 70, 22, "Cur Tag:");
    canvas_draw_str(canvas, 115, 22, furi_string_get_cstr(XStr));

    // Row 2 + 3: Full EPC value, wrapped at 20 chars per line (fits 128px display)
    const char* EpcStr = furi_string_get_cstr(MyModel->EpcValue);
    size_t EpcLen = strlen(EpcStr);
    const size_t CharsPerLine = 20;

    char Line1[21];
    memset(Line1, 0, sizeof(Line1));
    size_t Line1Len = EpcLen < CharsPerLine ? EpcLen : CharsPerLine;
    memcpy(Line1, EpcStr, Line1Len);

    canvas_draw_str(canvas, 0, 33, Line1);

    if(EpcLen > CharsPerLine) {
        char Line2[21];
        memset(Line2, 0, sizeof(Line2));
        size_t Line2Len = (EpcLen - CharsPerLine) < CharsPerLine ?
                              (EpcLen - CharsPerLine) : CharsPerLine;
        memcpy(Line2, EpcStr + CharsPerLine, Line2Len);
        canvas_draw_str(canvas, 0, 44, Line2);
    }

    if(!MyModel->IsReading) {
        elements_button_left(canvas, "Prev");
        elements_button_center(canvas, "Start");
        elements_button_right(canvas, "Next");
    } else {
        elements_button_center(canvas, "Stop");
    }
    furi_string_free(XStr);
}

/**
 * @brief      Callback when the user exits the read screen.
 * @details    This function is called when the user exits the read screen.
 * @param      context  The context - not used
 * @return     the view id of the next view.
*/
uint32_t uhf_reader_navigation_read_callback(void* context) {
    UNUSED(context);
    return UHFReaderViewRead;
}

/**
 * @brief      Callback for the timer used for the read screen
 * @details    This function is called for timer events.
 * @param      context  The UHFReaderApp - Used to change app variables.
*/
void uhf_reader_view_read_timer_callback(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;
    // Just trigger a redraw; EPC is now wrapped (not scrolled) on the Read Menu
    UNUSED(App);
    view_dispatcher_send_custom_event(App->ViewDispatcher, UHFReaderEventIdRedrawScreen);
}

/**
 * @brief      Callback for the saved text input screen.
 * @details    This function saves the current tag selected with all its info
 * @param      context  The UHFReaderApp - Used to change app variables.
*/
void uhf_reader_save_text_updated(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;
    bool Redraw = true;

    //Allocating FuriStrings to store each of the values associated with each UHF Tag
    FuriString* Tid = furi_string_alloc();
    FuriString* Mem = furi_string_alloc();
    FuriString* Res = furi_string_alloc();
    FuriString* Pc = furi_string_alloc();
    FuriString* Crc = furi_string_alloc();

    //Set the current EPC to save for the app based on the model's value
    with_view_model(
        App->ViewRead,
        UHFReaderConfigModel * model,
        {
            furi_string_set(model->EpcName, App->TempSaveBuffer);
            model->EpcName = App->EpcName;
            App->EpcToSave = (char*)furi_string_get_cstr(model->EpcValue);
        },
        Redraw);

    //Set the tid, User and Reserved memory values if any tags were read
    with_view_model(
        App->ViewEpc,
        UHFRFIDTagModel * model,
        {
            if(App->NumberOfEpcsToRead > 0) {
                furi_string_set(Tid, model->Tid);
                furi_string_set(Mem, model->User);
                furi_string_set(Res, model->Reserved);
                furi_string_set(Pc, model->Pc);
                furi_string_set(Crc, model->Crc);
            } else {
                furi_string_free(Tid);
                furi_string_free(Mem);
                furi_string_free(Res);
                furi_string_free(Pc);
                furi_string_free(Crc);
                return;
            }
        },
        Redraw);

    //Open the saved EPCS file if tags were read
    if(!flipper_format_file_open_append(App->EpcFile, APP_DATA_PATH("Saved_EPCs.txt"))) {
        FURI_LOG_E(TAG, "Failed to open file");
    }

    //Allocating some FuriStrings for use with the file
    FuriString* NumEpcs = furi_string_alloc();
    FuriString* EpcAndName = furi_string_alloc();

    //Increment the total number of saved tags and write this new tag with all its values to the epc_and_name FuriString
    App->NumberOfSavedTags++;
    furi_string_printf(NumEpcs, "Tag%ld", App->NumberOfSavedTags);
    furi_string_printf(
        EpcAndName,
        "%s:%s:%s:%s:%s:%s:%s",
        App->TempSaveBuffer,
        App->EpcToSave,
        furi_string_get_cstr(Tid),
        furi_string_get_cstr(Res),
        furi_string_get_cstr(Mem),
        furi_string_get_cstr(Pc),
        furi_string_get_cstr(Crc));

    //Attempt to write the string using the given format
    if(!flipper_format_write_string_cstr(
           App->EpcFile, furi_string_get_cstr(NumEpcs), furi_string_get_cstr(EpcAndName))) {
        FURI_LOG_E(TAG, "Failed to write to file");
        flipper_format_file_close(App->EpcFile);
    } else {
        //Add the new tag to the saved submenu
        submenu_add_item(
            App->SubmenuSaved,
            App->TempSaveBuffer,
            App->NumberOfSavedTags,
            uhf_reader_submenu_saved_callback,
            App);
        flipper_format_file_close(App->EpcFile);

        //Update the Index_File to have the new total number of saved tags and write to the file
        FuriString* NewNumEpcs = furi_string_alloc();
        furi_string_printf(NewNumEpcs, "%ld", App->NumberOfSavedTags);
        if(!flipper_format_file_open_existing(App->EpcIndexFile, APP_DATA_PATH("Index_File.txt"))) {
            FURI_LOG_E(TAG, "Failed to open index file");
        } else {
            if(!flipper_format_write_string_cstr(
                   App->EpcIndexFile, "Number of Tags", furi_string_get_cstr(NewNumEpcs))) {
                FURI_LOG_E(TAG, "Failed to write to file");
                flipper_format_file_close(App->EpcIndexFile);
            } else {
                flipper_format_file_close(App->EpcIndexFile);
            }
        }
        furi_string_free(NewNumEpcs);
    }

    //Freeing all the FuriStrings used
    furi_string_free(EpcAndName);
    furi_string_free(NumEpcs);
    furi_string_free(Res);
    furi_string_free(Tid);
    furi_string_free(Mem);
    furi_string_free(Pc);
    furi_string_free(Crc);
    dolphin_deed(DolphinDeedRfidSave);
    view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewRead);
}

/**
 * @brief      Callback for read screen input.
 * @details    This function is called when the user presses a button while on the read screen.
 * @param      event    The event - InputEvent object.
 * @param      context  The context - UHFReaderApp object.
 * @return     true if the event was handled, false otherwise.
*/
bool uhf_reader_view_read_input_callback(InputEvent* event, void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;
    if(event->key == InputKeyUp && !App->IsReading) {
        // Handle short press for save menu
        if(event->type == InputTypeShort) {
            //Setting the text input header
            text_input_set_header_text(App->SaveInput, "Save EPC");
            bool Redraw = false;
            with_view_model(
                App->ViewRead,
                UHFReaderConfigModel * model,
                {
                    //Copy the name contents from the text input
                    strncpy(
                        App->TempSaveBuffer,
                        furi_string_get_cstr(model->EpcName),
                        App->TempBufferSaveSize);
                },
                Redraw);

            //Set the text input result callback function
            bool ClearPreviousText = false;
            text_input_set_result_callback(
                App->SaveInput,
                uhf_reader_save_text_updated,
                App,
                App->TempSaveBuffer,
                App->TempBufferSaveSize,
                ClearPreviousText);
            view_set_previous_callback(
                text_input_get_view(App->SaveInput), uhf_reader_navigation_read_callback);
            view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewSaveInput);

            return true;
        }
        // Handle press and release for scrolling pause
        else if(event->type == InputTypePress || event->type == InputTypeRelease) {
            with_view_model(
                App->ViewRead,
                UHFReaderConfigModel * model,
                { model->IsScrolling = (event->type == InputTypePress); },
                true);
            return true;
        }
    }
    //Handles all short input types
    else if(event->type == InputTypeShort) {
        //If the user presses the left button while the app is not reading (M6E/M7E readers)
        if(event->key == InputKeyLeft && !App->IsReading && App->UHFModuleType != YRM100X_MODULE) {
            bool Redraw = true;

            with_view_model(
                App->ViewRead,
                UHFReaderConfigModel * model,
                {
                    //Check if there are any epcs that were read and decrement the current epc index to show the previous epc
                    if(App->NumberOfEpcsToRead > 1 && model->CurEpcIndex > 1) {
                        dolphin_deed(DolphinDeedNfcRead);
                        //TODO: Add checks for number of tids, res, and user mem read in case of short reads
                        model->CurEpcIndex -= 1;
                        App->CurTidIndex -= 1;
                        App->CurResIndex -= 1;
                        App->CurMemIndex -= 1;
                        furi_string_set_str(
                            model->EpcValue, App->EpcValues[model->CurEpcIndex * 26]);
                        model->NumEpcsRead = App->NumberOfEpcsToRead;
                    }
                },
                Redraw);

            //Updating the current TID, EPC, Reserved and User memory values to display on the view epc screen
            //TODO Add logic for PC and CRC values for the M6E/M7E readers
            with_view_model(
                App->ViewEpc,
                UHFRFIDTagModel * model,
                {
                    if(App->NumberOfEpcsToRead > 0) {
                        furi_string_set_str(model->Epc, App->EpcValues[App->CurTidIndex * 26]);
                    }
                    if(App->NumberOfTidsToRead > 0) {
                        furi_string_set_str(model->Tid, App->TidValues[App->CurTidIndex * 41]);
                    }
                    if(App->NumberOfResToRead > 0) {
                        furi_string_set_str(
                            model->Reserved, App->ResValues[App->CurResIndex * 17]);
                    }
                    if(App->NumberOfMemToRead > 0) {
                        furi_string_set_str(model->User, App->MemValues[App->CurMemIndex * 33]);
                    }
                },
                Redraw);

            return true;
        }
        //Increment the current epc index if the app is not reading
        else if(
            event->key == InputKeyRight && !App->IsReading &&
            App->UHFModuleType != YRM100X_MODULE) {
            bool Redraw = true;
            with_view_model(
                App->ViewRead,
                UHFReaderConfigModel * model,
                {
                    if(App->NumberOfEpcsToRead > 1 &&
                       model->CurEpcIndex < App->NumberOfEpcsToRead) {
                        //TODO: Add better bounds checking to ensure no crashes with short reads
                        model->CurEpcIndex += 1;
                        App->CurTidIndex += 1;
                        App->CurResIndex += 1;
                        App->CurMemIndex += 1;
                        furi_string_set_str(
                            model->EpcValue, App->EpcValues[model->CurEpcIndex * 26]);
                        model->NumEpcsRead = App->NumberOfEpcsToRead;
                    }
                },
                Redraw);

            //Updating the current TID, EPC, Reserved and User memory values to display on the view epc screen
            with_view_model(
                App->ViewEpc,
                UHFRFIDTagModel * model,
                {
                    if(App->NumberOfEpcsToRead > 0) {
                        furi_string_set_str(model->Epc, App->EpcValues[App->CurTidIndex * 26]);
                    }
                    if(App->NumberOfTidsToRead > 0) {
                        furi_string_set_str(model->Tid, App->TidValues[App->CurTidIndex * 41]);
                    }
                    if(App->NumberOfResToRead > 0) {
                        furi_string_set_str(
                            model->Reserved, App->ResValues[App->CurResIndex * 17]);
                    }
                    if(App->NumberOfMemToRead > 0) {
                        furi_string_set_str(model->User, App->MemValues[App->CurMemIndex * 33]);
                    }
                },
                Redraw);

            return true;
        }
        //YRM100: navigate to the previous collected EPC in the multi-tag list
        else if(
            event->key == InputKeyLeft && !App->IsReading &&
            App->UHFModuleType == YRM100X_MODULE) {
            bool Redraw = true;
            UHFTagWrapper* wrapper = App->YRM100XWorker->uhf_tag_wrapper;
            uint32_t idx = App->CurEpcIndex;
            if(App->NumberOfEpcsToRead > 1 && idx > 1 && idx <= wrapper->tag_count) {
                dolphin_deed(DolphinDeedNfcRead);
                idx -= 1;
                App->CurEpcIndex = idx;
                // New tag selected — reset deep-read state
                App->DeepReadDone = false;
                App->DeepReadTimerExpired = false;
                UHFTag* tag = wrapper->tags[idx - 1];
                char* TempEpc = convertToHexString(tag->epc->data, tag->epc->size);
                char* TempCrc = uint16_to_hex_string(tag->epc->crc);
                char* TempPc = uint16_to_hex_string(tag->epc->pc);

                with_view_model(
                    App->ViewRead,
                    UHFReaderConfigModel * model,
                    {
                        model->CurEpcIndex = idx;
                        model->NumEpcsRead = App->NumberOfEpcsToRead;
                        furi_string_set_str(model->EpcValue, TempEpc);
                        furi_string_set_str(model->Crc, TempCrc);
                        furi_string_set_str(model->Pc, TempPc);
                        model->ScrollOffset = 0;
                    },
                    Redraw);
                with_view_model(
                    App->ViewEpc,
                    UHFRFIDTagModel * model,
                    {
                        furi_string_set_str(model->Epc, TempEpc);
                        furi_string_set_str(model->Tid, "---");
                        furi_string_set_str(model->User, "---");
                        furi_string_set_str(model->Reserved, "---");
                        furi_string_set_str(model->Crc, TempCrc);
                        furi_string_set_str(model->Pc, TempPc);
                    },
                    Redraw);
                free(TempEpc);
                free(TempCrc);
                free(TempPc);
            }
            return true;
        }
        //YRM100: navigate to the next collected EPC in the multi-tag list
        else if(
            event->key == InputKeyRight && !App->IsReading &&
            App->UHFModuleType == YRM100X_MODULE) {
            bool Redraw = true;
            UHFTagWrapper* wrapper = App->YRM100XWorker->uhf_tag_wrapper;
            uint32_t idx = App->CurEpcIndex;
            if(App->NumberOfEpcsToRead > 1 && idx >= 1 && idx < wrapper->tag_count) {
                idx += 1;
                App->CurEpcIndex = idx;
                // New tag selected — reset deep-read state
                App->DeepReadDone = false;
                App->DeepReadTimerExpired = false;
                UHFTag* tag = wrapper->tags[idx - 1];
                char* TempEpc = convertToHexString(tag->epc->data, tag->epc->size);
                char* TempCrc = uint16_to_hex_string(tag->epc->crc);
                char* TempPc = uint16_to_hex_string(tag->epc->pc);

                with_view_model(
                    App->ViewRead,
                    UHFReaderConfigModel * model,
                    {
                        model->CurEpcIndex = idx;
                        model->NumEpcsRead = App->NumberOfEpcsToRead;
                        furi_string_set_str(model->EpcValue, TempEpc);
                        furi_string_set_str(model->Crc, TempCrc);
                        furi_string_set_str(model->Pc, TempPc);
                        model->ScrollOffset = 0;
                    },
                    Redraw);
                with_view_model(
                    App->ViewEpc,
                    UHFRFIDTagModel * model,
                    {
                        furi_string_set_str(model->Epc, TempEpc);
                        furi_string_set_str(model->Tid, "---");
                        furi_string_set_str(model->User, "---");
                        furi_string_set_str(model->Reserved, "---");
                        furi_string_set_str(model->Crc, TempCrc);
                        furi_string_set_str(model->Pc, TempPc);
                    },
                    Redraw);
                free(TempEpc);
                free(TempCrc);
                free(TempPc);
            }
            return true;
        }

        //If the down button is pressed, navigate to the EPC dump screen
        else if(event->key == InputKeyDown && !App->IsReading) {
            // Sync deep-read state into EPC dump model before switching
            with_view_model(
                App->ViewEpc,
                UHFRFIDTagModel * model,
                {
                    model->IsDeepReading = App->DeepReading;
                    model->DeepReadDone = App->DeepReadDone;
                },
                false);
            view_set_previous_callback(App->ViewEpc, uhf_reader_navigation_read_callback);
            view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewEpcDump);
            return true;
        }

    } else if(event->type == InputTypePress) {
        //Handles the start button being pressed
        if(event->key == InputKeyOk) {
            view_dispatcher_send_custom_event(App->ViewDispatcher, UHFReaderEventIdOkPressed);

            return true;
        }
    }

    return false;
}

/**
 * @brief      Callback when the user exits the read screen.
 * @details    This function is called when the user exits the read screen.
 * @param      context  The context - not used
 * @return     the view id of the next view.
*/
uint32_t uhf_reader_navigation_read_submenu_callback(void* context) {
    UNUSED(context);
    return UHFReaderViewSubmenu;
}

/**
 * @brief      Callback when the user exits the read screen.
 * @details    This function is called when the user exits the read screen.
 * @param      context  The context - UHFReaderApp object.
*/
void uhf_reader_view_read_exit_callback(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;

    if(App->UHFModuleType == YRM100X_MODULE && App->YRM100XWorker) {
        uhf_worker_stop(App->YRM100XWorker);
    }

    furi_timer_stop(App->Timer);
    furi_timer_free(App->Timer);
    App->Timer = NULL;
}

/**
 * @brief      Read enter callback function.
 * @details    This function is called when the view transitions to the read screen.
 * @param      context  The context - UHFReaderApp object.
*/
void uhf_reader_view_read_enter_callback(void* context) {
    //Grab the period for the timer
    uint32_t Period = furi_ms_to_ticks(300);
    UHFReaderApp* App = (UHFReaderApp*)context;
    dolphin_deed(DolphinDeedNfcRead);

    //Start the timer
    furi_assert(App->Timer == NULL);
    App->Timer =
        furi_timer_alloc(uhf_reader_view_read_timer_callback, FuriTimerTypePeriodic, context);
    furi_timer_start(App->Timer, Period);

    //Setting default reading states
    App->IsReading = false;
    with_view_model(
        App->ViewRead, UHFReaderConfigModel * model, { model->IsReading = false; }, true);
}

/**
 * @brief      Callback for the YRM100 worker
 * @details    This function is called when there is a worker event 
 * @param      event    The worker event  - UHFWorkerEvent event.
 * @param     ctx  The context - UHFReaderApp object. 
*/
void uhf_read_tag_worker_callback(UHFWorkerEvent event, void* ctx) {
    UHFReaderApp* App = (UHFReaderApp*)ctx;

    with_view_model(
        App->ViewRead,
        UHFReaderConfigModel * model,
        {
            uint32_t Len = strlen(model->ScrollingText);

            //Incrementing each offset
            model->ScrollOffset++;

            //Check the bounds of the offset and reset if necessary
            if(model->ScrollOffset >= Len) {
                model->ScrollOffset = 0;
            }
        },
        true);

    if(event == UHFWorkerEventSuccess || event == UHFWorkerEventNoTagDetected) {
        notification_message(App->Notifications, &uhf_sequence_blink_stop);
        if(event == UHFWorkerEventSuccess) {
            notification_message(App->Notifications, &sequence_success);
            dolphin_deed(DolphinDeedNfcReadSuccess);
        }
        view_dispatcher_send_custom_event(App->ViewDispatcher, UHFCustomEventWorkerExit);
    } else if(event == UHFWorkerEventCardDetected) {
        // Live update: a new unique EPC was appended to the wrapper during an active
        // multi-poll session. Refresh the # EPCs counter and current tag in real time.
        view_dispatcher_send_custom_event(App->ViewDispatcher, UHFCustomEventWorkerCardDetected);
    } else if(event == UHFWorkerEventAborted) {
        notification_message(App->Notifications, &uhf_sequence_blink_stop);
        view_dispatcher_send_custom_event(App->ViewDispatcher, UHFCustomEventWorkerExitAborted);
    }
}

/**
 * @brief      Worker callback for a deep-read of a single selected tag.
 * @details    On success it triggers navigation to the tag actions menu; any other
 *             outcome (abort/fail) returns to the read view.
 * @param      event  The worker event.
 * @param      ctx    The context - UHFReaderApp object.
*/
void uhf_deep_read_worker_callback(UHFWorkerEvent event, void* ctx) {
    UHFReaderApp* App = (UHFReaderApp*)ctx;
    // Treat both explicit success and timer-expired abort as "done" —
    // navigate to the banks screen with whatever data was captured.
    if(event == UHFWorkerEventSuccess ||
       (event == UHFWorkerEventAborted && App->DeepReadTimerExpired)) {
        if(event == UHFWorkerEventSuccess) {
            notification_message(App->Notifications, &sequence_success);
            dolphin_deed(DolphinDeedNfcReadSuccess);
        }
        App->DeepReadTimerExpired = false;
        view_dispatcher_send_custom_event(App->ViewDispatcher, UHFCustomEventDeepReadDone);
    } else {
        // User pressed Back — abort without navigating to banks
        notification_message(App->Notifications, &uhf_sequence_blink_stop);
        view_dispatcher_send_custom_event(App->ViewDispatcher, UHFCustomEventDeepReadAborted);
    }
}

/**
 * @brief      Callback for custom read events.
 * @details    This function is called when a custom event is sent to the view dispatcher.
 * @param      event    The event id - UHFReaderAppEventId value.
 * @param      context  The context - UHFReaderApp object.
 * @return     true if the event was handled, false otherwise. 
*/
bool uhf_reader_view_read_custom_event_callback(uint32_t event, void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;

    switch(event) {
    //Redraw the screen
    case UHFReaderEventIdRedrawScreen: {
        bool Redraw = true;
        with_view_model(App->ViewRead, UHFReaderConfigModel * _model, { UNUSED(_model); }, Redraw);
        return true;
    }

    //Handles a live tag detection during an active multi-poll session
    case UHFCustomEventWorkerCardDetected: {
        bool Redraw = true;
        UHFTagWrapper* wrapper = App->YRM100XWorker->uhf_tag_wrapper;
        size_t count = wrapper->tag_count;
        if(count == 0) {
            return true;
        }
        App->NumberOfEpcsToRead = count;

        // Show the most-recently discovered tag as it streams in
        UHFTag* latest = wrapper->tags[count - 1];
        char* TempEpc = convertToHexString(latest->epc->data, latest->epc->size);
        char* TempCrc = uint16_to_hex_string(latest->epc->crc);
        char* TempPc = uint16_to_hex_string(latest->epc->pc);

        with_view_model(
            App->ViewRead,
            UHFReaderConfigModel * _model,
            {
                furi_string_set_str(_model->EpcValue, TempEpc);
                _model->NumEpcsRead = (uint32_t)count;
                _model->CurEpcIndex = (uint32_t)count;
                furi_string_set_str(_model->Crc, TempCrc);
                furi_string_set_str(_model->Pc, TempPc);
            },
            Redraw);
        free(TempEpc);
        free(TempCrc);
        free(TempPc);
        return true;
    }

    //Handles the worker exiting after a multi-tag read
    case UHFCustomEventWorkerExit: {
        bool Redraw = true;
        App->IsReading = false;
        uhf_worker_stop(App->YRM100XWorker);

        UHFTagWrapper* wrapper = App->YRM100XWorker->uhf_tag_wrapper;
        size_t count = wrapper->tag_count;
        App->NumberOfEpcsToRead = count;
        App->CurEpcIndex = 1;
        App->CurTidIndex = 0;
        App->CurResIndex = 0;
        App->CurMemIndex = 0;

        if(count > 0) {
            UHFTag* first = wrapper->tags[0];
            char* TempEpc = convertToHexString(first->epc->data, first->epc->size);
            char* TempCrc = uint16_to_hex_string(first->epc->crc);
            char* TempPc = uint16_to_hex_string(first->epc->pc);

            with_view_model(
                App->ViewRead,
                UHFReaderConfigModel * _model,
                {
                    furi_string_set_str(_model->EpcValue, TempEpc);
                    _model->IsReading = false;
                    _model->NumEpcsRead = (uint32_t)count;
                    _model->CurEpcIndex = 1;
                    furi_string_set_str(_model->Crc, TempCrc);
                    furi_string_set_str(_model->Pc, TempPc);
                },
                Redraw);
            with_view_model(
                App->ViewEpc,
                UHFRFIDTagModel * _model,
                {
                    furi_string_set_str(_model->Epc, TempEpc);
                    furi_string_set_str(_model->Tid, "---");
                    furi_string_set_str(_model->User, "---");
                    furi_string_set_str(_model->Reserved, "---");
                    furi_string_set_str(_model->Crc, TempCrc);
                    furi_string_set_str(_model->Pc, TempPc);
                },
                Redraw);
            free(TempEpc);
            free(TempCrc);
            free(TempPc);
        } else {
            with_view_model(
                App->ViewRead,
                UHFReaderConfigModel * _model,
                {
                    furi_string_set_str(_model->EpcValue, "No tags found");
                    _model->IsReading = false;
                    _model->NumEpcsRead = 0;
                    _model->CurEpcIndex = 0;
                },
                Redraw);
        }
        return true;
    }

    //Handles the worker exiting after user pressed Stop
    case UHFCustomEventWorkerExitAborted: {
        bool Redraw = true;
        App->IsReading = false;
        with_view_model(
            App->ViewRead,
            UHFReaderConfigModel * _model,
            { _model->IsReading = false; },
            Redraw);
        return true;
    }

    //Deep-read of the selected multi-tag finished: populate banks and open tag actions
    case UHFCustomEventDeepReadDone: {
        // This event is now handled by the EPC dump view's custom callback.
        // It should not fire while the Read Menu is active.
        return false;
    }

    //Deep-read was aborted (back pressed / fail): stay on the read view
    case UHFCustomEventDeepReadAborted: {
        // This event is now handled by the EPC dump view's custom callback.
        return false;
    }

    //The ok button was pressed
    case UHFReaderEventIdOkPressed:

        //Check if the app is reading
        if(App->IsReading) {
            //Stop reading
            App->IsReading = false;
            notification_message(App->Notifications, &uhf_sequence_blink_stop);
            uhf_worker_stop(App->YRM100XWorker);
            with_view_model(
                App->ViewRead,
                UHFReaderConfigModel * model,
                { model->IsReading = App->IsReading; },
                true);
        } else {
            //Check if the reader is connected before sending a read command
            if(App->ReaderConnected) {
                App->IsReading = true;
                // Starting a new scan — reset deep-read state
                App->DeepReadDone = false;
                App->DeepReadTimerExpired = false;
                notification_message(App->Notifications, &uhf_sequence_blink_start_cyan);
                if(App->UHFModuleType == M6E_NANO_MODULE ||
                   App->UHFModuleType == M7E_HECTO_MODULE) {
                    //Send the read command to the RPi Zero via UART
                    uart_helper_send(App->UartHelper, "READ\n", 5);
                    with_view_model(
                        App->ViewRead,
                        UHFReaderConfigModel * model,
                        { model->IsReading = App->IsReading; },
                        true);
                } else {
                    with_view_model(
                        App->ViewRead,
                        UHFReaderConfigModel * model,
                        { model->IsReading = App->IsReading; },
                        true);
                    uhf_worker_start(
                        App->YRM100XWorker,
                        UHFWorkerStateDetectMultiple,
                        uhf_read_tag_worker_callback,
                        App);
                }
            }
        }
        return true;
    default:
        return false;
    }
}

/**
 * @brief      Allocates the saved input view.
 * @details    This function allocates all variables for the saved input view.
 * @param      context  The context - UHFReaderApp object.
*/
void saved_input_alloc(UHFReaderApp* App) {
    //Allocate a new text input component
    App->SaveInput = text_input_alloc();
    view_dispatcher_add_view(
        App->ViewDispatcher, UHFReaderViewSaveInput, text_input_get_view(App->SaveInput));

    //Setting the max size of the buffer
    App->TempBufferSaveSize = 150;
    App->TempSaveBuffer = (char*)malloc(App->TempBufferSaveSize);
}

/**
 * @brief      Allocates the read view.
 * @details    This function allocates all variables for the read view.
 * @param      context  The context - UHFReaderApp object.
*/
void view_read_alloc(UHFReaderApp* App) {
    //Allocating the view and setting all callback functions
    saved_input_alloc(App);
    App->ViewRead = view_alloc();
    view_set_draw_callback(App->ViewRead, uhf_reader_view_read_draw_callback);
    view_set_input_callback(App->ViewRead, uhf_reader_view_read_input_callback);
    view_set_previous_callback(App->ViewRead, uhf_reader_navigation_read_submenu_callback);
    view_set_enter_callback(App->ViewRead, uhf_reader_view_read_enter_callback);
    view_set_exit_callback(App->ViewRead, uhf_reader_view_read_exit_callback);
    view_set_context(App->ViewRead, App);
    view_set_custom_callback(App->ViewRead, uhf_reader_view_read_custom_event_callback);

    //Allocating the view model
    view_allocate_model(App->ViewRead, ViewModelTypeLockFree, sizeof(UHFReaderConfigModel));
    UHFReaderConfigModel* Model = view_get_model(App->ViewRead);
    FuriString* EpcValueDefault = furi_string_alloc();
    furi_string_set_str(EpcValueDefault, "Press Read");

    //Setting default values for the view model
    Model->Setting1Index = App->Setting1Index;
    Model->Setting2Power = App->Setting2PowerStr;
    Model->SettingReadAp = App->DefaultAccessPwdStr;
    Model->Setting3Index = App->Setting3Index;
    Model->Setting1Value = furi_string_alloc_set(App->Setting1Names[App->Setting1Index]);
    Model->Setting3Value = furi_string_alloc_set(App->Setting3Names[App->Setting3Index]);
    Model->Pc = furi_string_alloc_set("XXXX");
    Model->Crc = furi_string_alloc_set("XXXX");
    Model->EpcName = furi_string_alloc_set("Enter name");
    Model->ScrollOffset = 0;
    Model->ScrollingText = "Press Read";
    Model->EpcValue = EpcValueDefault;
    Model->CurEpcIndex = 1;
    Model->NumEpcsRead = 0;
    Model->IsReading = false;
    view_dispatcher_add_view(App->ViewDispatcher, UHFReaderViewRead, App->ViewRead);
}

/**
 * @brief      Frees the read view.
 * @details    This function frees all variables for the read view.
 * @param      context  The context - UHFReaderApp object.
*/
void view_read_free(UHFReaderApp* App) {
    view_dispatcher_remove_view(App->ViewDispatcher, UHFReaderViewSaveInput);
    text_input_free(App->SaveInput);
    free(App->TempSaveBuffer);
    view_dispatcher_remove_view(App->ViewDispatcher, UHFReaderViewRead);
    view_free(App->ViewRead);
}
