#include <Adafruit_SHTC3.h>
#include <driver/i2s.h>

// Pin Definitions
#define PIR_PIN D0
#define MIC_LRCLK D1
#define MIC_DOUT  D2
#define MIC_BCLK  D3
#define SHTC3_SDA D4
#define SHTC3_SCL D5

// I2S Microphone Config
const i2s_port_t I2S_PORT = I2S_NUM_0;

// Create the sensor object
Adafruit_SHTC3 shtc3 = Adafruit_SHTC3();

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); // Wait for Serial Monitor

  // 1. PIR Setup
  pinMode(PIR_PIN, INPUT);

  // 2. Adafruit SHTC3 Setup
  // We use Wire.begin with your specific pins before initializing the sensor
  Wire.begin(SHTC3_SDA, SHTC3_SCL);
  if (!shtc3.begin()) {
    Serial.println("Couldn't find SHTC3! Check wiring.");
    while (1) delay(10);
  }
  Serial.println("SHTC3 Found!");

  // 3. I2S Microphone Setup
  setupI2S();

  Serial.println("System Initialized");
}

void loop() {
  // --- TEMPERATURE & HUMIDITY ---
  sensors_event_t humidity, temp;
  shtc3.getEvent(&humidity, &temp); // Populate the events with data

  Serial.print("Temp: "); Serial.print(temp.temperature); Serial.print("°C | ");
  Serial.print("Hum: "); Serial.print(humidity.relative_humidity); Serial.println("%");

  // --- PIR MOTION SENSOR ---
  if (digitalRead(PIR_PIN)) {
    Serial.println(">> MOTION DETECTED!");
  }

  // --- MICROPHONE READ ---
  int32_t sample = 0;
  size_t bytes_read;
  i2s_read(I2S_PORT, &sample, sizeof(sample), &bytes_read, portMAX_DELAY);
  
  if (bytes_read > 0) {
    sample >>= 14; // Normalize for SPH0645
    if (abs(sample) > 500) {
       Serial.print("Sound Level: "); Serial.println(abs(sample));
    }
  }

  delay(500); 
}

void setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = MIC_BCLK,
    .ws_io_num = MIC_LRCLK,
    .data_out_num = -1, 
    .data_in_num = MIC_DOUT
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}