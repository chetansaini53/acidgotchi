/*
 * ============================================================================
 *  ACIDGOTCHI  —  a Tamagotchi that eats WiFi  🐾
 * ============================================================================
 *  A kawaii virtual cat on the ESP32-C6 + 1.47" LCD. It survives by sniffing
 *  the RF spectrum: every WiFi AP and BLE device nearby is food. Strong/new
 *  signals = happy + growth; dead zones = hungry, bored, needy. Evolves from
 *  Baby -> Teenager -> Pro Hacker Pet as it eats more unique signals.
 *  PASSIVE: it only scans (WiFi.scanNetworks + BLE scan), never attacks.
 *
 *  Hardware : Waveshare ESP32-C6-LCD-1.47  (ST7789 172x320 IPS, WiFi6 + BLE5)
 *  Build    : arduino-esp32 core 3.x | FQBN esp32:esp32:esp32c6:PartitionScheme=huge_app
 *  Library  : "GFX Library for Arduino" (Arduino_GFX)
 *  Author   : Chetan Saini (@cyberac1d) & Ella  |  License: MIT
 * ============================================================================
 */
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Preferences.h>
#include <math.h>

// ---------------- Waveshare C6-LCD-1.47 display (proven config) ----------------
#define PIN_SCK  7
#define PIN_MOSI 6
#define PIN_DC   15
#define PIN_CS   14
#define PIN_RST  21
#define PIN_BL   22
Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_RST, 0 /*portrait*/, true /*IPS*/, 172, 320, 34, 0, 34, 0);

#define C565(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
static uint16_t C_BG, C_CAT, C_CATSH, C_PINK, C_DARK, C_WHITE, C_TXT, C_SUB, C_GREEN, C_RED, C_AMBER, C_PANEL, C_BLUSH;
static void initColors(){
  C_BG   =C565(0x10,0x18,0x28); C_CAT  =C565(0xF4,0xEF,0xE2); C_CATSH=C565(0xCF,0xC8,0xBA);
  C_PINK =C565(0xFF,0x9E,0xC4); C_DARK =C565(0x2A,0x2C,0x3C); C_WHITE=C565(0xFF,0xFF,0xFF);
  C_TXT  =C565(0xE8,0xEE,0xFA); C_SUB  =C565(0x82,0x92,0xB0); C_GREEN=C565(0x4B,0xE0,0x8C);
  C_RED  =C565(0xFF,0x6B,0x6B); C_AMBER=C565(0xF5,0xB3,0x3C); C_PANEL=C565(0x1A,0x24,0x38);
  C_BLUSH=C565(0xFF,0xB3,0xCE);
}

// ---------------- pet state ----------------
struct Pet { float hunger, happy, energy, bored; uint32_t lifeUniq; uint32_t aliveMin; uint8_t evo; };
static Pet pet = {30,70,90,10,0,0,0};
enum Mood { M_HAPPY, M_CONTENT, M_HUNGRY, M_BORED, M_NEEDY, M_SLEEP, M_STARVE };
static Mood mood=M_CONTENT, lastMood=(Mood)99;

// seen-signals memory (LRU set of 32-bit hashes)
#define SEEN_MAX 300
static uint32_t seenHash[SEEN_MAX]; static uint32_t seenTs[SEEN_MAX]; static int seenN=0;
static uint32_t hashMac(const uint8_t* m){ uint32_t h=2166136261u; for(int i=0;i<6;i++){ h^=m[i]; h*=16777619u; } return h?h:1; }
static bool seenSeen(uint32_t h){                       // returns true if already known; inserts if new
  for(int i=0;i<seenN;i++) if(seenHash[i]==h){ seenTs[i]=millis(); return true; }
  int idx; if(seenN<SEEN_MAX){ idx=seenN++; } else { idx=0; for(int i=1;i<seenN;i++) if(seenTs[i]<seenTs[idx]) idx=i; }
  seenHash[idx]=h; seenTs[idx]=millis(); return false;
}

// ---------------- animation / event state ----------------
static BLEScan* pScan=nullptr;
static Preferences prefs;
static uint32_t lastScan=0, lastTick=0, lastBlink=0, lastTail=0, lastMinute=0, lastSave=0, nomUntil=0;
static bool sniffing=false; static int tailPhase=0; static bool eyesClosed=false;
static int lastAte=0, lastNew=0; static char msg[40]="hi! i eat wifi :3";
static int noNewStreak=0;

static const uint32_t SCAN_MS=25000;                    // sniff for food every 25 s

// ---------------- kawaii cat geometry (172 wide) ----------------
static const int CX=86, CY=104, HR=46;                  // head center + radius

static void arcDots(int cx,int cy,int r,int a0,int a1,uint16_t col,int th){
  for(int a=a0;a<=a1;a+=10){ float rad=a*0.01745f; gfx->fillCircle(cx+(int)(cosf(rad)*r),cy+(int)(sinf(rad)*r),th,col); }
}

// eyes: type 0 open,1 happy^,2 blink/closed,3 droopy,4 sad,5 sleep,6 dizzy
static void drawEye(int x,int y,int t){
  if(t==0){ gfx->fillCircle(x,y,12,C_WHITE); gfx->fillCircle(x,y+2,8,C_DARK); gfx->fillCircle(x-3,y-3,3,C_WHITE); }
  else if(t==1){ arcDots(x,y+4,11,200,340,C_DARK,2); }                    // ^ happy
  else if(t==2){ gfx->fillRect(x-11,y-1,22,3,C_DARK); }                   // blink line
  else if(t==3){ gfx->fillCircle(x,y+1,10,C_WHITE); gfx->fillCircle(x,y+3,7,C_DARK); gfx->fillRect(x-12,y-10,24,7,C_CAT); } // droopy (lid)
  else if(t==4){ gfx->fillCircle(x,y,13,C_WHITE); gfx->fillCircle(x,y+3,9,C_DARK); gfx->fillCircle(x-4,y-4,4,C_WHITE); gfx->fillCircle(x+2,y+13,3,C565(0x7A,0xC8,0xFF)); } // sad + tear
  else if(t==5){ arcDots(x,y,10,20,160,C_DARK,2); }                       // sleep u
  else { gfx->drawCircle(x,y,10,C_DARK); gfx->drawCircle(x,y,5,C_DARK); } // dizzy
}
// mouth: 0 :3 , 1 nom(open), 2 smile, 3 frown, 4 sniff-o
static void drawMouth(int t){
  int mx=CX, my=CY+22;
  if(t==1){ int r=6+(int)(3*sinf(millis()*0.02f)); gfx->fillCircle(mx,my+2,r,C565(0x9A,0x3A,0x3A)); gfx->fillCircle(mx,my+2-r/2,r/2,C_PINK); } // nom
  else if(t==2){ arcDots(mx,my-2,7,20,160,C_DARK,1); }
  else if(t==3){ arcDots(mx,my+8,7,200,340,C_DARK,1); }
  else if(t==4){ gfx->fillCircle(mx,my+1,3,C565(0x9A,0x3A,0x3A)); }
  else { arcDots(mx-4,my,4,20,160,C_DARK,1); arcDots(mx+4,my,4,20,160,C_DARK,1); }   // :3
}

static void drawTail(){
  gfx->fillRect(126,150,46,64,C_BG);                    // clear tail region
  float a=0.5f*sinf(tailPhase*0.6f);
  int bx=130, by=196;
  for(int i=0;i<20;i++){ float t=i/19.0f; int x=bx+(int)(t*30*cosf(-0.6f+a)); int y=by-(int)(t*46) + (int)(a*10*t);
    gfx->fillCircle(x,y,6-(int)(t*2),C_CAT); }
}

static int eyeTypeForMood(){
  if(eyesClosed) return 2;
  switch(mood){ case M_HAPPY:return 1; case M_HUNGRY:return 3; case M_BORED:return 3;
    case M_NEEDY:return 4; case M_SLEEP:return 5; case M_STARVE:return 6; default:return 0; }
}
static int mouthTypeForMood(){
  if(millis()<nomUntil) return 1; if(sniffing) return 4;
  switch(mood){ case M_HAPPY:return 2; case M_NEEDY:case M_STARVE:return 3; default:return 0; }
}
static void drawEyes(){ gfx->fillRect(CX-40,CY-22,80,26,C_CAT); int t=eyeTypeForMood(); drawEye(CX-20,CY-6,t); drawEye(CX+20,CY-6,t); }
static void drawMouthRegion(){ gfx->fillRect(CX-16,CY+10,32,26,C_CAT); gfx->fillTriangle(CX-3,CY+13,CX+3,CY+13,CX,CY+17,C_PINK); drawMouth(mouthTypeForMood()); } // nose + mouth

static void drawCat(){
  gfx->fillRect(0,24,172,190,C_BG);
  // body
  gfx->fillRoundRect(CX-40,CY+38,80,66,26,C_CAT);
  gfx->fillRoundRect(CX-40,CY+38,80,66,26,C_CAT);
  gfx->fillRoundRect(CX-18,CY+70,36,30,14,C_CATSH);     // belly
  // ears
  gfx->fillTriangle(CX-42,CY-30,CX-16,CY-46,CX-14,CY-16,C_CAT);
  gfx->fillTriangle(CX+42,CY-30,CX+16,CY-46,CX+14,CY-16,C_CAT);
  gfx->fillTriangle(CX-34,CY-28,CX-20,CY-38,CX-18,CY-20,C_PINK);
  gfx->fillTriangle(CX+34,CY-28,CX+20,CY-38,CX+18,CY-20,C_PINK);
  // head
  gfx->fillCircle(CX,CY,HR,C_CAT);
  // cheeks (blush when happy)
  if(mood==M_HAPPY||mood==M_CONTENT){ gfx->fillCircle(CX-34,CY+10,7,C_BLUSH); gfx->fillCircle(CX+34,CY+10,7,C_BLUSH); }
  // whiskers
  for(int i=-1;i<=1;i++){ gfx->drawLine(CX-30,CY+8+i*6,CX-58,CY+4+i*10,C_CATSH); gfx->drawLine(CX+30,CY+8+i*6,CX+58,CY+4+i*10,C_CATSH); }
  drawEyes(); drawMouthRegion(); drawTail();
  // Zzz when sleeping
  if(mood==M_SLEEP){ gfx->setTextColor(C_SUB); gfx->setTextSize(2); gfx->setCursor(CX+30,CY-40); gfx->print("z"); gfx->setTextSize(1); gfx->setCursor(CX+46,CY-46); gfx->print("z"); }
}

// ---------------- UI chrome ----------------
static const char* evoName(){ return pet.evo==0?"Baby": pet.evo==1?"Teenager":"Pro Hacker Pet"; }
static void drawStatusBar(){
  gfx->fillRect(0,0,172,22,C_PANEL);
  gfx->setTextSize(1); gfx->setTextColor(C_GREEN); gfx->setCursor(6,4); gfx->print("Acidgotchi");
  gfx->setTextColor(C_AMBER); gfx->setCursor(96,4); gfx->print(evoName());
  gfx->drawFastHLine(0,22,172,C_PANEL);
}
static void bar(int x,int y,int w,int pct,uint16_t col,const char* lbl){
  if(pct<0)pct=0; if(pct>100)pct=100;
  gfx->setTextSize(1); gfx->setTextColor(C_SUB); gfx->setCursor(x,y-2); gfx->print(lbl);
  gfx->fillRoundRect(x+34,y-2,w,9,3,C_PANEL); gfx->fillRoundRect(x+34,y-2,w*pct/100,9,3,col);
}
static void drawStats(){
  gfx->fillRect(0,216,172,50,C_BG);
  bar(8,224,110,(int)(100-pet.hunger),C_GREEN,"FED");
  bar(8,240,110,(int)pet.happy,C_PINK,"JOY");
  bar(8,256,110,(int)(100-pet.bored),C_AMBER,"FUN");
  gfx->setTextColor(C_SUB); gfx->setCursor(140,224); gfx->printf("%lu", (unsigned long)pet.lifeUniq);
  gfx->setCursor(140,234); gfx->print("eaten");
}
static void drawMsg(){
  gfx->fillRect(0,270,172,34,C_BG);
  gfx->setTextSize(1); gfx->setTextColor(sniffing?C_AMBER:C_TXT); gfx->setCursor(8,276);
  gfx->print(sniffing?"sniff sniff... \x10":msg);
}
static void drawFooter(){
  gfx->fillRect(0,306,172,14,C_BG);
  gfx->setTextColor(C_SUB); gfx->setTextSize(1); gfx->setCursor(8,309);
  gfx->printf("ate %d  +%d new  d%lu", lastAte, lastNew, (unsigned long)(pet.aliveMin/1440));
}

// ---------------- mood + messages ----------------
static void pickMood(){
  Mood m;
  if(pet.energy<14) m=M_SLEEP;
  else if(pet.hunger>82) m=M_STARVE;
  else if(pet.bored>76) m=M_BORED;
  else if(pet.hunger>56 || pet.happy<28) m=M_NEEDY;
  else if(pet.happy>68 && pet.hunger<40) m=M_HAPPY;
  else m=M_CONTENT;
  mood=m;
}
static const char* quip(){
  switch(mood){
    case M_HAPPY:   { const char* a[]={"nom nom nom!","fresh signals yum","i love this spot","+1 spectrum :3"}; return a[millis()/3000%4]; }
    case M_HUNGRY:
    case M_NEEDY:   { const char* a[]={"feed me wifi!","play with me!","oi where are you?","take me for a walk"}; return a[millis()/3000%4]; }
    case M_BORED:   { const char* a[]={"same old signals -_-","show me somewhere new","boooring","meh."}; return a[millis()/3000%4]; }
    case M_STARVE:  { const char* a[]={"so... hungry...","need... signal...","a dead zone?! nooo"}; return a[millis()/3000%3]; }
    case M_SLEEP:   return "zzz...";
    default:        { const char* a[]={"sniffing the air~","what's nearby?","purr","i eat wifi btw"}; return a[millis()/3000%4]; }
  }
}

// ---------------- feeding ----------------
static void feed(int rssi, const uint8_t* mac){
  uint32_t h=hashMac(mac); bool old=seenSeen(h);
  float strength = (rssi+90)/60.0f; if(strength<0)strength=0; if(strength>1)strength=1;   // -90..-30 -> 0..1
  float food = 3 + strength*10;
  lastAte++;
  if(!old){ food += 12; lastNew++; pet.lifeUniq++; nomUntil=millis()+1600; pet.happy+=6; }
  else food *= 0.4f;                                     // stale = boring, diminishing
  pet.hunger -= food; pet.happy += strength*2;
}
static void doSniffAndFeed(){
  sniffing=true; lastMood=(Mood)99; drawCat(); drawMsg();   // show sniff state before blocking scans
  lastAte=0; lastNew=0;
  int n=WiFi.scanNetworks(false,true);                  // blocking; show hidden
  for(int i=0;i<n && i<60;i++){ uint8_t* b=WiFi.BSSID(i); if(b) feed((int)WiFi.RSSI(i), b); }
  WiFi.scanDelete();
  BLEScanResults* res=pScan->start(3,false);            // blocking ~3s
  if(res){ int c=res->getCount(); for(int i=0;i<c && i<80;i++){ BLEAdvertisedDevice d=res->getDevice(i);
      feed(d.getRSSI(), d.getAddress().getNative()); } }
  pScan->clearResults(); pScan->stop();                 // clearResults MANDATORY (heap)
  if(lastNew==0){ noNewStreak++; pet.bored += 8; } else { noNewStreak=0; pet.bored -= 18; }
  if(lastAte==0){ pet.hunger += 6; }                    // dead zone
  // clamp
  if(pet.hunger<0)pet.hunger=0; if(pet.hunger>100)pet.hunger=100;
  if(pet.happy<0)pet.happy=0; if(pet.happy>100)pet.happy=100;
  if(pet.bored<0)pet.bored=0; if(pet.bored>100)pet.bored=100;
  // evolution
  uint8_t ne = pet.lifeUniq>=150?2 : pet.lifeUniq>=40?1 : 0; if(ne!=pet.evo){ pet.evo=ne; drawStatusBar(); }
  sniffing=false; lastMood=(Mood)99;                    // force full redraw after sniff
  snprintf(msg,sizeof msg,"%s", lastNew>0?"nom nom nom!":(lastAte>0?"munch... ok food":"nothing here :("));
}

// ---------------- persistence ----------------
static void loadPet(){ prefs.begin("acidg",false);
  pet.lifeUniq=prefs.getUInt("uniq",0); pet.aliveMin=prefs.getUInt("alive",0); pet.evo=prefs.getUChar("evo",0);
  pet.hunger=prefs.getUChar("hun",30); pet.happy=prefs.getUChar("joy",70); pet.bored=prefs.getUChar("bor",10); }
static void savePet(){ prefs.putUInt("uniq",pet.lifeUniq); prefs.putUInt("alive",pet.aliveMin); prefs.putUChar("evo",pet.evo);
  prefs.putUChar("hun",(uint8_t)pet.hunger); prefs.putUChar("joy",(uint8_t)pet.happy); prefs.putUChar("bor",(uint8_t)pet.bored); }

void setup(){
  Serial.begin(115200); delay(150);
  pinMode(PIN_BL,OUTPUT); digitalWrite(PIN_BL,HIGH);
  gfx->begin(); initColors(); gfx->fillScreen(C_BG);
  gfx->setTextColor(C_GREEN); gfx->setTextSize(2); gfx->setCursor(20,140); gfx->println("Acidgotchi");
  gfx->setTextSize(1); gfx->setTextColor(C_SUB); gfx->setCursor(46,170); gfx->println("waking up...");
  loadPet();
  BLEDevice::init("");                                  // BLE (Bluedroid) FIRST — C6 coexistence
  pScan=BLEDevice::getScan(); pScan->setActiveScan(false); pScan->setInterval(100); pScan->setWindow(99);
  WiFi.mode(WIFI_STA); WiFi.disconnect(false);          // disconnect(false) keeps radio ON
  esp_wifi_set_ps(WIFI_PS_NONE);
  gfx->fillScreen(C_BG); drawStatusBar(); pickMood(); drawCat(); drawStats(); drawMsg(); drawFooter();
  lastScan=millis()-SCAN_MS+3000;                       // first sniff ~3 s after boot
}

void loop(){
  uint32_t now=millis();

  if(now-lastScan>=SCAN_MS){ lastScan=now; doSniffAndFeed(); pickMood(); drawStatusBar(); drawCat(); drawStats(); drawFooter(); }

  // 1 s stat tick
  if(now-lastTick>1000){ lastTick=now;
    pet.hunger += 0.25f; pet.energy -= 0.15f; if(pet.hunger>60) pet.happy -= 0.2f; pet.bored += 0.05f;
    if(pet.hunger>100)pet.hunger=100; if(pet.energy<0)pet.energy=0; if(pet.happy<0)pet.happy=0; if(pet.bored>100)pet.bored=100;
    pickMood();
    if(mood!=lastMood){ lastMood=mood; snprintf(msg,sizeof msg,"%s",quip()); drawCat(); drawStats(); drawMsg(); }
    else { drawStats(); }
  }
  // 1 min alive counter + periodic save
  if(now-lastMinute>60000){ lastMinute=now; pet.aliveMin++; }
  if(now-lastSave>60000){ lastSave=now; savePet(); }

  // idle animation (partial redraws — no flicker)
  if(now-lastBlink>3000){ lastBlink=now; eyesClosed=true; drawEyes(); delay(120); eyesClosed=false; drawEyes(); }
  if(now-lastTail>450){ lastTail=now; tailPhase++; drawTail(); }
  if(millis()<nomUntil){ drawMouthRegion(); }           // animate mouth during nom
  if((now/2500)%1==0){ } // (msg refresh handled on mood change)

  delay(40);
}
