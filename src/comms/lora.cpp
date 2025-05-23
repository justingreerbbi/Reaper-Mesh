#include "lora.h"
#include "lora_defs.h"
#include "../config.h"
#include "../gps/gps.h"

SX1262 lora = new Module(8, 14, 12, 13);
std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, uint32_t> recentMsgs;

String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

bool isRecentMessage(const String& id) {
  uint32_t now = millis();
  for (auto it = recentMsgs.begin(); it != recentMsgs.end();) {
    if (now - it->second > BROADCAST_MEMORY_TIME)
      it = recentMsgs.erase(it);
    else
      ++it;
  }
  if (recentMsgs.count(id)) return true;
  recentMsgs[id] = now;
  return false;
}

void initLoRa(float freq, int txPower) {
  if (lora.begin(freq) != RADIOLIB_ERR_NONE) {
    Serial.println("ERR|LORA_INIT");
    while (true) {}
  }
  lora.setBandwidth(LORA_BANDWIDTH);
  lora.setSpreadingFactor(LORA_SPREADING_FACTOR);
  lora.setCodingRate(LORA_CODING_RATE);
  lora.setPreambleLength(LORA_PREAMBLE_LENGTH);
  lora.setSyncWord(LORA_SYNC_WORD);
  lora.setCRC(LORA_CRC);
  lora.setOutputPower(txPower);
  lora.startReceive();
}

void queueMessage(const String& type, const String& payload) {
  String msg = type + "|" + String(settings.deviceName) + "|" + payload;
  String id = generateMsgID();
  uint8_t total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  std::vector<Fragment> frags;
  for (uint8_t seq = 0; seq < total; ++seq) {
    Fragment f{};
    f.data[0] = TYPE_TEXT_FRAGMENT;
    uint16_t id16 = strtoul(id.c_str(), nullptr, 16);
    f.data[1] = id16 >> 8;
    f.data[2] = id16 & 0xFF;
    f.data[3] = seq;
    f.data[4] = total;

    String chunk = msg.substring(seq * FRAG_DATA_LEN, std::min((seq + 1) * FRAG_DATA_LEN, (int)msg.length()));
    f.data[5] = chunk.length();
    memcpy(&f.data[6], chunk.c_str(), chunk.length());

    f.length = chunk.length() + FRAG_HEADER_SIZE;
    f.retries = 0;
    f.timestamp = 0;
    f.acked = false;
    frags.push_back(f);
  }
  outgoing[id] = frags;
}

void sendMessages() {
  for (auto it = outgoing.begin(); it != outgoing.end();) {
    bool allAcked = true;

    for (auto& fr : it->second) {
      if (fr.acked || fr.retries >= settings.maxRetries) continue;

      if (fr.retries == 0 || (millis() - fr.timestamp >= settings.retryInterval)) {
        int result = lora.transmit(fr.data, fr.length);
        fr.retries++;
        fr.timestamp = millis();

        if (result == RADIOLIB_ERR_NONE) {
          Serial.printf("SEND|%s|%d/%d|try=%d\n", it->first.c_str(),
            (&fr - &it->second[0]) + 1, (int)it->second.size(), fr.retries);
        } else {
          Serial.printf("SEND|FAIL|%s|SEQ=%d|ERR=%d\n",
            it->first.c_str(), (&fr - &it->second[0]), result);
        }

        lora.startReceive();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }

      if (!fr.acked && fr.retries >= settings.maxRetries) {
        Serial.printf("SEND_FAILED|%s\n", it->first.c_str());
      }

      if (!fr.acked) allAcked = false;
    }

    if (allAcked) {
      it = outgoing.erase(it);
    } else {
      ++it;
    }
  }
}

void processAck(uint8_t* buf, size_t len) {
  if (len < 4 || buf[0] != TYPE_ACK_FRAGMENT) return;

  String id = String((buf[1] << 8) | buf[2], HEX);
  uint8_t seq = buf[3];
  auto it = outgoing.find(id);
  if (it != outgoing.end() && seq < it->second.size()) {
    it->second[seq].acked = true;
    Serial.printf("ACK|%s|SEQ=%d\n", id.c_str(), seq);
  }
}

void handleIncoming(uint8_t* buf, size_t len) {
  if (len < FRAG_HEADER_SIZE) return;

  uint8_t type = buf[0] & 0x0F;

  if (type == TYPE_TEXT_FRAGMENT) {
    String id = String((buf[1] << 8) | buf[2], HEX);
    id.toUpperCase();
    uint8_t seq = buf[3], total = buf[4], plen = buf[5];
    if (seq >= total || plen + FRAG_HEADER_SIZE > len) return;

    IncomingText& msg = incoming[id];
    if (msg.received.size() != total) {
      msg.received.assign(total, false);
      msg.parts.clear();
    }

    msg.parts[seq] = String((char*)&buf[6], plen);
    msg.received[seq] = true;
    Serial.printf("RECV|FRAG|%s|%d/%d\n", id.c_str(), seq + 1, total);

    // Send ACK fragment back
    uint8_t ack[16] = { TYPE_ACK_FRAGMENT, buf[1], buf[2], buf[3] };
    lora.transmit(ack, 16);
    lora.startReceive();

    if (std::all_of(msg.received.begin(), msg.received.end(), [](bool b) { return b; })) {
      if (isRecentMessage(id)) {
        incoming.erase(id);
        return;
      }

      String full;
      for (uint8_t i = 0; i < total; ++i)
        full += msg.parts[i];

      Serial.printf("RECV|FULL|%s\n", full.c_str());
      incoming.erase(id);
    }
    return;
  }

  if (type == TYPE_ACK_FRAGMENT) {
    processAck(buf, len);
  }
}

void sendBeacon() {
  ReaperGPSData g = getGPSData();
  String payload = String(g.latitude, 6) + "," +
                   String(g.longitude, 6) + "," +
                   String(g.altitude, 2) + "," +
                   String(g.speed, 2) + "," +
                   String(g.course) + "," +
                   String(g.satellites);
  queueMessage("BEACON", payload);
}
