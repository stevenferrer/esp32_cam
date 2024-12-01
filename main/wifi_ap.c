#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "string.h"

#include "wifi_ap.h"

#define WIFI_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define WIFI_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data) {
  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event =
        (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(WIFI_AP_TAG, "station " MACSTR " join, AID=%d",
             MAC2STR(event->mac), event->aid);

  } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event =
        (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(WIFI_AP_TAG, "station " MACSTR " leave, AID=%d, reason=%d",
             MAC2STR(event->mac), event->aid, event->reason);
  }
}

void wifi_ap_init(void) {
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ap_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .ap =
          {
              .ssid = WIFI_SSID,
              .ssid_len = strlen(WIFI_SSID),
              .channel = WIFI_CHANNEL,
              .password = WIFI_PASS,
              .max_connection = WIFI_MAX_STA_CONN,
              .authmode = WIFI_AUTH_WPA3_PSK,
              .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
              .pmf_cfg =
                  {
                      .required = true,
                  },
          },
  };
  if (strlen(WIFI_PASS) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(WIFI_AP_TAG, "wifi_ap_init finished. SSID:%s password:%s channel:%d",
           WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
}
