#include <HTTPClient.h>

#include "hue-client.h"

HueClient::HueClient(String address, String username) : address_(address), username_(username), url_prefix_(address + "api/" + username + "/") {}

std::vector<int> HueClient::GetLightsForRoom(int room) {
  std::vector<int> lights;
  DynamicJsonDocument doc(5000);
  bool error = Call(doc, "groups/" + String(room));
  if (error) {
    // TODO: return error code instead?
    return lights;
  }

  const char *name = doc["name"];
  if (name == nullptr) {
    Serial.println("No name");
  } else {
    Serial.printf("Room: %s\n", name);
  }
  JsonArray array = doc["lights"];
  for (JsonVariant light : array) {
    lights.push_back(light);
  }

  return lights;
}

int16_t HueClient::GetLightBrightness(int light) {
  DynamicJsonDocument doc(5000);
  bool error = Call(doc, "lights/" + String(light));
  if (error) {
    return -1;
  }
  bool on = doc["state"]["on"];
  uint8_t brightness = doc["state"]["bri"];
  //Serial.printf("on: %d, brightness: %u\n", on, brightness);

  // Hue API returns the brightness regardless if the light is on
  return on ? brightness : 0;
}

bool HueClient::Call(JsonDocument &doc, String endpoint) {
  WiFiClient wifi_client;
  HTTPClient http;

  http.begin(url_prefix_ + endpoint);
  int http_code = http.GET();
  if (http_code != HTTP_CODE_OK) {
    Serial.printf("Get groups failed: %d\n", http_code);
    http.end();
    return true;
  }

  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(err.c_str());
    return true;
  }

  return false;
}

void HueClient::PrintJsonKeys(JsonDocument &doc) {
  Serial.printf("Keys: ");
  JsonObject object = doc.as<JsonObject>();
  for (JsonPair p : object) {
    Serial.printf("%s, ", p.key().c_str());
  }
  Serial.printf("\n");
}
