#include "esp_camera.h"
#include "esp_log.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "freertos/idf_additions.h"

#include "camera.h"

#if ESP_CAMERA_SUPPORTED

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,    // QQVGA-UXGA, For ESP32, do not use sizes
                                 // above QVGA when not JPEG. The performance of
                                 // the ESP32-S series has improved a lot, but
                                 // JPEG mode always gives better frame rates.

    .jpeg_quality = 10, // 0-63, for OV series camera sensors, lower number
                        // means higher quality
    .fb_count = 2, // When jpeg mode is used, if fb_count more than one, the
                   // driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

esp_err_t camera_init(void) {
  // initialize the camera
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    ESP_LOGE(CAMERA_TAG, "camera init failed");
    return err;
  }

  return ESP_OK;
}

// #define CLIENT_IP "192.168.4.2"
// #define CLIENT_PORT "31416"

// struct addrinfo *get_client_addr() {
//   struct addrinfo hints;
//   memset(&hints, 0, sizeof(hints));
//   hints.ai_socktype = SOCK_DGRAM;
//   struct addrinfo *client_addr;
//   if (getaddrinfo(CLIENT_IP, CLIENT_PORT, &hints, &client_addr)) {
//     return NULL;
//   }

//   return client_addr;
// }

#define MAX_PACKET_SIZE 59192

struct sockaddr_in block_until_peer_request(int sock_cam) {
  struct sockaddr_in peer_addr;
  socklen_t addrlen = sizeof(peer_addr);
  char buf[4];

  while (1) {
    int bytes_recv = recvfrom(sock_cam, buf, sizeof(buf) - 1, 0,
                              (struct sockaddr *)&peer_addr, &addrlen);
    if (bytes_recv == 0) {
      break;
    }
  }

  return peer_addr;
}

static void camera_stream_task(void *pvParams) {
  int sock_cam = *((int *)pvParams);
  struct sockaddr_in peer_addr = block_until_peer_request(sock_cam);

  while (1) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      ESP_LOGE(CAMERA_TAG, "esp_camera_fb_get() failed.");
      continue;
    }

    uint8_t *buf = fb->buf;
    size_t buf_len = fb->len;
    size_t total_bytes_sent = 0;

    while (total_bytes_sent < buf_len) {
      size_t tx_len = buf_len - total_bytes_sent;
      if (tx_len > MAX_PACKET_SIZE) {
        tx_len = MAX_PACKET_SIZE;
      }

      int bytes_sent = sendto(sock_cam, &buf[total_bytes_sent], tx_len, 0,
                              (struct sockaddr *)&peer_addr, sizeof(peer_addr));
      if (bytes_sent < 0) {
        ESP_LOGE(CAMERA_TAG, "sendto() failed. %d", errno);
        if (errno == ENOMEM) {
          vTaskDelay(pdMS_TO_TICKS(10));
        } else {
          break;
        }
      } else if (bytes_sent == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
      } else {
        total_bytes_sent += bytes_sent;
      }
    }

    esp_camera_fb_return(fb);

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void camera_start(void) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *bind_addr;
  if (getaddrinfo(0, "9000", &hints, &bind_addr)) {
    ESP_LOGE(CAMERA_TAG, "getaddrinfo() failed. (%d)", errno);
    return;
  }

  int sock_cam = socket(bind_addr->ai_family, bind_addr->ai_socktype,
                        bind_addr->ai_protocol);
  if (sock_cam < 0) {
    ESP_LOGE(CAMERA_TAG, "socket() failed. (%d)", errno);
    return;
  }

  if (bind(sock_cam, bind_addr->ai_addr, bind_addr->ai_addrlen)) {
    ESP_LOGE(CAMERA_TAG, "bind() failed. (%d)", errno);
    return;
  }
  freeaddrinfo(bind_addr);

  xTaskCreatePinnedToCore(camera_stream_task, "camera_stream", 4096,
                          (void *)&sock_cam, 5, NULL, tskNO_AFFINITY);
}

#endif
