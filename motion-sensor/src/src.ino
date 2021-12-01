#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <dashboard.h>
#include <iomanip>
#include <map>
#include <periodic-runner.h>
#include <time.h>

#include <esp_wifi.h>

#include "constants.h"
#include "hue-client.h"
#include "state-manager.h"

// Events used for triggering state transitions.
enum class Events {
  kTimer,
  kMotion,
  kLightsChanged,
  kLightsTurnedOff,
};

// Context used by state machine states.
struct Context {
  bool is_night_mode;
};

StateManager<Context, Events, Events::kTimer>* state_manager;

Dashboard *dashboard;
AsyncWebServer server(80);
HueClient hue_client(kHueBridgeAddress, kHueUsername);
PeriodicRunner runner;

std::vector<int> lights;
std::map<int, int16_t> prev_brightness;
bool motion_interrupt_triggered = false;
bool is_night_mode = false;
bool we_changed_light = false;
uint32_t we_changed_light_at = 0;
uint32_t light_change_detected_at = 0;
uint32_t motion_detected_at = 0;

static const char* kMdnsName = "motion-sensor";

// The Hue group id for the lights to change
static const int kGroupId = 3;

// Tuning constants
// How long to leave the lights on after motion is detected
static const uint32_t kLightsOnDelay = 20 * 60 * 1000;
// If lights were externally changed, how long to wait until turning them off
static const uint32_t kExternalLightsOnDelay = 30 * 60 * 1000;
// How long to wait before turning the lights on after turning them off
static const uint32_t kLightsOffDelay = 30 * 1000;
// How long to ignore light changes after we set the lights
static const uint32_t kLightChangeIgnoreDelay = 5 * 1000;
// After motion is detected, how long to consider the room occupied
static const uint32_t kOccupiedDelay = 10 * 60 * 1000;

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

static std::function<std::string()> IntervalToString(uint32_t &ms) {
  return [&ms]() {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << (((millis() - ms) / 1000) / 60.0);
    return stream.str();
  };
}

void CheckForLightChanged() {
  bool all_off = true;
  bool did_lights_change = false;
  for (int light : lights) {
    LightStatus status = hue_client.GetLightStatus(light);

    // Ignore errors (hopefully temporary)
    if (status.error) {
      all_off = false;
      continue;
    }

    if (status.brightness != prev_brightness[light]) {
      Serial.printf("Light %3d changed to %3d, was %3d\n", light, status.brightness, prev_brightness[light]);
      prev_brightness[light] = status.brightness;
      did_lights_change = true;
    }

    if (status.brightness != 0) {
      all_off = false;
    }
  }
  // After we change the lights, ignore the first light change we get back from Hue.
  if (we_changed_light) {
    if (millis() - we_changed_light_at > kLightChangeIgnoreDelay) {
      we_changed_light = false;
    }
    return;
  }
  if (did_lights_change) {
    if (all_off) {
      Serial.println("Lights turned off");
      state_manager->HandleEvent(Events::kLightsTurnedOff);
    } else {
      Serial.println("Lights changed");
      state_manager->HandleEvent(Events::kLightsChanged);
    }
  }
}

void MotionHandler() {
  motion_interrupt_triggered = true;
}

void setup() {
  // Dummy value (must be 0) for displaying the uptime
  static uint32_t start_time = 0;
  Serial.begin(115200);

  pinMode(kMotionSensorPin, INPUT);
  attachInterrupt(kMotionSensorPin, MotionHandler, RISING);

  Serial.print("Connecting to wifi...");
  WiFi.begin(kSsid, kPassword);
  while (WiFi.status() != WL_CONNECTED) {}
  Serial.println(" done.");
  delay(500);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

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

  if (MDNS.begin(kMdnsName)) {
    // Add service to MDNS-SD
    MDNS.addService("_http", "_tcp", 80);
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  runner.Add(kRefreshMdnsDelay, []() {
    if (!MDNS.begin(kMdnsName)) {
      Serial.println("Error refreshing MDNS responder!");
    }
  });

  State<Context, Events>* off = new State<Context, Events>("off", {}, [](Context* context, bool state_changed) {
    return (uint32_t) 0;
  });

  State<Context, Events>* turn_off = new State<Context, Events>("turn_off", {}, [](Context* context, bool state_changed) {
    if (state_changed) {
      hue_client.SetGroupBrightness(kGroupId, 0);
      we_changed_light_at = millis();
    }
    return kLightsOffDelay;
  });

  State<Context, Events>* automatic_on = new State<Context, Events>("automatic_on", {}, [](Context* context, bool state_changed) {
    return kLightsOnDelay;
  });

  State<Context, Events>* turn_on = new State<Context, Events>("turn_on", {}, [](Context* context, bool state_changed) {
    if (state_changed) {
      uint32_t brightness = context->is_night_mode ? kNightBrightness : kDayBrightness;
      hue_client.SetGroupBrightness(kGroupId, brightness);
      we_changed_light_at = millis();
      we_changed_light = true;
    }
    return kLightChangeIgnoreDelay;
  });

  State<Context, Events>* manual_on = new State<Context, Events>("manual_on", {}, [](Context* context, bool state_changed) {
    light_change_detected_at = millis();
    return kExternalLightsOnDelay;
  });

  State<Context, Events>* manual_off = new State<Context, Events>("manual_off", {}, [](Context* context, bool state_changed) {
    return kOccupiedDelay;
  });

  off->transitions[Events::kMotion] = turn_on;
  off->transitions[Events::kLightsChanged] = manual_on;

  turn_off->transitions[Events::kTimer] = off;

  automatic_on->transitions[Events::kTimer] = turn_off;
  automatic_on->transitions[Events::kMotion] = State<Context, Events>::NO_CHANGE;
  automatic_on->transitions[Events::kLightsChanged] = manual_on;
  automatic_on->transitions[Events::kLightsTurnedOff] = manual_off;

  turn_on->transitions[Events::kTimer] = automatic_on;

  manual_on->transitions[Events::kTimer] = turn_off;
  manual_on->transitions[Events::kMotion] = State<Context, Events>::NO_CHANGE;
  manual_on->transitions[Events::kLightsChanged] = State<Context, Events>::NO_CHANGE;
  manual_on->transitions[Events::kLightsTurnedOff] = manual_off;

  manual_off->transitions[Events::kTimer] = off;
  manual_off->transitions[Events::kMotion] = State<Context, Events>::NO_CHANGE;
  manual_off->transitions[Events::kLightsChanged] = manual_on;

  state_manager = new StateManager<Context, Events, Events::kTimer>(off, new Context());

  Serial.println("Starting server...");
  dashboard = new Dashboard(&server);
  dashboard->Add("Uptime, minutes", IntervalToString(start_time), 5000);
  dashboard->Add("State", []() { return state_manager->CurrentStateName(); }, 1000);
  dashboard->Add("Motion last triggered, minutes", IntervalToString(motion_detected_at), 1000);
  dashboard->Add("We last changed lights, minutes", IntervalToString(we_changed_light_at), 1000);
  dashboard->Add("Light change detected at, minutes", IntervalToString(light_change_detected_at), 1000);
  dashboard->Add("Night mode", is_night_mode, 10000);
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

  // Create an HTTP endpoint for Prometheus
  server.on("/metrics", HTTP_GET, [](AsyncWebServerRequest *request) {
    String metrics = String(String("uptime ") + String(millis() - start_time));
    metrics = String(metrics + "\n# TYPE state gauge");
    metrics = String(metrics + String("\nstate{name=\"") + String(state_manager->CurrentStateName()) + String("\"} 1"));
    metrics = String(metrics + String("\nlast_put_latency_millis{type = \"http\"} ") + String(hue_client.last_put_latency()));
    metrics = String(metrics + String("\nlast_put_latency_millis{type = \"total\"} ") + String(hue_client.last_put_function_latency()));
    request->send(200, "text/plain", metrics);
  });

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
  bool all_off = true;
  for (int light : lights) {
    prev_brightness[light] = hue_client.GetLightStatus(light).brightness;
    Serial.printf("  %3d: %3d\n", light, prev_brightness[light]);
    if (prev_brightness[light] != 0) {
      all_off = false;
    }
  }
  if (!all_off) {
    state_manager->HandleEvent(Events::kLightsChanged);
  }

  runner.Add(2000, CheckForLightChanged);
}

void loop() {
  ArduinoOTA.handle();
  runner.Run();

  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    if (timeinfo.tm_hour > kDarkStartHour || (timeinfo.tm_hour == kDarkStartHour && timeinfo.tm_min >= kDarkStartMinute)
      || timeinfo.tm_hour < kDarkEndHour || (timeinfo.tm_hour == kDarkEndHour && timeinfo.tm_min <= kDarkEndMinute)) {
      state_manager->context()->is_night_mode = true;
    } else {
      state_manager->context()->is_night_mode = false;
    }
  } else {
    Serial.println("Failed to obtain time");
  }

  if (motion_interrupt_triggered) {
    Serial.println("Motion detected");
    state_manager->HandleEvent(Events::kMotion);
    motion_interrupt_triggered = false;
    motion_detected_at = millis();
  }
  state_manager->Run();
}
