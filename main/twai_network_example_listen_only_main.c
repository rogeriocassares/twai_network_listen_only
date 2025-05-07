/*
 * SPDX-FileCopyrightText: 2010-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/*
 * The following example demonstrates a Listen Only node in a TWAI network. The
 * Listen Only node will not take part in any TWAI bus activity (no acknowledgments
 * and no error frames). This example will execute multiple iterations, with each
 * iteration the Listen Only node will do the following:
 * 1) Listen for ping and ping response
 * 2) Listen for start command
 * 3) Listen for data messages
 * 4) Listen for stop and stop response
 */
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"

#include <time.h>

/* --------------------- Definitions and static variables ------------------ */
// Example Configuration
#define NO_OF_ITERS 3
#define RX_TASK_PRIO 9
#define TX_GPIO_NUM CONFIG_EXAMPLE_TX_GPIO_NUM
#define RX_GPIO_NUM CONFIG_EXAMPLE_RX_GPIO_NUM
#define EXAMPLE_TAG "TWAI Listen Only"

#define ID_MASTER_STOP_CMD 0x0A0
#define ID_MASTER_START_CMD 0x0A1
#define ID_MASTER_PING 0x0A2
#define ID_SLAVE_STOP_RESP 0x0B0
#define ID_SLAVE_DATA 0x0B1
#define ID_SLAVE_PING_RESP 0x0B2

static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
// static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_25KBITS();
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
// static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
// Set TX queue length to 0 due to listen only mode
static const twai_general_config_t g_config = {.mode = TWAI_MODE_LISTEN_ONLY,
                                               .tx_io = TX_GPIO_NUM,
                                               .rx_io = RX_GPIO_NUM,
                                               .clkout_io = TWAI_IO_UNUSED,
                                               .bus_off_io = TWAI_IO_UNUSED,
                                               .tx_queue_len = 0,
                                               .rx_queue_len = 5,
                                               .alerts_enabled = TWAI_ALERT_NONE,
                                               .clkout_divider = 0};

static SemaphoreHandle_t rx_sem;

/* --------------------------- Tasks and Functions -------------------------- */
// #define NB_PKT_MAX      8 /* max number of packets per fetch/send cycle */
// struct lgw_pkt_rx_s rxpkt[NB_PKT_MAX]; /* array containing inbound packets + metadata */
// struct lgw_pkt_rx_s *p; /* pointer on a RX packet */
// int nb_pkt;

////////////////////////
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

// Function to encode 3 bytes (24 bits) to 4 Base64 characters
void encode_block(unsigned char in[3], unsigned char out[4], int len)
{
  out[0] = base64_chars[(in[0] & 0xfc) >> 2];
  out[1] = base64_chars[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
  out[2] = (len > 1) ? base64_chars[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)] : '=';
  out[3] = (len > 2) ? base64_chars[in[2] & 0x3f] : '=';
}

// Function to convert a binary number to a Base64 string
void binary_to_base64(const unsigned char *binary, size_t len, char *base64)
{
  int i, j;
  unsigned char in[3], out[4];
  size_t out_len = ((len + 2) / 3) * 4;

  for (i = 0, j = 0; i < len; i += 3)
  {
    in[0] = binary[i];
    in[1] = (i + 1 < len) ? binary[i + 1] : 0;
    in[2] = (i + 2 < len) ? binary[i + 2] : 0;

    encode_block(in, out, len - i);

    base64[j++] = out[0];
    base64[j++] = out[1];
    base64[j++] = out[2];
    base64[j++] = out[3];
  }

  base64[out_len] = '\0';
}
//////////////////////////////////

static void twai_receive_task(void *arg)
{

  // ///////////////
  // // Example binary data
  // // unsigned char binary_data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello" in binary
  // unsigned char binary_data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello" in binary
  // size_t binary_len = sizeof(binary_data);

  // // Allocate memory for the Base64 string
  // size_t base64_len = ((binary_len + 2) / 3) * 4 + 1;
  // char *base64_data = (char *)malloc(base64_len);

  // // Convert binary to Base64
  // binary_to_base64(binary_data, binary_len, base64_data);

  // // Print the Base64 string
  // printf("Base64: %s\n", base64_data);

  // // Free allocated memory
  // free(base64_data);
  // //////////////

  xSemaphoreTake(rx_sem, portMAX_DELAY);

  // size_t binary_len = sizeof(binary_data);

  while (1)
  {
    twai_message_t rx_msg;
    twai_receive(&rx_msg, portMAX_DELAY);
    unsigned long long data = 0;
    for (int i = 0; i < rx_msg.data_length_code; i++)
    {
      data = data << 8 | rx_msg.data[i];
    }
    ESP_LOGI(EXAMPLE_TAG, "Id: %lu, Msg: %llu", rx_msg.identifier, data);
    // base64_encode(data, buffer);
    vTaskDelay(100);
  }

  xSemaphoreGive(rx_sem);
  vTaskDelete(NULL);
}

void app_main(void)
{
  rx_sem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(twai_receive_task, "TWAI_rx", 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);

  // Install and start TWAI driver
  ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
  ESP_LOGI(EXAMPLE_TAG, "Driver installed");
  ESP_ERROR_CHECK(twai_start());
  ESP_LOGI(EXAMPLE_TAG, "Driver started");

  xSemaphoreGive(rx_sem); // Start RX task
  vTaskDelay(pdMS_TO_TICKS(100));
  xSemaphoreTake(rx_sem, portMAX_DELAY); // Wait for RX task to complete

  // Stop and uninstall TWAI driver
  ESP_ERROR_CHECK(twai_stop());
  ESP_LOGI(EXAMPLE_TAG, "Driver stopped");
  ESP_ERROR_CHECK(twai_driver_uninstall());
  ESP_LOGI(EXAMPLE_TAG, "Driver uninstalled");

  // Cleanup
  vSemaphoreDelete(rx_sem);
}
