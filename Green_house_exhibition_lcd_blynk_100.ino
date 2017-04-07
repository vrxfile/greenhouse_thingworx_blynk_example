/*
  Выводы 4, 10, 11, 12, 13 на Arduino UNO нельзя использовать из-за Ethernet платы
  Выводы 0 и 1 нежелательно использовать, так как это порт отладки
  Created by Rostislav Varzar
*/
#define BLYNK_PRINT Serial
#include <DHT.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <Adafruit_BMP085_U.h>
#include "U8glib.h"
#include <BlynkSimpleEthernet.h>
#include <ServoTimer2.h>

// Датчики DS18B20
#define DS18B20_1 8
#define DS18B20_2 9
OneWire oneWire1(DS18B20_1);
OneWire oneWire2(DS18B20_2);
DallasTemperature ds_sensor1(&oneWire1);
DallasTemperature ds_sensor2(&oneWire2);

// Датчик DHT11
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Датчик MQ-2
#define MQ2PIN A3

// Датчик BMP085
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);

// Датчики влажности почвы и освещенности
#define MOISTURE_1 A0
#define MOISTURE_2 A1
#define LIGHT A2

// Выходы реле
#define RELAY1 4
#define RELAY2 6
#define RELAY3 7
#define RELAY4 A5

// Выход ШИМ
#define PWM_LED 3

// LCD дисплей
U8GLIB_NHD_C12864 u8g(22, 24, 26, 28, 30);

// Сервомоторы
#define SERVO1_PWM 5
//Servo servo_1;
ServoTimer2 servo_1;

// Настройки сетевого адаптера
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
// Local IP if DHCP fails
IPAddress ip(192, 168, 1, 250);
IPAddress dnsServerIP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
EthernetClient client;

// API key для Blynk
char auth[] = "8ee073f51255401490209506a54a29fd";
IPAddress blynk_ip(139, 59, 206, 133);
#define W5100_CS  10

// Периоды для асинхронных таймеров
#define DHT11_UPDATE_TIME 5000
#define BMP085_UPDATE_TIME 1000
#define DS1_UPDATE_TIME 1000
#define DS2_UPDATE_TIME 1000
#define ANALOG_UPDATE_TIME 1000
#define IOT_UPDATE_TIME 5000
#define LCD_UPDATE_TIME 1000
#define BLYNK_UPDATE_TIME 1000

// Счетчики для асинхронных таймеров
long timer_dht11 = 0;
long timer_bmp085 = 0;
long timer_analog = 0;
long timer_blynk = 0;
long timer_ds1 = 0;
long timer_ds2 = 0;
long timer_iot = 0;
long timer_lcd = 0;
long timer_auto = 0;

// Состояния управляющих устройств
int pump_state_ptc = 0;
int light_state_ptc = 0;
int window_state_ptc = 0;
int pump_state_blynk = 0;
int light_state_blynk = 0;
int window_state_blynk = 0;

// Параметры IoT сервера
char iot_server[] = "jrskillsiot.cloud.thingworx.com";
IPAddress iot_address(52, 203, 26, 63);
char appKey[] = "cedebb42-898d-4c47-a94f-fabc402e0b8a";
char thingName[] = "MGBot_Exhibition_Greenhouse";
char serviceName[] = "MGBot_Exhibition_SetParams";

// Параметры сенсоров для IoT сервера
#define sensorCount 9                                     // How many values you will be pushing to ThingWorx
char* sensorNames[] = {"dht11_temp", "dht11_hum", "sensor_light", "ds18b20_temp1", "ds18b20_temp2", "soil_moisture1", "soil_moisture2", "air_press", "gas_conc"};
float sensorValues[sensorCount];
// Номера датчиков
#define dht11_temp     0
#define dht11_hum      1
#define sensor_light   2
#define ds18b20_temp1  3
#define ds18b20_temp2  4
#define soil_moisture1 5
#define soil_moisture2 6
#define air_press      7
#define gas_conc       8

// Максимальное время ожидания ответа от сервера
#define IOT_TIMEOUT1 5000
#define IOT_TIMEOUT2 100

// Таймер ожидания прихода символов с сервера
long timer_iot_timeout = 0;

// Размер приемного буффера
#define BUFF_LENGTH 128

// Приемный буфер
char buff[BUFF_LENGTH] = "";

void setup()
{
  // Инициализация последовательного порта
  Serial.begin(115200);
  delay(1024);

  // Инициализация датчиков температуры DS18B20
  ds_sensor1.begin();
  ds_sensor2.begin();

  // Инициализация датчика DHT11
  dht.begin();

  // Инициализация датчика BMP085
  if (!bmp.begin())
  {
    Serial.println("Could not find a valid BMP085 sensor!");
  }

  // Инициализация выходов реле
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);

  // Инициализация выхода на ШИМ
  pinMode(PWM_LED, OUTPUT);

  // Инициализация портов для управления сервомоторами
  servo_1.attach(SERVO1_PWM);

  // Установка сервомотора в начальное положение
  servo_1.write(map(90, 0, 180, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH));
  delay(512);
  servo_1.detach();

  // Инициализация LCD
  u8g.setContrast(0);
  u8g.setColorIndex(1);
  u8g.setRot180();
  u8g.setFont(u8g_font_timB18);
  u8g.firstPage();
  do {
    u8g.drawStr(20, 35, "MGBot");
  } while (u8g.nextPage());

  // Инициализация сетевой платы
  //  if (Ethernet.begin(mac) == 0)
  //  {
  //    Serial.println("Failed to configure Ethernet using DHCP");
  //    Ethernet.begin(mac, ip, dnsServerIP, gateway, subnet);
  //  }
  //  Serial.print("LocalIP: ");
  //  Serial.println(Ethernet.localIP());
  //  Serial.print("SubnetMask: ");
  //  Serial.println(Ethernet.subnetMask());
  //  Serial.print("GatewayIP: ");
  //  Serial.println(Ethernet.gatewayIP());
  //  Serial.print("dnsServerIP: ");
  //  Serial.println(Ethernet.dnsServerIP());
  //  Serial.println("");

  // Инициализация Blynk
  Blynk.begin(auth, blynk_ip, 8442);

  // Однократный опрос датчиков
  readSensorDHT11();
  readSensorBMP085();
  readSensorDS1();
  readSensorDS2();
  readSensorAnalog();

  // Вывод данных на LCD
  u8g.setFont(u8g_font_helvR08);
  printDataLCD();
}

void loop()
{
  // Опрос датчика DHT11
  if (millis() > timer_dht11 + DHT11_UPDATE_TIME)
  {
    // Опрос датчиков
    readSensorDHT11();
    // Сброс таймера
    timer_dht11 = millis();
  }

  // Опрос датчика BMP085
  if (millis() > timer_bmp085 + BMP085_UPDATE_TIME)
  {
    // Опрос датчиков
    readSensorBMP085();
    // Сброс таймера
    timer_bmp085 = millis();
  }

  // Опрос датчика DS18B20 №1
  if (millis() > timer_ds1 + DS1_UPDATE_TIME)
  {
    // Опрос датчиков
    readSensorDS1();
    // Сброс таймера
    timer_ds1 = millis();
  }

  // Опрос датчика DS18B20 №2
  if (millis() > timer_ds2 + DS2_UPDATE_TIME)
  {
    // Опрос датчиков
    readSensorDS2();
    // Сброс таймера
    timer_ds2 = millis();
  }

  // Опрос аналоговых датчиков
  if (millis() > timer_analog + ANALOG_UPDATE_TIME)
  {
    // Опрос датчиков
    readSensorAnalog();
    // Сброс таймера
    timer_analog = millis();
  }

  // Вывод данных на сервер IoT
  if (millis() > timer_iot + IOT_UPDATE_TIME)
  {
    // Вывод данных на сервер IoT
    sendDataIot();
    // Сброс таймера
    timer_iot = millis();
  }

  // Вывод данных на LCD
  if (millis() > timer_lcd + LCD_UPDATE_TIME)
  {
    // Вывод данных на сервер IoT
    printDataLCD();
    // Сброс таймера
    timer_lcd = millis();
  }

  // Отправка данных Blynk
  if (millis() > timer_blynk + BLYNK_UPDATE_TIME)
  {
    sendBlynk();
    timer_blynk = millis();
  }

  // Опрос сервера Blynk
  Blynk.run();
  delay(10);
}

// Чтение датчика DHT11
void readSensorDHT11()
{
  // DHT11
  sensorValues[dht11_hum] = dht.readHumidity();
  sensorValues[dht11_temp] = dht.readTemperature();
}

// Чтение датчика BMP085
void readSensorBMP085()
{
  // BMP085
  float t = 0;
  float p = 0;
  sensors_event_t p_event;
  bmp.getEvent(&p_event);
  if (p_event.pressure)
  {
    p = p_event.pressure * 7.5006 / 10;
    bmp.getTemperature(&t);
  }
  sensorValues[air_press] = p;
}

// Чтение датчика DS18B20 №1
void readSensorDS1()
{
  // DS18B20
  ds_sensor1.requestTemperatures();
  sensorValues[ds18b20_temp1] = ds_sensor1.getTempCByIndex(0);
}

// Чтение датчика DS18B20 №2
void readSensorDS2()
{
  // DS18B20
  ds_sensor2.requestTemperatures();
  sensorValues[ds18b20_temp2] = ds_sensor2.getTempCByIndex(0);
}

// Чтение аналоговых датчиков
void readSensorAnalog()
{
  // Аналоговые датчики
  sensorValues[sensor_light] = (1023.0 - analogRead(LIGHT)) / 1023.0 * 100.0;
  sensorValues[soil_moisture1] = analogRead(MOISTURE_1) / 1023.0 * 100.0;
  sensorValues[soil_moisture2] = analogRead(MOISTURE_2) / 1023.0 * 100.0;
  sensorValues[gas_conc] = analogRead(MQ2PIN) / 1023.0 * 100.0;
}

// Отправка данных в приложение Blynk
void sendBlynk()
{
  Serial.println("Sending data to Blynk...");
  Blynk.virtualWrite(V0, sensorValues[dht11_temp]); delay(50);
  Blynk.virtualWrite(V1, sensorValues[dht11_hum]); delay(50);
  Blynk.virtualWrite(V2, sensorValues[ds18b20_temp1]); delay(50);
  Blynk.virtualWrite(V3, sensorValues[soil_moisture1]); delay(50);
  Blynk.virtualWrite(V4, sensorValues[ds18b20_temp2]); delay(50);
  Blynk.virtualWrite(V5, sensorValues[soil_moisture2]); delay(50);
  Blynk.virtualWrite(V6, sensorValues[sensor_light]); delay(50);
  Serial.println("Data successfully sent!");
}

// Управление освещением с Blynk
BLYNK_WRITE(V7)
{
  light_state_blynk = param.asInt();
  Serial.print("Light power: ");
  Serial.println(light_state_blynk);
  analogWrite(PWM_LED, max(light_state_ptc, light_state_blynk) * 2.55);
}

// Управление вентиляцией с Blynk
BLYNK_WRITE(V8)
{
  window_state_blynk = constrain((param.asInt() + 90), 90, 135);
  Serial.print("Window motor angle: ");
  Serial.println(window_state_blynk);
  servo_1.attach(SERVO1_PWM);
  servo_1.write(map(max(window_state_ptc, window_state_blynk), 0, 180, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH));
  delay(512);
  servo_1.detach();
}

// Управление помпой с Blynk
BLYNK_WRITE(V9)
{
  pump_state_blynk = param.asInt();
  Serial.print("Pump power: ");
  Serial.println(pump_state_blynk);
  digitalWrite(RELAY1, pump_state_ptc | pump_state_blynk);
}

// Подключение к серверу IoT ThingWorx
void sendDataIot()
{
  // Подключение к серверу
  Serial.println("Connecting to IoT server...");
  if (client.connect(iot_address, 80))
  {
    // Проверка установления соединения
    if (client.connected())
    {
      // Отправка заголовка сетевого пакета
      Serial.println("Sending data to IoT server...\n");
      Serial.print("POST /Thingworx/Things/");
      client.print("POST /Thingworx/Things/");
      Serial.print(thingName);
      client.print(thingName);
      Serial.print("/Services/");
      client.print("/Services/");
      Serial.print(serviceName);
      client.print(serviceName);
      Serial.print("?appKey=");
      client.print("?appKey=");
      Serial.print(appKey);
      client.print(appKey);
      Serial.print("&method=post&x-thingworx-session=true");
      client.print("&method=post&x-thingworx-session=true");
      // Отправка данных с датчиков
      for (int idx = 0; idx < sensorCount; idx ++)
      {
        Serial.print("&");
        client.print("&");
        Serial.print(sensorNames[idx]);
        client.print(sensorNames[idx]);
        Serial.print("=");
        client.print("=");
        Serial.print(sensorValues[idx]);
        client.print(sensorValues[idx]);
      }
      // Закрываем пакет
      Serial.println(" HTTP/1.1");
      client.println(" HTTP/1.1");
      Serial.println("Accept: application/json");
      client.println("Accept: application/json");
      Serial.print("Host: ");
      client.print("Host: ");
      Serial.println(iot_server);
      client.println(iot_server);
      Serial.println("Content-Type: application/json");
      client.println("Content-Type: application/json");
      Serial.println();
      client.println();

      // Ждем ответа от сервера
      timer_iot_timeout = millis();
      while ((client.available() == 0) && (millis() < timer_iot_timeout + IOT_TIMEOUT1));

      // Выводим ответ о сервера, и, если медленное соединение, ждем выход по таймауту
      int iii = 0;
      bool currentLineIsBlank = true;
      bool flagJSON = false;
      timer_iot_timeout = millis();
      while ((millis() < timer_iot_timeout + IOT_TIMEOUT2) && (client.connected()))
      {
        while (client.available() > 0)
        {
          char symb = client.read();
          Serial.print(symb);
          if (symb == '{')
          {
            flagJSON = true;
          }
          else if (symb == '}')
          {
            flagJSON = false;
          }
          if (flagJSON == true)
          {
            buff[iii] = symb;
            iii ++;
          }
          timer_iot_timeout = millis();
        }
      }
      buff[iii] = '}';
      buff[iii + 1] = '\0';
      Serial.println(buff);
      // Закрываем соединение
      client.stop();

      // Расшифровываем параметры
      StaticJsonBuffer<BUFF_LENGTH> jsonBuffer;
      JsonObject& json_array = jsonBuffer.parseObject(buff);
      if (json_array.success())
      {
        pump_state_ptc = json_array["pump_state"];
        window_state_ptc = json_array["window_state"];
        light_state_ptc = json_array["light_state"];
        if (window_state_ptc)
        {
          window_state_ptc = 120;
        }
        else
        {
          window_state_ptc = 90;
        }
      }
      Serial.println("Pump state:   " + String(pump_state_ptc));
      Serial.println("Light state:  " + String(light_state_ptc));
      Serial.println("Window state: " + String(window_state_ptc));
      Serial.println();

      // Делаем управление устройствами
      controlDevices();

      Serial.println("Packet successfully sent!");
      Serial.println();
    }
  }
}

// Управление исполнительными устройствами
void controlDevices()
{
  // Форточка
  servo_1.attach(SERVO1_PWM);
  servo_1.write(map(max(window_state_ptc, window_state_blynk), 0, 180, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH));
  delay(512);
  servo_1.detach();
  // Освещение
  analogWrite(PWM_LED, max(light_state_ptc, light_state_blynk) * 2.55);
  // Помпа
  digitalWrite(RELAY1, pump_state_ptc | pump_state_blynk);
}

// Вывод данных на LCD
void printDataLCD()
{
  char strtemp1[32] = "";
  char strtemp2[32] = "";
  float tmpparam = 0;
  u8g.firstPage();
  do
  {
    dtostrf(sensorValues[dht11_temp], 4, 1, strtemp1);
    sprintf(strtemp2, "AIR TEMP  = %s *C", strtemp1);
    u8g.drawStr(0, 8, strtemp2);
    dtostrf(sensorValues[dht11_hum], 4, 1, strtemp1);
    sprintf(strtemp2, "AIR HUM   = %s %%", strtemp1);
    u8g.drawStr(0, 18, strtemp2);
    dtostrf(sensorValues[air_press], 5, 1, strtemp1);
    sprintf(strtemp2, "AIR PRESS = %s mm", strtemp1);
    u8g.drawStr(0, 28, strtemp2);
    dtostrf(sensorValues[ds18b20_temp1], 4, 1, strtemp1);
    sprintf(strtemp2, "SOIL TEMP = %s *C", strtemp1);
    u8g.drawStr(0, 38, strtemp2);
    dtostrf(sensorValues[soil_moisture1], 4, 1, strtemp1);
    sprintf(strtemp2, "SOIL HUM = %s %%", strtemp1);
    u8g.drawStr(0, 48, strtemp2);
    dtostrf(sensorValues[gas_conc], 4, 1, strtemp1);
    sprintf(strtemp2, "GAS CONC = %s %%", strtemp1);
    u8g.drawStr(0, 58, strtemp2);
  } while (u8g.nextPage());
}

