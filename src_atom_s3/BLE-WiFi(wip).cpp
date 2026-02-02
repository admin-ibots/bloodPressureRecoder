#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <M5Unified.h>
#include <HTTPClient.h>
#include "../secretes/bpr_webhooks.h"

// --- 外部參數配置 ---
// define WIFI: HOME or OFFICE
#define  WIFI HOME
#include "../secretes/wifi_credentials.h"

// only works in office: String url = N8N_BPR_LOCALHOST_HTTP_WEBHOOK; 
String url = N8N_BPR_CLOUD_FLARE_HTTP_WEBHOOK;

// --- BLE HID 標準 UUID ---
static const NimBLEUUID hidServiceUUID((uint16_t)0x1812);
static const NimBLEUUID reportUUID((uint16_t)0x2A4D);
static const NimBLEUUID reportMapUUID((uint16_t)0x2A4B);
static const NimBLEUUID protocolModeUUID((uint16_t)0x2A4E);
static const NimBLEUUID batteryServiceUUID((uint16_t)0x180F);
static const NimBLEUUID batteryLevelCharUUID((uint16_t)0x2A19);

int remoteBatLevel = -1; // Stores the keyboard's battery level

// --- BLE 全域變數 ---
static NimBLEAdvertisedDevice* myDevice = nullptr;
static NimBLEClient* pClient = nullptr;
static bool doConnect = false;

// --- BLE KEYBOARD INPUT 處理變數 ---
String inputBuffer = "";
unsigned long lastKeyEventTime = 0;
const unsigned long SLEEP_TIMEOUT = 2000000; // 20 seconds
bool screenIsOn = true;

String valUR = "";  // Stores "A" or "B"
String valBU = "";  // Stores BU digits
String valBD = "";  // Stores BD digits
String valHR = "";  // Stores HR digits

String finalData = "";

int inputStep = 0;  // 0: idle, 1: BU, 2: BD, 3: HR
String tempDigits = ""; // Temporary buffer for the current number being typed

// --- UI 繪製函數 ---

/** 繪製右上角鍵盤電量 */
void drawBattery() {
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(BLACK);
    M5.Display.setTextDatum(top_right);
    
    // Clear the corner
    //M5.Display.fillRect(70, 0, 58, 20, GREEN); 
    
    String batText = (remoteBatLevel == -1) ? "KB: ???" : "KB:" + String(remoteBatLevel) + "%";

    M5.Display.drawString(batText, 125, 5);
}

/** 繪製側邊欄已輸入的數值 */
void drawSidebar() {
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(BLACK);
    M5.Display.setTextDatum(top_left); // Switch to left for the sidebar

    int startX = 10; // Align under the "K" of KB:100%
    if (valUR != "")  M5.Display.drawString("UR:" + valUR, 40, 24);
    if (valBU != "")  M5.Display.drawString("BU:" + valBU, startX, 52);
    if (valBD != "")  M5.Display.drawString("BD:" + valBD, startX, 80);
    if (valHR != "")  M5.Display.drawString("HR:" + valHR, startX, 108);
}

/** 繪製主畫面 (綠底紅心) */
void drawRedHeart() {
    M5.Display.fillScreen(GREEN);
    int x = M5.Display.width() / 2;
    int y = M5.Display.height() / 2;
    // Simple heart using two circles and a triangle
    M5.Display.fillCircle(x - 15, y - 10, 15, RED);
    M5.Display.fillCircle(x + 15, y - 10, 15, RED);
    // M5.Display.fillTriangle(x - 31, y - 2, x + 31, y - 2, x, y + 30, RED);
    M5.Display.fillTriangle(x - 27, y - 0, x + 27, y - 0, x, y + 30, RED);
    M5.Display.fillRect(x-10, y-10, 20, 10, RED); 

    drawBattery();
    drawSidebar();
}

/** 更新畫面 */
void refreshDisplay(String text) {
    M5.Display.fillScreen(GREEN);
    M5.Display.setTextColor(BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.drawString(text, M5.Display.width() / 2, M5.Display.height() / 2);

    drawBattery();
    drawSidebar();
}

/** 顯示全螢幕狀態訊息 */
void showStatus(int COLOR, String line1, String line2="", String line3="") {
    M5.Display.setBrightness(100);
    M5.Display.fillScreen(COLOR);

    M5.Display.setTextColor(BLACK); 
    M5.Display.setTextSize(5);
    M5.Display.setTextDatum(middle_center);
    int x = M5.Display.width() / 2;
    int y = M5.Display.height() / 2;

    M5.Display.setTextSize(3);
    y=32; M5.Display.drawString(line1, x, y);
    y=64; M5.Display.drawString(line2, x, y);    
    y=84; M5.Display.drawString(line3, x, y);   
}

/** 切換螢幕開及關 **/
void toggle_display() {
    if (!screenIsOn) {
        M5.Display.wakeup();
        M5.Display.setBrightness(100); 
        screenIsOn = true;
        Serial.println("Display: Awake");
    } else {
        M5.Display.sleep();
        screenIsOn = false;
        Serial.println("Display: Sleeping");
    }
}

/** 呼叫 n8n Webhook */
boolean call_n8n(String data) {
    WiFiClient client;
    HTTPClient http;

    Serial.println("--- Call n8n ---");
    
    String funalURL = url + data;
    Serial.println("Final URL: " + funalURL);
    http.begin(client, funalURL);
    http.setTimeout(10000); 
    http.addHeader("Connection", "close"); // 既然會重置，不要用 Keep-alive

    int httpCode = http.GET();

    if (httpCode > 0) {
        Serial.printf("成功! Code: %d\n", httpCode);
        String payload = http.getString();
        if (httpCode == 404) {
            Serial.println("警告: n8n 找不到 Webhook，請確認 Workflow 已 Active 並使用正式 URL。");
            showStatus(RED, "n8n", "Error");
        }
        Serial.println(payload);
        http.end();
        return true;
    } else {
        Serial.printf("失敗! 錯誤: %s\n", http.errorToString(httpCode).c_str());
        // 如果連線被拒絕，強制停止 client 釋放 socket
        client.stop();
        http.end();
        return false;
    }

}

/** 驗證資料格式是否正確 */
bool validateFormat(String s) {
    // 1. 檢查開頭是否為 ?UR=
    if (!s.startsWith("?UR=")) return false;

    // 2. 檢查 UR 是否為 A 或 B
    if (s[4] != 'A' && s[4] != 'B') return false;

    // 3. 檢查關鍵標記是否存在
    int buIdx = s.indexOf("BU=");
    int bdIdx = s.indexOf("BD=");
    int hrIdx = s.indexOf("HR=");

    if (buIdx == -1 || bdIdx == -1 || hrIdx == -1) return false;

    // 4.1. 提取 BU (會自動處理 2 位或 3 位數)
    int buStart = s.indexOf("BU=") + 3;
    int buEnd = s.indexOf("&&", buStart);
    int bu = s.substring(buStart, buEnd).toInt();
    if (bu < 50 || bu > 300) return false;

    // 4.2. 提取 BD
    int bdStart = s.indexOf("BD=") + 3;
    int bdEnd = s.indexOf("&HR="); // 注意這裡 HR 前面只有一個 &
    int bd = s.substring(bdStart, bdEnd).toInt();
    if (bd < 50 || bd > 200) return false;

    // 4.3 提取 HR
    int hrStart = s.indexOf("HR=") + 3;
    int hr = s.substring(hrStart).toInt();        
    if (hr < 40 || hr > 180) return false;

    return true;
}

/** BLE 接收到資料的回呼函數 */
void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    lastKeyEventTime = millis();

    if (length >= 3) {
        uint8_t keyCode = pData[2];
        if (keyCode == 0x00 || keyCode == 0x53) return;

        char c = 0;
        // Map keys
        if (keyCode >= 0x59 && keyCode <= 0x61) c = (char)('1' + (keyCode - 0x59));
        else if (keyCode == 0x62) c = '0';
        else if (keyCode == 0x54) c = '/';
        else if (keyCode == 0x55) c = '*';
        else if (keyCode == 0x56) c = '-';
        else if (keyCode == 0x57) c = '+';
        else if (keyCode == 0x63) c = '.'; // Numpad Dot
        else if (keyCode == 0x58) { // Enter - Reset all
            // 1. Construct the data string
            finalData = "?UR=" + valUR + "&&BU=" + valBU + "&&BD=" + valBD + "&&HR=" + valHR;

            //valid finalData before upload
            if (validateFormat(finalData)) {
                Serial.println("Final data is valid.");
                call_n8n(finalData);
                M5.Display.fillScreen(GREEN);
                M5.Display.setTextSize(3);
                M5.Display.setTextDatum(middle_center);
                M5.Display.drawString("SENT", M5.Display.width() / 2, M5.Display.height() / 2);
            } else {
                Serial.println("Final data is invalid.");
                M5.Display.fillScreen(RED);
                M5.Display.setTextSize(3);
                M5.Display.setTextDatum(middle_center);
                M5.Display.drawString("ERROR", M5.Display.width() / 2, M5.Display.height() / 2);
            }
            Serial1.flush();
            Serial.println("----------------------");
            Serial.println(finalData);

            delay(1000); // Brief pause to show "SENT" or "ERROR"

            // 4. Reset variables and return to Heart screen
            valUR = ""; valBU = ""; valBD = ""; valHR = ""; 
            inputStep = 0; tempDigits = "";
            drawRedHeart(); 
            return;
        }
       
        

        if (c == '-') { valUR = "A"; inputStep = 1; tempDigits = ""; }
        else if (c == '+') { valUR = "B"; inputStep = 1; tempDigits = ""; }
        else if ((c >= '0' && c <= '9') || c == '.') {
            tempDigits += c;
            //Serial.println("Typed: " + tempDigits);
            
            // Logic: Move to next field if 3 digits reached OR dot pressed
            if (tempDigits.length() == 3 || c == '.') {
                if (c == '.') tempDigits.remove(tempDigits.length()-1); // Remove dot from string
                //Serial.println("after 3 or .: " + tempDigits);

                if (inputStep == 1) { valBU = tempDigits; inputStep = 2; }
                else if (inputStep == 2) { valBD = tempDigits; inputStep = 3; }
                else if (inputStep == 3) { valHR = tempDigits; inputStep = 0; }

                //Serial1.printf("Set values: UR=%s, BU=%s, BD=%s, HR=%s\n", valUR.c_str(), valBU.c_str(), valBD.c_str(), valHR.c_str());
                tempDigits = ""; // Reset for next field
            }
        }
        
        // Refresh display
        M5.Display.fillScreen(GREEN);
        drawBattery();
        drawSidebar();
        //Show current typing progress in center
        M5.Display.setTextDatum(middle_center);

        M5.Display.setTextSize(3);
        // M5.Display.drawString(tempDigits == "" ? valUR : tempDigits, 10, 64);
        M5.Display.drawString(tempDigits, 16, 16);

        if (c == '/') {
            toggle_display();
            return;
        }
    }
}

/** 連線至 BLE 鍵盤 */
bool connectToServer() {
    if (pClient != nullptr) NimBLEDevice::deleteClient(pClient);
    pClient = NimBLEDevice::createClient();

    Serial.println(">>> 1. Connecting to physical layer...");
    if (!pClient->connect(myDevice)) return false;

    // 2. 強制加密 (Just Works)
    Serial.println(">>> 2. Requesting Security...");
    pClient->secureConnection();
    
    // 等待 LED 常亮 (代表加密完成)
    for(int i=0; i<3; i++) { delay(1000); Serial.print("."); }
    Serial.println(" Encrypted.");

    // 2.5 --- Get Keyboard Battery Level (Service 0x180F) ---
    NimBLERemoteService* pBatService = pClient->getService(batteryServiceUUID);
    if (pBatService) {
        NimBLERemoteCharacteristic* pBatChar = pBatService->getCharacteristic(batteryLevelCharUUID);
        if (pBatChar) {
            remoteBatLevel = pBatChar->readValue<uint8_t>();
            Serial.printf(">>> 2.5 Keyboard Battery Found: %d%%\n", remoteBatLevel);
        }
    }

    // 3. 獲取 HID 服務
    NimBLERemoteService* pRemoteService = pClient->getService(hidServiceUUID);
    bool subSuccess = false;

    if (pRemoteService != nullptr) {
        Serial.println(">>> 3. HID Service Found. Initializing...");

        NimBLERemoteCharacteristic* pReportMap = pRemoteService->getCharacteristic(reportMapUUID);
        if (pReportMap) {
            Serial.println("   - Reading Report Map...");
            pReportMap->readValue();
        }

        // 搜尋並訂閱所有 Report Characteristics (2A4D)
        auto charas = pRemoteService->getCharacteristics(true); 
        for (auto &chara : *charas) {
            if (chara->getUUID() == reportUUID) {
                Serial.printf("   - Found Report: %s\n", chara->getUUID().toString().c_str());

                if (chara->canNotify()) {
                    NimBLERemoteDescriptor* pDesc = chara->getDescriptor(NimBLEUUID((uint16_t)0x2902));
                    if (pDesc) {
                        uint8_t val[] = {0x01, 0x00};
                        pDesc->writeValue(val, 2, true);
                        Serial.println("     ==> CCCD Notification Enabled Manually.");
                    }

                    // [關鍵點 B] 訂閱通知
                    if (chara->subscribe(true, notifyCallback)) {
                        Serial.println("     ==> Subscribed successfully.");
                        subSuccess = true;
                    }
                }
            }
        }
    } else {
        Serial.println("!!! HID Service (1812) not found.");
    }

    // 4. [關鍵點 C] 檢查自定義服務 (6e40...)
    // 有些鍵盤雖然有 HID 服務，但其實是透過自定義串口 (NUS) 傳資料
    NimBLERemoteService* pCustomService = pClient->getService("6e40ff01-b5a3-f393-e0a9-e50e24dcca9e");
    if (pCustomService) {
        Serial.println(">>> 4. Found Custom Service (NUS). Trying to subscribe...");
        auto cCharas = pCustomService->getCharacteristics(true);
        for (auto &c : *cCharas) {
            if (c->canNotify()) {
                if (c->subscribe(true, notifyCallback)) {
                    Serial.printf("   - Subscribed to Custom Chara: %s\n", c->getUUID().toString().c_str());
                    subSuccess = true;
                }
            }
        }
    }

    // 5. 優化連線參數 (喚醒省電模式)
    if (subSuccess) {
        Serial.println(">>> 5. Updating Connection Parameters...");
        pClient->updateConnParams(12, 12, 0, 60); 
    }

    if (pClient->isConnected()) {
        Serial.println(">>> BLE Connection and Subscription Successful!");
        M5.Display.fillScreen(GREEN);
        drawRedHeart();
    }

    if (subSuccess) {
        pClient->updateConnParams(12, 12, 0, 60); 
        Serial.println(">>> 5. Connection Parameters Optimized.");
        
        // Final UI Update
        drawRedHeart(); 
    }

    lastKeyEventTime = millis();

    return subSuccess;
}

class MyCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (advertisedDevice->getName() == "YaRan KeyPad") {
            Serial.println(">>> Target Found! Stopping scan...");
            NimBLEDevice::getScan()->stop();

            // 安全做法：如果之前有舊的對象，先刪除避免記憶體洩漏
            if (myDevice != nullptr) {
                delete myDevice;
            }
            // 複製對象，確保 connect 時它還存在
            myDevice = new NimBLEAdvertisedDevice(*advertisedDevice);

            doConnect = true;

            showStatus(GREEN, "BLE", "Parse", "...");
        }
    }
};

void setup() {
    // finalData.reserve(64);
    // valUR.reserve(4);
    // valBU.reserve(4);  
    // valBD.reserve(4);  
    // valHR.reserve(4);

    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    // Screen background -> Green
    // M5.Display.setBrightness(100);
    // M5.Display.fillScreen(GREEN);

    // text: color->black, size->5, alignment to center-middle
    M5.Display.setTextColor(BLACK); 
    M5.Display.setTextSize(5);
    M5.Display.setTextDatum(middle_center);
    int x = M5.Display.width() / 2;
    int y = M5.Display.height() / 2;    

    delay(100);
    Serial.println("\n--- Atom S3 Start ---");
    
    showStatus(GREEN, "WiFi", "Start", "...");

    // 1. WiFi 階段 (連線並完成任務)
    WiFi.begin("MAC", "Kpc24642464");
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry++ < 20) { delay(500); Serial.print(".");}

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[1] WiFi Connected. Running n8n...");
        showStatus(GREEN, "Connect", "n8n", "...");
        if (!call_n8n(" ")) {
            Serial.println("n8n 呼叫失敗，重新啟動中...");
            showStatus(RED, "Connect", "n8n", "Failed");
            delay(3000);
            ESP.restart();
        }
        
    }

    Serial.printf("[2] Free Heap before BLE: %d\n", ESP.getFreeHeap());

    M5.Display.sleep(); 
    
    showStatus(GREEN, "BLE KB", "Scan", "...");

    // 3. 啟動 NimBLE (直接啟動，不要手動呼叫 esp_bt_... 系列函數)
    Serial.println("[3] Starting NimBLE...");
    
    // 名稱不要太長，避免佔用 Data 段空間
    NimBLEDevice::init("S3-Host");

    // 4. 配置連線參數 (針對 HID 鍵盤優化)
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, true, true);

    Serial.println("[4] SUCCESS! Starting Scan...");
    // NimBLEDevice::getScan()->start(5, false);
    // Serial.println("--- All Done ---");


    // 5. 啟動掃描 (使用保守的掃描參數)
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyCallbacks(), false);
    pScan->setInterval(100); // 掃描間隔長一點，給 WiFi 留呼吸空間
    pScan->setWindow(50);    
    pScan->setActiveScan(true);
    
    Serial.println(">>> Phase 3: Start Scanning...");
    pScan->start(0, false); // 不設限時，持續掃描

    M5.Display.wakeup();
}

void checkConnections() {
    Serial.println("\n--- [連線狀態檢查] ---");

    Serial.println("Free Heap: " + String(ESP.getFreeHeap()));

    // 1. 檢查 WiFi 狀態
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi: 已連線 (IP: %s, RSSI: %d dBm)\n", 
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("WiFi: 斷線！嘗試重新連線...");
        // 注意：這裡只在斷線時才呼叫 begin，不會影響現有連線
        WiFi.begin(ssid, password); 
    }

    // 2. 檢查 BLE 掃描狀態 (NimBLE)
    size_t clientCount = NimBLEDevice::getClientListSize();
    NimBLEScan* pScan = NimBLEDevice::getScan();

    if (clientCount > 0) {
        // 情況 A: 已經連上設備了
        Serial.println(clientCount);
    } else {
        // 情況 B: 目前沒有設備連線
        Serial.println(">>> 無連線設備，重新開啟掃描中...");
    }
}

unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 300000; 
void loop() {
    if (doConnect) {
        doConnect = false;
        if (connectToServer()) {
            // drawRedHeart(); 
            Serial.println(">>> SUCCESS: System Ready. Press keys now!");
        } else {
            Serial.println(">>> FAILED: Setup incomplete. Retrying...");
            NimBLEDevice::getScan()->start(0);
        }
    }  

    // delay(10);
    M5.update();

    if (millis() - lastCheckTime >= checkInterval) {
        lastCheckTime = millis();
        Serial.println("\n--- 5 minutes passed, restarting... ---");
        ESP.restart();
        // checkConnections();
    }

    // if (screenIsOn && (millis() - lastKeyEventTime > SLEEP_TIMEOUT)) {
    //     M5.Display.sleep();
    //     M5.Display.setBrightness(0);
    //     screenIsOn = false;
    //     Serial.println("Idle timeout: Sleeping display");
    // }

    // Check if the main screen button was pressed
    if (M5.BtnA.wasPressed()) {
        lastKeyEventTime = millis();
        toggle_display();
        // if (!screenIsOn) {
        //     // WAKE UP
        //     M5.Display.wakeup();
        //     M5.Display.setBrightness(100); 
        //     screenIsOn = true;
        //     Serial.println("Display: Awake");
        // } else {
        //     // GO TO SLEEP
        //     M5.Display.sleep();
        //     // Note: Some versions also need brightness 0 to kill the LED driver
        //     M5.Display.setBrightness(0); 
        //     screenIsOn = false;
        //     Serial.println("Display: Sleeping");
        // }
    }      

}