#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN 9
#define SS_PIN 10

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("--------------------------------");
  Serial.println("    Parking Fee Calculator");
  Serial.println("--------------------------------");
  Serial.println("Scan the RFID card to start...");
  Serial.println("--------------------------------");

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Verify UID
  byte expectedUID[] = {0x23, 0x5F, 0x87, 0xF5}; // Update if needed
  bool uidMatch = true;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] != expectedUID[i]) {
      uidMatch = false;
      break;
    }
  }

  if (!uidMatch) {
    Serial.println("This card's UID does not match expected UID.");
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // Authenticate for sector 1
  byte blockAddr = 4;
  MFRC522::StatusCode status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Authentication failed: ");
    Serial.println(rfid.GetStatusCodeName(status));
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // Read license plate (block 4)
  byte buffer[18];
  byte size = 18;
  status = rfid.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Read failed (block 4): ");
    Serial.println(rfid.GetStatusCodeName(status));
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  String licensePlate = "";
  for (byte i = 0; i < 16; i++) {
    if (buffer[i] != ' ') licensePlate += (char)buffer[i];
  }

  // Read cash amount (block 5)
  blockAddr = 5;
  status = rfid.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Read failed (block 5): ");
    Serial.println(rfid.GetStatusCodeName(status));
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  String cashStr = "";
  for (byte i = 0; i < 16; i++) {
    if (buffer[i] != ' ') cashStr += (char)buffer[i];
  }
  long currentCash = cashStr.toInt();

  // Send data to Python
  Serial.print("DATA:");
  Serial.print(licensePlate);
  Serial.print(",");
  Serial.println(currentCash);

  // Wait for CHARGE from Python
  while (Serial.available() == 0) {}
  String chargeStr = Serial.readStringUntil('\n');
  if (!chargeStr.startsWith("CHARGE:")) {
    Serial.println("Invalid charge format received");
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  long charge = chargeStr.substring(7).toInt();

  // Update cash amount
  long newCash = currentCash - charge;
  if (newCash < 0) {
    Serial.println("Insufficient funds!");
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // Write new cash amount (block 5)
  String newCashStr = String(newCash);
  for (byte i = 0; i < 16; i++) {
    buffer[i] = (i < newCashStr.length()) ? newCashStr[i] : ' ';
  }
  blockAddr = 5;
  status = rfid.MIFARE_Write(blockAddr, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Write failed: ");
    Serial.println(rfid.GetStatusCodeName(status));
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // Send DONE signal
  Serial.println("DONE");

  // Print transaction details
  Serial.println("--------------------------------");
  Serial.println("    Parking Fee Calculation");
  Serial.println("--------------------------------");
  Serial.print("License Plate: ");
  Serial.println(licensePlate);
  Serial.print("Charge: ");
  Serial.print(charge);
  Serial.println(" units");
  Serial.print("Previous Balance: ");
  Serial.print(currentCash);
  Serial.println(" units");
  Serial.print("New Balance: ");
  Serial.print(newCash);
  Serial.println(" units");
  Serial.println("--------------------------------");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(1000);
}