#include "task_lora.h"
#include "../comms/lora.h"
#include "../comms/lora_defs.h"

void taskLoRaHandler(void*){
  static uint8_t buf[MAX_FRAGMENT_SIZE];

  while (true) {
    int state = lora.receive(buf, sizeof(buf));

    if (state == RADIOLIB_ERR_NONE) {          // ← 0 = success
      size_t len = lora.getPacketLength();     // real number of bytes received
      handleIncoming(buf, len);                // pass correct length
    }
    else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
      Serial.printf("RECV|ERR|%d\n", state);   // log only genuine errors
    }

    lora.startReceive();                       // re-arm RX
    sendMessages();                            // retries / outbound
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}
