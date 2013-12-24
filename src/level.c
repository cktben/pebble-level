#include <pebble.h>
#include "libfixmath/fix16.h"

// Gives the number of items in an array.
// This must be used only on static arrays, not on arbitrary pointers.
#define COUNT(x) (sizeof(x) / sizeof(x[0]))

const char *STR_Backlight_On = "On";
const char *STR_Backlight_Normal = "Normal";

const char *STR_Filter_Level[] =
{
    "None",
    "Fast",
    "Medium",
    "Slow",
    "Very Slow"
};

typedef struct
{
    Window *window;
    SimpleMenuLayer *menu;
} simple_menu_t;

Window *display_window;
Layer *bubble_layer;

TextLayer *angle_layer;
char angle_text[32];

simple_menu_t main_menu;
simple_menu_t filter_menu;

// If true, the backlight stays on as long as this app is running.
bool force_backlight = false;

// Determines filter bandwidth.  See the comments in filter().
int filter_shift = 3;

// Normalized gravity vector.
fix16_t accel_normalized[3];

// Filter states.
int filter_state[3];

// Values for display_style.
enum
{
    DISPLAY_BUBBLE = 0,
    DISPLAY_CROSSHAIR,

    NUM_DISPLAY_STYLES
};

int display_style = DISPLAY_CROSSHAIR;

// Persistent storage keys.
// 
// Use explicit values to keep compatibility in future versions.
enum
{
    KEY_DISPLAY_STYLE = 0,
    KEY_BACKLIGHT     = 1,
    KEY_FILTER_SHIFT  = 2
};

void update_settings(void);

// Lowpass filter for accelerometer inputs.
//
// This is a one-pole IIR filter of the form:
//      y_n = (1 - 1/2^a) / 2^a * y_(n-1) + 1/2^a * x_n
// where a is filter_shift, which determines the cutoff frequency
// (higher values give lower filter bandwidth).
int filter(int *state, int input)
{
    *state = *state - (*state >> filter_shift) + input;
    return *state >> filter_shift;
}

void accel_handler(AccelData *data, uint32_t num_samples)
{
    // Ignore samples with excessive magnitude, which indicates movement.
    // We can't completely reject movement (a 3-axis gyro is required for that).
    // 
    // Dropping samples prevents outliers from contaminating the filter, but
    // it means that the filter will not run at a fixed sample rate when that happens.
    // The exact cutoff frequency is not critical, so it doesn't matter.
    // 
    // Under heavy vibration this may make the display freeze frequently.
    // I don't see a good solution to that.  Filtering isn't really the right solution
    // because the accelerometer may be rotating (not in an inertial frame).
    // In practice this is unlikely to be a serious problem.
    // 
    // Any sample while vibrating is also discarded.  Maybe this could happen if a notification
    // occurs while the app is running.
    if (!data->did_vibrate && abs(data->x) < 1200 && abs(data->y) < 1200 && abs(data->z) < 1200)
    {
        // Convert and normalize the acceleration vector.
        int32_t accel_raw[3] =
        {
            data->x << 4,
            data->y << 4,
            data->z << 4
        };

        fix16_t magsq = 0;
        for (int i = 0; i < 3; i++)
        {
            accel_raw[i] = filter(&filter_state[i], accel_raw[i]);

            // Find the magnitude-squared of the acceleration vector.
            //
            // This is not fix16_t because it will be too large.
            // This will not overflow because the maximum acceleration is limited above.
            magsq += accel_raw[i] * accel_raw[i];
        }

        // Normalize the acceleration vector.
        fix16_t mag = fix16_sqrt(magsq) << 8;
        for (int i = 0; i < 3; i++)
        {
            accel_normalized[i] = fix16_div(fix16_from_int(accel_raw[i]), mag);
        }

        // Get the angle from vertical in integer decidegrees.
        int a = fix16_acos(fix16_abs(accel_normalized[2])) * 1800 / fix16_pi;

        // Get decimal integer and fractional parts.
        int i = a / 10;
        int f = a % 10;

        // Update the angle text.
        snprintf(angle_text, sizeof(angle_text), "%d.%d\u00B0", i, f);
        text_layer_set_text(angle_layer, angle_text);

        // Redraw the display layer.
        layer_mark_dirty(bubble_layer);
    }
}

void draw_bubble(Layer *layer, GContext *ctx)
{
    GRect bounds = layer_get_bounds(layer);

    // Half size of the layer.
    int hw = bounds.size.w / 2;
    int hh = bounds.size.h / 2;

    // Center position for the mark.
    int cx = hw;
    int cy = hh;

    // Offset from center to the position of the mark.
    int ox = accel_normalized[0] * hw * 4 / fix16_one;
    int oy = accel_normalized[1] * hh * 4 / fix16_one;

    // Keep the mark on the screen.
    if (ox > hw)
    {
        ox = hw;
    } else if (ox < -hw)
    {
        ox = -hw;
    }

    if (oy > hh)
    {
        oy = hh;
    } else if (oy < -hh)
    {
        oy = -hh;
    }

    // Position of the mark.
    int x = cx - ox;
    int y = cy + oy;

    if (display_style == DISPLAY_BUBBLE)
    {
        graphics_draw_circle(ctx, GPoint(cx, cy), 20);
        graphics_fill_circle(ctx, GPoint(x, y), 20);
    } else if (display_style == DISPLAY_CROSSHAIR)
    {
        graphics_draw_line(ctx, GPoint(cx - 20, cy), GPoint(cx - 10, cy));
        graphics_draw_line(ctx, GPoint(cx + 10, cy), GPoint(cx + 20, cy));
        graphics_draw_line(ctx, GPoint(cx, cy - 20), GPoint(cx, cy - 10));
        graphics_draw_line(ctx, GPoint(cx, cy + 20), GPoint(cx, cy + 10));

        graphics_draw_line(ctx, GPoint(x - 10, y), GPoint(x + 10, y));
        graphics_draw_line(ctx, GPoint(x, y - 10), GPoint(x, y + 10));
    }

    // Draw a line at the top of the layer to separate it from the angle text.
    graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w, 0));
}

void handle_up()
{
    if (display_style > 0)
    {
        display_style--;
    } else {
        display_style = NUM_DISPLAY_STYLES - 1;
    }

    layer_mark_dirty(bubble_layer);
}

void handle_down()
{
    if (display_style < (NUM_DISPLAY_STYLES - 1))
    {
        display_style++;
    } else {
        display_style = 0;
    }

    layer_mark_dirty(bubble_layer);
}

void handle_select()
{
    window_stack_push(main_menu.window, true);
}

void ccp_level(void *context)
{
    window_single_click_subscribe(BUTTON_ID_UP, handle_up);
    window_single_click_subscribe(BUTTON_ID_DOWN, handle_down);
    window_single_click_subscribe(BUTTON_ID_SELECT, handle_select);
}

void window_load(Window *window)
{
    window_set_click_config_provider(window, ccp_level);

    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    angle_layer = text_layer_create(GRect(0, -4, 144, 24));
    text_layer_set_text_alignment(angle_layer, GTextAlignmentCenter);
    text_layer_set_font(angle_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
    layer_add_child(window_layer, text_layer_get_layer(angle_layer));

    bubble_layer = layer_create(GRect(0, 24, 144, bounds.size.h - 24));
    layer_set_update_proc(bubble_layer, draw_bubble);
    layer_add_child(window_layer, bubble_layer);

    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    accel_data_service_subscribe(1, accel_handler);
}

void window_unload(Window *window)
{
    accel_data_service_unsubscribe();
    layer_destroy(bubble_layer);
    text_layer_destroy(angle_layer);
}

void toggle_backlight(int index, void *context)
{
    force_backlight = !force_backlight;
    persist_write_bool(KEY_BACKLIGHT, force_backlight);
    update_settings();
}

void change_filter(int index, void *context)
{
    simple_menu_layer_set_selected_index(filter_menu.menu, filter_shift, false);
    window_stack_push(filter_menu.window, true);
}

void select_filter(int index, void *context)
{
    filter_shift = index;
    persist_write_int(KEY_FILTER_SHIFT, filter_shift);
    update_settings();
    window_stack_pop(true);
}

SimpleMenuItem main_menu_items[] =
{
    {
        .title = "Backlight",
        .callback = toggle_backlight
    },
    {
        .title = "Filtering",
        .callback = change_filter
    }
};
SimpleMenuSection main_menu_sections[] =
{
    {
        .items = main_menu_items,
        .num_items = COUNT(main_menu_items),
        .title = "Settings"
    }
};

SimpleMenuItem filter_menu_items[COUNT(STR_Filter_Level)];
SimpleMenuSection filter_menu_sections[] =
{
    {
        .items = filter_menu_items,
        .num_items = COUNT(filter_menu_items),
        .title = "Filtering"
    }
};

void update_settings()
{
    light_enable(force_backlight);
    main_menu_items[0].subtitle = force_backlight ? STR_Backlight_On : STR_Backlight_Normal;
    main_menu_items[1].subtitle = STR_Filter_Level[filter_shift];
    layer_mark_dirty(simple_menu_layer_get_layer(main_menu.menu));
}

void menu_setup(simple_menu_t *menu, SimpleMenuSection *sections, int num_sections)
{
    menu->window = window_create();
    Layer *menu_root_layer = window_get_root_layer(menu->window);
    GRect rect = layer_get_bounds(menu_root_layer);
    menu->menu = simple_menu_layer_create(rect, menu->window, sections, num_sections, NULL);
    layer_add_child(menu_root_layer, simple_menu_layer_get_layer(menu->menu));
}

// Reads a value from persistent storage, but only if it exists.
void persist_check_int(uint32_t key, int *value)
{
    if (persist_exists(key))
    {
        *value = persist_read_int(key);
    }
}

void init(void)
{
    // Read settings from persistent storage.

    persist_check_int(KEY_DISPLAY_STYLE, &display_style);
    persist_check_int(KEY_FILTER_SHIFT, &filter_shift);

    if (persist_exists(KEY_BACKLIGHT))
    {
        force_backlight = persist_read_bool(KEY_BACKLIGHT);
    }

    // Create windows.

    display_window = window_create();
    window_set_window_handlers(display_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(display_window, true);

    menu_setup(&main_menu, main_menu_sections, COUNT(main_menu_sections));
    update_settings();

    for (unsigned int i = 0; i < COUNT(filter_menu_items); i++)
    {
        filter_menu_items[i].title = STR_Filter_Level[i];
        filter_menu_items[i].callback = select_filter;
    }
    menu_setup(&filter_menu, filter_menu_sections, COUNT(filter_menu_sections));
}

void deinit(void)
{
    window_destroy(display_window);
    window_destroy(main_menu.window);
    simple_menu_layer_destroy(main_menu.menu);
    window_destroy(filter_menu.window);
    simple_menu_layer_destroy(filter_menu.menu);
    light_enable(false);

    // Display style is changed in the level display, not the menu, so write it here.
    persist_write_int(KEY_DISPLAY_STYLE, display_style);
}

int main(void)
{
    init();
    app_event_loop();
    deinit();
}
