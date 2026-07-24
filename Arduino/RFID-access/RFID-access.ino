#include <SPI.h>
#include <MFRC522.h>
#include "pitches.h"

#define SS_PIN 10
#define RST_PIN 9
#define BUZZER_PIN 8

MFRC522 rfid(SS_PIN, RST_PIN);

byte targetUID[4] = {0x19, 0x95, 0x07, 0xA4};


int successMelody[] = { NOTE_A4, NOTE_CS5, NOTE_E5, NOTE_A5 };
int successDurations[] = { 16, 16, 16, 8 };

int failureMelody[] = { NOTE_DS4, NOTE_D4, NOTE_CS4 };
int failureDurations[] = { 6, 6, 3 };

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  pinMode(BUZZER_PIN, OUTPUT);
 
  Serial.println(F("========================================"));
  Serial.println(F("   HACKTOR OS v4.2.0 - INITIALIZED    "));
  Serial.println(F("   WAITING FOR RFID INTERFACE...   "));
  Serial.println(F("========================================"));
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  Serial.print(F("[!] Detekován signál... UID: "));
  printHex(rfid.uid.uidByte, rfid.uid.size);

  if (checkUID(rfid.uid.uidByte, targetUID)) {
    Serial.println(F("[>>>] ACCESS GRANTED // DECRYPTING DATA..."));
    playSuccessTune();
  } else {
    Serial.println(F("[!!!] ICE ALERT: ACCESS DENIED // TRACE DETECTED!"));
    playFailureTune();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

bool checkUID(byte *readUID, byte* target) {
  for (byte i = 0; i < 4; i++) {
    if (readUID[i] != target[i]) {
      return false;
    }
  }
  return true;
}

void playSuccessTune() {
  int size = sizeof(successDurations) / sizeof(int);
  for (int note = 0; note < size; note++) {
    int duration = 1000 / successDurations[note];
    tone(BUZZER_PIN, successMelody[note], duration);
   
    int pauseBetweenNotes = duration * 1.15;
    delay(pauseBetweenNotes);
    noTone(BUZZER_PIN);
  }
}

void playFailureTune() {
  int size = sizeof(failureDurations) / sizeof(int);
  for (int note = 0; note < size; note++) {
    int duration = 1000 / failureDurations[note];
    tone(BUZZER_PIN, failureMelody[note], duration);
   
    int pauseBetweenNotes = duration * 1.30;
    delay(pauseBetweenNotes);
    noTone(BUZZER_PIN);
  }
}

void printHex(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println();
}
