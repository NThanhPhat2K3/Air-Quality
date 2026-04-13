#include "captive_dns.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

static const char *TAG = "captive_dns";

#define DNS_HEADER_SIZE 12
#define DNS_PACKET_MAX_SIZE 512
#define DNS_QTYPE_A 1
#define DNS_QTYPE_AAAA 28
#define DNS_QCLASS_IN 1
#define DNS_TTL_SECONDS 60
#define DNS_STOP_RETRY_MAX 30
#define DNS_STOP_RETRY_DELAY_MS 50
#define DNS_RECV_TIMEOUT_SECONDS 1

typedef struct {
  size_t question_end;
  uint16_t qtype;
  uint16_t qclass;
} dns_question_info_t;

static TaskHandle_t s_dns_task;
static volatile bool s_dns_running;
static esp_ip4_addr_t s_redirect_ip;

static esp_ip4_addr_t get_ap_ip_or_default(void) {
  esp_ip4_addr_t ap_ip = {0};
  esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap_netif != NULL) {
    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
      return ip_info.ip;
    }
  }

  ip4_addr_t fallback = {0};
  IP4_ADDR(&fallback, 192, 168, 4, 1);
  ap_ip.addr = fallback.addr;
  return ap_ip;
}

static bool parse_dns_question(const uint8_t *query, size_t query_len,
                               dns_question_info_t *question_out) {
  if (query == NULL || question_out == NULL || query_len < DNS_HEADER_SIZE) {
    return false;
  }

  size_t offset = DNS_HEADER_SIZE;
  while (offset < query_len && query[offset] != 0) {
    uint8_t label_len = query[offset];
    size_t label_start = offset + 1;
    size_t label_end = label_start + (size_t)label_len;

    if ((label_len & 0xC0U) != 0 || label_len == 0) {
      return false;
    }
    if (label_len > 63 || label_end > query_len) {
      return false;
    }

    offset = label_end;
  }

  if (offset >= query_len || offset + 5 > query_len) {
    return false;
  }

  question_out->question_end = offset + 5;
  question_out->qtype = (uint16_t)((query[offset + 1] << 8) | query[offset + 2]);
  question_out->qclass = (uint16_t)((query[offset + 3] << 8) | query[offset + 4]);
  return true;
}

static int build_dns_a_response(const uint8_t *query, size_t query_len,
                                uint8_t *response, size_t response_len,
                                esp_ip4_addr_t answer_ip) {
  if (query == NULL || response == NULL || query_len < DNS_HEADER_SIZE ||
      response_len < DNS_HEADER_SIZE) {
    return -1;
  }

  bool is_response = (query[2] & 0x80U) != 0;
  uint8_t opcode = (uint8_t)((query[2] >> 3) & 0x0FU);
  uint16_t qdcount = (uint16_t)((query[4] << 8) | query[5]);
  if (is_response || opcode != 0 || qdcount != 1) {
    return -1;
  }

  dns_question_info_t question = {0};
  if (!parse_dns_question(query, query_len, &question)) {
    return -1;
  }
  if (question.qclass != DNS_QCLASS_IN) {
    return -1;
  }

  size_t question_size = question.question_end - DNS_HEADER_SIZE;
  bool add_a_record = (question.qtype == DNS_QTYPE_A);
  bool reply_no_answer = (question.qtype == DNS_QTYPE_AAAA);
  if (!add_a_record && !reply_no_answer) {
    return -1;
  }

  size_t answer_size = add_a_record ? 16 : 0;
  size_t required_size = DNS_HEADER_SIZE + question_size + answer_size;
  if (required_size > response_len) {
    return -1;
  }

  memset(response, 0, required_size);
  response[0] = query[0];
  response[1] = query[1];
  response[2] = (uint8_t)(0x80U | (query[2] & 0x01U));
  response[3] = 0x00;
  response[4] = 0x00;
  response[5] = 0x01;
  response[6] = 0x00;
  response[7] = add_a_record ? 0x01 : 0x00;

  memcpy(response + DNS_HEADER_SIZE, query + DNS_HEADER_SIZE, question_size);

  size_t out = DNS_HEADER_SIZE + question_size;
  if (!add_a_record) {
    return (int)out;
  }

  response[out++] = 0xC0;
  response[out++] = 0x0C;
  response[out++] = 0x00;
  response[out++] = DNS_QTYPE_A;
  response[out++] = 0x00;
  response[out++] = DNS_QCLASS_IN;
  response[out++] = (uint8_t)((DNS_TTL_SECONDS >> 24) & 0xFF);
  response[out++] = (uint8_t)((DNS_TTL_SECONDS >> 16) & 0xFF);
  response[out++] = (uint8_t)((DNS_TTL_SECONDS >> 8) & 0xFF);
  response[out++] = (uint8_t)(DNS_TTL_SECONDS & 0xFF);
  response[out++] = 0x00;
  response[out++] = 0x04;

  ip4_addr_t ip = {0};
  ip.addr = answer_ip.addr;
  response[out++] = ip4_addr1(&ip);
  response[out++] = ip4_addr2(&ip);
  response[out++] = ip4_addr3(&ip);
  response[out++] = ip4_addr4(&ip);

  return (int)out;
}

static void dns_server_task(void *arg) {
  (void)arg;

  int sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "socket create failed: errno=%d", errno);
    s_dns_running = false;
    s_dns_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  struct timeval timeout = {0};
  timeout.tv_sec = DNS_RECV_TIMEOUT_SECONDS;
  lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  struct sockaddr_in bind_addr = {0};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(53);
  bind_addr.sin_addr.s_addr = s_redirect_ip.addr;

  if (lwip_bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
    ESP_LOGE(TAG, "bind UDP53 failed: errno=%d", errno);
    lwip_close(sock);
    s_dns_running = false;
    s_dns_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  uint8_t query[DNS_PACKET_MAX_SIZE] = {0};
  uint8_t response[DNS_PACKET_MAX_SIZE] = {0};

  while (s_dns_running) {
    struct sockaddr_in from_addr = {0};
    socklen_t from_len = sizeof(from_addr);
    int len = lwip_recvfrom(sock, query, sizeof(query), 0,
                            (struct sockaddr *)&from_addr, &from_len);
    if (len <= 0) {
      if (len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        continue;
      }
      if (s_dns_running) {
        ESP_LOGW(TAG, "recvfrom failed: errno=%d", errno);
      }
      continue;
    }

    int resp_len = build_dns_a_response(query, (size_t)len, response,
                                        sizeof(response), s_redirect_ip);
    if (resp_len <= 0) {
      continue;
    }

    int sent = lwip_sendto(sock, response, (size_t)resp_len, 0,
                           (struct sockaddr *)&from_addr, from_len);
    if (sent < 0 && s_dns_running) {
      ESP_LOGW(TAG, "sendto failed: errno=%d", errno);
    }
  }

  lwip_close(sock);
  s_dns_task = NULL;
  vTaskDelete(NULL);
}

esp_err_t captive_dns_start(void) {
  if (s_dns_task != NULL) {
    return ESP_OK;
  }

  s_redirect_ip = get_ap_ip_or_default();
  s_dns_running = true;

  BaseType_t ok = xTaskCreate(dns_server_task, "captive_dns", 4096, NULL,
                              tskIDLE_PRIORITY + 1, &s_dns_task);
  if (ok != pdPASS) {
    s_dns_running = false;
    s_dns_task = NULL;
    return ESP_ERR_NO_MEM;
  }

  ip4_addr_t ip = {0};
  ip.addr = s_redirect_ip.addr;
  ESP_LOGI(TAG, "DNS captive started -> " IPSTR, IP2STR(&ip));
  return ESP_OK;
}

esp_err_t captive_dns_stop(void) {
  if (s_dns_task == NULL) {
    return ESP_OK;
  }

  s_dns_running = false;
  for (int i = 0; i < DNS_STOP_RETRY_MAX && s_dns_task != NULL; ++i) {
    vTaskDelay(pdMS_TO_TICKS(DNS_STOP_RETRY_DELAY_MS));
  }

  if (s_dns_task != NULL) {
    ESP_LOGW(TAG, "DNS captive stop timeout");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "DNS captive stopped");
  return ESP_OK;
}
