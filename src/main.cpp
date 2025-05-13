#include <RadioLib.h>
#include <EEPROM.h>

#define EEPROM_SIZE 128

// EEPROM Offsets
#define ADDR_MAGIC        0
#define ADDR_DEVICE_NAME  4
#define ADDR_CHANNEL      36

#define EEPROM_MAGIC      0x42

// LoRa parameters
#define BANDWIDTH 500
#define SPREADING_FACTOR 7
#define CODING_RATE 5
#define PREAMBLE_LENGTH 8
#define SYNC_WORD 0x12
#define TX_POWER 14

// LoRa module for Heltec V3 (SX1262)
SX1262 lora = new Module(8, 14, 12, 13);

// Channel list (902 MHz band)
float channels[] = {902.0, 904.0, 908.0, 910.0, 912.0, 914.0, 915.0};
const int numChannels = sizeof(channels) / sizeof(channels[0]);

// Stored settings
char deviceName[32] = "NinaMesh1";
int currentChannel = 1;

// Save settings to EEPROM
void saveSettings() {
  EEPROM.write(ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.put(ADDR_DEVICE_NAME, deviceName);
  EEPROM.write(ADDR_CHANNEL, currentChannel);
  EEPROM.commit();
}

// Initialize LoRa module
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

// Load settings from EEPROM
void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(ADDR_MAGIC) != EEPROM_MAGIC) {
    strcpy(deviceName, "Device1");
    currentChannel = 1;
    saveSettings();
    return;
  }
  EEPROM.get(ADDR_DEVICE_NAME, deviceName);
  currentChannel = EEPROM.read(ADDR_CHANNEL);
  if (currentChannel < 1 || currentChannel > numChannels)
    currentChannel = 1;
}

/**
 * @brief Initializes the system setup.
 * 
 * This function performs the following tasks:
 * 1. Initializes the serial communication at a baud rate of 115200.
 * 2. Waits for the serial connection to be established.
 * 3. (Commented out) Contains logic for a handshake mechanism with the system:
 *    - Sends a "WAITING_FOR_CONNECTION" message.
 *    - Waits for up to 30 seconds for a "HELLO_PC" message from the system.
 *    - Responds with "HELLO_DEVICE" upon receiving the handshake message.
 * 4. Loads device settings using the `loadSettings()` function.
 * 5. Logs the initialization of the LoRa module, including the device name and current channel.
 * 6. Sets up the LoRa module using the `setupLoRa()` function.
 */
void setup() {
  Serial.begin(115200);

  while (!Serial); // Wait for serial connection

  // Wait for the handshake from the system.
  /*Serial.println("WAITING_FOR_CONNECTION");
  unsigned long start = millis();
  while (millis() - start < 30000) { // Second 30 second timeout
    if (Serial.available()) {
      String line = Serial.readStringUntil('\n');
      line.trim();
      if (line == "HELLO_PC") {
        Serial.println("HELLO_DEVICE");
        break;
      }
    }
  }*/

  loadSettings();

  Serial.print("LoRa Init (");
  Serial.print(deviceName);
  Serial.print(", Channel ");
  Serial.print(currentChannel);
  Serial.println(")");

  setupLoRa();
}

void sendMessage(const char* deviceName, const char* message) {
  char formattedMessage[128];
  snprintf(formattedMessage, sizeof(formattedMessage), "MSG|%s|%s|1", deviceName, message);

  int state = lora.transmit((uint8_t*)formattedMessage, strlen(formattedMessage));
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Message sent successfully");
  } else {
    Serial.print("Transmit failed: ");
    Serial.println(state);
  }
}

/**
 * Process AT commands.
 */
void processATCommand(String command) {
  if (command == "AT") {
    Serial.println("OK");
  } else if (command.startsWith("AT+MSG=")) {
    String message = command.substring(8);
    sendMessage(deviceName, message.c_str());
  } else {
    Serial.println("ERROR");
  }
}

void parseReceivedMessage(const char* message) {
  // Example: MSG|DeviceName|Hello World|1
  // Example: GPS|DeviceName|LAT:12.3334,LNG:-18.3434|1
  // The first part is the type of message, the second part is the device name, and the third part is the content and the last is the number of hops.
  char type[16], name[32], content[128];
  int hops;
  sscanf(message, "%15[^|]|%31[^|]|%127[^|]|%d", type, name, content, &hops);
  Serial.print("Type: ");
  Serial.println(type);
  Serial.print("Name: ");
  Serial.println(name);
  Serial.print("Content: ");
  Serial.println(content);
  Serial.print("Hops: ");
  Serial.println(hops);
}

/**
 * Main loop.
 */
void loop() {

  // Handle incoming messages.
  uint8_t buf[64];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println((char*)buf);
    parseReceivedMessage((char*)buf);
    Serial.println("--------------------");
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("Receive failed: ");
    Serial.println(state);
  }

  // Handle incoming AT commands.
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    processATCommand(command);
  }
}
