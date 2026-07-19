#include "rg_system.h"
#include "rg_input.h"
#include "rg_i2c.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include <driver/gpio.h>
#include <driver/adc.h>
// This is a lazy way to silence deprecation notices on some esp-idf versions...
// This hardcoded value is the first thing to check if something stops working!
#define ADC_ATTEN_DB_11 3
#else
#include <SDL2/SDL.h>
#endif

#if RG_BATTERY_DRIVER == 1
#include <esp_adc_cal.h>
static esp_adc_cal_characteristics_t adc_chars;
#endif

#if defined(RG_GAMEPAD_SEESAW_QT)
#define SEESAW_QT_ADDR                 0x50
#define SEESAW_STATUS_BASE             0x00
#define SEESAW_STATUS_HW_ID            0x01
#define SEESAW_GPIO_BASE               0x01
#define SEESAW_GPIO_DIRCLR_BULK        0x03
#define SEESAW_GPIO_BULK               0x04
#define SEESAW_GPIO_BULK_SET           0x05
#define SEESAW_GPIO_PULLENSET          0x0B
#define SEESAW_ADC_BASE                0x09
#define SEESAW_ADC_CHANNEL_OFFSET      0x07
#define SEESAW_QT_BUTTON_SELECT        0
#define SEESAW_QT_BUTTON_B             1
#define SEESAW_QT_BUTTON_Y             2
#define SEESAW_QT_BUTTON_A             5
#define SEESAW_QT_BUTTON_X             6
#define SEESAW_QT_BUTTON_START         16
#define SEESAW_QT_AXIS_X               14
#define SEESAW_QT_AXIS_Y               15
#define SEESAW_QT_AXIS_LOW             360
#define SEESAW_QT_AXIS_HIGH            664
#define SEESAW_QT_BUTTON_MASK          ((1UL << SEESAW_QT_BUTTON_SELECT) | (1UL << SEESAW_QT_BUTTON_B) | (1UL << SEESAW_QT_BUTTON_Y) | (1UL << SEESAW_QT_BUTTON_A) | (1UL << SEESAW_QT_BUTTON_X) | (1UL << SEESAW_QT_BUTTON_START))
#define SEESAW_QT_KEY_MASK             (RG_KEY_UP | RG_KEY_RIGHT | RG_KEY_DOWN | RG_KEY_LEFT | RG_KEY_SELECT | RG_KEY_START | RG_KEY_MENU | RG_KEY_OPTION | RG_KEY_A | RG_KEY_B)

static bool seesaw_qt_write(uint8_t reg_high, uint8_t reg_low, const uint8_t *payload, size_t payload_len)
{
    uint8_t buffer[10] = {reg_high, reg_low};
    if (payload_len > sizeof(buffer) - 2)
        return false;
    if (payload_len)
        memcpy(&buffer[2], payload, payload_len);
    return rg_i2c_write(SEESAW_QT_ADDR, -1, buffer, payload_len + 2);
}

static bool seesaw_qt_read(uint8_t reg_high, uint8_t reg_low, uint8_t *buffer, size_t length, uint32_t delay_us)
{
    if (!seesaw_qt_write(reg_high, reg_low, NULL, 0))
        return false;
    if (delay_us)
        rg_usleep(delay_us);
    return rg_i2c_read(SEESAW_QT_ADDR, -1, buffer, length);
}

static uint16_t seesaw_qt_analog_read(uint8_t pin)
{
    uint8_t buffer[2] = {0};
    if (!seesaw_qt_read(SEESAW_ADC_BASE, SEESAW_ADC_CHANNEL_OFFSET + pin, buffer, sizeof(buffer), 500))
        return 512;
    return ((uint16_t)buffer[0] << 8) | buffer[1];
}

static uint32_t seesaw_qt_digital_read_bulk(void)
{
    uint8_t buffer[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    if (!seesaw_qt_read(SEESAW_GPIO_BASE, SEESAW_GPIO_BULK, buffer, sizeof(buffer), 125))
        return 0xFFFFFFFF;
    return ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) | ((uint32_t)buffer[2] << 8) | buffer[3];
}

static void seesaw_qt_write_pin_mask(uint8_t reg_low, uint32_t pin_mask)
{
    const uint8_t payload[4] = {
        (uint8_t)(pin_mask >> 24),
        (uint8_t)(pin_mask >> 16),
        (uint8_t)(pin_mask >> 8),
        (uint8_t)pin_mask,
    };
    seesaw_qt_write(SEESAW_GPIO_BASE, reg_low, payload, sizeof(payload));
}
#endif

#ifdef RG_GAMEPAD_ADC_MAP
static rg_keymap_adc_t keymap_adc[] = RG_GAMEPAD_ADC_MAP;
#endif
#ifdef RG_GAMEPAD_GPIO_MAP
static rg_keymap_gpio_t keymap_gpio[] = RG_GAMEPAD_GPIO_MAP;
#endif
#ifdef RG_GAMEPAD_I2C_MAP
static rg_keymap_i2c_t keymap_i2c[] = RG_GAMEPAD_I2C_MAP;
#endif
#ifdef RG_GAMEPAD_KBD_MAP
static rg_keymap_kbd_t keymap_kbd[] = RG_GAMEPAD_KBD_MAP;
#endif
#ifdef RG_GAMEPAD_SERIAL_MAP
static rg_keymap_serial_t keymap_serial[] = RG_GAMEPAD_SERIAL_MAP;
#endif
#ifdef RG_GAMEPAD_VIRT_MAP
static rg_keymap_virt_t keymap_virt[] = RG_GAMEPAD_VIRT_MAP;
#endif
static bool input_task_running = false;
static uint32_t gamepad_state = -1; // _Atomic
static uint32_t gamepad_mapped = 0;
static rg_battery_t battery_state = {0};

#define UPDATE_GLOBAL_MAP(keymap)                 \
    for (size_t i = 0; i < RG_COUNT(keymap); ++i) \
        gamepad_mapped |= keymap[i].key;          \

#ifdef ESP_PLATFORM
static inline int adc_get_raw(adc_unit_t unit, adc_channel_t channel)
{
    if (unit == ADC_UNIT_1)
    {
        return adc1_get_raw(channel);
    }
    else if (unit == ADC_UNIT_2)
    {
        int adc_raw_value = -1;
        if (adc2_get_raw(channel, ADC_WIDTH_MAX - 1, &adc_raw_value) != ESP_OK)
            RG_LOGE("ADC2 reading failed, this can happen while wifi is active.");
        return adc_raw_value;
    }
    RG_LOGE("Invalid ADC unit %d", (int)unit);
    return -1;
}
#endif

bool rg_input_read_battery_raw(rg_battery_t *out)
{
    uint32_t raw_value = 0;
    bool present = true;
    bool charging = false;

#if RG_BATTERY_DRIVER == 1 /* ADC */
    for (int i = 0; i < 4; ++i)
    {
        int value = adc_get_raw(RG_BATTERY_ADC_UNIT, RG_BATTERY_ADC_CHANNEL);
        if (value < 0)
            return false;
        raw_value += esp_adc_cal_raw_to_voltage(value, &adc_chars);
    }
    raw_value /= 4;
#elif RG_BATTERY_DRIVER == 2 /* I2C */
    uint8_t data[5];
    if (!rg_i2c_read(0x20, -1, &data, 5))
        return false;
    raw_value = data[4];
    charging = data[4] == 255;
#else
    return false;
#endif

    if (!out)
        return true;

    *out = (rg_battery_t){
        .level = RG_MAX(0.f, RG_MIN(100.f, RG_BATTERY_CALC_PERCENT(raw_value))),
        .volts = RG_BATTERY_CALC_VOLTAGE(raw_value),
        .present = present,
        .charging = charging,
    };
    return true;
}

bool rg_input_read_gamepad_raw(uint32_t *out)
{
    uint32_t state = 0;

#if defined(RG_GAMEPAD_ADC_MAP)
    static int old_adc_values[RG_COUNT(keymap_adc)];
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        int value = adc_get_raw(mapping->unit, mapping->channel);
        if (value >= mapping->min && value <= mapping->max)
        {
            if (abs(old_adc_values[i] - value) < RG_GAMEPAD_ADC_FILTER_WINDOW)
                state |= mapping->key;
            // else
            //     RG_LOGD("Rejected input: %d", old_adc_values[i] - value);
            old_adc_values[i] = value;
        }
    }
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        if (gpio_get_level(mapping->num) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    uint32_t buttons = 0;
#if defined(RG_I2C_GPIO_DRIVER)
    int data0 = rg_i2c_gpio_read_port(0), data1 = rg_i2c_gpio_read_port(1);
    if (data0 > -1) // && data1 > -1)
    {
        buttons = (data1 << 8) | (data0);
#elif defined(RG_TARGET_T_DECK_PLUS)
    uint8_t data[5];
    if (rg_i2c_read(T_DECK_KBD_ADDRESS, -1, &data, 5))
    {
        buttons = ((data[0] << 25) | (data[1] << 18) | (data[2] << 11) | ((data[3] & 0xF8) << 4) | (data[4]));
#else
    uint8_t data[5];
    if (rg_i2c_read(RG_I2C_GPIO_ADDR, -1, &data, 5))
    {
        buttons = (data[2] << 8) | (data[1]);
#endif
        for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
        {
            const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
            if (((buttons >> mapping->num) & 1) == mapping->level)
                state |= mapping->key;
        }
    }
#endif

#if defined(RG_GAMEPAD_SEESAW_QT)
    uint16_t qt_x = 1023 - seesaw_qt_analog_read(SEESAW_QT_AXIS_X); // Match launcher X inversion
    uint16_t qt_y = seesaw_qt_analog_read(SEESAW_QT_AXIS_Y);
    uint32_t qt_buttons = seesaw_qt_digital_read_bulk();

    if (qt_y < SEESAW_QT_AXIS_LOW)
        state |= RG_KEY_UP;
    else if (qt_y > SEESAW_QT_AXIS_HIGH)
        state |= RG_KEY_DOWN;
    if (qt_x < SEESAW_QT_AXIS_LOW)
        state |= RG_KEY_LEFT;
    else if (qt_x > SEESAW_QT_AXIS_HIGH)
        state |= RG_KEY_RIGHT;

    if (((qt_buttons >> SEESAW_QT_BUTTON_A) & 1) == 0)
        state |= RG_KEY_A;
    if (((qt_buttons >> SEESAW_QT_BUTTON_B) & 1) == 0)
        state |= RG_KEY_B;
    if (((qt_buttons >> SEESAW_QT_BUTTON_X) & 1) == 0)
        state |= RG_KEY_OPTION;
    if (((qt_buttons >> SEESAW_QT_BUTTON_Y) & 1) == 0)
        state |= RG_KEY_SELECT;
    if (((qt_buttons >> SEESAW_QT_BUTTON_START) & 1) == 0)
        state |= RG_KEY_START;
    if (((qt_buttons >> SEESAW_QT_BUTTON_SELECT) & 1) == 0)
        state |= RG_KEY_MENU;
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
#ifdef RG_TARGET_SDL2
    int numkeys = 0;
    const uint8_t *keys = SDL_GetKeyboardState(&numkeys);
    for (size_t i = 0; i < RG_COUNT(keymap_kbd); ++i)
    {
        const rg_keymap_kbd_t *mapping = &keymap_kbd[i];
        if (mapping->src < 0 || mapping->src >= numkeys)
            continue;
        if (keys[mapping->src])
            state |= mapping->key;
    }
#else
#warning "not implemented"
#endif
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    rg_usleep(5);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 1);
    rg_usleep(1);
    uint32_t buttons = 0;
    for (int i = 0; i < 16; i++)
    {
        buttons |= gpio_get_level(RG_GPIO_GAMEPAD_DATA) << (15 - i);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 0);
        rg_usleep(1);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
        rg_usleep(1);
    }
    for (size_t i = 0; i < RG_COUNT(keymap_serial); ++i)
    {
        const rg_keymap_serial_t *mapping = &keymap_serial[i];
        if (((buttons >> mapping->num) & 1) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_VIRT_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_virt); ++i)
    {
        if (state == keymap_virt[i].src)
            state = keymap_virt[i].key;
    }
#endif

    if (out)
        *out = state;
    return true;
}

static void input_task(void *arg)
{
    uint8_t debounce[RG_KEY_COUNT];
    uint32_t local_gamepad_state = 0;
    uint32_t state;
    int64_t next_battery_update = 0;

    // Start the task with debounce history full to allow a button held during boot to be detected
    memset(debounce, 0xFF, sizeof(debounce));
    input_task_running = true;

    while (input_task_running)
    {
        if (rg_input_read_gamepad_raw(&state))
        {
            for (int i = 0; i < RG_KEY_COUNT; ++i)
            {
                uint32_t val = ((debounce[i] << 1) | ((state >> i) & 1));
                debounce[i] = val & 0xFF;

                if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1)) == ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1))
                {
                    local_gamepad_state |= (1 << i); // Pressed
                }
                else if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_RELEASE) - 1)) == 0)
                {
                    local_gamepad_state &= ~(1 << i); // Released
                }
            }
            gamepad_state = local_gamepad_state;
        }

        if (rg_system_timer() >= next_battery_update)
        {
            rg_battery_t temp = {0};
            if (rg_input_read_battery_raw(&temp))
            {
                if (fabsf(battery_state.level - temp.level) < RG_BATTERY_UPDATE_THRESHOLD)
                    temp.level = battery_state.level;
                if (fabsf(battery_state.volts - temp.volts) < RG_BATTERY_UPDATE_THRESHOLD_VOLT)
                    temp.volts = battery_state.volts;
            }
            battery_state = temp;
            next_battery_update = rg_system_timer() + 2 * 1000000; // update every 2 seconds
        }

        rg_task_delay(10);
    }

    input_task_running = false;
    gamepad_state = -1;
}

void rg_input_init(void)
{
    RG_ASSERT(!input_task_running, "Input already initialized!");

#if defined(RG_GAMEPAD_ADC_MAP)
    RG_LOGI("Initializing ADC gamepad driver...");
    adc1_config_width(ADC_WIDTH_MAX - 1);
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        if (mapping->unit == ADC_UNIT_1)
            adc1_config_channel_atten(mapping->channel, mapping->atten);
        else if (mapping->unit == ADC_UNIT_2)
            adc2_config_channel_atten(mapping->channel, mapping->atten);
        else
            RG_LOGE("Invalid ADC unit %d!", (int)mapping->unit);
    }
    UPDATE_GLOBAL_MAP(keymap_adc);
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    RG_LOGI("Initializing GPIO gamepad driver...");
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        gpio_set_direction(mapping->num, GPIO_MODE_INPUT);
        if (mapping->pullup && mapping->pulldown)
            gpio_set_pull_mode(mapping->num, GPIO_PULLUP_PULLDOWN);
        else if (mapping->pullup || mapping->pulldown)
            gpio_set_pull_mode(mapping->num, mapping->pullup ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
        else
            gpio_set_pull_mode(mapping->num, GPIO_FLOATING);
    }
    UPDATE_GLOBAL_MAP(keymap_gpio);
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    RG_LOGI("Initializing I2C gamepad driver...");
    rg_i2c_init();
#if defined(RG_I2C_GPIO_DRIVER)
    for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
    {
        const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
        if (mapping->pullup)
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT_PULLUP);
        else
            rg_i2c_gpio_set_direction(mapping->num, RG_GPIO_INPUT);
    }
#elif defined(RG_TARGET_T_DECK_PLUS)
    rg_i2c_write_byte(T_DECK_KBD_ADDRESS, -1, T_DECK_KBD_MODE_RAW_CMD);
#endif
    UPDATE_GLOBAL_MAP(keymap_i2c);
#endif

#if defined(RG_GAMEPAD_SEESAW_QT)
    RG_LOGI("Initializing seesaw QT gamepad driver...");
    rg_i2c_init();
    seesaw_qt_write_pin_mask(SEESAW_GPIO_DIRCLR_BULK, SEESAW_QT_BUTTON_MASK);
    seesaw_qt_write_pin_mask(SEESAW_GPIO_PULLENSET, SEESAW_QT_BUTTON_MASK);
    seesaw_qt_write_pin_mask(SEESAW_GPIO_BULK_SET, SEESAW_QT_BUTTON_MASK);
    uint8_t hw_id = 0;
    if (seesaw_qt_read(SEESAW_STATUS_BASE, SEESAW_STATUS_HW_ID, &hw_id, 1, 125))
        RG_LOGI("seesaw QT gamepad ready, hw=0x%02X", hw_id);
    else
        RG_LOGW("seesaw QT gamepad not detected yet");
    gamepad_mapped |= SEESAW_QT_KEY_MASK;
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
    RG_LOGI("Initializing KBD gamepad driver...");
    UPDATE_GLOBAL_MAP(keymap_kbd);
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    RG_LOGI("Initializing SERIAL gamepad driver...");
    gpio_set_direction(RG_GPIO_GAMEPAD_CLOCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_DATA, GPIO_MODE_INPUT);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
    UPDATE_GLOBAL_MAP(keymap_serial);
#endif


#if RG_BATTERY_DRIVER == 1 /* ADC */
    RG_LOGI("Initializing ADC battery driver...");
    if (RG_BATTERY_ADC_UNIT == ADC_UNIT_1)
    {
        adc1_config_width(ADC_WIDTH_MAX - 1); // there is no adc2_config_width
        adc1_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
    }
    else if (RG_BATTERY_ADC_UNIT == ADC_UNIT_2)
    {
        adc2_config_channel_atten(RG_BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
        esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_MAX - 1, 1100, &adc_chars);
    }
    else
    {
        RG_LOGE("Only ADC1 and ADC2 are supported for ADC battery driver!");
    }
#endif

    // The first read returns bogus data in some drivers, waste it.
    rg_input_read_gamepad_raw(NULL);

    // Start background polling
    rg_task_create("rg_input", &input_task, NULL, 3 * 1024, RG_TASK_PRIORITY_6, 1);
    while (gamepad_state == -1)
        rg_task_yield();
    RG_LOGI("Input ready. state=" PRINTF_BINARY_16 "\n", PRINTF_BINVAL_16(gamepad_state));
}

void rg_input_deinit(void)
{
    input_task_running = false;
    // while (gamepad_state != -1)
    //     rg_task_yield();
    RG_LOGI("Input terminated.\n");
}

bool rg_input_key_is_present(rg_key_t mask)
{
    return (gamepad_mapped & mask) == mask;
}

uint32_t rg_input_read_gamepad(void)
{
#ifdef RG_TARGET_SDL2
    SDL_PumpEvents();
#endif
    return gamepad_state;
}

bool rg_input_key_is_pressed(rg_key_t mask)
{
    return (bool)(rg_input_read_gamepad() & mask);
}

bool rg_input_wait_for_key(rg_key_t mask, bool pressed, int timeout_ms)
{
    int64_t expiration = timeout_ms < 0 ? INT64_MAX : (rg_system_timer() + timeout_ms * 1000);
    while (rg_input_key_is_pressed(mask) != pressed)
    {
        if (rg_system_timer() > expiration)
            return false;
        rg_task_delay(10);
    }
    return true;
}

rg_battery_t rg_input_read_battery(void)
{
    return battery_state;
}

const char *rg_input_get_key_name(rg_key_t key)
{
    switch (key)
    {
    case RG_KEY_UP: return "Up";
    case RG_KEY_RIGHT: return "Right";
    case RG_KEY_DOWN: return "Down";
    case RG_KEY_LEFT: return "Left";
    case RG_KEY_SELECT: return "Select";
    case RG_KEY_START: return "Start";
    case RG_KEY_MENU: return "Menu";
    case RG_KEY_OPTION: return "Option";
    case RG_KEY_A: return "A";
    case RG_KEY_B: return "B";
    case RG_KEY_X: return "X";
    case RG_KEY_Y: return "Y";
    case RG_KEY_L: return "Left Shoulder";
    case RG_KEY_R: return "Right Shoulder";
    case RG_KEY_NONE: return "None";
    default: return "Unknown";
    }
}
