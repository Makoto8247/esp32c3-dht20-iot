#include "hal/gpio_types.h"
#include "hal/i2c_types.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>

#define SDA_PIN GPIO_NUM_6
#define SCL_PIN GPIO_NUM_7
#define DHT20_ADDR 0x38

void get_dht20_data(double* temp_data, double* humi_data) 
{
	uint32_t processed_data = 0;
	uint8_t rx_data[8];
	const uint8_t DHT20_CMD[3] = {
		0b10101100,
		0b00110011,
		0b00000000
	};
	/*** DHT20 ***/
	// Commands to retrieve data
	i2c_master_write_to_device(I2C_NUM_0, DHT20_ADDR, DHT20_CMD, sizeof(DHT20_CMD), 100);
	ets_delay_us(100000);
    i2c_master_read_from_device(I2C_NUM_0, DHT20_ADDR, rx_data, 7, 100);
    // Humidity Calculation
    processed_data = rx_data[1] << 12;
    processed_data |= rx_data[2] << 4;
    processed_data |= (rx_data[3] & 0xF0) >> 4;
    *humi_data = (double)processed_data/1048576 * 100;
    // Temperature Calculation
    processed_data = (rx_data[3] & 0x0F) << 16;
    processed_data |= rx_data[4] << 8;
    processed_data |= rx_data[5];
    *temp_data = (double)processed_data/1048576 * 200 - 50;
    printf("Humidity = %3.2f%% : Temperature = %3.2fC\n", *humi_data, *temp_data);
}

void app_main(void)
{
	
	
	
	double humi_data = 0;
	double temp_data = 0;
	
	i2c_config_t conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = SDA_PIN,
		.scl_io_num = SCL_PIN,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 400000
	};
	
	i2c_param_config(I2C_NUM_0, &conf);
	ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
	
	
	while (true) {
		get_dht20_data(&temp_data, &humi_data);
        sleep(1);
        
    }
}
