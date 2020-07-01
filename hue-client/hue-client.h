/*
 * Copyright 2020 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HUE_CLIENT_H
#define HUE_CLIENT_H

#include <ArduinoJson.h>
#include <string>
#include <vector>

struct LightStatus {
  bool error;
  bool on;
  
  // Brightness, or 0 if off
  uint8_t brightness;

  // Raw brightness (could be non-zero even if the light is off).
  uint8_t raw_brightness;

  uint16_t temperature;
};

class HueClient {
  public:
    HueClient(String address, String username);

    std::vector<int> GetLightsForGroup(int group);
    LightStatus GetLightStatus(int light);
    bool SetGroupBrightness(int light, uint8_t brightness);

  private:
    // Calls the endpoint and parses it into doc. Returns true (and prints to the console) on error.
    bool Get(JsonDocument &doc, String endpoint);
    bool Put(JsonDocument &doc, String endpoint);
    void PrintJsonKeys(JsonDocument &doc);

    const String address_;
    const String username_;
    const String url_prefix_;
};

#endif // HUE_CLIENT_H
