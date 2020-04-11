#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <dashboard.h>
#include <map>
#include <periodic-runner.h>

#include "constants.h"
#include "hue-client.h"

Dashboard *dashboard;
AsyncWebServer server(80);
HueClient hue_client(kHueBridgeAddress, kHueUsername);
PeriodicRunner runner;

std::vector<int> lights;
std::map<int, int16_t> prev_brightness;
bool light_changed = false;

static const uint32_t refresh_mdns_delay = 60 * 1000;

void CheckForLightChanged() {
  bool all_off = true;
  for (int light : lights) {
    int16_t brightness = hue_client.GetLightBrightness(light);

    // Ignore errors (hopefully temporary)
    if (brightness < 0) {
      continue;
    }

    if (brightness != prev_brightness[light]) {
      prev_brightness[light] = brightness;
      light_changed = true;
      Serial.printf("Light %3d changed to %3d, was %3d\n", light, brightness, prev_brightness[light]);
    }

    if (brightness != 0) {
      all_off = false;
    }
  }
  if (all_off) {
    light_changed = false;
  }
}

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
  runner.Add(refresh_mdns_delay, []() {
    if (!MDNS.begin("motion-sensor")) {
      Serial.println("Error refreshing MDNS responder!");
    }
  });

  Serial.println("Starting server...");
  dashboard = new Dashboard(&server);
  dashboard->Add<uint32_t>("Uptime", millis, 5000);
  dashboard->Add("light changed", light_changed, 2000);
  dashboard->Add("lights", []() {
    std::string ret = "";
    for (auto light : lights) {
      const uint16_t buf_size = 50;
      char buf[buf_size];
      snprintf(buf, buf_size, "(%3d: %3d)&nbsp;&nbsp;", light, prev_brightness[light]);
      ret += buf;
    }
    return ret;
  }, 2000);
  server.begin();
  Serial.println("Started server.");

  lights = hue_client.GetLightsForRoom(2);
  Serial.print("Got lights: ");
  for (int light : lights) {
    Serial.printf("%d, ", light);
  }
  Serial.println();

  Serial.println("Initial brightness: ");
  for (int light : lights) {
    prev_brightness[light] = hue_client.GetLightBrightness(light);
    Serial.printf("  %3d: %3d\n", light, prev_brightness[light]);
  }
}

void loop() {
  ArduinoOTA.handle();
  runner.Run();

  runner.Add(1000, CheckForLightChanged);
}
