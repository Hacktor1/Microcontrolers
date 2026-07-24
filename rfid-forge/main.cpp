/* ============================================================
 *  RFID-FORGE
 *  Univerzalni cteni / zapis / klonovani MIFARE Classic karet
 *  ESP32 (NodeMCU-32S) + MFRC522, ovladano pres web (WiFi AP + mDNS)
 *
 *  Pripojeni k RC522 (VSPI, viz README pro tabulku pinu):
 *    SDA/SS -> GPIO 5      SCK  -> GPIO 18
 *    MOSI   -> GPIO 23     MISO -> GPIO 19
 *    RST    -> GPIO 22     3V3  -> 3.3V (NIKDY 5V!)   GND -> GND
 *
 *  ESP32 se pripoji k TVE domaci WiFi (vyplň WIFI_SSID / WIFI_PASSWORD
 *  nize). Po pripojeni otevri http://rfid.local, nebo IP adresu,
 *  kterou vypise seriovy monitor (115200 baud).
 *
 *  Pokud se pripojeni k domaci WiFi nepovede do 15s, zarizeni samo
 *  spusti zalozni hotspot "RFID-FORGE-SETUP" (heslo clone1234), aby
 *  slo dostupne i bez funkcni domaci site - pripoj se na nej a otevri
 *  http://192.168.4.1 pro diagnostiku.
 * ============================================================ */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

// ---------------- Piny ----------------
#define RFID_SS_PIN   5
#define RFID_RST_PIN  22
#define STATUS_LED    2

// ---------------- WiFi ----------------
// !!! VYPLN SVOJI DOMACI WIFI !!!
const char* WIFI_SSID     = "TVOJE_WIFI_JMENO";
const char* WIFI_PASSWORD = "TVOJE_WIFI_HESLO";

// Zalozni hotspot, ktery se sam spusti, pokud se STA pripojeni nepovede
// do WIFI_CONNECT_TIMEOUT_MS (napr. spatne heslo, WiFi mimo dosah...).
const char* AP_SSID     = "RFID-FORGE-SETUP";
const char* AP_PASSWORD = "clone1234";        // min. 8 znaku (WPA2)
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

const char* MDNS_NAME   = "rfid";             // http://rfid.local

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
MFRC522::MIFARE_Key masterKey;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Nejcastejsi tovarni / verejne zname klice pro MIFARE Classic
const byte KNOWN_KEYS[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00}
};
const int KNOWN_KEYS_COUNT = sizeof(KNOWN_KEYS) / sizeof(KNOWN_KEYS[0]);

// ---------------- Stavovy automat ----------------
enum Mode { IDLE, WAIT_READ, WAIT_WRITE_BLOCK, WAIT_CLONE_WRITE, WAIT_FORMAT };
volatile Mode mode = IDLE;

bool pendingOverwriteUID = false;
int  pendingBlock = -1;
byte pendingData[16];

// Ulozeny dump karty - MIFARE 1K = 16 sektoru * 4 bloky = 64 bloku po 16B
// (u 4K karet se v teto verzi zpracuje jen prvnich 16 sektoru / 64 bloku)
#define TOTAL_BLOCKS 64
byte dump[TOTAL_BLOCKS][16];
bool dumpValid[TOTAL_BLOCKS];
bool haveDump = false;
String dumpUID = "";

// ---------------- Pomocne funkce ----------------

String bytesToHex(byte *buf, byte len) {
  String s;
  for (byte i = 0; i < len; i++) {
    if (buf[i] < 0x10) s += "0";
    s += String(buf[i], HEX);
  }
  s.toUpperCase();
  return s;
}

void hexToBytes(const String &hex, byte *out, int outLen) {
  for (int i = 0; i < outLen; i++) {
    out[i] = (byte) strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  }
}

void notifyAll(const String &msg) {
  ws.textAll(msg);
}

void sendStatus(const String &msg) {
  JsonDocument doc;
  doc["event"] = "status";
  doc["msg"] = msg;
  String out;
  serializeJson(doc, out);
  notifyAll(out);
}

void sendError(const String &msg) {
  JsonDocument doc;
  doc["event"] = "error";
  doc["msg"] = msg;
  String out;
  serializeJson(doc, out);
  notifyAll(out);
}

void blinkLed() {
  digitalWrite(STATUS_LED, LOW);
  delay(60);
  digitalWrite(STATUS_LED, HIGH);
}

byte sectorCountFor(MFRC522::PICC_Type type) {
  byte s;
  switch (type) {
    case MFRC522::PICC_TYPE_MIFARE_MINI: s = 5; break;
    case MFRC522::PICC_TYPE_MIFARE_1K:   s = 16; break;
    case MFRC522::PICC_TYPE_MIFARE_4K:   s = 40; break;
    default: s = 16; break;
  }
  if (s > 16) s = 16; // v1: pracujeme jen s prvnimi 16 sektory (64 bloku)
  return s;
}

inline byte blockAddrForSectorBlock(byte sector, byte blockInSector) {
  return sector * 4 + blockInSector;
}

bool authenticateBlock(byte blockAddr, byte &usedKeyIndex) {
  for (byte k = 0; k < KNOWN_KEYS_COUNT; k++) {
    for (byte i = 0; i < 6; i++) masterKey.keyByte[i] = KNOWN_KEYS[k][i];
    MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &masterKey, &(mfrc522.uid));
    if (status == MFRC522::STATUS_OK) {
      usedKeyIndex = k;
      return true;
    }
    mfrc522.PCD_StopCrypto1();
  }
  return false;
}

bool writeBlockRaw(byte blockAddr, byte *data16) {
  MFRC522::StatusCode status = mfrc522.MIFARE_Write(blockAddr, data16, 16);
  return status == MFRC522::STATUS_OK;
}

// Odemykaci sekvence pro "magic" Gen1A karty (specialni prazdne karty
// urcene vyrobcem primo pro prepis UID / klonovani). Na normalne
// zabezpecenych kartach tento prikaz nema zadny efekt.
bool mfrc522OpenBackdoor() {
  byte cmd = 0x40;
  byte validBits = 7;
  byte received[2];
  byte receivedLength = 2;
  mfrc522.PCD_TransceiveData(&cmd, 1, received, &receivedLength, &validBits, 0, false);

  cmd = 0x43;
  validBits = 8;
  receivedLength = 2;
  mfrc522.PCD_TransceiveData(&cmd, 1, received, &receivedLength, &validBits, 0, false);
  return true;
}

// ---------------- Hlavni akce ----------------

void doReadDump() {
  memset(dumpValid, 0, sizeof(dumpValid));
  haveDump = false;

  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  byte sectors = sectorCountFor(piccType);
  dumpUID = bytesToHex(mfrc522.uid.uidByte, mfrc522.uid.size);

  JsonDocument doc;
  doc["event"] = "dump";
  doc["uid"] = dumpUID;
  doc["type"] = String(mfrc522.PICC_GetTypeName(piccType));
  JsonArray blocks = doc["blocks"].to<JsonArray>();

  for (byte s = 0; s < sectors; s++) {
    byte trailerBlock = blockAddrForSectorBlock(s, 3);
    byte usedKey;
    bool ok = authenticateBlock(trailerBlock, usedKey);

    for (byte b = 0; b < 4; b++) {
      byte blockAddr = blockAddrForSectorBlock(s, b);
      JsonObject jb = blocks.add<JsonObject>();
      jb["idx"] = blockAddr;
      jb["sector"] = s;
      jb["trailer"] = (b == 3);

      if (!ok) {
        jb["auth"] = false;
        dumpValid[blockAddr] = false;
        continue;
      }

      byte buffer[18];
      byte size = 18;
      MFRC522::StatusCode status = mfrc522.MIFARE_Read(blockAddr, buffer, &size);
      jb["auth"] = true;
      if (status == MFRC522::STATUS_OK) {
        memcpy(dump[blockAddr], buffer, 16);
        dumpValid[blockAddr] = true;
        jb["hex"] = bytesToHex(buffer, 16);
      } else {
        jb["error"] = "read_fail";
        dumpValid[blockAddr] = false;
      }
    }
    mfrc522.PCD_StopCrypto1();
  }

  haveDump = true;
  String out;
  serializeJson(doc, out);
  notifyAll(out);

  JsonDocument done;
  done["event"] = "done";
  done["action"] = "read";
  done["ok"] = true;
  String o2;
  serializeJson(done, o2);
  notifyAll(o2);
}

void doWriteCustomBlock(int blockAddr, byte *data16) {
  byte usedKey;
  byte trailerBlock = (blockAddr / 4) * 4 + 3;
  if (!authenticateBlock(trailerBlock, usedKey)) {
    sendError("Autentizace sektoru selhala (blok " + String(blockAddr) + ").");
    return;
  }
  bool ok = writeBlockRaw(blockAddr, data16);
  mfrc522.PCD_StopCrypto1();

  JsonDocument done;
  done["event"] = "done";
  done["action"] = "write";
  done["ok"] = ok;
  done["block"] = blockAddr;
  String o;
  serializeJson(done, o);
  notifyAll(o);
}

void doCloneWrite(bool overwriteUID) {
  if (!haveDump) {
    sendError("Neni ulozeny zadny dump. Nejdriv over 'Precist kartu'.");
    return;
  }

  if (overwriteUID) {
    sendStatus("Odemykam blok 0 (predpoklad: magic/UID karta)...");
    mfrc522OpenBackdoor();
    if (dumpValid[0]) {
      bool ok = writeBlockRaw(0, dump[0]);
      sendStatus(ok ? "Blok 0 (UID) zapsan."
                    : "Zapis bloku 0 selhal - karta pravdepodobne neni typu 'magic'.");
    }
  }

  int written = 0, failed = 0, skipped = 0;
  for (int blockAddr = 1; blockAddr < TOTAL_BLOCKS; blockAddr++) {
    if (!dumpValid[blockAddr]) { skipped++; continue; }

    byte trailerBlock = (blockAddr / 4) * 4 + 3;
    byte usedKey;
    if (!authenticateBlock(trailerBlock, usedKey)) {
      failed++;
      continue;
    }

    bool ok = writeBlockRaw(blockAddr, dump[blockAddr]);
    mfrc522.PCD_StopCrypto1();
    if (ok) written++; else failed++;

    JsonDocument prog;
    prog["event"] = "progress";
    prog["block"] = blockAddr;
    prog["total"] = TOTAL_BLOCKS;
    String o;
    serializeJson(prog, o);
    notifyAll(o);
  }

  JsonDocument done;
  done["event"] = "done";
  done["action"] = "clone";
  done["ok"] = failed == 0;
  done["written"] = written;
  done["failed"] = failed;
  done["skipped"] = skipped;
  String o;
  serializeJson(done, o);
  notifyAll(o);
}

void doFormatCard() {
  const byte emptyBlock[16] = {0};
  const byte defaultTrailer[16] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,   // Key A (tovarni)
    0xFF,0x07,0x80,0x69,             // Access bits (tovarni)
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF    // Key B (tovarni)
  };

  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  byte sectors = sectorCountFor(piccType);
  int okCount = 0, failCount = 0;

  for (byte s = 0; s < sectors; s++) {
    byte trailerBlock = blockAddrForSectorBlock(s, 3);
    byte usedKey;
    if (!authenticateBlock(trailerBlock, usedKey)) { failCount++; continue; }

    for (byte b = 0; b < 3; b++) {
      if (s == 0 && b == 0) continue; // blok 0 (vyrobni) se nikdy neprepisuje
      byte blockAddr = blockAddrForSectorBlock(s, b);
      if (writeBlockRaw(blockAddr, (byte*)emptyBlock)) okCount++; else failCount++;
    }
    if (writeBlockRaw(trailerBlock, (byte*)defaultTrailer)) okCount++; else failCount++;
    mfrc522.PCD_StopCrypto1();
  }

  JsonDocument done;
  done["event"] = "done";
  done["action"] = "format";
  done["ok"] = failCount == 0;
  done["written"] = okCount;
  done["failed"] = failCount;
  String o;
  serializeJson(done, o);
  notifyAll(o);
}

// ---------------- WebSocket ----------------

void handleWsMessage(uint8_t *data, size_t len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) { sendError("Neplatny JSON prikaz."); return; }

  String cmd = doc["cmd"] | "";

  if (cmd == "read") {
    mode = WAIT_READ;
    sendStatus("Priloz kartu ke cteni...");
  } else if (cmd == "write_block") {
    pendingBlock = doc["block"] | -1;
    String hex = doc["hex"] | "";
    if (pendingBlock <= 0 || pendingBlock % 4 == 3 || pendingBlock >= TOTAL_BLOCKS || hex.length() != 32) {
      sendError("Neplatny blok/data. (16 bajtu = 32 hex znaku; blok 0 a trailery [3,7,11,...] takto zapsat nelze.)");
      return;
    }
    hexToBytes(hex, pendingData, 16);
    mode = WAIT_WRITE_BLOCK;
    sendStatus("Priloz kartu k zapisu bloku " + String(pendingBlock) + "...");
  } else if (cmd == "clone_write") {
    pendingOverwriteUID = doc["overwrite_uid"] | false;
    mode = WAIT_CLONE_WRITE;
    sendStatus("Priloz cilovou kartu ke klonovani...");
  } else if (cmd == "format") {
    mode = WAIT_FORMAT;
    sendStatus("Priloz kartu k formatovani na tovarni nastaveni...");
  } else if (cmd == "cancel") {
    mode = IDLE;
    sendStatus("Zruseno. Cekam na dalsi prikaz.");
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    sendStatus("Terminal pripojen k RFID-FORGE.");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*) arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      handleWsMessage(data, len);
    }
  }
}

// ---------------- WiFi pripojeni ----------------

bool connectHomeWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Pripojuji se k WiFi '");
  Serial.print(WIFI_SSID);
  Serial.print("' ");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Pripojeno! IP adresa: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("Pripojeni k domaci WiFi selhalo (spatne jmeno/heslo, nebo mimo dosah).");
  return false;
}

void startFallbackAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("Spoustim zalozni hotspot:");
  Serial.print("  SSID: "); Serial.println(AP_SSID);
  Serial.print("  Heslo: "); Serial.println(AP_PASSWORD);
  Serial.print("  IP adresa: "); Serial.println(WiFi.softAPIP());
}

// ---------------- Setup / Loop ----------------

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  SPI.begin();
  mfrc522.PCD_Init();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount selhal! (nahral jsi filesystem image? -> pio run -t uploadfs)");
  } else if (!LittleFS.exists("/index.html")) {
    Serial.println("POZOR: /index.html na LittleFS neni! Spust 'pio run -t uploadfs' a restartuj.");
  } else {
    Serial.println("LittleFS OK, index.html nalezen.");
  }

  bool connected = connectHomeWifi();
  if (!connected) startFallbackAP();

  if (MDNS.begin(MDNS_NAME)) {
    Serial.println("mDNS bezi: http://" + String(MDNS_NAME) + ".local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("mDNS se nepodarilo spustit (pouzij IP adresu vyse).");
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Explicitni root route (nezavisi na chovani ruznych verzi
  // setDefaultFile - jistota, ze se stranka nacte i kdyby serveStatic
  // default-file mechanika na dane verzi knihovny nefungovala).
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html");
    } else {
      request->send(500, "text/plain",
        "index.html chybi na LittleFS - spust 'pio run -t uploadfs' a restartuj ESP32.");
    }
  });

  server.serveStatic("/", LittleFS, "/");
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Nenalezeno");
  });

  server.begin();
  digitalWrite(STATUS_LED, HIGH);
}

void loop() {
  ws.cleanupClients();

  if (mode == IDLE) return;

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  blinkLed();

  switch (mode) {
    case WAIT_READ:
      doReadDump();
      break;
    case WAIT_WRITE_BLOCK:
      doWriteCustomBlock(pendingBlock, pendingData);
      break;
    case WAIT_CLONE_WRITE:
      doCloneWrite(pendingOverwriteUID);
      break;
    case WAIT_FORMAT:
      doFormatCard();
      break;
    default:
      break;
  }

  mode = IDLE;
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}
