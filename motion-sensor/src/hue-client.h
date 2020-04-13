#ifndef HUE_CLIENT_H
#define HUE_CLIENT_H

#include <ArduinoJson.h>
#include <string>
#include <vector>

class HueClient {
  public:
    HueClient(String address, String username);

    std::vector<int> GetLightsForGroup(int group);
    int16_t GetLightBrightness(int light);
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
