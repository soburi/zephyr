/*
 * Extracts from M5GFX (https://github.com/m5stack/M5GFX) relevant to the
 * CoreS3 SPI wiring and register sequencing. Copied verbatim for
 * documentation and comparison purposes.
 */

// --- src/M5GFX.cpp (panel/bus setup) ---
struct Panel_M5StackCoreS3 : public lgfx::Panel_ILI9342 {
  Panel_M5StackCoreS3(void) {
    _cfg.pin_cs = GPIO_NUM_3;
    _cfg.invert = true;
    _cfg.offset_rotation = 3;

    _rotation = 1; // default rotation
  }

  void rst_control(bool level) override {
    uint8_t bits = level ? (1<<5) : 0;
    uint8_t mask = level ? ~0 : ~(1<<5);
    // LCD_RST
    lgfx::i2c::writeRegister8(i2c_port, aw9523_i2c_addr, 0x03, bits, mask, i2c_freq);
  }

  void cs_control(bool flg) override {
    lgfx::Panel_ILI9342::cs_control(flg);
    // CS操作時にGPIO35の役割を切り替える (MISO or D/C);
    *(volatile uint32_t*)( flg
                           ? GPIO_ENABLE1_W1TC_REG
                           : GPIO_ENABLE1_W1TS_REG
                         ) = 1u << (GPIO_NUM_35 & 31);
  }
};

// Autodetect branch for CoreS3 (bus_cfg setup)
    bus_cfg.pin_mosi = GPIO_NUM_37;
    bus_cfg.pin_miso = GPIO_NUM_35;
    bus_cfg.pin_sclk = GPIO_NUM_36;
    bus_cfg.pin_dc   = GPIO_NUM_35;// MISOとLCD D/CをGPIO35でシェアしている;
    bus_cfg.spi_mode = 0;
    bus_cfg.spi_3wire = true;
    bus_spi->config(bus_cfg);
    bus_spi->init();
    id = _read_panel_id(bus_spi, GPIO_NUM_3);
    if ((id & 0xFF) == 0xE3) {
      bus_cfg.freq_write = 40000000;
      bus_cfg.freq_read  = 16000000;
      bus_spi->config(bus_cfg);
      auto p = new Panel_M5StackCoreS3();
      p->bus(bus_spi);
      _panel_last.reset(p);
    }

// --- src/lgfx/v1/platforms/esp32/Bus_SPI.cpp (register writes) ---
void Bus_SPI::beginTransaction(void) {
  uint32_t freq_apb = getApbFrequency();
  uint32_t clkdiv_write = _clkdiv_write;
  if (_last_freq_apb != freq_apb) {
    _last_freq_apb = freq_apb;
    _clkdiv_read = FreqToClockDiv(freq_apb, _cfg.freq_read);
    clkdiv_write = FreqToClockDiv(freq_apb, _cfg.freq_write);
    _clkdiv_write = clkdiv_write;
    dc_control(true);
    pinMode(_cfg.pin_dc, pin_mode_t::output);
  }

  auto spi_mode = _cfg.spi_mode;
  uint32_t pin = (spi_mode & 2) ? SPI_CK_IDLE_EDGE : 0;
  pin = pin
#if defined ( SPI_CS0_DIS )
          | SPI_CS0_DIS
#endif
#if defined ( SPI_CS1_DIS )
          | SPI_CS1_DIS
#endif
#if defined ( SPI_CS2_DIS )
          | SPI_CS2_DIS
#endif
#if defined ( SPI_CS3_DIS )
          | SPI_CS3_DIS
#endif
#if defined ( SPI_CS4_DIS )
          | SPI_CS4_DIS
#endif
#if defined ( SPI_CS5_DIS )
          | SPI_CS5_DIS
#endif
  ;

  if (_cfg.use_lock) spi::beginTransaction(_cfg.spi_host);

  *_spi_user_reg = _user_reg;
  auto spi_port = _spi_port;
  (void)spi_port;
  writereg(SPI_PIN_REG(spi_port), pin);
  writereg(SPI_CLOCK_REG(spi_port), clkdiv_write);
#if defined ( SPI_UPDATE )
  *_spi_cmd_reg = SPI_UPDATE;
#endif
}

void Bus_SPI::beginRead(void) {
  uint32_t pin = (_cfg.spi_mode & 2) ? SPI_CK_IDLE_EDGE : 0;
  uint32_t user = ((_cfg.spi_mode == 1 || _cfg.spi_mode == 2) ? SPI_CK_OUT_EDGE | SPI_USR_MISO : SPI_USR_MISO)
                      | (_cfg.spi_3wire ? SPI_SIO : 0);
  dc_control(true);
  *_spi_user_reg = user;
  writereg(SPI_PIN_REG(_spi_port), pin);
  writereg(SPI_CLOCK_REG(_spi_port), _clkdiv_read);
#if defined ( SPI_UPDATE )
  *_spi_cmd_reg = SPI_UPDATE;
#endif
}
