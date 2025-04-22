#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LedControl.h>
#include <Keypad.h>

// LCD 16x2 (I2C)
// CRITICAL: Adjust contrast potentiometer on I2C backpack to make "Capteur inactif" visible
LiquidCrystal_I2C lcd(0x27, 16, 2);

// MAX7219 8x8 LED Matrix
#define DIN 51
#define CS 53
#define CLK 52
LedControl matrix = LedControl(DIN, CLK, CS, 1);

#define MQ7_PIN A0
#define PHOTORESISTOR_PIN A1
#define LED_PHOTORESISTOR 3

#define LED_VERT 7
#define LED_BLEU 6
#define LED_ROUGE 5
#define BUZZER 2

byte heart_big[8] = {
  0b00000000,
  0b01100110,
  0b11111111,
  0b11111111,
  0b01111110,
  0b00111100,
  0b00011000,
  0b00000000
};

byte heart_small[8] = {
  0b00000000,
  0b01100110,
  0b01111110,
  0b00111100,
  0b00011000,
  0b00000000,
  0b00000000,
  0b00000000
};

byte line[8] = {
  0b00000000,
  0b00000000,
  0b00000000,
  0b11111111,
  0b00000000,
  0b00000000,
  0b00000000,
  0b00000000
};

const byte LIGNES = 4, COLONNES = 4;
char touches[LIGNES][COLONNES] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte brochesLignes[LIGNES] = {24, 26, 28, 30};
byte brochesColonnes[COLONNES] = {25, 27, 29, 31};
Keypad clavier = Keypad(makeKeymap(touches), brochesLignes, brochesColonnes, LIGNES, COLONNES);

String codeSaisi = "";
const String codeDesactivation = "2025";
const String codeReactivation = "1234";
const String codeForceActivation = "0000";
const String codeNormalMode = "9999";

bool buzzerLocked = false;
bool forceSensorActive = false;

const int seuilGaz1 = 9;
const int seuilGaz2 = 35;
float R0 = 40536;
const int sampleInterval = 100;
const int averagingInterval = 3000;
const int displayUpdateInterval = 2000;

unsigned long previousSampleMillis = 0;
unsigned long previousDisplayMillis = 0;
unsigned long averagingStartMillis = 0;

float ppmBuffer[30];
int ppmBufferIndex = 0;
float avgPPM = 0;
bool sensorActive = false;
bool firstMeasurementTaken = false;
float firstPPM = 0;

void setup() {
  Serial.begin(9600);
  Wire.begin(); // Explicitly initialize I2C
  lcd.init();
  lcd.backlight(); // Maximum backlight brightness
  lcd.clear();
  delay(100); // Stabilize LCD initialization
  // CRITICAL: Adjust contrast potentiometer to make text visible

  matrix.shutdown(0, false);
  matrix.setIntensity(0, 1);
  matrix.clearDisplay(0);

  pinMode(LED_VERT, OUTPUT);
  pinMode(LED_BLEU, OUTPUT);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_PHOTORESISTOR, OUTPUT);
}

void loop() {
  unsigned long currentMillis = millis();

  // Lire la luminosité
  int sensorValue = analogRead(PHOTORESISTOR_PIN);
  float voltage = sensorValue * (5.0 / 1023.0);
  float Rp = (50000.0 / voltage) - 10000.0;
  float lux;
  if (voltage > 1.5) {
    lux = -0.0106 * Rp + 265.76;
  } else {
    lux = -0.00008 * Rp + 17.459;
  }
  if (lux < 0) lux = 0;

  Serial.print("Lux: ");
  Serial.println(lux);

  // Déterminer si le capteur est actif
  bool previousSensorActive = sensorActive;
  sensorActive = (lux < 40) || forceSensorActive;

  // Réinitialiser si capteur passe de actif à inactif
  if (!sensorActive && previousSensorActive) {
    avgPPM = 0;
    ppmBufferIndex = 0;
    averagingStartMillis = currentMillis;
    firstMeasurementTaken = false;
    firstPPM = 0;
  }

  // Lecture MQ-7 si capteur activé
  if (sensorActive && currentMillis - previousSampleMillis >= sampleInterval) {
    previousSampleMillis = currentMillis;

    float sensorValue = analogRead(MQ7_PIN);
    float sensor_volt = sensorValue / 1024.0 * 5.0;
    float RS_gas;
    if (sensor_volt > 0) {
      RS_gas = (5.0 - sensor_volt) / sensor_volt;
      } else {
      RS_gas = 0;
    }
    float ratio = RS_gas / R0;
    float x = 1538.46 * ratio;
    float ppm = pow(x, -1.709);

    if (ppm > 1000) {
      ppm = 1000;
    } else if (ppm < 0) {
      ppm = 0;
    }

    Serial.print("MQ7 sensorValue: ");
    Serial.print(sensorValue);
    Serial.print(" | sensor_volt: ");
    Serial.print(sensor_volt);
    Serial.print(" | RS_gas: ");
    Serial.print(RS_gas);
    Serial.print(" | ratio: ");
    Serial.print(ratio);
    Serial.print(" | ppm: ");
    Serial.println(ppm);

    // Si première mesure
    if (!firstMeasurementTaken) {
      firstPPM = ppm;
      firstMeasurementTaken = true;
      lcd.clear();
      delay(50); // Stabilize LCD
      digitalWrite(LED_PHOTORESISTOR, HIGH);
      lcd.setCursor(0, 0);
      lcd.print("PPM: ");
      lcd.print(firstPPM);
      updateDisplayBasedOnPPM(firstPPM);
      previousDisplayMillis = currentMillis;
    } else {
      // Ajouter au buffer pour moyenne
      if (ppmBufferIndex < 30) {
        ppmBuffer[ppmBufferIndex] = ppm;
        ppmBufferIndex++;
      }

      // Calculer moyenne si fenêtre de 3s terminée
      if (currentMillis - averagingStartMillis >= averagingInterval) {
        if (ppmBufferIndex > 0) {
          float sumPPM = 0;
          for (int i = 0; i < ppmBufferIndex; i++) {
            sumPPM += ppmBuffer[i];
          }
          avgPPM = sumPPM / ppmBufferIndex;
        } else {
          avgPPM = ppm;
        }

        ppmBufferIndex = 0;
        averagingStartMillis = currentMillis;

        Serial.print("avgPPM: ");
        Serial.println(avgPPM);
      }
    }
  }

  // Mise à jour affichage
  if (sensorActive && firstMeasurementTaken && currentMillis - previousDisplayMillis >= displayUpdateInterval) {
    previousDisplayMillis = currentMillis;

    lcd.clear();
    delay(50); // Stabilize LCD
    digitalWrite(LED_PHOTORESISTOR, HIGH);
    lcd.setCursor(0, 0);
    lcd.print("PPM: ");
    lcd.print(avgPPM);
    updateDisplayBasedOnPPM(avgPPM);
  } else if (!sensorActive) {
    digitalWrite(LED_PHOTORESISTOR, LOW); // Éteindre LED

      digitalWrite(LED_VERT, LOW);

      digitalWrite(LED_BLEU, LOW);

      digitalWrite(LED_ROUGE, LOW);

      digitalWrite(BUZZER, LOW);

      lcd.setCursor(0, 0);

      lcd.print("Capteur inactive");

      lcd.setCursor(0, 1);

      lcd.print("Activation:0000");
      matrix.clearDisplay(0);
    previousDisplayMillis = currentMillis;
  }

  // Vérification clavier
  lireClavier();
}

void updateDisplayBasedOnPPM(float ppm) {
  if (ppm < seuilGaz1) {
    digitalWrite(LED_VERT, HIGH);
    digitalWrite(LED_BLEU, LOW);
    digitalWrite(LED_ROUGE, LOW);
    lcd.setCursor(0, 1);
    lcd.print("PROPRE");
    digitalWrite(BUZZER, LOW);
    displayHeart(heart_big);
  } else if (ppm >= seuilGaz1 && ppm <= seuilGaz2) {
    digitalWrite(LED_VERT, LOW);
    digitalWrite(LED_BLEU, HIGH);
    digitalWrite(LED_ROUGE, LOW);
    lcd.setCursor(0, 1);
    lcd.print("MALSAIN");
    digitalWrite(BUZZER, LOW);
    displayHeart(heart_small);
  } else {
    digitalWrite(LED_VERT, LOW);
    digitalWrite(LED_BLEU, LOW);
    digitalWrite(LED_ROUGE, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("POLLUE");
    if (!buzzerLocked) {
      digitalWrite(BUZZER, HIGH);
    }
    displayHeart(line);
  }
}

void lireClavier() {
  char touche = clavier.getKey();
  if (touche) {
    if (codeSaisi.length() < 4) {
      codeSaisi += touche;
    } else {
      codeSaisi = touche;
    }

    lcd.clear();
    delay(50); // Stabilize LCD
    lcd.setCursor(0, 0);
    lcd.print("Code: ");
    lcd.print(codeSaisi);

    if (codeSaisi == codeDesactivation) {
      buzzerLocked = true;
      digitalWrite(BUZZER, LOW);
      lcd.clear();
      delay(50); // Stabilize LCD
      lcd.setCursor(0, 0);
      lcd.print("ALARME OFF");
      for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER, HIGH);
        delay(100);
        digitalWrite(BUZZER, LOW);
        delay(100);
      }
      matrix.clearDisplay(0);
      delay(2000);
      codeSaisi = "";
    }

    if (codeSaisi == codeReactivation) {
      buzzerLocked = false;
      lcd.clear();
      delay(50); // Stabilize LCD
      lcd.setCursor(0, 0);
      lcd.print("ALARME ON");
      for (int i = 0; i < 2; i++) {
        digitalWrite(BUZZER, HIGH);
        delay(300);
        digitalWrite(BUZZER, LOW);
        delay(200);
      }
      displayHeart(heart_big);
      delay(2000);
      codeSaisi = "";
    }

    if (codeSaisi == codeForceActivation) {
      forceSensorActive = true;
      lcd.clear();
      delay(50); // Stabilize LCD
      lcd.setCursor(0, 0);
      lcd.print("CAPTEUR FORCE ON");
      digitalWrite(BUZZER, HIGH);
      delay(200);
      digitalWrite(BUZZER, LOW);
      delay(2000);
      codeSaisi = "";
    }

    if (codeSaisi == codeNormalMode) {
      forceSensorActive = false;
      lcd.clear();
      delay(50); // Stabilize LCD
      lcd.setCursor(0, 0);
      lcd.print("MODE NORMAL");
      digitalWrite(BUZZER, HIGH);
      delay(200);
      digitalWrite(BUZZER, LOW);
      delay(2000);
      codeSaisi = "";
    }
  }
}

void displayHeart(byte heart[]) {
  for (int i = 0; i < 8; i++) {
    matrix.setRow(0, i, heart[i]);
  }
}
