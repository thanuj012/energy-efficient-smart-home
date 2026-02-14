#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "esp_http_server.h"
#include "cJSON.h"

#include "bsp/esp-box-3.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#define WIFI_SSID "MADTITAN"
#define WIFI_PASS "iambatmanhehe"

static const char *TAG="SMART_BOX";

// ===== GLOBAL DATA FROM ESP32 =====
#define COST_PER_UNIT 8.0
#define PEAK_COST_PER_UNIT 12.0  // Peak hour rate (50% higher)

static float g_current=0;
static float g_energy=0;
static float g_power=0;
static int g_motion=0;
static int g_ldr=0;

static float prev_current=0;
static int no_motion_counter=0;

static char command_to_send[32]="OK";
static char fault_message[128]="No ed";
static bool has_fault=false;

// Device states
static bool device1_state=false;
static bool device2_state=false;
static bool device3_state=false;

// ===== AUTOMATION MODES =====
static bool smart_presence_mode=false;  // PIR-based automation
static bool adaptive_lighting_mode=false;  // LDR-based automation
static bool peak_guard_enabled=true;  // Peak hour monitoring

// Smart Presence settings
#define NO_MOTION_TIMEOUT 5
static bool pending_shutdown=false;
static char shutdown_message[128]="";



// Peak Guard settings
typedef struct {
    int start_hour;
    int start_min;
    int end_hour;
    int end_min;
} PeakPeriod;

static PeakPeriod peak_periods[2] = {
    {6, 0, 10, 0},   // Morning peak: 6:00 AM - 10:00 AM
    {19, 0, 21, 0}   // Evening peak: 6:00 PM - 10:00 PM
};

static bool is_peak_hour=false;
static int next_peak_in_mins=-1;
static float peak_cost_projection=0.0;

// ===== UI OBJECTS =====
static lv_obj_t *home_screen;
static lv_obj_t *analytics_screen;
static lv_obj_t *device_ctrl_screen;
static lv_obj_t *automation_screen;
static lv_obj_t *fault_screen;

static lv_obj_t *clock_label;

static lv_obj_t *analytics_tileview;
static lv_obj_t *live_label;
static lv_obj_t *energy_label;
static lv_obj_t *peak_label;

static lv_obj_t *device1_btn;
static lv_obj_t *device2_btn;
static lv_obj_t *device3_btn;
static lv_obj_t *device1_label;
static lv_obj_t *device2_label;
static lv_obj_t *device3_label;

static lv_obj_t *fault_label;
static lv_obj_t *notification_mbox = NULL;

// Automation screen objects
static lv_obj_t *presence_switch;
static lv_obj_t *presence_status_label;
static lv_obj_t *adaptive_switch;
static lv_obj_t *adaptive_status_label;
static lv_obj_t *peak_guard_label;



// ===== TIME MANAGEMENT =====
static int hours=19, minutes=34, seconds=2;  // Start at 10 AM for demo

static void update_clock(void)
{
    seconds++;
    if(seconds>=60){
        seconds=0;
        minutes++;
    }
    if(minutes>=60){
        minutes=0;
        hours++;
    }
    if(hours>=24){
        hours=0;
    }

    char time_str[16];
    sprintf(time_str,"%02d:%02d:%02d",hours,minutes,seconds);
    
    bsp_display_lock(0);
    if(clock_label) lv_label_set_text(clock_label,time_str);
    bsp_display_unlock();
}

// ===== PEAK ANALYSIS FUNCTIONS =====
static bool is_in_peak_period(int h, int m)
{
    int current_mins = h * 60 + m;
    
    for(int i=0; i<2; i++){
        int start_mins = peak_periods[i].start_hour * 60 + peak_periods[i].start_min;
        int end_mins = peak_periods[i].end_hour * 60 + peak_periods[i].end_min;
        
        if(current_mins >= start_mins && current_mins < end_mins){
            return true;
        }
    }
    return false;
}

static int minutes_to_next_peak(int h, int m)
{
    int current_mins = h * 60 + m;
    int min_distance = 24 * 60;  // Max distance in a day
    
    for(int i=0; i<2; i++){
        int start_mins = peak_periods[i].start_hour * 60 + peak_periods[i].start_min;
        
        int distance;
        if(start_mins > current_mins){
            distance = start_mins - current_mins;
        }else{
            distance = (24 * 60 - current_mins) + start_mins;  // Next day
        }
        
        if(distance < min_distance){
            min_distance = distance;
        }
    }
    
    return min_distance;
}

static void update_peak_analysis(void)
{
    is_peak_hour = is_in_peak_period(hours, minutes);
    
    if(is_peak_hour){
        next_peak_in_mins = 0;
        peak_cost_projection = g_power * PEAK_COST_PER_UNIT / 1000.0;  // Cost per hour
    }else{
        next_peak_in_mins = minutes_to_next_peak(hours, minutes);
        peak_cost_projection = g_power * COST_PER_UNIT / 1000.0;  // Normal cost
    }
}

// ===== NOTIFICATION SYSTEM =====
static void close_notification(lv_event_t *e)
{
    if(notification_mbox){
        bsp_display_lock(0);
        lv_msgbox_close(notification_mbox);
        notification_mbox = NULL;
        bsp_display_unlock();
    }
}

static void notification_yes_action(lv_event_t *e)
{
    // Turn off all devices
    device1_state = false;
    device2_state = false;
    device3_state = false;
    strcpy(command_to_send, "ALL_OFF");
    pending_shutdown = false;
    
    bsp_display_lock(0);
    if(device1_label) lv_label_set_text(device1_label, "Device1: OFF");
    if(device2_label) lv_label_set_text(device2_label, "Device2: OFF");
    if(device3_label) lv_label_set_text(device3_label, "Device3: OFF");
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "User approved: All devices turned OFF");
    close_notification(e);
}

static void notification_no_action(lv_event_t *e)
{
    pending_shutdown = false;
    ESP_LOGI(TAG, "User declined: Devices remain ON");
    close_notification(e);
}

static void show_notification(const char *title, const char *message, bool show_yes_no)
{
    if(notification_mbox) return;   // already showing

    bsp_display_lock(0);

    notification_mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(notification_mbox, title);
    lv_msgbox_add_text(notification_mbox, message);

    if(show_yes_no){
        lv_obj_t *btn_yes = lv_msgbox_add_footer_button(notification_mbox, "YES");
        lv_obj_add_event_cb(btn_yes, notification_yes_action, LV_EVENT_CLICKED, NULL);

        lv_obj_t *btn_no = lv_msgbox_add_footer_button(notification_mbox, "NO");
        lv_obj_add_event_cb(btn_no, notification_no_action, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_t *btn_ok = lv_msgbox_add_footer_button(notification_mbox, "OK");
        lv_obj_add_event_cb(btn_ok, close_notification, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_center(notification_mbox);
    bsp_display_unlock();
}


// ===== HTTP RECEIVE =====
esp_err_t data_post_handler(httpd_req_t *req)
{
    char content[256];
    int ret=httpd_req_recv(req,content,req->content_len);
    if(ret<=0) return ESP_FAIL;

    content[ret]='\0';

    cJSON *root=cJSON_Parse(content);
    if(root){
        g_motion=cJSON_GetObjectItem(root,"motion")->valueint;
        g_current=cJSON_GetObjectItem(root,"current")->valuedouble;
        g_energy=cJSON_GetObjectItem(root,"energy")->valuedouble;
        g_ldr=cJSON_GetObjectItem(root,"ldr")->valueint;
        g_power=cJSON_GetObjectItem(root,"power")->valuedouble;
        cJSON_Delete(root);
    }

    // ===== SMART PRESENCE MODE =====
    if(smart_presence_mode){
        if(g_motion == 0){
            no_motion_counter++;
            
            // Check if we've reached the timeout
            if(no_motion_counter >= NO_MOTION_TIMEOUT && !pending_shutdown){
                // Check if any devices are ON
                if(device1_state || device2_state || device3_state){
                    sprintf(shutdown_message, 
                        "No motion detected for %ds.\nTurn off devices to save energy?",
                        NO_MOTION_TIMEOUT);
                    show_notification("Smart Presence Alert", shutdown_message, true);
                    pending_shutdown = true;
                }
                no_motion_counter = 0;  // Reset counter
            }
        }else{
            no_motion_counter = 0;
            pending_shutdown = false;
        }
    }else{
        no_motion_counter = 0;
        pending_shutdown = false;
    }

    
    if(adaptive_lighting_mode){
		// Correct logic:
    	// Lower LDR = Darker environment
	    // Higher LDR = Brighter environment
	
	   
	    
		if(g_ldr <= 2400){
	        // Very dark - turn all ON
	        if(!device1_state){
	            strcpy(command_to_send, "DEVICE1_ON");
	            device1_state = true;
	        }
	        else if(!device2_state){
	            strcpy(command_to_send, "DEVICE2_ON");
	            device2_state = true;
	        }
	        else if(!device3_state){
	            strcpy(command_to_send, "DEVICE3_ON");
	            device3_state = true;
	        }
	    }
	    else if(g_ldr > 2600 && g_ldr <= 3500){
	        // Dark - Devices 1,2 ON, Device 3 OFF
	        if(!device1_state){
	            strcpy(command_to_send, "DEVICE1_ON");
	            device1_state = true;
	        }
	        else if(!device2_state){
	            strcpy(command_to_send, "DEVICE2_ON");
	            device2_state = true;
	        }
	        else if(device3_state){
	            strcpy(command_to_send, "DEVICE3_OFF");
	            device3_state = false;
	        }
	    }
	    else if(g_ldr == 4095){
	        // Moderate - Device 1 ON only
	        if(!device1_state){
	            strcpy(command_to_send, "DEVICE1_ON");
	            device1_state = true;
	        }
	        else if(device2_state){
	            strcpy(command_to_send, "DEVICE2_OFF");
	            device2_state = false;
	        }
	        else if(device3_state){
	            strcpy(command_to_send, "DEVICE3_OFF");
	            device3_state = false;
	        }
	    }
	    else{
	        // Very bright - Turn OFF non-essential devices
	        if(device3_state){
	            strcpy(command_to_send, "DEVICE3_OFF");
	            device3_state = false;
	        }
	        else if(device2_state){
	            strcpy(command_to_send, "DEVICE2_OFF");
	            device2_state = false;
	        }
	        // Keep Device1 ON as essential
	    }
    }

    // FAULT DETECTION
    if(prev_current>0){
        if(g_current>prev_current*2){
            sprintf(fault_message,"OVERCURRENT! %.2fA to %.2fA",prev_current,g_current);
            has_fault=true;
        }
        else if(g_current<0.5){
            sprintf(fault_message,"UNDERCURRENT! %.2fA to %.2fA",prev_current,g_current);
            has_fault=true;
        }
        else{
			
		}
    }
    prev_current=g_current;

    httpd_resp_send(req,command_to_send,HTTPD_RESP_USE_STRLEN);
    strcpy(command_to_send,"OK");
    return ESP_OK;
}

// ===== SERVER =====
httpd_handle_t start_webserver(void)
{
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server=NULL;

    if(httpd_start(&server,&config)==ESP_OK){
        httpd_uri_t uri_post={
            .uri="/data",
            .method=HTTP_POST,
            .handler=data_post_handler
        };
        httpd_register_uri_handler(server,&uri_post);
    }
    return server;
}

// ===== NAVIGATION CALLBACKS =====
static void back_to_home(lv_event_t *e)
{
    bsp_display_lock(0);
    lv_scr_load(home_screen);
    bsp_display_unlock();
}

static void open_analytics(lv_event_t *e)
{
    bsp_display_lock(0);
    lv_scr_load(analytics_screen);
    bsp_display_unlock();
}

static void open_device_ctrl(lv_event_t *e)
{
    bsp_display_lock(0);
    lv_scr_load(device_ctrl_screen);
    bsp_display_unlock();
}

static void open_automation(lv_event_t *e)
{
    bsp_display_lock(0);
    lv_scr_load(automation_screen);
    bsp_display_unlock();
}

static void open_fault(lv_event_t *e)
{
    bsp_display_lock(0);
    lv_scr_load(fault_screen);
    bsp_display_unlock();
}

// ===== AUTOMATION CALLBACKS =====
static void toggle_smart_presence(lv_event_t *e)
{
    smart_presence_mode = !smart_presence_mode;
    no_motion_counter = 0;
    pending_shutdown = false;
    
    ESP_LOGI(TAG, "Smart Presence Mode: %s", smart_presence_mode ? "ON" : "OFF");
}

static void toggle_adaptive_lighting(lv_event_t *e)
{
    adaptive_lighting_mode = !adaptive_lighting_mode;
    
    ESP_LOGI(TAG, "Adaptive Lighting Mode: %s", adaptive_lighting_mode ? "ON" : "OFF");
}

// ===== DEVICE CONTROL CALLBACKS =====
static void toggle_device1(lv_event_t *e)
{
    device1_state=!device1_state;
    strcpy(command_to_send,device1_state?"DEVICE1_ON":"DEVICE1_OFF");
    
    bsp_display_lock(0);
    lv_label_set_text(device1_label,device1_state?"Device1: ON":"Device1: OFF");
    bsp_display_unlock();
}

static void toggle_device2(lv_event_t *e)
{
    device2_state=!device2_state;
    strcpy(command_to_send,device2_state?"DEVICE2_ON":"DEVICE2_OFF");
    
    bsp_display_lock(0);
    lv_label_set_text(device2_label,device2_state?"Device2: ON":"Device2: OFF");
    bsp_display_unlock();
}

static void toggle_device3(lv_event_t *e)
{
    device3_state=!device3_state;
    strcpy(command_to_send,device3_state?"DEVICE3_ON":"DEVICE3_OFF");
    
    bsp_display_lock(0);
    lv_label_set_text(device3_label,device3_state?"Device3: ON":"Device3: OFF");
    bsp_display_unlock();
}

static void placeholder_action(lv_event_t *e)
{
    ESP_LOGI(TAG,"Placeholder button pressed");
}

// ===== UI CREATION =====
static void create_home_screen(void)
{
    home_screen=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(home_screen,lv_color_hex(0x000000),0);

    // Clock (top-right)
    clock_label=lv_label_create(home_screen);
    lv_label_set_text(clock_label,"00:00:00");
    lv_obj_set_style_text_color(clock_label,lv_color_hex(0xFFFFFF),0);
    lv_obj_align(clock_label,LV_ALIGN_TOP_RIGHT,-10,10);

    // Title
    lv_obj_t *title=lv_label_create(home_screen);
    lv_label_set_text(title,"SMART HOME");
    lv_obj_set_style_text_color(title,lv_color_hex(0xFFFFFF),0);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,10);

    // 2x2 Grid Tiles
    int tile_w=100;
    int tile_h=80;
    int spacing=20;
    int start_x=30;
    int start_y=60;

    // Analytics
    lv_obj_t *analytics_btn=lv_btn_create(home_screen);
    lv_obj_set_size(analytics_btn,tile_w,tile_h);
    lv_obj_set_pos(analytics_btn,start_x,start_y);
    lv_obj_add_event_cb(analytics_btn,open_analytics,LV_EVENT_CLICKED,NULL);
    lv_obj_t *al=lv_label_create(analytics_btn);
    lv_label_set_text(al,"Analytics");
    lv_obj_center(al);

    // Device Control
    lv_obj_t *device_btn=lv_btn_create(home_screen);
    lv_obj_set_size(device_btn,tile_w,tile_h);
    lv_obj_set_pos(device_btn,start_x+tile_w+spacing,start_y);
    lv_obj_add_event_cb(device_btn,open_device_ctrl,LV_EVENT_CLICKED,NULL);
    lv_obj_t *dl=lv_label_create(device_btn);
    lv_label_set_text(dl,"Devices");
    lv_obj_center(dl);

    // Automation
    lv_obj_t *auto_btn=lv_btn_create(home_screen);
    lv_obj_set_size(auto_btn,tile_w,tile_h);
    lv_obj_set_pos(auto_btn,start_x,start_y+tile_h+spacing);
    lv_obj_add_event_cb(auto_btn,open_automation,LV_EVENT_CLICKED,NULL);
    lv_obj_t *aul=lv_label_create(auto_btn);
    lv_label_set_text(aul,"Automation");
    lv_obj_center(aul);

    // Fault Monitor
    lv_obj_t *fault_btn=lv_btn_create(home_screen);
    lv_obj_set_size(fault_btn,tile_w,tile_h);
    lv_obj_set_pos(fault_btn,start_x+tile_w+spacing,start_y+tile_h+spacing);
    lv_obj_add_event_cb(fault_btn,open_fault,LV_EVENT_CLICKED,NULL);
    lv_obj_t *fl=lv_label_create(fault_btn);
    lv_label_set_text(fl,"Faults");
    lv_obj_center(fl);
}

static void create_analytics_screen(void)
{
    analytics_screen=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(analytics_screen, lv_color_hex(0x1a1a1a), 0);

    // Back button - FIXED POSITION
    lv_obj_t *back_btn=lv_btn_create(analytics_screen);
    lv_obj_set_size(back_btn,60,30);
    lv_obj_align(back_btn,LV_ALIGN_TOP_LEFT,5,5);
    lv_obj_add_event_cb(back_btn,back_to_home,LV_EVENT_CLICKED,NULL);
    lv_obj_t *back_lbl=lv_label_create(back_btn);
    lv_label_set_text(back_lbl," Back ");
    lv_obj_center(back_lbl);

    // Tileview for swipeable pages - POSITIONED LOWER
    analytics_tileview=lv_tileview_create(analytics_screen);
    lv_obj_set_size(analytics_tileview,280,200);  // Reduced height
    lv_obj_set_pos(analytics_tileview,0,40);  // Start below back button

    lv_obj_t *tile1=lv_tileview_add_tile(analytics_tileview,0,0,LV_DIR_HOR);
    lv_obj_t *tile2=lv_tileview_add_tile(analytics_tileview,1,0,LV_DIR_HOR);
    lv_obj_t *tile3=lv_tileview_add_tile(analytics_tileview,2,0,LV_DIR_HOR);

    // Page 1: Live Dashboard - IMPROVED UI
    lv_obj_t *live_container = lv_obj_create(tile1);
    lv_obj_set_size(live_container, 260, 180);
    lv_obj_set_pos(live_container, 10, 10);
    lv_obj_set_style_bg_color(live_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(live_container, 2, 0);
    lv_obj_set_style_border_color(live_container, lv_color_hex(0x00aaff), 0);

    lv_obj_t *live_title = lv_label_create(live_container);
    lv_label_set_text(live_title, "LIVE DASHBOARD");
    lv_obj_set_style_text_color(live_title, lv_color_hex(0x00aaff), 0);
    lv_obj_align(live_title, LV_ALIGN_TOP_MID, 0, 5);

    live_label=lv_label_create(live_container);
    lv_label_set_text(live_label,"Waiting for data...");
    lv_obj_set_style_text_color(live_label, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(live_label, &lv_font_montserrat_12, 0);
    lv_obj_align(live_label,LV_ALIGN_TOP_LEFT,10,30);

    // Page 2: Energy Analytics - IMPROVED UI
    lv_obj_t *energy_container = lv_obj_create(tile2);
    lv_obj_set_size(energy_container, 260, 180);
    lv_obj_set_pos(energy_container, 10, 10);
    lv_obj_set_style_bg_color(energy_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(energy_container, 2, 0);
    lv_obj_set_style_border_color(energy_container, lv_color_hex(0x00ff88), 0);

    lv_obj_t *energy_title = lv_label_create(energy_container);
    lv_label_set_text(energy_title, "ENERGY ANALYTICS");
    lv_obj_set_style_text_color(energy_title, lv_color_hex(0x00ff88), 0);
    lv_obj_align(energy_title, LV_ALIGN_TOP_MID, 0, 5);

    energy_label=lv_label_create(energy_container);
    lv_label_set_text(energy_label,"Calculating...");
    lv_obj_set_style_text_color(energy_label, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(energy_label, &lv_font_montserrat_12, 0);
    lv_obj_align(energy_label,LV_ALIGN_TOP_LEFT,10,30);

    // Page 3: Peak Analysis - IMPROVED UI
    lv_obj_t *peak_container = lv_obj_create(tile3);
    lv_obj_set_size(peak_container, 260, 180);
    lv_obj_set_pos(peak_container, 10, 10);
    lv_obj_set_style_bg_color(peak_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(peak_container, 2, 0);
    lv_obj_set_style_border_color(peak_container, lv_color_hex(0xff5500), 0);

    lv_obj_t *peak_title = lv_label_create(peak_container);
    lv_label_set_text(peak_title, "PEAK GUARD");
    lv_obj_set_style_text_color(peak_title, lv_color_hex(0xff5500), 0);
    lv_obj_align(peak_title, LV_ALIGN_TOP_MID, 0, 5);

    peak_label=lv_label_create(peak_container);
    lv_label_set_text(peak_label,"Analyzing...");
    lv_obj_set_style_text_color(peak_label, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(peak_label, &lv_font_montserrat_12, 0);
    lv_obj_align(peak_label,LV_ALIGN_TOP_LEFT,10,30);
}

static void create_device_ctrl_screen(void)
{
    device_ctrl_screen=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(device_ctrl_screen, lv_color_hex(0x1a1a1a), 0);

    // Back button
    lv_obj_t *back_btn=lv_btn_create(device_ctrl_screen);
    lv_obj_set_size(back_btn,60,30);
    lv_obj_align(back_btn,LV_ALIGN_TOP_LEFT,5,5);
    lv_obj_add_event_cb(back_btn,back_to_home,LV_EVENT_CLICKED,NULL);
    lv_obj_t *back_lbl=lv_label_create(back_btn);
    lv_label_set_text(back_lbl," Back");
    lv_obj_center(back_lbl);

    // Title
    lv_obj_t *title=lv_label_create(device_ctrl_screen);
    lv_label_set_text(title,"DEVICE CONTROL");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,10);

    int y=50;

    // Device 1 - IMPROVED UI
    lv_obj_t *dev1_container = lv_obj_create(device_ctrl_screen);
    lv_obj_set_size(dev1_container, 260, 50);
    lv_obj_set_pos(dev1_container, 10, y);
    lv_obj_set_style_bg_color(dev1_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(dev1_container, 2, 0);
    lv_obj_set_style_border_color(dev1_container, lv_color_hex(0x00aaff), 0);

    device1_btn=lv_btn_create(dev1_container);
    lv_obj_set_size(device1_btn,80,35);
    lv_obj_align(device1_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(device1_btn,toggle_device1,LV_EVENT_CLICKED,NULL);
    lv_obj_t *btn1_lbl=lv_label_create(device1_btn);
    lv_label_set_text(btn1_lbl,"Toggle");
    lv_obj_center(btn1_lbl);

    device1_label=lv_label_create(dev1_container);
    lv_label_set_text(device1_label,"Device1: OFF");
    lv_obj_set_style_text_color(device1_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(device1_label, LV_ALIGN_LEFT_MID, 10, 0);

    y+=60;

    // Device 2 - IMPROVED UI
    lv_obj_t *dev2_container = lv_obj_create(device_ctrl_screen);
    lv_obj_set_size(dev2_container, 260, 50);
    lv_obj_set_pos(dev2_container, 10, y);
    lv_obj_set_style_bg_color(dev2_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(dev2_container, 2, 0);
    lv_obj_set_style_border_color(dev2_container, lv_color_hex(0xffaa00), 0);

    device2_btn=lv_btn_create(dev2_container);
    lv_obj_set_size(device2_btn,80,35);
    lv_obj_align(device2_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(device2_btn,toggle_device2,LV_EVENT_CLICKED,NULL);
    lv_obj_t *btn2_lbl=lv_label_create(device2_btn);
    lv_label_set_text(btn2_lbl,"Toggle");
    lv_obj_center(btn2_lbl);

    device2_label=lv_label_create(dev2_container);
    lv_label_set_text(device2_label,"Device2: OFF");
    lv_obj_set_style_text_color(device2_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(device2_label, LV_ALIGN_LEFT_MID, 10, 0);

    y+=60;

    // Device 3 - IMPROVED UI
    lv_obj_t *dev3_container = lv_obj_create(device_ctrl_screen);
    lv_obj_set_size(dev3_container, 260, 50);
    lv_obj_set_pos(dev3_container, 10, y);
    lv_obj_set_style_bg_color(dev3_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(dev3_container, 2, 0);
    lv_obj_set_style_border_color(dev3_container, lv_color_hex(0xff5500), 0);

    device3_btn=lv_btn_create(dev3_container);
    lv_obj_set_size(device3_btn,80,35);
    lv_obj_align(device3_btn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(device3_btn,toggle_device3,LV_EVENT_CLICKED,NULL);
    lv_obj_t *btn3_lbl=lv_label_create(device3_btn);
    lv_label_set_text(btn3_lbl,"Toggle");
    lv_obj_center(btn3_lbl);

    device3_label=lv_label_create(dev3_container);
    lv_label_set_text(device3_label,"Device3: OFF");
    lv_obj_set_style_text_color(device3_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(device3_label, LV_ALIGN_LEFT_MID, 10, 0);
}

static void create_automation_screen(void)
{
    automation_screen=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(automation_screen, lv_color_hex(0x1a1a1a), 0);

    // Back button
    lv_obj_t *back_btn=lv_btn_create(automation_screen);
    lv_obj_set_size(back_btn,60,30);
    lv_obj_align(back_btn,LV_ALIGN_TOP_LEFT,5,5);
    lv_obj_add_event_cb(back_btn,back_to_home,LV_EVENT_CLICKED,NULL);
    lv_obj_t *back_lbl=lv_label_create(back_btn);
    lv_label_set_text(back_lbl,"  Back");
    lv_obj_center(back_lbl);

    // Title
    lv_obj_t *title=lv_label_create(automation_screen);
    lv_label_set_text(title,"AUTOMATION");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,10);

    int y = 50;

    // ===== SMART PRESENCE MODE =====
    lv_obj_t *presence_container = lv_obj_create(automation_screen);
    lv_obj_set_size(presence_container, 260, 70);
    lv_obj_set_pos(presence_container, 10, y);
    lv_obj_set_style_bg_color(presence_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(presence_container, 2, 0);
    lv_obj_set_style_border_color(presence_container, lv_color_hex(0x00aaff), 0);

    lv_obj_t *presence_title = lv_label_create(presence_container);
    lv_label_set_text(presence_title, "Smart Presence");
    lv_obj_set_style_text_color(presence_title, lv_color_hex(0x00aaff), 0);
    lv_obj_align(presence_title, LV_ALIGN_TOP_LEFT, 5, 5);

    presence_status_label = lv_label_create(presence_container);
    lv_label_set_text(presence_status_label, "Auto-off after 5s no motion");
    lv_obj_set_style_text_color(presence_status_label, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(presence_status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(presence_status_label, LV_ALIGN_TOP_LEFT, 5, 25);

    presence_switch = lv_switch_create(presence_container);
    lv_obj_set_size(presence_switch, 50, 25);
    lv_obj_align(presence_switch, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_add_event_cb(presence_switch, toggle_smart_presence, LV_EVENT_VALUE_CHANGED, NULL);

    y += 80;

    // ===== ADAPTIVE LIGHTING MODE =====
    lv_obj_t *adaptive_container = lv_obj_create(automation_screen);
    lv_obj_set_size(adaptive_container, 260, 70);
    lv_obj_set_pos(adaptive_container, 10, y);
    lv_obj_set_style_bg_color(adaptive_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(adaptive_container, 2, 0);
    lv_obj_set_style_border_color(adaptive_container, lv_color_hex(0xffaa00), 0);

    lv_obj_t *adaptive_title = lv_label_create(adaptive_container);
    lv_label_set_text(adaptive_title, "Adaptive Lighting");
    lv_obj_set_style_text_color(adaptive_title, lv_color_hex(0xffaa00), 0);
    lv_obj_align(adaptive_title, LV_ALIGN_TOP_LEFT, 5, 5);

    adaptive_status_label = lv_label_create(adaptive_container);
    lv_label_set_text(adaptive_status_label, "Auto-adjust ambient light");
    lv_obj_set_style_text_color(adaptive_status_label, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(adaptive_status_label, &lv_font_montserrat_12, 0);
    lv_obj_align(adaptive_status_label, LV_ALIGN_TOP_LEFT, 5, 25);

    adaptive_switch = lv_switch_create(adaptive_container);
    lv_obj_set_size(adaptive_switch, 50, 25);
    lv_obj_align(adaptive_switch, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_add_event_cb(adaptive_switch, toggle_adaptive_lighting, LV_EVENT_VALUE_CHANGED, NULL);

    y += 80;

    // ===== PEAK GUARD INFO =====
    lv_obj_t *peak_container = lv_obj_create(automation_screen);
    lv_obj_set_size(peak_container, 260, 60);
    lv_obj_set_pos(peak_container, 10, y);
    lv_obj_set_style_bg_color(peak_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(peak_container, 2, 0);
    lv_obj_set_style_border_color(peak_container, lv_color_hex(0xff5500), 0);

    lv_obj_t *peak_title = lv_label_create(peak_container);
    lv_label_set_text(peak_title, "Peak Guard (Always Active)");
    lv_obj_set_style_text_color(peak_title, lv_color_hex(0xff5500), 0);
    lv_obj_align(peak_title, LV_ALIGN_TOP_LEFT, 5, 5);

    peak_guard_label = lv_label_create(peak_container);
    lv_label_set_text(peak_guard_label, "Monitoring peak hours...");
    lv_obj_set_style_text_color(peak_guard_label, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(peak_guard_label, &lv_font_montserrat_12, 0);
    lv_obj_align(peak_guard_label, LV_ALIGN_TOP_LEFT, 5, 25);
}

static void create_fault_screen(void)
{
    fault_screen=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(fault_screen, lv_color_hex(0x1a1a1a), 0);

    // Back button
    lv_obj_t *back_btn=lv_btn_create(fault_screen);
    lv_obj_set_size(back_btn,60,30);
    lv_obj_align(back_btn,LV_ALIGN_TOP_LEFT,5,5);
    lv_obj_add_event_cb(back_btn,back_to_home,LV_EVENT_CLICKED,NULL);
    lv_obj_t *back_lbl=lv_label_create(back_btn);
    lv_label_set_text(back_lbl," Back");
    lv_obj_center(back_lbl);

    lv_obj_t *title=lv_label_create(fault_screen);
    lv_label_set_text(title,"FAULT MONITOR");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,10);

    // Fault container - IMPROVED UI
    lv_obj_t *fault_container = lv_obj_create(fault_screen);
    lv_obj_set_size(fault_container, 260, 150);
    lv_obj_set_style_bg_color(fault_container, lv_color_hex(0x2d2d2d), 0);
    lv_obj_set_style_border_width(fault_container, 2, 0);
    lv_obj_set_style_border_color(fault_container, lv_color_hex(0x00ff00), 0);
    lv_obj_align(fault_container, LV_ALIGN_CENTER, 0, 10);

    fault_label=lv_label_create(fault_container);
    lv_label_set_text(fault_label,"No fault detected");
    lv_obj_set_style_text_color(fault_label,lv_color_hex(0x00FF00),0);
    lv_obj_align(fault_label,LV_ALIGN_CENTER,0,0);
}

// ===== UI UPDATE TASK =====
static void ui_task(void *arg)
{
    char buf[512];

    while(1)
    {
        // Update clock
        update_clock();
        
        // Update peak analysis
        update_peak_analysis();

        // Update live dashboard
        sprintf(buf,
        "Motion: %s\n"
        "Current: %.2f A\n"
        "Power: %.1f W\n"
        "Energy: %.3f kWh\n"
        "Light: %d\n\n"
        "Presence: %s\n"
        "Adaptive: %s",
        g_motion?"Detected":"No motion",
        g_current,
        g_power,
        g_energy,
        g_ldr,
        smart_presence_mode?"ON":"OFF",
        adaptive_lighting_mode?"ON":"OFF");

        bsp_display_lock(0);
        if(live_label) lv_label_set_text(live_label,buf);
        bsp_display_unlock();

        // Update energy analytics
        float today_units=g_energy;
        float today_cost=today_units*COST_PER_UNIT;
        float month_est=today_cost*30.0;

        sprintf(buf,
        "Units today: %.3f kWh\n"
        "Cost today: Rs %.2f\n"
        "Monthly est: Rs %.2f\n\n"
        "Avg current: %.2f A\n"
        "Devices ON: %d",
        today_units,
        today_cost,
        month_est,
        g_current,
        (device1_state?1:0)+(device2_state?1:0)+(device3_state?1:0));

        bsp_display_lock(0);
        if(energy_label) lv_label_set_text(energy_label,buf);
        bsp_display_unlock();

        // Update Peak Guard display
        if(is_peak_hour){
            float peak_hour_cost = g_power * PEAK_COST_PER_UNIT / 1000.0;  // Cost per hour in peak
            float savings = peak_hour_cost - (g_power * COST_PER_UNIT / 1000.0);
            
            sprintf(buf,
            "PEAK HOUR ACTIVE!\n"
            "Time: 6-10 AM / 7-9 PM\n\n"
            "AVOID HEAVY LOADS\n\n"
            "Current: %.1f W\n"
            "Peak rate: Rs %.2f/hr\n"
            "Extra cost: +Rs %.2f/hr\n\n"
            "TIP: Defer non-essential\ndevices to save money!",
            g_power,
            peak_hour_cost,
            savings);
        }else{
            int hours_to_peak = next_peak_in_mins / 60;
            int mins_to_peak = next_peak_in_mins % 60;
            float normal_cost = g_power * COST_PER_UNIT / 1000.0;
            float peak_projection = g_power * PEAK_COST_PER_UNIT / 1000.0;
            float cost_diff = peak_projection - normal_cost;
            
            sprintf(buf,
            "Next peak in: %dh %dm\n"
            "Peak periods:\n"
            "6:00-10:00 AM\n"
            "6:00-10:00 PM\n\n"
            "Current: %.1f W\n"
            "Normal: Rs %.2f/hr\n"
            "Peak: Rs %.2f/hr\n\n"
            "If used in peak:\n"
            "+Rs %.2f/hr extra!",
            hours_to_peak,
            mins_to_peak,
            g_power,
            normal_cost,
            peak_projection,
            cost_diff);
        }

        bsp_display_lock(0);
        if(peak_label) lv_label_set_text(peak_label,buf);
        bsp_display_unlock();

        // Update automation screen status
        if(smart_presence_mode && no_motion_counter > 0){
            sprintf(buf, "No motion: %ds / %ds", no_motion_counter, NO_MOTION_TIMEOUT);
        }else if(smart_presence_mode){
            strcpy(buf, "Monitoring motion...");
        }else{
            strcpy(buf, "Auto-off after 5s no motion");
        }
        
        bsp_display_lock(0);
        if(presence_status_label) lv_label_set_text(presence_status_label, buf);
        bsp_display_unlock();

        // Update adaptive lighting status - FIXED LOGIC REFLECTED
        if(adaptive_lighting_mode){
            if(g_ldr <=2400){
                strcpy(buf, "Very Bright: Dev 1 ON");
            }else if(g_ldr <= 3500 ){
                strcpy(buf, "Minimal Bright: Device 1,2 ON");
            }else if(g_ldr == 4095){
                strcpy(buf, "Very dark: All ON");
            }else{
                strcpy(buf, "Bright: Minimal");
            }
        }else{
            strcpy(buf, "Auto-adjust ambient light");
        }
        
        bsp_display_lock(0);
        if(adaptive_status_label) lv_label_set_text(adaptive_status_label, buf);
        bsp_display_unlock();

        // Update peak guard status in automation screen
        if(is_peak_hour){
            strcpy(buf, "Peak hour! Avoid heavy loads");
        }else{
            int h = next_peak_in_mins / 60;
            int m = next_peak_in_mins % 60;
            sprintf(buf, "Next peak in %dh %dm", h, m);
        }
        
        bsp_display_lock(0);
        if(peak_guard_label) lv_label_set_text(peak_guard_label, buf);
        bsp_display_unlock();

        // Update fault display
        bsp_display_lock(0);
        if(fault_label){
            if(has_fault){
                lv_label_set_text(fault_label,fault_message);
                lv_obj_set_style_text_color(fault_label,lv_color_hex(0xFF0000),0);
            }else{
                lv_label_set_text(fault_label,"No fault detected");
                lv_obj_set_style_text_color(fault_label,lv_color_hex(0x00FF00),0);
            }
        }
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ===== WIFI =====
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI("WIFI", "WiFi started, connecting...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("WIFI", "Disconnected... retrying");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI("WIFI", "GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// ===== MAIN =====
void app_main(void)
{
    bsp_i2c_init();
    bsp_display_start();
    bsp_display_backlight_on();

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    // Create all screens
    bsp_display_lock(0);
    create_home_screen();
    create_analytics_screen();
    create_device_ctrl_screen();
    create_automation_screen();
    create_fault_screen();

    // Load home screen
    lv_scr_load(home_screen);
    bsp_display_unlock();

    wifi_init();
    start_webserver();

    xTaskCreate(ui_task,"ui",4096,NULL,2,NULL);
}