//  Rob Green, University of Idaho, Center for Ecohydraulics, 2023.

//  A program to collect temperature data from multple DS18B20 sensors.
//  Data stored in root diretory in flash on esp32.
//  Data passed via WiFi upon hall effect state change to WiFi Serial phone app.

//  Functional Production Version rev 1, 2023 7/17/2023

// ***  INCLUDED LIBRARIES  ***
#include <Arduino.h>
#include <OneWire.h>
#include <Dallastemperature.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Adafruit_Sleepydog.h>
#include <ESP32Time.h>
#include <esp_sleep.h>

//  GPIO where the DS1820 sensors are connected
#define ONE_WIRE_BUS 25

//  Setup a oneWire instance to communicate with the devices
OneWire oneWire(ONE_WIRE_BUS);

//  Pass the oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);

//  Enable the ESP32 internal RTC
//  Utilize MST time zone
ESP32Time rtc(0);

// Functions used in communication from app
void handleWiFiCommand(char TCP);

// Define the touchpad sensitivity
#define Threshold 20


//  ***  Define global variables  ***
touch_pad_t touchPin;
RTC_DATA_ATTR int firstrun;

int touchValue;
int touchTotal;
int touchpin = 4;
float tempC;
bool connected = false;
File file;
File dir;

char menuDone = 'Q';
const char* ssid = "Idahostreams";  // Name of wifi acess point
const uint16_t portNumber = 80;     // port used for wifi access point
WiFiServer server(portNumber);
WiFiClient client;

//  Define variables to track the Dallas Temperature sensors
int numberOfDevices; // Number of temperature devices found
DeviceAddress tempDeviceAddress; // We'll use this variable to store a found device address

// Placeholder for printing device address function
void printAddress(DeviceAddress deviceAddress);

// Placeholder for a callback function related to touch pad
void touchCallback();


void setup() {
// Set the CPU frequency here
setCpuFrequencyMhz(80);
Serial.begin(9600);
// Set up the deep sleep timmer to sleep for 15 minutes
esp_sleep_enable_timer_wakeup(1000000 * (840 + 58.542131));

// initialize LED digital pin as an output.
pinMode (LED_BUILTIN, OUTPUT);

//  Set up the rtc initial time and date
//  hour, minute, sec, day, month, year
if (firstrun != 1) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(4000);
    digitalWrite (LED_BUILTIN, LOW);
    rtc.setTime(0, 0, 0, 9, 6, 2023);  
}
// Enable the touch pad functin as a wake up source
esp_sleep_enable_touchpad_wakeup();
//  Now call the function for touchpad wake up
touchAttachInterrupt(touchpin, touchCallback, Threshold);

//  Pin for I2C tmperature data collection
//  Enable the internal pullup resistor
pinMode(25, INPUT_PULLUP);

// Read the touchpin sensor value
// Average the 5 readings
// Print the average value to the serial line
touchValue = 0;
touchTotal = 0;
for (int i=0; i<5; i++) {
touchValue = touchRead(touchpin);
touchTotal = touchTotal + touchValue;
delay(2);
}
touchValue = touchTotal / 5;
Serial.print("The touchPin average value is  ");
Serial.println(touchValue);

//  ***  BEGIN PROCESSES NEEDED FOR BOTH OPERATING CONDITIONS  ***
sensors.begin();
// Set LittleFS up for file system      
if (!LittleFS.begin(true)) {
  Serial.println("Failed to mount file system");
  return;
}
// Place headers at the top of the data file
// Open a datafile for writing or appending
if (firstrun != 1) {
  File file = LittleFS.open("/datatempC.txt", "a");
  if (!file) {
    Serial.println("Failed to create or open a file");
    return;
    }
  // Set up a data file header
  file.println("... program release 1.0 ...");
  firstrun = 1;
}

// ***  SETUP ACTIONS FOR DATA COLLECTION  ***
if (touchValue > 50){

  // Discover and print the number of DS18B20 devices on the wire
  // Assign the number of sensors to a variable
  numberOfDevices = sensors.getDeviceCount();

  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(numberOfDevices);
  Serial.println(" devices.");

  // Loop through each device, print out address
  for(int i=0;i<numberOfDevices; i++) {
    // Search the wire for addresses
    if(sensors.getAddress(tempDeviceAddress, i)) {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
   }
 }

//*** DATA COLLECTION ACTIONS ***
// Take temperature data if the magnet is present at setup.
// Temp data will print out and save 
if (touchValue > 50){
    sensors.requestTemperatures(); // Send the command to get temperatures

    // Open a datafile for writing or appending
    File file = LittleFS.open("/datatempC.txt", "a");
    if (!file) {
      Serial.println("Failed to create or open a file");
      return;
    }
    // Print out the data line first column of date and time
    file.print(rtc.getTime("%F, %T, "));

  // Loop through each device, print out and save temperature data
  for(int i=0;i<numberOfDevices; i++) {
    // Search the wire for address
    if(sensors.getAddress(tempDeviceAddress, i)) {
      // Output the device ID
      Serial.print("Temperature for device: ");
      Serial.print(i,DEC);

      // Print the data
      tempC = sensors.getTempC(tempDeviceAddress);
      Serial.print(",  ");
      Serial.print(tempC);
      Serial.print(" Temp C: at ");
      Serial.println(rtc.getTime("%F, %T"));
      } 
      
      // Now store the data
      file.print(tempC);
      if (i < numberOfDevices -1) {
        file.print(",");
      }
  }
  file.println();
  file.close();
  Serial.println(" ...  Data written to file  ...");


//*** PUT ESP32 INTO DEEP SLEEP  ***
// This deep sleep action is only for the data collection portion of sketch
// Note, wake up is a complete reboot, except the "RTC_DATA_ATTR int firstrun;" variable,
// and the "datatempC" file stored in flash.
Serial.println("  ...  Entering deep sleep mode  ...");
Serial.flush();
esp_deep_sleep_start();
}

}
// ***  SETUP FUNCTIONS FOR COMMUNICATION  ***
if (touchValue < 50) {
// Begin process of setup and initialization of AP WiFi
  Serial.println( F("     Setup-Start") );
  Serial.print("Creating AP (Access Point) with name # ");
  Serial.print(ssid);
  Serial.println(" #");
  WiFi.softAP(ssid);
// Set the transmit power, can be used to reduce or increase
// The espressif datasheet references the folowing values
//  +19.5dBm @ 240mA  +16dBm @ 190mA   &   +14dBm @ 180mA
// The WiFi.h library actually has many more power settings.
// Use a provided "enum" value, starting wtih WIFI_POWER_ ...
WiFi.setTxPower(WIFI_POWER_11dBm);
  IPAddress IP = WiFi.softAPIP();
  Serial.print(" -> softAP with IP address: ");
  Serial.println(IP);
  server.begin();
  Serial.print("TCP-Server on port ");
  Serial.print(portNumber);
  Serial.print(" started");

// Wait 30 seconds while user connects to AP WiFi
  Serial.println("... Connect Client Now ...");
  for (int j=0; j<29; j++) {
    // digitalWrite (LED_BUILTIN, HIGH);
    delay(50);
    // digitalWrite (LED_BUILTIN, LOW);
    delay(950);
  }
}
}

void loop() {

//*** LOOP FOR COMMUNICATION CONDITION ***
if (touchValue < 50) {
  char serialChar;
  char TCP_Char;

  
  if (!connected) {
    // listen for incoming clients
    client = server.available();
    if (client) {
      Serial.print("\n New client connected to WiFi AP !\n");
      if (client.connected()) {
        Serial.print("  Client now connected via TCP !\n");
        connected = true;
      } else {
        Serial.println("but client is not connected over TCP !");        
        client.stop();  // close the connection:
      }
    }
  } 
  else {
    if (client.connected()) {
      
      // if character has ben sent from the client and is in the buffer
      while ( client.available()) { 
        TCP_Char = client.read(); // take one character out of the TCP-receive-buffer
        Serial.write(TCP_Char);   // print it to the serial monitor

        // Call the handle function to execute commands associated with the received character
        handleWiFiCommand(TCP_Char);
      }  

      // if characters have been typed into the serial monitor  
      while (Serial.available()) {
        char serialChar = Serial.read(); // take character out of the serial buffer
        Serial.write(serialChar); // print local echo
        client.write(serialChar); // send character over TCP to client
      }
    } else {
      Serial.println("Client has disconnected the TCP-connection");
      client.stop();  // close the connection:
      connected = false;
    }
 }
}
}

//***  FUNCTION DEFINITIONS  ***

// function to print a device address
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
      Serial.print(deviceAddress[i], HEX);
  }
}

// HANDLE INCOMING MACROS FROM THE WIFI SERIAL TERMINAL
// Menu, Dir, Size, Read, Del
void handleWiFiCommand(char TCP) {
  switch (TCP) {

    // DISPLAYS THE MENU UPON RECEIVING 'M'
    case 'M':
    client.println("Welcome to Idahostreams Data Collection");
    client.println("    ....   Button Commands   ....\n");
    client.println("Menu - Displays the menu options");
    client.println("Dir  - Displays files in data directory");
    client.println("Size - Displays file size of tempdataC.txt");
    client.println("Read - Displays datatempC.txt for saving");
    client.println("Del  - Permanently deletes datatempC.txt");
    client.println("Done - WiFi communication is shut down\n");
    LittleFS.end();
    break;

    // DISPLAYS DIRECTORY LISTING OF ROOT DIRECTORY UPON RECEEIVING 'D'
    case 'D':
    client.println("Directory contents are:");
    delay(100);
      if (!LittleFS.begin()) {
        client.println("Failed to mount LittleFS");
        break;
      }

    // Open the directory
    dir = LittleFS.open("/");

    // Iterate through each file in the directory
    while (true) {
     // Get the file name
     file = dir.openNextFile();
     if (!file) {
      // no more file
      return;
     }
     client.println(file.name());
     file.close();
    }
    dir.close();
    LittleFS.end();
    break;

    // DISPLAY FILE SIZE OF datatempC.txt AND ANY OTTHER FILE IN ROOT DIR.
    case 'S':
    client.println("Flie sizes are:");
    delay(100);
      if (!LittleFS.begin()) {
        client.println("Failed to mount LittleFS");
        break;
      }

    // Open the directory
    dir = LittleFS.open("/");

    if (!dir.isDirectory()) {
      Serial.println("Not a directory");
      break;
    }

    // Iterate through each file in the directory
    while (true) {
      file = dir.openNextFile();
      if (!file) {
        // No more files
        return;
      }
    
    client.print(file.name());
    client.print(" ,  ");
    client.println(file.size());
    file.close();
    }
    dir.close();
    LittleFS.end();
    break;

  // DISPLAYS CONTENTS OF datatempC.txt LINE BY LINE UPON RECEIVING 'R'
  case 'R':
  client.println("... The file will be printed to screen now ...\n");
  delay(200);
  if (!LittleFS.begin()) {
    client.println("... LittleFS mount failed ...");
    return;
  }

  //Open the file for reading
  file = LittleFS.open("/datatempC.txt", "r");
  if (!file) {
    client.println("File open failed..");
    return;
  }

  // Read and print the file contents line by line
  while (file.available()) {
    String line = file.readStringUntil('\n');
    client.println(line);
  }
  // Sign off and close file
  file.close();
  break;

  // DELETE datatempC.txt FOR NEXT ROUND OF DATA COLLECTION UPON RECEIVING 'D'
  // Check and see if a file already exists
  // If it does delete it
  case 'X':
  if (LittleFS.exists("/datatempC.txt")) {
    LittleFS.remove("/datatempC.txt");
    client.println("... Existig File deleted ...");
  } else {
    client.println("... No prior file exists ...");
  }
  LittleFS.end();
  break;
  
  // SHUTDOWN AFTER DATA COLLECTION UPON RECEIVING 'Z'
  case 'Z':
  // Send confirming message of ending communication and enter sleep
    client.println(" ... Entering deep sleep now ...");
    client.println("        ... good night ...");
    LittleFS.end();

  //*** TURN OFF ESP32  ***
  //This turn off function is only for communication
  Serial.flush();
  esp_deep_sleep_start();
  break;
  }
}

// The function that handles the touch pad wake up Call Back
void touchCallback() {
// Left blank as the only need here is to reboot from beginning of the code
}
