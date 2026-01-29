#include <WiFi.h>
#include <HTTPClient.h>

// const char* ssid = "TP-LINK_D413";
// const char* password = "12345678";
const char* ssid = "MAC";
const char* password = "Kpc24642464";
// 建議改用正式網址（去掉 -test），並在 n8n 點擊 Active
//String url = "http://192.168.0.102:5678/webhook/620e6635-20ae-44e7-b21e-a77e9cedb343"; // Upload/Reset 後，剛開始會有失敗，但幾次後(10次內)，開始成功後就 OK
String url = "http://n8n4090.yo3dp.cc/webhook/620e6635-20ae-44e7-b21e-a77e9cedb343"; // Upload/Reset 後，剛開始會有失敗，但幾次後(10次內)，開始成功後就 OK

unsigned long startMs;
unsigned long duration;

WiFiClient client;
HTTPClient http;

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.println("ESP32 Start");

    // 1. 強制清理 WiFi 緩存
    WiFi.disconnect(true);
    delay(1000);
    
    WiFi.begin(ssid, password);
    WiFi.setSleep(false); 

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected!");

    // 2. 顯示診斷資訊
    Serial.printf("IP: %s, GW: %s\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());

    // 3. 第一次連線前給予緩衝，避免 Reset 後立即攻擊伺服器
    delay(2000); 
}

void call_n8n() {
    Serial.println("--- Call n8n ---");
    
    // 每次呼叫前確保狀態乾淨
    http.begin(client, url);
    http.setTimeout(10000); 
    http.addHeader("Connection", "close"); // 既然會重置，不要用 Keep-alive

    int httpCode = http.GET();

    if (httpCode > 0) {
        Serial.printf("成功! Code: %d\n", httpCode);
        String payload = http.getString();
        if (httpCode == 404) {
            Serial.println("警告: n8n 找不到 Webhook，請確認 Workflow 已 Active 並使用正式 URL。");
        }
        Serial.println(payload);
    } else {
        Serial.printf("失敗! 錯誤: %s\n", http.errorToString(httpCode).c_str());
        // 如果連線被拒絕，強制停止 client 釋放 socket
        client.stop();
    }
    http.end();
}

void loop() {
    startMs = millis(); // 記錄開始毫秒數
    call_n8n();
    duration = millis() - startMs; // 計算耗時
    Serial.printf("n8n 耗時: %lu 毫秒\n", duration);
    delay(2000); // 增加間隔，避免被伺服器判定為攻擊
}

