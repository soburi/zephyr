# M5Stack Core2 SPI configuration comparison

## Zephyr board support (`boards/m5stack/m5stack_core2/m5stack_core2_procpu.dts`)
- The display panel is exposed via a MIPI DBI wrapper on `spi3`, using GPIO0.15 as DC and an AXP192 GPIO line as reset, with the panel bound to chip select 0 on the SPI bus.【F:boards/m5stack/m5stack_core2/m5stack_core2_procpu.dts†L57-L77】
- `spi3` runs at 20&nbsp;MHz with DMA enabled and offers two chip-select lines (GPIO0.5 and GPIO0.4). The first CS serves the LCD, while the second drives the `zephyr,sdhc-spi-slot` node for the microSD card, which is also limited to 20&nbsp;MHz.【F:boards/m5stack/m5stack_core2/m5stack_core2_procpu.dts†L207-L229】
- Pin control ties `spi3` to SCLK = GPIO18, MOSI = GPIO23 (preset low), and MISO = GPIO38 without additional drive-strength tuning.【F:boards/m5stack/m5stack_core2/m5stack_core2-pinctrl.dtsi†L38-L48】

## M5Unified library defaults (`M5Unified/src/M5Unified.cpp`)
- The `_pin_table_spi_sd` table selects SCLK = GPIO18, MOSI = GPIO23, MISO = GPIO38, and CS = GPIO4 for the Core2 SD interface, mirroring the base wiring used in Zephyr:

  ```c++
  { board_t::board_M5StackCore2 , GPIO_NUM_18, GPIO_NUM_23, GPIO_NUM_38, GPIO_NUM_4  },
  ```
  【F:doc/m5stack_core2_spi_comparison.md†L11-L13】
- During setup the library boosts the GPIO18/19/23 drive strength to 40&nbsp;mA and enables pull-ups to keep high-speed SPI links (20&nbsp;MHz for SD, 80&nbsp;MHz for external ModuleDisplay) reliable—behaviour not present in the Zephyr device tree:

  ```c++
  for (auto gpio: (const gpio_num_t[]){ GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_23 }) {
    *(volatile uint32_t*)(GPIO_PIN_MUX_REG[gpio]) = tmp | FUN_DRV_M; // gpio drive current set to 40mA.
    gpio_pulldown_dis(gpio);
    gpio_pullup_en(gpio);
  }
  ```
  【F:doc/m5stack_core2_spi_comparison.md†L18-L22】
- When M5Unified detects Atomic Speaker hardware it remaps the SD SPI pins to GPIO7/6/8 and disables the on-board IMU/RTC to avoid I²C conflicts, showing that its SPI allocation is dynamic depending on accessories:

  ```c++
  _get_pin_table[sd_spi_sclk] = GPIO_NUM_7;
  _get_pin_table[sd_spi_copi] = GPIO_NUM_6;
  _get_pin_table[sd_spi_cipo] = GPIO_NUM_8;
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  ```
  【F:doc/m5stack_core2_spi_comparison.md†L28-L32】

## Notable differences
1. **Electrical tuning** – Zephyr leaves the ESP32 pins at their default drive, while M5Unified explicitly increases drive strength and enables pull-ups to stabilise high-speed SPI transfers.
2. **Chip-select usage** – Zephyr reserves two CS lines on `spi3` (LCD and SD); M5Unified manages only the SD CS in its pin table and handles the LCD bus configuration through M5GFX.
3. **Accessory awareness** – Zephyr’s wiring is static, whereas M5Unified can reassign SPI pins (and even disable conflicting peripherals) when certain modular accessories are detected.
