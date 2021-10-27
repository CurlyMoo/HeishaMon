#include "webfunctions.h"
#include "decode.h"
#include "version.h"
#include "htmlcode.h"
#include "commands.h"
#include "src/common/webserver.h"

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#define UPTIME_OVERFLOW 4294967295 // Uptime overflow value

static int numSsid = 0;

struct websettings_t {
  String name;
  String value;
  struct websettings_t *next;
};

static struct websettings_t *websettings = NULL;

void log_message(char* string);

void getWifiScanResults(int networksFound) {
  numSsid = networksFound;
}

int dBmToQuality(int dBm) {
  if (dBm == 31)
    return -1;
  if (dBm <= -100)
    return 0;
  if (dBm >= -50)
    return 100;
  return 2 * (dBm + 100);
}

int getWifiQuality() {
  if (WiFi.status() != WL_CONNECTED)
    return -1;
  return dBmToQuality(WiFi.RSSI());
}

int getFreeMemory() {
  //store total memory at boot time
  static uint32_t total_memory = 0;
  if ( 0 == total_memory ) total_memory = ESP.getFreeHeap();

  uint32_t free_memory   = ESP.getFreeHeap();
  return (100 * free_memory / total_memory ) ; // as a %
}

// returns system uptime in seconds
char *getUptime(void) {
  static uint32_t last_uptime      = 0;
  static uint8_t  uptime_overflows = 0;

  if (millis() < last_uptime) {
    ++uptime_overflows;
  }
  last_uptime             = millis();
  uint32_t t = uptime_overflows * (UPTIME_OVERFLOW / 1000) + (last_uptime / 1000);

  uint8_t  d   = t / 86400L;
  uint8_t  h   = ((t % 86400L) / 3600L) % 60;
  uint32_t rem = t % 3600L;
  uint8_t  m   = rem / 60;
  uint8_t  sec = rem % 60;

  unsigned int len = snprintf_P(NULL, 0, PSTR("%d day%s %d hour%s %d minute%s %d second%s"), d, (d == 1) ? "" : "s", h, (h == 1) ? "" : "s", m, (m == 1) ? "" : "s", sec, (sec == 1) ? "" : "s");
  char *str = (char *)malloc(len+2);
  if(str == NULL) {
    Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
  }
  memset(str, 0, len+2);
  snprintf_P(str, len+1, PSTR("%d day%s %d hour%s %d minute%s %d second%s"), d, (d == 1) ? "" : "s", h, (h == 1) ? "" : "s", m, (m == 1) ? "" : "s", sec, (sec == 1) ? "" : "s");
  return str;
}

void loadSettings(settingsStruct *heishamonSettings) {
  //read configuration from FS json
  log_message((char *)"mounting FS...");

  if (LittleFS.begin()) {
    log_message((char *)"mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      log_message((char *)"reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        log_message((char *)"opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument jsonDoc(1024);
        DeserializationError error = deserializeJson(jsonDoc, buf.get());
        char log_msg[512];
        serializeJson(jsonDoc, log_msg);
        log_message(log_msg);
        if (!error) {
          log_message((char *)"\nparsed json");
          //read updated parameters, make sure no overflow
          if ( jsonDoc["wifi_ssid"] ) strncpy(heishamonSettings->wifi_ssid, jsonDoc["wifi_ssid"], sizeof(heishamonSettings->wifi_ssid));
          if ( jsonDoc["wifi_password"] ) strncpy(heishamonSettings->wifi_password, jsonDoc["wifi_password"], sizeof(heishamonSettings->wifi_password));
          if ( jsonDoc["wifi_hostname"] ) strncpy(heishamonSettings->wifi_hostname, jsonDoc["wifi_hostname"], sizeof(heishamonSettings->wifi_hostname));
          if ( jsonDoc["ota_password"] ) strncpy(heishamonSettings->ota_password, jsonDoc["ota_password"], sizeof(heishamonSettings->ota_password));
          if ( jsonDoc["mqtt_topic_base"] ) strncpy(heishamonSettings->mqtt_topic_base, jsonDoc["mqtt_topic_base"], sizeof(heishamonSettings->mqtt_topic_base));
          if ( jsonDoc["mqtt_server"] ) strncpy(heishamonSettings->mqtt_server, jsonDoc["mqtt_server"], sizeof(heishamonSettings->mqtt_server));
          if ( jsonDoc["mqtt_port"] ) strncpy(heishamonSettings->mqtt_port, jsonDoc["mqtt_port"], sizeof(heishamonSettings->mqtt_port));
          if ( jsonDoc["mqtt_username"] ) strncpy(heishamonSettings->mqtt_username, jsonDoc["mqtt_username"], sizeof(heishamonSettings->mqtt_username));
          if ( jsonDoc["mqtt_password"] ) strncpy(heishamonSettings->mqtt_password, jsonDoc["mqtt_password"], sizeof(heishamonSettings->mqtt_password));
          heishamonSettings->use_1wire = ( jsonDoc["use_1wire"] == "enabled" ) ? true : false;
          heishamonSettings->use_s0 = ( jsonDoc["use_s0"] == "enabled" ) ? true : false;
          heishamonSettings->listenonly = ( jsonDoc["listenonly"] == "enabled" ) ? true : false;
          heishamonSettings->logMqtt = ( jsonDoc["logMqtt"] == "enabled" ) ? true : false;
          heishamonSettings->logHexdump = ( jsonDoc["logHexdump"] == "enabled" ) ? true : false;
          heishamonSettings->logSerial1 = ( jsonDoc["logSerial1"] == "enabled" ) ? true : false;
          heishamonSettings->optionalPCB = ( jsonDoc["optionalPCB"] == "enabled" ) ? true : false;
          if ( jsonDoc["waitTime"]) heishamonSettings->waitTime = jsonDoc["waitTime"];
          if (heishamonSettings->waitTime < 5) heishamonSettings->waitTime = 5;
          if ( jsonDoc["waitDallasTime"]) heishamonSettings->waitDallasTime = jsonDoc["waitDallasTime"];
          if (heishamonSettings->waitDallasTime < 5) heishamonSettings->waitDallasTime = 5;
          if ( jsonDoc["dallasResolution"]) heishamonSettings->dallasResolution = jsonDoc["dallasResolution"];
          if ((heishamonSettings->dallasResolution < 9) || (heishamonSettings->dallasResolution > 12) ) heishamonSettings->dallasResolution = 12;
          if ( jsonDoc["updateAllTime"]) heishamonSettings->updateAllTime = jsonDoc["updateAllTime"];
          if (heishamonSettings->updateAllTime < heishamonSettings->waitTime) heishamonSettings->updateAllTime = heishamonSettings->waitTime;
          if ( jsonDoc["updataAllDallasTime"]) heishamonSettings->updataAllDallasTime = jsonDoc["updataAllDallasTime"];
          if (heishamonSettings->updataAllDallasTime < heishamonSettings->waitDallasTime) heishamonSettings->updataAllDallasTime = heishamonSettings->waitDallasTime;
          if (jsonDoc["s0_1_gpio"]) heishamonSettings->s0Settings[0].gpiopin = jsonDoc["s0_1_gpio"];
          if (jsonDoc["s0_1_ppkwh"]) heishamonSettings->s0Settings[0].ppkwh = jsonDoc["s0_1_ppkwh"];
          if (jsonDoc["s0_1_interval"]) heishamonSettings->s0Settings[0].lowerPowerInterval = jsonDoc["s0_1_interval"];
          if (jsonDoc["s0_1_minpulsewidth"]) heishamonSettings->s0Settings[0].minimalPulseWidth = jsonDoc["s0_1_minpulsewidth"];
          if (jsonDoc["s0_1_maxpulsewidth"]) heishamonSettings->s0Settings[0].maximalPulseWidth = jsonDoc["s0_1_maxpulsewidth"];
          if (jsonDoc["s0_2_gpio"]) heishamonSettings->s0Settings[1].gpiopin = jsonDoc["s0_2_gpio"];
          if (jsonDoc["s0_2_ppkwh"]) heishamonSettings->s0Settings[1].ppkwh = jsonDoc["s0_2_ppkwh"];
          if (jsonDoc["s0_2_interval"] ) heishamonSettings->s0Settings[1].lowerPowerInterval = jsonDoc["s0_2_interval"];
          if (jsonDoc["s0_2_minpulsewidth"]) heishamonSettings->s0Settings[1].minimalPulseWidth = jsonDoc["s0_2_minpulsewidth"];
          if (jsonDoc["s0_2_maxpulsewidth"]) heishamonSettings->s0Settings[1].maximalPulseWidth = jsonDoc["s0_2_maxpulsewidth"];
        } else {
          log_message((char *)"Failed to load json config, forcing config reset.");
          WiFi.persistent(true);
          WiFi.disconnect();
          WiFi.persistent(false);
        }
        configFile.close();
      }
    }
    else {
      log_message((char *)"No config.json exists! Forcing a config reset.");
      WiFi.persistent(true);
      WiFi.disconnect();
      WiFi.persistent(false);
    }
  } else {
    log_message((char *)"failed to mount FS");
  }
  //end read

}

void setupWifi(settingsStruct *heishamonSettings) {
  log_message((char *)"Wifi reconnecting with new configuration...");
  //no sleep wifi
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);

  if (heishamonSettings->wifi_ssid[0] != '\0') {
    log_message((char *)"Wifi client mode...");
    //WiFi.persistent(true); //breaks stuff

    if (heishamonSettings->wifi_password[0] == '\0') {
      WiFi.begin(heishamonSettings->wifi_ssid);
    } else {
      WiFi.begin(heishamonSettings->wifi_ssid, heishamonSettings->wifi_password);
    }
  }
  else {
    log_message((char *)"Wifi hotspot mode...");
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP((char*)"HeishaMon-Setup");
  }

  if (heishamonSettings->wifi_hostname[0] == '\0') {
    //Set hostname on wifi rather than ESP_xxxxx
    WiFi.hostname((char *)"HeishaMon");
  } else {
    WiFi.hostname(heishamonSettings->wifi_hostname);
  }
  //initiate a wifi scan at boot to fill the wifi scan list
  WiFi.scanNetworksAsync(getWifiScanResults);
}

int handleFactoryReset(struct webserver_t *client) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else {
    switch(client->content) {
      case 0: {
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
      } break;
      case 1: {
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodyRebootWarning, strlen_P(webBodyRebootWarning));
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
    }

    /*
     * FIXME: Replace with timerqueue function
     */
    delay(1000);
    LittleFS.begin();
    LittleFS.format();
    WiFi.disconnect(true);
    delay(1000);
    ESP.restart();
  }

  return 0;
}

int handleReboot(struct webserver_t *client) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else {
    switch(client->content) {
      case 0: {
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
      } break;
      case 1: {
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodyRebootWarning, strlen_P(webBodyRebootWarning));
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
    }

    /*
     * FIXME: Replace with timerqueue function
     */
    delay(5000);
    ESP.restart();
  }
  return 0;
}

void settingsToJson(DynamicJsonDocument &jsonDoc, settingsStruct *heishamonSettings) {
  //set jsonDoc with current settings
  jsonDoc["wifi_hostname"] = heishamonSettings->wifi_hostname;
  jsonDoc["wifi_password"] = heishamonSettings->wifi_password;
  jsonDoc["wifi_ssid"] = heishamonSettings->wifi_ssid;
  jsonDoc["ota_password"] = heishamonSettings->ota_password;
  jsonDoc["mqtt_topic_base"] = heishamonSettings->mqtt_topic_base;
  jsonDoc["mqtt_server"] = heishamonSettings->mqtt_server;
  jsonDoc["mqtt_port"] = heishamonSettings->mqtt_port;
  jsonDoc["mqtt_username"] = heishamonSettings->mqtt_username;
  jsonDoc["mqtt_password"] = heishamonSettings->mqtt_password;
  if (heishamonSettings->use_1wire) {
    jsonDoc["use_1wire"] = "enabled";
  } else {
    jsonDoc["use_1wire"] = "disabled";
  }
  if (heishamonSettings->use_s0) {
    jsonDoc["use_s0"] = "enabled";
  } else {
    jsonDoc["use_s0"] = "disabled";
  }
  if (heishamonSettings->listenonly) {
    jsonDoc["listenonly"] = "enabled";
  } else {
    jsonDoc["listenonly"] = "disabled";
  }
  if (heishamonSettings->logMqtt) {
    jsonDoc["logMqtt"] = "enabled";
  } else {
    jsonDoc["logMqtt"] = "disabled";
  }
  if (heishamonSettings->logHexdump) {
    jsonDoc["logHexdump"] = "enabled";
  } else {
    jsonDoc["logHexdump"] = "disabled";
  }
  if (heishamonSettings->logSerial1) {
    jsonDoc["logSerial1"] = "enabled";
  } else {
    jsonDoc["logSerial1"] = "disabled";
  }
  if (heishamonSettings->optionalPCB) {
    jsonDoc["optionalPCB"] = "enabled";
  } else {
    jsonDoc["optionalPCB"] = "disabled";
  }
  jsonDoc["waitTime"] = heishamonSettings->waitTime;
  jsonDoc["waitDallasTime"] = heishamonSettings->waitDallasTime;
  jsonDoc["dallasResolution"] = heishamonSettings->dallasResolution;
  jsonDoc["updateAllTime"] = heishamonSettings->updateAllTime;
  jsonDoc["updataAllDallasTime"] = heishamonSettings->updataAllDallasTime;
}

void saveJsonToConfig(DynamicJsonDocument &jsonDoc) {
  if (LittleFS.begin()) {
    File configFile = LittleFS.open("/config.json", "w");
    if (configFile) {
      serializeJson(jsonDoc, configFile);
      configFile.close();
    }
  }
}

int saveSettings(struct webserver_t *client, settingsStruct *heishamonSettings) {
  const char *wifi_ssid = NULL;
  const char *wifi_password = NULL;
  const char *new_ota_password = NULL;
  const char *current_ota_password = NULL;
  const char *use_s0 = NULL;

  bool reconnectWiFi = false;
  DynamicJsonDocument jsonDoc(1024);

  settingsToJson(jsonDoc, heishamonSettings); //stores current settings in a json document

  struct websettings_t *tmp = websettings;
  while(tmp) {
    Serial.print(tmp->name);
    Serial.print(":");
    Serial.println(tmp->value);
    tmp = tmp->next;
  }

  jsonDoc["listenonly"] = String("");
  jsonDoc["logMqtt"] = String("");
  jsonDoc["logHexdump"] = String("");
  jsonDoc["logSerial1"] = String("");
  jsonDoc["optionalPCB"] = String("");
  jsonDoc["use_1wire"] = String("");
  jsonDoc["use_s0"] = String("");

  tmp = websettings;
  while(tmp) {
    if(strcmp(tmp->name.c_str(), "wifi_hostname") == 0) {
      jsonDoc["wifi_hostname"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "mqtt_topic_base") == 0) {
      jsonDoc["mqtt_topic_base"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "mqtt_server") == 0) {
      jsonDoc["mqtt_server"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "mqtt_port") == 0) {
      jsonDoc["mqtt_port"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "mqtt_username") == 0) {
      jsonDoc["mqtt_username"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "mqtt_password") == 0) {
      jsonDoc["mqtt_password"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "use_1wire") == 0) {
      jsonDoc["use_1wire"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "use_s0") == 0) {
      jsonDoc["use_s0"] = tmp->value;
      if(strcmp(tmp->value.c_str(), "enabled") == 0) {
        use_s0 = tmp->value.c_str();
      }
    } else if(strcmp(tmp->name.c_str(), "listenonly") == 0) {
      jsonDoc["listenonly"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "logMqtt") == 0) {
      jsonDoc["logMqtt"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "logHexdump") == 0) {
      jsonDoc["logHexdump"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "logSerial1") == 0) {
      jsonDoc["logSerial1"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "optionalPCB") == 0) {
      jsonDoc["optionalPCB"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "waitTime") == 0) {
      jsonDoc["waitTime"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "waitDallasTime") == 0) {
      jsonDoc["waitDallasTime"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "updateAllTime") == 0) {
      jsonDoc["updateAllTime"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "dallasResolution") == 0) {
      jsonDoc["dallasResolution"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "updataAllDallasTime") == 0) {
      jsonDoc["updataAllDallasTime"] = tmp->value;
    } else if(strcmp(tmp->name.c_str(), "wifi_ssid") == 0) {
      wifi_ssid = tmp->value.c_str();
    } else if(strcmp(tmp->name.c_str(), "wifi_password") == 0) {
      wifi_password = tmp->value.c_str();
    } else if(strcmp(tmp->name.c_str(), "new_ota_password") == 0) {
      new_ota_password = tmp->value.c_str();
    } else if(strcmp(tmp->name.c_str(), "current_ota_password") == 0) {
      current_ota_password = tmp->value.c_str();
    }
    tmp = tmp->next;
  }

  tmp = websettings;
  while(tmp) {
    if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_gpio") == 0) {
      jsonDoc["s0_1_gpio"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_ppkwh") == 0) {
      jsonDoc["s0_1_ppkwh"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_interval") == 0) {
      jsonDoc["s0_1_interval"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_minpulsewidth") == 0) {
      jsonDoc["s0_1_minpulsewidth"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_1_maxpulsewidth") == 0) {
      jsonDoc["s0_1_maxpulsewidth"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_gpio") == 0) {
      jsonDoc["s0_2_gpio"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_ppkwh") == 0) {
      jsonDoc["s0_2_ppkwh"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_ppkwh") == 0) {
      jsonDoc["s0_2_ppkwh"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_interval") == 0) {
      jsonDoc["s0_2_interval"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_minpulsewidth") == 0) {
      jsonDoc["s0_2_minpulsewidth"] = tmp->value;
    } else if(use_s0 != NULL && strcmp(tmp->name.c_str(), "s0_2_maxpulsewidth") == 0) {
      jsonDoc["s0_2_maxpulsewidth"] = tmp->value;
    }
    tmp = tmp->next;
  }

  while(websettings) {
    tmp = websettings;
    websettings = websettings->next;
    free(tmp);
  }

  if(new_ota_password != NULL && strlen(new_ota_password) > 0 && current_ota_password != NULL && strlen(current_ota_password) > 0) {
    if(strcmp(heishamonSettings->ota_password, current_ota_password) == 0) {
      jsonDoc["ota_password"] = new_ota_password;
    } else {
      client->route = 111;
      return 0;
    }
  }

  if(wifi_password != NULL && wifi_ssid != NULL && strlen(wifi_ssid) > 0 && strlen(wifi_password) > 0) {
    if(strcmp(jsonDoc["wifi_ssid"], wifi_ssid) != 0 || strcmp(jsonDoc["wifi_password"], wifi_password) != 0) {
      reconnectWiFi = true;
    }
  }
  if(wifi_ssid != NULL) {
    jsonDoc["wifi_ssid"] = String(wifi_ssid);
  }
  if(wifi_password != NULL) {
    jsonDoc["wifi_password"] = String(wifi_password);
  }

  serializeJson(jsonDoc, Serial);

  saveJsonToConfig(jsonDoc); //save to config file
  loadSettings(heishamonSettings); //load config file to current settings

  if(reconnectWiFi) {
    client->route = 112;
    return 0;
  }

  client->route = 113;
  return 0;
}

int cacheSettings(struct webserver_t *client, struct arguments_t * args) {
  struct websettings_t *tmp = websettings;
  while(tmp) {
    if(strcmp(tmp->name.c_str(), args->name) == 0) {
      char *cpy = (char *)malloc(args->len+1);
      memset(cpy, 0, args->len+1);
      memcpy(cpy, args->value, args->len);
      tmp->value += cpy;
      free(cpy);
      break;
    }
    tmp = tmp->next;
  }
  if(tmp == NULL) {
    websettings_t *node = new websettings_t;
    if(node == NULL) {
      Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
      ESP.restart();
      exit(-1);
    }
    node->next = NULL;
    node->name += args->name;

    if(args->value != NULL) {
      char *cpy = (char *)malloc(args->len+1);
      if(node == NULL) {
        Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
        ESP.restart();
        exit(-1);
      }
      memset(cpy, 0, args->len+1);
      strncpy(cpy, args->value, args->len);
      node->value += cpy;
      free(cpy);
    }

    node->next = websettings;
    websettings = node;
  }

  return 0;
}

int settingsNewPassword(struct webserver_t *client, settingsStruct *heishamonSettings) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else {
    switch(client->content) {
      case 0: {
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
      } break;
      case 1: {
        webserver_send_content_P(client, webBodySettings1, strlen_P(webBodySettings1));
        webserver_send_content_P(client, webBodySettingsResetPasswordWarning, strlen_P(webBodySettingsResetPasswordWarning));
      } break;
      case 2: {
        webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
      case 3: {
        setupConditionals();
      } break;
    }
  }

  return 0;
}

int settingsReconnectWifi(struct webserver_t *client, settingsStruct *heishamonSettings) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else {
    switch(client->content) {
      case 0: {
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
      } break;
      case 1: {
        webserver_send_content_P(client, webBodySettings1, strlen_P(webBodySettings1));
        webserver_send_content_P(client, settingsForm, strlen_P(settingsForm));
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
      } break;
      case 2: {
        webserver_send_content_P(client, webBodySettingsNewWifiWarning, strlen_P(webBodySettingsNewWifiWarning));
        webserver_send_content_P(client, refreshMeta, strlen_P(refreshMeta));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
      case 3: {
        setupWifi(heishamonSettings);
      } break;
    }
  }

  return 0;
}

int getSettings(struct webserver_t *client, settingsStruct *heishamonSettings) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"application/json", 0);
  } else {
    switch(client->content) {
      case 0: {
        webserver_send_content_P(client, PSTR("{\"wifi_hostname\":\""), 18);
        webserver_send_content(client, heishamonSettings->wifi_hostname, strlen(heishamonSettings->wifi_hostname));
        webserver_send_content_P(client, PSTR("\",\"wifi_ssid\":\""), 15);
        webserver_send_content(client, heishamonSettings->wifi_ssid, strlen(heishamonSettings->wifi_ssid));
      } break;
      case 1: {
        webserver_send_content_P(client, PSTR("\",\"wifi_password\":\""), 19);
        webserver_send_content(client, heishamonSettings->wifi_password, strlen(heishamonSettings->wifi_password));
        webserver_send_content_P(client, PSTR("\",\"current_ota_password\":\""), 26);
        webserver_send_content_P(client, PSTR("\",\"new_ota_password\":\""), 22);
      } break;
      case 2: {
        webserver_send_content_P(client, PSTR("\",\"mqtt_topic_base\":\""), 21);
        webserver_send_content(client, heishamonSettings->mqtt_topic_base, strlen(heishamonSettings->mqtt_topic_base));
        webserver_send_content_P(client, PSTR("\",\"mqtt_server\":\""), 17);
        webserver_send_content(client, heishamonSettings->mqtt_server, strlen(heishamonSettings->mqtt_server));
      } break;
      case 3: {
        webserver_send_content_P(client, PSTR("\",\"mqtt_port\":\""), 15);
        webserver_send_content(client, heishamonSettings->mqtt_port, strlen(heishamonSettings->mqtt_port));
        webserver_send_content_P(client, PSTR("\",\"mqtt_username\":\""), 19);
        webserver_send_content(client, heishamonSettings->mqtt_username, strlen(heishamonSettings->mqtt_username));
      } break;
      case 4: {
        webserver_send_content_P(client, PSTR("\",\"mqtt_password\":\""), 19);
        webserver_send_content(client, heishamonSettings->mqtt_password, strlen(heishamonSettings->mqtt_password));
        webserver_send_content_P(client, PSTR("\",\"waitTime\":"), 13);

        char str[20];
        itoa(heishamonSettings->waitTime, str, 10);
        webserver_send_content(client, str, strlen(str));
      } break;
      case 5: {
        char str[20];
        webserver_send_content_P(client, PSTR(",\"updateAllTime\":"), 17);

        itoa(heishamonSettings->updateAllTime, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"listenonly\":"), 14);

        itoa(heishamonSettings->listenonly, str, 10);
        webserver_send_content(client, str, strlen(str));
      } break;
      case 6: {
        char str[20];
        webserver_send_content_P(client, PSTR(",\"logMqtt\":"), 11);

        itoa(heishamonSettings->logMqtt, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"logHexdump\":"), 14);

        itoa(heishamonSettings->logHexdump, str, 10);
        webserver_send_content(client, str, strlen(str));
      } break;
      case 7: {
        char str[20];
        webserver_send_content_P(client, PSTR(",\"logSerial1\":"), 14);

        itoa(heishamonSettings->logSerial1, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"optionalPCB\":"), 15);

        itoa(heishamonSettings->optionalPCB, str, 10);
        webserver_send_content(client, str, strlen(str));
      } break;
      case 8: {
        char str[20];
        webserver_send_content_P(client, PSTR(",\"use_1wire\":"), 13);

        itoa(heishamonSettings->use_1wire, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"waitDallasTime\":"), 18);

        itoa(heishamonSettings->waitDallasTime, str, 10);
        webserver_send_content(client, str, strlen(str));
      } break;
      case 9: {
        char str[20];
        webserver_send_content_P(client, PSTR(",\"updataAllDallasTime\":"), 23);

        itoa(heishamonSettings->updataAllDallasTime, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"dallasResolution\":"), 20);

        itoa(heishamonSettings->dallasResolution , str, 10);
        webserver_send_content(client, str, strlen(str));
      } break;
      case 10: {
        char str[20];
        webserver_send_content_P(client, PSTR(",\"use_s0\":"), 10);

        itoa(heishamonSettings->use_s0, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_1_gpio\":"), 13);

        int i = 0;

        itoa(heishamonSettings->s0Settings[i].gpiopin, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_1_ppkwh\":"), 14);

        itoa(heishamonSettings->s0Settings[i].ppkwh, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_1_interval\":"), 17);

        itoa(heishamonSettings->s0Settings[i].lowerPowerInterval, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_1_minpulsewidth\":"), 22);

        itoa(heishamonSettings->s0Settings[i].minimalPulseWidth, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_1_maxpulsewidth\":"), 22);

        itoa(heishamonSettings->s0Settings[i].maximalPulseWidth, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_1_minwatt\":"), 16);

        itoa((int) round((3600 * 1000 / heishamonSettings->s0Settings[i].ppkwh) / heishamonSettings->s0Settings[i].lowerPowerInterval), str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_2_gpio\":"), 13);
      } break;
      case 11: {
        char str[20];
        int i = 1;

        itoa(heishamonSettings->s0Settings[i].gpiopin, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_2_ppkwh\":"), 14);

        itoa(heishamonSettings->s0Settings[i].ppkwh, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_2_interval\":"), 17);

        itoa(heishamonSettings->s0Settings[i].lowerPowerInterval, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_2_minpulsewidth\":"), 22);

        itoa(heishamonSettings->s0Settings[i].minimalPulseWidth, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_2_maxpulsewidth\":"), 22);

        itoa(heishamonSettings->s0Settings[i].maximalPulseWidth, str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR(",\"s0_2_minwatt\":"), 16);

        itoa((int) round((3600 * 1000 / heishamonSettings->s0Settings[i].ppkwh) / heishamonSettings->s0Settings[i].lowerPowerInterval), str, 10);
        webserver_send_content(client, str, strlen(str));

        webserver_send_content_P(client, PSTR("}"), 1);
      } break;
    }
  }
  return 0;
}

int handleSettings(struct webserver_t *client) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else {
    switch(client->content) {
      case 0: {
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodySettings1, strlen_P(webBodySettings1));
      } break;
      case 1: {
        webserver_send_content_P(client, settingsForm, strlen_P(settingsForm));
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
        webserver_send_content_P(client, settingsJS, strlen_P(settingsJS));
        webserver_send_content_P(client, populatescanwifiJS, strlen_P(populatescanwifiJS));
      } break;
      case 2: {
        webserver_send_content_P(client, changewifissidJS, strlen_P(changewifissidJS));
        webserver_send_content_P(client, populategetsettingsJS, strlen_P(populategetsettingsJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
    }
  }
  return 0;
}

int handleWifiScan(struct webserver_t *client) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"application/json", 0);
  } else if(client->content == 0) {
    if(numSsid > 0) { //found wifi networks
      String httptext = "[";
      int indexes[numSsid];
      for(int i = 0; i < numSsid; i++) { //fill the sorted list with normal indexes first
        indexes[i] = i;
      }
      for(int i = 0; i < numSsid; i++) { //then sort
        for(int j = i + 1; j < numSsid; j++) {
          if(WiFi.RSSI(indexes[j]) > WiFi.RSSI(indexes[i])) {
            int temp = indexes[j];
            indexes[j] = indexes[i];
            indexes[i] = temp;
          }
        }
      }
      String ssid;
      for(int i = 0; i < numSsid; i++) { //then remove duplicates
        if(indexes[i] == -1) continue;
        ssid = WiFi.SSID(indexes[i]);
        for(int j = i + 1; j < numSsid; j++) {
          if (ssid == WiFi.SSID(indexes[j])) {
            indexes[j] = -1;
          }
        }
      }
      bool firstSSID = true;
      for(int i = 0; i < numSsid; i++) { //then output json
        if(indexes[i] == -1) {
          continue;
        }
        if(!firstSSID) {
          httptext = httptext + ",";
        }
        httptext = httptext + "{\"ssid\":\"" + WiFi.SSID(indexes[i]) + "\", \"rssi\": \"" + dBmToQuality(WiFi.RSSI(indexes[i])) + "%\"}";
        firstSSID = false;
      }
      httptext = httptext + "]";
      char *str = (char *)httptext.c_str();
      webserver_send_content(client, str, strlen(str));
    }
  }
  //initatie a new async scan for next try
  WiFi.scanNetworksAsync(getWifiScanResults);
  return 0;
}

int handleDebug(struct webserver_t *client, char *hex, byte hex_len) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else if(client->content == 0) {
    char log_msg[256];

    #define LOGHEXBYTESPERLINE 32
    for (int i = 0; i < hex_len; i += LOGHEXBYTESPERLINE) {
      char buffer [(LOGHEXBYTESPERLINE * 3) + 1];
      buffer[LOGHEXBYTESPERLINE * 3] = '\0';
      for (int j = 0; ((j < LOGHEXBYTESPERLINE) && ((i + j) < hex_len)); j++) {
        sprintf(&buffer[3 * j], PSTR("%02X "), hex[i + j]);
      }
      sprintf_P(log_msg, PSTR("data: %s\n"), buffer);
      webserver_send_content(client, log_msg, strlen_P(log_msg));
    }
  }
  return 0;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      break;
    case WStype_CONNECTED: {
      } break;
    case WStype_TEXT:
      break;
    case WStype_BIN:
      break;
    case WStype_PONG: {
      } break;
    default:
      break;
  }
}

int handleRoot(struct webserver_t *client, float readpercentage, int mqttReconnects, settingsStruct *heishamonSettings) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else {
    switch(client->content) {
      case 0: {
        webserver_send_content_P(client, webHeader, strlen_P(webHeader));
        webserver_send_content_P(client, webCSS, strlen_P(webCSS));
        webserver_send_content_P(client, webBodyStart, strlen_P(webBodyStart));
        webserver_send_content_P(client, webBodyRoot1, strlen_P(webBodyRoot1));
      } break;
      case 1: {
        webserver_send_content_P(client, heishamon_version, strlen_P(heishamon_version));
        webserver_send_content_P(client, webBodyRoot2, strlen_P(webBodyRoot2));
        if(heishamonSettings->use_1wire) {
          webserver_send_content_P(client, webBodyRootDallasTab, strlen_P(webBodyRootDallasTab));
        }
        if(heishamonSettings->use_s0) {
          webserver_send_content_P(client, webBodyRootS0Tab, strlen_P(webBodyRootS0Tab));
        }
        webserver_send_content_P(client, webBodyRootS0Tab, strlen_P(webBodyRootS0Tab));
        webserver_send_content_P(client, webBodyRootConsoleTab, strlen_P(webBodyRootConsoleTab));
      } break;
      case 2: {
        webserver_send_content_P(client, webBodyEndDiv, strlen_P(webBodyEndDiv));
        webserver_send_content_P(client, webBodyRootStatusWifi, strlen_P(webBodyRootStatusWifi));
        char str[200];
        itoa(getWifiQuality(), str, 10);
        webserver_send_content(client, (char *)str, strlen(str));
        webserver_send_content_P(client, webBodyRootStatusMemory, strlen_P(webBodyRootStatusMemory));
      } break;
      case 3: {
        char str[200];
        itoa(getFreeMemory(), str, 10);
        webserver_send_content(client, (char *)str, strlen(str));
        webserver_send_content_P(client, webBodyRootStatusReceived, strlen_P(webBodyRootStatusReceived));
        str[200];
        itoa(readpercentage, str, 10);
        webserver_send_content(client, (char *)str, strlen(str));
      } break;
      case 4: {
        webserver_send_content_P(client, webBodyRootStatusReconnects, strlen_P(webBodyRootStatusReconnects));
        char str[200];
        itoa(mqttReconnects, str, 10);
        webserver_send_content(client, (char *)str, strlen(str));
        webserver_send_content_P(client, webBodyRootStatusUptime, strlen_P(webBodyRootStatusUptime));
        char *up = getUptime();
        webserver_send_content(client, up, strlen(up));
        free(up);
      } break;
      case 5: {
        webserver_send_content_P(client, webBodyEndDiv, strlen_P(webBodyEndDiv));
        webserver_send_content_P(client, webBodyRootHeatpumpValues, strlen_P(webBodyRootHeatpumpValues));
        if(heishamonSettings->use_1wire) {
          webserver_send_content_P(client, webBodyRootDallasValues, strlen_P(webBodyRootDallasValues));
        }
        if(heishamonSettings->use_s0) {
          webserver_send_content_P(client, webBodyRootS0Values, strlen_P(webBodyRootS0Values));
        }
        webserver_send_content_P(client, webBodyRootConsole, strlen_P(webBodyRootConsole));
        webserver_send_content_P(client, menuJS, strlen_P(menuJS));
      } break;
      case 6: {
        webserver_send_content_P(client, refreshJS, strlen_P(refreshJS));
        webserver_send_content_P(client, selectJS, strlen_P(selectJS));
        webserver_send_content_P(client, websocketJS, strlen_P(websocketJS));
        webserver_send_content_P(client, webFooter, strlen_P(webFooter));
      } break;
    }
  }
  return 0;
}

int handleTableRefresh(struct webserver_t *client, String actData[]) {
  int ret = 0;

  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"text/html", 0);
  } else {
    if(client->route == 11) {
      if(client->content == 0) {
        dallasTableOutput(client);
      }
    } else if(client->route == 12) {
      if(client->content == 0) {
        s0TableOutput(client);
      }
    } else if(client->route == 10) {
      if(client->content < NUMBER_OF_TOPICS) {
        for(uint8_t topic = client->content; topic < NUMBER_OF_TOPICS && topic < client->content + 4; topic++) {
          String topicdesc;
          const char *valuetext = "value";
          if(strcmp_P(valuetext, topicDescription[topic][0]) == 0) {
            topicdesc = topicDescription[topic][1];
          } else {
            int value = actData[topic].toInt();
            int maxvalue = atoi(topicDescription[topic][0]);
            if ((value < 0) || (value > maxvalue)) {
              topicdesc = _unknown;
            }
            else {
              topicdesc = topicDescription[topic][value + 1]; //plus one, because 0 is the maxvalue container
            }
          }

          webserver_send_content_P(client, PSTR("<tr><td>TOP"), 11);

          char str[12];
          itoa(topic, str, 10);
          webserver_send_content(client, str, strlen(str));

          webserver_send_content_P(client, PSTR("</td><td>"), 9);

          String t = topics[topic];
          char *tmp = (char *)t.c_str();
          webserver_send_content(client, tmp, strlen(tmp));

          webserver_send_content_P(client, PSTR("</td><td>"), 9);

          {
            char *str = (char *)actData[topic].c_str();
            webserver_send_content(client, str, strlen(str));
          }

          webserver_send_content_P(client, PSTR("</td><td>"), 9);

          {
            char *str = (char *)topicdesc.c_str();
            webserver_send_content(client, str, strlen(str));
          }

          webserver_send_content_P(client, PSTR("</td></tr>"), 10);
        }
        // The webserver also increases by 1
        client->content += 3;
      }
    }
  }
  return 0;
}

int handleJsonOutput(struct webserver_t *client, String actData[]) {
  if(client->step == WEBSERVER_CLIENT_SEND_HEADER) {
    webserver_send(client, 200, (char *)"application/json", 0);
  } else {
    if(client->content == 0) {
      webserver_send_content_P(client, PSTR("{\"heatpump\":["), 13);
    } else if(client->content < NUMBER_OF_TOPICS) {
      for(uint8_t topic = client->content; topic < NUMBER_OF_TOPICS && topic < client->content + 4; topic++) {
        PGM_P topicdesc;
        const char *valuetext = "value";
        if(strcmp_P(valuetext, topicDescription[topic][0]) == 0) {
          topicdesc = topicDescription[topic][1];
        } else {
          int value = actData[topic].toInt();
          int maxvalue = atoi(topicDescription[topic][0]);
          if ((value < 0) || (value > maxvalue)) {
            topicdesc = _unknown;
          } else {
            topicdesc = topicDescription[topic][value + 1]; //plus one, because 0 is the maxvalue container
          }
        }

        webserver_send_content_P(client, PSTR("{\"Topic\":\"TOP"), 14);

        {
          char str[12];
          itoa(topic, str, 10);
          webserver_send_content(client, str, strlen(str));
        }

        webserver_send_content_P(client, PSTR("\",\"Name\":\""), 10);

        webserver_send_content_P(client, topics[topic], strlen_P(topics[topic]));

        webserver_send_content_P(client, PSTR("\",\"Value\":\""), 11);

        {
          char *str = (char *)actData[topic].c_str();
          webserver_send_content_P(client, str, strlen(str));
        }

        webserver_send_content_P(client, PSTR("\",\"Description\":\""), 17);

        webserver_send_content_P(client, topicdesc, strlen_P(topicdesc));

        webserver_send_content_P(client, PSTR("\"}"), 2);

        if(topic < NUMBER_OF_TOPICS - 1) {
          webserver_send_content_P(client, PSTR(","), 1);
        }
      }
      // The webserver also increases by 1
      client->content += 3;
      if(client->content > NUMBER_OF_TOPICS) {
        client->content = NUMBER_OF_TOPICS;
      }
    } else if(client->content == NUMBER_OF_TOPICS+1) {
      webserver_send_content_P(client, PSTR("],\"1wire\":"), 10);

      dallasJsonOutput(client);
    } else if(client->content == NUMBER_OF_TOPICS+2) {
      webserver_send_content_P(client, PSTR(",\"s0\":"), 6);

      s0JsonOutput(client);

      webserver_send_content_P(client, PSTR("}"), 1);
    }
  }
  return 0;
}
