// RFID library
#include <SPI.h>
#include <MFRC522.h>
// Servo library
#include <Servo.h>
// DHT/Temp & humidity libraries
#include <DHT.h>
#include <DHT_U.h>
// Barometer libraries
#include <SPL06-007.h>
#include <Wire.h>

//RFID stuff
#define RST_PIN 5
#define SS_PIN 53
MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance
byte savedCardBytes[8] = { 0 };    // Variable to store the saved card bytes
byte cardbytes[8] = { 0 };

//Servo setup
#define ServoPin 3
Servo MyServo;

//Fan stuff
#define fanPWM 2
#define fanAnalog A0

//Potentiometer stuff
#define potentiometer A15

//DHT stuff
#define DHTTYPE DHT11
#define DHTPIN 40
DHT_Unified dht(DHTPIN, DHTTYPE);

// Define pin numbers for buttons and LEDs
const int buttonPins[] = { 46, 48, 49, 47 };  // Pin numbers for the buttons
const int ledPins[] = { 25, 23, 22, 24 };     // Pin numbers for the LEDs
const int switchPin[] = { 45, 43, 41, 39 };
const int numberOfSwitches = 4;

// Variables will change:
int ledStates[] = { LOW, LOW, LOW, LOW };         // Array for the LED status
int buttonStates[] = { LOW, LOW, LOW, LOW };      // Array for the button status
int lastButtonStates[] = { LOW, LOW, LOW, LOW };  // Array for the previous button state
int isSwitchOn[numberOfSwitches];
bool lock[] = { false, false, false, false };

// Variables for debouncing
unsigned long lastDebounceTimes[] = { 0, 0, 0, 0 };  // Arrays to store the last debounce times
unsigned long debounceDelay = 100;                   // The debounce time; increase if the output flickers

void setup() {
  Serial.begin(9600);
  // Initialize the button and LED pins
  for (int i = 0; i < numberOfSwitches; i++) {
    pinMode(ledPins[i], OUTPUT);
    pinMode(buttonPins[i], INPUT);
    pinMode(switchPin[i], INPUT);
    digitalWrite(ledPins[i], ledStates[i]);
  }

  MyServo.attach(ServoPin);
  SPI.begin();         // Start SPI
  mfrc522.PCD_Init();  // Start RC522 module to get RFID capabilities
  mfrc522.PCD_DumpVersionToSerial();

  //Start the DHT 11 sensor to get temperature and humidity data
  dht.begin();

  //Start barometer
  Wire.begin();
  SPL_init();
}

void loop() {
  for (int i = 0; i < numberOfSwitches; i++) {
    isSwitchOn[i] = digitalRead(switchPin[i]);
  }
  if (!(isSwitchOn[0] || isSwitchOn[1] || isSwitchOn[2] || isSwitchOn[3])) {
    controlServoWithFan();
  }

  // 1. Read a card and save what sensors should be activated upon reading it in 2.
  if (isSwitchOn[0] && !(isSwitchOn[1] || isSwitchOn[2] || isSwitchOn[3])) {
    //Save new config for a card
    checkSensorSetup();
  } else {
    for (int i = 0; i < 4; i++) {
      ledStates[i] = LOW;
      buttonStates[i] = LOW;
      lastButtonStates[i] = LOW;
    }
  }

  // 2. Read a card and read the sensors based on what's in the db
  if (isSwitchOn[1] && !(isSwitchOn[0] || isSwitchOn[2] || isSwitchOn[3])) {
    readSensorDataFromCard();
  }
  // 3. Delete the saved card if it is scanned
  else if (isSwitchOn[2] && !(isSwitchOn[0] || isSwitchOn[1] || isSwitchOn[3])) {
    deleteCard();

  }

  // 4. Control the fan using the potentiometer
  else if (isSwitchOn[3] && !(isSwitchOn[0] || isSwitchOn[1] || isSwitchOn[2])) {
    controlFanWithPotentiometer();
  }


  if ((isSwitchOn[0] ? 1 : 0) + (isSwitchOn[1] ? 1 : 0) + (isSwitchOn[2] ? 1 : 0) + (isSwitchOn[3] ? 1 : 0) > 1) {
    Serial.println("Multiple switches");
    Serial.print("Switch 1: ");
    Serial.println(isSwitchOn[0]);
    Serial.print("Switch 2: ");
    Serial.println(isSwitchOn[1]);
    Serial.print("Switch 3: ");
    Serial.println(isSwitchOn[2]);
    Serial.print("Switch 4: ");
    Serial.println(isSwitchOn[3]);
    delay(1000);
  }
}

void deleteCard() {
  String cardUID = getCardUID();

  if (cardUID != "") {
    Serial.print("DELETE:");
    Serial.println(cardUID);
  } else {
    Serial.println("Error: No card detected or card read failed");
  }
}

void readSensorDataFromCard() {
  String cardUID = getCardUID();
  if (cardUID != "") {
    Serial.print("GET:");
    Serial.print(cardUID);
    Serial.println("");

    // Wait for the response from the serial
    String response = readSerialResponse();
    Serial.println(response);

    // Parse the response and extract the boolean values
    bool pressure, temperature, humidity;
    parseResponse(response, pressure, temperature, humidity);
    sensors_event_t event;

    // Prepare the output string
    String output = "ADDTO:data,CARD:" + cardUID;

    // Add pressure value or NULL
    if (pressure) {
      Serial.println("reading pressure");
      float pressureValue = get_pressure();
      output += ",PRESSURE:" + String(pressureValue, 2);
    } else {
      output += ",PRESSURE:NULL";
    }

    // Add temperature value or NULL
    dht.temperature().getEvent(&event);
    if (temperature && !isnan(event.temperature)) {
      output += ",TEMPERATURE:" + String(event.temperature);
    } else {
      output += ",TEMPERATURE:NULL";
    }

    // Add humidity value or NULL
    dht.humidity().getEvent(&event);
    if (humidity && !isnan(event.relative_humidity)) {
      output += ",HUMIDITY:" + String(event.relative_humidity);
    } else {
      output += ",HUMIDITY:NULL";
    }

    // Print the output string
    Serial.println(output);
  } else {
    Serial.println("Error: No card detected or card read failed");
  }
}

void controlFanWithPotentiometer() {
  int potValue = analogRead(potentiometer);
  int fanSpeed = map(potValue, 0, 1023, 0, 255);
  analogWrite(fanPWM, fanSpeed);
  delay(100);
}

void controlServoWithFan() {
  int fanValue = analogRead(fanAnalog);
  int servoValue = map(fanValue, 1023, 0, 0, 180);
  MyServo.write(servoValue);
  delay(100);
}

void checkSensorSetup() {
  static bool confirmPressed = false;

  for (int i = 0; i < 4; i++) {
    int reading = digitalRead(buttonPins[i]);  // Read the state of the button

    // Check if button state changed from the last reading
    if (reading != lastButtonStates[i]) {
      lastDebounceTimes[i] = millis();  // reset the debouncing timer
      lock[i] = false;
    }

    if (((millis() - lastDebounceTimes[i]) > debounceDelay) && lock[i] == false) {
      lock[i] = true;

      // If the button state has changed
      if (reading != buttonStates[i]) {
        buttonStates[i] = reading;

        // Toggle the LED if the new button state is HIGH
        if (buttonStates[i] == HIGH) {
          if (i < 3) {  // Buttons 0-2 control LEDs
            ledStates[i] = !ledStates[i];
            digitalWrite(ledPins[i], ledStates[i]);
          } else {  // Button 3 is the confirm button
            confirmPressed = true;
            digitalWrite(ledPins[3], HIGH);  // Blink LED 3
          }
        }
      }
    }

    // Save the reading for the next iteration
    lastButtonStates[i] = reading;
  }

  if (confirmPressed) {

    // Call getCardUID
    String cardUID = getCardUID();
    if (cardUID != "") {
      // Send data to serial
      Serial.print("ADDTO:cards,ID:");
      Serial.print(cardUID);
      Serial.print(",PRESSURE:");
      Serial.print(ledStates[0] ? "true" : "false");
      Serial.print(",TEMPERATURE:");
      Serial.print(ledStates[1] ? "true" : "false");
      Serial.print(",HUMIDITY:");
      Serial.println(ledStates[2] ? "true" : "false");

      // Turn off all LEDs
      for (int i = 0; i < 4; i++) {
        ledStates[i] = LOW;
        digitalWrite(ledPins[i], ledStates[i]);
      }

      confirmPressed = false;  // Reset the confirm flag
    }
  }
}

// Function to read the response from the serial
String readSerialResponse() {
  String response = "";
  bool receivedData = false;
  int attempts = 0;
  const int maxAttempts = 5;

  while (!receivedData && attempts < maxAttempts) {
    while (Serial.available() > 0) {
      response += (char)Serial.read();
      receivedData = true;
    }

    if (!receivedData) {
      Serial.println("No information received. Trying again in 1 second...");
      delay(1000);
      attempts++;
    }
  }

  if (receivedData) {
    response.trim();
    Serial.print("Received: ");
    Serial.println(response);
  } else {
    Serial.println("No data received after multiple attempts.");
  }

  return response;
}

// Function to parse the response and extract the boolean values
void parseResponse(String response, bool &pressure, bool &temperature, bool &humidity) {
  pressure = false;
  temperature = false;
  humidity = false;

  int start = 0;
  int end = response.indexOf(",");
  while (end != -1) {
    String part = response.substring(start, end);

    if (part.startsWith("pressure:")) {
      pressure = (part.endsWith("True"));
    } else if (part.startsWith("temperature:")) {
      temperature = (part.endsWith("True"));
    } else if (part.startsWith("humidity:")) {
      humidity = (part.endsWith("True"));
    }
    start = end + 1;
    end = response.indexOf(",", start);
  }
  String lastPart = response.substring(start);
  if (lastPart.startsWith("pressure:")) {
    pressure = (lastPart.endsWith("True"));
  } else if (lastPart.startsWith("temperature:")) {
    temperature = (lastPart.endsWith("True"));
  } else if (lastPart.startsWith("humidity:")) {
    humidity = (lastPart.endsWith("True"));
  }
}

String getCardUID() {
  String uidString = "";
  bool cardDetected = false;
  bool switchStateChanged = false;
  int prevSwitchStates[numberOfSwitches];  // Store the previous switch states

  // Get the initial switch states
  for (int i = 0; i < numberOfSwitches; i++) {
    prevSwitchStates[i] = isSwitchOn[i];
  }

  while (!cardDetected && !switchStateChanged) {
    // Check if a card is present
    if (mfrc522.PICC_IsNewCardPresent()) {
      if (mfrc522.PICC_ReadCardSerial()) {
        for (byte i = 0; i < mfrc522.uid.size; i++) {
          uidString += String(mfrc522.uid.uidByte[i], DEC);
        }
        cardDetected = true;
        mfrc522.PICC_HaltA();
      }
    }

    // Check if any switch state has changed
    for (int i = 0; i < numberOfSwitches; i++) {
      if (isSwitchOn[i] != prevSwitchStates[i]) {
        switchStateChanged = true;
        break;
      }
    }

    // Store the current switch states for the next iteration
    for (int i = 0; i < numberOfSwitches; i++) {
      prevSwitchStates[i] = isSwitchOn[i];
    }

    // Add a small delay to avoid excessive loop iterations
    delay(10);
  }

  if (cardDetected) {
    return uidString;
  } else {
    return "";
  }
}
