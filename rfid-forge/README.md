# RFID-FORGE — univerzální RFID nástroj pro ESP32

Firmware pro **NodeMCU ESP-32S** + čtečku **RC522 (MFRC522)**, ovládaný přes
webové rozhraní. ESP32 vytvoří vlastní WiFi hotspot a mDNS jméno, takže se
připojíš telefonem/PC přímo k němu — internet ani domácí síť nejsou potřeba.

## Funkce

- **Číst** — přiložením karty přečte UID + obsah všech sektorů (zkouší
  několik nejběžnějších/tovarních klíčů)
- **Překlad dat** — přepínač HEX / DEC / ASCII / BIN pro každý přečtený blok
- **Zapsat blok** — ručně napsat 16 bajtů (32 hex znaků) do vybraného bloku
- **Klonovat** — uloží dump ze zdrojové karty a zapíše ho na cílovou; volitelně
  i UID (blok 0), pokud je cílová karta typu „magic“/Gen1A (speciální
  přepisovatelné karty prodávané přímo pro tento účel)
- **Formátovat** — vrátí kartu na tovární klíče/access bity a vynuluje data

> Používej pouze na kartách, které vlastníš, nebo k tomu máš svolení
> (záloha vlastní přístupové karty, testování vlastního systému apod.).

## Pinout — ESP32 (NodeMCU-32S) ↔ RC522

RC522 běží na **VSPI** (hardwarové SPI piny ESP32). RC522 modul napájej
**pouze 3.3V** — 5V ho může poškodit.

| RC522 pin | ESP32 GPIO      | Poznámka                    |
|-----------|-----------------|------------------------------|
| SDA (SS)  | GPIO 5          | chip select                 |
| SCK       | GPIO 18         | SPI clock                   |
| MOSI      | GPIO 23         | SPI data (ESP32 → RC522)    |
| MISO      | GPIO 19         | SPI data (RC522 → ESP32)    |
| IRQ       | nezapojeno      | nepoužito                   |
| GND       | GND             |                              |
| RST       | GPIO 22         | reset                       |
| 3.3V      | 3.3V            | **NIKDY 5V**                |

Volitelně:

| Funkce      | ESP32 GPIO | Poznámka                         |
|-------------|-----------|-----------------------------------|
| Status LED  | GPIO 2    | většina devboardů má LED přímo zde |

## Struktura projektu

```
rfid-forge/
├── platformio.ini
├── src/
│   └── main.cpp        # firmware (WiFi AP, mDNS, RFID logika, WebSocket API)
└── data/
    └── index.html       # webová stránka (nahrává se zvlášť do LittleFS)
```

## Sestavení a nahrání (PlatformIO)

1. Otevři složku `rfid-forge` v PlatformIO (VS Code rozšíření nebo CLI).
2. Zapoj RC522 podle tabulky výše a připoj ESP32 přes USB.
3. Nahraj firmware:
   ```
   pio run -t upload
   ```
4. **Nahraj i webovou stránku do LittleFS** (samostatný krok, jinak stránka
   nebude k dispozici):
   ```
   pio run -t uploadfs
   ```
   V PlatformIO IDE je to tlačítko „Upload Filesystem Image“.
5. Otevři sériový monitor (`pio device monitor`) — uvidíš IP adresu a
   potvrzení, že mDNS běží.

## Připojení

- WiFi síť: **`RFID-FORGE`**, heslo **`clone1234`** (uprav si v `main.cpp`
  v proměnných `AP_SSID` / `AP_PASSWORD`)
- Otevři v prohlížeči: **`http://rfid.local`**
- Pokud mDNS v tvém prohlížeči/OS nefunguje (občas problém na Androidu),
  použij přímo IP: **`http://192.168.4.1`**

## Jak to funguje uvnitř

Prohlížeč komunikuje s ESP32 přes WebSocket (`/ws`) v jednoduchém JSON
protokolu:

**Prohlížeč → ESP32**
```json
{"cmd":"read"}
{"cmd":"write_block","block":4,"hex":"00112233445566778899AABBCCDDEEFF"}
{"cmd":"clone_write","overwrite_uid":false}
{"cmd":"format"}
{"cmd":"cancel"}
```

**ESP32 → prohlížeč**
```json
{"event":"status","msg":"..."}
{"event":"dump","uid":"...","type":"...","blocks":[{"idx":0,"sector":0,"trailer":false,"auth":true,"hex":"..."}]}
{"event":"progress","block":12,"total":64}
{"event":"done","action":"read|write|clone|format","ok":true}
{"event":"error","msg":"..."}
```

Firmware čeká v `loop()` na přiložení karty jen když je nastaven konkrétní
režim (read/write/clone/format) — mimo to RC522 jen nečinně čeká.

## Omezení této verze

- Cílí na **MIFARE Classic 1K** (nejběžnější — modré/bílé přívěsky,
  většina jednoduchých docházkových/přístupových karet). U 4K karet se
  zpracuje jen prvních 16 sektorů (64 bloků).
  MIFARE Ultralight / NTAG (jiná paměťová struktura) nejsou podporovány.
- Přepis UID (bloku 0) funguje **pouze** na speciálních „magic“/Gen1A
  kartách určených výrobcem k přepisu UID — na běžných zabezpečených
  kartách to hardware neumožní (to je záměr).
- Autentizace zkouší jen malou sadu nejběžnějších/továrních klíčů;
  karty se skutečně vlastními (neznámými) klíči nepřečte.
