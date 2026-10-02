#include "gm_plugin_lvgl_api.h"
#include "gm_plugin_libc.h"

/* Standalone glasses app (no phone): a stopwatch with a lap counter. Head up
 * starts/resumes, head down pauses, the primary button records a lap and a long
 * press resets everything. Useful for interval training where the wearer wants
 * splits without touching a device. */

#define MAX_FORMAT 16

typedef struct {
    const gm_plugin_host_api_t *host;
    const gm_plugin_lvgl_api_t *ui;
    const gm_plugin_libc_extension_api_t *libc;
    gm_plugin_lvgl_obj_t *screen;
    gm_plugin_lvgl_obj_t *total_label;
    gm_plugin_lvgl_obj_t *state_label;
    gm_plugin_lvgl_obj_t *lap_label;
    gm_plugin_lvgl_obj_t *hint_label;

    bool running;
    uint32_t total_ms;    /* accumulated running time */
    uint32_t lap_start_ms; /* total_ms value when the current lap began */
    uint32_t last_lap_ms;
    uint16_t lap_count;
    uint32_t refresh_elapsed_ms;
    uint8_t language; /* 0 = French (default), 1 = English */
} timer_t;

/* Picks the French or English string depending on the detected UI locale. */
#define L(fr_str, en_str) (tm.language != 0U ? (en_str) : (fr_str))

static timer_t tm;

#define number gm_plugin_lvgl_style_number
#define color gm_plugin_lvgl_style_color

static void set_style(gm_plugin_lvgl_obj_t *object,
                       gm_plugin_lvgl_style_prop_t property,
                       gm_plugin_lvgl_style_value_t value)
{
    tm.ui->style_set(object, property, value, GM_PLUGIN_LVGL_SELECTOR_MAIN);
}

static void style_panel(gm_plugin_lvgl_obj_t *object, uint8_t fill,
                         uint8_t border, uint8_t radius)
{
    set_style(object, GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(fill));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BG_OPA, number(255));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BORDER_COLOR, color(0xA0));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BORDER_OPA, number(255));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BORDER_WIDTH, number(border));
    set_style(object, GM_PLUGIN_LVGL_STYLE_RADIUS, number(radius));
    tm.ui->obj_clear_flag(object, GM_PLUGIN_LVGL_FLAG_SCROLLABLE);
}

static gm_plugin_lvgl_obj_t *make_label(gm_plugin_lvgl_obj_t *parent,
                                         const char *text, uint8_t shade)
{
    gm_plugin_lvgl_obj_t *label = tm.ui->label_create(parent);
    if (label == 0) return 0;
    tm.ui->label_set_text(label, text);
    tm.ui->label_set_long_mode(label, GM_PLUGIN_LVGL_LABEL_DOT);
    set_style(label, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(shade));
    set_style(label, GM_PLUGIN_LVGL_STYLE_TEXT_ALIGN,
              number(GM_PLUGIN_LVGL_TEXT_ALIGN_CENTER));
    return label;
}

/* mm:ss.cc, with minutes capped so the small display never overflows. */
static void format_time(char *buffer, size_t size, uint32_t ms)
{
    uint32_t minutes = ms / 60000U;
    uint32_t seconds = (ms / 1000U) % 60U;
    uint32_t centis = (ms / 10U) % 100U;
    if (minutes > 99U) minutes = 99U;
    tm.libc->snprintf(buffer, size, "%02u:%02u.%02u",
                      (unsigned int)minutes, (unsigned int)seconds,
                      (unsigned int)centis);
}

static void refresh_display(void)
{
    char text[MAX_FORMAT];
    format_time(text, sizeof(text), tm.total_ms);
    tm.ui->label_set_text(tm.total_label, text);

    if (tm.running) {
        tm.ui->label_set_text(tm.state_label, L("En cours", "Running"));
        set_style(tm.state_label, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(0xF0));
    } else if (tm.total_ms == 0U) {
        tm.ui->label_set_text(tm.state_label, L("Pret", "Ready"));
        set_style(tm.state_label, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(0x90));
    } else {
        tm.ui->label_set_text(tm.state_label, L("En pause", "Paused"));
        set_style(tm.state_label, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(0xC0));
    }

    if (tm.lap_count == 0U) {
        tm.ui->label_set_text(tm.lap_label, L("Aucun tour", "No lap yet"));
    } else {
        char lap_time[MAX_FORMAT];
        format_time(lap_time, sizeof(lap_time), tm.last_lap_ms);
        tm.libc->snprintf(text, sizeof(text), L("Tour %u - %s", "Lap %u - %s"),
                          (unsigned int)tm.lap_count, lap_time);
        tm.ui->label_set_text(tm.lap_label, text);
    }

    tm.ui->label_set_text(tm.hint_label,
        L("Tete haut: demarrer   Tete bas: pause",
          "Head up: start   Head down: pause"));
}

static void add_lap(void)
{
    uint32_t now = tm.total_ms;
    tm.last_lap_ms = now - tm.lap_start_ms;
    tm.lap_start_ms = now;
    if (tm.lap_count < 0xFFFFU) tm.lap_count++;
    refresh_display();
}

static void set_running(bool running)
{
    if (running == tm.running) return;
    tm.running = running;
    tm.refresh_elapsed_ms = 100U; /* repaint promptly on state change */
    refresh_display();
}

static void reset_timer(void)
{
    tm.running = false;
    tm.total_ms = 0U;
    tm.lap_start_ms = 0U;
    tm.last_lap_ms = 0U;
    tm.lap_count = 0U;
    refresh_display();
}

static gm_plugin_result_t timer_start(void *context)
{
    gm_plugin_display_info_t display;
    gm_plugin_lvgl_obj_t *root;
    char locale[GM_PLUGIN_LOCALE_TAG_MAX];
    (void)context;

    tm.language = 0U;
    if (tm.host->locale_get != 0 &&
        tm.host->locale_get(locale) == GM_PLUGIN_OK &&
        locale[0] == 'e' && locale[1] == 'n')
        tm.language = 1U;

    if (tm.host->display_get_info(&display) != GM_PLUGIN_OK ||
        display.width < 80U || display.height < 120U)
        return GM_PLUGIN_ESTATE;

    root = tm.ui->root_get();
    if (root == 0) return GM_PLUGIN_ESTATE;
    tm.ui->obj_clean(root);

    tm.screen = tm.ui->obj_create(root);
    if (tm.screen == 0) return GM_PLUGIN_ENOMEM;
    tm.ui->obj_set_size(tm.screen, display.width, display.height);
    tm.ui->obj_align(tm.screen, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 0);
    style_panel(tm.screen, 0x00, 0, 0);

    tm.state_label = make_label(tm.screen, "", 0x90);
    tm.total_label = make_label(tm.screen, "00:00.00", 0xFF);
    tm.lap_label = make_label(tm.screen, "", 0xC0);
    tm.hint_label = make_label(tm.screen, "", 0x70);
    if (tm.state_label == 0 || tm.total_label == 0 || tm.lap_label == 0 ||
        tm.hint_label == 0)
        goto no_memory;

    {
        gm_plugin_lvgl_style_value_t large_font = {0};
        large_font.ptr = tm.ui->font_large;
        set_style(tm.total_label, GM_PLUGIN_LVGL_STYLE_TEXT_FONT, large_font);
    }

    tm.ui->obj_set_size(tm.state_label, display.width - 40U, 28);
    tm.ui->obj_align(tm.state_label, GM_PLUGIN_LVGL_ALIGN_TOP_MID, 0, 16);

    tm.ui->obj_set_size(tm.total_label, display.width - 20U, 56);
    tm.ui->obj_align(tm.total_label, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, -12);

    tm.ui->obj_set_size(tm.lap_label, display.width - 20U, 28);
    tm.ui->obj_align(tm.lap_label, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 40);

    tm.ui->obj_set_size(tm.hint_label, display.width - 20U, 30);
    tm.ui->obj_align(tm.hint_label, GM_PLUGIN_LVGL_ALIGN_BOTTOM_MID, 0, -10);

    tm.running = false;
    tm.total_ms = 0U;
    tm.lap_start_ms = 0U;
    tm.last_lap_ms = 0U;
    tm.lap_count = 0U;
    tm.refresh_elapsed_ms = 0U;
    refresh_display();

    if (tm.host->imu_enable(GM_PLUGIN_IMU_ENABLE_GESTURES) != GM_PLUGIN_OK)
        goto no_memory;
    return GM_PLUGIN_OK;

no_memory:
    tm.ui->obj_clean(root);
    tm.screen = 0;
    tm.total_label = 0;
    tm.state_label = 0;
    tm.lap_label = 0;
    tm.hint_label = 0;
    return GM_PLUGIN_ENOMEM;
}

static void timer_loop(void *context, uint32_t elapsed_ms)
{
    (void)context;
    /* Clamp long stalls so the clock does not jump after a resume. */
    if (elapsed_ms > 1000U) elapsed_ms = 1000U;

    if (tm.running) {
        tm.total_ms += elapsed_ms;
        tm.refresh_elapsed_ms += elapsed_ms;
        if (tm.refresh_elapsed_ms >= 100U) {
            tm.refresh_elapsed_ms = 0U;
            refresh_display();
        }
    }
}

static bool timer_event(void *context, const gm_plugin_event_t *event)
{
    (void)context;
    if (event == 0) return false;

    if (event->type == GM_PLUGIN_EVENT_BUTTON) {
        if (event->data.button.button != GM_PLUGIN_BUTTON_PRIMARY) return false;
        if (event->data.button.action == GM_PLUGIN_BUTTON_ACTION_SINGLE) {
            if (tm.running) add_lap();
            else set_running(true); /* first press also starts from a stop */
            return true;
        }
        if (event->data.button.action == GM_PLUGIN_BUTTON_ACTION_LONG) {
            reset_timer();
            return true;
        }
        return false;
    }

    if (event->type == GM_PLUGIN_EVENT_IMU_GESTURE &&
        event->data.imu_gesture.active) {
        if (event->data.imu_gesture.gesture == GM_PLUGIN_IMU_GESTURE_HEAD_RAISE) {
            set_running(true);
            return true;
        }
        if (event->data.imu_gesture.gesture == GM_PLUGIN_IMU_GESTURE_HEAD_LOWER) {
            set_running(false);
            return true;
        }
    }
    return false;
}

static void timer_stop(void *context)
{
    gm_plugin_lvgl_obj_t *root;
    (void)context;
    root = tm.ui->root_get();
    (void)tm.host->imu_enable(GM_PLUGIN_IMU_ENABLE_NONE);
    if (root != 0) tm.ui->obj_clean(root);
    tm.screen = 0;
    tm.total_label = 0;
    tm.state_label = 0;
    tm.lap_label = 0;
    tm.hint_label = 0;
    tm.running = false;
}

gm_plugin_result_t gm_plugin_entry(const gm_plugin_host_api_t *host,
                                    gm_plugin_descriptor_t *plugin)
{
    const gm_plugin_capabilities_t required =
        GM_PLUGIN_CAP_BUTTON | GM_PLUGIN_CAP_IMU_EVENTS;

    if (host == 0 || plugin == 0 || host->log == 0 ||
        host->display_get_info == 0 || host->graphics.lvgl == 0 ||
        host->imu_enable == 0 ||
        !GM_PLUGIN_VERSION_COMPATIBLE(host->abi_version,
                                      GM_PLUGIN_ABI_MIN_VERSION) ||
        host->struct_size < GM_PLUGIN_HOST_API_MIN_SIZE ||
        plugin->struct_size < GM_PLUGIN_DESCRIPTOR_MIN_SIZE ||
        (host->capabilities & required) != required)
        return GM_PLUGIN_ENOTSUP;

    tm.host = host;
    tm.ui = host->graphics.lvgl;
    if (gm_plugin_libc_get(host, &tm.libc) != GM_PLUGIN_OK)
        return GM_PLUGIN_ENOTSUP;
    if (tm.ui->struct_size < GM_PLUGIN_LVGL_API_MIN_SIZE ||
        !GM_PLUGIN_VERSION_COMPATIBLE(tm.ui->api_version,
                                      GM_PLUGIN_LVGL_API_MIN_VERSION))
        return GM_PLUGIN_EVERSION;

    plugin->abi_version = GM_PLUGIN_ABI_MIN_VERSION;
    plugin->context = &tm;
    plugin->on_start = timer_start;
    plugin->on_loop = timer_loop;
    plugin->on_event = timer_event;
    plugin->on_stop = timer_stop;
    return GM_PLUGIN_OK;
}
