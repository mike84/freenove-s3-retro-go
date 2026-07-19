# Freenove ESP32-S3 Retro-Go

This is a Freenove ESP32-S3 handheld build of Retro-Go, tuned for:

- Freenove ESP32-S3 board with ILI9341 display
- SD_MMC card slot
- ES8311 speaker/audio codec
- Adafruit Mini I2C Gamepad QT on the Qwiic/I2C connector
- Doom through `prboom-go`
- Master System, Game Gear, Game Boy, and Game Boy Color through `retro-core`

The current default firmware image builds:

```text
launcher retro-core prboom-go
```

## SD card layout

Put your legally obtained game files on the SD card like this:

```text
/roms/doom/doom1.wad
/roms/sms/Wonder Boy III.sms
/roms/gg/Game Gear Game.gg
/roms/gb/Game Boy Game.gb
/roms/gbc/Game Boy Color Game.gbc
```

Optional BIOS files:

```text
/retro-go/bios/gb_bios.bin
/retro-go/bios/gbc_bios.bin
```

Do not commit commercial ROMs or WADs to this repo.

## Build

From this repository:

```powershell
.\scripts\build.ps1
```

The script expects ESP-IDF and ESP-IDF tools next to this repo by default:

```text
../_esp_idf
../_esp_tools
```

You can override them with:

```powershell
$env:FREENOVE_RETRO_IDF_PATH = "C:\path\to\esp-idf"
$env:FREENOVE_RETRO_IDF_TOOLS_PATH = "C:\path\to\idf-tools"
```

## Flash

With the board connected on COM3:

```powershell
.\scripts\flash.ps1
```

Or specify a different port:

```powershell
.\scripts\flash.ps1 -Port COM5
```

## Controls

Default QT Gamepad mapping:

```text
Stick       Move / menu navigation
A           Fire / confirm
B           Run / back
X           Map
Y           Weapon toggle
Start       Use
Select      Retro-Go menu / escape
```

## Notes

- Audio is intentionally configured for the Freenove ES8311 path at 44.1 kHz.
- The amp-enable pin is active-low on this hardware.
- Display is landscape-flipped so the controller orientation is comfortable.
- The original upstream Retro-Go README remains in `README.md`.
