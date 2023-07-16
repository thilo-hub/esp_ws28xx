#include "esp_ws28xx.h"

uint16_t* dma_buffer;
CRGB* ws28xx_pixels;
static int led_num, reset_delay, dma_buf_size;
led_strip_model_t led_model;

static spi_settings_t spi_settings = {
		.host = SPI2_HOST,
		.dma_chan = SPI_DMA_CH_AUTO,
		.buscfg = {
				.miso_io_num = -1,
				.sclk_io_num = -1,
				.quadwp_io_num = -1,
				.quadhd_io_num = -1,
		},
		.devcfg = { .clock_speed_hz = 3.2 * 1000 * 1000, //Clock out at 3.2 MHz
				.mode = 0, //SPI mode 0
				.spics_io_num = -1, //CS pin
				.queue_size = 1,
				.command_bits = 0,
				.address_bits = 0,
				.flags = SPI_DEVICE_TXBIT_LSBFIRST,
		}
};


void ws28xx_init(int pin, led_strip_model_t model, int led_number) {
	esp_err_t err;
	led_num = led_number;
	led_model = model;
	dma_buf_size = led_num * 12 + (reset_delay + 1) * 2; //12 bytes for each led + bytes for initial zero and reset state
	ws28xx_pixels = malloc(sizeof(CRGB) * led_num);
	spi_settings.buscfg.mosi_io_num = pin;
	spi_settings.buscfg.max_transfer_sz = dma_buf_size;
	err = spi_bus_initialize(spi_settings.host, &spi_settings.buscfg, spi_settings.dma_chan);
	ESP_ERROR_CHECK(err);
	err = spi_bus_add_device(spi_settings.host, &spi_settings.devcfg, &spi_settings.spi);
	ESP_ERROR_CHECK(err);
	reset_delay = (model == WS2812B) ? 3 : 30; //insrease if something breaks. values are less that recommended in datasheets but seem stable
	dma_buffer = heap_caps_malloc(dma_buf_size, MALLOC_CAP_DMA); // Critical to be DMA memory.
}


void ws28xx_fill_color(CRGB color) {
	for(int i=0; i < led_num; i++) {
		ws28xx_pixels[i] = color;
	}
}


void ws28xx_update() {
	uint16_t timing_bits[16] = {
		0x1111,
		0x7111,
		0x1711,
		0x7711,
		0x1171,
		0x7171,
		0x1771,
		0x7771,
		0x1117,
		0x7117,
		0x1717,
		0x7717,
		0x1177,
		0x7177,
		0x1777,
		0x7777
	};
	esp_err_t err;
	int n = 0;
	memset(dma_buffer, 0, dma_buf_size);
	
	dma_buffer[n++] = 0;
	for (int i = 0; i < led_num; i++) {
		uint32_t temp = ws28xx_pixels[i].num;// Data you want to write to each LEDs
		if (led_model == WS2815) {
			//Red
			dma_buffer[n++] = timing_bits[0x0f & (temp >>4)];
			dma_buffer[n++] = timing_bits[0x0f & (temp)];

			//Green
			dma_buffer[n++] = timing_bits[0x0f & (temp >>12)];
			dma_buffer[n++] = timing_bits[0x0f & (temp)>>8];
		} else {
			//Green
			dma_buffer[n++] = timing_bits[0x0f & (temp >>12)];
			dma_buffer[n++] = timing_bits[0x0f & (temp)>>8];

			//Red
			dma_buffer[n++] = timing_bits[0x0f & (temp >>4)];
			dma_buffer[n++] = timing_bits[0x0f & (temp)];
		}
		//Blue
		dma_buffer[n++] = timing_bits[0x0f & (temp >>20)];
		dma_buffer[n++] = timing_bits[0x0f & (temp)>>16];
	}
	for (int i = 0; i < reset_delay; i++) dma_buffer[n++] = 0;

	err = spi_device_transmit(spi_settings.spi, &(spi_transaction_t){
		.length = dma_buf_size * 8,
		.tx_buffer = dma_buffer,
	});
	ESP_ERROR_CHECK(err);
}
