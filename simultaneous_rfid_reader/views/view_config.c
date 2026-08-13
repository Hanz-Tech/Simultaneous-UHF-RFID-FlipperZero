#include "view_config.h"
#include "../helpers/uhf_reader_settings.h"

static void uhf_reader_settings_snapshot(
    const UHFReaderApp* App,
    UHFReaderSettings* settings) {
    settings->reader_profile_initialized = App->SettingsInitialized;
    settings->save_on_write_index = App->SettingSavingIndex;
    settings->region_index = App->SettingRegionIndex;
    settings->power_dbm = App->SettingPowerIndex;
    settings->session_index = App->SettingSessionIndex;
    settings->target_index = App->SettingTargetIndex;
    settings->default_access_password = bytes_to_uint32(App->ApTempBuffer, 4);
}

static bool uhf_reader_settings_save_current(UHFReaderApp* App) {
    UHFReaderSettings settings;
    uhf_reader_settings_snapshot(App, &settings);
    if(!uhf_reader_settings_save(App->TagStorage, &settings)) {
        FURI_LOG_E(TAG, "Failed to save reader settings");
        return false;
    }
    return true;
}

static uint8_t uhf_reader_region_index(WorkingRegion region) {
    switch(region) {
    case WR_US:
        return USA_REGION;
    case WR_EU:
        return EU_REGION;
    case WR_KOREA:
        return KOREA_REGION;
    case WR_CHINA_800:
        return CHINA_800_REGION;
    case WR_CHINA_900:
        return CHINA_900_REGION;
    default:
        return USA_REGION;
    }
}

static bool uhf_reader_settings_adopt_yrm100x(UHFReaderApp* App) {
    WorkingRegion region;
    uint16_t power_raw = 0;
    uint8_t session = 0;
    uint8_t target = 0;
    if(!m100_get_working_region(App->YRM100XWorker->module, &region) ||
       !m100_get_transmitting_power(App->YRM100XWorker->module, &power_raw) ||
       !m100_get_query_params(App->YRM100XWorker->module, &session, &target)) {
        return false;
    }

    uint32_t power_dbm = (power_raw + 50U) / 100U;
    if(power_dbm < UHF_READER_MIN_POWER_DBM || power_dbm > UHF_READER_MAX_POWER_DBM) {
        return false;
    }
    App->SettingRegionIndex = uhf_reader_region_index(region);
    App->UHFRegionType = App->SettingRegionIndex;
    App->SettingPowerIndex = (uint8_t)power_dbm;
    App->SettingSessionIndex = session;
    App->SettingTargetIndex = target;
    App->SettingsInitialized = true;
    App->YRM100XWorker->DefaultAP = bytes_to_uint32(App->ApTempBuffer, 4);
    if(!uhf_reader_settings_save_current(App)) {
        App->SettingsInitialized = false;
        return false;
    }
    return true;
}

static bool uhf_reader_settings_apply_yrm100x(UHFReaderApp* App) {
    WorkingRegion expected_region = WORKING_REGIONS[App->SettingRegionIndex];
    WorkingRegion actual_region;
    uint16_t expected_power = (uint16_t)App->SettingPowerIndex * 100U;
    uint16_t actual_power = 0;
    uint8_t actual_session = 0;
    uint8_t actual_target = 0;

    if(!m100_set_working_region(App->YRM100XWorker->module, expected_region) ||
       !m100_get_working_region(App->YRM100XWorker->module, &actual_region) ||
       actual_region != expected_region ||
       !m100_set_transmitting_power(App->YRM100XWorker->module, expected_power) ||
       !m100_get_transmitting_power(App->YRM100XWorker->module, &actual_power) ||
       actual_power != expected_power ||
       !m100_set_query_params(
           App->YRM100XWorker->module,
           App->SettingSessionIndex,
           App->SettingTargetIndex) ||
       !m100_get_query_params(
           App->YRM100XWorker->module, &actual_session, &actual_target) ||
       actual_session != App->SettingSessionIndex || actual_target != App->SettingTargetIndex) {
        return false;
    }

    App->UHFRegionType = App->SettingRegionIndex;
    App->YRM100XWorker->DefaultAP = bytes_to_uint32(App->ApTempBuffer, 4);
    return true;
}

/**
 * @brief      Callback for returning to the configuration screen.
 * @details    This function is called when user press back button.
 * @param      context  The context - unused
*/
static void locked_popup_back_callback(void* context) {
    UHFReaderApp* app = context;
    view_dispatcher_switch_to_view(app->ViewDispatcher, UHFReaderViewConfigure);
}

/**
 * @brief      Shows a notification when the reader is not connected
 * @details    This function shows a notification when the reader is not connected and the user tries to change a setting.
 * @param      App  The UHFReaderApp - used to allocate app variables and views.
*/
static void show_locked_notification(UHFReaderApp* App) {
    notification_message(App->Notifications, &sequence_error);

    Popup* popup = App->LockPopup;
    popup_reset(popup);
    popup_set_header(popup, "Connect\nTo Reader\nFirst!", 68, 30, AlignLeft, AlignTop);
    popup_set_icon(popup, 0, 3, &I_WarningDolphin_45x42);

    // Set timeout for 2 seconds
    popup_enable_timeout(popup);
    popup_set_timeout(popup, 2000);
    popup_set_context(popup, App);
    popup_set_callback(popup, locked_popup_back_callback);

    view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewLockPopup);
}

static void show_command_error_notification(UHFReaderApp* App) {
    notification_message(App->Notifications, &sequence_error);

    Popup* popup = App->LockPopup;
    popup_reset(popup);
    popup_set_header(popup, "Command\nFailed!", 68, 30, AlignLeft, AlignTop);
    popup_set_icon(popup, 0, 3, &I_WarningDolphin_45x42);

    popup_enable_timeout(popup);
    popup_set_timeout(popup, 2000);
    popup_set_context(popup, App);
    popup_set_callback(popup, locked_popup_back_callback);

    view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewLockPopup);
}

/**
 * @brief      Callback for returning to submenu.
 * @details    This function is called when user press back button.
 * @param      context  The context - unused
 * @return     next view id
*/
uint32_t uhf_reader_navigation_config_submenu_callback(void* context) {
    UNUSED(context);
    return UHFReaderViewSubmenu;
}

/**
 * @brief      Allocates the configuration menu
 * @details    This function allocates all views and variables related to the configuration menu.
 * @param      app  The UHFReaderApp - used to allocate app variables and views.
*/
void view_config_alloc(UHFReaderApp* App) {
    ap_menu_alloc(App);
    UHFReaderSettings settings;
    uhf_reader_settings_set_defaults(&settings);
    uhf_reader_settings_load(App->TagStorage, &settings);
    App->SettingsInitialized = settings.reader_profile_initialized;
    App->ApTempBuffer[0] = (uint8_t)(settings.default_access_password >> 24);
    App->ApTempBuffer[1] = (uint8_t)(settings.default_access_password >> 16);
    App->ApTempBuffer[2] = (uint8_t)(settings.default_access_password >> 8);
    App->ApTempBuffer[3] = (uint8_t)settings.default_access_password;

    //Creating the variable item list
    App->VariableItemListConfig = variable_item_list_alloc();
    variable_item_list_reset(App->VariableItemListConfig);

    //Initializing configuration setting variables
    App->Setting1Values[0] = 1;
    App->Setting1Values[1] = 2;
    App->Setting1Names[0] = "Disconnected";
    App->Setting1Names[1] = "Connected";
    App->ReaderConnected = false;
    App->Setting1ConfigLabel = "Connection";
    App->Setting2ConfigLabel = "Power Level";
    App->Setting3Values[0] = 1;
    App->Setting3Values[1] = 2;
    App->Setting3Names[0] = "Internal";
    App->Setting3Names[1] = "External";
    App->Setting3ConfigLabel = "Antenna";

    //Baud rate values (just for YRM100 Module for now!)
    App->SettingBaudValues[0] = 1;
    App->SettingBaudValues[1] = 2;
    App->SettingBaudValues[2] = 3;
    App->SettingBaudNames[0] = "9600";
    App->SettingBaudNames[2] = "384000";
    App->SettingBaudNames[1] = "115200";
    App->SettingBaudConfigLabel = "Baud Rate";
    App->UHFBaudRate = 115200;

    //Setting the module values for the config menu
    App->SettingModuleValues[0] = 1;
    App->SettingModuleValues[1] = 2;
    App->SettingModuleValues[2] = 3;
    App->SettingModuleNames[0] = "YRM100";
    App->SettingModuleNames[1] = "M6e";
    App->SettingModuleNames[2] = "M7e";
    App->SettingModuleConfigLabel = "UHF Module";
    App->UHFModuleType = YRM100X_MODULE;

    App->SettingSavingValues[0] = 1;
    App->SettingSavingValues[1] = 2;
    App->SettingSavingNames[0] = "No";
    App->SettingSavingNames[1] = "Yes";
    App->SettingSavingConfigLabel = "Save on Write";
    App->SettingSavingIndex = settings.save_on_write_index;
    App->UHFSaveType =
        App->SettingSavingIndex == 1 ? YES_SAVE_ON_WRITE : NO_SAVE_ON_WRITE;

    //Setting the available regions
    App->SettingRegionValues[0] = 1;
    App->SettingRegionValues[1] = 2;
    App->SettingRegionValues[2] = 3;
    App->SettingRegionValues[3] = 4;
    App->SettingRegionValues[4] = 5;
    App->SettingRegionNames[0] = "USA";
    App->SettingRegionNames[1] = "EU";
    App->SettingRegionNames[2] = "Korea";
    App->SettingRegionNames[3] = "China 800";
    App->SettingRegionNames[4] = "China 900";
    App->SettingRegionConfigLabel = "Region";
    App->SettingRegionIndex = settings.region_index;
    App->UHFRegionType = App->SettingRegionIndex;

    //Setting the config menu labels for the default read access password
    App->ReadAccessPasswordLabel = strdup("Default AP");
    App->AccessPasswordPlaceHolder = strdup("Enter Access Password!");
    App->DefaultAccessPassword = malloc(9);
    snprintf(
        App->DefaultAccessPassword,
        9,
        "%08lX",
        (unsigned long)settings.default_access_password);

    // Add setting 1 to variable item list
    VariableItem* Item = variable_item_list_add(
        App->VariableItemListConfig,
        App->Setting1ConfigLabel,
        COUNT_OF(App->Setting1Values),
        uhf_reader_setting_1_change,
        App);

    //Creating the default index for setting one which is the connection status
    App->Setting1Index = 0;
    variable_item_set_current_value_index(Item, App->Setting1Index);
    variable_item_set_current_value_text(Item, App->Setting1Names[App->Setting1Index]);

    // Store module selection reference
    App->ModuleSelectionItem = variable_item_list_add(
        App->VariableItemListConfig,
        App->SettingModuleConfigLabel,
        COUNT_OF(App->SettingModuleValues),
        uhf_reader_module_setting_change,
        App);

    //Default index for the module selection option
    App->SettingModuleIndex = 0;
    variable_item_set_current_value_index(App->ModuleSelectionItem, App->SettingModuleIndex);
    variable_item_set_current_value_text(
        App->ModuleSelectionItem, App->SettingModuleNames[App->SettingModuleIndex]);

    // Add other items and track them for locking
    //Setting the available sessions (S0-S3, default S2)
    App->SettingSessionNames[0] = "S0";
    App->SettingSessionNames[1] = "S1";
    App->SettingSessionNames[2] = "S2";
    App->SettingSessionNames[3] = "S3";
    App->SettingSessionConfigLabel = "Session";
    App->SettingSessionIndex = settings.session_index;

    //Setting the available targets (A/B, default A)
    App->SettingTargetNames[0] = "A";
    App->SettingTargetNames[1] = "B";
    App->SettingTargetConfigLabel = "Target";
    App->SettingTargetIndex = settings.target_index;

    App->num_items = 7; // Adjust based on total number of lockable items
    App->item_locks = malloc(sizeof(VariableItemLock) * 7);

    // Initialize all items as locked except module and save
    for(size_t i = 0; i < App->num_items; i++) {
        App->item_locks[i].locked = true;
    }

    // YRM1002 supports 15-26 dBm. Keep index==dBm so existing M6E/M7E callbacks
    // retain their current representation while YRM validation rejects lower values.
    App->SettingPowerIndex = settings.power_dbm;
    App->Setting2Item = variable_item_list_add(
        App->VariableItemListConfig, App->Setting2ConfigLabel, 27, uhf_reader_power_setting_change, App);
    variable_item_list_set_enter_callback(
        App->VariableItemListConfig, uhf_reader_setting_item_clicked, App);
    variable_item_set_current_value_index(App->Setting2Item, App->SettingPowerIndex);
    variable_item_set_current_value_text(App->Setting2Item, "LOCKED");

    App->BaudSelection = variable_item_list_add(
        App->VariableItemListConfig,
        App->SettingBaudConfigLabel,
        COUNT_OF(App->SettingBaudValues),
        uhf_reader_baud_setting_change,
        App);

    //Default index for the baud selection option
    App->SettingBaudIndex = 1;
    variable_item_set_current_value_index(App->BaudSelection, App->SettingBaudIndex);
    variable_item_set_current_value_text(App->BaudSelection, "LOCKED");

    App->RegionSelection = variable_item_list_add(
        App->VariableItemListConfig,
        App->SettingRegionConfigLabel,
        COUNT_OF(App->SettingRegionValues),
        uhf_reader_region_setting_change,
        App);

    variable_item_set_current_value_index(App->RegionSelection, App->SettingRegionIndex);
    variable_item_set_current_value_text(App->RegionSelection, "LOCKED");
    //Default access password input for reading and writing to the tag, or locking
    App->DefaultAccessPwdStr = furi_string_alloc_set(App->DefaultAccessPassword);
    App->SettingApPwdItem = variable_item_list_add(
        App->VariableItemListConfig, App->ReadAccessPasswordLabel, 1, NULL, NULL);
    variable_item_list_set_enter_callback(
        App->VariableItemListConfig, uhf_reader_setting_item_clicked, App);
    variable_item_set_current_value_text(App->SettingApPwdItem, "LOCKED");
    VariableItem* SavingSelection = variable_item_list_add(
        App->VariableItemListConfig,
        App->SettingSavingConfigLabel,
        COUNT_OF(App->SettingSavingValues),
        uhf_reader_save_setting_change,
        App);

    variable_item_set_current_value_index(SavingSelection, App->SettingSavingIndex);
    variable_item_set_current_value_text(
        SavingSelection, App->SettingSavingNames[App->SettingSavingIndex]);

    // Add setting 3 to variable item list
    App->AntennaSelection = variable_item_list_add(
        App->VariableItemListConfig,
        App->Setting3ConfigLabel,
        COUNT_OF(App->Setting3Values),
        uhf_reader_setting_3_change,
        App);

    //Default index for the antenna selection option
    App->Setting3Index = 0;
    variable_item_set_current_value_index(App->AntennaSelection, App->Setting3Index);
    variable_item_set_current_value_text(App->AntennaSelection, "LOCKED");

    App->SessionSelection = variable_item_list_add(
        App->VariableItemListConfig,
        App->SettingSessionConfigLabel,
        COUNT_OF(App->SettingSessionNames),
        uhf_reader_session_setting_change,
        App);
    variable_item_set_current_value_index(App->SessionSelection, App->SettingSessionIndex);
    variable_item_set_current_value_text(App->SessionSelection, "LOCKED");

    App->TargetSelection = variable_item_list_add(
        App->VariableItemListConfig,
        App->SettingTargetConfigLabel,
        COUNT_OF(App->SettingTargetNames),
        uhf_reader_target_setting_change,
        App);
    variable_item_set_current_value_index(App->TargetSelection, App->SettingTargetIndex);
    variable_item_set_current_value_text(App->TargetSelection, "LOCKED");
    //Setting previous callback
    view_set_previous_callback(
        variable_item_list_get_view(App->VariableItemListConfig),
        uhf_reader_navigation_config_submenu_callback);
    view_dispatcher_add_view(
        App->ViewDispatcher,
        UHFReaderViewConfigure,
        variable_item_list_get_view(App->VariableItemListConfig));
}

/**
 * @brief      Callback for returning to configure screen.
 * @details    This function is called when user press back button.
 * @param      context  The context - unused
 * @return     next view id
*/
uint32_t uhf_reader_navigation_configure_callback(void* context) {
    UNUSED(context);
    return UHFReaderViewConfigure;
}

/**
 * @brief      Handles the connection setting
 * @details    Attempts to connect/disconnect from the reader.
 * @param      item  VariableItem - the current selection for connect values.
*/
void uhf_reader_setting_1_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);

    if(!App->ReaderConnected) {
        bool connected = true;
        if(App->UHFModuleType == YRM100X_MODULE) {
            connected = false;
            // M100/QM100 wake consumes the wake byte and may drop the first command
            // after wake. Three idempotent attempts cover both losses and the real
            // profile sync without requiring another UI toggle.
            for(uint8_t attempt = 0; attempt < 3 && !connected; attempt++) {
                connected = App->SettingsInitialized ? uhf_reader_settings_apply_yrm100x(App) :
                                                       uhf_reader_settings_adopt_yrm100x(App);
            }
        } else {
            uart_helper_send(App->UartHelper, "C\n", 2);
        }

        if(!connected) {
            Index = 0;
            variable_item_set_current_value_index(Item, Index);
            show_command_error_notification(App);
        } else {
            App->ReaderConnected = true;
            for(size_t i = 0; i < App->num_items; i++) {
                App->item_locks[i].locked = false;
            }

            App->SettingBaudIndex = 1;
            App->Setting3Index = 0;
            variable_item_set_current_value_index(
                App->Setting2Item, App->SettingPowerIndex);
            variable_item_set_current_value_index(
                App->RegionSelection, App->SettingRegionIndex);
            variable_item_set_current_value_index(
                App->BaudSelection, App->SettingBaudIndex);
            variable_item_set_current_value_index(
                App->AntennaSelection, App->Setting3Index);
            variable_item_set_current_value_index(
                App->SessionSelection, App->SettingSessionIndex);
            variable_item_set_current_value_index(
                App->TargetSelection, App->SettingTargetIndex);

            char power_label[8];
            snprintf(power_label, sizeof(power_label), "%ddBm", App->SettingPowerIndex);
            variable_item_set_current_value_text(App->Setting2Item, power_label);
            variable_item_set_current_value_text(
                App->BaudSelection, App->SettingBaudNames[App->SettingBaudIndex]);
            variable_item_set_current_value_text(
                App->RegionSelection, App->SettingRegionNames[App->SettingRegionIndex]);
            variable_item_set_current_value_text(
                App->SettingApPwdItem, furi_string_get_cstr(App->DefaultAccessPwdStr));
            variable_item_set_current_value_text(
                App->AntennaSelection, App->Setting3Names[App->Setting3Index]);
            variable_item_set_current_value_text(
                App->SessionSelection, App->SettingSessionNames[App->SettingSessionIndex]);
            variable_item_set_current_value_text(
                App->TargetSelection, App->SettingTargetNames[App->SettingTargetIndex]);
            variable_item_list_set_enter_callback(
                App->VariableItemListConfig, uhf_reader_setting_item_clicked, App);
        }
    } else {
        if(App->UHFModuleType != YRM100X_MODULE) {
            uart_helper_send(App->UartHelper, "D\n", 2);
        }
        App->ReaderConnected = false;
        for(size_t i = 0; i < App->num_items; i++) {
            App->item_locks[i].locked = true;
        }
        variable_item_set_current_value_text(App->Setting2Item, "LOCKED");
        variable_item_set_current_value_text(App->BaudSelection, "LOCKED");
        variable_item_set_current_value_text(App->RegionSelection, "LOCKED");
        variable_item_set_current_value_text(App->SettingApPwdItem, "LOCKED");
        variable_item_set_current_value_text(App->AntennaSelection, "LOCKED");
        variable_item_set_current_value_text(App->SessionSelection, "LOCKED");
        variable_item_set_current_value_text(App->TargetSelection, "LOCKED");
    }

    variable_item_set_current_value_text(Item, App->Setting1Names[Index]);
    UHFReaderConfigModel* ModelRead = view_get_model(App->ViewRead);
    ModelRead->Setting1Index = Index;
    furi_string_set(ModelRead->Setting1Value, App->Setting1Names[Index]);
    UHFReaderWriteModel* ModelWrite = view_get_model(App->ViewWrite);
    ModelWrite->Setting1Index = Index;
    furi_string_set(ModelWrite->Setting1Value, App->Setting1Names[Index]);
}

/**
 * @brief Handles power level changes. YRM1002 supports 15-26 dBm.
 * @param Item The VariableItem that was changed.
*/
void uhf_reader_power_setting_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);
    if(App->item_locks[0].locked) {
        show_locked_notification(App);
        variable_item_set_current_value_index(Item, App->SettingPowerIndex);
        char power_label[8];
        snprintf(power_label, sizeof(power_label), "%ddBm", App->SettingPowerIndex);
        variable_item_set_current_value_text(Item, power_label);
        return;
    }
    if(App->UHFModuleType == YRM100X_MODULE &&
       (Index < UHF_READER_MIN_POWER_DBM || Index > UHF_READER_MAX_POWER_DBM)) {
        variable_item_set_current_value_index(Item, App->SettingPowerIndex);
        show_command_error_notification(App);
        return;
    }
    if(App->UHFModuleType == YRM100X_MODULE) {
        if(!m100_set_transmitting_power(App->YRM100XWorker->module, (uint16_t)Index * 100)) {
            variable_item_set_current_value_index(Item, App->SettingPowerIndex);
            char power_label[8];
            snprintf(power_label, sizeof(power_label), "%ddBm", App->SettingPowerIndex);
            variable_item_set_current_value_text(Item, power_label);
            show_command_error_notification(App);
            return;
        }
    }
    App->SettingPowerIndex = Index;
    char power_label[8];
    snprintf(power_label, sizeof(power_label), "%ddBm", Index);
    variable_item_set_current_value_text(Item, power_label);
    if(!uhf_reader_settings_save_current(App)) show_command_error_notification(App);
}

/**
 * @brief      Handles setting the default access password
 * @details    This function handles setting the default access password for reading, writing, and locking actions 
 * @param      context The UHFReaderApp app - used to allocate app variables and views.
*/
void uhf_reader_setting_6_text_updated(void* context) {
    UHFReaderApp* App = (UHFReaderApp*)context;
    char value[9];
    snprintf(
        value,
        sizeof(value),
        "%02X%02X%02X%02X",
        App->ApTempBuffer[0],
        App->ApTempBuffer[1],
        App->ApTempBuffer[2],
        App->ApTempBuffer[3]);

    furi_string_set_str(App->DefaultAccessPwdStr, value);
    memcpy(App->DefaultAccessPassword, value, sizeof(value));
    with_view_model(
        App->ViewRead,
        UHFReaderConfigModel * Model,
        { furi_string_set_str(Model->SettingReadAp, value); },
        true);
    variable_item_set_current_value_text(App->SettingApPwdItem, value);

    if(App->UHFModuleType == YRM100X_MODULE) {
        App->YRM100XWorker->DefaultAP = bytes_to_uint32(App->ApTempBuffer, 4);
    } else {
        //TODO: ADD SUPPORT FOR M6E and M7E
        uart_helper_send(App->UartHelper, "SETPWD\n", 7);
        uart_helper_send_string(App->UartHelper, App->DefaultAccessPwdStr);
    }
    if(!uhf_reader_settings_save_current(App)) show_command_error_notification(App);
    view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewConfigure);
}

/**
 * @brief      Handles the Antenna Selection
 * @details    This function is a place holder for future functionality.
 * @param      item  VariableItem - the current selection for antenna values.
*/
void uhf_reader_setting_3_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);
    if(App->item_locks[4].locked) {
        show_locked_notification(App);
        return;
    }
    if(Index == 1) {
        uart_helper_send(App->UartHelper, "External\n", 9);
        //TODO: ADD SUPPORT FOR DIFFERENT ANTENNA TYPES AFTER HARDWARE DEVELOPED!
    } else {
        uart_helper_send(App->UartHelper, "Internal\n", 9);
    }

    //TODO: WAIT FOR ACK AND THEN SET TEXT VALUE
    variable_item_set_current_value_text(Item, App->Setting3Names[Index]);

    //Updating the antenna value for the read screen
    UHFReaderConfigModel* ModelRead = view_get_model(App->ViewRead);
    ModelRead->Setting3Index = Index;
    furi_string_set(ModelRead->Setting3Value, App->Setting3Names[Index]);

    //Updating the value of the antenna mode for the write screen
    UHFReaderWriteModel* ModelWrite = view_get_model(App->ViewWrite);
    ModelWrite->Setting3Index = Index;
    furi_string_set(ModelWrite->Setting3Value, App->Setting3Names[Index]);
}

/**
 * @brief      Handles the UHF Reader Module Selection
 * @details    This function handles switching between different supported UHF Readers like the YRM100X, M6e, and M7e Nano RFID modules.
 * @param      item  VariableItem - the current selection for the UHF Module.
*/
void uhf_reader_module_setting_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);

    variable_item_set_current_value_text(Item, App->SettingModuleNames[Index]);

    if(Index == 0) {
        //Mark the YRM100X as being used
        App->UHFModuleType = YRM100X_MODULE;

        //Free regular uart worker when switching from M6e/M7e.
        if(App->UartHelper) {
            uart_helper_free(App->UartHelper);
            App->UartHelper = NULL;
        }

        //Create a UART worker for the YRM100X module if needed.
        if(!App->YRM100XWorker) {
            App->YRM100XWorker = uhf_worker_alloc();
            UHFTagWrapper* WorkerTagWrapper = uhf_tag_wrapper_alloc();
            App->YRM100XWorker->uhf_tag_wrapper = WorkerTagWrapper;
            App->YRM100XWorker->DefaultAP = bytes_to_uint32(App->ApTempBuffer, 4);
            m100_disable_write_mask(App->YRM100XWorker->module, WRITE_EPC);
        }

    } else if(Index == 2) {
        //Freeing the YRM100X uart helper
        if(App->YRM100XWorker) {
            //Free Tag Wrapper
            if(App->YRM100XWorker->uhf_tag_wrapper) {
                uhf_tag_wrapper_free(App->YRM100XWorker->uhf_tag_wrapper);
            }

            //Freeing yrm100x worker
            uhf_worker_stop(App->YRM100XWorker);
            uhf_worker_free(App->YRM100XWorker);
            App->YRM100XWorker = NULL;
        }
        //Mark the M7E as being used
        if(!App->UartHelper) {
            App->UartHelper = uart_helper_alloc();
            uart_helper_set_delimiter(App->UartHelper, LINE_DELIMITER, INCLUDE_LINE_DELIMITER);
            uart_helper_set_callback(App->UartHelper, uart_demo_process_line, App);
        }
        App->UHFModuleType = M7E_HECTO_MODULE;
    } else {
        //Freeing the YRM100X uart helper
        if(App->YRM100XWorker) {
            //Free Tag Wrapper
            if(App->YRM100XWorker->uhf_tag_wrapper) {
                uhf_tag_wrapper_free(App->YRM100XWorker->uhf_tag_wrapper);
            }

            //Freeing yrm100x worker
            uhf_worker_stop(App->YRM100XWorker);
            uhf_worker_free(App->YRM100XWorker);
            App->YRM100XWorker = NULL;
        }

        //Mark the M6E as being used
        if(!App->UartHelper) {
            App->UartHelper = uart_helper_alloc();
            uart_helper_set_delimiter(App->UartHelper, LINE_DELIMITER, INCLUDE_LINE_DELIMITER);
            uart_helper_set_callback(App->UartHelper, uart_demo_process_line, App);
        }
        App->UHFModuleType = M6E_NANO_MODULE;
    }
}
/**
 * @brief      Handles the App save settings
 * @details    This function toggles the saving mode after a write operation.
 * @param      item  VariableItem - the current selection for setting.
*/
void uhf_reader_save_setting_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);

    variable_item_set_current_value_text(Item, App->SettingSavingNames[Index]);

    if(Index == 1) {
        App->UHFSaveType = YES_SAVE_ON_WRITE;
    } else {
        App->UHFSaveType = NO_SAVE_ON_WRITE;
    }
    App->SettingSavingIndex = Index;
    if(!uhf_reader_settings_save_current(App)) show_command_error_notification(App);
}

/**
 * @brief      Handles the UHF Reader Baud Rate Selection
 * @details    This function handles switching between different supported baud rates for the UHF Readers like the YRM100X, M6e, and M7e Nano RFID modules.
 * @param      item  VariableItem - the current selection for the UHF baud rate.
*/
void uhf_reader_baud_setting_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);
    // Check if locked
    if(App->item_locks[1].locked) {
        show_locked_notification(App);
        return;
    }
    variable_item_set_current_value_text(Item, App->SettingBaudNames[Index]);

    if(Index == 1) {
        //Use 115200 as baud rate
        App->UHFBaudRate = 115200;
    } else if(Index == 2) {
        //Use 19200 as baud rate
        App->UHFBaudRate = 384000;
    } else {
        //Use 9600 as baud rate
        App->UHFBaudRate = 9600;
    }

    //Setting the baudrate for each module
    if(App->UHFModuleType == YRM100X_MODULE && App->ReaderConnected) {
        m100_set_baudrate(App->YRM100XWorker->module, App->UHFBaudRate);
    } else {
        uart_helper_set_baud_rate(App->UartHelper, App->UHFBaudRate);
    }
}

/**
 * @brief      Handles the UHF Reader Region Selection
 * @details    This function handles switching between different supported regions for the selected UHF RFID Reader.
 * @param      item  VariableItem - the current selection for the Region.
*/
void uhf_reader_region_setting_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);
    if(App->item_locks[2].locked) {
        show_locked_notification(App);
        return;
    }
    if(!App->ReaderConnected) return;

    if(App->UHFModuleType == YRM100X_MODULE) {
        WorkingRegion region = WORKING_REGIONS[Index];
        if(!m100_set_working_region(App->YRM100XWorker->module, region)) {
            variable_item_set_current_value_index(Item, App->SettingRegionIndex);
            show_command_error_notification(App);
            return;
        }
    } else {
        //PLACE COMMAND HERE TO CHANGE THE REGION FOR M6E and M7E
        uart_helper_send(App->UartHelper, "Region\n", 7);
    }

    // Region constants intentionally match the configuration-menu index order.
    App->SettingRegionIndex = Index;
    App->UHFRegionType = Index;
    variable_item_set_current_value_text(Item, App->SettingRegionNames[Index]);
    if(!uhf_reader_settings_save_current(App)) show_command_error_notification(App);
}

void uhf_reader_session_setting_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);
    if(App->item_locks[5].locked) {
        show_locked_notification(App);
        return;
    }
    if(App->UHFModuleType == YRM100X_MODULE) {
        if(!m100_set_query_params(
               App->YRM100XWorker->module, Index, App->SettingTargetIndex)) {
            // Revert UI to previous value
            variable_item_set_current_value_index(Item, App->SettingSessionIndex);
            variable_item_set_current_value_text(
                Item, App->SettingSessionNames[App->SettingSessionIndex]);
            show_command_error_notification(App);
            return;
        }
    }
    App->SettingSessionIndex = Index;
    variable_item_set_current_value_text(Item, App->SettingSessionNames[Index]);
    if(!uhf_reader_settings_save_current(App)) show_command_error_notification(App);
}

void uhf_reader_target_setting_change(VariableItem* Item) {
    UHFReaderApp* App = variable_item_get_context(Item);
    uint8_t Index = variable_item_get_current_value_index(Item);
    if(App->item_locks[6].locked) {
        show_locked_notification(App);
        return;
    }
    if(App->UHFModuleType == YRM100X_MODULE) {
        if(!m100_set_query_params(
               App->YRM100XWorker->module, App->SettingSessionIndex, Index)) {
            // Revert UI to previous value
            variable_item_set_current_value_index(Item, App->SettingTargetIndex);
            variable_item_set_current_value_text(
                Item, App->SettingTargetNames[App->SettingTargetIndex]);
            show_command_error_notification(App);
            return;
        }
    }
    App->SettingTargetIndex = Index;
    variable_item_set_current_value_text(Item, App->SettingTargetNames[Index]);
    if(!uhf_reader_settings_save_current(App)) show_command_error_notification(App);
}

/**

 * @brief      Allocates the AP text screen
 * @details    Allocates the text input object for the AP screen.
 * @param      app  The UHFReaderApp - used for allocating variables and text input.
*/
void ap_menu_alloc(UHFReaderApp* App) {
    App->ApInput = byte_input_alloc();
    view_dispatcher_add_view(
        App->ViewDispatcher, UHFReaderViewSetReadAp, byte_input_get_view(App->ApInput));
    App->ApInputBufferSize = 4;
    App->ApTempBuffer = (uint8_t*)malloc(App->ApInputBufferSize);
}

/**
 * @brief      Handles the setting items clicked
 * @details    Handles the power value input by the user.
 * @param      context, index - context used for UHFReaderApp, index used for state check.
*/
void uhf_reader_setting_item_clicked(void* context, uint32_t index) {
    UHFReaderApp* App = (UHFReaderApp*)context;
    index++;
    if(index == 6) {
        // Header to display on the AP value input screen.
        if(App->item_locks[3].locked) {
            show_locked_notification(App);
            return;
        }
        byte_input_set_header_text(App->ApInput, App->AccessPasswordPlaceHolder);


        //Setting the AP text input callback function
        byte_input_set_result_callback(
            App->ApInput,
            uhf_reader_setting_6_text_updated,
            NULL,
            App,
            App->ApTempBuffer,
            App->ApInputBufferSize);
        view_set_previous_callback(
            byte_input_get_view(App->ApInput), uhf_reader_navigation_configure_callback);
        view_dispatcher_switch_to_view(App->ViewDispatcher, UHFReaderViewSetReadAp);
    }
}

/**
 * @brief      Frees the configure screen.
 * @details    Frees all variables and views for the configure screen.
 * @param      app  The UHFReaderApp - used for freeing variables and text input.
*/
void view_config_free(UHFReaderApp* App) {
    view_dispatcher_remove_view(App->ViewDispatcher, UHFReaderViewSetReadAp);
    byte_input_free(App->ApInput);
    free(App->ApTempBuffer);
    furi_string_free(App->DefaultAccessPwdStr);
    free(App->DefaultAccessPassword);
    free(App->ReadAccessPasswordLabel);
    free(App->AccessPasswordPlaceHolder);
    free(App->item_locks);
    view_dispatcher_remove_view(App->ViewDispatcher, UHFReaderViewConfigure);
    variable_item_list_free(App->VariableItemListConfig);
}
