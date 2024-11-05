#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

void app_main(void)
{
    while (true) {
        printf("Hello from app_main!\n");
        sleep(1);
        printf(CONFIG_ESP_WIFI_SSID);
    }
}
