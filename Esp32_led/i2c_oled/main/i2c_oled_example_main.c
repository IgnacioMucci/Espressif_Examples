/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "esp_timer.h"

#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
#include "esp_lcd_sh1107.h"
#else
#include "esp_lcd_panel_vendor.h"
#endif

static const char *TAG = "example";

#define I2C_BUS_PORT  0

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ    (400 * 1000)
#define EXAMPLE_PIN_NUM_SDA           5
#define EXAMPLE_PIN_NUM_SCL           4
#define EXAMPLE_PIN_NUM_RST           -1
#define EXAMPLE_I2C_HW_ADDR           0x3C
#define EXAMPLE_I2C_TRANSACTION_TIMEOUT_MS 1000

// The pixel number in horizontal and vertical
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SSD1306
#define EXAMPLE_LCD_H_RES              128
#define EXAMPLE_LCD_V_RES              CONFIG_EXAMPLE_SSD1306_HEIGHT
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
#define EXAMPLE_LCD_H_RES              64
#define EXAMPLE_LCD_V_RES              128
#endif
// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8

#define STACK_SIZE 2048*2


//Ball parameters 
static lv_obj_t *ball_obj;
static int16_t ball_x = 10;
static int16_t ball_y = 10;
static int16_t ball_dx = 2;
static int16_t ball_dy = 2;
static bool ball_state = true;


//Spinner parameters
static lv_obj_t *spinner_obj;

TaskHandle_t ballTaskHandle = NULL; //Global variable to hold the handle of the ball task


extern void example_lvgl_demo_ui(lv_disp_t *disp);
esp_err_t create_tasks( lv_disp_t *disp ); // Function to create tasks, which is called from the main function, the esp_err_t is used to return an error if the task cannot be created.
void ball_task( void * pvParameters );
void spinner_task( void * pvParameters );
void animation_controller_task( void * pvParameters ); // Function to create a task that will control the animation, switching between the ball and the spinner every 5 seconds.
void timer_init(); // Function to initialize the timer that will call the lvgl_timer_handler function every 5 seconds to switch between the ball and the spinner.
static void lvgl_timer_handler(void* arg); // Function to handle the timer callback, which will be called every 5 seconds to switch between the ball and the spinner.


void app_main(void)
{
    ESP_LOGI(TAG, "Initialize I2C bus");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = EXAMPLE_I2C_HW_ADDR,
        .scl_speed_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .transaction_timeout_ms = EXAMPLE_I2C_TRANSACTION_TIMEOUT_MS,
        .control_phase_bytes = 1,               // According to SSD1306 datasheet
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,   // According to SSD1306 datasheet
        .lcd_param_bits = EXAMPLE_LCD_CMD_BITS, // According to SSD1306 datasheet
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SSD1306
        .dc_bit_offset = 6,                     // According to SSD1306 datasheet
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
        .dc_bit_offset = 0,                     // According to SH1107 datasheet
        .flags =
        {
            .disable_control_phase = 1,
        }
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
    };
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SSD1306
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = EXAMPLE_LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh1107(io_handle, &panel_config, &panel_handle));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif

    ESP_LOGI(TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES,
        .double_buffer = true,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        }
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);

    /* Rotation of the screen */
    lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);

    ESP_LOGI(TAG, "Display LVGL Scroll Text");

    create_tasks(disp); //Call the function to create the task, the one that will execute the lvgl_task, the one that will execute the lv_timer_handler, the one that will update the screen.
    timer_init(); //Initialize the timer that will call the lv_timer_handler function every 5 seconds to switch between the ball and the spinner.
}
 


void ball_task( void * pvParameters ) 
{
    // Cast the generic parameter back to a display pointer
    lv_disp_t *disp = (lv_disp_t *)pvParameters;
    
    // 1. Get the active screen from the display
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // 2. Wait up to 100ms for the mutex to ensure safe UI creation
    if (lvgl_port_lock(100)) {
        
        // Create the base object on the active screen
        ball_obj = lv_obj_create(scr); 
        lv_obj_set_size(ball_obj, 12, 12); 
        
        // Apply a 50% radius to turn the square into a perfect circle
        lv_obj_set_style_radius(ball_obj, LV_RADIUS_CIRCLE, 0); 
        
        // Force 100% opacity (cover) so it's not a transparent ghost
        lv_obj_set_style_bg_opa(ball_obj, LV_OPA_COVER, 0); 
        lv_obj_set_style_bg_color(ball_obj, lv_color_black(), 0); 
        lv_obj_set_style_border_width(ball_obj, 0, 0); 
        
        // Set the initial position
        lv_obj_set_pos(ball_obj, ball_x, ball_y); 
        
        // Release the mutex
        lvgl_port_unlock();
    }

    // 3. Infinite animation loop
    while (1) {
        // Suspend task for 30ms (~33 FPS) to yield CPU to other tasks
        vTaskDelay(pdMS_TO_TICKS(30)); 

        // Request UI access again with a 100ms timeout
        if (lvgl_port_lock(100)) {
            
            // FAILSAFE: Only calculate and move if the object actually exists
            if (ball_obj != NULL) {
                
                // Update coordinates based on current velocity
                ball_x += ball_dx; 
                ball_y += ball_dy; 

                // X-axis collision detection (Left and Right borders)
                if (ball_x <= 0) {
                    ball_x = 0;
                    ball_dx *= -1; // Reverse horizontal direction
                } else if (ball_x >= (EXAMPLE_LCD_H_RES - 12)) {
                    ball_x = EXAMPLE_LCD_H_RES - 12;
                    ball_dx *= -1;
                }

                // Y-axis collision detection (Top and Bottom borders)
                if (ball_y <= 0) {
                    ball_y = 0;
                    ball_dy *= -1; // Reverse vertical direction
                } else if (ball_y >= (EXAMPLE_LCD_V_RES - 12)) {
                    ball_y = EXAMPLE_LCD_V_RES - 12;
                    ball_dy *= -1;
                }

                // Apply the new calculated coordinates to the LVGL object
                lv_obj_set_pos(ball_obj, ball_x, ball_y); 
            }
            
            // Release the mutex so the background task can render the frame
            lvgl_port_unlock();
        }
    }
}


void spinner_task( void * pvParameters ) 
{
    // Cast the generic parameter back to a display pointer
    lv_disp_t *disp = (lv_disp_t *)pvParameters;
    
    // Get the active screen from the display
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // Wait up to 100ms for the mutex to ensure safe UI creation
    if (lvgl_port_lock(100)) {
        
        //Force the background color to black for safety
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

        spinner_obj = lv_spinner_create(scr, 1500, 180); // Create a spinner with: lv_spinner_create(parent, spin_time_ms, arc_angle)
        lv_obj_set_size(spinner_obj, 35, 35); // Set the size of the spinner to 50x50 pixels

        lv_obj_center(spinner_obj); // Align the spinner to the center of the screen
        lv_obj_set_style_arc_color(spinner_obj, lv_color_black(), LV_PART_MAIN); // LV_PART_MAIN is the background ring 
        lv_obj_set_style_arc_color(spinner_obj, lv_color_white(), LV_PART_INDICATOR);  // LV_PART_INDICATOR is the spinning portion
        
       
        lv_obj_set_style_arc_width(spinner_obj, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner_obj, 2, LV_PART_INDICATOR);

        // Release the mutex
        lvgl_port_unlock();
    }

    // The task already created the UI and finished its job, it must delete itself.
    vTaskDelete(NULL);
}

static void lvgl_timer_handler(void* arg) {
    
    
    if (lvgl_port_lock(0)) {
        if (ball_state) { // If the ball is currently visible, hide it and show the spinner
             if(ball_obj) lv_obj_add_flag(ball_obj, LV_OBJ_FLAG_HIDDEN);
            if(spinner_obj) lv_obj_clear_flag(spinner_obj, LV_OBJ_FLAG_HIDDEN);
        } else {// If the spinner is currently visible, hide it and show the ball
             if(spinner_obj) lv_obj_add_flag(spinner_obj, LV_OBJ_FLAG_HIDDEN);
            if(ball_obj) lv_obj_clear_flag(ball_obj, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
        if (ball_state) {
             vTaskSuspend(ballTaskHandle); // Suspend the ball movement task to freeze the ball in place
        } else {
             vTaskResume(ballTaskHandle);  // Resume the ball movement task
        }
        ball_state = !ball_state; // Toggle the state of the ball (true = show ball, false = show spinner)
    }
}


 void timer_init() {
    const esp_timer_create_args_t timer_args = { // Arguments to create the timer
        .callback = &lvgl_timer_handler, // Callback function that will be called when the timer expires
        .name = "timer_animacion", // Name of the timer
        .arg = NULL,               // Argument to pass to the callback function (not used here)            
        .dispatch_method = ESP_TIMER_TASK  // The timer callback will be called from the timer task context
    };

    esp_timer_handle_t lvgl_timer;// Handle for the timer
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &lvgl_timer));// Create the timer with the specified arguments
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_timer, 10 * 500000)); // 5000ms
    lvgl_timer_handler(NULL); // Call the handler once to set the initial state of the ball and spinner
 }


/*
void animation_controller_task(void *pvParameters) {
    //Give some time for both the animations to start and display their initial state
    vTaskDelay(pdMS_TO_TICKS(500));

    while(1) {
    
        vTaskSuspend(ballTaskHandle); // Suspend the ball movement task to freeze the ball in place
        
        if (lvgl_port_lock(100)) {
            // Hide the ball and show the spinner
            if(ball_obj) lv_obj_add_flag(ball_obj, LV_OBJ_FLAG_HIDDEN);
            if(spinner_obj) lv_obj_clear_flag(spinner_obj, LV_OBJ_FLAG_HIDDEN);
            lvgl_port_unlock();
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // wait 5 seconds

    
        if (lvgl_port_lock(100)) {
            // Hide the spinner and show the ball
            if(spinner_obj) lv_obj_add_flag(spinner_obj, LV_OBJ_FLAG_HIDDEN);
            if(ball_obj) lv_obj_clear_flag(ball_obj, LV_OBJ_FLAG_HIDDEN);
            lvgl_port_unlock();
        }
        
        vTaskResume(ballTaskHandle); // Resume the ball movement task
        vTaskDelay(pdMS_TO_TICKS(5000)); // wait 5 seconds
    }
}
    */

 esp_err_t create_tasks( lv_disp_t *disp ) //Function to create tasks, which is called from the main function, the esp_err_t is used to return an error if the task cannot be created.
  {
  TaskHandle_t xHandle = NULL; // Variable used to check if the task was created correctly, if not, an error is returned.
 
  
    xTaskCreate( ball_task,   //Function to create the task, which receives as the first argument the name of the function to be executed in the task, the second argument is the name of the task, the third argument is the size of the task stack, the fourth argument is a pointer to the parameters to be passed to the task, the fifth argument is the priority of the task and the sixth argument is a pointer to a variable that will contain the handle of the task.
        "Bouncing_ball", 
        STACK_SIZE, 
        disp, 
        5, 
        &ballTaskHandle ); // Store the handle of the ball task in the global variable ballTaskHandle so it can be suspended and resumed later.

        xTaskCreate( spinner_task,   //Function to create the task, which receives as the first argument the name of the function to be executed in the task, the second argument is the name of the task, the third argument is the size of the task stack, the fourth argument is a pointer to the parameters to be passed to the task, the fifth argument is the priority ofthe task andthe sixth argument is a pointer to a variable that will containthe handle ofthe task.
        "Spinner", 
        STACK_SIZE, 
        disp, 
        5, 
        &xHandle );
        
        /*
        xTaskCreate( animation_controller_task,   //Function to create the task, which receives as the first argument the name of the function to be executed in the task, the second argument is the name of the task, the third argument is the size of the task stack, the fourth argument is a pointer to the parameters to be passed to the task, the fifth argument is the priority ofthe task andthe sixth argument is a pointer to a variable that will containthe handle ofthe task.
        "Handler", 
        STACK_SIZE, 
        NULL, 
        10, 
        &xHandle );    
            */
 return ESP_OK; // Returns an error if the task cannot be created.
  }