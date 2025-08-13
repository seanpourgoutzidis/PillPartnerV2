#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "time.h"
#include "driver/rtc_io.h"

const char* ssid = "yournetworkname";
const char* password = "yourpassword";

// Initialize Telegram BOT
String BOTtoken = "yourbottoken";  // your Bot Token (Get from Botfather)
String CHAT_ID = "yourchatid";

bool sendPhoto = false;

WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOTtoken, clientTCP);

bool flashState = LOW;

RTC_DATA_ATTR int bootCount = 0; //store bootcounts to know if we are in the initial boot

struct AppState {
  int daysSinceSunday;
  bool morningCheck;
  bool afternoonCheck;
  bool nightCheck;
};

RTC_DATA_ATTR AppState state = {0, false, false, false}; //store state in RTC to preserve through sleeping

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3600*4;
const int   daylightOffset_sec = 0;

// Pin definition for ESP WRover Kit
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      21
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       19
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM        5
#define Y2_GPIO_NUM        4
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Times To Check
#define MORNING_CHECK_HOUR    11
#define AFTERNOON_CHECK_HOUR  17
#define NIGHT_CHECK_HOUR      21

// Timer Config
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  1200       /* Time ESP32 will go to sleep (in seconds) */

// Pins for button and buzzer
#define WAKEUP_GPIO GPIO_NUM_33
#define BUZZER_GPIO GPIO_NUM_14

/*
  Method to handle the reason by which ESP32
  has been awaken from sleep
*/
void handle_wakeup_reason() {

  // Handle Messages Sent to Bot First
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    Serial.println("got response");
    handleNewMessages(numNewMessages);
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }

  // Get current time
  struct tm timeInfo;
  if(!getLocalTime(&timeInfo)){
    Serial.println("Failed to obtain time");
    return;
  }

  //If it is the next day, reset our state
  if (timeInfo.tm_wday != state.daysSinceSunday) {
    state.daysSinceSunday = timeInfo.tm_wday;
    state.morningCheck = false;
    state.afternoonCheck = false;
    state.nightCheck = false;
  }

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  String buttonUpdate = "";
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal using button press");
      if (!state.morningCheck) {
        buttonUpdate += "[Morning]: ";
        state.morningCheck = true;
      } else if (!state.afternoonCheck) {
        // Taken Afternoon pill prematurely (before noon)
        if (timeInfo.tm_hour < 12) {
          buttonUpdate += "Premature! \n";
        }
        buttonUpdate += "[Afternoon]: ";
        state.afternoonCheck = true;
      } else if (!state.nightCheck) {
        // Taken Night pill prematurely (before 5:00)
        if (timeInfo.tm_hour < 17) {
          buttonUpdate += "Premature! \n";
        }
        buttonUpdate += "[Night]: ";
        state.nightCheck = true;
      }

      buttonUpdate += "Pill Taken Button Pushed";

      //Send message and picture to Telegram
      bot.sendMessage(CHAT_ID, buttonUpdate, "");
      sendPhotoTelegram();

      break;
    case ESP_SLEEP_WAKEUP_TIMER:    
      Serial.println("Wakeup caused by timer");

      if (!state.morningCheck && timeInfo.tm_hour > MORNING_CHECK_HOUR) {
        bot.sendMessage(CHAT_ID, "Timer Reminder - Morning pill not taken");
        sendPhotoTelegram();
        state.morningCheck = true;
      } else if (state.morningCheck && !state.afternoonCheck && timeInfo.tm_hour > AFTERNOON_CHECK_HOUR) {
        bot.sendMessage(CHAT_ID, "Timer Reminder - Afternoon pill not taken");
        sendPhotoTelegram();
        state.afternoonCheck = true;
      } else if (state.morningCheck && state.afternoonCheck && !state.nightCheck && timeInfo.tm_hour > NIGHT_CHECK_HOUR) {
        bot.sendMessage(CHAT_ID, "Timer Reminder - Night pill not taken");
        sendPhotoTelegram();
        state.nightCheck = true;
      }

      break;
    default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void configInitCamera(){
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;  //0-63 lower number means higher quality
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
}

void handleNewMessages(int numNewMessages) {
  Serial.print("Handle New Messages: ");
  Serial.println(numNewMessages);

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);
    
    String from_name = bot.messages[i].from_name;
    if (text == "/start") {
      String welcome = "Welcome , " + from_name + "\n";
      welcome += "Use the following commands to interact with the ESP32-CAM \n";
      welcome += "/photo : takes a new photo\n";
      welcome += "/beep : plays a beep noise \n";
      bot.sendMessage(CHAT_ID, welcome, "");
    }
    if (text == "/beep") {
      Serial.println("Beeping");
      digitalWrite(BUZZER_GPIO, HIGH);
      delay(3000);
      digitalWrite(BUZZER_GPIO, LOW);
    }
    if (text == "/photo") {
      Serial.println("Responding to photo request");
      bot.sendMessage(CHAT_ID, "Photo Request Response", "");
      sendPhotoTelegram();
    }
  }
}

String sendPhotoTelegram() {
  const char* myDomain = "api.telegram.org";
  String getAll = "";
  String getBody = "";

  //Dispose first picture because of bad quality
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb); // dispose the buffered image
  
  // Take a new photo
  fb = NULL;  
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
    return "Camera capture failed";
  }  
  
  Serial.println("Connect to " + String(myDomain));


  if (clientTCP.connect(myDomain, 443)) {
    Serial.println("Connection successful");
    
    String head = "--PillPartnerV2\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + CHAT_ID + "\r\n--PillPartnerV2\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--PillPartnerV2--\r\n";

    size_t imageLen = fb->len;
    size_t extraLen = head.length() + tail.length();
    size_t totalLen = imageLen + extraLen;
  
    clientTCP.println("POST /bot"+BOTtoken+"/sendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(myDomain));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=PillPartnerV2");
    clientTCP.println();
    clientTCP.print(head);
  
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0;n<fbLen;n=n+1024) {
      if (n+1024<fbLen) {
        clientTCP.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        clientTCP.write(fbBuf, remainder);
      }
    }  
    
    clientTCP.print(tail);
    
    esp_camera_fb_return(fb);
    
    int waitTime = 10000;   // timeout 10 seconds
    long startTimer = millis();
    boolean state = false;
    
    while ((startTimer + waitTime) > millis()){
      Serial.print(".");
      delay(100);      
      while (clientTCP.available()) {
        char c = clientTCP.read();
        if (state==true) getBody += String(c);        
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } 
        else if (c != '\r')
          getAll += String(c);
        startTimer = millis();
      }
      if (getBody.length()>0) break;
    }
    clientTCP.stop();
    Serial.println(getBody);
  }
  else {
    getBody="Connected to api.telegram.org failed.";
    Serial.println("Connected to api.telegram.org failed.");
  }
  return getBody;
}

void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void setup(){
  // Get rid of brownout issues
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 

  // Init Serial Monitor
  Serial.begin(115200);

  // Config and init the camera
  configInitCamera();

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP());

  //Config Buzzer GPIO
  pinMode(BUZZER_GPIO, OUTPUT); 

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  // Setup GPIO Wakeup
  esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 1);  //1 = High, 0 = Low
  rtc_gpio_pullup_dis(WAKEUP_GPIO);
  rtc_gpio_pulldown_en(WAKEUP_GPIO);

  // Setup Timer Wakeup
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) + " Seconds");

  // This is the first boot
  if (bootCount == 1) {

    //Setup State based on current time
    struct tm timeInfo;
    while(!getLocalTime(&timeInfo)){
      Serial.println("Failed to obtain time");
    }

    printLocalTime();

    //Initialize state
    state.daysSinceSunday = timeInfo.tm_wday;
    if (timeInfo.tm_hour < MORNING_CHECK_HOUR) {
      state.morningCheck = false;
      state.afternoonCheck = false;
      state.nightCheck = false;
    } else if (timeInfo.tm_hour < AFTERNOON_CHECK_HOUR) {
      state.morningCheck = true;
      state.afternoonCheck = false;
      state.nightCheck = false;
    } else if (timeInfo.tm_hour < NIGHT_CHECK_HOUR) {
      state.morningCheck = true;
      state.afternoonCheck = true;
      state.nightCheck = false;
    } else {
      state.morningCheck = true;
      state.afternoonCheck = true;
      state.nightCheck = true;
    }
  }

  handle_wakeup_reason();

  //disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println("Going to sleep now");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
  //This will never run since we are in deep sleep
}