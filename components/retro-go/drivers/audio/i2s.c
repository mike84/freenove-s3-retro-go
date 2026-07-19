#include "rg_system.h"
#include "rg_audio.h"
#include "rg_i2c.h"

#if RG_AUDIO_USE_INT_DAC || RG_AUDIO_USE_EXT_DAC

#ifndef ESP_PLATFORM
#error "I2S support can only be built inside esp-idf!"
#elif !CONFIG_IDF_TARGET_ESP32 && RG_AUDIO_USE_INT_DAC
#error "Your chip has no DAC! Please set RG_AUDIO_USE_INT_DAC to 0 in your target file."
#endif

#include <driver/gpio.h>
#include <driver/i2s.h>

#ifdef RG_GPIO_SND_AMP_ENABLE_INVERT
#define MUTE_ENABLE 1
#define MUTE_DISABLE 0
#else
#define MUTE_ENABLE 0
#define MUTE_DISABLE 1
#endif

// We can safely assume that no application will submit more than 640 audio frames per call to
// driver_submit (32000/50). Using a single large buffer risks blocking the call needlessly because
// some apps submit more than once per cycle or there could be occasional jitter (early submission).
#define DMA_BUFFER_COUNT 4
#define DMA_BUFFER_LEN 180

#ifdef RG_AUDIO_ES8311
#define ES8311_ADDR                 0x18
#define ES8311_RESET_REG00          0x00
#define ES8311_CLK_MANAGER_REG01    0x01
#define ES8311_CLK_MANAGER_REG02    0x02
#define ES8311_CLK_MANAGER_REG03    0x03
#define ES8311_CLK_MANAGER_REG04    0x04
#define ES8311_CLK_MANAGER_REG05    0x05
#define ES8311_CLK_MANAGER_REG06    0x06
#define ES8311_CLK_MANAGER_REG07    0x07
#define ES8311_CLK_MANAGER_REG08    0x08
#define ES8311_SDPIN_REG09          0x09
#define ES8311_SDPOUT_REG0A         0x0A
#define ES8311_SYSTEM_REG0D         0x0D
#define ES8311_SYSTEM_REG0E         0x0E
#define ES8311_SYSTEM_REG12         0x12
#define ES8311_SYSTEM_REG13         0x13
#define ES8311_ADC_REG1C            0x1C
#define ES8311_DAC_REG31            0x31
#define ES8311_DAC_REG32            0x32
#define ES8311_DAC_REG37            0x37

typedef struct {
    uint32_t rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
} es8311_coeff_t;

static const es8311_coeff_t es8311_coeffs[] = {
    // These are the ES8311 256x-MCLK coefficients used by the launcher driver.
    {22050, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {32000, 0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {48000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

static bool es8311_write(uint8_t reg, uint8_t value)
{
    return rg_i2c_write_byte(ES8311_ADDR, reg, value);
}

static bool es8311_read(uint8_t reg, uint8_t *value)
{
    return rg_i2c_read(ES8311_ADDR, reg, value, 1);
}

static const es8311_coeff_t *es8311_get_coeff(int sample_rate)
{
    for (size_t i = 0; i < RG_COUNT(es8311_coeffs); ++i)
        if ((int)es8311_coeffs[i].rate == sample_rate)
            return &es8311_coeffs[i];
    return NULL;
}

static bool es8311_configure_sample_rate(int sample_rate)
{
    const es8311_coeff_t *coeff = es8311_get_coeff(sample_rate);
    uint8_t regv;

    if (!coeff)
    {
        RG_LOGE("ES8311 unsupported sample rate: %d", sample_rate);
        return false;
    }

    if (!es8311_read(ES8311_CLK_MANAGER_REG02, &regv))
        return false;
    regv &= 0x07;
    regv |= (coeff->pre_div - 1) << 5;
    regv |= coeff->pre_multi << 3;
    if (!es8311_write(ES8311_CLK_MANAGER_REG02, regv))
        return false;
    if (!es8311_write(ES8311_CLK_MANAGER_REG03, (coeff->fs_mode << 6) | coeff->adc_osr))
        return false;
    if (!es8311_write(ES8311_CLK_MANAGER_REG04, coeff->dac_osr))
        return false;
    if (!es8311_write(ES8311_CLK_MANAGER_REG05, ((coeff->adc_div - 1) << 4) | (coeff->dac_div - 1)))
        return false;
    if (!es8311_read(ES8311_CLK_MANAGER_REG06, &regv))
        return false;
    regv &= 0xE0;
    regv |= (coeff->bclk_div < 19) ? (coeff->bclk_div - 1) : coeff->bclk_div;
    if (!es8311_write(ES8311_CLK_MANAGER_REG06, regv))
        return false;
    if (!es8311_read(ES8311_CLK_MANAGER_REG07, &regv))
        return false;
    regv &= 0xC0;
    regv |= coeff->lrck_h;
    return es8311_write(ES8311_CLK_MANAGER_REG07, regv) &&
           es8311_write(ES8311_CLK_MANAGER_REG08, coeff->lrck_l);
}

static bool es8311_set_volume(int volume)
{
    volume = RG_MIN(RG_MAX(volume, 0), 100);
    int reg32 = volume == 0 ? 0 : (volume * 256 / 100) - 1;
    return es8311_write(ES8311_DAC_REG32, reg32);
}

static bool es8311_set_mute(bool mute)
{
    uint8_t reg31;
    if (!es8311_read(ES8311_DAC_REG31, &reg31))
        return false;
    if (mute)
        reg31 |= (1 << 6) | (1 << 5);
    else
        reg31 &= ~((1 << 6) | (1 << 5));
    return es8311_write(ES8311_DAC_REG31, reg31);
}

static bool es8311_init_codec(int sample_rate)
{
    rg_i2c_init();

    if (!es8311_write(ES8311_RESET_REG00, 0x1F))
        return false;
    rg_usleep(20 * 1000);
    if (!es8311_write(ES8311_RESET_REG00, 0x00) ||
        !es8311_write(ES8311_RESET_REG00, 0x80) ||
        !es8311_write(ES8311_CLK_MANAGER_REG01, 0x3F))
        return false;

    uint8_t reg06 = 0;
    if (!es8311_read(ES8311_CLK_MANAGER_REG06, &reg06) ||
        !es8311_write(ES8311_CLK_MANAGER_REG06, reg06 & ~(1 << 5)) ||
        !es8311_configure_sample_rate(sample_rate))
        return false;

    // Slave serial port, I2S format, 16-bit in/out.
    uint8_t reg00 = 0;
    if (!es8311_read(ES8311_RESET_REG00, &reg00))
        return false;
    reg00 &= 0xBF;

    if (!es8311_write(ES8311_RESET_REG00, reg00) ||
        !es8311_write(ES8311_SDPIN_REG09, 3 << 2) ||
        !es8311_write(ES8311_SDPOUT_REG0A, 3 << 2) ||
        !es8311_write(ES8311_SYSTEM_REG0D, 0x01) ||
        !es8311_write(ES8311_SYSTEM_REG0E, 0x02) ||
        !es8311_write(ES8311_SYSTEM_REG12, 0x00) ||
        !es8311_write(ES8311_SYSTEM_REG13, 0x10) ||
        !es8311_write(ES8311_ADC_REG1C, 0x6A) ||
        !es8311_write(ES8311_DAC_REG37, 0x08))
        return false;

    es8311_set_volume(85);
    es8311_set_mute(false);
    return true;
}
#endif

static struct {
    const char *last_error;
    int device;
    int volume;
    bool muted;
} state;

static bool driver_init(int device, int sample_rate)
{
    state.last_error = NULL;
    state.device = device;

    if (state.device == 0)
    {
    #if RG_AUDIO_USE_INT_DAC
        esp_err_t ret = i2s_driver_install(I2S_NUM_0, &(i2s_config_t){
            .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,
            .sample_rate = sample_rate,
            .bits_per_sample = 16,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_MSB,
            .intr_alloc_flags = 0, // ESP_INTR_FLAG_LEVEL1
            .dma_buf_count = DMA_BUFFER_COUNT,
            .dma_buf_len = DMA_BUFFER_LEN,
        }, 0, NULL);
        if (ret == ESP_OK)
            ret = i2s_set_dac_mode(RG_AUDIO_USE_INT_DAC);
        if (ret != ESP_OK)
            state.last_error = esp_err_to_name(ret);
    #else
        state.last_error = "This device does not support internal DAC mode!";
    #endif
    }
    else if (state.device == 1)
    {
    #if RG_AUDIO_USE_EXT_DAC
        esp_err_t ret = i2s_driver_install(I2S_NUM_0, &(i2s_config_t){
            .mode = I2S_MODE_MASTER | I2S_MODE_TX,
            .sample_rate = sample_rate,
            .bits_per_sample = 16,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = 0, // ESP_INTR_FLAG_LEVEL1
            .dma_buf_count = DMA_BUFFER_COUNT,
            .dma_buf_len = DMA_BUFFER_LEN,
            .use_apll = true,
            .fixed_mclk = sample_rate * 256,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        #if 0
            .use_apll = true, // External DAC may care about accuracy
        #endif
        }, 0, NULL);
        if (ret == ESP_OK)
        {
            ret = i2s_set_pin(I2S_NUM_0, &(i2s_pin_config_t) {
            #ifdef RG_GPIO_SND_I2S_MCLK
                .mck_io_num = RG_GPIO_SND_I2S_MCLK,
            #else
                .mck_io_num = GPIO_NUM_NC,
            #endif
                .bck_io_num = RG_GPIO_SND_I2S_BCK,
                .ws_io_num = RG_GPIO_SND_I2S_WS,
                .data_out_num = RG_GPIO_SND_I2S_DATA,
                .data_in_num = GPIO_NUM_NC
            });
        }
#ifdef RG_AUDIO_ES8311
        if (ret == ESP_OK && !es8311_init_codec(sample_rate))
            ret = ESP_FAIL;
#endif
        if (ret != ESP_OK)
            state.last_error = esp_err_to_name(ret);
    #else
        state.last_error = "This device does not support external DAC mode!";
    #endif
    }
    #ifdef RG_GPIO_SND_AMP_ENABLE
        gpio_reset_pin(RG_GPIO_SND_AMP_ENABLE);
        gpio_set_level(RG_GPIO_SND_AMP_ENABLE, MUTE_ENABLE);
        gpio_set_direction(RG_GPIO_SND_AMP_ENABLE, GPIO_MODE_OUTPUT);
    #endif
    return state.last_error == NULL;
}

static bool driver_set_sample_rates(int sampleRate)
{
    bool ok = i2s_set_sample_rates(I2S_NUM_0, sampleRate) == ESP_OK;
#ifdef RG_AUDIO_ES8311
    ok = ok && es8311_configure_sample_rate(sampleRate);
#endif
    return ok;
}

static bool driver_deinit(void)
{
    i2s_driver_uninstall(I2S_NUM_0);
    if (state.device == 0)
    {
    #if RG_AUDIO_USE_INT_DAC
        i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
    #endif
    }
    else if (state.device == 1)
    {
    #if RG_AUDIO_USE_EXT_DAC
    #ifdef RG_GPIO_SND_I2S_MCLK
        gpio_reset_pin(RG_GPIO_SND_I2S_MCLK);
    #endif
        gpio_reset_pin(RG_GPIO_SND_I2S_BCK);
        gpio_reset_pin(RG_GPIO_SND_I2S_DATA);
        gpio_reset_pin(RG_GPIO_SND_I2S_WS);
    #endif
    }
    #ifdef RG_GPIO_SND_AMP_ENABLE
    gpio_reset_pin(RG_GPIO_SND_AMP_ENABLE);
    #endif
    return true;
}

static bool driver_submit(const rg_audio_frame_t *frames, size_t count)
{
    float volume = state.muted ? 0.f : (state.volume * 0.01f);
    bool use_internal_dac = state.device == 0;
    rg_audio_frame_t buffer[DMA_BUFFER_LEN];
    size_t pos = 0;

    for (size_t i = 0; i < count; ++i)
    {
        int left = frames[i].left * volume;
        int right = frames[i].right * volume;

        if (use_internal_dac)
        {
        #if RG_AUDIO_USE_INT_DAC == 1
            left = ((left + right) >> 1) + 0x8000; // the internal DAC expects unsigned data
            right = 0;
        #elif RG_AUDIO_USE_INT_DAC == 2
            left = 0;
            right = ((left + right) >> 1) + 0x8000; // the internal DAC expects unsigned data
        #elif RG_AUDIO_USE_INT_DAC == 3
            // In two channel mode we use left and right as a differential mono output to increase resolution.
            int sample = (left + right) >> 1;
            if (sample > 0x7F00)
            {
                left = 0x8000 + (sample - 0x7F00);
                right = -0x8000 + 0x7F00;
            }
            else if (sample < -0x7F00)
            {
                left = 0x8000 + (sample + 0x7F00);
                right = -0x8000 + -0x7F00;
            }
            else
            {
                left = 0x8000;
                right = -0x8000 + sample;
            }
        #endif
        }

        // Clipping   (not necessary, we have (int16 * vol) and volume is never more than 1.0)
        // if (left > 32767) left = 32767; else if (left < -32768) left = -32767;
        // if (right > 32767) right = 32767; else if (right < -32768) right = -32767;

        // Queue
        buffer[pos].left = left;
        buffer[pos].right = right;

        if (i == count - 1 || ++pos == RG_COUNT(buffer))
        {
            size_t written;
            if (i2s_write(I2S_NUM_0, (void *)buffer, pos * 4, &written, 1000) != ESP_OK)
                RG_LOGW("I2S Submission error! Written: %d/%d\n", written, pos * 4);
            pos = 0;
        }
    }
    return true;
}

static bool driver_set_mute(bool mute)
{
    i2s_zero_dma_buffer(I2S_NUM_0);
    #ifdef RG_AUDIO_ES8311
    es8311_set_mute(mute);
    #endif
    #ifdef RG_GPIO_SND_AMP_ENABLE
    gpio_set_level(RG_GPIO_SND_AMP_ENABLE, mute ? MUTE_ENABLE : MUTE_DISABLE);
    #endif
    state.muted = mute;
    return true;
}

static bool driver_set_volume(int volume)
{
    state.volume = volume;
    #ifdef RG_AUDIO_ES8311
    es8311_set_volume(volume);
    #endif
    return true;
}

static const char *driver_get_error(void)
{
    return state.last_error;
}

const rg_audio_driver_t rg_audio_driver_i2s = {
    .name = "i2s",
    .init = driver_init,
    .deinit = driver_deinit,
    .submit = driver_submit,
    .set_mute = driver_set_mute,
    .set_volume = driver_set_volume,
    .set_sample_rate = driver_set_sample_rates,
    .get_error = driver_get_error,
};

#endif // RG_AUDIO_USE_INT_DAC || RG_AUDIO_USE_EXT_DAC
