/*
 * ============================================================================
 *  ACIDGOTCHI  —  a Tamagotchi that eats WiFi  🐾   (animated-emotion build)
 * ============================================================================
 *  A kawaii virtual cat on the ESP32-C6 + 1.47" LCD. Each mood plays its own
 *  short frame animation (happy / needy / attention / sad / eating). It feeds
 *  on nearby WiFi + BLE signals; strong/new = happy, dead zones = hungry/needy.
 *  Evolves Baby->Teen->Pro. PASSIVE: it only scans, never attacks.
 *
 *  Hardware : Waveshare ESP32-C6-LCD-1.47 (ST7789 172x320 IPS, WiFi6 + BLE5)
 *  Build    : arduino-esp32 core 3.x | FQBN esp32:esp32:esp32c6:PartitionScheme=huge_app
 *  Library  : "GFX Library for Arduino" (Arduino_GFX)
 *  Author   : Chetan Saini (@cyberac1d) & Ella | License: MIT
 *  NOTE     : emotions.h is generated from LOCAL image files and is .gitignore'd
 *             — supply your own / CC-licensed art before publishing.
 * ============================================================================
 */
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <math.h>
#include <string.h>
#include "emotions.h"                          // EMOS[E_COUNT] frame animations

// ---------------- display (proven Waveshare C6-LCD-1.47 config) ----------------
#define PIN_SCK 7
#define PIN_MOSI 6
#define PIN_DC  15
#define PIN_CS  14
#define PIN_RST 21
#define PIN_BL  22
Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_RST, 0, true, 172, 320, 34, 0, 34, 0);
static Arduino_GFX *g;   // current draw target (LCD normally; a RAM canvas during screenshot)

#define C565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
static uint16_t C_BG,C_HDR,C_TXT,C_SUB,C_PINK,C_GREEN,C_AMBER,C_BLUE,C_BUBBLE;
static void initColors(){
  C_BG=C565(0xFF,0xFF,0xFF); C_HDR=C565(0xFF,0xE1,0xEC); C_TXT=C565(0x3C,0x3A,0x4A);
  C_SUB=C565(0x9A,0xA2,0xB4); C_PINK=C565(0xFF,0x74,0xAE); C_GREEN=C565(0x2E,0xC0,0x82);
  C_AMBER=C565(0xF0,0x9A,0x28); C_BLUE=C565(0x5A,0xAA,0xF0); C_BUBBLE=C565(0xF3,0xEE,0xFA);
}

// ---------------- pet state ----------------
struct Pet { float hunger,happy,energy,bored; uint32_t lifeUniq,aliveMin,wifiEat,bleEat; uint8_t evo; };
static Pet pet={30,70,90,10,0,0,0,0,0};
enum Mood { M_HAPPY,M_CONTENT,M_HUNGRY,M_BORED,M_NEEDY,M_SLEEP,M_STARVE };
static Mood mood=M_CONTENT;

#define SEEN_MAX 300
static uint32_t seenHash[SEEN_MAX],seenTs[SEEN_MAX]; static int seenN=0;
static uint32_t hashMac(const uint8_t* m){ uint32_t h=2166136261u; for(int i=0;i<6;i++){ h^=m[i]; h*=16777619u; } return h?h:1; }
static bool seenSeen(uint32_t h){
  for(int i=0;i<seenN;i++) if(seenHash[i]==h){ seenTs[i]=millis(); return true; }
  int idx; if(seenN<SEEN_MAX){ idx=seenN++; } else { idx=0; for(int i=1;i<seenN;i++) if(seenTs[i]<seenTs[idx]) idx=i; }
  seenHash[idx]=h; seenTs[idx]=millis(); return false;
}

static BLEScan* pScan=nullptr; static Preferences prefs;

// ---------------- SD card logging (shared SPI bus with the LCD) ----------------
#define SD_SCK 7
#define SD_MISO 5
#define SD_MOSI 6
#define SD_CS 4
static SPIClass sdSPI(FSPI);
static bool sdOK=false;
static uint32_t petDay();                       // fwd (defined below)
static void logLine(const char* s){
  if(!sdOK) return;
  File f=SD.open("/acidgotchi.log", FILE_APPEND);
  if(f){ f.printf("[Day %lu +%lum] %s\n",(unsigned long)petDay(),(unsigned long)pet.aliveMin,s); f.close(); }
}
static uint32_t lastScan=0,lastTick=0,lastMinute=0,lastSave=0,nomUntil=0,lastFrame=0;
static int lastAte=0,lastNew=0,noNewStreak=0;
static int curEmo=-1, frIdx=0, scanPhase=0;   // scanPhase: 0=idle, 1=async WiFi scan running
// ---- "talking" text: static, word-wrapped, held ~4.5s so it is readable ----
static char msgL1[24]="hello human", msgL2[24]="";
static int  msgIdx=0, msgSrc=-1, shownMood=-1;
static uint32_t lastMsgSwitch=0;
static const uint32_t MSG_HOLD=4500;     // hold each line ~4.5s to read
// scan cadence: 5s on day1, +10s per pet-day as it grows (see scanInterval() below)
static char eatName[24]=""; static int eatScore=0;   // what the pet is currently eating (shown in the demo)
static const int PAD=11, PET_Y=52, PET_H=112, MOOD_Y=168, MSG_Y=190, ST_Y=232;

// ---------------- render ----------------
static int emoForMood(uint32_t now){
  if(now<nomUntil) return E_EATING;
  switch(mood){ case M_HAPPY:case M_CONTENT:case M_SLEEP:return E_HAPPY;
    case M_NEEDY:return E_OIDEKHO; case M_HUNGRY:return E_NEEDATT;
    case M_STARVE:return E_SAD; case M_BORED:return E_NEEDATT; default:return E_HAPPY; }
}
static void drawFrame(){
  const Emo& e=EMOS[curEmo];
  g->draw16bitRGBBitmap((172-e.w)/2, PET_Y+(PET_H-e.h)/2, (uint16_t*)e.fr[frIdx], e.w, e.h);
}
static uint32_t petDay(){ return pet.aliveMin/720 + 1; }   // 12h = 1 day
static uint32_t scanInterval(){ return 5000UL + (petDay()-1)*10000UL; }   // 5s day1, +10s each pet-day
static void drawHeader(){                          // "Ella" title + centered Day, both readable
  g->fillRect(0,0,172,50,C_HDR);
  g->setTextSize(3); g->setTextColor(C_PINK);
  g->setCursor((172-4*18)/2,5); g->print("Ella");                 // 4 chars, size3 = 18px each
  char d[16]; snprintf(d,sizeof d,"Day %lu",(unsigned long)petDay());
  g->setTextSize(2); g->setTextColor(C_AMBER);
  g->setCursor((172-(int)strlen(d)*12)/2,31); g->print(d);
}
static const char* evoName(){ return pet.evo>=2?"Pro":pet.evo>=1?"Teen":"Baby"; }
static void fmtUp(char* b,size_t n){              // uptime -> "4d6h" / "6h30m" / "45m"
  uint32_t m=pet.aliveMin;
  if(m>=1440) snprintf(b,n,"%lud%luh",(unsigned long)(m/1440),(unsigned long)((m%1440)/60));
  else if(m>=60) snprintf(b,n,"%luh%lum",(unsigned long)(m/60),(unsigned long)(m%60));
  else snprintf(b,n,"%lum",(unsigned long)m);
}
static void techPair(int y,const char* l1,const char* v1,uint16_t c1,const char* l2,const char* v2,uint16_t c2){
  g->setTextSize(1);
  g->setTextColor(C_SUB); g->setCursor(PAD,y);     g->print(l1);
  g->setTextColor(c1);    g->setCursor(PAD+34,y);  g->print(v1);
  g->setTextColor(C_SUB); g->setCursor(94,y);      g->print(l2);
  g->setTextColor(c2);    g->setCursor(94+30,y);   g->print(v2);
}
static void drawStats(){                          // compact technical readout (small font)
  g->fillRect(0,ST_Y,172,74,C_BG);
  char a[12],b[12]; int Y=ST_Y;
  snprintf(a,12,"%d",(int)(100-pet.hunger)); snprintf(b,12,"%d",(int)pet.happy);
  techPair(Y,"FED",a,C_GREEN,"JOY",b,C_PINK); Y+=12;
  snprintf(a,12,"%d",(int)(100-pet.bored));
  techPair(Y,"FUN",a,C_AMBER,"EVO",evoName(),C_BLUE); Y+=12;
  snprintf(a,12,"%lu",(unsigned long)pet.wifiEat); snprintf(b,12,"%lu",(unsigned long)pet.bleEat);
  techPair(Y,"WiFi",a,C_BLUE,"BLE",b,C_PINK); Y+=12;
  uint32_t tot=pet.wifiEat+pet.bleEat, rept=tot>pet.lifeUniq?tot-pet.lifeUniq:0;
  snprintf(a,12,"%lu",(unsigned long)pet.lifeUniq); snprintf(b,12,"%lu",(unsigned long)rept);
  techPair(Y,"Uniq",a,C_GREEN,"Rept",b,C_AMBER); Y+=12;
  snprintf(a,12,"%d/%d",lastAte,lastNew); fmtUp(b,12);
  techPair(Y,"Scan",a,C_TXT,"Up",b,C_TXT); Y+=12;
  snprintf(a,12,"%luK",(unsigned long)(ESP.getFreeHeap()/1024));
  techPair(Y,"Heap",a,C_TXT,"SD",sdOK?"on":"off",sdOK?C_GREEN:C_SUB);
}
static void drawMsgLine(const char* t,int y){     // one centered line, size-2 (readable)
  int w=(int)strlen(t)*12; int x=(172-w)/2; if(x<2)x=2;
  g->setTextSize(2); g->setTextColor(C_TXT); g->setCursor(x,y); g->print(t);
}
static void drawMsgText(){                        // static word-wrapped talking text (no bubble)
  g->fillRect(0,MSG_Y-2,172,42,C_BG);
  if(msgL2[0]){ drawMsgLine(msgL1,MSG_Y); drawMsgLine(msgL2,MSG_Y+18); }
  else drawMsgLine(msgL1,MSG_Y+9);
}
static void wrapMsg(const char* s){               // split into <=2 centered lines (~14 chars each)
  msgL1[0]=msgL2[0]=0; int n=(int)strlen(s);
  if(n<=14){ snprintf(msgL1,sizeof msgL1,"%s",s); return; }
  int split=-1; for(int i=0;i<n && i<=14;i++) if(s[i]==' ') split=i;
  if(split<=0) split=14;
  snprintf(msgL1,sizeof msgL1,"%.*s",split,s);
  const char* r=s+split; while(*r==' ')r++;
  snprintf(msgL2,sizeof msgL2,"%s",r);
}
static const char* moodWord(){
  if(millis()<nomUntil) return "eating";
  switch(mood){
    case M_HAPPY:  return "happy";    case M_CONTENT: return "chill";
    case M_HUNGRY: return "hungry";   case M_BORED:   return "bored";
    case M_NEEDY:  return "needy";    case M_SLEEP:   return "yawn";
    case M_STARVE: return "starving"; default:        return "chill";
  }
}
static uint16_t moodColor(){
  if(millis()<nomUntil) return C_GREEN;
  switch(mood){
    case M_HAPPY:  return C_PINK;  case M_CONTENT: return C_BLUE;
    case M_HUNGRY: return C_AMBER; case M_BORED:   return C_AMBER;
    case M_NEEDY:  return C_PINK;  case M_SLEEP:   return C_BLUE;
    case M_STARVE: return C_PINK;  default:        return C_BLUE;
  }
}
static void drawMoodWord(){                          // one-word mood label, centered under the cat
  g->fillRect(0,MOOD_Y-1,172,19,C_BG);
  const char* w=moodWord(); int len=(int)strlen(w);
  int x=(172-len*12)/2; if(x<2) x=2;                 // size-2 glyphs are 12 px wide
  g->setTextSize(2); g->setTextColor(moodColor()); g->setCursor(x,MOOD_Y); g->print(w);
}
static void drawChrome(){ g->fillScreen(C_BG); drawHeader(); drawMoodWord(); drawMsgText(); drawStats(); curEmo=-1; }

// ---------------- mood + messages ----------------
static bool napping=false;
static void pickMood(){
  if(napping){ if(pet.energy>=55) napping=false; }   // wake once rested (hysteresis, no flicker)
  else if(pet.energy<18) napping=true;               // nap only when actually drained
  if(napping){ mood=M_SLEEP; return; }
  if(pet.hunger>82) mood=M_STARVE;
  else if(pet.bored>76) mood=M_BORED;
  else if(pet.hunger>56 || pet.happy<28) mood=M_NEEDY;
  else if(pet.happy>68 && pet.hunger<40) mood=M_HAPPY;
  else mood=M_CONTENT;
}
// talking marquee bank — 15 English one-liners per mood (index = Mood enum), + eating set.
static const char* const MQ[7][15]={
 { // M_HAPPY
  "purring happily","best day ever","i love you human","so full and cozy","you are the best",
  "life is good","feeling awesome","that was tasty","happy happy happy","give me a pat",
  "what a great spot","more of this please","one lucky cat","good vibes only","content and calm" },
 { // M_CONTENT
  "whatsup human","anything new around","i eat wifi you know","give me a signal","just chilling here",
  "shall we scan","you still there","all good here","just relaxing","find me a router",
  "checking on you","where to today","a ble would be nice","i am right here","sniffing around" },
 { // M_HUNGRY
  "feed me im hungry","need some wifi","belly is rumbling","got any signals","getting hungry now",
  "bring me a router","wheres my food","feed me please","a little wifi please","empty belly sad cat",
  "signals please","feed me human","snack time now","i am hungry","so hungry meow" },
 { // M_BORED
  "feeling bored","show me something new","same old signals","take me somewhere","i am so bored",
  "need new ble","this place is dull","do something fun","same old same old","lets explore",
  "boring boring","no fun here","adventure time","new signals new fun","bored out here" },
 { // M_NEEDY
  "take me to walk","look at me human","dont ignore me","i need a hug","talk to me",
  "feeling lonely","hey look here","i miss you","give me some love","come closer human",
  "notice me please","play with me","just look once","pay attention","i want you here" },
 { // M_SLEEP
  "yawn so sleepy","can i nap now","battery low yawn","getting drowsy","tired little cat",
  "eyes feel heavy","just five minutes","so tired today","dreaming of wifi","goodnight human",
  "i am sleepy","let me rest","nap time meow","half asleep now","lights off please" },
 { // M_STARVE
  "starving help me","signals or i faint","a dead zone here","belly totally empty","so very hungry",
  "wifi now please","feed me anything","need food badly","feeling dizzy now","i am starving",
  "so so hungry","save me feed me","one signal please","running on empty","i might faint" }
};
static const char* const MQ_EAT[15]={
  "nom nom nom","yummy signals","hunt in progress","found something","yay new flavour",
  "chomp chomp chomp","more more more","what a taste","fresh wifi slurp","eating away happily",
  "gulp another one","a new device yum","feast time now","so delicious","thanks for the food" };
static void loadMsg(int src,int i){ wrapMsg((src==7)?MQ_EAT[i]:MQ[src][i]); }
static void setEatMsg(){                           // demo: "Hi <name>" + tasty score of the eaten signal
  char nm[12]; snprintf(nm,sizeof nm,"%s",eatName);        // fit "Hi " + 11 chars on one size-2 line
  snprintf(msgL1,sizeof msgL1,"Hi %s",nm);
  snprintf(msgL2,sizeof msgL2,"tasty %d/10",eatScore);
}
static char shownEatName[24]="";                   // last eaten name actually drawn (so it can refresh live)
static void msgTick(uint32_t now){                 // eating -> show eaten signal (live); else cycle mood lines
  int src=(now<nomUntil)?7:(int)mood;                      // 7 = actively eating
  if(src!=7 && (src<0||src>6)) src=1;
  if(src!=msgSrc){ msgSrc=src;
    if(src==7 && eatName[0]){ setEatMsg(); snprintf(shownEatName,sizeof shownEatName,"%s",eatName); }
    else { msgIdx=0; loadMsg(src,0); }
    drawMsgText(); lastMsgSwitch=now; return;
  }
  if(src==7){                                             // still eating: refresh whenever it catches a NEW signal
    if(strcmp(eatName,shownEatName)!=0){ setEatMsg(); snprintf(shownEatName,sizeof shownEatName,"%s",eatName); drawMsgText(); }
    return;
  }
  if(now-lastMsgSwitch>=MSG_HOLD){ lastMsgSwitch=now; msgIdx=(msgIdx+1)%15; loadMsg(src,msgIdx); drawMsgText(); }
}

// ---------------- feeding ----------------
static void feed(int rssi,const uint8_t* mac,bool isBle){
  uint32_t h=hashMac(mac); bool old=seenSeen(h);
  float s=(rssi+90)/60.0f; if(s<0)s=0; if(s>1)s=1;
  float food=3+s*10; lastAte++;
  if(isBle) pet.bleEat++; else pet.wifiEat++;            // lifetime WiFi/BLE eaten (incl. repeats)
  if(!old){ food+=12; lastNew++; pet.lifeUniq++; nomUntil=millis()+2500; pet.happy+=6; }
  else food*=0.4f;
  pet.hunger-=food; pet.happy+=s*2;
}
static void startScan(){                               // kick off an ASYNC WiFi scan (loop keeps animating)
  lastAte=0; lastNew=0;
  WiFi.mode(WIFI_STA); delay(60);
  WiFi.scanNetworks(true,true);                        // async=true -> returns immediately, scans in background
  scanPhase=1;
}
static void finishScan(int n){                         // WiFi results ready: tally + a short 1s BLE burst
  int bestR=-999; char bestNm[24]="";                  // strongest eaten signal this scan (for the demo)
  for(int i=0;i<n && i<60;i++){ uint8_t* b=WiFi.BSSID(i); if(!b) continue; int r=(int)WiFi.RSSI(i); feed(r,b,false);
    if(r>bestR){ bestR=r; String s=WiFi.SSID(i); snprintf(bestNm,sizeof bestNm,"%s", s.length()?s.c_str():"hidden net"); } }
  WiFi.scanDelete();
  BLEScanResults* res=pScan->start(1,false);           // 1s BLE (was 3s) — only ~1s freeze, keeps it "live"
  if(res){ int c=res->getCount(); for(int i=0;i<c && i<80;i++){ BLEAdvertisedDevice d=res->getDevice(i); int r=d.getRSSI(); feed(r,d.getAddress().getNative(),true);
    if(r>bestR){ bestR=r; String nm=d.getName().c_str(); snprintf(bestNm,sizeof bestNm,"%s", nm.length()?nm.c_str():"BLE device"); } } }
  pScan->clearResults(); pScan->stop();                 // MANDATORY (heap)
  WiFi.mode(WIFI_OFF);                                   // radio OFF between scans
  if(bestR>-999){ snprintf(eatName,sizeof eatName,"%s",bestNm);          // remember what it just ate (demo)
    int sc=1+(int)lround(((bestR+90)/60.0f)*9.0f); eatScore = sc<1?1:(sc>10?10:sc); nomUntil=millis()+4500; }
  if(lastAte==0){ noNewStreak++; pet.bored+=8; pet.hunger+=6; }   // dead zone -> bored + hungry (walk or it dies)
  else if(lastNew>0){ noNewStreak=0; pet.bored-=18; }             // fresh signal -> excited, un-bored
  else pet.bored+=1;                                             // same signals -> boredom creeps slowly (stays happy in demo)
  if(pet.hunger<0)pet.hunger=0; if(pet.hunger>100)pet.hunger=100;
  if(pet.happy<0)pet.happy=0; if(pet.happy>100)pet.happy=100;
  if(pet.bored<0)pet.bored=0; if(pet.bored>100)pet.bored=100;
  pet.evo = pet.lifeUniq>=150?2 : pet.lifeUniq>=40?1 : 0;
  char lg[100]; snprintf(lg,sizeof lg,"sniff ate=%d new=%d FED=%d JOY=%d FUN=%d eaten=%lu",lastAte,lastNew,(int)(100-pet.hunger),(int)pet.happy,(int)(100-pet.bored),(unsigned long)pet.lifeUniq);
  logLine(lg);
  pickMood(); drawStats();
  scanPhase=0;
}

// ---------------- persistence ----------------
static void loadPet(){ prefs.begin("acidg",false);
  pet.lifeUniq=prefs.getUInt("uniq",0); pet.aliveMin=prefs.getUInt("alive",0); pet.evo=prefs.getUChar("evo",0);
  pet.wifiEat=prefs.getUInt("wifi",0); pet.bleEat=prefs.getUInt("ble",0);
  pet.hunger=prefs.getUChar("hun",30); pet.happy=prefs.getUChar("joy",70); pet.bored=prefs.getUChar("bor",10); pet.energy=prefs.getUChar("nrg",90);
  if(!prefs.getBool("demoR2",false)){                     // one-time FRESH reset for the recording build
    pet={30,70,90,10,0,0,0,0,0};                          // full clean state: Day 1, healthy, counters 0
    prefs.putUInt("uniq",0); prefs.putUInt("wifi",0); prefs.putUInt("ble",0); prefs.putUInt("alive",0); prefs.putUChar("evo",0);
    prefs.putUChar("hun",30); prefs.putUChar("joy",70); prefs.putUChar("bor",10); prefs.putUChar("nrg",90);
    prefs.putBool("demoR2",true);
  } }
  // (energy recovers via the nap cycle in pickMood; no manual heal needed)
static void savePet(){ prefs.putUInt("uniq",pet.lifeUniq); prefs.putUInt("alive",pet.aliveMin); prefs.putUChar("evo",pet.evo);
  prefs.putUInt("wifi",pet.wifiEat); prefs.putUInt("ble",pet.bleEat);
  prefs.putUChar("hun",(uint8_t)pet.hunger); prefs.putUChar("joy",(uint8_t)pet.happy); prefs.putUChar("bor",(uint8_t)pet.bored); prefs.putUChar("nrg",(uint8_t)pet.energy); }

// ---------------- serial screenshot: render current screen into a RAM canvas, base64 over USB ----------------
static void screenshot(){
  Arduino_Canvas* cv = new Arduino_Canvas(172,320,gfx);
  if(!cv || !cv->begin()){ Serial.println("SS_FAIL_ALLOC"); if(cv) delete cv; return; }
  if(curEmo<0) curEmo=emoForMood(millis());
  Arduino_GFX* save=g; g=(Arduino_GFX*)cv;
  g->fillScreen(C_BG); drawHeader(); drawFrame(); drawMoodWord(); drawMsgText(); drawStats();
  g=save;
  uint8_t* p=(uint8_t*)cv->getFramebuffer(); const int len=172*320*2;
  static const char B[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  Serial.printf("\nSS_BEGIN 172 320\n");
  for(int i=0;i<len;i+=3){
    uint32_t n=((uint32_t)p[i]<<16)|((uint32_t)(i+1<len?p[i+1]:0)<<8)|(uint32_t)(i+2<len?p[i+2]:0);
    char o[4]={B[(n>>18)&63],B[(n>>12)&63],(char)(i+1<len?B[(n>>6)&63]:'='),(char)(i+2<len?B[n&63]:'=')};
    Serial.write((const uint8_t*)o,4);
  }
  Serial.printf("\nSS_END\n");
  delete cv;
}

void setup(){
  Serial.begin(115200); delay(150);
  setCpuFrequencyMhz(80);                            // 160->80 MHz: ~half the CPU heat (WiFi still works at 80)
  pinMode(PIN_BL,OUTPUT); analogWrite(PIN_BL,165);   // ~65% backlight = less current/heat
  gfx->begin(); g=gfx; initColors(); g->fillScreen(C_BG);
  g->setTextColor(C_PINK); g->setTextSize(3); g->setCursor(50,140); g->println("Ella");
  g->setTextSize(1); g->setTextColor(C_SUB); g->setCursor(52,178); g->println("waking up...");
  loadPet();
  BLEDevice::init("");
  pScan=BLEDevice::getScan(); pScan->setActiveScan(false); pScan->setInterval(100); pScan->setWindow(99);
  WiFi.mode(WIFI_OFF);                               // radio OFF at boot; turned ON only during each scan burst
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);          // shares SCK/MOSI with the LCD, own CS=4
  sdOK = SD.begin(SD_CS, sdSPI, 20000000);
  Serial.printf("[sd] %s\n", sdOK?"mounted":"FAILED");
  if(sdOK) logLine("=== boot ===");
  pickMood(); drawChrome();
  lastScan=millis()-scanInterval()+2000;              // first scan ~2s after boot
}

void loop(){
  uint32_t now=millis();
  if(Serial.available()){ int c=Serial.read(); if(c=='S'||c=='s'){ screenshot(); drawChrome(); curEmo=-1; } }   // serial screenshot on 'S'
  if(ESP.getFreeHeap()<28000){ savePet(); delay(50); ESP.restart(); }   // self-heal

  if(scanPhase==0 && now-lastScan>=scanInterval()){ lastScan=now; startScan(); }           // begin async scan
  else if(scanPhase==1){ int16_t r=WiFi.scanComplete();                                     // -1 running, -2 fail, >=0 done
    if(r>=0) finishScan(r);
    else if(r==-2 || now-lastScan>15000){ WiFi.scanDelete(); WiFi.mode(WIFI_OFF); scanPhase=0; } }   // fail/stuck -> self-recover

  if(now-lastTick>1000){ lastTick=now;
    pet.hunger+=0.03f; pet.energy += napping?0.5f:-0.03f; if(pet.hunger>60) pet.happy-=0.03f; pet.bored+=0.01f;   // energy: drain awake, recover while napping
    if(pet.hunger>100)pet.hunger=100; if(pet.energy<0)pet.energy=0; if(pet.energy>100)pet.energy=100; if(pet.happy<0)pet.happy=0; if(pet.bored>100)pet.bored=100;
    pickMood();
    drawStats();
  }
  if(now-lastMinute>60000){ lastMinute=now; uint32_t od=pet.aliveMin/720; pet.aliveMin++; if(pet.aliveMin/720!=od){ drawHeader(); logLine("=== new day ==="); } }
  if(now-lastSave>60000){ lastSave=now; savePet(); }

  int dm=(now<nomUntil)?7:(int)mood; if(dm!=shownMood){ shownMood=dm; drawMoodWord(); }   // mood word on change
  msgTick(now);                                      // static talking text, new line every ~4.5s

  // emotion frame animation
  int e=emoForMood(now);
  if(e!=curEmo){ curEmo=e; frIdx=0; g->fillRect(0,PET_Y,172,PET_H,C_BG); drawFrame(); lastFrame=now; }
  else if(now-lastFrame>=(uint32_t)EMOS[curEmo].delay){ lastFrame=now; frIdx=(frIdx+1)%EMOS[curEmo].n; drawFrame(); }

  delay(15);
}
