#include "view_epc_info.h"

// ─── Page indices ─────────────────────────────────────────────────────────────
#define PAGE_EPC   0
#define PAGE_TID   1
#define PAGE_USR   2
#define PAGE_RES   3
#define PAGE_COUNT 4

static const char* const PAGE_TITLES[PAGE_COUNT] = {"EPC", "TID", "USR", "RES"};

// [page][0=left label, 1=right label]
static const char* const PAGE_NAV[PAGE_COUNT][2] = {
    {"RES", "TID"},
    {"EPC", "USR"},
    {"TID", "RES"},
    {"USR", "EPC"},
};

// ─── Helper: draw wrapped 2-line value ───────────────────────────────────────
static void draw_value_wrapped(Canvas* canvas, const char* value, uint8_t y1, uint8_t y2) {
    const size_t CPL = 20; // chars per line
    size_t len = strlen(value);

    char l1[21];
    memset(l1, 0, sizeof(l1));
    memcpy(l1, value, len < CPL ? len : CPL);
    canvas_draw_str(canvas, 0, y1, l1);

    if(len > CPL) {
        char l2[21];
        memset(l2, 0, sizeof(l2));
        size_t rem = len - CPL;
        memcpy(l2, value + CPL, rem < CPL ? rem : CPL);
        canvas_draw_str(canvas, 0, y2, l2);
    }
}

// ─── Draw callback ────────────────────────────────────────────────────────────
void uhf_reader_view_epc_info_draw_callback(Canvas* canvas, void* model) {
    UHFRFIDTagModel* m = (UHFRFIDTagModel*)model;
    uint32_t page = m->CurrentBank % PAGE_COUNT;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    // Bold bank name top-left
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 11, PAGE_TITLES[page]);
    canvas_set_font(canvas, FontSecondary);

    // Bank value wrapped across two lines
    const char* value = "---";
    switch(page) {
    case PAGE_EPC: value = furi_string_get_cstr(m->Epc);      break;
    case PAGE_TID: value = furi_string_get_cstr(m->Tid);      break;
    case PAGE_USR: value = furi_string_get_cstr(m->User);     break;
    case PAGE_RES: value = furi_string_get_cstr(m->Reserved); break;
    default:       break;
    }
    draw_value_wrapped(canvas, value, 22, 33);

    // EPC page: CRC + PC on row 3
    if(page == PAGE_EPC) {
        canvas_draw_str(canvas, 4,  44, "CRC:");
        canvas_draw_str(canvas, 28, 44, furi_string_get_cstr(m->Crc));
        canvas_draw_str(canvas, 70, 44, "PC:");
        canvas_draw_str(canvas, 88, 44, furi_string_get_cstr(m->Pc));
    }

    // Left / right navigation hints
    elements_button_left(canvas,  PAGE_NAV[page][0]);
    elements_button_right(canvas, PAGE_NAV[page][1]);
}

// ─── Input handler ────────────────────────────────────────────────────────────
bool uhf_reader_view_epc_info_input_callback(InputEvent* event, void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;

    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyLeft) {
        with_view_model(
            App->ViewEpcInfo,
            UHFRFIDTagModel * m,
            { m->CurrentBank = (m->CurrentBank + PAGE_COUNT - 1) % PAGE_COUNT; },
            true);
        return true;
    }

    if(event->key == InputKeyRight) {
        with_view_model(
            App->ViewEpcInfo,
            UHFRFIDTagModel * m,
            { m->CurrentBank = (m->CurrentBank + 1) % PAGE_COUNT; },
            true);
        return true;
    }

    return false;
}

// ─── Navigation callback ──────────────────────────────────────────────────────
uint32_t uhf_reader_navigation_exit_epc_info_callback(void* context) {
    UNUSED(context);
    return UHFReaderViewTagAction;
}

// ─── Timer callback (kept as dead stub for link compatibility) ────────────────
void uhf_reader_view_epc_info_timer_callback(void* context) {
    UNUSED(context);
}

// ─── Enter callback ───────────────────────────────────────────────────────────
void uhf_reader_view_epc_info_enter_callback(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;

    // Always start on the EPC page
    with_view_model(
        App->ViewEpcInfo,
        UHFRFIDTagModel * m,
        { m->CurrentBank = PAGE_EPC; },
        false);

    // Load saved tag data from file
    FuriString* TempStr = furi_string_alloc();
    FuriString* TempTag = furi_string_alloc();

    if(flipper_format_file_open_existing(App->EpcFile, APP_DATA_PATH("Saved_EPCs.txt"))) {
        furi_string_printf(TempStr, "Tag%ld", App->SelectedTagIndex);
        if(flipper_format_read_string(
               App->EpcFile, furi_string_get_cstr(TempStr), TempTag)) {
            const char* s = furi_string_get_cstr(TempTag);
            char* xepc = extract_epc(s);
            char* xtid = extract_tid(s);
            char* xres = extract_res(s);
            char* xmem = extract_mem(s);
            char* xcrc = extract_crc(s);
            char* xpc  = extract_pc(s);
            with_view_model(
                App->ViewEpcInfo,
                UHFRFIDTagModel * m,
                {
                    if(xepc) furi_string_set_str(m->Epc,      xepc);
                    if(xtid) furi_string_set_str(m->Tid,      xtid);
                    if(xres) furi_string_set_str(m->Reserved, xres);
                    if(xmem) furi_string_set_str(m->User,     xmem);
                    if(xcrc) furi_string_set_str(m->Crc,      xcrc);
                    if(xpc)  furi_string_set_str(m->Pc,       xpc);
                },
                true);
            if(xepc) free(xepc);
            if(xtid) free(xtid);
            if(xres) free(xres);
            if(xmem) free(xmem);
            if(xcrc) free(xcrc);
            if(xpc)  free(xpc);
        }
        flipper_format_file_close(App->EpcFile);
    }

    furi_string_free(TempStr);
    furi_string_free(TempTag);
}

// ─── Exit callback ────────────────────────────────────────────────────────────
void uhf_reader_view_epc_info_exit_callback(void* context) {
    UNUSED(context);
    // No timer to clean up.
}

// ─── Alloc / Free ─────────────────────────────────────────────────────────────
void view_epc_info_alloc(UHFReaderApp* App) {
    App->ViewEpcInfo = view_alloc();
    view_set_draw_callback(App->ViewEpcInfo,     uhf_reader_view_epc_info_draw_callback);
    view_set_input_callback(App->ViewEpcInfo,    uhf_reader_view_epc_info_input_callback);
    view_set_previous_callback(App->ViewEpcInfo, uhf_reader_navigation_exit_epc_info_callback);
    view_set_enter_callback(App->ViewEpcInfo,    uhf_reader_view_epc_info_enter_callback);
    view_set_exit_callback(App->ViewEpcInfo,     uhf_reader_view_epc_info_exit_callback);
    view_set_context(App->ViewEpcInfo, App);

    view_allocate_model(App->ViewEpcInfo, ViewModelTypeLockFree, sizeof(UHFRFIDTagModel));
    UHFRFIDTagModel* m = view_get_model(App->ViewEpcInfo);
    m->Epc        = furi_string_alloc_set("---");
    m->Tid        = furi_string_alloc_set("---");
    m->User       = furi_string_alloc_set("---");
    m->Reserved   = furi_string_alloc_set("---");
    m->Crc        = furi_string_alloc_set("----");
    m->Pc         = furi_string_alloc_set("----");
    m->CurrentBank = PAGE_EPC;

    view_dispatcher_add_view(App->ViewDispatcher, UHFReaderViewEpcInfo, App->ViewEpcInfo);
}

void view_epc_info_free(UHFReaderApp* App) {
    view_dispatcher_remove_view(App->ViewDispatcher, UHFReaderViewEpcInfo);
    view_free(App->ViewEpcInfo);
}
