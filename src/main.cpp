#include <RadioLib.h>
#include <EEPROM.h>
#include <vector>

#define EEPROM_SIZE 128
#define MAX_HOPS 3
#define ROUTE_TABLE_SIZE 20

// EEPROM Offsets
#define ADDR_MAGIC        0
#define ADDR_DEVICE_NAME  4
#define ADDR_CHANNEL      36
#define EEPROM_MAGIC      0x42

// LoRa Parameters
#define BANDWIDTH 500
#define SPREADING_FACTOR 7
#define CODING_RATE 5
#define PREAMBLE_LENGTH 8
#define SYNC_WORD 0x12
#define TX_POWER 14

SX1262 lora = new Module(8, 14, 12, 13); // Heltec V3 pins

// Channel list
float channels[] = {902.0, 904.0, 908.0, 910.0, 912.0, 914.0, 915.0};
const int numChannels = sizeof(channels) / sizeof(channels[0]);

char deviceName[32] = "NinaMesh1";
int currentChannel = 1;

unsigned long lastSendTime = 0;
const unsigned long ackTimeout = 3000;
const int MAX_RETRIES = 3;

bool awaitingAck = false;
int retryCount = 0;
int currentMsgId = 0;
char pendingMessage[128];

std::vector<String> routeTable;

// === Forward Declarations ===
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
    Serial.println("LoRa RX ready");
  } else {
    Serial.print("LoRa init failed: ");
    Serial.println(state);
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
    Serial.print("Forwarded: ");
    Serial.println(forward);
    addToRouteTable(key);
  } else {
    Serial.print("Forward failed: ");
    Serial.println(state);
  }

  delay(200);
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
    Serial.println("Duplicate ignored.");
    return;
  }

  Serial.println("Message Received:");
  Serial.printf("Type: %s\nFrom: %s\nContent: %s\nHops: %d\nMsgID: %d\n", type, origin, content, hops, msgId);
  Serial.println("--------------------");

  addToRouteTable(key);

  if (strcmp(type, "MSG") == 0 && strcmp(origin, deviceName) != 0) {
    // Send ACK
    char ackMsg[64];
    snprintf(ackMsg, sizeof(ackMsg), "ACK|%s|%d|1", deviceName, msgId);
    lora.transmit((uint8_t*)ackMsg, strlen(ackMsg));
    delay(200);

    forwardMessage(type, origin, content, hops, msgId);
  } else if (strcmp(type, "ACK") == 0) {
    int ackId = atoi(content);
    if (ackId == currentMsgId) {
      Serial.println("ACK received");
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
      Serial.print("Device name updated to: ");
      Serial.println(deviceName);
      Serial.println("Rebooting...");
      delay(500);
      ESP.restart();
    } else {
      Serial.println("Name too long.");
    }

  } else {
    Serial.println("ERROR");
  }
}

// ==== Send with ACK Wait ====

void sendMessage(const char* deviceName, const char* message) {
  currentMsgId++;
  snprintf(pendingMessage, sizeof(pendingMessage), "MSG|%s|%s|%d", deviceName, message, currentMsgId);

  int state = lora.transmit((uint8_t*)pendingMessage, strlen(pendingMessage));
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("Message sent: ");
    Serial.println(pendingMessage);

    unsigned long startWait = millis();
    awaitingAck = true;

    while (millis() - startWait < ackTimeout) {
      uint8_t buf[64];
      int rxState = lora.receive(buf, sizeof(buf));
      if (rxState == RADIOLIB_ERR_NONE) {
        Serial.print("RX after TX: ");
        Serial.println((char*)buf);
        parseReceivedMessage((char*)buf);
        if (!awaitingAck) break;  // ACK received
      }
      delay(10);
    }

    if (awaitingAck) {
      Serial.println("⏱ACK not received. Will retry...");
      retryCount = 0;
      lastSendTime = millis();
    }

  } else {
    Serial.print("Transmit failed: ");
    Serial.println(state);
  }
}

// ==== Arduino Setup ====

void setup() {
  Serial.begin(115200);
  while (!Serial);

  loadSettings();

  Serial.print("Device: ");
  Serial.println(deviceName);
  Serial.print("Channel: ");
  Serial.println(currentChannel);

  setupLoRa();
}

// ==== Main Loop ====

void loop() {
  // Handle incoming messages
  uint8_t buf[64];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    parseReceivedMessage((char*)buf);
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("Receive failed: ");
    Serial.println(state);
  }

  // Retry if awaiting ACK
  if (awaitingAck && millis() - lastSendTime > ackTimeout) {
    retryCount++;
    if (retryCount > MAX_RETRIES) {
      Serial.println("No ACK after retries. Giving up.");
      awaitingAck = false;
    } else {
      Serial.printf("Retrying (%d/%d)...\n", retryCount, MAX_RETRIES);
      int txState = lora.transmit((uint8_t*)pendingMessage, strlen(pendingMessage));
      if (txState == RADIOLIB_ERR_NONE) {
        Serial.println("Resent:");
        Serial.println(pendingMessage);
        lastSendTime = millis();
      } else {
        Serial.print("Retry failed: ");
        Serial.println(txState);
      }
    }
  }

  // AT Command via Serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    processATCommand(command);
  }

  delay(10);
}
