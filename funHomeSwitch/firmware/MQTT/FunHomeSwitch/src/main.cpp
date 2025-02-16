#include <Arduino.h>
#include <WiFi.h>  // 引入 Wi-Fi 库
#include "esp_bt.h"
#include <PubSubClient.h>  // MQTT 客户端库
#include <esp_sleep.h>
#include <ESP32Servo.h>

#include <driver/adc.h>
#include <esp_adc_cal.h>

// ADC 配置
#define ADC_CHANNEL    ADC1_CHANNEL_1  // 对应 GPIO1
#define DEFAULT_VREF   1100            // 默认参考电压（单位 mV）
#define NO_OF_SAMPLES  64              // 多次采样以提高精度
// 定义电压相关参数
#define VOLTAGE_DIVIDER_RATIO 2.0  // 分压比，例如 10k 和 10k 电阻分压
#define MIN_BATTERY_VOLTAGE 3.0   // 最低锂电池电压
#define MAX_BATTERY_VOLTAGE 4.2   // 最高锂电池电压
esp_adc_cal_characteristics_t *adc_chars;

// 舵机
Servo myservo;  // create servo object to control a servo
const int servoPower = 6; 
const int servoPWM = 7;

// LED
#define LED_PIN 10 // 定义GPIO10为LED引脚

// Wi-Fi 配置
const char* ssid = "TEST";
const char* password = "12345678";

// MQTT 配置
const char* mqtt_server = "192.168.31.254";
const int mqtt_port = 1883; // Mosquitto 默认端口是 1883
const char* mqtt_user = ""; // 如果设置了用户名则填写，否则留空
const char* mqtt_password = ""; // 如果设置了密码则填写，否则留空
const char* mqtt_topic = "funhomeswitch/topic"; // 发布和订阅的主题：开关
const char* mqtt_battery_topic = "funhomeswitchbattery/topic"; // 发布和订阅的主题：电量

WiFiClient espClient;
PubSubClient client(espClient);

// 标志变量，记录是否收到消息
volatile bool messageReceived = false;

//mqtt接收消息缓存
char mqtt_rcv[100] = {0};

//MQTT 客户端ID
String clientID;

// 计算电量百分比
float getBatteryPowerPercent() {
  uint32_t adc_reading = 0;
  // 多次采样平均
  for (int i = 0; i < NO_OF_SAMPLES; i++) {
    adc_reading += adc1_get_raw(ADC_CHANNEL);
  }
  adc_reading /= NO_OF_SAMPLES;
  // 转换为电压（单位：V）
  float voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars) * VOLTAGE_DIVIDER_RATIO * 0.001;
  // 打印电压值
  //Serial.printf("ADC Reading: %d\tVoltage: %d mV\n", adc_reading, voltage);
   float batteryPercentage = map(voltage * 100, MIN_BATTERY_VOLTAGE * 100, MAX_BATTERY_VOLTAGE * 100, 0, 100);
  batteryPercentage = constrain(batteryPercentage, 0, 100);  // 限制电量范围在 0% 到 100%
  // 打印电压和电量百分比
  // Serial.print("Battery Voltage: ");
  // Serial.print(voltage, 2);  // 保留两位小数
  // Serial.print(" V, Battery Percentage: ");
  // Serial.print(batteryPercentage, 1);  // 保留一位小数
  // Serial.println(" %");
  return batteryPercentage;
}

//开关设置
void setSwitch(bool onoff) {
    if (onoff) {

        digitalWrite(servoPower, HIGH);
        myservo.write(85);
        delay(300);
        //myservo.write(90);
        //delay(300);
        digitalWrite(servoPower, LOW); 
    } else {
        digitalWrite(servoPower, HIGH); 
        myservo.write(120);      
        delay(300);
        //myservo.write(90);
        //delay(300); 
        digitalWrite(servoPower, LOW); 
    }
}

// 回调函数：处理接收到的 MQTT 消息
void callback(char* topic, byte* payload, unsigned int length) {
//   Serial.print("收到消息 [");
//   Serial.print(topic);
//   Serial.print("]: ");
//   for (int i = 0; i < length; i++) {
//     Serial.print((char)payload[i]);
//   }
//   Serial.println();
  memset(mqtt_rcv, 0, sizeof(mqtt_rcv));
  for (int i = 0; i < length; i++) {
    mqtt_rcv[i] = payload[i];
  }
  messageReceived = true; // 标记收到消息

//   //发送消息给服务器
//   static int count=0;
//   count++;
//   char message[20] = {0};
//   sprintf(message, "ESP32_back_%d", count);
//   client.publish(mqtt_topic, message);
}

// 连接到 WiFi
void connectWiFi() {
  WiFi.begin(ssid, password);
  //Serial.print("正在连接 WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    //Serial.print(".");
  }
  //Serial.println("\nWiFi 已连接");
  // WiFi连接成功后打印IP地址
  // Serial.println("WiFi connected!");
  // Serial.print("IP Address: ");
  // Serial.println(WiFi.localIP()); // 打印当前IP地址
}

// 连接到 MQTT 服务器
void connectMQTT() {
  while (!client.connected()) {
    //Serial.print("正在连接到 MQTT 服务器...");
    if (client.connect(clientID.c_str())) {
      //Serial.println("连接成功！");
      client.subscribe(mqtt_topic); // 订阅主题
      //Serial.println("已订阅主题: " + String(mqtt_topic));
    } else {
      //Serial.print("连接失败，状态码: ");
      //Serial.println(client.state());
      delay(2000); // 2 秒后重试
    }
  }
}

// 进入 Light Sleep 模式
void enterLightSleep(unsigned long sleepDurationMs) {
  //Serial.println("准备进入 Light Sleep 模式...");
  esp_sleep_enable_timer_wakeup(sleepDurationMs * 1000); // 设置定时唤醒时间（微秒）
  //Serial.println("进入 Light Sleep...");
  esp_light_sleep_start(); // 启动 Light Sleep
  //Serial.println("从 Light Sleep 唤醒...");
}

void setup() {
  Serial.begin(115200);

  //电量测量
  //analogReadResolution(12);  // 设置 ADC 分辨率为 12 位
// 配置 ADC 通道
  adc1_config_width(ADC_WIDTH_BIT_12);               // 配置为 12 位宽度
  adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11); // 设置衰减为 11dB（输入范围 0-3.3V）
  // 初始化校准
  adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);


  //初始化LED
  pinMode(LED_PIN, OUTPUT); // 设置GPIO10为输出模式
  digitalWrite(LED_PIN, LOW); // 关闭LED灯

  //初始化舵机
  myservo.attach(servoPWM);
  pinMode(servoPower, OUTPUT); 
  digitalWrite(servoPower, HIGH); 
  myservo.write(90);    
  delay(500);
  digitalWrite(servoPower, LOW); 

  // 禁用蓝牙
  esp_bt_controller_disable();
  //Serial.println("Bluetooth disabled.");

  // 初始化 WiFi 和 MQTT
  WiFi.setSleep(true); // 开启 WiFi 省电模式
  connectWiFi();
  String macAddress = WiFi.macAddress();
  clientID = "ESP32Client-" + macAddress;
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  //client.setKeepAlive(60); // 默认是 15 秒，这里改为 60 秒

  // 连接到 MQTT
  connectMQTT();

  //Serial.println("INIT...");
}

void loop() {
  // 关闭LED灯
  digitalWrite(LED_PIN, LOW); 

  // 检查 MQTT 连接
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop(); // 保持 MQTT 连接并处理消息

  //获取电量并发送消息给服务器
  static uint32_t count = 0;
  count++;
  if ((count % 10) == 0) {
    char message_battery[10] = {0};
    sprintf(message_battery, "%f", getBatteryPowerPercent());
    client.publish(mqtt_battery_topic, message_battery);
  }

  // 等待消息 1s
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    //client.loop();
    if (messageReceived) {
      //Serial.println("检测到消息，准备处理...");
       digitalWrite(LED_PIN, HIGH); // 点亮LED灯
      if (0 == strcmp(mqtt_rcv, "funhomeswitch_on")) {
        //Serial.print("cmd rcv funhomeswitch_on!\n");    
        setSwitch(true);
        client.publish(mqtt_topic, "", true);  // 发送空消息，清除该主题的保留消息
      } else if (0 == strcmp(mqtt_rcv, "funhomeswitch_off")) {
        setSwitch(false);
        client.publish(mqtt_topic, "", true);  // 发送空消息，清除该主题的保留消息    
        //Serial.print("cmd rcv funhomeswitch_off!\n");  
      } else {
        //Serial.print("cmd error!\n");    
      }
 
      messageReceived = false; // 清除标志
    }
    delay(200); // 防止阻塞
  }

  //未收到消息，进入 Light Sleep 模式
  //Serial.println("未检测到消息，进入 Light Sleep...");
  enterLightSleep(3000); // Light Sleep 3 秒
}


