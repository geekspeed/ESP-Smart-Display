#include <Arduino.h>

#pragma region Constants

// Types
#define WIDGET_PROGMEM_IMAGE 0
#define WIDGET_FS_IMAGE 1
#define WIDGET_CLOCK 2
#define WIDGET_ANALOG_CLOCK 3
#define WIDGET_TEXT 4
#define WIDGET_TEXT_GET_JSON 5
#define WIDGET_TEXT_GET 6
#define WIDGET_WEATHER_ICON 7
#define WIDGET_SHAPE 8
#define FONT_TINY 0
#define FONT_TETRIS 1
#define FONT_GFX 2
#define IMAGE_UINT8 0
#define IMAGE_UINT16 1
#define IMAGE_GIMP 2
#define TEXT_NOWRAP 0
#define TEXT_WRAPPED 1
#define TEXT_WORD_WRAPPED 2
#define BRIGHTNESS_STATIC 0
#define BRIGHTNESS_AUTO 1
#define BRIGHTNESS_TIME 2
#define BRIGHTNESS_SUN 3
#define CLOCK_TYPE_24 0
#define CLOCK_TYPE_12 1
#define CLOCK_TYPE_12_PERIOD 2
#define STATE_NORMAL 0
#define STATE_UPDATING 1

// Configuration
#define MAX_CONFIG_FILE_SIZE 3000 // Max config file size in flash
#define CONFIG_JSON_BUFFER_SIZE 5000 // The buffer for configuration file parsing
#define JSON_REQUEST_BUFFER_SIZE 2000 // The JSON buffer for parsing JSON GET requests
#define MAX_IMAGE_DATA_FILE_SIZE 2000 // The maximum size for the image data file
#define IMAGE_JSON_BUFFER_SIZE 2500 // The buffer size for image data, increase if not seeing all uploaded images
#define WEATHER_UPDATE_INTERVAL 3600000 // Every Hour update the weather if using any weather widgets
#define NTP_UPDATE_INTERVAL 3700000 // Just over every hour to avoid large update delays
#define PIXEL_YIELD_THRESHOLD 100 // Might not be necisary, yields while drawign images larger than 10 by 10
#define BRIGHTNESS_UPDATE_INTERVAL 10 // Millis between brightness increments
#define SUN_UPDATE_INTERVAL 7200000 // Every 2 hours, since sun won't rise within 2 hours of date change
#define BRIGHTNESS_SENSOR_PIN A0 // The pin to get photoresistor value from, if using
#define BRIGHTNESS_ROLLING_AVG_SIZE 10 // Higher value makes transition smoother, 4 bytes per buffer value, zero for no buffer
#define HTTPS_TRANSMIT_BUFFER 512 // Limit HTTPS buffers to prevent HTTPS GET requests all failing
#define HTTPS_RECEIVE_BUFFER 1024

// Configuration Values
#ifndef USE_BRIGHTNESS_SENSOR
#define USE_BRIGHTNESS_SENSOR true // Disables Vcc reading functionality to use brightness sensor
#endif
#ifndef PROFILE_RENDERING
#define PROFILE_RENDERING false // Enable to log rendering times
#endif
#ifndef ENABLE_ARDUINO_OTA
#define ENABLE_ARDUINO_OTA false // LAN based OTA, but doesn't work for FS updates
#endif
#ifndef USE_DOUBLE_BUFFERING
#define USE_DOUBLE_BUFFERING true // Only disable if pressed for memory, will cause visual glitches/partial rendering
#endif
#ifndef CACHE_DASHBOARD
#define CACHE_DASHBOARD true // Shouldn't cause issues, with dashboard version checking
#endif
#ifndef USE_SUNRISE
#define USE_SUNRISE true // Disable to save space if Subrise brightness mode isn't needed
#endif
#ifndef USE_OTA_UPDATING
#define USE_OTA_UPDATING true // Disable to save space if OTA updating isn't needed
#endif
#ifndef USE_NTP
#define USE_NTP true // Disable to save space if OTA updating isn't needed
#endif
#ifndef USE_HTTPS
#define USE_HTTPS true // Disable if only http GET requests are needed space
#endif
#ifndef DELETE_TRANSPARENCY_BUFFER_ON_HTTPS
#define DELETE_TRANSPARENCY_BUFFER_ON_HTTPS false // Delete transparency buffer when making HTTPS requests if they are failing due to not enough memory
#endif
#ifndef USE_PROGMEM_IMAGES
#define USE_PROGMEM_IMAGES true // Disable if firmware images aren't needed to save space
#endif
#ifndef USE_WEATHER
#define USE_WEATHER true // Disable to save space if weather widgets aren't needed
#endif
#ifndef USE_TETRIS
#define USE_TETRIS false
#endif
#ifndef VERSION_CODE
#define VERSION_CODE 0
#endif
#ifndef BOARD_NAME
#ifdef ESP32
#define BOARD_NAME "generic-esp32" // Used to identify OTA update binaries
#elif
#define BOARD_NAME "generic-esp8266" // Used to identify OTA update binaries
#endif
#endif
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 64 // The width of the display
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 32 // The height of the display
#endif
#ifndef DISPLAY_PANELS
#define DISPLAY_PANELS 1 // The number of total panels being horizontally chained
#endif
#ifndef DISPLAY_ROW_PATTERN
#define DISPLAY_ROW_PATTERN 16 // The row pattern for the display
#endif
#if ENABLE_ARDUINO_OTA
#include <ArduinoOTA.h>
#endif
#ifdef DEBUGGING
#define DEBUG(fmt, ...)  Serial.printf_P((PGM_P)PSTR( "DEBUG: " fmt), ## __VA_ARGS__)
#else
#define DEBUG(...)
#endif

#pragma endregion

// Source files
#ifdef ESP32 // ESP32 Specific
#include <ESPAsyncTCP.h>
#include <esp_wifi.h>
#include <LITTLEFS.h>
#define LittleFS LITTLEFS
#define Dir File
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiMulti.h>
//#include <WiFiClientSecure.h>
//#define sint16_t signed short
//#define sint32_t signed long
#else // ESP8266 Specific
#include <ESPAsyncTCP.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#endif

// Common Libraries
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESP_DoubleResetDetector.h>
#include <DNSServer.h>
//#include <ESP_WiFiManager.h>
#include <ESPAsyncWiFiManager.h>
#if USE_NTP
#include <NTPClient.h>
#endif
#include <TimeLib.h>
#include <map>
#if USE_WEATHER
#include <JsonListener.h>
#include <OpenWeatherMapForecast.h>
#include "TinyIcons.h"
#endif
#include <Timezone.h>
#if USE_SUNRISE
#include <SunMoonCalc.h>
#endif
#if USE_TETRIS
#include <TetrisMatrixDraw.h>
#endif
#include "TinyFont.h"
#include "Structures.h"

// Images
#if USE_PROGMEM_IMAGES
#include "BLM.h"
#include "Taz.h"
#include "Mario.h"
#include "Youtube.h"
#endif

TimeChangeRule EDT = {"EDT", Second, Sun, Mar, 2, -240};    //Daylight time = UTC - 4 hours
TimeChangeRule EST = {"EST", First, Sun, Nov, 2, -300};     //Standard time = UTC - 5 hours
Timezone Eastern(EDT, EST);
TimeChangeRule CDT = {"CDT", Second, Sun, Mar, 2, -300};    //Daylight time = UTC - 5 hours
TimeChangeRule CST = {"CST", First, Sun, Nov, 2, -360};     //Standard time = UTC - 6 hours
Timezone Central(CDT, CST);
TimeChangeRule MDT = {"MDT", Second, Sun, Mar, 2, -360};    //Daylight time = UTC - 6 hours
TimeChangeRule MST = {"MST", First, Sun, Nov, 2, -420};     //Standard time = UTC - 7 hours
Timezone Mountain(MDT, MST);
TimeChangeRule PDT = {"PDT", Second, Sun, Mar, 2, -420};    //Daylight time = UTC - 7 hours
TimeChangeRule PST = {"PST", First, Sun, Nov, 2, -480};     //Standard time = UTC - 8 hours
Timezone Pacific(PDT, PST);

std::map<std::string, Timezone *> timezones = {{"eastern",  &Eastern},
                                               {"central",  &Central},
                                               {"mountain", &Mountain},
                                               {"pacific",  &Pacific}};

#pragma region PxMatrix
//#define PxMATRIX_COLOR_DEPTH 4
#if USE_DOUBLE_BUFFERING
#define double_buffer
#endif
#define PxMATRIX_MAX_HEIGHT DISPLAY_HEIGHT
#define PxMATRIX_MAX_WIDTH DISPLAY_WIDTH

#include <PxMatrix.h>

#ifdef ESP32
#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 26 // Use 26 instead of 2 since pin isn't broken out on NodeMCU 32s
TaskHandle_t displayUpdateTaskHandle = NULL;
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#else
#include <Ticker.h>
Ticker display_ticker;
#define P_LAT D0
#define P_A D1
#define P_B D2
#define P_C D8
#define P_D D6
#define P_E D3
#define P_OE D4
#endif

PxMATRIX display(DISPLAY_WIDTH, DISPLAY_HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

#ifdef ESP8266
// ISR for display refresh
void display_updater() {
  //display.displayTestPattern(70);
  display.display(70);
}
#else
void IRAM_ATTR display_updater(){
  portENTER_CRITICAL_ISR(&timerMux);
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  //notify task to unblock it 
  vTaskNotifyGiveFromISR(displayUpdateTaskHandle, &xHigherPriorityTaskWoken );

  //display task will be unblocked
  if(xHigherPriorityTaskWoken){
    //force context switch
    portYIELD_FROM_ISR( );
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}
 
void displayUpdateTask(void *) {
  for(;;){
    //block here untill timer ISR unblocks task
    if (ulTaskNotifyTake( pdTRUE, portMAX_DELAY))
        display.display(70);
  }
}

#endif
#pragma endregion

const uint16_t RED = display.color565(255, 0, 0);
const uint16_t GREEN = display.color565(0, 255, 0);
const uint16_t BLUE = display.color565(0, 0, 255);
const uint16_t WHITE = display.color565(255, 255, 255);
const uint16_t YELLOW = display.color565(255, 255, 0);
const uint16_t CYAN = display.color565(0, 255, 255);
const uint16_t MAGENTA = display.color565(255, 0, 255);
const uint16_t BLACK = display.color565(0, 0, 0);

class DisplayBuffer : public Adafruit_GFX {
    struct coords {
        int16_t x;
        int16_t y;
    };
    bool rotate;
    bool flip;
    std::unique_ptr <uint16_t> buffer;

public:
    DisplayBuffer(uint8_t width, uint8_t height) : Adafruit_GFX(width, height) {
        rotate = false;
        flip = false;
    }

    void setRotate(bool rotate) {
        this->rotate = rotate;
    }

    void setFlip(bool flip) {
        this->flip = flip;
    }

    coords transformCoords(int16_t x, int16_t y) {
        if (rotate) {
            int16_t temp_x = x;
            x = y;
            y = _height - 1 - temp_x;
        }
        // Todo: Verify transformations are correct
        if (flip)
            x = _width - 1 - x;

        if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height)) {
            DEBUG("DisplayBuffer: Transformed coordinates out of range: (%i, %i)\n", x, y);
            yield();
            return coords{0, 0};
        }
        return coords{x, y};
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) {
        if (!buffer) {
            DEBUG("DisplayBuffer: Buffer is not allocated");
            return;
        }
        //Serial.printf("Coordinates: (%i, %i)\n", x, y);
        coords c = transformCoords(x, y);
        if (c.y * _width + c.x >= _width * _height) {
            DEBUG("DisplayBuffer: index out of range");
            return;
        }
        buffer.get()[c.y * _width + c.x] = color;
    }

    void write(Adafruit_GFX &display, uint8_t xOff, uint8_t yOff, uint8_t width, uint8_t height) {
        if (!buffer) {
            DEBUG("DisplayBuffer: Buffer is  not allocated");
            return;
        }

        for (uint8_t x = xOff; x < xOff + width; x++) {
            for (uint8_t y = yOff; y < yOff + height; y++) {
                if (y * _width + x >= _width * _height) {
                    DEBUG("DisplayBuffer: index out of range");
                    return;
                }
                display.drawPixel(x, y, buffer.get()[y * _width + x]);
            }
            if (width * height > PIXEL_YIELD_THRESHOLD)
                yield();
        }
    }

    bool isAllocated() {
        return (bool)buffer;
    }

    bool allocate() {
        DEBUG("Allocating display buffer...\n");
        buffer.reset(new uint16_t[_width * _height]);
        if (!buffer) {
            Serial.println(F("Failed to allocate display buffer"));
            return false;
        }
        return true;
    }

    void close() {
        DEBUG("Freeing display buffer...\n");
        buffer.reset();
    }
};

std::map <std::string, ProgmemImage> progmemImages = {
#if USE_PROGMEM_IMAGES
                                                     {"taz",  {23, 28, 1,  IMAGE_UINT16, taz}},
                                                     {"blm",  {64, 32, 36, IMAGE_UINT8,  blmAnimations}},
                                                     {"mario",  {64, 32, 1, IMAGE_UINT16,  mario}},
                                                     {"youtube", {21, 16, 1, IMAGE_UINT16, youtube}}
#endif
                                                     };

// Configuration values
std::vector <Widget> widgets;
uint16_t backgroundColor;
uint8_t brightnessMode;
uint8_t brightnessBright;
uint16_t brightnessSensorDark;
uint16_t brightnessSensorBright;
uint8_t brightnessDim;
uint8_t brightnessBrightMin;
uint8_t brightnessBrightHour;
uint8_t brightnessDimMin;
uint8_t brightnessDimHour;
std::string localTimezone;
bool usingWeather;
bool usingTransparency;
bool metric;
bool fastUpdate;
String weatherKey;
String weatherLocation;
bool usingSunMoon;
double longitude;
double lattitude;

// State values
uint8_t dashboardVersion;
uint8_t state;
time_t weatherUpdateTime;
uint16_t weatherID = 200;
bool needsConfig; // If a configuration is needed
bool needsUpdate; // If a full update is needed
bool needsRestart; // If a system reboot is needed
uint8_t theHour12;
uint8_t theHour;
uint8_t theMinute;
time_t bootTime;
time_t brightnessTime;
time_t profileTime;
time_t sunMoonTime;
uint8_t targetBrightness;
uint8_t currentBrightness = 100;
time_t sunRiseTime;
time_t sunSetTime;
uint8_t scanPattern;
uint8_t muxPattern;
uint8_t muxDelay;

#if BRIGHTNESS_ROLLING_AVG_SIZE > 0
#include <RunningAverage.h>
RunningAverage brightnessAverage(BRIGHTNESS_ROLLING_AVG_SIZE);
#endif

DoubleResetDetector drd(10, 0);
AsyncWebServer server(80);
DNSServer dnsServer;

#if USE_NTP
NTPClient *ntpClient;
#endif
#if USE_WEATHER
OpenWeatherMapForecast weatherClient;
#endif
DisplayBuffer displayBuffer(DISPLAY_WIDTH, DISPLAY_HEIGHT);

#if !USE_BRIGHTNESS_SENSOR
ADC_MODE(ADC_VCC);
#endif

#pragma region Configuration

void writeDefaultConfig() {
    LittleFS.remove("/config.json");
    File file = LittleFS.open("/config.json", "w");
    file.print(F("{\"widgets\":[{\"xOff\":17,\"yOff\":11,\"type\":4,\"transparent\":false,\"font\":2,\"content\":\"Hello\",\"colors\":[\"0x000000\"],\"width\":30,\"height\":8,\"backgroundColor\":\"0x00FFFF\"}],\"brightnessMode\":0,\"brightnessLower\":1,\"brightnessUpper\":100,\"backgroundColor\":\"0x00FFFF\",\"timezone\":\"eastern\",\"metric\":false,\"weatherKey\":\"\",\"weatherLocation\":\"5014227\"}"));
    file.close();
}

uint16_t parseHexColorString(std::string s) {
    uint offset = s.substr(0, 2) == "0x" ? 2 : 0;
    if (s.length() - offset < 6) {
        Serial.print(F("Invalid HEX format: "));
        Serial.println(s.c_str());
        return 0;
    }
    return display.color565(strtoul(s.substr(offset, 2).c_str(), NULL, 16),
                            strtoul(s.substr(offset + 2, 2).c_str(), NULL, 16),
                            strtoul(s.substr(offset + 4, 2).c_str(), NULL, 16));
}

void closeFiles() {
    for (uint i = 0; i < widgets.size(); i++)
        widgets[i].file.close();
}

bool parseConfig(JsonVariant& json, String& errorString) {
    if (!json) {
        errorString.concat("Invalid Config");
        return false;
    }
    uint16_t tempBackgroundColor = json["backgroundColor"].isNull() ? 0 : parseHexColorString(
            json["backgroundColor"].as<char *>());
    String tempWeatherKey = json["weatherKey"].isNull() ? "" : String(json["weatherKey"].as<char *>());
    String tempWeatherLocation = json["weatherLocation"].isNull() ? "" : String(
            json["weatherLocation"].as<char *>());
    bool tempMetric = json["metric"];
    bool tempTransparency = false;
    bool tempFastUpdate = json["fastUpdate"];
    std::string tempTimezone = json["timezone"].isNull() ? "eastern" : std::string(json["timezone"].as<char *>());
    if (timezones.count(tempTimezone) == 0) {
        errorString.concat(F("Invalid timezone"));
        return false;
    }
    uint8_t tempBrightnessMode = json["brightnessMode"];
    bool tempUsingSunMoon = tempBrightnessMode == BRIGHTNESS_SUN;
    uint8_t tempBrightnessDim = json["brightnessLower"];
    uint8_t tempBrightnessBright = json["brightnessUpper"].isNull() ? 255 : json["brightnessUpper"];
    uint16_t tempBrightnessSensorBright = json["sensorBright"];
    uint16_t tempBrightnessSensorDark = json["sensorDark"];
    uint8_t tempBrightnessBrightMin;
    uint8_t tempBrightnessBrightHour;
    uint8_t tempBrightnessDimMin;
    uint8_t tempBrightnessDimHour;
    String brightTime = json["brightTime"].isNull() ? "09:30" : String(json["brightTime"].as<char*>());
    tempBrightnessBrightHour = brightTime.substring(0, 2).toInt();
    tempBrightnessBrightMin = brightTime.substring(3, 5).toInt();
    String darkTime = json["darkTime"].isNull() ? "20:30" : String(json["darkTime"].as<char*>());
    tempBrightnessDimHour = darkTime.substring(0, 2).toInt();
    tempBrightnessDimMin = darkTime.substring(3, 5).toInt();
    double tempLongitude = json["longitude"];
    double tempLattitude = json["lattitude"];
    uint8_t tempScanPattern = json["scanPattern"];
    uint8_t tempMuxPattern = json["muxPattern"];
    uint8_t tempMuxDelay = json["muxDelay"];

    bool tempUsingWeather = false;
    std::vector <Widget> tempWidgets;
    JsonArray configWidgets = json["widgets"];
    if (configWidgets) {
        for (ushort i = 0; i < configWidgets.size(); i++) {
            JsonObject widget = configWidgets[i];
            if (!widget || widget["width"].isNull() || widget["height"].isNull()) {
                errorString.concat("Failed to parse widget: ");
                errorString.concat(i);
                return false;
            }
            if (widget[F("disabled")])
                continue;
            if (widget[F("type")] == WIDGET_WEATHER_ICON)
                tempUsingWeather = true;

            if (widget["transparent"])
                tempTransparency = true;

            JsonArray a = widget["args"];
            std::vector <std::string> args;
            for (uint i = 0; i < a.size(); i++)
                args.push_back(a[i].isNull() ? "" : a[i].as<char *>());

            std::vector <uint16_t> colors;
            JsonArray c = widget[F("colors")];
            switch (widget["type"].as<uint8_t>()) {
                case WIDGET_ANALOG_CLOCK:
                    if (c.size() < 3) {
                        errorString.concat(F("Analog clocks require 2 color arguments"));
                        return false;
                    }
                    break;
                case WIDGET_CLOCK:
                    if (c.size() < 1) {
                        errorString.concat(F("Digital clocks require 1 color argument"));
                        return false;
                    }
                    break;
                case WIDGET_TEXT:
                case WIDGET_TEXT_GET_JSON:
                case WIDGET_TEXT_GET:
                    if (c.size() < 1) {
                        errorString.concat(F("Text require 1 color argument"));
                        return false;
                    }
                    break;
                default:
                    break;
            }
            for (uint i = 0; i < c.size(); i++)
                colors.push_back(parseHexColorString(std::string(c[i].as<char *>())));

            tempWidgets.push_back({widget["id"], widget["type"],
                                   widget["content"].isNull() ? "" : std::string(widget["content"].as<char *>()),
                                   widget["source"].isNull() ? "" : std::string(widget["source"].as<char *>()),
                                   widget["contentType"],
                                   widget["auth"].isNull() ? "" : std::string(widget["auth"].as<char *>()), args,
                                   widget["xOff"], widget["yOff"], widget["width"], widget["height"],
                                   widget["frequency"], widget["offset"], widget["length"], colors,
                                   widget["font"],
                                   widget["bordered"], widget["borderColor"].isNull() ? 0 : parseHexColorString(
                            widget["borderColor"].as<char *>()),
                                   widget["transparent"],
                                   widget["backgroundColor"].isNull() ? 0 : parseHexColorString(
                                           widget["backgroundColor"].as<char *>()), widget["background"]});
        }
        std::sort(tempWidgets.begin(), tempWidgets.end(), [](Widget w1, Widget w2) { return w1.id < w2.id; });
    }

    // Only load new configuration after successful completion of config parsing
    for (uint i = 0; i < widgets.size(); i++)
        widgets[i].file.close();

    needsUpdate = true;
    needsConfig = false;
    widgets = tempWidgets;
    brightnessDim = tempBrightnessDim;
    brightnessMode = tempBrightnessMode;
    brightnessBright = tempBrightnessBright;
    brightnessBrightHour = tempBrightnessBrightHour;
    brightnessBrightMin = tempBrightnessBrightMin;
    brightnessDimHour = tempBrightnessDimHour;
    brightnessDimMin = tempBrightnessDimMin;
    brightnessSensorBright = tempBrightnessSensorBright;
    brightnessSensorDark = tempBrightnessSensorDark;
    usingSunMoon = tempUsingSunMoon;
    backgroundColor = tempBackgroundColor;
    localTimezone = tempTimezone;
    usingWeather = tempUsingWeather;
    weatherLocation = tempWeatherLocation;
    weatherKey = tempWeatherKey;
    metric = tempMetric;
    fastUpdate = tempFastUpdate;
    usingTransparency = tempTransparency;
    lattitude = tempLattitude;
    longitude = tempLongitude;
    muxPattern = tempMuxPattern;
    scanPattern = tempScanPattern;
    muxDelay = tempMuxDelay;

    display.setFastUpdate(fastUpdate);
    display.setMuxPattern(static_cast<mux_patterns>(muxPattern));
    display.setScanPattern(static_cast<scan_patterns>(scanPattern));
    display.setMuxDelay(muxDelay, muxDelay, muxDelay, muxDelay, muxDelay);

    weatherUpdateTime = 0;
    sunMoonTime = 0;

    Serial.println(F("Successfully loaded configuration!"));
#ifdef ESP8266
    Serial.printf("Memory Free: %i, Fragmentation: %i%%\n", ESP.getFreeHeap(), ESP.getHeapFragmentation());
#endif
    return true;
}

bool getConfig() {
    File configFile = LittleFS.open("/config.json", "r");
    if (!configFile) {
        Serial.println(F("Failed to open config file"));
        return false;
    }

    size_t size = configFile.size();
    Serial.print(F("Configuration file size: "));
    Serial.println(size);
    if (size > MAX_CONFIG_FILE_SIZE) {
        Serial.println(F("Config file size is too large, replacing with default..."));
        configFile.close();
        writeDefaultConfig();
        return false;
    }

    DynamicJsonDocument doc(CONFIG_JSON_BUFFER_SIZE);
    DeserializationError deserializeError = deserializeJson(doc, configFile);
    configFile.close();
    if (deserializeError) {
        Serial.printf("Failed to parse config: %s\n, replacing with default...\n", deserializeError.c_str());
        writeDefaultConfig();
        return false;
    }
    
    String error;
    JsonVariant json = doc.as<JsonVariant>();
    if (!parseConfig(json, error)) {
        Serial.printf("Failed to parse config: %s\n, replacing with default...\n", error.c_str());
        writeDefaultConfig();
        return false;
    }

    return true;
}

#pragma endregion


#pragma region Web Server

void closeConnection(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200);
    response->addHeader("Connection", "close");
    request->send(response);
}

void onStartAccessPoint(AsyncWiFiManager *wiFiManager) {
    Serial.println(F("Entering AP config mode"));
    display.setCursor(0, 0);
    display.println(F("Wifi"));
    display.println(F("Config"));
    display.println(F("Mode"));
    display.showBuffer();
    needsConfig = true;
}

String getContentType(String filename) {
    if (filename.endsWith(F(".html"))) 
        return F("text/html");
    else if (filename.endsWith(F(".css"))) 
        return F("text/css");
    else if (filename.endsWith(F(".js"))) 
        return F("application/javascript");
    else if (filename.endsWith(F(".ico"))) 
        return F("image/x-icon");
    else if (filename.endsWith(F(".json"))) 
        return F("text/json");
    return F("text/plain");
}

int getHeaderCacheVersion(AsyncWebServerRequest *request) {
    if (!request->hasHeader(F("If-None-Match")))
        return -1;
    for (int i = 0; i < request->headers(); i++ )
        if (request->headerName(i).equals(F("If-None-Match")))
            return request->header(i).toInt();
    return -1;
}

bool sendFile(AsyncWebServerRequest *request, String path, bool cache = true) {
    if (path.endsWith(F("/")))
        return false;
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, getContentType(path));
    if (!response)
        return false;
    /*if (CACHE_DASHBOARD && cache) {
        if (getHeaderCacheVersion(request) == dashboardVersion) {
            request->send(304);
            return true;
        }
        response->addHeader("ETag", String(dashboardVersion, 10));
        response->addHeader(F("Cache-Control"), F("no-cache"));
    }*/
    request->send(response);
    return true;
}

void serveRoot(AsyncWebServerRequest *request) {
    if (!sendFile(request, F("/index.html")))
        request->send(200, "text/html", R"=====(
  <!DOCTYPE html>
  <html>
  <body>
  <strong>Dashboard is not installed</strong>
  <a href="https://github.com/TheLogicMaster/ESP-LED-Matrix-Display">Get Dashboard</a>
  </body>
  </html>
)=====");
}

void serveSaveConfig(AsyncWebServerRequest *request, JsonVariant &json) {
    DEBUG("SAVE CONFIG");
    if (displayBuffer.isAllocated())
            displayBuffer.close();
        for (uint i = 0; i < widgets.size(); i++) {
            widgets[i].file.close();
#if USE_TETRIS
            widgets[i].tetris.reset();
#endif
        }
        needsUpdate = true;

        String error;
        if (parseConfig(json, error)) {
            LittleFS.remove("/config.json");
            File file = LittleFS.open("/config.json", "w");
            serializeJson(json, file);
            file.close();
            Serial.println(F("Successfully updated config file"));
            request->send(200);
        } else {
            Serial.printf("Failed to update config file: %s\n", error.c_str());
            request->send(400, F("text/plain"), error);
        }
}

void serveConfig(AsyncWebServerRequest *request) {
    if (!sendFile(request, F("/config.json"), false)) {
        writeDefaultConfig();
        if (!sendFile(request, F("/config.json"), false))
            request->send(500, F("text/plain"), F("Config file error"));
    }
}

void serveFullUpdate(AsyncWebServerRequest *request) {
    needsUpdate = true;
    request->send(200);
}

void serveNotFound(AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
        request->send(200);
        return;
    }
  
    DEBUG("Not found: %s %s %s\n", request->url().c_str(), request->contentType().c_str(), request->methodToString());
    if (!sendFile(request, request->url()))
        request->send(404, "text/html", F("Not found"));
}

void serveRestart(AsyncWebServerRequest *request) {
    closeConnection(request);
    needsRestart = true;
}

void startOTA(AsyncWebServerRequest *request) {
    state = STATE_UPDATING;
    LittleFS.end();
    request->send(200);
}

void abortOTA(AsyncWebServerRequest *request) {
    state = STATE_NORMAL;
    LittleFS.begin();
    request->send(200);
    needsUpdate = true;
}

void serveStats(AsyncWebServerRequest *request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonVariant& json = response->getRoot();

#if USE_NTP
    json["uptime"] = ntpClient->getRawTime() - bootTime;
#endif
    char reason[25];
    json["version"] = VERSION_CODE;
#ifdef ESP8266
    json["resetReason"] = strcpy(reason, ESP.getResetReason().c_str());
    json["fragmentation"] = ESP.getHeapFragmentation();
    json["memoryFree"] = ESP.getFreeHeap();
    FSInfo info;
    LittleFS.info(info);
    json["filesystemUsed"] = info.usedBytes;
    json["filesystemTotal"] = info.totalBytes;
    json["maxOpenFiles"] = info.maxOpenFiles;
    json["maxPathLength"] = info.maxPathLength;
    json["vcc"] = ESP.getVcc();
#endif
    json["transparencyBuffer"] = displayBuffer.isAllocated();
    json["platform"] = BOARD_NAME;
    json["width"] = DISPLAY_WIDTH;
    json["height"] = DISPLAY_HEIGHT;
    json["brightness"] = currentBrightness;
    json["brightnessSensor"] = analogRead(BRIGHTNESS_SENSOR_PIN);
    response->setLength();
    request->send(response);
}

void writeDefaultImageData() {
    File dataFile = LittleFS.open(F("/imageData.json"), "w");
    dataFile.print("{}");
    dataFile.close();
}

std::map <std::string, FSImage> getFSImageData() {
    std::map <std::string, FSImage> data;

    File dataFile = LittleFS.open("/imageData.json", "r");
    if (!dataFile) {
        Serial.println(F("Failed to open image data file, writing default"));
        writeDefaultImageData();
        dataFile = LittleFS.open("/imageData.json", "r");
    }

    size_t size = dataFile.size();
    if (size > MAX_IMAGE_DATA_FILE_SIZE) {
        Serial.printf("Image data file size is too large(%i), replacing with default...\n", size);
        dataFile.close();
        writeDefaultImageData();
        dataFile = LittleFS.open("/imageData.json", "r");
    }

    std::unique_ptr<char[]> buf(new char[size]);

    dataFile.readBytes(buf.get(), size);
    dataFile.close();

    DynamicJsonDocument doc(IMAGE_JSON_BUFFER_SIZE);
    deserializeJson(doc, buf.get());
    for (auto const &entry : doc.as<JsonObject>())
        data[entry.key().c_str()] = {entry.value()["width"], entry.value()["height"], entry.value()["length"]};

    return data;
}

void writeFSImageData(std::map <std::string, FSImage> data) {
    File dataFile = LittleFS.open("/imageData.json", "w");
    if (!dataFile) {
        Serial.println(F("Failed to open Image Data file"));
        return;
    }
    DynamicJsonDocument doc(IMAGE_JSON_BUFFER_SIZE);
    for (auto const &entry : data) {
        JsonObject o = doc.createNestedObject(entry.first.c_str());
        o["width"] = entry.second.width;
        o["height"] = entry.second.height;
        o["length"] = entry.second.length;
    }
    std::unique_ptr<char[]> buf(new char[MAX_IMAGE_DATA_FILE_SIZE]);
    serializeJson(doc, buf.get(), MAX_IMAGE_DATA_FILE_SIZE);
    dataFile.print(buf.get());
    dataFile.close();
}

Dir getImageDir() {
    if (!LittleFS.exists("/images"))
        LittleFS.mkdir("/images");
#ifdef ESP32
    return LittleFS.open("/images");
#else
    return LittleFS.openDir("/images");
#endif
}

void serveImageData(AsyncWebServerRequest *request) {
    Dir imageDir = getImageDir();
    std::map <std::string, FSImage> fsImageData = getFSImageData();

    size_t size = 100 + (progmemImages.size() + fsImageData.size()) * 100;
    DynamicJsonDocument doc(size);

    for (auto const &entry : progmemImages) {
        JsonObject o = doc.createNestedObject(entry.first.c_str());
        o["width"] = entry.second.width;
        o["height"] = entry.second.height;
        o["length"] = entry.second.length;
        o["type"] = entry.second.type;
        o["progmem"] = true;
        }

    for (auto const &entry : fsImageData) {
        JsonObject o = doc.createNestedObject(entry.first.c_str());
        o["width"] = entry.second.width;
        o["height"] = entry.second.height;
        o["length"] = entry.second.length;
        o["progmem"] = false;
    }

    std::unique_ptr<char[]> buf(new char[size]);
    serializeJson(doc, buf.get(), size);
    request->send(200, F("text/json"), buf.get());
}

void serveUploadImage(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    for (uint i = 0; i < widgets.size(); i++)
        widgets[i].file.close();
    String imageFilename = "/images/" + request->arg("name");

    if (!index) {
        DEBUG("Uploading Image: %s\n", request->arg("name").c_str());
        request->_tempFile = LittleFS.open(imageFilename, "w");
    }

    if (request->_tempFile && request->_tempFile.write(data, len) != len) {
        Serial.printf("Error saving image: %i\n", request->_tempFile.getWriteError());
        request->_tempFile.close();
    }

    if (final) {
        if (request->_tempFile) {
            std::map <std::string, FSImage> fsImageData = getFSImageData();
            fsImageData[request->arg(F("name")).c_str()] = {strtoul(request->arg(F("width")).c_str(), NULL, 10),
                                                        strtoul(request->arg(F("height")).c_str(), NULL, 10),
                                                        strtoul(request->arg(F("length")).c_str(), NULL, 10)};
            writeFSImageData(fsImageData);
            needsUpdate = true;
        } else {
            request->send(400, F("text/plain"), F("Failed to save image"));
            if (LittleFS.exists(imageFilename))
                LittleFS.remove(imageFilename);
        }
    }
}

void serveImage(AsyncWebServerRequest *request) {
    String image = request->arg(F("image"));
    if (image.endsWith(F("_P"))) {
        std::string progmem = std::string(image.substring(0, image.length() - 2).c_str());
        if (progmemImages.count(progmem) < 1) {
            request->send(404);
            return;
        }
        size_t size = (progmemImages[progmem].type == IMAGE_GIMP ? 4 : 2) * progmemImages[progmem].width *
                    progmemImages[progmem].height * progmemImages[progmem].length;
        request->send_P(200, F("application/binary"), (uint8_t*) progmemImages[progmem].data, size);
    } else {
        if (!sendFile(request, "/images/" + image, false)) {
            DEBUG("Image doesn't exist: %s\n", image.c_str());
            request->send(404);
        }
    }
}

void serveRenameImage(AsyncWebServerRequest *request) {
    String name = request->arg(F("name"));
    String newName = request->arg(F("newName"));
    if (!LittleFS.exists("/images/" + name)) {
        DEBUG("Image to rename doesn't exist: %s\n", name.c_str());
        request->send(404);
    } else {
        if (LittleFS.exists("/images/" + newName))
            LittleFS.remove("/images/" + newName);
        LittleFS.rename("/images/" + name, "/images/" + newName);
        std::map <std::string, FSImage> fsImageData = getFSImageData();
        fsImageData[newName.c_str()] = fsImageData[name.c_str()];
        fsImageData.erase(name.c_str());
        writeFSImageData(fsImageData);
        request->send(200);
    }
}

void serveDeleteImage(AsyncWebServerRequest *request) {
    for (uint i = 0; i < widgets.size(); i++)
        widgets[i].file.close();
    std::string name = request->arg(F("name")).c_str();
    std::map <std::string, FSImage> fsImageData = getFSImageData();
    fsImageData.erase(name);
    std::string filename = "/images/" + name;
    if (LittleFS.exists(filename.c_str()))
        LittleFS.remove(filename.c_str());
    writeFSImageData(fsImageData);
    request->send(200);
}

void deleteAllImages() {
    for (uint i = 0; i < widgets.size(); i++)
        widgets[i].file.close();
    writeDefaultImageData();
    Dir dir = getImageDir();
    LittleFS.rmdir(F("/images"));
    LittleFS.mkdir(F("/images"));
#ifdef ESP8266
    std::vector <String> files;
    while (dir.next()) {
        Serial.println(dir.fileName().c_str());
        files.push_back(dir.fileName());
    }
    for (const auto &file: files)
        LittleFS.remove("/images/" + file);
#endif
}

void serveDeleteAllImages(AsyncWebServerRequest *request) {
    deleteAllImages();
    request->send(200);
}

void serveResetConfiguration(AsyncWebServerRequest *request) {
    writeDefaultConfig();
    closeConnection(request);
    needsRestart = true;
}

void serveOK(AsyncWebServerRequest *request) {
    request->send(200);
}

void serveFactoryReset(AsyncWebServerRequest *request) {
    writeDefaultConfig();
    deleteAllImages();
    AsyncWiFiManager wifiManager(&server, &dnsServer);
    wifiManager.resetSettings();
    needsRestart = true;
    closeConnection(request);
}

void serveOTA(AsyncWebServerRequest *request) {
    needsRestart = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", needsRestart ? "OK" : String(Update.getError(), 10));
    response->addHeader("Connection", "close");
    request->send(response);
}

void serveOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        Update.runAsync(true);
        if (filename.equals("firmware")) {
            if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000))
                Update.printError(Serial);
        } else {
            LittleFS.end();
            if (!Update.begin((size_t) &_FS_end - (size_t) &_FS_start, U_FS))
                Update.printError(Serial);
        }
    }
    if (!Update.hasError() && Update.write(data, len) != len)
      Update.printError(Serial);
    if (final) {
      if(Update.end(true))
        Serial.printf("Update Success: %uB\n", index+len);
      else
        Update.printError(Serial);
    }
}

void setupWebserver() {
    server.on("/", serveRoot);
    server.on("/index.html", serveRoot);
    server.on("/config", HTTP_GET, serveConfig);
    server.addHandler(new AsyncCallbackJsonWebHandler("/config", serveSaveConfig, MAX_CONFIG_FILE_SIZE));
    server.on("/update", HTTP_POST, serveOTA, serveOTAUpload);
    server.on("/abortUpdate", abortOTA);
    server.on("/beginUpdate", startOTA);
    server.on("/fullRefresh", serveFullUpdate);
    server.on("/restart", serveRestart);
    server.on("/stats", serveStats);
    server.on("/uploadImage", HTTP_POST, serveOK, serveUploadImage);
    server.on("/image", serveImage);
    server.on("/images", serveImageData);
    server.on("/deleteImage", serveDeleteImage);
    server.on("/deleteAllImages", serveDeleteAllImages);
    server.on("/renameImage", serveRenameImage);
    server.on("/factoryReset", serveFactoryReset);
    server.on("/resetConfiguration", serveResetConfiguration);
    server.serveStatic("/", LittleFS, "/", CACHE_DASHBOARD ? "no-cache" : __null);
    server.onNotFound(serveNotFound);
    Serial.println(F("Starting configuration webserver..."));
    server.begin();
}

#pragma endregion

#pragma region Rendering

bool checkAlphaColors(std::vector<uint16_t> alphaColors, uint16_t color) {
    for (uint i = 0; i < alphaColors.size(); i++)
        if (alphaColors[i] == color)
            return true;
    return false;
}

void drawImage(Adafruit_GFX &d, uint8_t xOffset, uint8_t yOffset, uint8_t width, uint8_t height, uint8_t offset,
                    uint8_t *data, std::vector<uint16_t> alphaColors, bool transparent, bool uInt16) {
    uint16_t line_buffer[width];
    for (uint8_t y = 0; y < height; y++) {
        memcpy_P(line_buffer, data + (offset * width * height + y * width) * 2, width * 2);
        for (uint8_t x = 0; x < width; x++) {
            if (transparent && checkAlphaColors(alphaColors, line_buffer[x]))
                continue;
            d.drawPixel(xOffset + x, yOffset + y, line_buffer[x]);
        }
        if (width * height > PIXEL_YIELD_THRESHOLD)
            yield();
    }
}

void drawImageFs(Adafruit_GFX &d, uint8_t xOffset, uint8_t yOffset, uint8_t width, uint8_t height, uint8_t offset,
                      const char name[], std::vector<uint16_t> alphaColors, bool transparent, File &file) {
    if (!file) {
        if (!LittleFS.exists(name)) {
            Serial.print(F("Couldn't find image to draw for: "));
            Serial.println(name);
            return;
        }
        Serial.printf("Opening File: %s\n", name);
        file = LittleFS.open(name, "r");
        if (!file) {
            Serial.printf("Failed to open image: %s\n", name);
            return;
        }
    }

    char line_buffer[width * 2];
    file.seek(offset * width * height * 2);
    for (uint8_t y = 0; y < height; y++) {
        file.readBytes(line_buffer, width * 2);
        for (uint8_t x = 0; x < width; x++) {
            uint16_t color = line_buffer[x * 2] | line_buffer[x * 2 + 1] << 8;
            if (transparent && checkAlphaColors(alphaColors, color))
                continue;
            d.drawPixel(xOffset + x, yOffset + y, color);
        }
        if (width * height > PIXEL_YIELD_THRESHOLD)
            yield();
    }
}

void
drawGimpImage(Adafruit_GFX &display, uint8_t xOff, uint8_t yOff, uint8_t width, uint8_t height, std::vector<uint16_t> alphaColors, bool transparent, char *data) {
    for (uint y = 0; y < height; y++) {
        for (uint x = 0; x < width; x++) {
            uint offset = (x + y * width) * 4;
            char c0 = data[offset];
            char c1 = data[offset + 1];
            char c2 = data[offset + 2];
            char c3 = data[offset + 3];
            uint8_t r = (((c0 - 33) << 2) | ((c1 - 33) >> 4));
            uint8_t g = ((((c1 - 33) & 0xF) << 4) | ((c2 - 33) >> 2));
            uint8_t b = ((((c2 - 33) & 0x3) << 6) | ((c3 - 33)));
            uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            if (transparent && checkAlphaColors(alphaColors, color))
                continue;
            display.drawPixel(x + xOff, y + yOff, color);
        }
        if (width * height > PIXEL_YIELD_THRESHOLD)
            yield();
    }
}

void drawAnalogClock(Adafruit_GFX &d, uint8_t xOffset, uint8_t yOffset, uint8_t radius, uint16_t colorMinute,
                     uint16_t colorHour, uint16_t colorMarking) {
    double minAngle = 2 * PI * (theMinute / 60. - .25);
    double hourAngle = 2 * PI * (theHour12 / 12. + theMinute / 720. - .25);
    for (uint i = 0; i < 12; i++) {
        double angle = 2 * PI * i / 12.;
        d.drawPixel(xOffset + round(cos(angle) * radius), yOffset + round(sin(angle) * radius), colorMarking);
    }
    d.drawLine(xOffset, yOffset,
               xOffset + round(cos(minAngle) * radius * .9),
               yOffset + round(sin(minAngle) * radius * .9), colorMinute);
    d.drawLine(xOffset, yOffset,
               xOffset + round(cos(hourAngle) * radius * .5),
               yOffset + round(sin(hourAngle) * radius * .5), colorHour);
}

String getTimeText(uint8_t type) {
    String text;
    uint8_t hourValue = type == CLOCK_TYPE_24 ? theHour : theHour12;
    if (hourValue < 10)
        text += "0";
    text += hourValue;
    text += ":";
    if (theMinute < 10)
        text += "0";
    text += theMinute;
    if (type == CLOCK_TYPE_12_PERIOD)
        text += theHour < 12 ? "AM" : "PM";
    return text;
}

void drawText(Adafruit_GFX &d, const char *text, uint8_t xOffset, uint8_t yOffset, uint8_t width, uint8_t height,
                 uint16_t color, uint8_t wrapping, uint8_t size) {
    d.setTextSize(size);
    d.setTextColor(color);
    d.setCursor(xOffset, yOffset);
    d.print(text);
}

void drawTinyText(Adafruit_GFX &display, const char text[], uint8_t x, uint8_t y, uint8_t width, uint8_t height,
                  uint16_t color, bool transparent, uint16_t backgroundColor, uint8_t wrapping) {
    TFDrawText(display, text, x, y, color, transparent, backgroundColor);
}

void drawTextWidget(Adafruit_GFX &d, Widget &widget) {
    if (widget.font >= FONT_GFX)
                drawText(d, widget.content.c_str(),
                        widget.xOff + _max(0, (widget.width - 6 * (int) widget.content.length() + 1) / 2),
                        widget.yOff + (widget.height - 7) / 2, widget.width, widget.height, widget.colors[0],
                        widget.contentType, widget.font - FONT_GFX + 1);
            else if (widget.font == FONT_TINY)
                drawTinyText(d, widget.content.c_str(),
                         widget.xOff + _max(0, (widget.width - (TF_COLS + 1) * (int) widget.content.length() + 1) / 2),
                         widget.yOff + (widget.height - TF_ROWS) / 2, widget.width, widget.height, widget.colors[0],
                         true, 0, widget.contentType);
#if USE_TETRIS
            else if (widget.tetris) {
                widget.tetris->setText(widget.content.c_str());
                uint x = widget.xOff + _max(0, (widget.width - (2 + TETRIS_DISTANCE_BETWEEN_DIGITS * widget.content.length())) / 2);
                uint y = widget.yOff + _max(0, (widget.height - 20) / 2);
                widget.finished = widget.tetris->drawText(x, y + 20);
                if (widget.type == WIDGET_CLOCK && widget.stateExtra) {
                    d.drawRect(x + 16, y + 13, 2, 2, widget.colors[0]);
                    d.drawRect(x + 16, y + 17, 2, 2, widget.colors[0]);
                }
            }
#endif
}

bool sendGetRequest(std::string url, std::string auth, uint16_t timeout, bool json, std::string &result) {
    if (!String(url.c_str()).startsWith(F("http"))) {
        Serial.println("Invalid GET protocol");
        return false;
    }
    bool ssl = String(url.c_str()).startsWith(F("https"));
    uint port = ssl ? 443 : 80;
    std::string address = ssl ? url.substr(8) : url.substr(7);
    uint addressEnd = address.find_first_of('/');
    std::string hostPort = address.substr(0, addressEnd);
    uint hostEnd = address.find_first_of(':');
    std::string host = hostPort.substr(0, hostEnd);
    if (hostEnd < hostPort.length())
        port = strtoul(hostPort.substr(hostEnd + 1).c_str(), NULL, 10);

    std::unique_ptr<WiFiClient> clientPtr;

    if (DELETE_TRANSPARENCY_BUFFER_ON_HTTPS && ssl)
        displayBuffer.close();

#if USE_HTTPS
    clientPtr.reset(ssl ? new WiFiClientSecure() : new WiFiClient());
#else
    if (ssl)
        return false;
    clientPtr.reset(new WiFiClient());
#endif
    WiFiClient* client = clientPtr.get();
    if (ssl) {
        // Impossible to send requests without limiting buffer sizes, though this may cause issues receiving responses if server doesn't support MFLN
#ifdef ESP8266
        WiFiClientSecure* secureClient = (WiFiClientSecure*)client;
        secureClient->setInsecure();
        secureClient->setBufferSizes(HTTPS_RECEIVE_BUFFER, HTTPS_TRANSMIT_BUFFER);
#endif
    }
    client->flush();
    client->setTimeout(timeout);
    DEBUG("Connecting to: %s\n", host.c_str());
	if (!client->connect(host.c_str(), port)){
        Serial.println(F("Failed to connect to GET address"));
        return false;
    }

    yield();

    client->print(F("GET "));
    client->print(address.substr(addressEnd).c_str());
    client->println(F(" HTTP/1.1"));
    client->print(F("Host: "));
    client->println(host.c_str());
    if (auth.length() > 0)
        client->println(("Authorization: " + auth).c_str());
    // client->println(F("Cache-Control: no-cache")); // Needed for such a request?
    if (client->println() == 0) {
        Serial.println(F("Failed to send request"));
        return false;
    }

    // Get status code
    int statusCode = -1;
    if(client->find("HTTP/")) {
        client->parseFloat();
        statusCode = client->parseInt();
    }
    if (statusCode != 200) {
        Serial.print(F("Unexpected HTTP status code: "));
        Serial.println(statusCode);
        return false;
    }

    // Skip headers
    bool body = false;
    while (client->available()) {
        String line = client->readStringUntil('\n');
        if (line.equals("\r")) {
            body = true;
            break;
        }
    }
    if (!body) {
        Serial.println("Failed to read GET response body");
        return false;
    }

    while (json && client->available() && client->peek() != '{') {
        char c = 0;
        client->readBytes(&c, 1);
        DEBUG("Unexpected body character: %c\n", c);
    }

    result += client->readString().c_str();
    DEBUG("GET Response Body: %s\n", result.c_str());
    return true;
}

const char *jsonVariantToString(JsonVariant &variant) {
    if (variant.is<char*>())
        return variant.as<char *>();
    else if (variant.is<bool>())
        return variant.as<bool>() ? "true" : "false";
    else if (variant.is<long>())
        return String(variant.as<long>(), 10).c_str();
        else if (variant.is<float>())
        return String(variant.as<float>(), 2).c_str();
    else {
        Serial.println("Unknown JSON type");
        return "";
    }
}

void ensureTetrisAllocated(Widget &widget) {
#if USE_TETRIS
    if (!widget.tetris) {
        widget.tetris.reset(new TetrisMatrixDraw(display));
        DEBUG("Allocating Tetris object\n");
    }
    if (!widget.tetris)
        DEBUG("Failed to create Tetris object\n");
#endif
}

void updateTextGETWidgetJson(Widget &widget) {
    std::string data;
    if (!sendGetRequest(widget.source, widget.auth, widget.length, true, data))
        return;

    DynamicJsonDocument doc(JSON_REQUEST_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, data);
    if (error) {
        Serial.print(F("Failed to deserialize response data: "));
        Serial.println(error.c_str());
        widget.content = "ERROR";
    } else {
        JsonVariant current;
        if (doc.is<JsonArray>())
            current = doc.as<JsonArray>();
        else
            current = doc.as<JsonObject>();
        if (!current) {
            Serial.println("Failed to parse JSON request root");
            return;
        }
        for (uint i = 0; i < widget.args.size(); i++) {
            if (current.is<JsonArray>())
                current = current[strtol(widget.args[i].c_str(), NULL, 10)];
            else
                current = current[widget.args[i].c_str()];
            if (!current) {
                Serial.printf("Failed to get JSON response value, key: %s\n", widget.args[i].c_str());
                return;
            }
        }
        String s = String(jsonVariantToString(current));
        if (widget.font >= FONT_GFX)
            s.toUpperCase();
        if (widget.font == FONT_TETRIS)
            ensureTetrisAllocated(widget);
        widget.content = std::string(s.c_str());
        widget.dirty = true;
    }
}

void updateTextGETWidget(Widget &widget) {
    std::string data;
    if (!sendGetRequest(widget.source, widget.auth, widget.length, false, data))
        return;
    String s = String(data.c_str());
    if (widget.font >= FONT_GFX)
        s.toUpperCase();
    if (widget.font == FONT_TETRIS)
            ensureTetrisAllocated(widget);
    widget.content = std::string(s.c_str());
    widget.dirty = true;
}

void incrementWidgetState(Widget &widget, uint8_t max) {
    if (max > 0) {
        widget.state++;
        widget.state %= max;
        widget.dirty = true;
    }
}

void incrementWidgetState(Widget &widget) {
    incrementWidgetState(widget, widget.length);
}

void updateClockWidget(Widget &widget) {
    if (widget.state != theMinute) {
        widget.state = theMinute;
        widget.dirty = true;
        widget.finished = false;
        if (widget.type != WIDGET_ANALOG_CLOCK)
            widget.content = getTimeText(widget.contentType).c_str();
    }

    time_t t = millis();
    if (widget.stateExtra != (bool)(t & 1024)) {
        widget.stateExtra = (bool)(t & 1024);
        widget.dirty = true;
        String temp = String(widget.content.c_str());
        if (widget.stateExtra || widget.font == FONT_TETRIS)
            temp.replace(':', ' ');
        else
            temp.replace(' ', ':');
        widget.content = temp.c_str();
    }

#if USE_TETRIS
    if (widget.font == FONT_TETRIS) {
        ensureTetrisAllocated(widget.tetris);
        if (widget.tetris) {
            if (!widget.finished)
                widget.dirty = true;
        }
    }
#endif
}

void updateWidget(Widget &widget) {
    switch (widget.type) {
        case WIDGET_PROGMEM_IMAGE:
        case WIDGET_FS_IMAGE:
            incrementWidgetState(widget);
            break;
        case WIDGET_TEXT_GET_JSON:
            updateTextGETWidgetJson(widget);
            break;
        case WIDGET_TEXT_GET:
            updateTextGETWidget(widget);
            break;
        case WIDGET_WEATHER_ICON:
            incrementWidgetState(widget, 5);
            break;
        case WIDGET_ANALOG_CLOCK:
        case WIDGET_CLOCK:
            updateClockWidget(widget);
            break;
        case WIDGET_TEXT:
        if (widget.font == FONT_TETRIS) {
            ensureTetrisAllocated(widget);
            if (!widget.finished)
                widget.dirty = true;
        }
            break;
        default:
            Serial.print(F("Invalid widget type to update: "));
            Serial.println(widget.type);
            break;
    }

    if (widget.background)
        needsUpdate = true;
    widget.lastUpdate = millis();
}

void updateWidgets() {
    for (uint i = 0; i < widgets.size(); i++)
        if (widgets[i].lastUpdate == 0 || widgets[i].updateFrequency > 0 && (millis() - widgets[i].lastUpdate > widgets[i].updateFrequency))
            updateWidget(widgets[i]);
}

void drawWidget(Adafruit_GFX &d, Widget &widget, bool buffering) {
    if (!usingTransparency || !widget.transparent) {
        if (widget.type == WIDGET_ANALOG_CLOCK)
            d.fillCircle(widget.xOff + widget.height / 2, widget.yOff + widget.height / 2, widget.height / 2,
                         widget.backgroundColor);
        else
            d.fillRect(widget.xOff, widget.yOff, widget.width, widget.height, widget.backgroundColor);
    } else if (!buffering)
        displayBuffer.write(display, widget.xOff, widget.yOff, widget.width, widget.height);


    switch (widget.type) {
        case WIDGET_PROGMEM_IMAGE:
            switch (progmemImages[widget.content].type) {
                case IMAGE_UINT8:
                case IMAGE_UINT16:
                    drawImage(d, widget.xOff + (widget.width - progmemImages[widget.content].width) / 2,
                                    widget.yOff + (widget.height - progmemImages[widget.content].height) / 2,
                                    progmemImages[widget.content].width, progmemImages[widget.content].height,
                                    widget.state + widget.offset, (uint8_t *) progmemImages[widget.content].data,
                                    widget.colors, widget.transparent, progmemImages[widget.content].type == IMAGE_UINT16);
                    break;
                case IMAGE_GIMP:
                    drawGimpImage(d, widget.xOff + (widget.width - progmemImages[widget.content].width) / 2,
                                  widget.yOff + (widget.height - progmemImages[widget.content].height) / 2,
                                  progmemImages[widget.content].width, progmemImages[widget.content].height,
                                  widget.colors, widget.transparent, (char *) progmemImages[widget.content].data);
                    break;
                default:
                    Serial.println(F("Unknown image type"));
                    break;
            }
            break;
        case WIDGET_FS_IMAGE:
            drawImageFs(d, widget.xOff, widget.yOff, widget.width, widget.height, widget.state + widget.offset,
                             ("/images/" + widget.content).c_str(), widget.colors, widget.transparent, widget.file);
            break;
        case WIDGET_ANALOG_CLOCK:
            drawAnalogClock(d, widget.xOff + widget.height / 2, widget.yOff + widget.height / 2,
                            widget.height / 2 - (widget.bordered ? 1 : 0), widget.colors[0], widget.colors[1],
                            widget.colors[2]);
            break;
        case WIDGET_TEXT_GET_JSON:
        case WIDGET_TEXT_GET:
        case WIDGET_TEXT:
        case WIDGET_CLOCK:
            drawTextWidget(d, widget);
            break;
        case WIDGET_WEATHER_ICON:
#if USE_WEATHER
            TIDrawIcon(d, weatherID, widget.xOff + _max(0, (widget.width - 10 + 1) / 2), widget.yOff + (widget.height - 5) / 2, widget.state, true, widget.transparent, widget.backgroundColor);
#endif
            break;
        default:
            Serial.print(F("Invalid widget type: "));
            Serial.println(widget.type);
            break;
    }

    if (widget.bordered) {
        if (widget.type == WIDGET_ANALOG_CLOCK)
            d.drawCircle(widget.xOff + widget.height / 2, widget.yOff + widget.height / 2, widget.height / 2,
                         widget.borderColor);
        else
            d.drawRect(widget.xOff, widget.yOff, widget.width, widget.height, widget.borderColor);
    }
}

void drawScreen(Adafruit_GFX &d, bool fullUpdate, bool buffering, bool finalize) {
    if (fullUpdate)
        d.fillScreen(backgroundColor);

    for (uint i = 0; i < widgets.size(); i++) {
        Widget &widget = widgets[i];
        if (buffering && !widget.background)
            continue;

        if (!fullUpdate && !widget.dirty)
            continue;

        profileTime = millis();
        drawWidget(d, widget, buffering);
        if (PROFILE_RENDERING)
            Serial.printf("Render time for %i: %lu\n", widget.id, millis() - profileTime);

        if (finalize)
            widget.dirty = false;
    }
}

void updateScreen(bool fullUpdate) {
    needsUpdate = false;
    updateWidgets();
    if (needsUpdate)
        return;
    if (usingTransparency && fullUpdate) {
        if (!displayBuffer.isAllocated() && !displayBuffer.allocate())
            Serial.println(F("Can't draw screen on null buffer"));
        else
            drawScreen(displayBuffer, fullUpdate, true, false);
    }
    if (USE_DOUBLE_BUFFERING) {
        drawScreen(display, fullUpdate, false, false);
        display.showBuffer();
    }
    drawScreen(display, fullUpdate, false, true);
}

#pragma endregion

// Takes total minutes in day as values
bool isTimeInRange(uint16_t startMins, uint16_t endMins, uint16_t testMins) {
    if (startMins > endMins)
        return testMins >= startMins || testMins < endMins;
    else
        return testMins >= startMins && testMins < endMins;
}

void updateBrightness() {
    uint16_t avg, constrained;
    switch (brightnessMode) {
        case BRIGHTNESS_AUTO:
            if(BRIGHTNESS_ROLLING_AVG_SIZE > 0) {
                brightnessAverage.addValue(analogRead(BRIGHTNESS_SENSOR_PIN));
                avg = brightnessAverage.getAverage();
            }
            else
                avg = analogRead(BRIGHTNESS_SENSOR_PIN);
            constrained = _min((uint16_t)_max(brightnessSensorDark, avg), brightnessSensorBright) - brightnessSensorDark;
            targetBrightness = constrained / ((float)_max(1, brightnessSensorBright - brightnessSensorDark)) * (float)(brightnessBright - brightnessDim) + brightnessDim;
            break;
        case BRIGHTNESS_STATIC:
            targetBrightness = brightnessBright;
            break;
        case BRIGHTNESS_TIME:
            targetBrightness = isTimeInRange(brightnessBrightHour * 60 + brightnessBrightMin, brightnessDimHour * 60 + brightnessDimMin, theHour * 60 + theMinute) ? brightnessBright : brightnessDim;
            break;
        case BRIGHTNESS_SUN:
            targetBrightness = now() > sunRiseTime && now() < sunSetTime ? brightnessBright : brightnessDim;
            break;
        default:
            Serial.print(F("Invalid brightness mode: "));
            Serial.println(brightnessMode);
            break;
    }
    if (currentBrightness != targetBrightness && millis() - brightnessTime > BRIGHTNESS_UPDATE_INTERVAL) {
        currentBrightness += targetBrightness > currentBrightness ? 1 : -1;
        display.setBrightness(currentBrightness);
        brightnessTime = millis();
    }
}

void updateWeather() {
    if (!usingWeather || weatherUpdateTime == 0 || millis() - weatherUpdateTime < WEATHER_UPDATE_INTERVAL)
        return;
    weatherUpdateTime = millis();

#if USE_WEATHER
    OpenWeatherMapForecastData data[1];
    weatherClient.setMetric(metric);
    weatherClient.setLanguage(F("en"));
    uint8_t allowedHours[] = {0, 12};
    weatherClient.setAllowedHours(allowedHours, 2);
    // See https://docs.thingpulse.com/how-tos/openweathermap-key/

    if (!weatherClient.updateForecastsById(data, weatherKey, weatherLocation, 1)) {
        Serial.println(F("Failed to update weather info"));
        return;
    }

    weatherID = data[0].weatherId;
#endif
}

void updateSunMoon() {
    if (!usingSunMoon || (sunMoonTime != 0 && millis() - sunMoonTime < SUN_UPDATE_INTERVAL))
        return;
    sunMoonTime = millis();

    #if USE_SUNRISE
    SunMoonCalc sunMoonCalc = SunMoonCalc(now(), lattitude, longitude);
    SunMoonCalc::Sun sun = sunMoonCalc.calculateSunAndMoonData().sun;
    sunRiseTime = timezones[localTimezone]->toLocal(sun.rise);
    sunSetTime = timezones[localTimezone]->toLocal(sun.set);
    #endif
}

void updateTime() {
#if USE_NTP
    ntpClient->update();
#endif
    theHour12 = hourFormat12();
    theHour = hour();
    theMinute = minute();
}

void setupArduinoOTA() {
#if ENABLE_ARDUINO_OTA
    Serial.println("Starting Development OTA Server");
    ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
      LittleFS.end();
    }
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
#endif
}

void updateArduinoOTA() {
#if ENABLE_ARDUINO_OTA
    ArduinoOTA.handle();
#endif
}

void setup() {
    Serial.begin(115200);
    Serial.println();

    display.begin(16);
#ifdef ESP8266
    display_ticker.attach(0.002, display_updater);
#endif
#ifdef ESP32
    xTaskCreatePinnedToCore(displayUpdateTask, "displayUpdateTask", 2048, NULL, 4, &displayUpdateTaskHandle, 1);
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &display_updater, true);
    timerAlarmWrite(timer, 2000, true);
    timerAlarmEnable(timer);;
#endif
    //display.setBrightness(1);
    display.setPanelsWidth(DISPLAY_PANELS);
    display.fillScreen(GREEN);
    display.showBuffer();
    display.fillScreen(GREEN);

    if (!LittleFS.begin()) {
        display.fillScreen(BLUE);
        display.print("FS ERROR");
        Serial.println("Failed to open filesystem");
    }
    File versionFile = LittleFS.open("/version.txt", "r");
    if (versionFile) {
        dashboardVersion = versionFile.parseInt();
       versionFile.close();
    }
    else
        Serial.println(F("Failed to read version file."));

    AsyncWiFiManager wifiManager(&server, &dnsServer);
    wifiManager.setAPCallback(onStartAccessPoint);

    if (drd.detectDoubleReset()) {
        Serial.println(F("Double reset detected, entering config mode"));
        wifiManager.startConfigPortal("LED Matrix Display");
    } else {
        Serial.println(F("Attemping to connect to network..."));
        display.print(F("Connecting..."));
        display.showBuffer();
        wifiManager.autoConnect("LED Matrix Display");
    }

    drd.stop();
    Serial.print(F("Connected to network with IP: "));
    Serial.println(WiFi.localIP());

    std::string result;
    Serial.println(result.c_str());

    // Development OTA
    setupArduinoOTA();

    // Webserver
    setupWebserver();

    // Development FS stuff
    //writeDefaultConfig();
    //LittleFS.remove("/config.json");

#if USE_NTP
    ntpClient = new NTPClient("time.nist.gov", 0, NTP_UPDATE_INTERVAL);
    ntpClient->begin();
    ntpClient->forceUpdate();
    bootTime = ntpClient->getRawTime();
#endif

    if (needsConfig || !getConfig()) {
        display.fillScreen(BLUE);
        display.setCursor(0, 0);
        display.println(F("Configure Display at"));
        drawTinyText(display, WiFi.localIP().toString().c_str(), 0, 17, 0, 0, WHITE, true, 0, 0);
        display.showBuffer();
        needsConfig = true;
        while (needsConfig) {
            updateArduinoOTA();
            yield();
        }
        Serial.println(F("Successfully configured display"));
    }

    display.setTextWrap(false);

#if USE_NTP
    setSyncProvider([]() { return timezones[localTimezone]->toLocal(ntpClient->getRawTime()); });
    setSyncInterval(10);
#endif
}

void loop() {
    if (needsRestart)
        ESP.restart();

    updateTime();
    updateWeather();
    updateSunMoon();
    updateBrightness();

    switch (state) {
        default:
            Serial.printf("Unknown state: %i/n", state);
        case STATE_NORMAL:
            updateScreen(needsUpdate);
            break;
        case STATE_UPDATING:
            display.fillScreen(BLACK);
            display.showBuffer();
            break;
    }

    updateArduinoOTA();
}
