// ARES_Sensor_Logger
// Anthony Ricevuto
// 11/17/2026

// Uses BMP5xx sensor API and the Adalogger’s onboard SD configuration: SPI1, 
// chip-select 23. The sensor library reports pressure in hPa, so the code multiplies 
// by 100 to save Pa.

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP5xx.h>
#include <SdFat.h>

// Global Objects
Adafruit_BMP5xx bmp;
SdFat SD;
FsFile logFile;

// Configuring the SD connection
SdSpiConfig sdConfig(23, DEDICATED_SPI, SD_SCK_MHZ(16), &SPI1);

const char LOG_NAME[] = "ares_log.csv";
const unsigned long INTERVAL_MS = 1000;
const unsigned int TOTAL_ATTEMPTS = 60;

unsigned long lastAttemptMs = 0;
unsigned int attempts = 0;
unsigned int savedRows = 0;
bool running = false;

// Write the same CSV row to either Serial or the SD file
void writeRow(Print &output, unsigned long timestamp,
               float temperatureC, float pressurePa){
  output.print(timestamp);
  output.print(',');
  output.print(temperatureC, 2);
  output.print(',');
  output.println(pressurePa, 2);
}

void setup(){
  // Initializes the serial interface using the conventional baud setting of 115200.
  Serial.begin(115200);

  while (!Serial) {
    delay(10);
  }
  delay(100);

  Serial.println("Starting ARES sensor logger...");

  if (!bmp.begin(BMP5XX_ALTERNATIVE_ADDRESS, &Wire)){
    Serial.println("ERROR: BMP581 initialization failed.");
    return;
  }

  bmp.setTemperatureOversampling(BMP5XX_OVERSAMPLING_2X);
  bmp.setPressureOversampling(BMP5XX_OVERSAMPLING_16X);
  bmp.setIIRFilterCoeff(BMP5XX_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP5XX_ODR_10_HZ);
  bmp.enablePressure(true);
  bmp.setPowerMode(BMP5XX_POWERMODE_NORMAL);
  bmp.configureInterrupt(
    BMP5XX_INTERRUPT_LATCHED,
    BMP5XX_INTERRUPT_ACTIVE_HIGH,
    BMP5XX_INTERRUPT_PUSH_PULL,
    BMP5XX_INTERRUPT_DATA_READY,
    true
  );
  if (!SD.begin(sdConfig)){
    Serial.println("ERROR: SD initialization failed.");
    return;
  }

  // Append to the exisitng file; do not erase earlier measurements.
  logFile = SD.open(LOG_NAME, FILE_WRITE);
  if (!logFile){
    Serial.println("ERROR: Could not open ares_log.csv");
    return;
  }

  // Only add the column names to an empty file.
  if (logFile.size() == 0){
    logFile.println("time_ms,temperature_c,pressure_pa");
  }

  bool headerSaved = logFile.sync();
  logFile.close();
  if (!headerSaved) {
    Serial.println("ERROR: Could not save file header.");
    return;
  }
  
  Serial.println("time_ms,temperature_c,pressure_pa");
  lastAttemptMs = millis();
  running = true;
}

void loop(){
  if (!running) {
    return;
  }
  unsigned long now = millis();
  if (now - lastAttemptMs < INTERVAL_MS){
    return;
  }
  lastAttemptMs = now;
  attempts++;
  if (!bmp.dataReady()){
    Serial.println("SENSOR: Data-ready flag is not set.");
  } else if (!bmp.performReading()){
    Serial.println("SENSOR: Data ready, but reading failed.");
  }else {
    unsigned long timestamp = millis();
    float temperatureC = bmp.temperature;
    float pressurePa = bmp.pressure * 100.0f;
    logFile = SD.open(LOG_NAME, FILE_WRITE);
    if (!logFile){
      Serial.println("SD ERROR: Could not open log. Test stopped");
      running = false;
      return;
    }
    logFile.clearWriteError();
    writeRow(logFile, timestamp, temperatureC, pressurePa);
    bool writeOK = (logFile.getWriteError() == 0);
    bool syncOK = logFile.sync();
    logFile.close();
    if(!writeOK || !syncOK){
      Serial.println("SD ERROR: Could not save row. Test stopped.");
      running = false;
      return;
    }

    writeRow(Serial, timestamp, temperatureC, pressurePa);
    savedRows++;
  }
  
  if (attempts >= TOTAL_ATTEMPTS){
    running = false;
    Serial.print("Finished. Rows saved: ");
    Serial.println(savedRows);
    Serial.println("File closed. Disconnect USB before removing card.");
  }
}
