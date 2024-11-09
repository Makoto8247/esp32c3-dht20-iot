#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include <driver/i2c.h>
#include <lwip/apps/sntp.h>
#include <esp_http_client.h>
#include <stdio.h>
#include <stdlib.h>

#define SDA_PIN GPIO_NUM_6
#define SCL_PIN GPIO_NUM_7
#define DHT20_ADDR 0x38

#define SLEEP_TIME_SECONDS (30 * 60)

static bool wifi_connected = false;

extern const char _binary_ESP32_pem_start[];
extern const char _binary_ESP32_pem_end[];

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        printf("Connecting to WiFi...\n");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        printf("WiFi connected.\n");
        wifi_connected = true;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("WiFi disconnected, retrying...\n");
        wifi_connected = false;
        vTaskDelay(100/portTICK_PERIOD_MS);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("Got IP: \n" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

// Function to initialize Wi-Fi and connect to the network
bool wifi_init(void) {
    // Initialize NVS flash
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default Wi-Fi station interface
    esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register Wi-Fi and IP event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Configure Wi-Fi connection settings (SSID, password)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    // Set Wi-Fi mode to station and apply the configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    // Start Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_start());
    while (!wifi_connected) {
        printf("Waiting for Wi-Fi connection...\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 1 second delay (use vTaskDelay or any appropriate delay function)
    }
    printf("Wi-Fi connected, proceeding to the next task\n");
	return true;
}

void sync_time()
{
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_init();
	
	time_t now = 0;
	struct tm timeinfo = { 0 };
	int retry = 0;
	const int MAX_RETRIES = 10;
	
	while (timeinfo.tm_year < (2023 - 1900) && ++retry < MAX_RETRIES){
		printf("Waiting for time sync... (%d/%d)\n", retry, MAX_RETRIES);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
	}
	
	if (retry == MAX_RETRIES) {
		printf("Failed to sync time\n");
	} else {
		printf("Time synced: %s", asctime(&timeinfo));
	}
}

void http_get_request(const char* url)
{	
	esp_http_client_config_t config = {
		.url = url,
		.cert_pem = _binary_ESP32_pem_start
	};
	esp_http_client_handle_t client = esp_http_client_init(&config);
	
	esp_err_t err = esp_http_client_perform(client);
	if(err == ESP_OK) {
		printf("HTTP GET Request successful\n");
	} else {
		printf("HTTP GET Request failed: %s\n", esp_err_to_name(err));
	}
	
	esp_http_client_cleanup(client);
}

void get_dht20_data(double* temp_data, double* humi_data) 
{
	uint32_t processed_data = 0;
	uint8_t rx_data[8];
	const uint8_t DHT20_CMD[3] = {
		0b10101100,
		0b00110011,
		0b00000000
	};
	
	// Commands to retrieve data
	i2c_master_write_to_device(I2C_NUM_0, DHT20_ADDR, DHT20_CMD, sizeof(DHT20_CMD), 100);
	vTaskDelay(100 / portTICK_PERIOD_MS);
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
	while(!wifi_init());
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
	
	sync_time();
	
	while (true) {
		get_dht20_data(&temp_data, &humi_data);
		
		char* url = (char*)malloc((sizeof(CONFIG_GAS_URL) + 256) * sizeof(char));
		sprintf(url, "%s?temperature=%3.2f&humidity=%3.2f", CONFIG_GAS_URL, temp_data, humi_data);
		printf("%s\n", url);
		http_get_request(url);
        sleep(3);
        
        free(url);
    }
}
