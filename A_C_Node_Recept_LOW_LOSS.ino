#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "stage1_statistical.h"
#include "stage2_frequency.h"
#include "stage2b_controlled_frequency.h"
#include "stage3_spatial.h"
#include "stage4_fusion.h"
#include "stage5_robustness.h"
#include "stage6_model.h"

// ================================================================
// ESP32 #1 : NODE A + NODE C
// AC LOW-LOSS COMMISSIONING TEST
//
// Local link only:
//        NODE A  ---------------->  NODE C
//
// Both radios are on the SAME ESP32, so we can guarantee that
// Node C enters RX mode BEFORE Node A starts transmitting.
// This removes the synchronization problem from the test.
// ================================================================

const char* SSID = "Redmi Note 13 5G";
const char* PASSWORD = "AtharvDave";

WebServer server(80);

// Node A: Top-Left / Elevated
RF24 nodeA(4, 13);

// Node C: Bottom-Left / Ground
// GPIO2 is an ESP32 boot strap pin. Keep only if current wiring
// is already confirmed to work.
RF24 nodeC(21, 22);

// AC pipe
const uint64_t ADDR_AC = 0xE7E7E7E703LL;

// Existing AC channel
const uint8_t CH_AC = 110;
const char* ESP1_MDNS = "ac-node";
volatile bool bdPeerReady = false;
float bdPdrPeer = NAN;
uint16_t bdPacketsPeer = 0;

// Commissioning parameters
const uint16_t PACKETS_PER_TEST = 30;
const uint16_t INTER_PACKET_DELAY_MS = 10;

// Statistics
uint16_t txAckOK = 0;
uint16_t rxReceived = 0;
uint16_t txFailed = 0;
float deliveryLoss = 100.0;

void configureNRF(RF24& radio) {
  if (!radio.begin()) {
    Serial.println("ERROR: nRF24 initialization failed.");
    return;
  }

  radio.setDataRate(RF24_250KBPS);

  // Maximum power for hardware commissioning.
  radio.setPALevel(RF24_PA_MAX);

  // Reliability mode.
  radio.setAutoAck(true);
  radio.setRetries(15, 15);

  // Strong packet integrity check.
  radio.setCRCLength(RF24_CRC_16);

  // Fixed 1-byte payload for this test.
  radio.setPayloadSize(1);

  radio.stopListening();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  SPI.begin(); // SCK=18, MISO=19, MOSI=23

  configureNRF(nodeA);
  configureNRF(nodeC);

  // Wi-Fi is only for the telemetry endpoint.
  WiFi.begin(SSID, PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  uint32_t wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStart < 15000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected!");
    Serial.print("ESP32 #1 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi connection failed.");
    Serial.println("AC RF test will continue.");
  }

  server.on("/api/telemetry", handleTelemetry);
  server.on("/api/ml_peer", HTTP_POST, handleMLPeer);
  MDNS.begin(ESP1_MDNS);
  server.begin();

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 #1 : AC LOW-LOSS TEST");
  Serial.println(" NODE A  --->  NODE C");
  Serial.println(" 250 kbps | PA_MAX | AutoACK | Retry 15");
  Serial.println("========================================");
}

void runACTest() {

  txAckOK = 0;
  rxReceived = 0;
  txFailed = 0;

  // ------------------------------------------------
  // STEP 1: Configure C as receiver FIRST
  // ------------------------------------------------
  nodeC.stopListening();
  nodeC.setChannel(CH_AC);
  nodeC.openReadingPipe(1, ADDR_AC);

  // Remove anything left from an earlier test.
  nodeC.flush_rx();

  nodeC.startListening();

  // Give the nRF24 time to physically enter RX mode.
  delay(100);

  // ------------------------------------------------
  // STEP 2: Configure A as transmitter
  // ------------------------------------------------
  nodeA.stopListening();
  nodeA.setChannel(CH_AC);
  nodeA.openWritingPipe(ADDR_AC);

  // ------------------------------------------------
  // STEP 3: Send one packet at a time
  //
  // nodeA.write() blocks until:
  //   - ACK is received, OR
  //   - all hardware retries are exhausted.
  //
  // Therefore the receiver gets a chance to drain each
  // packet before the next packet is transmitted.
  // ------------------------------------------------
  for (uint16_t i = 0; i < PACKETS_PER_TEST; i++) {

    uint8_t payload = 0xA3;

    bool ok = nodeA.write(&payload, sizeof(payload));

    if (ok) {
      txAckOK++;
    } else {
      txFailed++;
    }

    // Drain C immediately after every transmission.
    while (nodeC.available()) {
      uint8_t receivedPayload;
      nodeC.read(&receivedPayload, sizeof(receivedPayload));

      if (receivedPayload == 0xA3) {
        rxReceived++;
      }
    }

    server.handleClient();
    delay(INTER_PACKET_DELAY_MS);
  }

  // Give the receiver a final drain period.
  delay(100);

  while (nodeC.available()) {
    uint8_t receivedPayload;
    nodeC.read(&receivedPayload, sizeof(receivedPayload));

    if (receivedPayload == 0xA3) {
      rxReceived++;
    }
  }

  nodeC.stopListening();

  deliveryLoss =
      100.0 * (PACKETS_PER_TEST - min(txAckOK, PACKETS_PER_TEST))
      / (float)PACKETS_PER_TEST;

  // ------------------------------------------------
  // Print results
  // ------------------------------------------------
  Serial.println();
  Serial.println("========================================");
  Serial.println("             AC TEST RESULT");
  Serial.println("========================================");
  Serial.printf("Channel          : %u\n", CH_AC);
  Serial.printf("Packets attempted: %u\n", PACKETS_PER_TEST);
  Serial.printf("TX ACK success   : %u\n", txAckOK);
  Serial.printf("TX failed        : %u\n", txFailed);
  Serial.printf("RX received      : %u\n", rxReceived);
  Serial.printf("Delivery loss    : %.2f %%\n", deliveryLoss);
  Serial.println("----------------------------------------");

  if (txAckOK == PACKETS_PER_TEST && rxReceived == PACKETS_PER_TEST) {
    Serial.println("RESULT: AC LINK CLEAN");
  } else if (txAckOK == PACKETS_PER_TEST) {
    Serial.println("RESULT: ACK SUCCESS BUT RX COUNT MISMATCH");
  } else {
    Serial.println("RESULT: AC LINK HAS REAL DELIVERY FAILURES");
  }

  Serial.println("========================================");
}

void loop() {
  server.handleClient();

  runACTest();
  runTinyMLAndAdapt();

  // Pause between independent tests.
  delay(1000);
}

void handleTelemetry() {

  StaticJsonDocument<400> doc;

  doc["esp_id"] = 1;
  doc["link"] = "AC";
  doc["channel"] = CH_AC;
  doc["packets"] = PACKETS_PER_TEST;
  doc["tx_ack_ok"] = txAckOK;
  doc["tx_failed"] = txFailed;
  doc["rx_received"] = rxReceived;
  doc["delivery_loss_percent"] = deliveryLoss;

  String output;
  serializeJson(doc, output);

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", output);
}


void handleMLPeer() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "missing body"); return; }
  StaticJsonDocument<300> doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  bdPdrPeer = doc["pdr"] | NAN;
  bdPacketsPeer = doc["packets"] | 0;
  bdPeerReady = isfinite(bdPdrPeer);
  server.send(200, "application/json", "{\"ok\":true}");
}

void buildTinyMLVector(float x[STAGE6_N_FEATURES]) {
  float bd = isfinite(bdPdrPeer) ? bdPdrPeer : pgm_read_float(&stage6_feature_median[13]);
  uint16_t bdN = bdPacketsPeer ? bdPacketsPeer : PACKETS_PER_TEST;
  stage3_build_46_features(
      x,
      txAckOK / (float)PACKETS_PER_TEST, PACKETS_PER_TEST,
      bd, bdN);
  stage6_fill_missing(x, nullptr);
}

void runTinyMLAndAdapt() {
  float x[STAGE6_N_FEATURES];
  float prob[STAGE6_N_CLASSES];

  buildTinyMLVector(x);
  int cls = stage6_predict(x, prob);
  int selected = STAGE6_POLICY_CHANNEL[cls];
  float confidence = prob[cls];
  float margin = stage5_confidence_margin(prob, STAGE6_N_CLASSES);

  Serial.println();
  Serial.println("================================================");
  Serial.println("          FINAL TINYML DEPLOYMENT");
  Serial.println("================================================");
  Serial.println("AIM 1: MATERIAL IDENTIFICATION");
  Serial.printf("Material            : %s\n", STAGE6_MATERIALS[cls]);
  Serial.printf("Confidence          : %.2f %%\n", confidence*100.0f);
  Serial.printf("Confidence margin   : %.2f %%\n", margin*100.0f);
  Serial.println("Class probabilities:");
  for(int c=0;c<STAGE6_N_CLASSES;c++)
    Serial.printf("  %-9s : %6.2f %%\n", STAGE6_MATERIALS[c], prob[c]*100.0f);

  Serial.println();
  Serial.println("AIM 2: MATERIAL-CONDITIONED CHANNEL ADAPTATION");
  Serial.printf("AC measured PDR    : %.3f\n", txAckOK/(float)PACKETS_PER_TEST);
  Serial.printf("BD measured PDR    : %.3f\n", isfinite(bdPdrPeer)?bdPdrPeer:-1.0f);
  Serial.printf("Selected channel   : %d\n", selected);
  Serial.printf("Policy mapping     : %s -> CH %d\n", STAGE6_MATERIALS[cls], selected);

  // Apply the learned material-conditioned channel to the AC radio pair.
  if (stage2b_channel_valid(selected)) {
    nodeC.stopListening();
    nodeC.setChannel((uint8_t)selected);
    nodeC.openReadingPipe(1, ADDR_AC);
    nodeC.flush_rx();
    nodeC.startListening();
    delay(50);

    nodeA.stopListening();
    nodeA.setChannel((uint8_t)selected);
    nodeA.openWritingPipe(ADDR_AC);
  }

  Serial.println("RF ACTION         : AC radios switched to selected channel");
  Serial.println("================================================");

  bdPeerReady=false;
}
