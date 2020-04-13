#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <dashboard.h>
#include <map>
#include <periodic-runner.h>
#include <time.h>

#include "constants.h"
#include "hue-client.h"

Dashboard *dashboard;
AsyncWebServer server(80);
HueClient hue_client(kHueBridgeAddress, kHueUsername);
PeriodicRunner runner;

std::vector<int> lights;
std::map<int, int16_t> prev_brightness;
bool lights_changed = false;
bool lights_on = false;
bool motion_interrupt_triggered = false;
bool is_dark_now = false;
uint32_t we_changed_light_at = 0;
uint32_t motion_detected_at = 0;

// The Hue group id for the lights to change
static const int kGroupId = 2;

// Tuning constants
// How long to leave the lights on after motion is detected
static const uint32_t kLightsOnDelay = 60 * 60 * 1000;
// If lights were externally changed, how long to wait until turning them off
static const uint32_t kExternalLightsOnDelay = 6 * 60 * 60 * 1000;
// How long to wait before turning the lights on after turning them off
static const uint32_t kLightsOffDelay = 30 * 1000;
// How long to ignore light changes after we set the lights
static const uint32_t kLightChangeIgnoreDelay = 1 * 1000;

// Daytime light brightness
static const uint8_t kDayBrightness = 254;
// Nightime light brightness
static const uint8_t kNightBrightness = 10;

// During dark time, set the lights to a lower brightness.
static const int kDarkStartHour = 23;
static const int kDarkStartMinute = 30;
static const int kDarkEndHour = 7;
static const int kDarkEndMinute = 30;

// NTP settings
const char* kNtpServer = "pool.ntp.org";
const long kGmtOffsetSeconds = -7 * 3600;
const int kDaylightOffsetSeconds = 3600;

static const int kMotionSensorPin = 27;

static const uint32_t kRefreshMdnsDelay = 60 * 1000;

void CheckForLightChanged() {
  if (millis() - we_changed_light_at < kLightChangeIgnoreDelay) {
    return;
  }

  bool all_off = true;
  for (int light : lights) {
    int16_t brightness = hue_client.GetLightBrightness(light);

    // Ignore errors (hopefully temporary)
    if (brightness < 0) {
      continue;
    }

    if (brightness != prev_brightness[light]) {
      Serial.printf("Light %3d changed to %3d, was %3d\n", light, brightness, prev_brightness[light]);
      prev_brightness[light] = brightness;
      lights_changed = true;
    }

    if (brightness != 0) {
      all_off = false;
    }
  }
  if (all_off) {
    lights_changed = false;
    if (lights_on) {
      // Repect the lights-off to lights-on delay if someone else turned off the lights
      we_changed_light_at = millis();
    }
  }
  lights_on = !all_off;
}

void MotionHandler() {
  motion_interrupt_triggered = true;
}

void setup() {
  Serial.begin(115200);

  pinMode(kMotionSensorPin, INPUT);
  attachInterrupt(kMotionSensorPin, MotionHandler, RISING);

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

  Serial.println("Starting server...");
  dashboard = new Dashboard(&server);
  dashboard->Add<uint32_t>("Uptime", millis, 5000);
  dashboard->Add<uint32_t>("Motion last triggered, seconds", []() { return (millis() - motion_detected_at) / 1000; }, 1000);
  dashboard->Add<uint32_t>("We last changed lights, seconds", []() { return (millis() - we_changed_light_at) / 1000; }, 1000);
  dashboard->Add("External light changed", lights_changed, 2000);
  dashboard->Add("Dark now", is_dark_now, 10000);
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

  configTime(kGmtOffsetSeconds, kDaylightOffsetSeconds, kNtpServer);
  // Print the time
  struct tm timeinfo;
   if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");


  lights = hue_client.GetLightsForGroup(kGroupId);
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

  runner.Add(1000, CheckForLightChanged);
}

uint32_t prev_millis = 0;
void loop() {
  ArduinoOTA.handle();
  runner.Run();

  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    if (timeinfo.tm_hour > kDarkStartHour || (timeinfo.tm_hour == kDarkStartHour && timeinfo.tm_min >= kDarkStartMinute)
      || timeinfo.tm_hour < kDarkEndHour || (timeinfo.tm_hour == kDarkEndHour && timeinfo.tm_min <= kDarkEndMinute)) {
      is_dark_now = true;
    } else {
      is_dark_now = false;
    }
  } else {
    Serial.println("Failed to obtain time");
  }

  if (motion_interrupt_triggered) {
    Serial.println("Motion detected");
    motion_detected_at = millis();

    if (!lights_changed && millis() - we_changed_light_at > kLightsOffDelay) {
      Serial.println("Turning on lights");
      uint32_t brightness = is_dark_now ? kNightBrightness : kDayBrightness;
      hue_client.SetGroupBrightness(kGroupId, brightness);
      for (int light : lights) {
        prev_brightness[light] = brightness;
      }
    }
    motion_interrupt_triggered = false;
  }

  if (lights_on) {
    if ((!lights_changed && millis() - motion_detected_at > kLightsOnDelay) ||
      (millis() - motion_detected_at > kExternalLightsOnDelay)) {
      Serial.println("Turning off lights");
      hue_client.SetGroupBrightness(kGroupId, 0);
      we_changed_light_at = millis();
      lights_on = false;
    }
  }
}
