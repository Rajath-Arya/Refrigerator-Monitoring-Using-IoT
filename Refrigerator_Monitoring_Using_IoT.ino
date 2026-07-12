// Required Libraries - Make sure all are installed
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>

// WiFi credentials
const char* ssid = "Rajath Arya";
const char* password = "Rajath Arya";

// Telegram Bot Token
#define BOT_TOKEN "8002786048:AAEWfQfF1Jd9U0FonbJHzqGoBfkX0ATePlg"

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// Sensor pins
#define DHT_PIN 4
#define MQ135_PIN 34
#define DHT_TYPE DHT22

// OLED Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHT_PIN, DHT_TYPE);
Preferences preferences;

// Timing
unsigned long lastBotCheck = 0;
const unsigned long BOT_CHECK_INTERVAL = 500;  // Check every 0.5 seconds

unsigned long lastSensorCheck = 0;
const unsigned long SENSOR_CHECK_INTERVAL = 10000;

unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 2000;

unsigned long lastExpiryCheck = 0;
const unsigned long EXPIRY_CHECK_INTERVAL = 3600000; // Check every hour

// Alert thresholds
#define TEMP_HIGH_THRESHOLD 35.0
#define TEMP_LOW_THRESHOLD 15.0
#define HUMIDITY_HIGH_THRESHOLD 80.0
#define HUMIDITY_LOW_THRESHOLD 20.0
#define GAS_DANGER_THRESHOLD 2500

// Alert tracking
int tempAlertCount = 0;
int humidityAlertCount = 0;
int gasAlertCount = 0;
const int MAX_ALERTS = 2;

bool tempAlertActive = false;
bool humidityAlertActive = false;
bool gasAlertActive = false;

// Store chat ID for alerts
String alertChatId = "";
bool alertsEnabled = false;

// Current sensor readings
float currentTemp = 0;
float currentHumidity = 0;
int currentGas = 0;
String currentAirQuality = "Reading...";
String currentAlert = "";

// Refrigerator items storage
struct FridgeItem {
  String name;
  String expiryDate; // Format: DD-MM-YYYY
  bool notified[3]; // [0]=today, [1]=tomorrow, [2]=2days
};

FridgeItem fridgeItems[20]; // Max 20 items
int itemCount = 0;

// Add new item mode
bool addItemMode = false;
String newItemName = "";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize preferences
  preferences.begin("fridge", false);
  loadFridgeItems();
  
  Wire.begin();
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("❌ SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Fridge Monitor"));
  display.println(F("Starting..."));
  display.display();
  delay(2000);
  
  dht.begin();
  
  Serial.println("\n🚀 Starting Smart Refrigerator Monitor");
  Serial.println("📡 Connecting to WiFi...");
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Connecting WiFi"));
  display.display();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("📍 IP: ");
    Serial.println(WiFi.localIP());
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("WiFi Connected!"));
    display.display();
    delay(2000);
  } else {
    Serial.println("\n❌ WiFi Failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("WiFi Failed!"));
    display.display();
  }
  
  client.setInsecure();
  configTime(0, 0, "pool.ntp.org");
  
  Serial.println("✅ System Ready!");
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("System Ready!"));
  display.display();
  delay(2000);
  
  bot.getUpdates(0);
  Serial.println("📩 Message queue cleared");
}

void loop() {
  // Check for Telegram commands - PRIORITY!
  if (millis() - lastBotCheck > BOT_CHECK_INTERVAL) {
    lastBotCheck = millis();
    
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    
    if (numNewMessages > 0) {
      Serial.println("📬 New messages: " + String(numNewMessages));
      handleNewMessages(numNewMessages);
    }
  }
  
  // Check sensors for dangerous conditions
  if (alertsEnabled && (millis() - lastSensorCheck > SENSOR_CHECK_INTERVAL)) {
    lastSensorCheck = millis();
    checkDangerousConditions();
  }
  
  // Update OLED display
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
  
  // Check expiring items every hour
  if (millis() - lastExpiryCheck > EXPIRY_CHECK_INTERVAL) {
    lastExpiryCheck = millis();
    checkExpiringItems();
  }
  
  // Small delay to prevent watchdog issues
  yield();
}

void updateDisplay() {
  currentTemp = dht.readTemperature();
  currentHumidity = dht.readHumidity();
  
  currentGas = 0;
  for (int i = 0; i < 5; i++) {
    currentGas += analogRead(MQ135_PIN);
    delay(5);
  }
  currentGas = currentGas / 5;
  
  // Determine foul smell status (only for bad conditions)
  if (currentGas < 2200) {
    currentAirQuality = "Fresh";
  } else if (currentGas < 3000) {
    currentAirQuality = "Foul Smell!";
  } else {
    currentAirQuality = "Bad Smell!";
  }
  
  // Check alerts
  currentAlert = "";
  if (tempAlertActive) {
    if (currentTemp > TEMP_HIGH_THRESHOLD) {
      currentAlert = "TEMP HIGH!";
    } else if (currentTemp < TEMP_LOW_THRESHOLD) {
      currentAlert = "TEMP LOW!";
    }
  }
  if (humidityAlertActive) {
    if (currentHumidity > HUMIDITY_HIGH_THRESHOLD) {
      currentAlert = "HUMID HIGH!";
    } else if (currentHumidity < HUMIDITY_LOW_THRESHOLD) {
      currentAlert = "HUMID LOW!";
    }
  }
  if (gasAlertActive) {
    currentAlert = "FOUL SMELL!";
  }
  
  // Display on OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Temperature
  display.setCursor(0, 0);
  display.print(F("Temp: "));
  if (!isnan(currentTemp)) {
    display.print(currentTemp, 1);
    display.println(F(" C"));
  } else {
    display.println(F("Error"));
  }
  
  // Humidity
  display.setCursor(0, 12);
  display.print(F("Humid: "));
  if (!isnan(currentHumidity)) {
    display.print(currentHumidity, 1);
    display.println(F(" %"));
  } else {
    display.println(F("Error"));
  }
  
  // Air Quality (only show if foul smell detected)
  display.setCursor(0, 24);
  display.print(F("Air: "));
  display.println(currentAirQuality);
  
  // Fridge items count
  display.setCursor(0, 36);
  display.print(F("Items: "));
  display.println(itemCount);
  
  // Show expiring items count
  int expiringCount = getExpiringItemsCount();
  if (expiringCount > 0) {
    display.setCursor(0, 48);
    display.print(F("Expiring: "));
    display.println(expiringCount);
  }
  
  // Alert message
  if (currentAlert != "") {
    display.drawLine(0, 56, 128, 56, SSD1306_WHITE);
    display.setCursor(0, 58);
    display.println(currentAlert);
  }
  
  display.display();
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;

    Serial.println("👤 From: " + from_name);
    Serial.println("💬 Message: " + text);

    // Handle add item mode FIRST before other commands
    if (addItemMode) {
      if (text == "/cancel") {
        addItemMode = false;
        newItemName = "";
        bot.sendMessage(chat_id, "❌ Cancelled", "");
        Serial.println("❌ Add mode cancelled");
        continue;
      }
      
      if (newItemName == "") {
        // First step: Get item name
        newItemName = text;
        String msg = "✅ Item: *" + newItemName + "*\n\n";
        msg += "Now send expiry date\n";
        msg += "Format: DD-MM-YYYY\n";
        msg += "Example: 20-11-2025";
        bot.sendMessage(chat_id, msg, "Markdown");
        Serial.println("📝 Item name set: " + newItemName);
        continue;
      } else {
        // Second step: Get expiry date
        Serial.println("📅 Checking date: " + text);
        
        if (isValidDate(text)) {
          addFridgeItem(newItemName, text);
          
          String msg = "✅ *Added Successfully!*\n\n";
          msg += "📦 " + newItemName + "\n";
          msg += "📅 Expires: " + text + "\n";
          msg += "Total items: " + String(itemCount);
          bot.sendMessage(chat_id, msg, "Markdown");
          
          Serial.println("✅ Item added! Total: " + String(itemCount));
          
          addItemMode = false;
          newItemName = "";
          continue;
        } else {
          bot.sendMessage(chat_id, "❌ Bad date format!\nUse: DD-MM-YYYY\nExample: 20-11-2025", "");
          Serial.println("❌ Invalid date format");
          continue;
        }
      }
    }

    // Regular commands
    if (text == "/start") {
      alertChatId = chat_id;
      alertsEnabled = true;
      
      String welcome = "🧊 *Smart Fridge Monitor*\n\n";
      welcome += "👋 Hello!\n\n";
      welcome += "📋 *Sensors:*\n";
      welcome += "/dht22 - Temp & Humidity\n";
      welcome += "/mq135 - Smell Detection\n"; 
      welcome += "/status - All Readings\n\n";
      welcome += "📦 *Fridge Items:*\n";
      welcome += "/add - Add item\n";
      welcome += "/list - View items\n";
      welcome += "/delete [num] - Remove\n";
      welcome += "/expiring - Expiring soon\n\n";
      welcome += "⚙️ *System:*\n";
      welcome += "/alerts - Toggle alerts\n";
      welcome += "/reset - Reset counters";
      
      bot.sendMessage(chat_id, welcome, "Markdown");
      Serial.println("✅ Start command");
    }
    
    else if (text == "/dht22" || text == "/dht") {
      Serial.println("📊 DHT22 request");
      String response = readDHT();
      bot.sendMessage(chat_id, response, "Markdown");
    }
    
    else if (text == "/mq135" || text == "/gas") {
      Serial.println("📊 MQ135 request");
      String response = readMQ135();
      bot.sendMessage(chat_id, response, "Markdown");
    }
    
    else if (text == "/status" || text == "/all") {
      Serial.println("📊 Status request");
      String status = "📡 *System Status*\n\n";
      status += readDHT() + "\n\n";
      status += readMQ135() + "\n\n";
      status += "📦 Items: " + String(itemCount);
      bot.sendMessage(chat_id, status, "Markdown");
    }
    
    else if (text == "/add") {
      if (itemCount >= 20) {
        bot.sendMessage(chat_id, "❌ Full! (Max 20)\nDelete items first", "");
        Serial.println("❌ Storage full");
      } else {
        addItemMode = true;
        newItemName = "";
        String msg = "📦 *Add New Item*\n\n";
        msg += "Send item name:\n";
        msg += "(e.g., Milk, Eggs, Cheese)";
        bot.sendMessage(chat_id, msg, "Markdown");
        Serial.println("✅ Add mode activated");
      }
    }
    
    else if (text == "/list") {
      Serial.println("📋 List request");
      String list = listFridgeItems();
      bot.sendMessage(chat_id, list, "Markdown");
    }
    
    else if (text == "/expiring") {
      Serial.println("⏰ Expiring request");
      String expiring = getExpiringItems();
      bot.sendMessage(chat_id, expiring, "Markdown");
    }
    
    else if (text.startsWith("/delete")) {
      int index = text.substring(7).toInt() - 1;
      Serial.println("🗑️ Delete request: index " + String(index));
      
      if (index >= 0 && index < itemCount) {
        String itemName = fridgeItems[index].name;
        deleteFridgeItem(index);
        bot.sendMessage(chat_id, "✅ Deleted: " + itemName, "");
        Serial.println("✅ Deleted: " + itemName);
      } else {
        bot.sendMessage(chat_id, "❌ Invalid number\nUse /list to see items", "");
        Serial.println("❌ Invalid index");
      }
    }
    
    else if (text == "/alerts") {
      alertChatId = chat_id;
      alertsEnabled = !alertsEnabled;
      
      String msg = "🔔 Alerts: ";
      msg += alertsEnabled ? "✅ *ON*" : "❌ *OFF*";
      bot.sendMessage(chat_id, msg, "Markdown");
      Serial.println(alertsEnabled ? "✅ Alerts ON" : "❌ Alerts OFF");
    }
    
    else if (text == "/reset") {
      tempAlertCount = 0;
      humidityAlertCount = 0;
      gasAlertCount = 0;
      tempAlertActive = false;
      humidityAlertActive = false;
      gasAlertActive = false;
      currentAlert = "";
      
      bot.sendMessage(chat_id, "🔄 *Counters Reset!*", "Markdown");
      Serial.println("🔄 Reset done");
    }
    
    else if (text == "/help") {
      String help = "📋 *Commands*\n\n";
      help += "*Sensors:* /dht22, /mq135, /status\n";
      help += "*Fridge:* /add, /list, /expiring, /delete\n";
      help += "*System:* /alerts, /reset\n\n";
      help += "💡 Get notifications for expiring items!";
      
      bot.sendMessage(chat_id, help, "Markdown");
      Serial.println("ℹ️ Help sent");
    }
    
    else {
      Serial.println("❓ Unknown: " + text);
    }
  }
}

void checkDangerousConditions() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  int gasValue = 0;
  for (int i = 0; i < 10; i++) {
    gasValue += analogRead(MQ135_PIN);
    delay(10);
  }
  gasValue = gasValue / 10;
  
  // Temperature alerts
  if (!isnan(t)) {
    if ((t > TEMP_HIGH_THRESHOLD || t < TEMP_LOW_THRESHOLD) && tempAlertCount < MAX_ALERTS) {
      if (!tempAlertActive) {
        tempAlertActive = true;
        tempAlertCount++;
        sendTemperatureAlert(t);
      }
    } else if (t >= TEMP_LOW_THRESHOLD && t <= TEMP_HIGH_THRESHOLD) {
      tempAlertActive = false;
    }
  }
  
  // Humidity alerts
  if (!isnan(h)) {
    if ((h > HUMIDITY_HIGH_THRESHOLD || h < HUMIDITY_LOW_THRESHOLD) && humidityAlertCount < MAX_ALERTS) {
      if (!humidityAlertActive) {
        humidityAlertActive = true;
        humidityAlertCount++;
        sendHumidityAlert(h);
      }
    } else if (h >= HUMIDITY_LOW_THRESHOLD && h <= HUMIDITY_HIGH_THRESHOLD) {
      humidityAlertActive = false;
    }
  }
  
  // Gas alerts (only for foul smell - high values)
  if (gasValue > GAS_DANGER_THRESHOLD && gasAlertCount < MAX_ALERTS) {
    if (!gasAlertActive) {
      gasAlertActive = true;
      gasAlertCount++;
      sendGasAlert(gasValue);
    }
  } else if (gasValue <= GAS_DANGER_THRESHOLD) {
    gasAlertActive = false;
  }
}

void sendTemperatureAlert(float temp) {
  String alert = "🚨 *TEMPERATURE ALERT!*\n\n";
  
  if (temp > TEMP_HIGH_THRESHOLD) {
    alert += "⚠️ Temperature VERY HIGH!\n";
    alert += "🌡 Current: *" + String(temp, 1) + " °C*\n";
  } else {
    alert += "⚠️ Temperature VERY LOW!\n";
    alert += "🌡 Current: *" + String(temp, 1) + " °C*\n";
  }
  
  alert += "\n🔔 Alert " + String(tempAlertCount) + " of " + String(MAX_ALERTS);
  
  bot.sendMessage(alertChatId, alert, "Markdown");
}

void sendHumidityAlert(float humidity) {
  String alert = "🚨 *HUMIDITY ALERT!*\n\n";
  
  if (humidity > HUMIDITY_HIGH_THRESHOLD) {
    alert += "⚠️ Humidity VERY HIGH!\n";
    alert += "💧 Current: *" + String(humidity, 1) + " %*\n";
  } else {
    alert += "⚠️ Humidity VERY LOW!\n";
    alert += "💧 Current: *" + String(humidity, 1) + " %*\n";
  }
  
  alert += "\n🔔 Alert " + String(humidityAlertCount) + " of " + String(MAX_ALERTS);
  
  bot.sendMessage(alertChatId, alert, "Markdown");
}

void sendGasAlert(int gasValue) {
  String alert = "🚨 *FOUL SMELL DETECTED!*\n\n";
  alert += "⚠️ BAD ODOR IN FRIDGE!\n";
  alert += "👃 Level: *" + String(gasValue) + "*\n\n";
  alert += "🚨 *ACTION NEEDED:*\n";
  alert += "• Check for spoiled food\n";
  alert += "• Remove expired items\n";
  alert += "• Clean refrigerator\n";
  alert += "\n🔔 Alert " + String(gasAlertCount) + " of " + String(MAX_ALERTS);
  
  bot.sendMessage(alertChatId, alert, "Markdown");
}

String getAlertStatus() {
  String status = "🔔 *Alert Status:*\n\n";
  status += "System: " + String(alertsEnabled ? "✅ ON" : "❌ OFF") + "\n\n";
  status += "🌡 Temp: " + String(tempAlertCount) + "/" + String(MAX_ALERTS) + "\n";
  status += "💧 Humid: " + String(humidityAlertCount) + "/" + String(MAX_ALERTS) + "\n";
  status += "👃 Smell: " + String(gasAlertCount) + "/" + String(MAX_ALERTS);
  
  return status;
}

String readDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  if (isnan(h) || isnan(t)) {
    return "❌ *DHT22 Error*\n\nCheck connections.";
  }
  
  String message = "🌡️ *DHT22 Sensor*\n\n";
  message += "🌡 Temp: *" + String(t, 1) + " °C*\n";
  message += "💧 Humid: *" + String(h, 1) + " %*\n\n";
  
  if (t < 20) {
    message += "Status: ❄️ Cold";
  } else if (t < 26) {
    message += "Status: ✅ Good";
  } else {
    message += "Status: 🔥 Hot";
  }
  
  return message;
}

String readMQ135() {
  // Quick reading - just 3 samples for faster response
  int gasValue = 0;
  for (int i = 0; i < 3; i++) {
    gasValue += analogRead(MQ135_PIN);
    delay(5);
  }
  gasValue = gasValue / 3;
  
  String message = "👃 *MQ-135 Gas Sensor*\n\n";
  message += "📊 Level: *" + String(gasValue) + "*\n\n";
  
  message += "💨 *Status:* ";
  
  if (gasValue < 2200) {
    message += "✅ *Fresh Air*\nNo odor detected";
  } 
  else if (gasValue < 3000) {
    message += "🟠 *Foul Smell!*\n⚠️ Check for spoiled food";
  } 
  else {
    message += "🔴 *Bad Smell!*\n🚨 Clean refrigerator!";
  }
  
  return message;
}

// Fridge Item Management Functions

void loadFridgeItems() {
  itemCount = preferences.getInt("itemCount", 0);
  
  for (int i = 0; i < itemCount; i++) {
    String key = "item" + String(i);
    String data = preferences.getString(key.c_str(), "");
    
    if (data != "") {
      int sep1 = data.indexOf('|');
      int sep2 = data.indexOf('|', sep1 + 1);
      
      fridgeItems[i].name = data.substring(0, sep1);
      fridgeItems[i].expiryDate = data.substring(sep1 + 1, sep2);
      
      String notifData = data.substring(sep2 + 1);
      fridgeItems[i].notified[0] = notifData.charAt(0) == '1';
      fridgeItems[i].notified[1] = notifData.charAt(1) == '1';
      fridgeItems[i].notified[2] = notifData.charAt(2) == '1';
    }
  }
  
  Serial.println("📦 Loaded " + String(itemCount) + " items from storage");
}

void saveFridgeItems() {
  preferences.putInt("itemCount", itemCount);
  
  for (int i = 0; i < itemCount; i++) {
    String key = "item" + String(i);
    String data = fridgeItems[i].name + "|" + fridgeItems[i].expiryDate + "|";
    data += String(fridgeItems[i].notified[0] ? '1' : '0');
    data += String(fridgeItems[i].notified[1] ? '1' : '0');
    data += String(fridgeItems[i].notified[2] ? '1' : '0');
    
    preferences.putString(key.c_str(), data);
  }
  
  Serial.println("💾 Saved " + String(itemCount) + " items to storage");
}

void addFridgeItem(String name, String expiryDate) {
  if (itemCount < 20) {
    fridgeItems[itemCount].name = name;
    fridgeItems[itemCount].expiryDate = expiryDate;
    fridgeItems[itemCount].notified[0] = false;
    fridgeItems[itemCount].notified[1] = false;
    fridgeItems[itemCount].notified[2] = false;
    itemCount++;
    saveFridgeItems();
  }
}

void deleteFridgeItem(int index) {
  for (int i = index; i < itemCount - 1; i++) {
    fridgeItems[i] = fridgeItems[i + 1];
  }
  itemCount--;
  saveFridgeItems();
}

String listFridgeItems() {
  if (itemCount == 0) {
    return "📦 *Fridge is empty!*\n\nUse /add to add items";
  }
  
  String list = "📦 *Fridge Items (" + String(itemCount) + ")*\n\n";
  
  for (int i = 0; i < itemCount; i++) {
    list += String(i + 1) + ". *" + fridgeItems[i].name + "*\n";
    list += "   📅 Expires: " + fridgeItems[i].expiryDate + "\n";
    
    int daysLeft = getDaysUntilExpiry(fridgeItems[i].expiryDate);
    if (daysLeft < 0) {
      list += "   ⚠️ EXPIRED!\n";
    } else if (daysLeft == 0) {
      list += "   🔴 Expires TODAY!\n";
    } else if (daysLeft <= 7) {
      list += "   🟡 " + String(daysLeft) + " days left\n";
    }
    list += "\n";
  }
  
  list += "Use /delete [number] to remove";
  
  return list;
}

String getExpiringItems() {
  String msg = "⏰ *Items Expiring Soon*\n\n";
  bool found = false;
  
  for (int i = 0; i < itemCount; i++) {
    int days = getDaysUntilExpiry(fridgeItems[i].expiryDate);
    
    if (days <= 7 && days >= 0) {
      found = true;
      msg += "• *" + fridgeItems[i].name + "*\n";
      msg += "  📅 " + fridgeItems[i].expiryDate + "\n";
      
      if (days == 0) {
        msg += "  🔴 Expires TODAY!\n";
      } else if (days == 1) {
        msg += "  🟠 Expires TOMORROW!\n";
      } else {
        msg += "  🟡 " + String(days) + " days left\n";
      }
      msg += "\n";
    }
  }
  
  if (!found) {
    msg += "✅ No items expiring this week!";
  }
  
  return msg;
}

int getExpiringItemsCount() {
  int count = 0;
  for (int i = 0; i < itemCount; i++) {
    int days = getDaysUntilExpiry(fridgeItems[i].expiryDate);
    if (days <= 7 && days >= 0) {
      count++;
    }
  }
  return count;
}

void checkExpiringItems() {
  if (!alertsEnabled || alertChatId == "") return;
  
  for (int i = 0; i < itemCount; i++) {
    int days = getDaysUntilExpiry(fridgeItems[i].expiryDate);
    
    // Notify for items expiring today
    if (days == 0 && !fridgeItems[i].notified[0]) {
      String msg = "🔴 *EXPIRES TODAY!*\n\n";
      msg += "📦 " + fridgeItems[i].name + "\n";
      msg += "📅 " + fridgeItems[i].expiryDate;
      bot.sendMessage(alertChatId, msg, "Markdown");
      fridgeItems[i].notified[0] = true;
      saveFridgeItems();
    }
    // Notify for items expiring tomorrow
    else if (days == 1 && !fridgeItems[i].notified[1]) {
      String msg = "🟠 *EXPIRES TOMORROW!*\n\n";
      msg += "📦 " + fridgeItems[i].name + "\n";
      msg += "📅 " + fridgeItems[i].expiryDate;
      bot.sendMessage(alertChatId, msg, "Markdown");
      fridgeItems[i].notified[1] = true;
      saveFridgeItems();
    }
    // Notify for items expiring in 2 days
    else if (days == 2 && !fridgeItems[i].notified[2]) {
      String msg = "🟡 *EXPIRES IN 2 DAYS!*\n\n";
      msg += "📦 " + fridgeItems[i].name + "\n";
      msg += "📅 " + fridgeItems[i].expiryDate;
      bot.sendMessage(alertChatId, msg, "Markdown");
      fridgeItems[i].notified[2] = true;
      saveFridgeItems();
    }
  }
}

int getDaysUntilExpiry(String expiryDate) {
  // Parse date DD-MM-YYYY
  int day = expiryDate.substring(0, 2).toInt();
  int month = expiryDate.substring(3, 5).toInt();
  int year = expiryDate.substring(6, 10).toInt();
  
  // Get current time
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  
  int currentDay = timeinfo.tm_mday;
  int currentMonth = timeinfo.tm_mon + 1;
  int currentYear = timeinfo.tm_year + 1900;
  
  // Calculate days difference (simplified)
  int currentDays = currentYear * 365 + currentMonth * 30 + currentDay;
  int expiryDays = year * 365 + month * 30 + day;
  
  return expiryDays - currentDays;
}

bool isValidDate(String date) {
  // Remove any extra spaces
  date.trim();
  
  Serial.println("🔍 Validating date: '" + date + "' Length: " + String(date.length()));
  
  if (date.length() != 10) {
    Serial.println("❌ Length is not 10");
    return false;
  }
  
  if (date.charAt(2) != '-' || date.charAt(5) != '-') {
    Serial.println("❌ Missing dashes at positions 2 and 5");
    return false;
  }
  
  int day = date.substring(0, 2).toInt();
  int month = date.substring(3, 5).toInt();
  int year = date.substring(6, 10).toInt();
  
  Serial.println("📅 Parsed: Day=" + String(day) + " Month=" + String(month) + " Year=" + String(year));
  
  if (day < 1 || day > 31) {
    Serial.println("❌ Invalid day");
    return false;
  }
  if (month < 1 || month > 12) {
    Serial.println("❌ Invalid month");
    return false;
  }
  if (year < 2024 || year > 2030) {
    Serial.println("❌ Invalid year (must be 2024-2030)");
    return false;
  }
  
  Serial.println("✅ Date is valid!");
  return true;
}
