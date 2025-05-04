#include <WiFi.h>
#include "FS.h"
#include "SD_MMC.h"
#include "esp_camera.h"
#include "esp_sleep.h"
#include <EEPROM.h>            // read and write from flash memory
#include <Preferences.h>
#include "RTClib.h"
#include <Wire.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

#define S_TO_uS_FACTOR 1000000  //Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  10 // set time for sleep and wakeup
#define TIME_TO_WAIT 4 // set time to wait for button press

// define number of bytes to set aside in persistant memory
#define EEPROM_SIZE 4
#define BUTTON 0
#define LED 4

#define AP_MODE 1
#define TRAILCAMERA_MODE 2

#define I2C_SDA 14
#define I2C_SCL 15

int pictureNumber = 0;
int sleep_time = TIME_TO_SLEEP  ;

const char* ssid = "Frontier TrailCam Access Point";
const char* password = "123456789"; //<--Placeholder password, change to whatever you desire :)
RTC_DS3231 rtc;
Preferences preferences ;

void startCameraServer();

/*
 * This function takes a picture and stores in a file
 */
static esp_err_t save_camera_image(fs::FS &fs, const char * path) {
    // Variable definitions for camera frame buffer
    camera_fb_t * fb = NULL;
    int64_t fr_start = esp_timer_get_time();

    // Get the contents of the camera frame buffer
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return ESP_FAIL;
    }

    // Save image to file
    size_t fb_len = 0;
    fb_len = fb->len;
    File file = fs.open(path, FILE_WRITE);
    if(file){
      file.write(fb->buf, fb->len); // payload (image), payload length
      file.close();
    } else {
      Serial.println("File save failed");
      return ESP_FAIL;      
    }

    // Release the camera frame buffer
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));

    pinMode(LED, OUTPUT);
    digitalWrite(LED,0);

    return ESP_OK;
}

//Function that prints the reason by which ESP32 has been awaken from sleep
void print_wakeup_reason(esp_sleep_wakeup_cause_t wakeup_reason){

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0: 
      Serial.println("Wakeup caused by external signal using RTC_IO"); 
      break;
    case ESP_SLEEP_WAKEUP_EXT1: 
      Serial.println("Wakeup caused by external signal using RTC_CNTL"); 
      break;
    case ESP_SLEEP_WAKEUP_TIMER: 
      Serial.println("Wakeup caused by ESP_SLEEP_WAKEUP_TIMER"); 
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: 
      Serial.println("Wakeup caused by ESP_SLEEP_WAKEUP_TOUCHPAD"); 
      break;
    case ESP_SLEEP_WAKEUP_ULP: 
      Serial.println("Wakeup caused by ESP_SLEEP_WAKEUP_ULP"); 
      break;
    default : Serial.println("Wakeup was not caused by deep sleep"); break;
  }
}

// initialize the camera
void initialize_camera(void){
  // Initial camera configuration
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; // This is very important for saving the frame buffer as a JPeg

  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  Serial.println("Camera initialized!");  
}

void initialize_sd_card(void){
  if(!SD_MMC.begin()){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD_MMC card attached");
    return;
  }
  Serial.println("SD Card Initialized");  
}

unsigned long calculateSleepTime(void){

  unsigned long start_time ;
  unsigned long current_time;  
  long timeDiff, sleep_time ;
  long incrementTime = 0;
  char frequency;

  frequency = preferences.getChar("frequency");
  
  switch(frequency){
    case 'M': // minute
      incrementTime = 60;
      break;
    case 'H': // hour
      incrementTime = 60*60;
      break;
    case 'd': // day
      incrementTime = 60*60*24;
      break;
    case 'w': // week
      incrementTime = 60*60*24*7;
      break;
    case 'm': // month
      incrementTime = 60*60*24*7*30;
      break;
    default:
      Serial.printf("calcuateSleepOnBoot %c not recognized", frequency);  
      break;
  }

  current_time = (unsigned long) time(NULL);
  start_time = (unsigned long) preferences.getULong64("start_time",0) ;
  timeDiff = start_time-current_time ;

  if(timeDiff<0) {
    sleep_time = incrementTime - (-timeDiff % incrementTime);
  } else {
    sleep_time = timeDiff ;
  }
  
  return (unsigned long)sleep_time ;
}

void trail_camera(void){

  unsigned long time_to_sleep ;
  int i ;
    
  while (1) {
    // initialize preferences
    struct timeval tv_now ;
    struct tm * timeinfo;
    char filename [80];

    time_to_sleep = calculateSleepTime();
  
    Serial.printf("sleep time: %lu\n", time_to_sleep);
    esp_sleep_enable_timer_wakeup(time_to_sleep*S_TO_uS_FACTOR);
    
    // capture 5 photos
    for (i=0;i<5;i++) {
      if(SD_MMC.begin()){
        uint8_t cardType = SD_MMC.cardType();
        
        if(cardType != CARD_NONE){
          Serial.println("SD Card good to go");  
          
          gettimeofday(&tv_now, NULL);
          timeinfo = localtime ((const time_t *)&tv_now);
          //strftime(filename, 80, "/img_%Y%m%d_%H%M%S.jpg",timeinfo);
          strftime(filename, 80, "/img_%d-%m-%Y_%H-%M-%S.jpg",timeinfo);//changed order of time in image address

          // Call function to capture the image and save it as a file
          if(save_camera_image(SD_MMC, filename) != ESP_OK ) {
            Serial.printf("Captured %s failure\n", filename);
            delay(1000); // wait for 1 second
          } else{
            Serial.printf("Captured %s success\n", filename);           
          }
        } else {
          Serial.println("Card Mount Failed");       
        }
        // wait a second!
        delay(1000);
        SD_MMC.end();
      } else {
        Serial.println("No SD card");  
      }
    }
    
    Serial.println("ESP32 going to sleep for " + String(time_to_sleep) + " Seconds");
    //Go to sleep now
    preferences.end();
    esp_deep_sleep_start();
  }
  
}

void update_image_settings(void) {

  int res = 0;
  int val ;

  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);//flip it back
    s->set_brightness(s, 1);//up the blightness just a bit
    s->set_saturation(s, -2);//lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  if (preferences.isKey("framesize")) {
    val = preferences.getUInt("framesize");
    s->set_framesize(s, (framesize_t)val);
    Serial.printf("framesize to %d\n",val);
    
  }
  if(preferences.isKey("quality")) {
    val = preferences.getUInt("quality");
    res = s->set_quality(s, val);
    Serial.printf("quality to %d\n",val);
  }
  
  if(preferences.isKey("contrast")) {
    val = preferences.getUInt("contrast");
    res = s->set_contrast(s, val);
    Serial.printf("contrast to %d\n",val);
  }
  if(preferences.isKey("brightness")) {
    val = preferences.getUInt("brightness");
    res = s->set_brightness(s, val);
    Serial.printf("brightness to %d\n",val);
  }
  if(preferences.isKey("saturation")) {
    val = preferences.getUInt("saturation");
    res = s->set_saturation(s, val);
    Serial.printf("saturation to %d\n",val);
  }
  if(preferences.isKey("gainceiling")) {
    val = preferences.getUInt("gainceiling");
    res = s->set_gainceiling(s, (gainceiling_t)val);
    Serial.printf("gainceiling to %d\n",val);
  }
  if(preferences.isKey("colorbar")) {
    val = preferences.getUInt("colorbar");
    res = s->set_colorbar(s, val);
     Serial.printf("colorbar to %d\n",val);
 }
  if(preferences.isKey("awb")) {
    val = preferences.getUInt("awb");
    res = s->set_whitebal(s, val);
  }
  if(preferences.isKey("agc")) {
    val = preferences.getUInt("agc");
    res = s->set_gain_ctrl(s, val);
    Serial.printf("agc to %d\n",val);
  }
  if(preferences.isKey("aec")) {
    val = preferences.getUInt("aec");
    res = s->set_exposure_ctrl(s, val);
  }
  if(preferences.isKey("hmirror")) {
    val = preferences.getUInt("hmirror");
    res = s->set_hmirror(s, val);
  }
  if(preferences.isKey("vflip")) {
    val = preferences.getUInt("vflip");
    res = s->set_vflip(s, val);
  }
  if(preferences.isKey("awb_gain")) {
    val = preferences.getUInt("awb_gain");
    res = s->set_awb_gain(s, val);
  }
  if(preferences.isKey("agc_gain")) {
    val = preferences.getUInt("agc_gain");
    res = s->set_agc_gain(s, val);
  }
  if(preferences.isKey("aec_value")) {
    val = preferences.getUInt("aec_value");
    res = s->set_aec_value(s, val);
  }
  if(preferences.isKey("aec2")) {
    val = preferences.getUInt("aec2");
    res = s->set_aec2(s, val);
  }
  if(preferences.isKey("dcw")) {
    val = preferences.getUInt("dcw");
    res = s->set_dcw(s, val);
  }
  if(preferences.isKey("bpc")) {
    val = preferences.getUInt("bpc");
    res = s->set_bpc(s, val);
  }
  if(preferences.isKey("wpc")) {
    val = preferences.getUInt("wpc");
    res = s->set_wpc(s, val);
  }
  if(preferences.isKey("raw_gma")) {
    val = preferences.getUInt("raw_gma");
    res = s->set_raw_gma(s, val);
  }
  if(preferences.isKey("lenc")) {
    val = preferences.getUInt("lenc");
    res = s->set_lenc(s, val);
  }
  if(preferences.isKey("special_effect")) {
    val = preferences.getUInt("special_effect");
    res = s->set_special_effect(s, val);
  }
  if(preferences.isKey("wb_mode")) {
    val = preferences.getUInt("wb_mode");
    res = s->set_wb_mode(s, val);
  }
  if(preferences.isKey("ae_level")) {
    val = preferences.getUInt("ae_level");
    res = s->set_ae_level(s, val);
  }
}

void run_ap(void) {
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");  
}

void set_time_from_rtc(void){
  DateTime now;
  struct tm tm;

  Wire.begin(I2C_SDA, I2C_SCL);
  rtc.begin();
  now = rtc.now();
  
  tm.tm_year = now.year() - 1900;
  tm.tm_mon = now.month(); 
  tm.tm_mday = now.day();
  tm.tm_hour = now.hour();
  tm.tm_min = now.minute(); 
  tm.tm_sec = now.second();
  time_t t = mktime(&tm);

  Serial.printf("Setting time from RTC: %s\n", asctime(&tm));

  struct timeval tv_now = { .tv_sec = t };

  settimeofday(&tv_now, NULL);
}

void setup() {
  camera_config_t config;
  unsigned long t ;
  int buttonState ;
  int state = TRAILCAMERA_MODE ;
  esp_sleep_wakeup_cause_t wakeup_reason;
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  preferences.begin("my−app", false);

  /* TODO - fix power supply 
   * STF - disable brown out detection 
   */
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  set_time_from_rtc();
  /*
   * Check whether the wakeup was not from the deep sleep timer. If not then check if user 
   * is reconfiguring the sytem by pushing the config button
   */
  wakeup_reason = esp_sleep_get_wakeup_cause();
  print_wakeup_reason(wakeup_reason);
  
  if(wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Enabling user to switch to AP by pushing the config button");
    pinMode(BUTTON, INPUT_PULLUP);
    t = millis() ;
    while (1) {
      buttonState = digitalRead(BUTTON);
      if (((millis()-t) / 1000) > TIME_TO_WAIT) {
        state = TRAILCAMERA_MODE ;
        Serial.println("timeout - trailcamera mode");
        break ;
      }
      if (buttonState == 0) {
        state = AP_MODE ;
        Serial.println("run AP");
        break ;
      }
    }
    delay(2000); // avoid crash to allow button to be released
  } else {
    Serial.println("run Trailcamera");
    state = TRAILCAMERA_MODE;
  }

  switch(state){
    case TRAILCAMERA_MODE:
      initialize_camera();
      update_image_settings();     
      trail_camera();
      break;
    case AP_MODE:
      run_ap();
      break;
    default:
      Serial.println("Error in device state");
      break;        
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(10000);
}
