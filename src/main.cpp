#include <RadioLib.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include <vector>
#include <deque>
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

#define MAX_RETRIES      0
#define RETRY_INTERVAL   6000
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

#define SCREEN_WIDTH 128 // OLED width
#define SCREEN_HEIGHT 64 // OLED height

#define OLED_POWER_PIN 36
#define RST_OLED_PIN 21
#define SCL_OLED_PIN 18
#define SDA_OLED_PIN 17

// Fragment definition must come before usage
struct Fragment {
  uint8_t data[AES_BLOCK_LEN];
  int retries;
  unsigned long timestamp;
  bool acked;
};

struct IncomingText {
  int total;
  unsigned long start;
  std::map<uint8_t, String> parts;
  std::vector<bool> received;
};

SX1262 lora = new Module(8, 14, 12, 13);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, RST_OLED_PIN);

char deviceName[16];
AES128 aes;
uint8_t aes_key[16] = {0x60,0x3D,0xEB,0x10,0x15,0xCA,0x71,0xBE,0x2B,0x73,0xAE,0xF0,0x85,0x7D,0x77,0x81};

// Outgoing queue state
std::deque<String> msgQueue;
bool sending = false;
String currentMsgId;
std::vector<Fragment> currentFrags;
unsigned long verifyTimestamp = 0;

std::map<String, IncomingText> incoming;
std::map<String, unsigned long> recentMsgs;

String generateMsgID() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)esp_random());
  return String(buf);
}

void encryptFragment(uint8_t* input) { aes.encryptBlock(input, input); }
void decryptFragment(uint8_t* input) { aes.decryptBlock(input, input); }
bool isRecentMessage(const String& msgId) {
  unsigned long now = millis();
  for(auto it=recentMsgs.begin(); it!=recentMsgs.end();) {
    if(now - it->second > BROADCAST_MEMORY_TIME) it = recentMsgs.erase(it);
    else ++it;
  }
  if(recentMsgs.count(msgId)) return true;
  recentMsgs[msgId] = now; return false;
}

// --- Sender FSM Functions ---
void sendVerifyRequest(const String& msgId) {
  uint8_t v[AES_BLOCK_LEN] = {0};
  v[0] = TYPE_VERIFY_REQUEST;
  v[1] = highByte((uint16_t)strtoul(msgId.c_str(), NULL, 16));
  v[2] = lowByte((uint16_t)strtoul(msgId.c_str(), NULL, 16));
  encryptFragment(v);
  lora.transmit(v, AES_BLOCK_LEN);
  Serial.printf("VERIFY|SEND|%s\n", msgId.c_str());
  delay(50);
  lora.startReceive();
  verifyTimestamp = millis();
}

void startTransmission(const String& msg) {
  bool highPriority = msg.startsWith("!");
  String payload = highPriority ? msg.substring(1) : msg;
  payload = String(deviceName) + "|" + payload;
  currentMsgId = generateMsgID();
  int total = (payload.length() + FRAG_DATA_LEN - 1) / FRAG_DATA_LEN;
  currentFrags.clear();
  for(int i=0;i<total;i++){
    uint8_t block[AES_BLOCK_LEN] = {0};
    block[0] = highPriority?PRIORITY_HIGH:PRIORITY_NORMAL;
    block[1] = highByte((uint16_t)strtoul(currentMsgId.c_str(),NULL,16));
    block[2] = lowByte((uint16_t)strtoul(currentMsgId.c_str(),NULL,16));
    block[3] = i;
    block[4] = total;
    String chunk = payload.substring(i*FRAG_DATA_LEN, min((i+1)*FRAG_DATA_LEN, (int)payload.length()));
    memcpy(&block[5], chunk.c_str(), chunk.length());
    encryptFragment(block);
    Fragment f;
    memcpy(f.data, block, AES_BLOCK_LEN);
    f.retries = 0;
    f.timestamp = millis();
    f.acked = false;
    currentFrags.push_back(f);
    for(int r=0;r<2;r++){
      if(lora.transmit(block,AES_BLOCK_LEN)==RADIOLIB_ERR_NONE)
        Serial.printf("SEND|FRAG|%s|%d/%d|TRY=%d\n", currentMsgId.c_str(), i+1, total, r+1);
      delay(50);
    }
  }
  sending = true;
  sendVerifyRequest(currentMsgId);
}

void maybeStartNext() {
  if(!sending && !msgQueue.empty()){
    String m = msgQueue.front(); msgQueue.pop_front();
    startTransmission(m);
  }
}

void handleSenderTimeout() {
  if(sending && millis() - verifyTimestamp >= REQ_TIMEOUT) {
    Serial.printf("VERIFY|TIMEOUT|%s\n", currentMsgId.c_str());
    sending = false;
  }
}

// --- Receiver Handlers ---
void processFragment(uint8_t* buf) {
  decryptFragment(buf);
  if((buf[0] & 0x0F) != TYPE_TEXT_FRAGMENT) return;
  String mid = String((buf[1] << 8) | buf[2], HEX);
  uint8_t seq = buf[3], tot = buf[4];
  auto& it = incoming[mid];
  if(it.parts.empty()){
    it.total = tot;
    it.start = millis();
    it.received.assign(tot, false);
  }
  String part;
  for(int i=5; i<AES_BLOCK_LEN; i++){
    if(buf[i] == 0) break;
    part += (char)buf[i];
  }
  it.parts[seq] = part;
  it.received[seq] = true;
  Serial.printf("RECV|FRAG|%s|%d/%d\n", mid.c_str(), seq+1, tot);
  uint8_t ack[AES_BLOCK_LEN] = {0};
  ack[0] = TYPE_ACK_FRAGMENT;
  ack[1] = buf[1];
  ack[2] = buf[2];
  ack[3] = seq;
  encryptFragment(ack);
  lora.transmit(ack, AES_BLOCK_LEN);
  delay(50);
  lora.startReceive();
  bool done = true;
  for(bool got : it.received) if(!got){ done = false; break; }
  if(!done) return;
  if(isRecentMessage(mid)){
    Serial.printf("SUPPRESS|DUPLICATE|%s\n", mid.c_str());
    incoming.erase(mid);
    return;
  }
  String full;
  for(int i=0; i<it.total; i++) full += it.parts[i];
  int p = full.indexOf('|');
  String sender = full.substring(0, p);
  String message = full.substring(p+1);
  Serial.printf("RECV|%s|%s|%s\n", sender.c_str(), message.c_str(), mid.c_str());
  std::vector<uint8_t> missing;
  for(int i=0; i<it.total; i++) if(!it.received[i]) missing.push_back(i);
  if(missing.empty()){
    uint8_t vr[AES_BLOCK_LEN] = {0};
    vr[0] = TYPE_VERIFY_REPLY;
    vr[1] = buf[1];
    vr[2] = buf[2];
    memcpy(&vr[3], "OK", 2);
    encryptFragment(vr);
    lora.transmit(vr, AES_BLOCK_LEN);
    Serial.printf("VERIFY|REPLY|OK|%s\n", mid.c_str());
  } else {
    for(uint8_t s : missing){
      uint8_t rr[AES_BLOCK_LEN] = {0};
      rr[0] = TYPE_REFRAGMENT_REQ;
      rr[1] = buf[1];
      rr[2] = buf[2];
      rr[3] = s;
      encryptFragment(rr);
      lora.transmit(rr, AES_BLOCK_LEN);
      Serial.printf("REFRAG|REQ|%s|SEQ=%d\n", mid.c_str(), s);
      delay(50);
    }
  }
  incoming.erase(mid);
}

void processAck(uint8_t* buf) {
  decryptFragment(buf);
  if(buf[0] != TYPE_ACK_FRAGMENT) return;
  String mid = String((buf[1] << 8) | buf[2], HEX);
  uint8_t seq = buf[3];
  if(mid == currentMsgId && seq < currentFrags.size()){
    currentFrags[seq].acked = true;
    Serial.printf("ACK|RECV|%s|SEQ=%d\n", mid.c_str(), seq);
  }
}

void processRefragReq(uint8_t* buf) {
  decryptFragment(buf);
  if(buf[0] != TYPE_REFRAGMENT_REQ) return;
  String mid = String((buf[1] << 8) | buf[2], HEX);
  uint8_t seq = buf[3];
  if(mid != currentMsgId || seq >= currentFrags.size()) return;
  lora.transmit(currentFrags[seq].data, AES_BLOCK_LEN);
  Serial.printf("REFRAG|SEND|%s|SEQ=%d\n", mid.c_str(), seq);
  delay(50);
  sendVerifyRequest(mid);
}

void processVerifyReply(uint8_t* buf) {
  decryptFragment(buf);
  if(buf[0] != TYPE_VERIFY_REPLY) return;
  String mid = String((buf[1] << 8) | buf[2], HEX);
  if(mid != currentMsgId) return;
  if(buf[3]=='O' && buf[4]=='K'){
    Serial.printf("CONFIRM|OK|%s\n", mid.c_str());
    sending = false;
  }
}

void retryFragments() {
  // no-op: handshake covers retries
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(OLED_POWER_PIN, OUTPUT);
  digitalWrite(OLED_POWER_PIN, LOW);
  delay(100);
  Wire.begin(SDA_OLED_PIN, SCL_OLED_PIN, 500000);
  delay(100);
  digitalWrite(LED_PIN, HIGH);
  Serial.begin(115200); while(!Serial);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)){
    Serial.println("INIT|OLED_FAILED"); for(;;);
  }
  display.clearDisplay();
  int cx = SCREEN_WIDTH/2;
  int cy = SCREEN_HEIGHT/2 - 8;
  display.fillCircle(cx, cy, 12, SSD1306_WHITE);
  display.fillCircle(cx, cy+3, 12, SSD1306_BLACK);
  display.fillCircle(cx, cy+6, 7, SSD1306_WHITE);
  display.fillCircle(cx-3, cy+5, 1, SSD1306_BLACK);
  display.fillCircle(cx+3, cy+5, 1, SSD1306_BLACK);
  display.drawLine(cx+8, cy-6, cx+14, cy+12, SSD1306_WHITE);
  display.drawLine(cx+12, cy-10, cx+18, cy-2, SSD1306_WHITE);
  String title = "Reaper Mesh - v" + String(REAPER_VERSION);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w)/2, SCREEN_HEIGHT - h - 2);
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.print(title); display.display();
  aes.setKey(aes_key, sizeof(aes_key));
  uint16_t cid = (uint16_t)((ESP.getEfuseMac() >> 32) & 0xFFFF);
  snprintf(deviceName, sizeof(deviceName), "%04X", cid);
  int st = lora.begin(FREQUENCY);
  if(st != RADIOLIB_ERR_NONE){
    Serial.printf("ERR|INIT_FAIL|%d\n", st);
    while(1);
  }
  lora.setBandwidth(BANDWIDTH);
  lora.setSpreadingFactor(SPREADING_FACTOR);
  lora.setCodingRate(CODING_RATE);
  lora.setPreambleLength(PREAMBLE_LENGTH);
  lora.setSyncWord(SYNC_WORD);
  lora.setOutputPower(TX_POWER);
  lora.setCRC(true);
  Serial.printf("INIT|LoRa Ready as %s\n", deviceName);
  delay(1000 + random(0, 1500));
  lora.startReceive();
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  if(Serial.available()){
    String in = Serial.readStringUntil('\n'); in.trim();
    if(in.startsWith("AT+MSG=")){ msgQueue.push_back(in.substring(7)); Serial.println("QUEUE|MSG"); }
    else if(in.startsWith("AT+GPS=")){ msgQueue.push_back("GPS:" + in.substring(7)); Serial.println("QUEUE|GPS"); }
    else Serial.println("ERR|UNKNOWN_CMD");
  }
  uint8_t buf[128]; int st = lora.receive(buf, sizeof(buf));
  if(st == RADIOLIB_ERR_NONE){
    uint8_t t = buf[0] & 0x0F;
    if(t == TYPE_TEXT_FRAGMENT) processFragment(buf);
    else if(t == TYPE_ACK_FRAGMENT) processAck(buf);
    else if(t == TYPE_REFRAGMENT_REQ) processRefragReq(buf);
    else if(t == TYPE_VERIFY_REPLY) processVerifyReply(buf);
    lora.startReceive();
  } else if(st != RADIOLIB_ERR_RX_TIMEOUT){
    Serial.printf("ERR|RX_FAIL|%d\n", st);
    lora.startReceive();
  }
  handleSenderTimeout();
  maybeStartNext();
}
