//**********************************************************************************************************
//*    audioI2S-- I2S audiodecoder for ESP32,                                                              *
//**********************************************************************************************************
//
// first release on 11/2018
// Version 3  , Jul.02/2020
//
//
// THE SOFTWARE IS PROVIDED "AS IS" FOR PRIVATE USE ONLY, IT IS NOT FOR COMMERCIAL USE IN WHOLE OR PART OR CONCEPT.
// FOR PERSONAL USE IT IS SUPPLIED WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR
// OR COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE
//

#include "Arduino.h"
#include "WiFiMulti.h"
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <arduino-timer.h>  // https://www.arduino.cc/reference/en/libraries/arduino-timer/

// Digital I/O used
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC 26

Audio audio;
String ssid = "wifi_ssid";
String password = "password";

#define EOT 0x04
#define HMI Serial2
#define HMI_CMD(...) \
  { \
    HMI.printf(__VA_ARGS__); \
    HMI.write(EOT); \
  }
// Image ID of HMI
#define IMG_ID_PLAY 1
#define IMG_ID_STOP 5

// Disp volume contrl panel
bool disp_volpanel = false;
int durationTime = 0;
auto timer = timer_create_default();

// The algorithm is called “FNV-1a”
uint32_t hashString(const char *key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

void hmi_vsw_handler(const char *cmd) {
  if (disp_volpanel) {
    // Hide panVol
    HMI_CMD("panVol.anim_y(190, 200, 0,1);");
  } else {
    // Show panVol
    HMI_CMD("panVol.anim_y(130, 200, 0,1);");
  }
  disp_volpanel = !disp_volpanel;
  return;
}

void hmi_mp3_handler(const char *cmd) {
  audio.stopSong();
  audio.connecttoFS(SD, cmd);
}

void hmi_pre_handler(const char *cmd) {
  if (!audio.isRunning()) {
    return;
  }
  HMI_CMD("ptr(\"MP3:\",pl.selected_str());");
}

void hmi_nxt_handler(const char *cmd) {
  if (!audio.isRunning()) {
    return;
  }
  HMI_CMD("ptr(\"MP3:\",pl.selected_str());");
}

void hmi_ply_handler(const char *cmd) {
  if (audio.isRunning()) {
    audio.stopSong();
    HMI_CMD("imgPlay.src(%d);", IMG_ID_PLAY);
  } else {
    HMI_CMD("ptr(\"MP3:\",pl.selected_str());imgPlay.src(%d);", IMG_ID_STOP);
  }
}

// Volume control
void hmi_vol_handler(const char *cmd) {
  if (!audio.isRunning()) {
      return;
    }
    int val = strtoul(cmd, NULL, 0);
    audio.setVolume(val); 
    return;
}

// Play time control
void hmi_cue_handler(const char *cmd) {
  if (!audio.isRunning()) {
      return;
    }
    int val = strtoul(cmd, NULL, 0);
    int offset = val - audio.getAudioCurrentTime();    
    audio.setTimeOffset(offset); 
    return;
}

void hmi_spk_handler(const char *cmd) {  
    if(strlen(cmd) > 0){
      audio.stopSong();
      audio.connecttospeech(cmd, "en");
    }
}

// Define CMD table entry
#define HMI_CMD_LEN 4  // 4 bytes cmd length
typedef struct
{
  const char *cmd;
  void (*func)(const char *);
  uint32_t hash;
} HMI_CMD_t;

// Hmi CMD list table
HMI_CMD_t hmi_cmd_table[] = {
  { "VSW:", hmi_vsw_handler },  // Vol switch
  { "MP3:", hmi_mp3_handler },  //  Select mp3 file,
  { "PRE:", hmi_pre_handler },  //  Prev
  { "NXT:", hmi_nxt_handler },  //  Next
  { "PLY:", hmi_ply_handler },  //  Play
  { "SPK:", hmi_spk_handler },  //  Speak
  { "VOL:", hmi_vol_handler },  //  Set volume
  { "CUE:", hmi_cue_handler },  //  Set volume
  { NULL, NULL }                //  Table end,
};

// update HMI playlist
void updatePlaylist(File dir) {
  char buff[256];
  int cnt = 0;

  // Clear play list
  HMI_CMD("pl.options(\"\");");
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      // no more files
      break;
    }
    const char *f_name = entry.name();
    if (!entry.isDirectory()) {
      int f_name_len = strlen(f_name);
      if (f_name_len > 4) {
        // Check file ext name is .mp3 or not?
        if (strncmp(".mp3", &f_name[f_name_len - 4], 4) == 0) {
          if (cnt != 0) {
            strncat(buff, "\\n", sizeof(buff) - strlen(buff));
          }
          strncat(buff, f_name, sizeof(buff) - strlen(buff));
          cnt++;
        }
      }
    }
    entry.close();
  }
  HMI_CMD("pl.options(\"%s\");", buff);
}

/**
Run tick for every 100ms
*/
bool timer_tick(void *) {
  if (audio.isRunning()) {
    int durTime = audio.getAudioFileDuration();
    if (durTime > 0) {
      if (durationTime == 0) {
        durationTime = durTime;
        HMI_CMD("playTime.range(0,%d);", durationTime);
      }
      int curTime = audio.getAudioCurrentTime();
      HMI_CMD("playTime.val(%d); labDur.text(\"%d/%d\");", curTime, curTime, durationTime);
    }
  }
  return true;
}

// Init hmi cmd hash
void init_hmi_cmd_hash() {
  int i = 0;
  while (true) {
    if (hmi_cmd_table[i].cmd == NULL) {
      return;
    }
    hmi_cmd_table[i].hash = hashString(hmi_cmd_table[i].cmd, strlen(hmi_cmd_table[i].cmd));
    i++;
  }
}

void setup() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setFrequency(1000000);
  SD.begin(SD_CS);
  Serial.begin(115200);
  HMI.begin(115200);
  HMI.setTimeout(10);
  // Wait HMI bootup
  delay(100);
  // Send dummy cmd to make HMI clear cmd buffer
  HMI.printf("\x04\x04");
  // flush serial rx buffer
  char buff[256];
  int rxlen;
  do {
    rxlen = HMI.readBytes(buff, sizeof(buff));
  } while (rxlen > 0);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  int init_vol = 12;
  audio.setVolume(init_vol);  // 0...21

  // Reset HMI status
  HMI_CMD("playTime.val(0);");
  HMI_CMD("vol.range(0,21);vol.val(%d);", init_vol);
  HMI_CMD("imgPlay.src(%d);", IMG_ID_PLAY);

  File root = SD.open("/");
  updatePlaylist(root);

  // Init wifi and set labstat text
  HMI_CMD("labstat.text(\"Start Wifi...\");");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  int cnt = 0;
  // Connect to wifi AP
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    HMI_CMD("labstat.text(\"Wifi Conn:%d...\");", (cnt++) / 10);
  }
  HMI_CMD("labstat.text(\"Wifi Linked\");");
  
  // Cal hash of cmd table
  init_hmi_cmd_hash();
  timer.every(100, timer_tick);
}


/**
  * Description: Receive Data from HMI
  * Parameters:
  *  dat: Data buffer
  *  dat_len: Data buffer length
  * Return:
  *  length of rx bytes
*/
int rxHmiData(char *dat, int dat_len) {
  static char hmiBuff[256];
  static uint8_t hmiBuffLen = 0;

  if (!HMI.available()) {
    return 0;
  }
  int rxlen = HMI.readBytes(hmiBuff + hmiBuffLen, sizeof(hmiBuff) - hmiBuffLen);
  int i;
  hmiBuffLen += rxlen;
  for (i = hmiBuffLen - 1; i >= 0; i--) {
    if (hmiBuff[i] == EOT) {
      // Got EOT Byte
      hmiBuff[i++] = 0;  // Change EOT to NULL,  string  terminate
      int hmi_len = (i < dat_len) ? i : dat_len;
      // Copy hmiBuff to dat
      memcpy(dat, hmiBuff, hmi_len);
      // Move remain data to the head of hmiBuff
      int remain_len = 0;
      while (i < hmiBuffLen) {
        hmiBuff[remain_len] = hmiBuff[i];
        remain_len++;
        i++;
      }
      hmiBuffLen = remain_len;
      return hmi_len;
    }
  }
  return 0;
}

// Handle hmi cmd
void handle_hmi_cmd(const char *cmd) {
 
  int i = 0;  
  int hash = hashString(cmd, HMI_CMD_LEN);
  while (true) {
    if (hmi_cmd_table[i].cmd == NULL) {
      return;
    }
    if (hmi_cmd_table[i].hash == hash) {
      hmi_cmd_table[i].func(cmd + HMI_CMD_LEN);
      return;
    }
    i++;
  }
}


void loop() {
  char rxbuff[256];
  int rxlen;

  timer.tick();
  audio.loop();
  // Check Data from HMI
  rxlen = rxHmiData(rxbuff, sizeof(rxbuff));
  if (rxlen) {
    // Handle data from HMI
    handle_hmi_cmd(rxbuff);
  }

  if (Serial.available()) {  // put streamURL in serial monitor
    audio.stopSong();
    String r = Serial.readString();
    r.trim();
    if (r.length() > 5) audio.connecttohost(r.c_str());
    log_i("free heap=%i", ESP.getFreeHeap());
  }
}

// optional
void audio_info(const char *info) {
  Serial.print("info        ");
  Serial.println(info);
}
void audio_id3data(const char *info) {  //id3 metadata
  Serial.print("id3data     ");
  Serial.println(info);
}
void audio_eof_mp3(const char *info) {  //end of file
  HMI_CMD("imgPlay.src(%d);", IMG_ID_PLAY);
  Serial.print("eof_mp3     ");
  Serial.println(info);
}
void audio_showstation(const char *info) {
  Serial.print("station     ");
  Serial.println(info);
}
void audio_showstreamtitle(const char *info) {
  Serial.print("streamtitle ");
  Serial.println(info);
}
void audio_bitrate(const char *info) {
  Serial.print("bitrate     ");
  Serial.println(info);
}
void audio_commercial(const char *info) {  //duration in sec
  Serial.print("commercial  ");
  Serial.println(info);
}
void audio_icyurl(const char *info) {  //homepage
  Serial.print("icyurl      ");
  Serial.println(info);
}
void audio_lasthost(const char *info) {  //stream URL played
  Serial.print("lasthost    ");
  Serial.println(info);
}
