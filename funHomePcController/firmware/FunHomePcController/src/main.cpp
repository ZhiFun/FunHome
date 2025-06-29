#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <libssh/libssh.h>

#define LED_PIN 10 // 定义GPIO10为LED引脚

// WiFi配置
const char* ssid = "test";
const char* password = "12345678";

// 目标电脑的MAC地址
const char* macAddress = "xx:xx:xx:xx:xx:xx";

// WOL魔法包端口
const int wolPort = 9;

// SSH 服务器配置
const char* ssh_host = "192.168.xx.xxx";
const int ssh_port = 22;
const char* ssh_user = "remote-shutdown";
const char* ssh_password = "zhifun";
const char* command = "ls -l";  // 要执行的命令

// 按键配置
const int buttonPin = 7;  // GPIO7连接按钮
const int shortPressTime = 50;  // 短按时间阈值(毫秒)
const int longPressTime = 2000; // 长按时间阈值(毫秒)

int buttonState = HIGH;      // 当前按键状态
int lastButtonState = HIGH;  // 上一次按键状态
unsigned long pressStartTime = 0;  // 按键按下开始时间
bool isLongPress = false;    // 是否满足长按条件

// 深度休眠配置
const uint64_t WAKEUP_PIN_BITMASK = 0b0010;     //GPIO0-GPIO3
//RTC_DATA_ATTR int bootCount = 0;

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_GPIO : Serial.println("Wakeup caused by external signal using GPIO"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.println("Wakeup was not caused by deep sleep"); break;
  }
}

void sendWOLPacket() {
  WiFiUDP udp;
  
  // 创建魔法包
  uint8_t magicPacket[102];
  
  // 前6字节是0xFF
  for(int i = 0; i < 6; i++) {
    magicPacket[i] = 0xFF;
  }
  
  // 解析MAC地址
  uint8_t mac[6];
  sscanf(macAddress, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  
  // 剩余部分为16次重复的MAC地址
  for(int i = 1; i <= 16; i++) {
    for(int j = 0; j < 6; j++) {
      magicPacket[i * 6 + j] = mac[j];
    }
  }
  
  // 发送UDP广播包
  udp.beginPacket("255.255.255.255", wolPort);
  udp.write(magicPacket, sizeof(magicPacket));
  udp.endPacket();
  
  Serial.println("已发送WOL魔法包");
}

void executeSSHCommand() {
  ssh_session session = ssh_new();
  if (session == NULL) return;

  ssh_options_set(session, SSH_OPTIONS_HOST, ssh_host);
  ssh_options_set(session, SSH_OPTIONS_PORT, &ssh_port);
  ssh_options_set(session, SSH_OPTIONS_USER, ssh_user);

  if (ssh_connect(session) != SSH_OK) {
    Serial.println("SSH connection failed");
    ssh_free(session);
    return;
  }

  if (ssh_userauth_password(session, NULL, ssh_password) != SSH_AUTH_SUCCESS) {
    Serial.println("Authentication failed");
    ssh_disconnect(session);
    ssh_free(session);
    return;
  }

  ssh_channel channel = ssh_channel_new(session);
  if (channel && 
      ssh_channel_open_session(channel) == SSH_OK && 
      ssh_channel_request_exec(channel, command) == SSH_OK) {
    
    Serial.println("Command output:");
    char buffer[256];
    int nbytes;
    while ((nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0)) > 0) {
      Serial.write(buffer, nbytes);
    }
  }

  if (channel) {
    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
  }
  ssh_disconnect(session);
  ssh_free(session);
}

void mMainTask(void *pvParameters) {
  // 主循环（替代loop）
  while (true) {
  // 读取按键状态
    buttonState = digitalRead(buttonPin);

    // 检测按键按下（从高电平到低电平）
    if (buttonState == LOW && lastButtonState == HIGH) {
      pressStartTime = millis();  // 记录按下开始时间
      isLongPress = false;       // 重置长按标志
      Serial.println("Button pressed down");
    }

    // 检测按键释放（从低电平到高电平）
    if (buttonState == HIGH && lastButtonState == LOW) {
      // 计算按下持续时间
      unsigned long pressDuration = millis() - pressStartTime;
      
      if (pressDuration >= longPressTime) {
        //Serial.println("Long press released");
        //执行关机
        executeSSHCommand();
        digitalWrite(LED_PIN, LOW);
        //进入深度休眠
        gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
        esp_deep_sleep_start();
       } else {
        //Serial.println("Short press released");
        //执行开机
        sendWOLPacket();
        digitalWrite(LED_PIN, HIGH);
       }
    }

    // 检测是否达到长按时间
    if (buttonState == LOW && (millis() - pressStartTime) >= longPressTime) {
      isLongPress = true;
    }
    
    lastButtonState = buttonState;
    delay(10);
  }
}


void setup() {
  //初始化串口
  Serial.begin(115200);

  //初始化LED
  pinMode(LED_PIN, OUTPUT); // 设置GPIO7为输出模式
  digitalWrite(LED_PIN, LOW);

  //初始化按钮引脚(使用内部上拉电阻)
  pinMode(buttonPin, INPUT_PULLUP);
  
  //连接WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Serial.println("");
  // Serial.println("WiFi连接成功");
  // Serial.print("IP地址: ");
  // Serial.println(WiFi.localIP());
 
  //深度休眠唤醒处理
  //print_wakeup_reason();
  //检查是否是按键唤醒
  if(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
    //Serial.println("Button wakeup detected - executing boot sequence");
    //执行开机
    sendWOLPacket();
    digitalWrite(LED_PIN, HIGH);
  }

  //深度休眠配置
  gpio_deep_sleep_hold_dis();
  esp_err_t errPD = esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
//  Serial.print("esp_sleep_pd_config: ");
  switch(errPD) {
    case ESP_OK: Serial.println("ESP_OK"); break;
    case ESP_ERR_INVALID_ARG: Serial.println("ESP_ERR_INVALID_ARG"); break;
    default: Serial.println("None"); break;
  }
  esp_err_t errGPIO = esp_deep_sleep_enable_gpio_wakeup(WAKEUP_PIN_BITMASK, ESP_GPIO_WAKEUP_GPIO_LOW);
//  Serial.print("esp_deep_sleep_enable_gpio_wakeup: ");
  switch(errGPIO) {
    case ESP_OK: Serial.println("ESP_OK"); break;
    case ESP_ERR_INVALID_ARG: Serial.println("ESP_ERR_INVALID_ARG"); break;
    default: Serial.println("None"); break;
  }

  //创建新的主任务，增加栈大小
  xTaskCreatePinnedToCore(
    mMainTask,   // 任务函数
    "MainTask",   // 任务名
    32768,        // 栈大小（32KB）
    NULL,
    1,            // 优先级
    NULL,
    ARDUINO_RUNNING_CORE
  );
 }

void loop() {
    //  Serial.println("loop!!!");
    // //进入深度休眠
    // gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
    // esp_deep_sleep_start();
}