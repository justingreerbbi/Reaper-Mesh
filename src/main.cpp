/**
 * @brief Sends an encrypted text message over LoRa, fragmenting it as needed.
 * 
 * This function prepares a message for transmission by:
 * - Checking for high priority (if the message starts with '!').
 * - Prefixing the message with the device name.
 * - Generating a unique message ID.
 * - Splitting the message into fragments, each fitting within the AES block size.
 * - Encrypting each fragment using AES-128.
 * - Transmitting each fragment twice for reliability.
 * - Storing the fragments in the outgoing map for possible retries.
 * 
 * @param msg The plaintext message to send. If it starts with '!', it is sent as high priority.
 */
#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include <vector>
#include <map>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define REAPER_VERSION "1.77.6"

#define FREQUENCY        915.0
#define BANDWIDTH        500.0
#define SPREADING_FACTOR 12
#define CODING_RATE      8
#define PREAMBLE_LENGTH  20
#define SYNC_WORD        0xF3
#define TX_POWER         22

#define MAX_RETRIES      1
#define RETRY_INTERVAL   3000
#define FRAG_DATA_LEN    11
#define AES_BLOCK_LEN    16

#define TYPE_TEXT_FRAGMENT  0x03
#define TYPE_ACK_FRAGMENT   0x04
#define TYPE_REFRAGMENT_REQ 0x05
#define TYPE_VERIFY_REQUEST 0x06
#define TYPE_VERIFY_REPLY   0x07
#define TYPE_ACK_CONFIRM    0x08

#define PRIORITY_NORMAL 0x03
#define PRIORITY_HIGH   0x13

#define BROADCAST_MEMORY_TIME 30000
#define REQ_TIMEOUT 2000

#define LED_PIN 35

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_POWER_PIN 36
#define RST_OLED_PIN 21
#define SCL_OLED_PIN 18
#define SDA_OLED_PIN 17

SX1262 lora = new Module(8, 14, 12, 13);  // Heltec V3.2 pins
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, RST_OLED_PIN);

// Flag variables
bool isReceiving_flag = false;
bool isSending_flag = false;
bool retry_flag = false;

// Device Name Holder
char deviceName[16];

// AES encryption/decryption
AES128 aes;
uint8_t aes_key[16] = {
  0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
  0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81
};

struct Fragment {
  uint8_t data[AES_BLOCK_LEN];
  int retries;
  unsigned long timestamp;
  bool acked = false;
};

struct IncomingText {
  int total;
  unsigned long start;
  std::map<uint8_t, String> parts;
  std::vector<bool> received;
};

std::map<String, std::vector<Fragment>> outgoing;
std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

void processFragment(uint8_t* buf);
void processAck(uint8_t* buf);

/**
 * @brief Generates a unique message ID.
 * 
 * This function generates a unique message ID by using the esp_random() function
 * to create a random number and formatting it as a hexadecimal string.
 * The ID is 4 hexadecimal digits long.
 * 
 * @return A string representing the unique message ID.
 *         The ID is formatted as a 4-digit hexadecimal number.
 */
String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

/**
 * @brief Checks if a message ID is recent and updates the recent messages map.
 * 
 * This function checks if a message ID has been seen recently (within a defined time limit).
 * If the message ID is found, it returns true. Otherwise, it adds the message ID to the recent messages map
 * and returns false.
 * 
 * @param msgId The message ID to check.
 * @return true if the message ID is recent, false otherwise.
 */
bool isRecentMessage(String msgId) {
  unsigned long now = millis();
  for (auto it = recentMsgs.begin(); it != recentMsgs.end(); ) {
    if (now - it->second > BROADCAST_MEMORY_TIME) {
      it = recentMsgs.erase(it);
    } else {
      ++it;
    }
  }
  if (recentMsgs.find(msgId) != recentMsgs.end()) return true;
  recentMsgs[msgId] = now;
  return false;
}

void encryptFragment(uint8_t* input) {
  aes.encryptBlock(input, input);
}

void decryptFragment(uint8_t* input) {
  aes.decryptBlock(input, input);
}

/**
 * @brief Sends an encrypted text message over LoRa, fragmenting it as needed.
 * 
 * This function prepares a message for transmission by:
 * - Checking for high priority (if the message starts with '!').
 * - Prefixing the message with the device name.
 * - Generating a unique message ID.
 * - Splitting the message into fragments, each fitting within the AES block size.
 * - Encrypting each fragment using AES-128.
 * - Transmitting each fragment twice for reliability.
 * - Storing the fragments in the outgoing map for possible retries.
 * 
 * @param msg The plaintext message to send. If it starts with '!', it is sent as high priority.
 */
void sendEncryptedText(String msg) {
  bool highPriority = false;
  if (msg.startsWith("!")) {
    highPriority = true;
    msg.remove(0, 1);
  }

  msg = String(deviceName) + "|" + msg;
  std::vector<Fragment> frags;
  String msgId = generateMsgID();
  int total = (msg.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;

  delay(random(100, 400));  // initial jitter

  for (int i = 0; i < total; i++) {
    uint8_t block[AES_BLOCK_LEN] = {0};
    block[0] = highPriority ? PRIORITY_HIGH : PRIORITY_NORMAL;
    block[1] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) >> 8);
    block[2] = (uint8_t)(strtoul(msgId.c_str(), NULL, 16) & 0xFF);
    block[3] = (uint8_t)i;
    block[4] = (uint8_t)total;

    String chunk = msg.substring(i * FRAG_DATA_LEN, min((i + 1) * FRAG_DATA_LEN, (int)msg.length()));
    memcpy(&block[5], chunk.c_str(), chunk.length());
    encryptFragment(block);

    Fragment frag;
    memcpy(frag.data, block, AES_BLOCK_LEN);
    frag.retries = 0;
    frag.timestamp = millis();
    frags.push_back(frag);

    // @TODO: Look into sending two fragments. This is causing some flooding concerns.
    for (int r = 0; r < 1; r++) {
      int state = lora.transmit(block, AES_BLOCK_LEN);
      if (state == RADIOLIB_ERR_NONE) {
        Serial.printf("SEND|FRAG|%s|%d/%d|TRY=%d\n", msgId.c_str(), i + 1, total, r + 1);
      } else {
        Serial.print("ERR|TX_FAIL|");
        Serial.println(state);
      }
    }
  }

  outgoing[msgId] = frags;
}

/**
 * @brief Processes incoming fragments and acknowledgments.
 * 
 * This function handles the decryption of received fragments, checks their type,
 * and processes them accordingly. It also manages the acknowledgment of received
 * fragments and the assembly of complete messages.
 */
void processFragment(uint8_t* buf) {
  decryptFragment(buf);
  uint8_t type = buf[0];

  // If the message is a text fragement
  if ((type & 0x0F) == TYPE_TEXT_FRAGMENT) {
    String msgId = String((buf[1] << 8) | buf[2], HEX);
    msgId.toUpperCase();
    uint8_t seq = buf[3];
    uint8_t total = buf[4];
    String part = "";

    for (int i = 5; i < AES_BLOCK_LEN; i++) {
      if (buf[i] == 0x00) break;
      part += (char)buf[i];
    }

    IncomingText& msg = incoming[msgId];
    if (msg.received.size() != total) {
      msg.total = total;
      msg.received.assign(total, false);
    }
    msg.parts[seq]    = part;
    msg.received[seq] = true;
    
    Serial.printf("RECV|FRAG|%s|%d/%d\n", msgId.c_str(), seq + 1, total);

    bool complete = true;
    for (int idx = 0; idx < msg.received.size(); ++idx) {
      if (!msg.received[idx]) {
        //Serial.printf("MISSING|FRAG|%d/%d\n", idx + 1, msg.received.size());
        complete = false;
        break;
      }
    }

    if (complete) {

      // Supress duplicate messages and simply remove it from the incoming map.
      // @TODO: Remove Serial.print statements. It is not really needed. It is just good for debugging.
      if (isRecentMessage(msgId)) {
        //Serial.print("SUPPRESS|DUPLICATE|");
        //Serial.println(msgId);
        incoming.erase(msgId);
        return;
      }

      // Send ACK_CONFIRM
      // @TODO: There is a bug in teh module or code that requires the transmit to be called twice for the call to actually go out.
      uint8_t ackConfirm[AES_BLOCK_LEN] = {0};
      ackConfirm[0] = TYPE_ACK_CONFIRM;
      ackConfirm[1] = buf[1];
      ackConfirm[2] = buf[2];
      ackConfirm[3] = buf[3]; // Sequence number
      encryptFragment(ackConfirm);
      lora.transmit(ackConfirm, AES_BLOCK_LEN);
      lora.transmit(ackConfirm, AES_BLOCK_LEN);
      delay(50);
      lora.startReceive();

      String fullMessage;
      for (int i = 0; i < total; i++) fullMessage += msg.parts[i];

      int sep = fullMessage.indexOf('|');
      String sender = fullMessage.substring(0, sep);
      String message = fullMessage.substring(sep + 1);

      Serial.print("RECV|"); 
      Serial.print(sender);
      Serial.print("|");
      Serial.print(message);
      Serial.print("|");
      msgId.toUpperCase();
      Serial.println(msgId);
    }

  } else if (type == TYPE_ACK_CONFIRM) {

    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    digitalWrite(LED_PIN, LOW);

    Serial.print("RECV|ACK_CONFIRM|");
    char msgIdBuf[5];
    snprintf(msgIdBuf, sizeof(msgIdBuf), "%02X%02X", buf[1], buf[2]);
    String msgId = String(msgIdBuf);
    msgId.toUpperCase();
    Serial.print(msgId);
    Serial.println();

    auto it = outgoing.find(msgId);
    if (it != outgoing.end()) {
      for (auto& frag : it->second) {
        frag.acked = true;
      }
    }
  }
}

/**
 * @brief Processes acknowledgment messages.
 * 
 * This function handles the decryption of received acknowledgment messages,
 * checks their type, and updates the status of the corresponding fragments
 * in the outgoing map.
 */
void processAck(uint8_t* buf) {
  decryptFragment(buf);
  if (buf[0] != TYPE_ACK_FRAGMENT) return;

  String msgId = String((buf[1] << 8) | buf[2], HEX);
  uint8_t seq = buf[3];

  auto it = outgoing.find(msgId);
  if (it != outgoing.end() && seq < it->second.size()) {
    it->second[seq].acked = true;
    Serial.printf("ACK|RECV|%s|SEQ=%d\n", msgId.c_str(), seq);
  }
}

/**
 * @brief Retries sending fragments that have not been acknowledged.
 * 
 * This function checks the outgoing fragments for any that have not been acknowledged
 * and attempts to resend them. It also handles the case where a fragment has
 * exceeded the maximum number of retries. If a fragment is acknowledged during
 * the retry process, it is marked as such.
 */
void retryFragments() {
  for (auto it = outgoing.begin(); it != outgoing.end(); ) {
    bool allAcked = true;
    for (auto& frag : it->second) {
      if (frag.acked || frag.retries >= MAX_RETRIES) continue;

      unsigned long start = millis();
      while (millis() - start < RETRY_INTERVAL) {
        uint8_t buf[128];
        int state = lora.receive(buf, sizeof(buf));
        if (state == RADIOLIB_ERR_NONE) {
          processFragment(buf);
          processAck(buf);
        }

        if (frag.acked) {
          Serial.print("RETRY|SKIP|ACKED|");
          for (int i = 0; i < AES_BLOCK_LEN; i++) Serial.printf("%02X", frag.data[i]);
          Serial.println();
          break;
        }
      }

      if (!frag.acked) {
        int state = lora.transmit(frag.data, AES_BLOCK_LEN);
        if (state == RADIOLIB_ERR_NONE) {
          frag.timestamp = millis();
          frag.retries++;
          Serial.print("RETRY|SEND|");
          for (int i = 0; i < AES_BLOCK_LEN; i++) Serial.printf("%02X", frag.data[i]);
          Serial.println();
        } else {
          Serial.print("RETRY|FAIL|");
          Serial.println(state);
        }

        lora.startReceive();
      }

      if (!frag.acked && frag.retries < MAX_RETRIES) allAcked = false;
    }

    if (allAcked) it = outgoing.erase(it);
    else ++it;
  }
}

/**
 * @brief Setup function
 * 
 * This function initializes the hardware components, including the LED,
 * OLED display, and LoRa module. It also sets up the AES encryption key
 * and prepares the device for communication.
 * 
 * @return void
 */
void setup() {

  // Initialize the LED
  pinMode(LED_PIN, OUTPUT);

  // Turn on the LED
  digitalWrite(LED_PIN, HIGH);

  // Initialize the OLED display
  pinMode(OLED_POWER_PIN, OUTPUT);
  digitalWrite(OLED_POWER_PIN, LOW);
  delay(100);
  
  // Initialize the I2C bus for the OLED display
  Wire.begin(SDA_OLED_PIN, SCL_OLED_PIN, 500000);
  delay(100);

  // Begin the Serial connection on baud rate 115200
  Serial.begin(115200);

  // Wait for the Serial connection to be established
  while (!Serial);

  // Check to ensure that the OLED display is connected
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("INIT|OLED_FAILED. CHECK CONNECTIONS.");
    for(;;); // Don't proceed, loop forever
  }

  // Initialize the display.
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // Draw a small "reaper" icon (skull with hood) in the center of the screen
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 2 - 8;

  // Draw hood (semicircle)
  display.fillCircle(centerX, centerY, 12, SSD1306_WHITE);
  display.fillCircle(centerX, centerY + 3, 12, SSD1306_BLACK);

  // Draw face (skull)
  display.fillCircle(centerX, centerY + 6, 7, SSD1306_WHITE);

  // Draw eyes
  display.fillCircle(centerX - 3, centerY + 5, 1, SSD1306_BLACK);
  display.fillCircle(centerX + 3, centerY + 5, 1, SSD1306_BLACK);

  // Draw mouth (simple line)
  //display.drawFastHLine(centerX - 2, centerY + 10, 5, SSD1306_BLACK);

  // Draw scythe handle
  display.drawLine(centerX + 8, centerY - 6, centerX + 14, centerY + 12, SSD1306_WHITE);

  // Draw scythe blade
  display.drawLine(centerX + 12, centerY - 10, centerX + 18, centerY - 2, SSD1306_WHITE);

  // Draw "Reaper Mesh" text under the flower
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  String title = "Reaper Mesh - v" + String(REAPER_VERSION);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int textX = (SCREEN_WIDTH - w) / 2;
  int textY = SCREEN_HEIGHT - h - 2; // 2 pixels above the bottom edge
  display.setCursor(textX, textY);
  display.print(title);
  display.display();

  // Set the AES key for encryption/decryption
  aes.setKey(aes_key, sizeof(aes_key));
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(deviceName, sizeof(deviceName), "%04X", (uint16_t)((chipId >> 32) & 0xFFFF));

  // Initialize the LoRa module on teh frequency defined above
  int state = lora.begin(FREQUENCY);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("ERR|INIT_FAIL|"); Serial.println(state);
    while (true);
  }

  // Set the LoRa parameters.
  lora.setBandwidth(BANDWIDTH);
  lora.setSpreadingFactor(SPREADING_FACTOR);
  lora.setCodingRate(CODING_RATE);
  lora.setPreambleLength(PREAMBLE_LENGTH);
  lora.setSyncWord(SYNC_WORD);
  lora.setOutputPower(TX_POWER);
  lora.setCRC(true);

  // Report back to tehe serial monitor that the device is ready.
  Serial.print("INIT|LoRa Ready as "); Serial.println(deviceName);
  delay(1000 + random(0, 1500));
  lora.startReceive();

  // Turn off the LED
  digitalWrite(LED_PIN, LOW);
}

/**
 * @brief Main loop function
 * 
 * This function continuously checks for incoming messages, processes them,
 * and handles user input from the Serial monitor. It also manages the
 * sending of encrypted text messages and the retrying of unacknowledged fragments.
 * 
 * @return void
 */
void loop() {

  // Check for incoming commands from the Serial monitor
  if (Serial.available()) {
    
    // Read the input from the Serial monitor
    String input = Serial.readStringUntil('\n');
    input.trim();

    // Handle the input command accordingly.
    if (input.startsWith("AT+MSG=")) {
      String msg = input.substring(7);
      sendEncryptedText(msg);
    } else if (input.startsWith("AT+GPS=")) {
      String coords = input.substring(7);
      sendEncryptedText("GPS:" + coords);
    } else {
      Serial.println("ERR|UNKNOWN_CMD");
    }
  } 

  // Check for incoming messages from the LoRa module
  uint8_t buf[128];
  int state = lora.receive(buf, sizeof(buf));
  if (state == RADIOLIB_ERR_NONE) {
    processFragment(buf);
    lora.startReceive();
  } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    // Handle any errors that occur during reception.
    Serial.print("ERR|RX_FAIL|");
    Serial.println(state);
    lora.startReceive();
  }

  // Handle Retrying if an message fragments that need to go out.
  retryFragments();
}
