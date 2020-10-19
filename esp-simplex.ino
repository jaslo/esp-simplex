#include <stdarg.h>

/**
   The MIT License (MIT)

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/


/* controller for Simplex clock with 
 *  clutch relay 115AC (on white/green)
 *  clock power 115AC (on white/black)
 *  
 *  using ESP-01 lctech relay board
 *  wire the black wire power to NO on the relay, 
 *  the green wire from the clock to COM on the relay
 */
 
#include <ESP8266Wifi.h>
#include <WiFiUdp.h>

#include <Timezone.h>    // https://github.com/JChristensen/Timezone
#include <TimeLib.h>     // https://playground.arduino.cc/Code/Time
// #include <SoftwareSerial.h>

// WIFI configuration
#define WIFI_SSID "jaslonetn"
#define WIFI_KEY  "winwheel"

// NTP server name or IP, sync interval in seconds
static const char ntpServerName[] = "pool.ntp.org";
#define NTP_SYNC_INTERVAL 300

// Screensaver to save OLED
#define SCREENSAVER_TIMER 600

// Time Zone (DST) settings, change to your country
TimeChangeRule PDT = { "PDT", Last, Sun, Mar, 2, -420 }; // Central European Summer Time
TimeChangeRule PST =  { "PST ", Last, Sun, Oct, 3, -480 }; // Central European Standard Time
Timezone ClockTZ(PDT,PST);

unsigned int localPort = 8888;
WiFiUDP udp;
static const char hname[] = "esp-simplex-clock";

short state = 0;
int8_t show_impulse = 0;
time_t last_ntp_sync = 0;
time_t oled_activate = 0;
time_t last_t = 0;

          // GPIO3 (Rx), GPIO1 (Tx)
//SoftwareSerial swSer(3,1); // RX, TX


// for testing, enable the Serial println below to see diagnostic output
// on the serial port
//
void log_i(char* fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);
  char buffer[256];
  vsprintf (buffer, fmt, vargs);
  // Serial.println(buffer);
  va_end(vargs);
}

void setup() {
  delay(10000);
  Serial.begin(115200);

  log_i("starting up...");
  
  // swSer.begin(9600);
  
  // set hostname
  WiFi.hostname(hname);
  // connect to wifi
  log_i(console_text, "Connecting to wifi (%s)", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_KEY);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  log_i("Connected, IP address: %s", WiFi.localIP().toString().c_str());

  log_i("Starting UDP...");
  udp.begin(localPort);
  log_i("Waiting for sync");

  setSyncProvider(getNtpTime);
  setSyncInterval(NTP_SYNC_INTERVAL); // sync with NTP
  while (timeStatus() == timeNotSet) {
    delay(10);
  }
}

/* for multi-relay boards:
 *  1 on A0 01 01 A2
 *  1 off A0 01 00 A1
 *  2 on A0 02 01 A3
 *  2 off A0 02 00 A2
 *  3 on A0 03 01 A4
 *  3 off A0 03 00 A3
 *  4 on A0 04 01 A5
 *  4 off A0 04 00 A4
 */
 
char relay1on[] = {0xA0, 0x01, 0x01, 0xA2};
char relay1off[] = {0xA0, 0x01, 0x00, 0xA1};

/*-------- Main loop ----------*/
void loop() {
  time_t utc = now();
  time_t local_t = ClockTZ.toLocal(utc);
  int hour_12 = hour(local_t);
  if (hour_12 >= 12) hour_12 -= 12;
 
  // for testing, run this every five minutes!
  // if ((minute(local_t) % 5 == 0) && (second(local_t) == 54)) {
  if ((minute(local_t) == 57) && (second(local_t) == 54)) {
    // turn on the clutch relay
    Serial.write(relay1on,sizeof(relay1on));
    if (hour_12 == 5) {
      delay(14000);
    }
    else {
      delay(8000);
    }
    Serial.write(relay1off,sizeof(relay1off));
  }
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime() {
  IPAddress ntpServerIP; // NTP server's ip address

  while (udp.parsePacket() > 0); // discard any previously received packets
  log_i("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  log_i("%s:%s", ntpServerName, ntpServerIP.toString().c_str());
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      log_i("Receive NTP Response");
      udp.read(packetBuffer, NTP_PACKET_SIZE); // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 = (unsigned long) packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long) packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long) packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long) packetBuffer[43];
      last_ntp_sync = secsSince1900 - 2208988800UL;
      return secsSince1900 - 2208988800UL;
    }
  }
  log_i("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0; // Stratum, or type of clock
  packetBuffer[2] = 6; // Polling Interval
  packetBuffer[3] = 0xEC; // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
