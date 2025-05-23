#include "task_lora.h"

#include "../comms/lora.h"

void taskLoRaHandler(void* param) {
  while (true) {
    uint8_t buf[200];
    int state = lora.receive(buf, sizeof(buf));
    if (state == RADIOLIB_ERR_NONE) {
      handleIncoming(buf);
    }

    // @todo: Maybe only do this ever 5 seconds or so? Not sure how that will
    // affect the system.
    sendMessages();
    lora.startReceive();
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}
