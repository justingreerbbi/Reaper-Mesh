#include <RadioLib.h>
#include <EEPROM.h>
#include <vector>

#define EEPROM_SIZE 128
#define MAX_HOPS 3
#define ROUTE_TABLE_SIZE 20

#define ADDR_MAGIC        0
#define ADDR_DEVICE_NAME  4
#define ADDR_CHANNEL      36
#define EEPROM_MAGIC      0x42

#define BANDWIDTH 500
#define SPREADING_FACTOR 7
#define CODING_RATE 5
#define PREAMBLE_LENGTH 8
#define SYNC_WORD 0x12
#define TX_POWER 14

SX1262 lora = new Module(8, 14, 12, 13); // Heltec V3 pins

float channels[] = {902.0, 904.0, 908.0, 910.0, 912.0, 914.0, 915.0};
const int numChannels = sizeof(channels) / sizeof(channels[0]);

char deviceName[32] = "NinaMesh1";
int currentChannel = 1;

unsigned long ackWaitStart = 0;
const unsigned long ackTimeout = 4000;
bool awaitingAck = false;
int currentMsgId = 0;
char pendingMessage[128];

std::vector<String> routeTable;

void sendMessage(const char* deviceName, const char* message);

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

// ==== LoRa Init ====
void setupLoRa() {
  int state = lora.begin(channels[currentChannel - 1], BANDWIDTH);
  lora.setSpreadingFactor(SPREADING_FACTOR);
  lora.setCodingRate(CODING_RATE);
  lora.setPreambleLength(PREAMBLE_LENGTH);
  lora.setSyncWord(SYNC_WORD);
  lora.setOutputPower(TX_POWER);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LOG|INIT|LoRa RX ready");
    lora.startReceive();  // persistent RX
  } else {
    Serial.printf("ERR|INIT_FAILED|Code=%d\n", state);
    while (true);
  }
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

// ==== Forwarding ====
void forwardMessage(const char* type, const char* origin, const char* content, int hops, int msgId) {
  if (strcmp(origin, deviceName) == 0) return;
  if (hops >= MAX_HOPS) return;

  String key = getMessageKey(origin, msgId);
  if (isDuplicate(key)) return;

  char forward[128];
  snprintf(forward, sizeof(forward), "%s|%s|%s|%d", type, origin, content, hops + 1);

  int state = lora.transmit((uint8_t*)forward, strlen(forward));
  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("LOG|FORWARDED|%s\n", forward);
    addToRouteTable(key);
  } else {
    Serial.printf("ERR|FORWARD_FAIL|Code=%d\n", state);
  }

  delay(200);
  lora.startReceive();  // resume RX
}

// ==== Parse Message ====
void parseReceivedMessage(const char* message) {
  char type[16], origin[32], content[128];
  int hops;

  sscanf(message, "%15[^|]|%31[^|]|%127[^|]|%d", type, origin, content, &hops);

  int msgId = 0;
  sscanf(content, "%*[^[][%d]", &msgId);
  if (msgId == 0) sscanf(message, "%*[^|]|%*[^|]|%*[^|]|%d", &msgId);

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
    lora.transmit((uint8_t*)ackMsg, strlen(ackMsg));
    Serial.printf("ACK|SENT|ID=%d\n", msgId);
    delay(200);
    lora.startReceive();
    forwardMessage(type, origin, content, hops, msgId);
  } else if (strcmp(type, "ACK") == 0) {
    int ackId = atoi(content);
    if (ackId == currentMsgId) {
      Serial.printf("ACK|RECV|ID=%d\n", ackId);
      awaitingAck = false;
    }
  }
}

// ==== AT Command Handling ====
void processATCommand(String command) {
  if (command == "AT") {
    Serial.println("OK");
  } else if (command.startsWith("AT+MSG=")) {
    String message = command.substring(8);
    sendMessage(deviceName, message.c_str());
  } else if (command.startsWith("AT+NAME=")) {
    String newName = command.substring(8);
    if (newName.length() < sizeof(deviceName)) {
      newName.toCharArray(deviceName, sizeof(deviceName));
      saveSettings();
      Serial.printf("LOG|NAME_UPDATED|%s\n", deviceName);
      Serial.println("LOG|REBOOTING");
      delay(500);
      ESP.restart();
    } else {
      Serial.println("ERR|NAME_TOO_LONG");
    }
  } else {
    Serial.println("ERR|UNKNOWN_COMMAND");
  }
}

// ==== Send Once, Then Wait for ACK ====
void sendMessage(const char* from, const char* message) {
  currentMsgId++;
  snprintf(pendingMessage, sizeof(pendingMessage), "MSG|%s|%s|%d", from, message, currentMsgId);

  int state = lora.transmit((uint8_t*)pendingMessage, strlen(pendingMessage));
  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("LOG|MSG_SENT|%s\n", pendingMessage);
    awaitingAck = true;
    ackWaitStart = millis();
  } else {
    Serial.printf("ERR|TX_FAIL|Code=%d\n", state);
  }

  lora.startReceive();  // go back to listening
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  while (!Serial);

  loadSettings();
  Serial.printf("LOG|BOOT|Device=%s|Channel=%.1f\n", deviceName, channels[currentChannel - 1]);
  setupLoRa();
}

// ==== Main Loop ====
void loop() {
  // Passive RX (blocking receive)
  uint8_t buf[64];
  int state = lora.receive(buf, sizeof(buf));

  if (state == RADIOLIB_ERR_NONE) {
    parseReceivedMessage((char*)buf);
    lora.startReceive();
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.printf("ERR|RECV_FAIL|Code=%d\n", state);
    lora.startReceive();
  }

  // Wait only once for ACK after sending
  if (awaitingAck && millis() - ackWaitStart > ackTimeout) {
    Serial.printf("ACK|TIMEOUT|ID=%d\n", currentMsgId);
    awaitingAck = false;
  }

  // Handle serial AT commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    processATCommand(command);
  }
}
