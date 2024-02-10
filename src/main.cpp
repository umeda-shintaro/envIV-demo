#include <M5Unified.h>
#include <SensirionI2CSht4x.h>
#include <Adafruit_BMP280.h>
#include "Adafruit_Sensor.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <deque>
#include <SPIFFS.h>

// WiFi network name and password:
const char * ssid = "hoge";
const char * password = "fuga";

// データ取得間隔[msec]
uint32_t delayTime = 600000;


// 5分間のデータを保存するためのデータ構造
std::deque<float> pressures;
std::deque<float> temperatures;
std::deque<float> humidities;

// Server details
AsyncWebServer  server(80);


Adafruit_BMP280 bmp;      // 気圧センサBMP280用のインスタンスを作成
SensirionI2CSht4x sht4x;  // 温湿度センサSHT40用のインスタンスを作成
M5Canvas canvas(&M5.Lcd); // 液晶表示用、メモリ描画領域表示（スプライト）のインスタンスを作成


float temperature, pressure, humidity;  // 測定値格納用
uint16_t error;         // SHT40エラー格納用
char errorMessage[256]; // SHT40エラーメッセージ格納用


// エラー文字列変換関数
String readHtmlFile() {
  File file = SPIFFS.open("/graph.html", "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return String();
  }

  String html = file.readString();
  file.close();

  String pressureLabels, pressureData, temperatureLabels, temperatureData, humidityLabels, humidityData;
  for (int i = 0; i < pressures.size(); i++) {
    if (i != 0) {
      pressureLabels += ",";
      pressureData += ",";
    }
    pressureLabels += String(i);
    pressureData += String(pressures[i]);
  }
  for (int i = 0; i < temperatures.size(); i++) {
    if (i != 0) {
      temperatureLabels += ",";
      temperatureData += ",";
    }
    temperatureLabels += String(i);
    temperatureData += String(temperatures[i]);
  }
  for (int i = 0; i < humidities.size(); i++) {
    if (i != 0) {
      humidityLabels += ",";
      humidityData += ",";
    }
    humidityLabels += String(i);
    humidityData += String(humidities[i]);
  }

  html.replace("%PRESSURE_LABELS%", pressureLabels);
  html.replace("%PRESSURE_DATA%", pressureData);
  html.replace("%TEMPERATURE_LABELS%", temperatureLabels);
  html.replace("%TEMPERATURE_DATA%", temperatureData);
  html.replace("%HUMIDITY_LABELS%", humidityLabels);
  html.replace("%HUMIDITY_DATA%", humidityData);

  return html;
}
// 初期設定 -----------------------------------------
void setup() {
  auto cfg = M5.config();  // 本体初期設定
  M5.begin(cfg);  

  Wire.begin(32, 33);  // I2C通信初期化　SDA=32, SCL=33
  Serial.begin(9600);  // シリアル通信初期化
  while (!Serial) {
      delay(100);
  }
    
  // ベース画面の初期設定
  M5.Lcd.fillScreen(BLACK); // 背景色
  M5.Lcd.setRotation(1);    // 画面向き設定（USB位置基準 0：下/ 1：右/ 2：上/ 3：左）
  M5.Lcd.setTextSize(1);    // 文字サイズ（整数倍率）

  canvas.setTextWrap(true); // 改行をする（画面をはみ出す時自動改行する場合はtrue。書かないとtrue）
  canvas.createSprite(M5.Lcd.width()-10, M5.Lcd.height()); // canvasサイズ（メモリ描画領域）設定（画面サイズに設定）
  canvas.setCursor(0, 0);   // 表示座標
  canvas.setFont(&fonts::Font4); // フォント

  // 気圧センサ BMP280初期化
  while (!bmp.begin(0x76)) {  // 通信失敗でエラー処理
    Serial.println("BMP280 fail"); // シリアル出力
    canvas.setCursor(0, 0);        // 液晶表示
    canvas.println("BMP280 fail");
    canvas.pushSprite(&M5.Lcd, 10, 15); // 画面を指定座標に一括表示実行
    delay(1000);
  }
  // BMP280のサンプリングレートとフィルタ設定
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,   // モード：ノーマル
                  Adafruit_BMP280::SAMPLING_X2,   // 温度サンプリングレート：2倍
                  Adafruit_BMP280::SAMPLING_X16,  // 圧力サンプリングレート：16倍
                  Adafruit_BMP280::FILTER_X16,    // フィルタ：16倍
                  Adafruit_BMP280::STANDBY_MS_500);  // 待ち時間：500ms
  Serial.println("BMP280 OK!");

  // 温湿度センサ SHT40初期化
  sht4x.begin(Wire, 0x44);
  uint32_t serialNumber;                     // シリアルナンバー格納用
  error = sht4x.serialNumber(serialNumber);  // シリアルナンバー取得実行
  if (error) {  // シリアルナンバー取得失敗でエラー処理
    Serial.print("SHT40 Error: ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else {      // シリアルナンバー取得成功で準備完了
    Serial.println("SHT40 OK!");
    Serial.print("Serial Number: ");
    Serial.println(serialNumber);
  }


  // Connect to Wi-Fi network with SSID and password
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println(WiFi.localIP());

  server.on("/read", HTTP_GET, [](AsyncWebServerRequest *request){
  String html = readHtmlFile();
  Serial.println("HTTP GET /read");
  request->send(200, "text/html", html);
  });

  server.begin();

  if (!SPIFFS.begin()) {
  Serial.println("An Error has occurred while mounting SPIFFS");
  return;
}
  Serial.println("Connected to Wi-Fi");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// メイン -----------------------------------------
void loop() {
  // LCD表示設定
  canvas.fillScreen(BLACK); // 画面初期化
  canvas.setFont(&fonts::lgfxJapanGothicP_28);  // フォント
  canvas.setCursor(0, 0);   // メモリ描画領域の表示座標指定

  // IPアドレス表示
  canvas.setFont(&fonts::Font0); // IPアドレスのフォントを小さくする
  canvas.println(WiFi.localIP());
  canvas.setFont(&fonts::lgfxJapanGothicP_28);  

  // 圧力データ取得
  float pressure = bmp.readPressure();
  pressures.push_back(pressure);
  if (pressures.size() > 300) pressures.pop_front(); // 保持上限を超えたデータを削除

  // 温湿度データ取得、エラー処理
  float temperature, humidity;
  error = sht4x.measureHighPrecision(temperature, humidity);
  temperatures.push_back(temperature);
  humidities.push_back(humidity);
  Serial.println(temperatures.size());
  if (temperatures.size() > 300) temperatures.pop_front(); // 保持上限を超えたデータを削除
  if (humidities.size() > 300) humidities.pop_front();
  if (error) {  // trueならエラー処理
    errorToString(error, errorMessage, 256); // エラーを文字列に変換
    Serial.print("SHT40 Error: "); // シリアル出力
    Serial.println(errorMessage);

    canvas.setFont(&fonts::Font4); // 液晶表示
    canvas.printf("SHT40 Error:\n");
    canvas.print(errorMessage);
  } else {      // falseで測定データ表示実行
    Serial.printf("Press:%.0fhPa, Temp:%.1fc, Hum:%.1f%%\n", pressure, temperature, humidity); // シリアル出力

    canvas.printf("気圧：%.0fhPa\n", pressure / 100); // 液晶表示
    canvas.printf("温度：%.1f℃\n", temperature);
    canvas.printf("湿度：%.1f％\n", humidity);
  }
  canvas.pushSprite(&M5.Lcd, 10, 15);  // 画面を指定座標に一括表示実行
  delay();  // 取得間隔[msec]
}
