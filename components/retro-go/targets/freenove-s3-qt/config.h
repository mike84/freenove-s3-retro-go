// Freenove ESP32-S3 + 2.8" ILI9341 + Adafruit QT Gamepad target.
#define RG_TARGET_NAME             "FREENOVE-S3-QT"

// Storage: built-in SD_MMC socket. Retro-Go's SDMMC path currently uses 1-bit mode,
// which is fine for a first Doom boot and avoids touching the shared storage driver.
#define RG_STORAGE_ROOT            "/sd"
#define RG_STORAGE_SDMMC_HOST      SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED     SDMMC_FREQ_DEFAULT

// Audio: ES8311 codec on the shared I2C bus, fed by I2S + MCLK.
#define RG_AUDIO_USE_INT_DAC       0
#define RG_AUDIO_USE_EXT_DAC       1
#define RG_AUDIO_ES8311            1

// Video: Freenove FNK0104B 2.8" 240x320 ILI9341 as configured by the launcher.
#define RG_SCREEN_DRIVER           0
#define RG_SCREEN_HOST             SPI2_HOST
#define RG_SCREEN_SPEED            SPI_MASTER_FREQ_40M
#define RG_SCREEN_BACKLIGHT        1
#define RG_SCREEN_WIDTH            320
#define RG_SCREEN_HEIGHT           240
#define RG_SCREEN_ROTATE           0
#define RG_SCREEN_VISIBLE_AREA     {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA        {0, 0, 0, 0}
#define RG_SCREEN_INIT()                                                                                         \
    ILI9341_CMD(0xCF, 0x00, 0xc3, 0x30);                                                                         \
    ILI9341_CMD(0xED, 0x64, 0x03, 0x12, 0x81);                                                                   \
    ILI9341_CMD(0xE8, 0x85, 0x00, 0x78);                                                                         \
    ILI9341_CMD(0xCB, 0x39, 0x2c, 0x00, 0x34, 0x02);                                                             \
    ILI9341_CMD(0xF7, 0x20);                                                                                     \
    ILI9341_CMD(0xEA, 0x00, 0x00);                                                                               \
    ILI9341_CMD(0xC0, 0x1B);                                                                                     \
    ILI9341_CMD(0xC1, 0x12);                                                                                     \
    ILI9341_CMD(0xC5, 0x32, 0x3C);                                                                               \
    ILI9341_CMD(0xC7, 0x91);                                                                                     \
    ILI9341_CMD(0x36, 0xA8);                 /* Memory Access Control (MY|MV|BGR), flipped landscape */          \
    ILI9341_CMD(0x21);                       /* Match TFT_eSPI's TFT_INVERSION_ON setting */                    \
    ILI9341_CMD(0xB1, 0x00, 0x10);                                                                         \
    ILI9341_CMD(0xB6, 0x0A, 0xA2);                                                                         \
    ILI9341_CMD(0xF6, 0x01, 0x30);                                                                         \
    ILI9341_CMD(0xF2, 0x00);                                                                               \
    ILI9341_CMD(0x26, 0x01);                                                                               \
    ILI9341_CMD(0xE0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00); \
    ILI9341_CMD(0xE1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);

// Input: Adafruit Mini I2C Gamepad QT, seesaw at 0x50.
// Mapping follows the launcher:
//   stick = move/turn, A = fire/enter, B = run/back, X = map, Y = weapon toggle,
//   Start = use, Select = menu/escape.
#define RG_GAMEPAD_SEESAW_QT       1
#define RG_GAMEPAD_DEBOUNCE_PRESS  1
#define RG_GAMEPAD_DEBOUNCE_RELEASE 1

// Battery
#define RG_BATTERY_DRIVER          0

// Status LED
#define RG_GPIO_LED                GPIO_NUM_NC

// I2C bus for QT Gamepad and, later, ES8311 codec setup.
#define RG_GPIO_I2C_SDA            GPIO_NUM_16
#define RG_GPIO_I2C_SCL            GPIO_NUM_15

// SPI Display
#define RG_GPIO_LCD_MISO           GPIO_NUM_13
#define RG_GPIO_LCD_MOSI           GPIO_NUM_11
#define RG_GPIO_LCD_CLK            GPIO_NUM_12
#define RG_GPIO_LCD_CS             GPIO_NUM_10
#define RG_GPIO_LCD_DC             GPIO_NUM_46
#define RG_GPIO_LCD_BCKL           GPIO_NUM_45
// #define RG_GPIO_LCD_RST          GPIO_NUM_NC

// SD_MMC pins. The storage driver currently uses CLK/CMD/D0 for 1-bit mode.
#define RG_GPIO_SDSPI_CLK          GPIO_NUM_38
#define RG_GPIO_SDSPI_CMD          GPIO_NUM_40
#define RG_GPIO_SDSPI_D0           GPIO_NUM_39

// ES8311/I2S pins for later audio bring-up.
#define RG_GPIO_SND_I2S_MCLK       GPIO_NUM_4
#define RG_GPIO_SND_I2S_BCK        GPIO_NUM_5
#define RG_GPIO_SND_I2S_WS         GPIO_NUM_7
#define RG_GPIO_SND_I2S_DATA       GPIO_NUM_8
#define RG_GPIO_SND_AMP_ENABLE     GPIO_NUM_1
#define RG_GPIO_SND_AMP_ENABLE_INVERT 1

#define RG_UPDATER_ENABLE          0
