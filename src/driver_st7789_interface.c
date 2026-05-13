#include "driver_st7789_interface.h"
#include "hw.h"

uint8_t st7789_interface_spi_init(void)    { return 0; }
uint8_t st7789_interface_spi_deinit(void)  { return 0; }

uint8_t st7789_interface_spi_write_cmd(uint8_t *buf, uint16_t len) {
    gpio_put(DISPLAY_CS, 0);
    spi_write_blocking(SPI_PORT, buf, len);
    gpio_put(DISPLAY_CS, 1);
    return 0;
}

void st7789_interface_delay_ms(uint32_t ms) { sleep_ms(ms); }

void st7789_interface_debug_print(const char *const fmt, ...) { (void)fmt; }

uint8_t st7789_interface_cmd_data_gpio_init(void)          { return 0; }
uint8_t st7789_interface_cmd_data_gpio_deinit(void)        { return 0; }
uint8_t st7789_interface_cmd_data_gpio_write(uint8_t value) {
    gpio_put(DISPLAY_DC, value);
    return 0;
}

uint8_t st7789_interface_reset_gpio_init(void)          { return 0; }
uint8_t st7789_interface_reset_gpio_deinit(void)        { return 0; }
uint8_t st7789_interface_reset_gpio_write(uint8_t value) { (void)value; return 0; }
