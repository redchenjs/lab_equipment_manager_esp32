/*
 * gui.c
 *
 *  Created on: 2018-02-13 22:57
 *      Author: Jack Chen <redchenjs@live.com>
 */

#include <time.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"

#include "gfx.h"
#include "qrcode.h"

#include "core/os.h"
#include "user/gui.h"
#include "user/audio_player.h"
#include "user/http_app_status.h"

#define TAG "gui"

static const char *img_file_ptr[][2] = {
    {ani0_240x135_gif_ptr, ani0_240x135_gif_end}, // "WiFi"
    {ani1_240x135_gif_ptr, ani1_240x135_gif_end}, // "Loading"
    {ani2_240x135_gif_ptr, ani2_240x135_gif_end}, // "Success"
    {ani3_240x135_gif_ptr, ani3_240x135_gif_end}, // "NFC"
    {ani4_240x135_gif_ptr, ani4_240x135_gif_end}, // "PowerOff"
    {ani5_240x135_gif_ptr, ani5_240x135_gif_end}, // "Clock"
    {ani6_240x135_gif_ptr, ani6_240x135_gif_end}, // "Error"
    {ani7_240x135_gif_ptr, ani7_240x135_gif_end}, // "Config"
    {ani8_240x135_gif_ptr, ani8_240x135_gif_end}, // "Updating"
};

GDisplay *gui_gdisp = NULL;

static coord_t gui_disp_width = 0;
static coord_t gui_disp_height = 0;

static GTimer gui_flush_timer;

static uint8_t gui_mode = 0;
static uint8_t gui_backlight = 255;

static char u_info[11] = {0};

static uint8_t t_hour = 0;
static uint8_t t_min  = 0;
static uint8_t t_sec  = 0;

static uint8_t l_hour = 0;
static uint8_t l_min  = 0;
static uint8_t l_sec  = 0;

void printQr(const uint8_t qrcode[])
{
    int size = qrcodegen_getSize(qrcode);
    int border = 2;

    gdispGClear(gui_gdisp, White);

    for (int y=-border; y<size+border; y++) {
        for (int x=-border; x<size+border; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                gdispGFillArea(gui_gdisp, x*5+59, y*5+5, 5, 5, Black);
            } else {
                gdispGFillArea(gui_gdisp, x*5+59, y*5+5, 5, 5, White);
            }
        }
    }

    gtimerJab(&gui_flush_timer);
}

static void gui_flush_task(void *pvParameter)
{
    gdispGFlush(gui_gdisp);
}

static void gui_task(void *pvParameter)
{
    font_t gui_font;
    char text_buff[32] = {0};
    portTickType xLastWakeTime;

    gfxInit();

    gui_gdisp = gdispGetDisplay(0);
    gui_font = gdispOpenFont("DejaVuSans32");
    gui_disp_width = gdispGGetWidth(gui_gdisp);
    gui_disp_height = gdispGGetHeight(gui_gdisp);

    gtimerStart(&gui_flush_timer, gui_flush_task, NULL, TRUE, TIME_INFINITE);

    ESP_LOGI(TAG, "started.");

    gdispGSetOrientation(gui_gdisp, GDISP_ROTATE_270);

    while (1) {
        switch (gui_mode) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08: {
            gdispImage gfx_image;

            if (!(gdispImageOpenMemory(&gfx_image, img_file_ptr[gui_mode][0]) & GDISP_IMAGE_ERR_UNRECOVERABLE)) {
                gdispImageSetBgColor(&gfx_image, Black);

                gdispGSetBacklight(gui_gdisp, gui_backlight);

                while (1) {
                    xLastWakeTime = xTaskGetTickCount();

                    if (xEventGroupGetBits(user_event_group) & GUI_RELOAD_BIT) {
                        xEventGroupClearBits(user_event_group, GUI_RELOAD_BIT);
                        break;
                    }

                    if (gdispImageDraw(&gfx_image, 0, 0, gfx_image.width, gfx_image.height, 0, 0) != GDISP_IMAGE_ERR_OK) {
                        ESP_LOGE(TAG, "failed to draw image: %u", gui_mode);
                        gui_mode = GUI_MODE_IDX_OFF;
                        break;
                    }

                    gtimerJab(&gui_flush_timer);

                    delaytime_t delay = gdispImageNext(&gfx_image);
                    if (delay == TIME_INFINITE) {
                        gui_mode = GUI_MODE_IDX_PAUSE;
                        break;
                    }

                    if (delay != TIME_IMMEDIATE) {
                        vTaskDelayUntil(&xLastWakeTime, delay / portTICK_RATE_MS);
                    }
                }

                gdispImageClose(&gfx_image);
            } else {
                ESP_LOGE(TAG, "failed to open image: %u", gui_mode);
                gui_mode = GUI_MODE_IDX_OFF;
                break;
            }
            break;
        }
        case GUI_MODE_IDX_TIMER:
            xLastWakeTime = xTaskGetTickCount();

            gdispGClear(gui_gdisp, Black);

            time_t now = 0;
            struct tm timeinfo = {0};

            time(&now);
            localtime_r(&now, &timeinfo);

            int64_t left_time = t_hour * 3600 + t_min * 60 + t_sec -
                                timeinfo.tm_hour * 3600 - timeinfo.tm_min * 60 - timeinfo.tm_sec;
            if (left_time <= 0) {
                left_time = 0;
            }
            l_hour = left_time / 3600;
            l_min  = left_time / 60 % 60;
            l_sec  = left_time % 60;

            snprintf(text_buff, sizeof(text_buff), "(%10s)", u_info);
            gdispGFillStringBox(gui_gdisp, 2, 2, 236, 32, text_buff, gui_font, Yellow, Black, justifyCenter);

            snprintf(text_buff, sizeof(text_buff), "T-N:");
            gdispGFillStringBox(gui_gdisp, 2, 34, 86, 32, text_buff, gui_font, Cyan, Black, justifyLeft);

            snprintf(text_buff, sizeof(text_buff), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            gdispGFillStringBox(gui_gdisp, 88, 34, 150, 32, text_buff, gui_font, Cyan, Black, justifyRight);

            snprintf(text_buff, sizeof(text_buff), "T-R:");
            gdispGFillStringBox(gui_gdisp, 2, 67, 86, 32, text_buff, gui_font, Magenta, Black, justifyLeft);

            snprintf(text_buff, sizeof(text_buff), "%02d:%02d:%02d", t_hour, t_min, t_sec);
            gdispGFillStringBox(gui_gdisp, 88, 67, 150, 32, text_buff, gui_font, Magenta, Black, justifyRight);

            if (l_hour <= 0 && l_min <= 4) {
                snprintf(text_buff, sizeof(text_buff), "T-L:");
                gdispGFillStringBox(gui_gdisp, 2, 100, 86, 32, text_buff, gui_font, Orange, Black, justifyLeft);

                snprintf(text_buff, sizeof(text_buff), "%02d:%02d:%02d", l_hour, l_min, l_sec);
                gdispGFillStringBox(gui_gdisp, 88, 100, 150, 32, text_buff, gui_font, Orange, Black, justifyRight);

                if (l_sec == 5) {
                    audio_player_play_file(0);
                }
            } else {
                snprintf(text_buff, sizeof(text_buff), "T-L:");
                gdispGFillStringBox(gui_gdisp, 2, 100, 86, 32, text_buff, gui_font, Lime, Black, justifyLeft);

                snprintf(text_buff, sizeof(text_buff), "%02d:%02d:%02d", l_hour, l_min, l_sec);
                gdispGFillStringBox(gui_gdisp, 88, 100, 150, 32, text_buff, gui_font, Lime, Black, justifyRight);
            }

            gdispGSetBacklight(gui_gdisp, gui_backlight);

            gtimerJab(&gui_flush_timer);

            if (l_hour <= 0 && l_min <= 0 && l_sec <= 0) {
                http_app_update_status(HTTP_REQ_IDX_OFF);
            }

            vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_RATE_MS);

            break;
        case GUI_MODE_IDX_QR_CODE:
            qrcode_encode(http_app_get_token());

            gdispGSetBacklight(gui_gdisp, gui_backlight);

            xEventGroupWaitBits(
                user_event_group,
                GUI_RELOAD_BIT,
                pdTRUE,
                pdFALSE,
                portMAX_DELAY
            );

            break;
        case GUI_MODE_IDX_PAUSE:
            xEventGroupWaitBits(
                user_event_group,
                GUI_RELOAD_BIT,
                pdTRUE,
                pdFALSE,
                portMAX_DELAY
            );

            break;
        case GUI_MODE_IDX_OFF:
        default:
            gdispGSetBacklight(gui_gdisp, 0);

            vTaskDelay(500 / portTICK_RATE_MS);

            gdispGClear(gui_gdisp, Black);
            gtimerJab(&gui_flush_timer);

            xEventGroupWaitBits(
                user_event_group,
                GUI_RELOAD_BIT,
                pdTRUE,
                pdFALSE,
                portMAX_DELAY
            );

            break;
        }
    }
}

void gui_set_user_info(const char *u_i)
{
    strncpy(u_info, u_i, sizeof(u_info)-1);

    ESP_LOGI(TAG, "user info: %s", u_info);
}

void gui_set_timer_time(int t_h, int t_m, int t_s)
{
    t_hour = t_h;
    t_min  = t_m;
    t_sec  = t_s;

    ESP_LOGI(TAG, "timer time: %02d:%02d:%02d", t_hour, t_min, t_sec);
}

void gui_set_mode(uint8_t idx)
{
    gui_mode = idx;

    xEventGroupSetBits(user_event_group, GUI_RELOAD_BIT);

    ESP_LOGI(TAG, "mode: 0x%02X", gui_mode);
}

uint8_t gui_get_mode(void)
{
    return gui_mode;
}

void gui_init(void)
{
    xTaskCreatePinnedToCore(gui_task, "guiT", 1920, NULL, 6, NULL, 1);
}
