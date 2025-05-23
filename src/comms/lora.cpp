#include "lora.h"

#include "../config.h"
#include "../gps/gps.h"
#include "lora_defs.h"

// ── Radio instance
// ────────────────────────────────────────────────────────────
SX1262 lora = new Module(8, 14, 12, 13);

// ── Message tracking containers
// ───────────────────────────────────────────────
std::map<String, std::vector<Fragment>> outgoing;  // msg‑id  ➜ fragments
std::map<String, IncomingText> incoming;           // msg‑id  ➜ partial rx
std::map<String, uint32_t> recentMsgs;             // msg‑id  ➜ timestamp

static unsigned long lastSendTime = 0;  // global inter-fragment pacing

// ── Helpers
// ───────────────────────────────────────────────────────────────────
// ── Helpers
// ───────────────────────────────────────────────────────────────────
String generateMsgID() {  // <- no “static”
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

bool isRecentMessage(const String& id) {  // <- no “static”
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

static inline void sendAckConfirm(uint8_t msb, uint8_t lsb) {
  uint8_t pkt[3] = {TYPE_ACK_CONFIRM, msb, lsb};  // 1‑byte type + 2‑byte id
  lora.transmit(pkt, sizeof(pkt));
  lora.startReceive();
}

// ── Radio initialisation
// ──────────────────────────────────────────────────────
void initLoRa(float freq, int txPower) {
  if (lora.begin(freq) != RADIOLIB_ERR_NONE) {
    Serial.println("ERR|LORA_INIT");
    while (true) { /* halt */
    }
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

// ── Outgoing queue builder
// ────────────────────────────────────────────────────
void queueMessage(const String& type, const String& payload) {
  const String msg = type + "|" + String(settings.deviceName) + "|" + payload;
  const String id = generateMsgID();

  const uint8_t total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;
  std::vector<Fragment> frags;
  frags.reserve(total);

  const uint16_t id16 = strtoul(id.c_str(), nullptr, 16);

  for (uint8_t seq = 0; seq < total; ++seq) {
    Fragment f{};
    f.data[0] = TYPE_TEXT_FRAGMENT;
    f.data[1] = id16 >> 8;
    f.data[2] = id16 & 0xFF;
    f.data[3] = seq;
    f.data[4] = total;

    String chunk =
        msg.substring(seq * FRAG_DATA_LEN,
                      std::min((seq + 1) * FRAG_DATA_LEN, (int)msg.length()));
    f.data[5] = chunk.length();
    memcpy(&f.data[6], chunk.c_str(), chunk.length());

    f.length = chunk.length() + FRAG_HEADER_SIZE;
    f.retries = 0;
    f.timestamp = 0;
    frags.push_back(f);
  }
  outgoing[id] = std::move(frags);
}

// ── Transmission scheduler
// ────────────────────────────────────────────────────
void sendMessages() {
  const unsigned long RETRY_DELAY_MS = 2000;
  const unsigned long INTER_FRAGMENT_DELAY = 2000;

  if (millis() - lastSendTime < INTER_FRAGMENT_DELAY) {
    return;  // don't send anything yet — global pacing
  }

  for (auto it = outgoing.begin(); it != outgoing.end();
       /* ++ handled below */) {
    const String& id = it->first;
    bool allFragmentsRetried = true;
    bool allAckedOrFailed = true;

    for (auto& fr : it->second) {
      if (fr.acked) continue;
      if (fr.retries >= 2) continue;

      if (millis() - fr.timestamp >= RETRY_DELAY_MS) {
        int result = lora.transmit(fr.data, fr.length);
        fr.retries++;
        fr.timestamp = millis();
        lastSendTime = millis();  // pace next transmission globally

        if (result == RADIOLIB_ERR_NONE) {
          Serial.printf("SEND|%s|%d/%d|try=%d\n", id.c_str(),
                        (&fr - &it->second[0]) + 1, (int)it->second.size(),
                        fr.retries);
        } else {
          Serial.printf("SEND|FAIL|%s|SEQ=%d|ERR=%d\n", id.c_str(),
                        (&fr - &it->second[0]), result);
        }

        lora.startReceive();
        return;  // exit early to honor inter-fragment delay
      }

      if (!fr.acked && fr.retries < settings.maxRetries) {
        allAckedOrFailed = false;
        allFragmentsRetried = false;
      } else if (!fr.acked) {
        Serial.printf("SEND_FAILED|%s|SEQ=%d\n", id.c_str(),
                      (&fr - &it->second[0]));
      }
    }

    if (allAckedOrFailed) {
      if (!std::all_of(it->second.begin(), it->second.end(),
                       [](const Fragment& f) { return f.acked; })) {
        Serial.printf("SEND_FAILED|FINAL|%s\n", id.c_str());
      }
      it = outgoing.erase(it);  // done with this message
    } else {
      ++it;
    }
  }
}

// ── ACK‑CONFIRM handler
// ───────────────────────────────────────────────────────
void processAck(uint8_t* buf, size_t len) {
  if (len < 3 || buf[0] != TYPE_ACK_CONFIRM) return;

  String id = String((buf[1] << 8) | buf[2], HEX);
  id.toUpperCase();

  auto it = outgoing.find(id);
  if (it != outgoing.end()) {
    Serial.printf("ACK_CONFIRM|%s\n", id.c_str());
    outgoing.erase(it);
  }
}

// ── Packet dispatcher
// ─────────────────────────────────────────────────────────
void handleIncoming(uint8_t* buf, size_t len) {
  if (len < FRAG_HEADER_SIZE) return;

  const uint8_t type = buf[0] & 0x0F;

  // ── TEXT FRAGMENT ─────────────────────────────────────────────────────────
  if (type == TYPE_TEXT_FRAGMENT) {
    String id = String((buf[1] << 8) | buf[2], HEX);
    id.toUpperCase();

    const uint8_t seq = buf[3];
    const uint8_t total = buf[4];
    const uint8_t plen = buf[5];
    if (seq >= total || plen + FRAG_HEADER_SIZE > len) return;

    IncomingText& msg = incoming[id];
    if (msg.received.size() != total) {
      msg.received.assign(total, false);
      msg.parts.clear();
    }

    msg.parts[seq] = String((char*)&buf[6], plen);
    msg.received[seq] = true;
    Serial.printf("RECV|FRAG|%s|%d/%d\n", id.c_str(), seq + 1, total);

    // All fragments received?
    if (std::all_of(msg.received.begin(), msg.received.end(),
                    [](bool b) { return b; })) {
      if (isRecentMessage(id)) {  // duplicate – discard
        incoming.erase(id);
        return;
      }

      String full;
      for (uint8_t i = 0; i < total; ++i) full += msg.parts[i];
      Serial.printf("RECV|FULL|%s\n", full.c_str());

      // Send one ACK_CONFIRM back to the sender
      sendAckConfirm(buf[1], buf[2]);

      incoming.erase(id);  // tidy up
    }
    return;
  }

  // ── ACK‑CONFIRM ───────────────────────────────────────────────────────────
  if (type == TYPE_ACK_CONFIRM) {
    processAck(buf, len);
  }
}

// ── Beacon helper
// ─────────────────────────────────────────────────────────────
void sendBeacon() {
  ReaperGPSData g = getGPSData();
  String payload = String(g.latitude, 6) + "," + String(g.longitude, 6) + "," +
                   String(g.altitude, 2) + "," + String(g.speed, 2) + "," +
                   String(g.course) + "," + String(g.satellites);
  queueMessage("BEACON", payload);
}