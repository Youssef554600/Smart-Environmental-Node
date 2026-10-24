#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT11.h>

// --- Display settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Sensor settings (DHT11 library) ---
#define DHTPIN 4
DHT11 dht11(DHTPIN);

// --- Alarm LED ---
#define LED_PIN 2

// --- WiFi and MQTT settings ---
const char* ssid = "Galaxy A71 B562";
const char* password = "12345689";
const char* mqtt_server = "172.105.149.22";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";

WiFiClient espClient;
PubSubClient client(espClient);

// --- Shared variables ---
volatile float temperature = 0.0;
volatile float humidity = 0.0;
RTC_DATA_ATTR int bootCount = 0;

// --- FreeRTOS tasks ---
TaskHandle_t TaskSensorHandle = NULL;
TaskHandle_t TaskDisplayHandle = NULL;
TaskHandle_t TaskMqttHandle = NULL;

void TaskSensor(void *pvParameters);
void TaskDisplay(void *pvParameters);
void TaskMqtt(void *pvParameters);

void setup() {
  Serial.begin(115200);
  delay(1000);

  bootCount++;
  Serial.println("\n[INFO] System Boot Count: " + String(bootCount));

  pinMode(LED_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] SSD1306 allocation failed");
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Booting System...");
  display.display();

  WiFi.begin(ssid, password, 6);
  Serial.print("[INFO] Connecting to WiFi");
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[INFO] WiFi Connected!");
    client.setServer(mqtt_server, mqtt_port);
  } else {
    Serial.println("\n[WARN] WiFi Connection Failed! Continuing offline mode...");
  }

  xTaskCreatePinnedToCore(TaskSensor,  "SensorTask",  4096, NULL, 2, &TaskSensorHandle,  1);
  xTaskCreatePinnedToCore(TaskDisplay, "DisplayTask", 4096, NULL, 1, &TaskDisplayHandle, 1);
  xTaskCreatePinnedToCore(TaskMqtt,    "MqttTask",    4096, NULL, 2, &TaskMqttHandle,    0);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(10000));  // stay awake 10 s

  Serial.println("[INFO] Going to Deep Sleep now...");
  esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);  // sleep 30 s
  esp_deep_sleep_start();
}

void TaskSensor(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(1500));  // let the sensor settle after boot
  for (;;) {
    int t = 0, h = 0;
    int result = dht11.readTemperatureHumidity(t, h);  // 0 = success

    if (result != 0) {
      Serial.print("[ERROR] DHT11 read failed: ");
      Serial.println(DHT11::getErrorString(result));
    } else {
      temperature = t;
      humidity = h;
      Serial.printf("[INFO] Temp: %d C | Humid: %d %%\n", t, h);

      if (t > 30) {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("[WARN] High Temperature Alert! LED ON");
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2000));  // DHT11 needs >= 1 s between reads
  }
}

void TaskDisplay(void *pvParameters) {
  for (;;) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("=== SMART NODE A ===");
    display.print("Boot Count: "); display.println(bootCount);
    display.print("Temp: "); display.print((int)temperature); display.println(" C");
    display.print("Humidity: "); display.print((int)humidity); display.println(" %");
    display.print("WiFi: ");
    display.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    display.display();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void TaskMqtt(void *pvParameters) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!client.connected()) {
        Serial.print("[INFO] Connecting to MQTT broker...");
        if (client.connect("ESP32ClientNodeA", mqtt_user, mqtt_password)) {
          Serial.println(" connected!");
        } else {
          Serial.print(" failed, rc=");
          Serial.println(client.state());
          vTaskDelay(pdMS_TO_TICKS(5000));
          continue;
        }
      }
      client.loop();

      String payload = "{\"temp\":" + String((int)temperature) +
                       ",\"humid\":" + String((int)humidity) +
                       ",\"boots\":" + String(bootCount) + "}";
      client.publish("home/environmental/nodeA/data", payload.c_str());
      Serial.println("[INFO] Data published to MQTT.");
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
