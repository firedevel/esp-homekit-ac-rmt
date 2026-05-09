/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS products only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */



#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>
#include <hap_fw_upgrade.h>

#include <iot_button.h>

#include <app_wifi.h>
#include <app_hap_setup_payload.h>

#include "air_conditioner.h"

/* Comment out the below line to disable Firmware Upgrades */
#define CONFIG_FIRMWARE_SERVICE

static const char *TAG = "HAP Air Conditioner";

#define AIRCONDITIONER_TASK_PRIORITY  1
#define AIRCONDITIONER_TASK_STACKSIZE 4 * 1024
#define AIRCONDITIONER_TASK_NAME      "hap_airconditioner"

/* Reset homekit if button is pressed for more than 3 seconds and then released */
#define RESET_NETWORK_BUTTON_TIMEOUT        3

/* Reset to factory if button is pressed and held for more than 10 seconds */
#define RESET_TO_FACTORY_BUTTON_TIMEOUT     10

/* The button "Boot" will be used as the Reset button for the example */
#define RESET_GPIO  GPIO_NUM_9

AC_INFO ac_current_info = {
    .on = false,
    .mode = AUTO_MODE,
    .temp = 24,
    .fan_speed = AUTO_FAN_SPEED
};

static TickType_t ac_send_tick_count = 0;

bool ac_send_r05d_code_flag = false;    // 是否可以发送红外信号
/**
 * @brief The network reset button callback handler.
 * Useful for testing the Wi-Fi re-configuration feature of WAC2
 */
static void reset_network_handler(void* arg)
{
    hap_reset_network();
}
/**
 * @brief The factory reset button callback handler.
 */
static void reset_to_factory_handler(void* arg)
{
    hap_reset_to_factory();
}

/**
 * The Reset button  GPIO initialisation function.
 * Same button will be used for resetting Wi-Fi network as well as for reset to factory based on
 * the time for which the button is pressed.
 */
static void reset_key_init(uint32_t key_gpio_pin)
{
    button_handle_t handle = iot_button_create(key_gpio_pin, BUTTON_ACTIVE_LOW);
    iot_button_add_on_release_cb(handle, RESET_NETWORK_BUTTON_TIMEOUT, reset_network_handler, NULL);
    iot_button_add_on_press_cb(handle, RESET_TO_FACTORY_BUTTON_TIMEOUT, reset_to_factory_handler, NULL);
}

/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int air_conditioner_identify(hap_acc_t *ha)
{
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}


static char val[260];
static char * emulator_print_value(hap_char_t *hc, const hap_val_t *cval)
{
    uint16_t perm = hap_char_get_perm(hc);
    if (perm & HAP_CHAR_PERM_PR) {
        hap_char_format_t format = hap_char_get_format(hc);
	    switch (format) {
		    case HAP_CHAR_FORMAT_BOOL : {
                snprintf(val, sizeof(val), "%s", cval->b ? "true":"false");
    			break;
		    }
		    case HAP_CHAR_FORMAT_UINT8:
		    case HAP_CHAR_FORMAT_UINT16:
		    case HAP_CHAR_FORMAT_UINT32:
		    case HAP_CHAR_FORMAT_INT:
                snprintf(val, sizeof(val), "%d", cval->i);
                break;
    		case HAP_CHAR_FORMAT_FLOAT :
                snprintf(val, sizeof(val), "%f", cval->f);
		    	break;
    		case HAP_CHAR_FORMAT_STRING :
                if (cval->s) {
                    snprintf(val, sizeof(val), "%s", cval->s);
                } else {
                    snprintf(val, sizeof(val), "null");
                }
			    break;
            default :
                snprintf(val, sizeof(val), "unsupported");
		}
    } else {
        snprintf(val, sizeof(val), "null");
    }
    return val;
}

/* Callback for handling writes on the Air Conditioner Service
 */
static int air_conditioner_write(hap_write_data_t write_data[], int count,
        void *serv_priv, void *write_priv)
{
    int i, ret = HAP_SUCCESS;
    hap_write_data_t *write;
    for (i = 0; i < count; i++) {
        /* 每次只对一个特征hc进行处理 */
        write = &write_data[i];
        /* Setting a default error value */
        *(write->status) = HAP_STATUS_VAL_INVALID;
        // int iid = hap_char_get_iid(write->hc);
        // int aid = hap_acc_get_aid(hap_serv_get_parent(hap_char_get_parent(write->hc)));
        // printf("Write aid = %d, iid = %d, val = %s\n", aid, iid, emulator_print_value(write->hc, &(write->val)));
        // printf("hap_char_get_type_uuid(write->hc) : %s\n", hap_char_get_type_uuid(write->hc));
        /* 操作 */
        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE)) 
        {
            switch (write->val.u)
            {
            case 0:
                ac_current_info.on = false;
                break;
            case 1:
                // 加热模式
                ac_current_info.on = true;
                ac_current_info.mode = HEAT_MODE;
                break;
            case 2:
                // 制冷模式
                ac_current_info.on = true;
                ac_current_info.mode = COOL_MODE;
                break;
            case 3:
                // 自动模式
                ac_current_info.on = true;
                ac_current_info.mode = AUTO_MODE;
                break;
            }
            ESP_LOGI(TAG, "Received Write for TARGET_HEATING_COOLING_STATE: %u", write->val.u);
        
            *(write->status) = HAP_STATUS_SUCCESS;
        } 
        /* 设定制冷温度操作 */
        else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_TARGET_TEMPERATURE))
        {   /* 根据空调实际温度范围进行限制 */
            if (write->val.f > 30 )
            {
                write->val.f = 30;
            }
            else if (write->val.f < 17)
            {
                write->val.f = 17;
            }
            ac_current_info.temp = (uint8_t)write->val.f;
            ESP_LOGI(TAG, "Received Write for TARGET_TEMPERATURE %d", ac_current_info.temp);
            *(write->status) = HAP_STATUS_SUCCESS;
        }
        else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_TEMPERATURE_DISPLAY_UNITS))
        {   /* 不允许更改温度显示单位 */
            write->val.u = 0; //摄氏度
            ESP_LOGI(TAG, "Received Write for TEMPERATURE_DISPLAY_UNITS %d", write->val.u);
            *(write->status) = HAP_STATUS_SUCCESS;
        }
        else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
            ESP_LOGW(TAG, "Received Write FAIL: %s", hap_char_get_type_uuid(write->hc));
        }
        /* If the characteristic write was successful, update it in hap core
         */
        if (*(write->status) == HAP_STATUS_SUCCESS) {
            hap_char_update_val(write->hc, &(write->val));
            //TODO 立即更新工作状态
        } else {
            /* Else, set the return value appropriately to report error */
            ESP_LOGW(TAG, "Received Write for Air Conditioner Failed!");
            ret = HAP_FAIL;
        }
    }
    /* 在保存设定的状态后，使能发送红外指令的信号，并且记录此时的tick计数 */
    ac_send_r05d_code_flag = true;
    ac_send_tick_count = xTaskGetTickCount();   // 更新当前要发红外时的tick计数
    return ret;
}

static int air_conditioner_read(hap_char_t *hc, hap_status_t *status_code, void *serv_priv, void *read_priv)
{
    if (hap_req_get_ctrl_id(read_priv)) {
        ESP_LOGI(TAG, "Received read %s from %s", hap_char_get_type_uuid(hc), hap_req_get_ctrl_id(read_priv));
    }
    /* 更新当前模式为设定模式 */
    if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE)) 
    {
        hap_val_t new_val;

        if(ac_current_info.on){
            switch (ac_current_info.mode)
            {
            case HEAT_MODE:
                new_val.u = 1;
                break;
            case COOL_MODE:
                new_val.u = 2;
                break;
            case AUTO_MODE:
                // 自动模式
                //TODO: 读取环境温度并判断制冷/加热
                new_val.u = 1;  // 显示为加热
                break;
            }
        } else {
            new_val.u = 0;
        }
        ESP_LOGI(TAG, "Received read for CURRENT_HEATING_COOLING_STATE: %u", new_val.u);
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    }
    /* 更新当前温度为空调设定温度 */
    else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_TEMPERATURE)) 
    {
        hap_val_t new_val;

        //TODO: 读取环境温度
        new_val.f = ac_current_info.temp;
        ESP_LOGI(TAG, "Received read for CURRENT_TEMPERATURE: %d", ac_current_info.temp);
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    }

    return HAP_SUCCESS;
}

/*The main thread for handling the Air Conditioner Accessory */
static void air_conditioner_thread_entry(void *arg)
{
    hap_acc_t *accessory;
    hap_serv_t *service;

    /* Initialize the HAP core */
    hap_init(HAP_TRANSPORT_WIFI);

    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    hap_acc_cfg_t cfg = {
        .name = "AirConditioner",
        .manufacturer = "Media",
        .model = "KJR-90D/BK",
        .serial_num = "DEV0001",
        .fw_rev = "1.0",
        .hw_rev = "1.0",
        .pv = "1.1.0",
        .identify_routine = air_conditioner_identify,
        .cid = HAP_CID_AIR_CONDITIONER,
    };

    /* Create accessory object */
    accessory = hap_acc_create(&cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        goto thermostat_err;
    }

    /* Add a dummy Product Data */
    uint8_t product_data[] = {'Z','H','X','K','Q','0','0','1'};
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

    /* Create the Air Conditioner Service. Include the "name" since this is a user visible service  */
    service = hap_serv_thermostat_create(0, 0, 24.0, 24.0, 0);      // 实际上创建的是加热器制冷器
    hap_serv_add_char(service, hap_char_name_create("AirConditioner"));

    if (!service) {
        ESP_LOGE(TAG, "Failed to create thermostat Service");
        goto thermostat_err;
    }

    /* Set the write callback for the service */
    hap_serv_set_write_cb(service, air_conditioner_write);
    
    hap_serv_set_read_cb(service, air_conditioner_read);
    /* Add the Air Conditioner Service to the Accessory Object */
    hap_acc_add_serv(accessory, service);

    /* Add the Accessory to the HomeKit Database */
    hap_add_accessory(accessory);

    /* Initialize the air conditioner Bulb Hardware */
    air_conditioner_init();

    /* Register a common button for reset Wi-Fi network and reset to factory.
     */
    reset_key_init(RESET_GPIO);

    /* TODO: Do the actual hardware initialization here */

    /* For production accessories, the setup code shouldn't be programmed on to
     * the device. Instead, the setup info, derived from the setup code must
     * be used. Use the factory_nvs_gen utility to generate this data and then
     * flash it into the factory NVS partition.
     *
     * By default, the setup ID and setup info will be read from the factory_nvs
     * Flash partition and so, is not required to set here explicitly.
     *
     * However, for testing purpose, this can be overridden by using hap_set_setup_code()
     * and hap_set_setup_id() APIs, as has been done here.
     */
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
    /* Unique Setup code of the format xxx-xx-xxx. Default: 111-22-333 */
    hap_set_setup_code(CONFIG_EXAMPLE_SETUP_CODE);
    /* Unique four character Setup Id. Default: ES32 */
    hap_set_setup_id(CONFIG_EXAMPLE_SETUP_ID);
#ifdef CONFIG_APP_WIFI_USE_WAC_PROVISIONING
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, true, cfg.cid);
#else
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
#endif
#endif

    /* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
    //hap_enable_mfi_auth(HAP_MFI_AUTH_HW);

    /* Initialize Wi-Fi */
    app_wifi_init();

    /* After all the initializations are done, start the HAP core */
    hap_start();
    /* Start Wi-Fi */
    app_wifi_start(portMAX_DELAY);

    /* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
    vTaskDelete(NULL);

thermostat_err:
    hap_acc_delete(accessory);
    vTaskDelete(NULL);
}

/** 在write回调函数更新红外指令后的一段时间内，如果没有新的更新，才发送，否则知道没有新的更新为止再发送
 * 因为在家庭App进行模式切换时，会被调用两次write回调函数，为了防止发重复指令
 * 也防止在改变风速这一特征时，造成发送频率太高
 */
static void air_conditioner_send_task(void *arg)
{
    TickType_t nowTickCount = 0;
    while (1)
    {
        if (ac_send_r05d_code_flag == true)
        {
            nowTickCount = xTaskGetTickCount();
            if ((TickType_t)(nowTickCount - ac_send_tick_count) > pdMS_TO_TICKS(1000))
            {
                ac_send_r05d_code(ac_current_info);
                ac_send_r05d_code_flag = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

void app_main()
{
    xTaskCreate(air_conditioner_thread_entry, AIRCONDITIONER_TASK_NAME, AIRCONDITIONER_TASK_STACKSIZE,
            NULL, AIRCONDITIONER_TASK_PRIORITY, NULL);
    xTaskCreate(air_conditioner_send_task, "air_conditioner_send_task", 2048, NULL, 10, NULL);
}
