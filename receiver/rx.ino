/******************************************************************************
 * Project      : J4G
 * File         : rx.ino
 * Description  : Receives wireless data and triggers outputs based on configuration
 *
 * Hardware     : any ESP32-C3 board (e.g. XIAO ESP32C3 or ESP32-C3 Dev Board) 
 *
 * Author       : Nelectra s.r.o.
 * Contact      : info@nelectra.eu
 *
 * Version      : v1.0.0
 * Date         : 2026-04-20
 *
 * ---------------------------------------------------------------------------
 * Purpose:
 * This software is provided as a reference implementation for the associated
 * hardware platform. It is intended for:
 *  - Hardware testing and validation
 *  - Proof of concept demonstrations
 *  - Customer guidance and development support
 *
 * Customers are expected to develop their own application software based on
 * their specific requirements.
 *
 * ---------------------------------------------------------------------------
 * License:
 * MIT
 *
 * ---------------------------------------------------------------------------
 * Change Log:
 *  2026-04-02  PSe  - Initial version
 *  2026-04-18  PSe  - Multi-Sender, Message Types, Print Debug Information
 *
 *****************************************************************************/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ===== CONFIG =====
#define CHANNEL 1  // ESP-NOW channel must match sender

// ===== MULTI-SENDER TABLE =====
#define MAX_SENDERS 50

typedef struct {
  uint8_t mac[6];

  uint32_t lastSeen;
  uint32_t lastCounter;

  uint16_t nextSleep;
  uint8_t setting1;
  uint8_t setting2;

  bool active;
} sender_t;

sender_t senders[MAX_SENDERS];
uint8_t lastMac[6];

// ===== DATA STRUCTS =====
typedef struct {
  uint32_t counter;
  uint16_t vscap_instant;
  uint16_t vscap_avg;
  uint16_t avg_samples;
  uint32_t lastAwakeTime;
  uint8_t wakeupReason;
  uint32_t vscap_sum;
  uint32_t timestamp;
  // NEW:
  float temperature;
  float humidity;
  uint8_t dhtStatus;
  uint8_t execDone;
  uint32_t execTimestamp;
} message_t;

typedef enum : uint8_t {
    MSG_SYNC,
    MSG_CONTROL
} msg_type_t;

typedef struct {
  uint8_t setting1;
  uint8_t setting2;
  uint16_t nextSleep;
  uint32_t timestamp;   // master timestamp
  msg_type_t msgType;
} settings_t;

// ===== INSTANCES =====
message_t msgReceived;

settings_t settingsToSend = {
  .setting1 = 42, //example
  .setting2 = 77, //example
  .nextSleep = 0,
  .timestamp = 0,
  .msgType = MSG_CONTROL
};

// ===== FLAGS =====
volatile bool msgReceivedFlag = false;


// ===== MULTI-SENDER HELPER FUNCTIONS =====
int findSender(const uint8_t *mac) {
  for (int i = 0; i < MAX_SENDERS; i++) {
    if (senders[i].active && memcmp(senders[i].mac, mac, 6) == 0) {
      return i;
    }
  }
  return -1;
}

int addSender(const uint8_t *mac) {
  for (int i = 0; i < MAX_SENDERS; i++) {
    if (!senders[i].active) {
      memcpy(senders[i].mac, mac, 6);
      senders[i].active = true;

      // default values
      senders[i].setting1 = 42;
      senders[i].setting2 = 77;
      senders[i].nextSleep = 0;

      return i;
    }
  }
  return -1;
}

void printMac(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (i > 0) Serial.print(":");
    Serial.printf("%02X", mac[i]);
  }
}

// ===== CALLBACK =====
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {

  if (len == sizeof(message_t)) {
    memcpy(&msgReceived, data, sizeof(message_t));
    msgReceivedFlag = true;

// ===== FIND OR CREATE SENDER =====
    int idx = findSender(mac);
    if (idx < 0) {
      idx = addSender(mac);
    }
    if (idx < 0) return; // no space

    // ===== UPDATE STATE =====
    uint32_t now = millis();

    //senders[idx].lastSeen = millis();
    senders[idx].lastCounter = msgReceived.counter;

    // ===== SCHEDULING / PRIORITIZATION EXAMPLES =====

    // Example 1: low voltage → longer sleep
    if (msgReceived.vscap_instant < 100) {
      senders[idx].nextSleep = 30;
    }

    // Example 2: too frequent messages → slow down
    if (now - senders[idx].lastSeen < 2000) {
      senders[idx].nextSleep = 10;
    }

    senders[idx].lastSeen = now;

    // ===== PREPARE INDIVIDUAL RESPONSE =====
    settingsToSend.setting1 = senders[idx].setting1;
    settingsToSend.setting2 = senders[idx].setting2;
    settingsToSend.nextSleep = senders[idx].nextSleep;

    // cycle 3 SYNC messages and 1 CONTROL message
    if ((msgReceived.counter % 4) == 0) {
      settingsToSend.msgType = MSG_CONTROL;
    } else {
      settingsToSend.msgType = MSG_SYNC;
    }

    settingsToSend.timestamp = millis();


    // --- TEMPORARY PEER ADD ---
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = CHANNEL;
    peer.encrypt = false;

    memcpy(lastMac, mac, 6);

    if (esp_now_add_peer(&peer) != ESP_OK) return;

    // --- SEND RESPONSE ---
    esp_now_send(mac, (uint8_t*)&settingsToSend, sizeof(settingsToSend));

    // --- REMOVE PEER IMMEDIATELY ---
    esp_now_del_peer(mac);
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true);
  }

  esp_now_register_recv_cb(onReceive);
}

// ===== LOOP =====
void loop() {


  if (msgReceivedFlag) {
    msgReceivedFlag = false;

    

    Serial.println("==== Received from sender ====");
    Serial.print("Sender MAC: ");
    printMac(lastMac);
    Serial.println();

    Serial.printf("Counter: %u\n", msgReceived.counter);
    Serial.printf("VSCAP instant: %u\n", msgReceived.vscap_instant);
    Serial.printf("VSCAP avg: %u\n", msgReceived.vscap_avg);
    Serial.printf("Avg samples: %u\n", msgReceived.avg_samples);
    Serial.printf("Last awake ms: %u\n", msgReceived.lastAwakeTime);
    Serial.printf("Wakeup reason: %u\n", msgReceived.wakeupReason);
    Serial.printf("VSCAP sum: %u\n", msgReceived.vscap_sum);

    // Timestamp debug
    // Print timestamp from sender and local timestamp
    Serial.printf("Sender timestamp: %u ms\n", msgReceived.timestamp);
    Serial.printf("Local timestamp (settingsToSend): %u ms\n", settingsToSend.timestamp);

    int32_t offset = (int32_t)settingsToSend.timestamp - (int32_t)msgReceived.timestamp;
    Serial.print("Clock offset (ms): ");
    Serial.println(offset);
    Serial.printf("Assigned msgType: %s\n",
      (settingsToSend.msgType == MSG_CONTROL) ? "CONTROL" : "SYNC");
    Serial.println();

    if (msgReceived.execDone) {
      Serial.printf("Temp: %.2f\n", msgReceived.temperature);
      Serial.printf("Humidity: %.2f\n", msgReceived.humidity);
      Serial.printf("DHT status: %u\n", msgReceived.dhtStatus);
      Serial.printf("Exec timestamp: %u\n", msgReceived.execTimestamp);
      Serial.println();
    } 
  }

  //Cleanup inactive senders (optional)
  for (int i = 0; i < MAX_SENDERS; i++) {
    if (senders[i].active && (millis() - senders[i].lastSeen > 360000000)) {
      senders[i].active = false; // remove after 100 hour inactivity
    }
}


}
