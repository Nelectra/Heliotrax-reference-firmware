/******************************************************************************
 * Project      : J4G
 * File         : tx.cpp
 * Description  : Handles sensor data acquisition and wireless transmission to receiver node
 *
 * Hardware     : j4g1.0 PCB
 *
 * Author       : Nelectra s.r.o.
 * Contact      : info@nelectra.eu
 *
 * Version      : v7.2
 * Date         : 2026-04-18
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
 *  2026-04-18  PSe  - Multi-Sender, Message types, DHT11 sensor reading
 *
 *****************************************************************************/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <Arduino.h>  // for random()
#include "DHT.h" //DHT sensor library

// ================= CONFIG =================
#define CHANNEL 1               // fixed channel for ESP-NOW
#define ADC_PIN 4               // ADC channel for VSCAP
#define SLEEP_SHORT_SEC 5       // short sleep in seconds
#define SLEEP_LONG_SEC 600     // long sleep in seconds
#define RANDOM_SPREAD_MS 1000   // ±1000 ms random spread for short sleep time

#define MAX_TRX_TIME 80         // Max. Transmit time in ms
#define SW_FLASH_WINDOW 5000    // Additional awake time for SW flashing after Reset or Power-up in ms

#define DHTTYPE DHT11   // DHT 11
#define DHTPIN 5     // Digital pin connected to the DHT sensor

DHT dht(DHTPIN, DHTTYPE);
//DHT dht(5, DHT11);//Pin GPIO5=D3 on connector J11, type DHT11


uint8_t receiverMac[6] = {0xE8, 0xF6, 0x0A, 0x15, 0xFC, 0xEC}; //adjust receiver MAC-address

typedef enum : uint8_t {
    MSG_SYNC,     // synchronization only
    MSG_CONTROL   // execute control/read sensor at sender
} msg_type_t;

// ================= DATA STRUCTS =================
typedef struct {
  uint32_t counter;
  uint16_t vscap_instant;
  uint16_t vscap_avg;
  uint16_t avg_samples;
  uint32_t lastAwakeTime;
  uint8_t wakeupReason;
  uint32_t vscap_sum;
  uint32_t timestamp;  // timestamp sent by sender
  // NEW:
  float temperature;
  float humidity;
  uint8_t dhtStatus;
  uint8_t execDone;
  uint32_t execTimestamp;

} message_t;

typedef struct {
  uint8_t setting1;
  uint8_t setting2;
  uint16_t nextSleep; // 0 = ignore
  uint32_t timestamp; // timestamp received from RX, master timestamp
  msg_type_t msgType; //type of message

} settings_t;

// ================= RTC VARIABLES =================
RTC_DATA_ATTR uint32_t counter = 0;
RTC_DATA_ATTR uint32_t lastAwakePrev = 0;
RTC_DATA_ATTR uint32_t adcSum = 0;
RTC_DATA_ATTR uint16_t adcSamples = 0;
RTC_DATA_ATTR uint32_t lastTimestamp = 0;  // last millis() before deep sleep
RTC_DATA_ATTR uint32_t lastSleepDuration_ms= 0; // store the time spent in Deep sleep mode

RTC_DATA_ATTR float lastTemperature = 0;
RTC_DATA_ATTR float lastHumidity = 0;
RTC_DATA_ATTR uint8_t lastDhtStatus = 1; // 0=OK, 1=error
RTC_DATA_ATTR bool lastExecDone = false;
RTC_DATA_ATTR uint32_t lastExecTimestamp = 0;

// ================= GLOBALS =================
message_t msg;
settings_t settings;        // last received settings
bool txDone = false;
bool ackReceived = false;
bool dataReceived = false;
bool doControl = true; //do only communication or communicaton and control

uint32_t RXtimestamp; //rember the time of reception of system time


// ================= CALLBACKS =================
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  txDone = true;
  ackReceived = (status == ESP_NOW_SEND_SUCCESS);
}

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len == sizeof(settings_t)) {
    memcpy(&settings, data, sizeof(settings_t));
    dataReceived = true;

    // Copy master timestamp from RX
    // This is the authoritative “current time”
    //msg.timestamp = settings.timestamp;
    RXtimestamp = millis(); //store the time of reception of settings.timestamp - the system time
    
    // check message type here and define what to do (SYNC-only set timimg, no output control
    doControl = (settings.msgType == MSG_CONTROL);
  }
}

// ================= HELPERS =================
void sleepShort() {
  // Generate random offset between -1000 ms and +1000 ms
  long offset_ms = random(-RANDOM_SPREAD_MS, RANDOM_SPREAD_MS + 1);

  // Convert total sleep time to microseconds
  uint32_t sleep_ms = ((SLEEP_SHORT_SEC * 1000L) + offset_ms);
  lastSleepDuration_ms = sleep_ms;
  lastTimestamp = settings.timestamp + (millis() - RXtimestamp);
  esp_sleep_enable_timer_wakeup(sleep_ms * 1000ULL);
  esp_deep_sleep_start();
}

void sleepLong() {
  uint32_t sleep_ms = SLEEP_LONG_SEC * 1000ULL;
  lastSleepDuration_ms = sleep_ms;
  lastTimestamp = settings.timestamp + (millis() - RXtimestamp);
  esp_sleep_enable_timer_wakeup(sleep_ms * 1000ULL);
  esp_deep_sleep_start();
}

void sleepNext(uint32_t nextSec) {
  if (nextSec > 0) {
    uint32_t sleep_ms = nextSec * 1000ULL;
    lastSleepDuration_ms = sleep_ms;
    lastTimestamp = settings.timestamp + (millis() - RXtimestamp);
    esp_sleep_enable_timer_wakeup(sleep_ms * 1000ULL);
    esp_deep_sleep_start();
  } else {
    sleepLong();
  }
}


void sleepFlash() {
  uint32_t start = millis();

  // Stay awake for SW_flashing in milliseconds
  while (millis() - start < SW_FLASH_WINDOW) {
    // do nothing
  }

  // Then go to normal long sleep
  uint32_t sleep_ms = SLEEP_LONG_SEC * 1000ULL;
  lastSleepDuration_ms = sleep_ms;
  lastTimestamp = settings.timestamp + (millis() - RXtimestamp);
  esp_sleep_enable_timer_wakeup(sleep_ms * 1000ULL);
  esp_deep_sleep_start();
}

// ================= SETUP =================
void setup() {
  uint32_t awakeStart = millis();

  counter++;

  // ================= PRE-COMMUNICATION HANDLER =================
  handleBefore();


  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    sleepShort();  // ESP-NOW init failed → short sleep
  }

  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  // ================= ADC Instant Reading =================
  uint16_t vscapInstant = analogRead(ADC_PIN);
  
  // ================= Prepare TX Message =================
  msg.counter = counter;
  msg.vscap_instant = vscapInstant;
  msg.vscap_avg = (adcSamples > 0) ? (adcSum / adcSamples) : 0;
  msg.avg_samples = adcSamples;
  msg.lastAwakeTime = lastAwakePrev;
  msg.vscap_sum = adcSum;

  msg.temperature = lastTemperature;
  msg.humidity = lastHumidity;
  msg.dhtStatus = lastDhtStatus;
  msg.execDone = lastExecDone;
  msg.execTimestamp = lastExecTimestamp;
  // reset exec flag after sending
  lastExecDone = false;

  uint8_t wakeupReason = esp_sleep_get_wakeup_cause();
  msg.wakeupReason = wakeupReason;

  // approximate timestamp before first TX
    msg.timestamp = lastTimestamp + lastSleepDuration_ms + millis();

  // ================= ESP-NOW Send =================
  txDone = ackReceived = dataReceived = false;

  
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMac, 6);
  peer.channel = CHANNEL;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) sleepShort();

  esp_now_send(receiverMac, (uint8_t*)&msg, sizeof(msg));

  // Wait for ACK or data for up to MAX_TRX_TIME ms while reading ADC and averaging ADC
  adcSum = 0;
  adcSamples = 0;
  uint32_t start = millis();

  while ((millis() - start < MAX_TRX_TIME)) {
    uint16_t v = analogRead(ADC_PIN);
    adcSum += v;
    adcSamples++;

    if (txDone && ackReceived && dataReceived) break; //communication finished
    
  }

  // ================= POST-COMMUNICATION HANDLER =================
  if (doControl) { 
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);

    handleAfter();
  }

  lastAwakePrev = millis() - awakeStart;

  // ================= SLEEP DECISION (UNIFIED) =================
  bool manualFlash = (wakeupReason == 0) && (vscapInstant > 560); // SW flash window but only if VSCAP > 2V (in order not to collapse during startup at 1.5V)
 
  uint32_t nextSleepSec = (settings.nextSleep > 0) ? settings.nextSleep : 0;


  if (manualFlash) {
    sleepFlash();
  }
  else if (!ackReceived) {
    sleepShort(); // retry
  }
  else if (doControl) {
    // we just executed a command → report next cycle
    sleepShort();
  }
  else if (nextSleepSec > 0) {
    sleepNext(nextSleepSec);
  }
  else {
    // no command → normal idle behavior
    sleepLong();
  }

}

// ================= LOOP =================
void loop() {
  // Not used: everything handled in setup() → deep sleep
}

// ===== HANDLERS =====
void handleBefore() {
  // Processing before starting communication
  // Example: read sensor data
}

void handleAfter() {
  // Processing after communication is finished

  uint32_t execStart = millis();
  
  // Example: set outputs
  pinMode(3, OUTPUT); //GPIO3 is D1 or EN24V
  
  digitalWrite(3,1); //Turn on 12V/24V DCDC
  delay(10); //wait 10ms until 12V/24V is stable
  pinMode(10, OUTPUT); //GPIO10 is D10 used for controlling of H-Bridge A
  digitalWrite(10,1); //H-Bridge A output positive voltage
  delay(15); //wait 15ms
  pinMode(10, INPUT); //GPIO10 switch to HiZ (no pull-up/pull-down) -> turn off H-Bridge A outputs
  delay(1000); //wait 1000ms
  pinMode(10, OUTPUT); //GPIO10 is D10 used for controlling of H-Bridge A
  digitalWrite(10,0); //H-Bridge A output negative voltage
  delay(15); //wait 15ms
  pinMode(10, INPUT); //GPIO10 switch to HiZ (no pull-up/pull-down) -> turn off H-Bridge A outputs
  delay(10); //wait 10ms

  digitalWrite(3,0); //Turn off 12V/24V DCDC

  //Example2: read DHT sensor (temperature and humidity)
  dht.begin();
  pinMode(2, OUTPUT); //GPIO2 is D0 for turning on/off 4V_SW and 3V3_SW
  
  digitalWrite(2,0); //Turn on 4V_SW and 3V3_SW (TurnOn is Low)
  delay(10); //wait 10ms
  
  //read sensor DHT11
  float h = dht.readHumidity();
  float t = dht.readTemperature(); // Temperatur in Celsius

  if (isnan(h) || isnan(t)) {
    lastDhtStatus = 1;
    lastTemperature = 0;
    lastHumidity = 0;
  } else {
    lastDhtStatus = 0;
    lastTemperature = t;
    lastHumidity = h;
  }

// store precise timestamp
  lastExecTimestamp = settings.timestamp + (millis() - execStart);

  delay(10); //wait 10ms
  digitalWrite(2,1); //Turn off 4V_SW and 3V3_SW (TurnOff is High)

  // --- STORE EXECUTION RESULT ---
  lastExecDone = true; 
  
}
