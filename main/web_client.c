/*
	Example using WEB Socket.
	This example code is in the Public Domain (or CC0 licensed, at your option.)
	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "client_task";

#include "websocket_server.h"

extern MessageBufferHandle_t xMessageBufferRx;
extern MessageBufferHandle_t xMessageBufferTx;
extern size_t xItemSize;

// Simple UTF-8 validator: returns true if buffer is valid UTF-8
static bool is_valid_utf8(const uint8_t *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = s[i];
        if (c <= 0x7F) { // ASCII
            i++;
            continue;
        }
        // Determine sequence length and minimal code point
        size_t seq_len = 0;
        uint32_t min_cp = 0;
        if ((c & 0xE0) == 0xC0) { // 110xxxxx -> 2 bytes
            seq_len = 2;
            min_cp = 0x80;
            if ((c & 0xFE) == 0xC0) return false; // overlong 2-byte start (0xC0/0xC1)
        } else if ((c & 0xF0) == 0xE0) { // 1110xxxx -> 3 bytes
            seq_len = 3;
            min_cp = 0x800;
        } else if ((c & 0xF8) == 0xF0) { // 11110xxx -> 4 bytes
            seq_len = 4;
            min_cp = 0x10000;
            if (c > 0xF4) return false; // max valid is 0xF4 (U+10FFFF)
        } else {
            return false; // invalid first byte
        }

        if (i + seq_len > len) return false; // truncated sequence
        uint32_t cp = c & ((1u << (8 - seq_len - 1)) - 1); // extract prefix bits
        for (size_t j = 1; j < seq_len; j++) {
            uint8_t cb = s[i + j];
            if ((cb & 0xC0) != 0x80) return false; // not a continuation byte
            cp = (cp << 6) | (cb & 0x3F);
        }
        // Reject overlongs and surrogate halves
        if (cp < min_cp) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false; // UTF-16 surrogates are invalid in UTF-8
        if (cp > 0x10FFFF) return false; // outside Unicode range
        i += seq_len;
    }
    return true;
}

void client_task(void* pvParameters) {
    ESP_LOGI(TAG, "Start");
    char messageBuffer[xItemSize];
    while(1) {
        size_t readBytes = xMessageBufferReceive(xMessageBufferRx, messageBuffer, sizeof(messageBuffer), portMAX_DELAY );
		ESP_LOGD(TAG, "readBytes=%d", readBytes);
		cJSON *root = cJSON_Parse(messageBuffer);
		if (cJSON_GetObjectItem(root, "id")) {
			char *id = cJSON_GetObjectItem(root,"id")->valuestring;
			ESP_LOGD(TAG, "id=%s",id);

			// Do something when the browser started
			if ( strcmp (id, "init") == 0) {
			} // end of init

			// Do something when the send button pressed.
			if ( strcmp (id, "send-request") == 0) {
				xMessageBufferSend(xMessageBufferTx, messageBuffer, readBytes, portMAX_DELAY);
			} // end of send-request

            if ( strcmp (id, "recv-request") == 0) {
                char *payload = cJSON_GetObjectItem(root,"payload")->valuestring;
                size_t payload_len = strlen(payload);
                bool utf8_ok = is_valid_utf8((const uint8_t *)payload, payload_len);

                if (utf8_ok) {
                    time_t now;
                    struct tm *tm_now;
                    now = time(NULL);
                    now = now + (CONFIG_LOCAL_TIMEZONE*60*60);
                    tm_now = localtime(&now);
                    ESP_LOGD(TAG, "DATE %04d/%02d/%02d", tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday);
                    ESP_LOGD(TAG, "TIME %02d:%02d:%02d", tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

                    char out[256];
                    char DEL = 0x04;
                    int outlen = snprintf(out, sizeof(out), "RECEIVE%c%02d:%02d:%02d%c%s", DEL, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec, DEL, payload);
                    if (outlen < 0) {
                        ESP_LOGE(TAG, "snprintf failed building websocket message");
                    } else if ((size_t)outlen >= sizeof(out)) {
                        ESP_LOGW(TAG, "websocket message truncated to %d bytes", outlen);
                        ws_server_send_text_all(out, sizeof(out)-1);
                    } else {
                        ESP_LOGI(TAG, "Sending UTF-8 payload via websocket (%d bytes)", (int)payload_len);
                        ws_server_send_text_all(out, outlen);
                    }
                } else {
                    ESP_LOGW(TAG, "Non-UTF-8 data received; not sending via websocket. Length=%d", (int)payload_len);
                    ESP_LOG_BUFFER_HEXDUMP(TAG, payload, payload_len, ESP_LOG_WARN);
                }
            } // end of recv-request

		}
		cJSON_Delete(root);
	}
	vTaskDelete(NULL);
}
