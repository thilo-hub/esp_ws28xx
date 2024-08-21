#include "esp_ws28xx.h"

static uint16_t *dma_buffer;
CRGB *ws28xx_pixels;
static int n_of_leds, reset_delay, dma_buf_size;
static led_strip_model_t led_model;

static SemaphoreHandle_t xSemaphore;
static spi_settings_t spi_settings = {
    .host = SPI2_HOST,
    .dma_chan = SPI_DMA_CH_AUTO,
    .buscfg =
        {
            .miso_io_num = -1,
            .sclk_io_num = -1,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
        },
    .devcfg =
        {
            .clock_speed_hz = 3.2 * 1000 * 1000, // Clock out at 3.2 MHz
            .mode = 0,                           // SPI mode 0
            .spics_io_num = -1,                  // CS pin
            .queue_size = 1,
            .command_bits = 0,
            .address_bits = 0,
            .flags = SPI_DEVICE_TXBIT_LSBFIRST,
        },
};

static const uint16_t timing_bits[16] = {
    // 0 -> 0001   
    // 1 -> 0111
    0x1111, 0x7111, 0x1711, 0x7711, 0x1171, 0x7171, 0x1771, 0x7771,
    0x1117, 0x7117, 0x1717, 0x7717, 0x1177, 0x7177, 0x1777, 0x7777};

esp_err_t ws28xx_init(int pin, led_strip_model_t model, int num_of_leds,
                      CRGB **led_buffer_ptr) {
    esp_err_t err = ESP_OK;
    n_of_leds = num_of_leds;
    led_model = model;
    // Insrease if something breaks. values are less than recommended in
    // datasheets but seem stable
    reset_delay = (model == WS2812B) ? 3 : 30;

    // 12 bytes for each led + bytes for initial zero and reset state
    dma_buf_size = n_of_leds * 12 + (reset_delay + 1) * 2;

    ws28xx_pixels = malloc(sizeof(CRGB) * n_of_leds);
    if (ws28xx_pixels == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *led_buffer_ptr = ws28xx_pixels;
    spi_settings.buscfg.mosi_io_num = pin;
    spi_settings.buscfg.max_transfer_sz = dma_buf_size;
    err = spi_bus_initialize(spi_settings.host, &spi_settings.buscfg,
                             spi_settings.dma_chan);

    if (err == ESP_OK) 
	err = spi_bus_add_device(spi_settings.host, &spi_settings.devcfg,
				 &spi_settings.spi);
    
    if (err == ESP_OK && (xSemaphore = xSemaphoreCreateMutex()) == NULL )
	err = ESP_ERR_NO_MEM; // error number ??

    // Critical to be DMA memory.
    if (err == ESP_OK && 
	(dma_buffer = heap_caps_malloc(dma_buf_size, MALLOC_CAP_DMA)) == NULL )
	err = ESP_ERR_NO_MEM;

    if (err != ESP_OK) {
        free(ws28xx_pixels);
    }
    return err;
}

void ws28xx_fill_all(CRGB color) {
    for (int i = 0; i < n_of_leds; i++) {
        ws28xx_pixels[i] = color;
    }
}

esp_err_t ws28xx_update() {
    esp_err_t err;
    uint16_t *pixptr = dma_buffer;
    xSemaphoreTake(xSemaphore, portMAX_DELAY);

    *pixptr++ = 0;
    for (int i = 0; i < n_of_leds; i++) {
        // Data you want to write to each LEDs
        uint32_t temp = ws28xx_pixels[i].num;
        if (led_model == WS2815) {
            // Red
            *pixptr++ = timing_bits[0x0f & (temp >> 4)];
            *pixptr++ = timing_bits[0x0f & (temp)];

            // Green
            *pixptr++ = timing_bits[0x0f & (temp >> 12)];
            *pixptr++ = timing_bits[0x0f & (temp) >> 8];
        } else {
            // Green
            *pixptr++ = timing_bits[0x0f & (temp >> 12)];
            *pixptr++ = timing_bits[0x0f & (temp) >> 8];

            // Red
            *pixptr++ = timing_bits[0x0f & (temp >> 4)];
            *pixptr++ = timing_bits[0x0f & (temp)];
        }
        // Blue
        *pixptr++ = timing_bits[0x0f & (temp >> 20)];
        *pixptr++ = timing_bits[0x0f & (temp) >> 16];
    }
    for (int i = 0; i < reset_delay; i++) {
        *pixptr++ = 0;
    }

    err = spi_device_transmit(spi_settings.spi, &(spi_transaction_t){
                                                    .length = dma_buf_size * 8,
                                                    .tx_buffer = dma_buffer,
                                                });
    xSemaphoreGive(xSemaphore);
    return err;
}
