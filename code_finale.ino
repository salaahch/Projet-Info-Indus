#include <Wire.h>

#include <LiquidCrystal_I2C.h>

#include <LedControl.h>

#include <Keypad.h>



// LCD 16x2 (I2C)

// Note: Ensure 0x27 is the correct I2C address for your LCD; use an I2C scanner if it doesn't work.

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

  0b00011000,

  0b00111100,

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

const String codeForceActivation = "0000"; // Code pour activer le capteur

const String codeNormalMode = "9999";      // Nouveau code pour revenir au mode normal



bool buzzerLocked = false;

bool forceSensorActive = false; // Variable pour suivre l'activation forcée



const int seuilGaz1 = 9;

const int seuilGaz2 = 35;

// Note: R0 = 40536 seems high; ensure it's calibrated for your MQ-7 sensor in clean air.

float R0 = 40536;

const int sampleInterval = 100;    // Échantillon MQ-7 toutes les 100ms

const int averagingInterval = 3000; // Moyenne sur 3 secondes (3000ms)

const int displayUpdateInterval = 2000; // Mise à jour affichage toutes les 2s



unsigned long previousSampleMillis = 0;

unsigned long previousDisplayMillis = 0;

unsigned long averagingStartMillis = 0; // Début de la fenêtre de moyenne



float ppmBuffer[30]; // Buffer pour stocker jusqu'à 30 échantillons (3s / 100ms)

int ppmBufferIndex = 0; // Index actuel dans le buffer

float avgPPM = 0;

bool sensorActive = false;



void setup() {

  Serial.begin(9600);

  lcd.init();

  lcd.backlight();



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



  // Lecture instantanée de la luminosité

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



  // Log pour débogage

  Serial.print("Lux: ");

  Serial.println(lux);



  // Activation immédiate basée sur lux ou activation forcée

  sensorActive = (lux < 20) || forceSensorActive;



  if (!sensorActive) {

    // Réinitialiser PPM si capteur désactivé

    avgPPM = 0;

    ppmBufferIndex = 0;

    averagingStartMillis = currentMillis; // Réinitialiser le timer

  }



  // Lecture MQ-7 si capteur activé

  if (sensorActive && currentMillis - previousSampleMillis >= sampleInterval) {

    previousSampleMillis = currentMillis;



    float sensorValue = analogRead(MQ7_PIN);

    float sensor_volt = sensorValue / 1024.0 * 5.0;

    float RS_gas;

    // Prevent division by zero

    if (sensor_volt > 0) {

      RS_gas = (5.0 - sensor_volt) / sensor_volt;

    } else {

      RS_gas = 0; // Default to 0 if voltage is invalid

    }

    float ratio = RS_gas / R0;

    float x = 1538.46 * ratio;

    float ppm = pow(x, -1.709);



    // Cap PPM to a logical maximum (e.g., 1000 PPM) to prevent unrealistic values

    if (ppm > 1000) {

      ppm = 1000;

    } else if (ppm < 0) {

      ppm = 0; // Ensure no negative PPM values

    }



    // Logs pour débogage

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



    // Ajouter la lecture au buffer

    if (ppmBufferIndex < 30) { // Limiter à 30 échantillons pour éviter le dépassement

      ppmBuffer[ppmBufferIndex] = ppm;

      ppmBufferIndex++;

    }



    // Vérifier si la fenêtre de 3 secondes est terminée

    if (currentMillis - averagingStartMillis >= averagingInterval) {

      // Calculer la moyenne

      if (ppmBufferIndex > 0) {

        float sumPPM = 0;

        for (int i = 0; i < ppmBufferIndex; i++) {

          sumPPM += ppmBuffer[i];

        }

        avgPPM = sumPPM / ppmBufferIndex;

      } else {

        avgPPM = 0; // Pas de lectures valides

      }



      // Réinitialiser pour la prochaine fenêtre

      ppmBufferIndex = 0;

      averagingStartMillis = currentMillis;



      // Log pour débogage

      Serial.print("avgPPM: ");

      Serial.println(avgPPM);

    }

  }



  // Mise à jour affichage

  if (currentMillis - previousDisplayMillis >= displayUpdateInterval) {

    previousDisplayMillis = currentMillis;



    lcd.clear();

    if (sensorActive) {

      digitalWrite(LED_PHOTORESISTOR, HIGH); // Allumer LED

      lcd.setCursor(0, 0);

      lcd.print("PPM: ");

      lcd.print(avgPPM);



      if (avgPPM < seuilGaz1) {

        digitalWrite(LED_VERT, HIGH);

        digitalWrite(LED_BLEU, LOW);

        digitalWrite(LED_ROUGE, LOW);

        lcd.setCursor(0, 1);

        lcd.print("PROPRE");

        digitalWrite(BUZZER, LOW);

        displayHeart(heart_big);

      }

      else if (avgPPM >= seuilGaz1 && avgPPM <= seuilGaz2) {

        digitalWrite(LED_VERT, LOW);

        digitalWrite(LED_BLEU, HIGH);

        digitalWrite(LED_ROUGE, LOW);

        lcd.setCursor(0, 1);

        lcd.print("MALSAIN");

        digitalWrite(BUZZER, LOW);

        displayHeart(heart_small);

      }

      else {

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

    } else {

      digitalWrite(LED_PHOTORESISTOR, LOW); // Éteindre LED

      digitalWrite(LED_VERT, LOW);

      digitalWrite(LED_BLEU, LOW);

      digitalWrite(LED_ROUGE, LOW);

      digitalWrite(BUZZER, LOW);

      lcd.setCursor(0, 0);

      lcd.print("Capteur non");

      lcd.setCursor(0, 1);

      lcd.print("active");

      matrix.clearDisplay(0);

    }

  }



  // Vérification clavier

  lireClavier();

}



void lireClavier() {

  char touche = clavier.getKey();

  if (touche) {

    // Prevent buffer overflow by limiting to 4 characters

    if (codeSaisi.length() < 4) {

      codeSaisi += touche;

    } else {

      codeSaisi = touche; // Reset to the latest key if buffer is full

    }



    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print("Code: ");

    lcd.print(codeSaisi);



    if (codeSaisi == codeDesactivation) {

      buzzerLocked = true;

      digitalWrite(BUZZER, LOW);

      lcd.clear();

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

      forceSensorActive = true; // Activer le capteur même si luminosité élevée

      lcd.clear();

      lcd.setCursor(0, 0);

      lcd.print("CAPTEUR FORCE ON");

      digitalWrite(BUZZER, HIGH);

      delay(200); // Un bip unique pour confirmation

      digitalWrite(BUZZER, LOW);

      delay(2000);

      codeSaisi = "";

    }



    if (codeSaisi == codeNormalMode) {

      forceSensorActive = false; // Revenir au mode normal (contrôle par photorésistance)

      lcd.clear();

      lcd.setCursor(0, 0);

      lcd.print("MODE NORMAL");

      digitalWrite(BUZZER, HIGH);

      delay(200); // Un bip unique pour confirmation

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
