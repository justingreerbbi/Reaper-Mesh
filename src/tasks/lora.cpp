#include "lora.h"

#include <AES.h>
#include <Crypto.h>
#include <RadioLib.h>
#include <map>
#include <vector>
#include <Arduino.h>

#include "../gps/gps.h"
#include "../system/settings.h"
#include  "../config.h"

AES128 aes;
uint8_t aes_key[16] = {0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
                       0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81};

SX1262 lora = new Module(8, 14, 12, 13);
std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

bool isTransmitting = false;
int retryAttemptLimit = 3;

void encryptFragment(uint8_t *b) { aes.encryptBlock(b, b); }
void decryptFragment(uint8_t *b) { aes.decryptBlock(b, b); }

// Generate a random message ID
// The ID is a 4-digit hexadecimal number, generated using esp_random().
String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

// Initialize the LoRa module with the given frequency and transmission power.
void initLoRa(float freq, int txPower) {
  aes.setKey(aes_key, sizeof(aes_key));
  int state = lora.begin(freq);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("ERR|LORA_INIT_FAILED|%d\n", state);
    while (1);
  }

  // Set LoRa parameters. For now, we use a set of hard coded parameters to keep
  // the system simple. These can be changed later to allow for more flexibility
  // but for now, we will keep it simple and use a set of hard coded parameters.
  lora.setBandwidth(LORA_BANDWIDTH);
  lora.setSpreadingFactor(LORA_SPREADING_FACTOR);
  lora.setCodingRate(LORA_CODING_RATE);
  lora.setPreambleLength(LORA_PREAMBLE_LENGTH);
  lora.setSyncWord(LORA_SYNC_WORD);
  lora.setOutputPower(txPower);
  lora.setCRC(LORA_CRC);
  lora.startReceive();
}

// Check if the message ID is recent.
// If it is, return true. Otherwise, add it to the recent messages map and
// return false.
bool isRecentMessage(const String &msgId) {
  unsigned long now = millis();
  for (auto it = recentMsgs.begin(); it != recentMsgs.end();) {
    if (now - it->second > BROADCAST_MEMORY_TIME)
      it = recentMsgs.erase(it);
    else
      ++it;
  }
  if (recentMsgs.count(msgId)) return true;
  recentMsgs[msgId] = now;
  return false;
}

// Handle all incoming messages.
// At this point, we do not know what was sent to us, so we will need to
// decrypt the message and then process it accordingly.
void handleIncoming(uint8_t *buf) {
  // Before we can do anything, we need to decrypt the incoming.
  decryptFragment(buf);
  uint8_t type = buf[0] & 0x0F;

  // If incoming is a MESSAGE FRAGMENT (3 or 00x3)
  if (type == TYPE_TEXT_FRAGMENT) {
    // Get the message ID from the fragment.
    String msgId = String((buf[1] << 8) | buf[2], HEX);
    msgId.toUpperCase();  // For uniformity.

    // Current sequence number of the total fragments.
    uint8_t seq = buf[3];

    // Total number of fragments.
    uint8_t total = buf[4];

    // Define part as a String to get ready to start building the message.
    String part;

    // We start at 5 because the first 5 bytes are the header.
    // It is important to note that is the sending is changed, this will need to
    // be updated as well.
    for (int i = 5; i < AES_BLOCK_LEN; i++) {
      // If we reach the end of the fragment, break.
      // The end will be marked by a 0x00 (empty) byte.
      if (buf[i] == 0x00) {
        break;
      }

      // Otherwise, add the characters of each buffer to the "part".
      part += (char)buf[i];
    }

    IncomingText &msg = incoming[msgId];
    if (msg.received.size() != total) {
      msg.total = total;
      msg.received.assign(total, false);
    }
    msg.parts[seq] = part;
    msg.received[seq] = true;

    // Report to the serial that we received a fragment.
    // @todo: We will need to add the sender to this as well.
    Serial.printf("RECV|FRAG|%s|%d/%d\n", msgId.c_str(), seq + 1, total);

    // Complete message is true and if all fragments are note received, then it
    // will be sent to false.
    bool complete = true;
    for (bool got : msg.received) {
      if (!got) {
        complete = false;
        break;
      }
    }

    // If the message is complete we will process the message.
    if (complete) {
      // Due to lora and the possibility that the message is just a duplicate
      // because ACK_CONFIRM was not received, we will check if the message is
      // recent. If it is, we will remove it from the incoming map and return.
      if (isRecentMessage(msgId)) {
        incoming.erase(msgId);
        return;
      }

      // Send Back an ACK_CONFIRM
      // @todo: I would love to send the ACK_CONFIRM after we process the serial
      // response just to be more responsive ot the user.
      // @todo: We will need to move this to is own function eventually.
      uint8_t ackConfirm[AES_BLOCK_LEN] = {0};
      ackConfirm[0] = TYPE_ACK_CONFIRM;
      ackConfirm[1] = buf[1];
      ackConfirm[2] = buf[2];
      ackConfirm[3] = buf[3];

      // Encrypt the ACK_CONFIRM message before sending it out.
      encryptFragment(ackConfirm);

      // Transmit the ACK_CONFIRM message. transmit is blocking, so we will
      // wait for the transmission to complete before continuing.
      lora.transmit(ackConfirm, AES_BLOCK_LEN);

      // Delay . Be sure to delay using vTaskDelay and not delay.
      // This is because delay will block the task and vTaskDelay will not.
      vTaskDelay(500 / portTICK_PERIOD_MS);

      // Put LoRa back into receive mode.
      lora.startReceive();
      // END COMPLETE

      // Build the full message from the fragments.
      String fullMessage;
      for (int i = 0; i < total; i++) {
        fullMessage += msg.parts[i];
      }

      std::vector<String> parts;
      int last = 0, next = 0;

      while ((next = fullMessage.indexOf('|', last)) != -1) {
        parts.push_back(fullMessage.substring(last, next));
        last = next + 1;
      }
      parts.push_back(fullMessage.substring(last));

      // Get the message type and sender from the message.
      // This may change if the sending logic is updated. This will need to be
      // updated as well.
      String msgType = parts[0];
      String sender = parts[1];

      // If the message is a global message, the type will be "MSG"
      if (msgType == "MSG") {
        String message = parts[2];
        Serial.printf("RECV|MSG|%s|%s|%s\n", sender.c_str(), message.c_str(),
                      msgId.c_str());

        // If the message is a direct message, the type will be "DMSG"
      } else if (msgType == "DMSG") {
        String recipient = parts[2];
        String message = parts[3];
        Serial.printf("RECV|DMSG|%s|%s|%s|%s\n", sender.c_str(),
                      recipient.c_str(), message.c_str(), msgId.c_str());

        // If the message is a beacon message, the type will be "BEACON"
      } else if (msgType == "BEACON") {
        Serial.print("RECV|");
        Serial.println(fullMessage);

        // Else, we don;t know what the message is so we will report is as such.
      } else {
        Serial.printf("RECV|UNKNOWN|%s\n", fullMessage.c_str());
      }
      // END COMPLETE
    }

    // If we receive  the type for an ACK_CONFIRM, we will process the ACK.
    // @todo: I would also love to update the transmit logic for this so we can
    // get the device name for and ACK so that the app will know who got the
    // message and we can display it in the app at some point.
  } else if (type == TYPE_ACK_CONFIRM) {
    char bufId[5];
    snprintf(bufId, sizeof(bufId), "%02X%02X", buf[1], buf[2]);
    String mId(bufId);
    mId.toUpperCase();
    for (auto &frag : outgoing[mId]) frag.acked = true;
    Serial.printf("ACK|CONFIRM|%s\n", mId.c_str());
  }
  // END
}

// Send messages in the outgoing queue....
// This functions is called in the main.cpp file if we ever want to modify how
// and when it gets called.
void sendMessages() {
  if(isTransmitting) return;
  isTransmitting = true;

  // Start loop through the outgoing messages/fragments.
  for (auto it = outgoing.begin(); it != outgoing.end();) {
    bool allAcked = true;
    for (auto &frag : it->second) {

      if (frag.acked || frag.retries >= retryAttemptLimit) {
        continue;
      }

      // Send out the fragment and report the status to the serial.
      int state = lora.transmit(frag.data, AES_BLOCK_LEN);
      if (state == RADIOLIB_ERR_NONE) {
        frag.timestamp = millis();
        frag.retries++;
        Serial.printf("SEND|ATTEMPT|%s|%d/%d|try=%d\n", it->first.c_str(),
                      (&frag - &it->second[0]) + 1, (int)it->second.size(),
                      frag.retries);
      }
      lora.startReceive();
      allAcked = false;
    }

    // If all ac
    if (allAcked)
      it = outgoing.erase(it);
    else
      ++it;
  }
  isTransmitting = false;
  lora.startReceive(); // Just in case?
}

// Send a beacon maesage.
// This should be move to its own function eventually. This needs to be updated
// to check if the GPS data is valid or not before just blindly sending out the
// info.
void sendBeacon() {
  String msg;
  ReaperGPSData data = getGPSData();
  msg = String(data.latitude, 6);
  msg += "," + String(data.longitude, 6);
  msg += "," + String(data.altitude);
  msg += "," + String(data.speed);
  msg += "," + String(data.course);
  msg += "," + String(data.satellites);
  msg = "BEACON|" + String(settings.deviceName) + "|" + msg;
  processMessageToOutgoing(msg);
}

/**
 * Process a message to the outgoing messages.
 */
void processMessageToOutgoing(String msg) {
  String msgId = generateMsgID();
  std::vector<Fragment> frags;
  int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  for (int i = 0; i < total; i++) {
    uint8_t block[AES_BLOCK_LEN] = {0};
    block[0] = PRIORITY_NORMAL;
    block[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
    block[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
    block[3] = i;
    block[4] = total;

    String chunk = msg.substring(
      i * FRAG_DATA_LEN,
      min((i + 1) * FRAG_DATA_LEN, (int)msg.length())
    );

    memset(&block[5], 0, AES_BLOCK_LEN - 5);
    memcpy(&block[5], chunk.c_str(), chunk.length());
    encryptFragment(block);

    Fragment frag;
    memcpy(frag.data, block, AES_BLOCK_LEN);
    frag.retries = 0;
    frag.timestamp = millis();
    frag.acked = false;

    frags.push_back(frag);
  }

  outgoing[msgId] = frags;
}


/**
 * TASK HANDLER FOR LoRa
 */
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
