// Halloween-koets met 3x3 raster: gremlin, felle led, ledstrip, geluid
//
// Twee kanalen vormen samen een raster van 9 vakjes:
//
//              kolom 1   2   3     <- kanaal 4 (pin 5), laag naar hoog
//   rij a      (kanaal 3 hoog)
//   rij b
//   rij c      (kanaal 3 laag)     <- kanaal 3 (pin 10)
//
// Rust = vakje c2 (rij c, kolom 2): geen geluid, geen licht, gremlin beneden.
// Wat elk vakje doet staat in de tabel "cellen" verderop.
//
// Aansluiting:
//   Ontvanger CH3 -> D10, CH4 -> D5   (GND ontvanger -> GND Arduino)
//   DM542 PUL+ -> D9, DIR+ -> D8, ENA+ -> D7   (PUL-, DIR-, ENA- -> GND)
//   MOSFET gate felle led (gremlin) -> D12
//   WS2812 data -> D11 via 330 Ohm   (strip apart 5 V voeden, GND gemeenschappelijk)
//   DY-SV17F IO1/RX -> D3 via 1 kOhm, IO0/TX -> D2 (mag los blijven)
//   DY-SV17F CON1 en CON2 -> GND via 10 kOhm, CON3 -> V33 via 10 kOhm
//
// Geluiden op de module: 00001.mp3, 00002.mp3, ... (nummer = bestandsnaam)
// Bibliotheek nodig: Adafruit NeoPixel (Bibliotheekbeheer)

#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>

// ---------- Pinnen ----------
const int kanaal3Pin    = 10;
const int kanaal4Pin    = 5;
const int pulPin        = 9;
const int dirPin        = 8;
const int enaPin        = 7;    // HIGH = motor stroomloos
const int gremlinLedPin = 12;   // HIGH = felle led aan (via MOSFET)
const int ledStripPin   = 11;
const int geluidRxPin   = 2;
const int geluidTxPin   = 3;

// ---------- Instellingen die je mag aanpassen ----------
const bool debug = false;           // true = ruwe pulswaarden op de Serial Monitor (9600 baud)

const int  aantalLeds      = 40;
const byte stripHelderheid = 128;   // 0-255
const int  leedsUitAanBegin = 10;   // de eerste leds van de strip blijven altijd uit
const int  actieveLeds     = aantalLeds - leedsUitAanBegin;

const int stappenPop      = 1200;
const int richtingOmhoog  = HIGH;   // zet op LOW als de gremlin de verkeerde kant op gaat
const int snelheidOmhoog  = 250;    // microseconden per halve puls (lager = sneller)
const int snelheidOmlaag  = 800;
const unsigned int gremlinLedVertraging = 500;  // ms na het begin van het omhoog gaan voor de felle led aangaat

const unsigned int flikkerMin = 30; // ms
const unsigned int flikkerMax = 140;

// Grenzen tussen de 3 zones per as, in microseconden (2 grenzen = 3 zones).
// Meet met debug = true wat jouw sticks echt halen en pas dit aan.
const int grenzenKanaal3[2] = { 1350, 1650 };   // verticaal: c, b, a
const int grenzenKanaal4[2] = { 1350, 1650 };   // horizontaal: 1, 2, 3
const int marge = 40;               // voorkomt heen-en-weer springen op een grens

const bool rijenOmgekeerd    = false;  // true als rij a bij een LAGE pulswaarde hoort
const bool kolommenOmgekeerd = false;  // true als kolom 1 bij een HOGE pulswaarde hoort

const byte bevestigingen = 4;       // zo vaak moet een nieuw vakje achter elkaar gezien worden

const byte volume = 25;             // 0 (stil) tot 30 (maximum)

// ---------- Ledstrip-effecten ----------
// Een balkje van "breedte" leds dat heen en weer loopt.
// kleur: 0 = groen, 1 = regenboog.  snelheid in leds per seconde.
// kleur: 2 = rode ademhaling, 4 = groene ademhaling (hele strip).  snelheid = ademhalingen per minuut.
// kleur: 3 = oranje knipperen (hele strip).  snelheid = knipperingen per seconde.
struct Effect {
  byte  breedte;
  byte  kleur;
  float snelheid;
};

const byte EFF_UIT       = 0;
const byte EFF_GROEN3    = 1;   // 3 leds groen, snel
const byte EFF_REGEN10   = 2;   // 10 leds regenboog
const byte EFF_REGEN10_B = 3;   // 10 leds regenboog, sneller
const byte EFF_REGEN10_C = 4;   // 10 leds regenboog, nog sneller
const byte EFF_ADEMHALING = 5;  // hele strip rood, pulserend als een ademhaling
const byte EFF_ADEM_GROEN = 6;  // hele strip groen, pulserend als een ademhaling
const byte EFF_KNIPPER_ORANJE = 7;  // hele strip oranje, knipperend
const byte EFF_ADEM_BLAUW = 8;  // hele strip blauw, pulserend als een ademhaling
const byte EFF_ADEM_ROOSWIT = 10;  // hele strip roze-wit, pulserend als een ademhaling
const byte EFF_ROOD_GROEN = 9;  // hele strip wisselt elke 500 ms tussen rood en groen

const Effect effecten[] = {
  {  0, 0,  0 },   // uit
  {  3, 0, 40 },   // groen 3 leds
  { 10, 1, 25 },   // regenboog
  { 10, 1, 45 },   // regenboog sneller
  { 10, 1, 80 },   // regenboog nog sneller
  {  0, 2, 12 },   // rode ademhaling, 12 per minuut (= 5 s per ademhaling)
  {  0, 4, 12 },   // groene ademhaling, 12 per minuut
  {  0, 3,  2 },   // oranje knipperen, 2 keer per seconde
  {  0, 5, 12 },   // blauwe ademhaling, 12 per minuut
  {  0, 6,  1 },   // rood/groen wisselen, om de 500 ms
  {  0, 7, 12 },   // roze-witte ademhaling, 12 per minuut
};

// ---------- Het raster ----------
struct Cel {
  byte geluid;     // 0 = geen geluid, anders het nummer van het mp3-bestand
  bool herhaal;    // true = blijft herhalen zolang je in het vakje staat
  byte strip;      // ledstrip-effect (EFF_...)
  bool gremlin;    // true = gremlin omhoog + felle led aan
};

#define NIETS     { 0, false, EFF_UIT, false }
#define GREMLIN   { 2, false, EFF_UIT, true  }   // schrikgeluid (2) eenmalig, gremlin omhoog

// cellen[rij][kolom]: rij 0 = c (rust), 1 = b, 2 = a;  kolom 0 = 1 ... kolom 2 = 3
//
// Alles met "VOORSTEL" heb ik zelf ingevuld op basis van je vorige raster.
// Pas gerust aan, of zet er NIETS neer als je het vakje leeg wilt laten.
const Cel cellen[3][3] = {
  //  kolom 1                                       kolom 2                                         kolom 3
  { { 2, true, EFF_UIT, false },                    { 3, true, EFF_ADEM_ROOSWIT, false },                                          { 7, true, EFF_ROOD_GROEN, false } },   // c  c1 = geluid 2, c2 = geluid 3 + roze-witte ademhaling, c3 = geluid 7 + rood/groen knipperen
  { { 1, true, EFF_ADEM_GROEN, false },             { 8, true, EFF_ADEM_BLAUW, false },             { 5, true, EFF_ROOD_GROEN, false } },   // b  b1 = geluid 1 + groene ademhaling, b2 = geluid 8 + blauwe ademhaling, b3 = geluid 5 + rood/groen knipperen
  { { 6, true, EFF_ADEMHALING, false },             NIETS,                                          GREMLIN }                               // a  a1 = geluid 6 + rode ademhaling, a2 = niets, a3 = gremlin omhoog (felle led gaat later aan)
};

// ---------- Objecten ----------
SoftwareSerial geluid(geluidRxPin, geluidTxPin);
Adafruit_NeoPixel strip(aantalLeds, ledStripPin, NEO_GRB + NEO_KHZ800);

// ---------- Toestand ----------
const byte restZone3   = rijenOmgekeerd ? 2 : 0;     // zone van rij c
const byte middenZone4 = 1;                          // kolom 2

byte huidigZ3 = restZone3;      // zones (in pulsvolgorde) van het actieve vakje
byte huidigZ4 = middenZone4;
byte kandidaatZ3 = restZone3;
byte kandidaatZ4 = middenZone4;
byte teller = 0;
byte fouten3 = 0;
byte fouten4 = 0;
bool vrijgegeven = false;       // true zodra de stick één keer in rij c stond

byte huidigGeluid = 0;
bool huidigHerhaal = false;

bool popStaat = false;
bool flikkerAan = false;
unsigned long volgendeFlikker = 0;

byte effectNu = EFF_UIT;
float stripPos = 0;
int stripRichting = 1;
unsigned long laatsteFrame = 0;
uint16_t tint = 0;
float ademFase = 0;

// ---------- Geluid ----------
void stuurCommando(byte commando, const byte *data, byte lengte) {
  byte som = 0xAA + commando + lengte;
  geluid.write((byte)0xAA);
  geluid.write(commando);
  geluid.write(lengte);
  for (byte i = 0; i < lengte; i++) {
    geluid.write(data[i]);
    som += data[i];
  }
  geluid.write(som);
  delay(20);
}

void zetVolume(byte v) {
  byte data[] = { v };
  stuurCommando(0x13, data, 1);
}

void speelGeluid(unsigned int nummer, bool herhalen) {
  byte modus[] = { (byte)(herhalen ? 0x01 : 0x02) };
  stuurCommando(0x18, modus, 1);
  byte data[] = { highByte(nummer), lowByte(nummer) };
  stuurCommando(0x07, data, 2);
}

void stopGeluid() {
  stuurCommando(0x04, nullptr, 0);
}

// ---------- Ledstrip ----------
void zetEffect(byte e) {
  effectNu = e;
  stripPos = 0;
  stripRichting = 1;
  ademFase = 0;
  strip.clear();
  strip.show();
  laatsteFrame = millis();
}

void updateStrip() {
  if (effectNu == EFF_UIT) return;
  unsigned long nu = millis();
  if (nu - laatsteFrame < 25) return;
  float dt = (nu - laatsteFrame) / 1000.0;
  if (dt > 0.1) dt = 0.1;
  laatsteFrame = nu;

  const Effect &ef = effecten[effectNu];

  if (ef.kleur == 2 || ef.kleur == 4 || ef.kleur == 5 || ef.kleur == 7) {
    // Ademhaling over de hele strip (2 = rood, 4 = groen); snelheid = ademhalingen per minuut
    ademFase += TWO_PI * (ef.snelheid / 60.0) * dt;
    if (ademFase > TWO_PI) ademFase -= TWO_PI;
    float b = (sin(ademFase - HALF_PI) + 1.0) / 2.0;   // 0..1, begint donker
    b = b * b;                                         // zachtere, natuurlijkere curve
    byte h = (byte)(b * 255);
    uint32_t c = (ef.kleur == 2) ? strip.Color(h, 0, 0) : (ef.kleur == 4) ? strip.Color(0, h, 0) : (ef.kleur == 5) ? strip.Color(0, 0, h) : strip.Color(h, (byte)(h * 0.55), (byte)(h * 0.75));
    strip.fill(c, leedsUitAanBegin, actieveLeds);
    strip.show();
    return;
  }

  if (ef.kleur == 6) {
    // Rood en groen wisselen elke 500 ms
    bool rood = (nu % 1000) < 500;
    strip.fill(rood ? strip.Color(255, 0, 0) : strip.Color(0, 255, 0), leedsUitAanBegin, actieveLeds);
    strip.show();
    return;
  }

  if (ef.kleur == 3) {
    // Oranje knipperen over de hele strip; snelheid = knipperingen per seconde
    unsigned long periode = (unsigned long)(1000.0 / ef.snelheid);
    bool aan = (nu % periode) < (periode / 2);
    strip.fill(aan ? strip.Color(255, 80, 0) : 0, leedsUitAanBegin, actieveLeds);
    strip.show();
    return;
  }

  float maxPos = actieveLeds - ef.breedte;
  stripPos += stripRichting * ef.snelheid * dt;
  if (stripPos >= maxPos) { stripPos = maxPos; stripRichting = -1; }
  if (stripPos <= 0)      { stripPos = 0;      stripRichting = 1; }

  tint += 1500;  // regenboog schuift langzaam door
  strip.clear();
  int start = (int)(stripPos + 0.5);
  for (byte i = 0; i < ef.breedte; i++) {
    uint32_t kleur;
    if (ef.kleur == 0) {
      kleur = strip.Color(0, 255, 0);
    } else {
      kleur = strip.ColorHSV((uint16_t)(tint + (uint32_t)i * 65536UL / ef.breedte));
    }
    strip.setPixelColor(leedsUitAanBegin + start + i, kleur);
  }
  strip.show();
}

// ---------- Motor ----------
void motorAan() {
  digitalWrite(enaPin, LOW);
  delay(100);
}

void motorUit() {
  digitalWrite(enaPin, HIGH);
}

// zakken = true: de felle led flikkert en de ledstrip blijft meebewegen
// ledVertraagd = true: de felle led gaat pas aan na gremlinLedVertraging ms
void zetStappen(int richting, int snelheid, bool zakken, bool ledVertraagd) {
  digitalWrite(dirPin, richting);
  delayMicroseconds(50);
  unsigned long start = millis();
  bool ledIsAan = !ledVertraagd;
  for (int i = 0; i < stappenPop; i++) {
    digitalWrite(pulPin, HIGH);
    delayMicroseconds(snelheid);
    digitalWrite(pulPin, LOW);
    delayMicroseconds(snelheid);

    if (zakken) {
      unsigned long nu = millis();
      if (nu >= volgendeFlikker) {
        flikkerAan = !flikkerAan;
        digitalWrite(gremlinLedPin, flikkerAan ? HIGH : LOW);
        volgendeFlikker = nu + random(flikkerMin, flikkerMax);
      }
      updateStrip();
    }

    if (!ledIsAan && millis() - start >= gremlinLedVertraging) {
      digitalWrite(gremlinLedPin, HIGH);
      ledIsAan = true;
    }
  }

  // Was de beweging korter dan de vertraging: led alsnog aan
  if (!ledIsAan) digitalWrite(gremlinLedPin, HIGH);
}

void popOmhoog() {
  if (popStaat) return;
  motorAan();                        // felle led blijft nog uit
  zetStappen(richtingOmhoog, snelheidOmhoog, false, true);
  popStaat = true;  // motor blijft aan om de gremlin boven te houden
}

void popOmlaag() {
  if (!popStaat) return;
  motorAan();
  volgendeFlikker = 0;
  zetStappen(richtingOmhoog == HIGH ? LOW : HIGH, snelheidOmlaag, true, false);
  popStaat = false;
  motorUit();
  digitalWrite(gremlinLedPin, LOW);
  flikkerAan = false;
}

// ---------- Zones ----------
// Bepaalt de zone (0-2) met hysterese: de grens schuift een beetje mee
// met de huidige zone zodat de waarde niet heen en weer springt.
byte bepaalZone(unsigned long puls, const int *grenzen, byte huidig) {
  byte zone = 0;
  for (byte i = 0; i < 2; i++) {
    int g = grenzen[i] + (huidig > i ? -marge : marge);
    if (puls >= (unsigned long)g) zone = i + 1;
  }
  return zone;
}

// Leest een kanaal. Geeft de zone terug.
// Eén gemiste meting wordt genegeerd (zone blijft gelijk);
// blijft het signaal weg, dan geldt de veilige zone.
byte leesZone(unsigned long puls, const int *grenzen, byte huidig,
              byte &fouten, byte veilig, bool &geldig) {
  if (puls < 800 || puls > 2200) {
    if (fouten < 255) fouten++;
    if (fouten < 10) { geldig = false; return huidig; }
    geldig = true;
    return veilig;
  }
  fouten = 0;
  geldig = true;
  return bepaalZone(puls, grenzen, huidig);
}

// ---------- Vakje activeren ----------
void gaNaarCel(byte rij, byte kol) {
  const Cel &c = cellen[rij][kol];

  Serial.print("Vakje: ");
  Serial.print("cba"[rij]);
  Serial.println(kol + 1);

  // Geluid: niet opnieuw starten als hetzelfde geluid al speelt
  if (c.geluid == 0) {
    if (huidigGeluid != 0) stopGeluid();
    huidigGeluid = 0;
  } else if (c.geluid != huidigGeluid || c.herhaal != huidigHerhaal) {
    speelGeluid(c.geluid, c.herhaal);
    huidigGeluid = c.geluid;
    huidigHerhaal = c.herhaal;
  }

  zetEffect(c.strip);

  if (c.gremlin) popOmhoog();
  else           popOmlaag();
}

void setup() {
  pinMode(kanaal3Pin, INPUT);
  pinMode(kanaal4Pin, INPUT);
  pinMode(pulPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(enaPin, OUTPUT);
  pinMode(gremlinLedPin, OUTPUT);
  digitalWrite(gremlinLedPin, LOW);
  motorUit();

  Serial.begin(9600);

  strip.begin();
  strip.setBrightness(stripHelderheid);
  strip.clear();
  strip.show();

  geluid.begin(9600);
  delay(1000);  // geluidsmodule laten opstarten
  zetVolume(volume);

  randomSeed(analogRead(A0));
}

void loop() {
  unsigned long p3 = pulseIn(kanaal3Pin, HIGH, 50000);
  unsigned long p4 = pulseIn(kanaal4Pin, HIGH, 50000);

  if (debug) {
    Serial.print("CH3: "); Serial.print(p3);
    Serial.print("  CH4: "); Serial.println(p4);
  }

  bool geldig3, geldig4;
  byte z3 = leesZone(p3, grenzenKanaal3, huidigZ3, fouten3, restZone3, geldig3);
  byte z4 = leesZone(p4, grenzenKanaal4, huidigZ4, fouten4, middenZone4, geldig4);

  updateStrip();

  // Veiligheid: na het inschakelen gebeurt er niets
  // tot de stick één keer in rij c (rust) heeft gestaan.
  if (!vrijgegeven) {
    if (geldig3 && z3 == restZone3) vrijgegeven = true;
    return;
  }

  if (z3 == huidigZ3 && z4 == huidigZ4) {
    teller = 0;
    return;
  }

  if (z3 != kandidaatZ3 || z4 != kandidaatZ4) {
    kandidaatZ3 = z3;
    kandidaatZ4 = z4;
    teller = 0;
  }
  teller++;
  if (teller >= bevestigingen) {
    teller = 0;
    huidigZ3 = z3;
    huidigZ4 = z4;
    byte rij = rijenOmgekeerd ? 2 - z3 : z3;
    byte kol = kolommenOmgekeerd ? 2 - z4 : z4;
    gaNaarCel(rij, kol);
  }
}
