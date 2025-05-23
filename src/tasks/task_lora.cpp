#include "task_lora.h"

#include "../comms/lora.h"

void taskLoRaHandler(void* param) {
  while (true) {
    uint8_t buf[MAX_FRAGMENT_SIZE];  // Use consistent max size
    int state = lora.receive(buf, sizeof(buf));
    if (state == RADIOLIB_ERR_NONE) {
      handleIncoming(buf, sizeof(buf));  // Fix: pass length
    }

    sendMessages();
    lora.startReceive();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
