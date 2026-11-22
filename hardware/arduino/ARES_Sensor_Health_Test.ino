// ARES_Sensor_Health_Test
// Anthony Ricevuto
// 11/21/2026

// Logs BMP581 readings and sensor health to microSD.
// Injects a five-second reading interruption to test stale-data detection
// and requires three consecutive successful readings to confirm recovery.

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

const char LOG_NAME[] = "health.csv";
const unsigned long INTERVAL_MS = 1000;
const unsigned int TOTAL_ATTEMPTS = 60;
const bool INJECT_FAULT = true;
const unsigned long FAULT_START_MS = 20000;
const unsigned long FAULT_END_MS = 25000;
const unsigned long STALE_LIMIT_MS = 2500;
const unsigned int RECOVERY_READINGS = 3;

unsigned long lastAttemptMs = 0;
unsigned int attempts = 0;
unsigned int savedRows = 0;
unsigned long testStartMs = 0;
unsigned long lastGoodMs = 0;
bool running = false;

float lastTemperatureC = 0.0f;
float lastPressurePa = 0.0f;

bool haveSample = false;
bool degraded = false;
unsigned int recoveryCount = 0;

// Write the same CSV row to either Serial or the SD file
void writeRow(Print &output, unsigned long elapsedMs,
               bool fresh, bool injected, const char *status){
  output.print(elapsedMs);
  output.print(',');
  
  // Leave measurement fields empty until the first successful read.
  if (haveSample){
    output.print(lastTemperatureC, 2);
  }
  output.print(',');

  if(haveSample){
    output.print(lastPressurePa, 2);
  }
  output.print(',');

  // 1 means a new successful reading was acquired this cycle.
  output.print(fresh ? 1 : 0);
  output.print(',');

  if (haveSample){
    output.print(elapsedMs - lastGoodMs);
  }
  output.print(',');

  output.print(injected ? 1 : 0);
  output.print(',');
  output.print(status);
  output.print(',');

  if(degraded){
    output.println("DEGRADED");
  } else if (!haveSample){
    output.println("INITIALIZING");
  }else {
    output.println("NOMINAL");
  }
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
    logFile.println("elapsed_ms,temperature_c,pressure_pa,fresh,sample_age_ms,"
  "injected,read_status,mode");
  }

  bool headerSaved = logFile.sync();
  logFile.close();
  if (!headerSaved) {
    Serial.println("ERROR: Could not save file header.");
    return;
  }
  
  Serial.println("elapsed_ms,temperature_c,pressure_pa,fresh,sample_age_ms,"
  "injected,read_status,mode");
  testStartMs = millis();
  lastAttemptMs = testStartMs;
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

  unsigned long elapsedMs = now - testStartMs;

  bool injected = INJECT_FAULT && 
  elapsedMs >= FAULT_START_MS && 
  elapsedMs < FAULT_END_MS;

  bool fresh = false;
  const char *status = "NOT_READY";

  // Simulate missing readings without disconnecting hardware.
  if(injected){
    status = "INJECTED";
  }else if (!bmp.dataReady()){
    status = "NOT_READY";
  } else if (!bmp.performReading()){
    status = "READ_FAILED";
  } else {
    // Timestamp only a successful acquisition.
    elapsedMs = millis() - testStartMs;

    lastTemperatureC = bmp.temperature;
    lastPressurePa = bmp.pressure * 100.0f;
    lastGoodMs = elapsedMs;

    haveSample = true;
    fresh = true;
    status = "OK";
  }

  // Before the first smaple, measure the startup waiting time.
  unsigned long sampleAgeMs = haveSample ? elapsedMs - lastGoodMs : elapsedMs;

  // Detect stale data independently of the injection flag.
  if (sampleAgeMs >= STALE_LIMIT_MS){
    if (!degraded){
      Serial.print("# EVENT,SENSOR_STALE,");
      Serial.println(elapsedMs);
    }

    degraded = true;
    recoveryCount = 0;
  }

  // Require three consecutive successful readings to recover.
  if(degraded){
    if(fresh){
      recoveryCount++;

      if (recoveryCount >= RECOVERY_READINGS){
        degraded = false;
        recoveryCount = 0;

        Serial.print("# EVENT,Sensor_RECOVERED,");
        Serial.println(elapsedMs);
      }
    }else{
      recoveryCount = 0;
    }
  }

  // Write a row even when no new sensor reading was obtained.
  logFile = SD.open(LOG_NAME, FILE_WRITE);

  if (!logFile){
    Serial.println("SD ERROR: Could not open log. Test stopped.");
    running = false;
    return;
  }

  logFile.clearWriteError();

  writeRow(logFile, elapsedMs, fresh, injected, status);

  bool writeOK = (logFile.getWriteError() == 0);
  bool syncOK = logFile.sync();
  logFile.close();

  if(!writeOK || !syncOK){
    Serial.println("SD ERROR: Could not save row. test stopped.");
    running = false;
    return;
  }

  writeRow(Serial, elapsedMs, fresh, injected, status);
  savedRows++;

  if (attempts >= TOTAL_ATTEMPTS) {
    running = false;

    Serial.print("Finished. Rows saved: ");
    Serial.print(savedRows);
    Serial.println("file closed. Disconnect USB before removing card.");
  }
}
