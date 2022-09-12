// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "camera_index.h"
#include "Arduino.h"
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include "RTClib.h"

#include "fb_gfx.h"

typedef struct {
        size_t size; //number of values used for filtering
        size_t index; //current value index
        size_t count; //value count
        int sum;
        int * values; //array to be filled with values
} ra_filter_t;

typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

//static mtmn_config_t mtmn_config = {0};
static int8_t detection_enabled = 0;
static int8_t recognition_enabled = 0;
static int8_t is_enrolling = 0;
// static face_id_list id_list = {0};
int8_t current_hour = 0;
int8_t current_min = 0;

extern Preferences preferences ;
extern RTC_DS3231 rtc;

static ra_filter_t * ra_filter_init(ra_filter_t * filter, size_t sample_size){
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if(!filter->values){
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t * filter, int value){
    if(!filter->values){
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->sum / filter->count;
}

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t out_len, out_width, out_height;
    uint8_t * out_buf;
    bool s;
    bool detected = false;
    int face_id = 0;

    size_t fb_len = 0;
    if(fb->format == PIXFORMAT_JPEG){
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
        jpg_chunking_t jchunk = {req, 0};
        res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
        httpd_resp_send_chunk(req, NULL, 0);
        fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];
    bool detected = false;
    int face_id = 0;
    int64_t fr_start = 0;
    int64_t fr_ready = 0;
    int64_t fr_face = 0;
    int64_t fr_recognize = 0;
    int64_t fr_encode = 0;

    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while(true){
        detected = false;
        face_id = 0;
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        } else {
            fr_start = esp_timer_get_time();
            fr_ready = fr_start;
            fr_face = fr_start;
            fr_encode = fr_start;
            fr_recognize = fr_start;

            if(fb->format != PIXFORMAT_JPEG){
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if(!jpeg_converted){
                    Serial.println("JPEG compression failed");
                    res = ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(fb){
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if(_jpg_buf){
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if(res != ESP_OK){
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t ready_time = (fr_ready - fr_start)/1000;
        int64_t face_time = (fr_face - fr_ready)/1000;
        int64_t recognize_time = (fr_recognize - fr_face)/1000;
        int64_t encode_time = (fr_encode - fr_recognize)/1000;
        int64_t process_time = (fr_encode - fr_start)/1000;
        
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
        Serial.printf("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps), %u+%u+%u+%u=%u %s%d\n",
            (uint32_t)(_jpg_buf_len),
            (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
            avg_frame_time, 1000.0 / avg_frame_time,
            (uint32_t)ready_time, (uint32_t)face_time, (uint32_t)recognize_time, (uint32_t)encode_time, (uint32_t)process_time,
            (detected)?"DETECTED ":"", face_id
        );
    }

    last_frame = 0;
    return res;
}

extern void initialize_camera(void);
extern void update_image_settings(void); 

static esp_err_t cmd_handler(httpd_req_t *req){
    char*  buf;
    size_t buf_len;
    char variable[32] = {0,};
    char value[32] = {0,};

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char*)malloc(buf_len);
        if(!buf){
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
            } else {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        } else {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int val = atoi(value);
    sensor_t * s = esp_camera_sensor_get();
    int res = 0;
    struct timeval tv_now ;
    struct tm * timeinfo;
    char buffer [80];
    
    gettimeofday(&tv_now, NULL);
    timeinfo = localtime ((const time_t *)&tv_now);
    strftime(buffer, 80, "%F-%X",timeinfo);
    Serial.printf("cmd called @ %s with %s = %s\n", buffer, variable, value);
    
    if(!strcmp(variable, "current_time")) {
      // Accepted format is 2022-06-12T22:30
      int y,m,d,H,M,c;
      struct tm tm;
      DateTime now ;

      Serial.printf("current time set to %s\n", value);
      sscanf(value,"%d%c%d%c%d%c%d%c%d", &y,&c,&m,&c,&d,&c,&H,&c,&M);   
      Serial.printf("y:%d m:%d d:%d H:%d M:%d\n", y,m,d,H,M);
      now = DateTime(y,m-1,d,H,M,0) ; // convert from month base Jan@1 to base Jan@0
            
      tm.tm_year = y - 1900;
      tm.tm_mon = m-1;
      tm.tm_mday = d;
      tm.tm_hour = H;
      tm.tm_min = M; 
      tm.tm_sec = 0;
      time_t t = mktime(&tm);

      struct timeval tv_now = { .tv_sec = t };

      settimeofday(&tv_now, NULL);

      esp_camera_deinit();
      Serial.printf("writing time\n");
      rtc.adjust(now);
      initialize_camera();
      update_image_settings();  
    }
    else if(!strcmp(variable, "start_time")) {
      int y,m,d,H,M,c;
      struct tm tm;
      
      sscanf(value,"%d%c%d%c%d%c%d%c%d", &y,&c,&m,&c,&d,&c,&H,&c,&M);   
      tm.tm_year = y - 1900;
      tm.tm_mon = m-1;
      tm.tm_mday = d;
      tm.tm_hour = H;
      tm.tm_min = M; 
      tm.tm_sec = 0;
      time_t t = mktime(&tm);

      preferences.putULong64("start_time", (uint64_t)t);
      Serial.printf("picture time set to %ld\n", (unsigned long)t);
    }
    else if(!strcmp(variable, "frequency")) {
      Serial.printf("freq set to %s\n", value);
      if (tolower(value[0]) == 'm' && tolower(value [1]) == 'o') { // Month
          preferences.putChar("frequency", 'm'); // using m from strftime method
      } else if (tolower(value[0]) == 'w' ) { // Week
          preferences.putChar("frequency", 'w'); // using w from strftime method        
      } else if (tolower(value[0]) == 'd' ) { // Day
          preferences.putChar("frequency", 'd'); // using d from strftime method        
      } else if (tolower(value[0]) == 'h' ) { // Hour
          preferences.putChar("frequency", 'H'); // using H from strftime method        
      } else if (tolower(value[0]) == 'm' && tolower(value[1]) == 'i') { // Minute
          preferences.putChar("frequency", 'M'); // using M from strftime method        
      } else { // Error
        Serial.println("freq error");
      }
    }
    else if(!strcmp(variable, "framesize")) {
        if(s->pixformat == PIXFORMAT_JPEG) { 
          res = s->set_framesize(s, (framesize_t)val);
          preferences.putUInt("framesize", val);
        }
    }
    else if(!strcmp(variable, "quality")) {
      res = s->set_quality(s, val);
      preferences.putUInt("quality", val);
    }
    else if(!strcmp(variable, "contrast")) { 
      res = s->set_contrast(s, val); 
      preferences.putUInt("contrast", val);
    }
    else if(!strcmp(variable, "brightness")) {
      res = s->set_brightness(s, val);
      preferences.putUInt("brightness", val);
    }
    else if(!strcmp(variable, "saturation")) { 
      res = s->set_saturation(s, val);
      preferences.putUInt("saturation", val);
    }
    else if(!strcmp(variable, "gainceiling")) {
      res = s->set_gainceiling(s, (gainceiling_t)val);
      preferences.putUInt("gainceiling", val);
    }
    else if(!strcmp(variable, "colorbar")) { 
      res = s->set_colorbar(s, val);
      preferences.putUInt("colorbar", val);
    }
    else if(!strcmp(variable, "awb")) { 
      res = s->set_whitebal(s, val);
      preferences.putUInt("awb", val);
    }
    else if(!strcmp(variable, "agc")) { 
      res = s->set_gain_ctrl(s, val);
      preferences.putUInt("agc", val);
   }
    else if(!strcmp(variable, "aec")) {
      res = s->set_exposure_ctrl(s, val);
      preferences.putUInt("aec", val);
    }
    else if(!strcmp(variable, "hmirror")) { 
      res = s->set_hmirror(s, val);
      preferences.putUInt("hmirror", val);
    }
    else if(!strcmp(variable, "vflip")) {
      res = s->set_vflip(s, val);
      preferences.putUInt("vflip", val);
    }
    else if(!strcmp(variable, "awb_gain")) {
      res = s->set_awb_gain(s, val);
      preferences.putUInt("awb_gain", val);
    }
    else if(!strcmp(variable, "agc_gain")) {
      res = s->set_agc_gain(s, val);
      preferences.putUInt("agc_gain", val);
    }
    else if(!strcmp(variable, "aec_value")) {
      res = s->set_aec_value(s, val);
      preferences.putUInt("aec_value", val);
    }
    else if(!strcmp(variable, "aec2")) {
      res = s->set_aec2(s, val);
      preferences.putUInt("aec2", val);
    }
    else if(!strcmp(variable, "dcw")) {
      res = s->set_dcw(s, val);
      preferences.putUInt("dcw", val);
    }
    else if(!strcmp(variable, "bpc")) {
      res = s->set_bpc(s, val);
      preferences.putUInt("bpc", val);
    }
    else if(!strcmp(variable, "wpc")) {
      res = s->set_wpc(s, val);
      preferences.putUInt("wpc", val);
    }
    else if(!strcmp(variable, "raw_gma")) {
      res = s->set_raw_gma(s, val);
      preferences.putUInt("raw_gma", val);
    }
    else if(!strcmp(variable, "lenc")) {
      res = s->set_lenc(s, val);
      preferences.putUInt("lenc", val);
    }
    else if(!strcmp(variable, "special_effect")) {
      res = s->set_special_effect(s, val);
      preferences.putUInt("special_effect", val);
    }
    else if(!strcmp(variable, "wb_mode")) {
      res = s->set_wb_mode(s, val);
      preferences.putUInt("wb_mode", val);
    }
    else if(!strcmp(variable, "ae_level")) {
      res = s->set_ae_level(s, val);
      preferences.putUInt("ae_level", val);
    }
    else if(!strcmp(variable, "face_detect")) {
        detection_enabled = val;
        if(!detection_enabled) {
            recognition_enabled = 0;
        }
    }
    else if(!strcmp(variable, "face_enroll")) {
      is_enrolling = val;
    }
    else if(!strcmp(variable, "face_recognize")) {
        recognition_enabled = val;
        if(recognition_enabled){
            detection_enabled = val;
        }
    }
    else {
        res = -1;
    }

    if(res){
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req){
    static char json_response[1024];

    sensor_t * s = esp_camera_sensor_get();
    char * p = json_response;
    *p++ = '{';

    p+=sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p+=sprintf(p, "\"quality\":%u,", s->status.quality);
    p+=sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p+=sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p+=sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p+=sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p+=sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p+=sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p+=sprintf(p, "\"awb\":%u,", s->status.awb);
    p+=sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p+=sprintf(p, "\"aec\":%u,", s->status.aec);
    p+=sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p+=sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p+=sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p+=sprintf(p, "\"agc\":%u,", s->status.agc);
    p+=sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p+=sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p+=sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p+=sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p+=sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p+=sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p+=sprintf(p, "\"vflip\":%u,", s->status.vflip);
    p+=sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p+=sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p+=sprintf(p, "\"colorbar\":%u,", s->status.colorbar);
    p+=sprintf(p, "\"face_detect\":%u,", detection_enabled);
    p+=sprintf(p, "\"face_enroll\":%u,", is_enrolling);
    p+=sprintf(p, "\"face_recognize\":%u", recognition_enabled);
    p+=sprintf(p, "\"frequency\":%c", (char) preferences.getChar("frequency",'x'));
    p+=sprintf(p, "\"start_time\":%lu", (unsigned long) preferences.getULong64("start_time",0));
    p+=sprintf(p, "\"current_time\":%lu", (unsigned long) time(NULL));
    
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t configure_handler(httpd_req_t *req){

    esp_camera_deinit();
  
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    return httpd_resp_send(req, (const char *)index_config_html_gz, index_config_html_gz_len);
}
//copy this part----
static esp_err_t eric_handler(httpd_req_t *req){

    esp_camera_deinit();
  
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    return httpd_resp_send(req, (const char *)eric_config_html_gz, eric_config_html_gz_len);
}
//----------------

//copied part----
static esp_err_t help_handler(httpd_req_t *req){

    esp_camera_deinit();
  
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    return httpd_resp_send(req, (const char *)help_config_html_gz, help_config_html_gz_len);
}
//----------------

//copied part#2----
static esp_err_t homepage_handler(httpd_req_t *req){

    esp_camera_deinit();
  
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    return httpd_resp_send(req, (const char *)homepage_config_html_gz, homepage_config_html_gz_len);
}
//----------------
extern void initialize_camera();
extern void update_image_settings();  
static esp_err_t index_handler(httpd_req_t *req){

    initialize_camera();
    update_image_settings();  
  
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    
    sensor_t * s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        return httpd_resp_send(req, (const char *)index_ov3660_html_gz, index_ov3660_html_gz_len);
    }
    return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}


void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri = {
        .uri       = "/stream.html",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t configure_uri = {
        .uri       = "/configure",
        .method    = HTTP_GET,
        .handler   = configure_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t eric_uri = {
        .uri       = "/settings.html",
        .method    = HTTP_GET,
        .handler   = eric_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };

   httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t help_uri = {
        .uri       = "/help.html",
        .method    = HTTP_GET,
        .handler   = help_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t homepage_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = homepage_handler,
        .user_ctx  = NULL
    };
    ra_filter_init(&ra_filter, 20);
        
    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &configure_uri);
        httpd_register_uri_handler(camera_httpd, &eric_uri);
        httpd_register_uri_handler(camera_httpd, &help_uri);
        httpd_register_uri_handler(camera_httpd, &homepage_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
