#include <RadioLib.h>
#include <EEPROM.h>
#include <vector>
#include <AES.h>
#include <string.h>

#define EEPROM_SIZE 128
#define ADDR_MAGIC        0
#define ADDR_DEVICE_NAME  4
#define ADDR_CHANNEL      36
#define EEPROM_MAGIC      0x42

#define MAX_HOPS 3
#define ROUTE_TABLE_SIZE 20
#define BANDWIDTH 500
#define SPREADING_FACTOR 7
#define CODING_RATE 5
#define PREAMBLE_LENGTH 8
#define SYNC_WORD 0xF3  // Custom sync word for private network
#define TX_POWER 14

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2 pins

float channels[] = {902.0, 904.0, 908.0, 910.0, 912.0, 914.0, 915.0};
const int numChannels = sizeof(channels) / sizeof(channels[0]);

char deviceName[32] = "NinaMesh1";
int currentChannel = 1;

bool awaitingAck = false;
int currentMsgId = 0;
char pendingMessage[128];
unsigned long ackWaitStart = 0;
const unsigned long ackTimeout = 4000;

std::vector<String> routeTable;

// === AES ===
AES128 aes;
uint8_t aes_key[] = {
  0x60, 0x3d, 0xeb, 0x10,
  0x15, 0xca, 0x71, 0xbe,
  0x2b, 0x73, 0xae, 0xf0,
  0x85, 0x7d, 0x77, 0x81
};

// ==== EEPROM ====
void saveSettings() {
  EEPROM.write(ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.put(ADDR_DEVICE_NAME, deviceName);
  EEPROM.write(ADDR_CHANNEL, currentChannel);
  EEPROM.commit();
}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(ADDR_MAGIC) != EEPROM_MAGIC) {
    strcpy(deviceName, "Device223");
    currentChannel = 1;
    saveSettings();
    return;
  }
  EEPROM.get(ADDR_DEVICE_NAME, deviceName);
  currentChannel = EEPROM.read(ADDR_CHANNEL);
  if (currentChannel < 1 || currentChannel > numChannels)
    currentChannel = 1;
}

// ==== Routing Table ====
String getMessageKey(const char* origin, int msgId) {
  return String(origin) + "|" + String(msgId);
}

void addToRouteTable(const String& key) {
  if (routeTable.size() >= ROUTE_TABLE_SIZE) {
    routeTable.erase(routeTable.begin());
  }
  routeTable.push_back(key);
}

bool isDuplicate(const String& key) {
  for (const String& entry : routeTable) {
    if (entry == key) return true;
  }
  return false;
}

// ==== Encrypt / Decrypt ====
String encryptMessage(String msg) {
  uint8_t padded[32] = {0};
  strncpy((char*)padded, msg.c_str(), sizeof(padded));
  for (int i = 0; i < 32; i += 16)
    aes.encryptBlock(padded + i, padded + i);
  return String((char*)padded, 32);
}

String decryptMessage(uint8_t* data, size_t len) {
  if (len % 16 != 0) return "";
  char output[33] = {0};
  for (size_t i = 0; i < len; i += 16)
    aes.decryptBlock(data + i, (uint8_t*)&output[i]);
  output[len] = '\0';
  return String(output);
}

// ==== LoRa Setup ====
void setupLoRa() {
  int state = lora.begin(channels[currentChannel - 1], BANDWIDTH);
  lora.setSpreadingFactor(SPREADING_FACTOR);
  lora.setCodingRate(CODING_RATE);
  lora.setPreambleLength(PREAMBLE_LENGTH);
  lora.setSyncWord(SYNC_WORD);
  lora.setOutputPower(TX_POWER);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LOG|INIT|LoRa RX ready");
    lora.startReceive();
  } else {
    Serial.printf("ERR|INIT_FAIL|Code=%d\n", state);
    while (true);
  }
}

// ==== Parse Message ====
void parseReceivedMessage(const char* decryptedMsg) {
  char type[16], origin[32], content[128];
  int hops;

  sscanf(decryptedMsg, "%15[^|]|%31[^|]|%127[^|]|%d", type, origin, content, &hops);

  int msgId = 0;
  sscanf(content, "%*[^[][%d]", &msgId);
  if (msgId == 0) sscanf(decryptedMsg, "%*[^|]|%*[^|]|%*[^|]|%d", &msgId);

  String key = getMessageKey(origin, msgId);
  if (isDuplicate(key)) {
    Serial.printf("LOG|DUPLICATE_IGNORED|%s\n", key.c_str());
    return;
  }

  Serial.printf("LOG|RECV|TYPE=%s|FROM=%s|MSG=%s|HOPS=%d|ID=%d\n", type, origin, content, hops, msgId);
  addToRouteTable(key);

  if (strcmp(type, "MSG") == 0 && strcmp(origin, deviceName) != 0) {
    char ackMsg[64];
    snprintf(ackMsg, sizeof(ackMsg), "ACK|%s|%d|1", deviceName, msgId);
    String encAck = encryptMessage(String(ackMsg));
    lora.transmit((uint8_t*)encAck.c_str(), encAck.length());
    Serial.printf("ACK|SENT|ID=%d\n", msgId);
    delay(200);

    if (hops < MAX_HOPS) {
      char forward[128];
      snprintf(forward, sizeof(forward), "MSG|%s|%s|%d", origin, content, hops + 1);
      String encFwd = encryptMessage(String(forward));
      lora.transmit((uint8_t*)encFwd.c_str(), encFwd.length());
      Serial.printf("LOG|FORWARDED|%s\n", forward);
      delay(200);
    }
  } else if (strcmp(type, "ACK") == 0) {
    int ackId = atoi(content);
    if (ackId == currentMsgId) {
      Serial.printf("ACK|RECV|ID=%d\n", ackId);
      awaitingAck = false;
    }
  }
}

// ==== AT Commands ====
void processATCommand(String command) {
  if (command == "AT") {
    Serial.println("OK");
  } else if (command.startsWith("AT+MSG=")) {
    String message = command.substring(8);
    currentMsgId++;
    snprintf(pendingMessage, sizeof(pendingMessage), "MSG|%s|%s|%d", deviceName, message.c_str(), currentMsgId);
    String enc = encryptMessage(String(pendingMessage));
    int state = lora.transmit((uint8_t*)enc.c_str(), enc.length());
    if (state == RADIOLIB_ERR_NONE) {
      Serial.printf("LOG|MSG_SENT|%s\n", pendingMessage);
      awaitingAck = true;
      ackWaitStart = millis();
    } else {
      Serial.printf("ERR|TX_FAIL|Code=%d\n", state);
    }
    lora.startReceive();
  } else if (command.startsWith("AT+NAME=")) {
    String newName = command.substring(8);
    if (newName.length() < sizeof(deviceName)) {
      newName.toCharArray(deviceName, sizeof(deviceName));
      saveSettings();
      Serial.printf("LOG|NAME_UPDATED|%s\n", deviceName);
      delay(500);
      ESP.restart();
    } else {
      Serial.println("ERR|NAME_TOO_LONG");
    }
  } else {
    Serial.println("ERR|UNKNOWN_COMMAND");
  }
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  while (!Serial);

  loadSettings();
  Serial.printf("LOG|BOOT|Device=%s|Channel=%.1f\n", deviceName, channels[currentChannel - 1]);
  aes.setKey(aes_key, sizeof(aes_key));
  setupLoRa();
}

// ==== Loop ====
void loop() {
  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    String decrypted = decryptMessage(buf, lora.getPacketLength());
    if (decrypted.length() > 0) {
      parseReceivedMessage(decrypted.c_str());
    } else {
      Serial.println("ERR|DECRYPT_FAIL");
    }
    lora.startReceive();
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.printf("ERR|RECV_FAIL|Code=%d\n", state);
    lora.startReceive();
  }

  if (awaitingAck && millis() - ackWaitStart > ackTimeout) {
    Serial.printf("ACK|TIMEOUT|ID=%d\n", currentMsgId);
    awaitingAck = false;
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    processATCommand(command);
  }
}
// ==== End of File ====