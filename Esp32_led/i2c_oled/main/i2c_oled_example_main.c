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


//Spinner parameters
static lv_obj_t *spinner_obj;


extern void example_lvgl_demo_ui(lv_disp_t *disp);
esp_err_t create_tasks( lv_disp_t *disp );
void ball_task( void * pvParameters );
void spinner_task( void * pvParameters );

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
    create_tasks(disp); //Llamo a la funcion para crear la tarea, la cual es la que va a ejecutar el lvgl_task, la cual es la que va a ejecutar el lv_timer_handler, el cual es el que va a actualizar la pantalla.
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

        // Center the content on the screen
        lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_main_place(scr, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(scr, LV_FLEX_ALIGN_CENTER, 0);        

        spinner_obj = lv_spinner_create(scr, 1500, 180); // Create a spinner with: lv_spinner_create(parent, spin_time_ms, arc_angle)
        lv_obj_set_size(spinner_obj, 35, 35); // Set the size of the spinner to 50x50 pixels

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

 esp_err_t create_tasks( lv_disp_t *disp ) //Funcion para poder crear tareas, la cual es llamada desde el main, el esp_err_t es para poder retornar un error en caso de que no se pueda crear la tarea.
  {
  TaskHandle_t xHandle = NULL; // Varaible que se utiliza para ver si la task fue creada correctamente, si no es asi, se retorna un error.
 
  /*
    xTaskCreate( ball_task,   //Funcion para crear la tarea, la cual recibe como primer argumento el nombre de la funcion que se va a ejecutar en la tarea, el segundo argumento es el nombre de la tarea, el tercer argumento es el tamaño de la pila de la tarea, el cuarto argumento es un puntero a los parametros que se le van a pasar a la tarea, el quinto argumento es la prioridad de la tarea y el sexto argumento es un puntero a una variable que va a contener el handle de la tarea.
        "Bouncing_ball", 
        STACK_SIZE, 
        disp, 
        5, 
        &xHandle );
    */
        xTaskCreate( spinner_task,   //Funcion para crear la tarea, la cual recibe como primer argumento el nombre de la funcion que se va a ejecutar en la tarea, el segundo argumento es el nombre de la tarea, el tercer argumento es el tamaño de la pila de la tarea, el cuarto argumento es un puntero a los parametros que se le van a pasar a la tarea, el quinto argumento es la prioridad de la tarea y el sexto argumento es un puntero a una variable que va a contener el handle de la tarea.
        "Spinner", 
        STACK_SIZE, 
        disp, 
        5, 
        &xHandle );    

 return ESP_OK; // Retorna un error en caso de que no se pueda crear la tarea.
  }