#include <pebble.h>

// Period of accelerometer updates, in milliseconds.
#define ACCEL_UPDATE_PERIOD 67

Window *window;
Layer *bubble_layer;

AppTimer *accel_timer;

int nx, ny, nz;
int magsq;

extern const int16_t acos_table[1024];

// Fixed-point inverse cosine.
// x is the input scaled by 1024.
// Returns a positive angle in tenths of a degree.
int16_t acos_fixed(int x)
{
    if (x > 1023)
    {
        return 0;
    } else if (x < -1023)
    {
        return 1800;
    } else if (x >= 0)
    {
        // Positive input: 0-90 degrees
        return acos_table[x];
    } else {
        // Negative input: 90-180 degrees
        return 1800 - acos_table[-x];
    }
}

// http://www.finesse.demon.co.uk/steven/sqrt.html
unsigned int sqrti(unsigned int n)
{
   unsigned int root = 0, bit, trial;
   bit = (n >= 0x10000) ? 1<<30 : 1<<14;
   do
   {
      trial = root+bit;
      if (n >= trial)
      {
         n -= trial;
         root = trial+bit;
      }
      root >>= 1;
      bit >>= 2;
   } while (bit);
   return root;
}

void update_accel(void *param)
{
    layer_mark_dirty(bubble_layer);

    AccelData data;
    accel_service_peek(&data);

    // Find the magnitude-squared of the acceleration vector.
    magsq = data.x * data.x + data.y * data.y + data.z * data.z;

    // Ignore samples with excessive magnitude, which indicates movement.
    // We can't completely reject movement (a 3-axis gyro is required for that).
    if (magsq >= 640000 && magsq <= 1440000)
    {
        // Normalize the vector and scale it to 1024.
        // This makes it 21.10 fixed point, which is convenient for later processing
        // and keeps the original resolution.
        int mag = sqrti(magsq);
        nx = data.x * 1024 / mag;
        ny = data.y * 1024 / mag;
        nz = data.z * 1024 / mag;
    }

    accel_timer = app_timer_register(ACCEL_UPDATE_PERIOD, update_accel, NULL);
}

void accel_handler(AccelData *data, uint32_t num_samples)
{
}

void draw_bubble(Layer *layer, GContext *ctx)
{
    GRect bounds = layer_get_bounds(layer);
    int w = bounds.size.w;
    int h = bounds.size.h;

    int cx = w / 2;
    int cy = h / 2;
    graphics_draw_circle(ctx, GPoint(cx, cy), 20);

    int x = cx - nx * w / 2048;
    int y = cy + ny * h / 2048;
    graphics_fill_circle(ctx, GPoint(x, y), 20);

    char buf[32];
    int a = acos_fixed(abs(nz));
    int i = a / 10;
    int f = a % 10;
    snprintf(buf, sizeof(buf), "%d.%d\u00B0", i, f);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24), GRect(0, 0, 144, 168), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

void window_load(Window *window)
{
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    bubble_layer = layer_create(bounds);
    layer_set_update_proc(bubble_layer, draw_bubble);
    layer_add_child(window_layer, bubble_layer);

    accel_service_set_sampling_rate(10);
    accel_data_service_subscribe(0, accel_handler);
    update_accel(NULL);
}

void window_unload(Window *window)
{
    app_timer_cancel(accel_timer);
    accel_data_service_unsubscribe();
    layer_destroy(bubble_layer);
}

void init(void)
{
    window = window_create();
    window_set_window_handlers(window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });
    window_stack_push(window, true);
}

void deinit(void)
{
    window_destroy(window);
}

int main(void)
{
    init();
    app_event_loop();
    deinit();
}
