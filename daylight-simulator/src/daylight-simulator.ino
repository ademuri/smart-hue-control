#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <dashboard.h>
#include <periodic-runner.h>

#include "constants.h"
#include "hue-client.h"

static const uint32_t kRefreshMdnsDelay = 60 * 1000;

Dashboard *dashboard;
AsyncWebServer server(80);
HueClient hue_client(kHueBridgeAddress, kHueUsername);
PeriodicRunner runner;

void setup() {
  Serial.begin(115200);
  Serial.print("Connecting to wifi...");
  WiFi.begin(kSsid, kPassword);
  while (WiFi.status() != WL_CONNECTED) {}
  Serial.println(" done.");
  delay(500);
  WiFi.setSleep(false);

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_SPIFFS
        type = "filesystem";
      }

      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();

  if (MDNS.begin("motion-sensor")) {
    // Add service to MDNS-SD
    MDNS.addService("_http", "_tcp", 80);
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  runner.Add(kRefreshMdnsDelay, []() {
    if (!MDNS.begin("motion-sensor")) {
      Serial.println("Error refreshing MDNS responder!");
    }
  });
}

void loop() {
  ArduinoOTA.handle();
  runner.Run();
}
