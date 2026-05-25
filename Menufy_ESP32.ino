#include <WiFi.h>
#include <String>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

typedef std::vector<String> StringList;
AsyncWebServer server(80);
DNSServer dnsServer;

struct Session {
  String token;
  String account;
  IPAddress ip;
  uint32_t expire;
};

Session session[5];

String readFile(const String& path) {
  if (!LittleFS.exists(path)) return "";
  File file = LittleFS.open(path, "r");
  if (!file) return "";
  String content = file.readString();
  content.trim();
  file.close();
  return content;
}

void writeFile(const String& path, const String& value) {
  File file = LittleFS.open(path, "w");
  if (file) {
    file.print(value);
    file.close();
  }
}

bool readBool(const String& path) {
  String val = readFile(path);
  return (val == "true" || val == "1");
}

void writeBool(const String& path, bool value) {
  writeFile(path, value ? "true" : "false");
}

int readInt(const String& path) {
  return readFile(path).toInt();
}

void writeInt(const String& path, int value) {
  writeFile(path, String(value));
}

IPAddress readIP(const String& path) {
  IPAddress ip;
  ip.fromString(readFile(path));
  return ip;
}

void writeIP(const String& path, IPAddress ip) {
  writeFile(path, ip.toString());
}

StringList splitString(String input, char delimiter = '\n') {
  StringList result;
  int lastIndex = 0;
  int index;
  while ((index = input.indexOf(delimiter, lastIndex)) != -1) {
    String item = input.substring(lastIndex, index);
    item.trim();
    if (item.length() > 0) {
      result.push_back(item);
    }
    lastIndex = index + 1;
  }
  if (lastIndex < input.length()) {
    String lastItem = input.substring(lastIndex);
    lastItem.trim();
    if (lastItem.length() > 0) {
      result.push_back(lastItem);
    }
  }
  return result;
}

String generateToken() {
  const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  String token = "";
  srand(esp_random());
  for (int i = 0; i < 12; i++) {
    token += chars[rand() % strlen(chars)];
  }
  return token;
}

void saveToken(const String& token, String account, AsyncWebServerRequest *request) {
  IPAddress clientIP = request->client()->remoteIP();
  uint32_t now = millis();
  for (int i = 0; i < 5; i++) {
    if (session[i].token == "" || session[i].expire < now) {
      session[i].token = token;
      session[i].account = account;
      session[i].ip = clientIP;
      session[i].expire = now + (24UL * 60UL * 60UL * 1000UL);
      return;
    }
  }
  for (int i = 0; i < 5; i++) {
    if (session[i].ip == clientIP) {
      session[i].token = token;
      session[i].account = account;
      session[i].expire = now + (24UL * 60UL * 60UL * 1000UL);
      return;
    }
  }
  session[0].token = token;
  session[0].account = account;
  session[0].ip = clientIP;
  session[0].expire = now + (24UL * 60UL * 60UL * 1000UL);
}

String isTokenValid(const String& token, AsyncWebServerRequest *request) {
  IPAddress clientIP = request->client()->remoteIP();
  uint32_t now = millis();
  for (int i = 0; i < 5; i++) {
    if (session[i].token == token &&
        session[i].ip == clientIP &&
        session[i].expire > now) {
      return session[i].account;
    }
  }
  return "invalid";
}

void setup() {
  LittleFS.begin(true);
  
  String apName = readFile("/configs/ap.txt");
  if (apName == "") apName = "Menufy-ESP";
  int maxcon = readInt("/configs/maxcon.txt");
  if (maxcon == 0) maxcon = 4;
  
  WiFi.softAP(apName, "", 1, false, maxcon);
  
  WiFi.hostname("Menufy-ESP");
  WiFi.setAutoReconnect(true);
  
  String ssid = readFile("/configs/ssid.txt");
  String key = readFile("/configs/key.txt");
  bool dhcp = readBool("/configs/dhcp.txt");
  
  if (!dhcp) {
    IPAddress ip = readIP("/configs/ip.txt");
    IPAddress gateway = readIP("/configs/gateway.txt");
    IPAddress subnet = readIP("/configs/subnet.txt");
    IPAddress dns = readIP("/configs/dns.txt");
    WiFi.config(ip, gateway, subnet, dns);
  }
  
  if (ssid != "") {
    WiFi.begin(ssid, key);
  }
  
  server.onNotFound([](AsyncWebServerRequest * request) {
    request->redirect("/");
  });
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/language.htm", "text/html");
  });
  
  server.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
    String result = "{\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/fa/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/menu.htm", "text/html");
  });
  
  server.on("/fa/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/fa/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File unit = LittleFS.open("/fa/unit.txt", "r");
    result += "\"unit\":\"" + unit.readString() + "\",";
    unit.close();
    result += "\"categorys\":[";
    File dir = LittleFS.open("/fa/categorys");
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
          fileName = fileName.substring(lastSlash + 1);
        }
        if (fileName == "null.txt") {
          entry.close();
          continue;
        }
        String text = entry.readString();
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex != -1) {
          fileName = fileName.substring(0, dotIndex);
        }
        result += "{\"id\":\"" + fileName + "\",";
        result += "\"name\":\"" + splitString(text)[0] + "\",";
        result += "\"side\":\"" + splitString(text)[1] + "\",";
        result += "\"enable\":\"" + splitString(text)[2] + "\"},";
      }
      entry.close();
    }
    dir.close();
    if (result.endsWith(",")) {
      result.remove(result.length() - 1);
    }
    result += "],";
    result += "\"products\":[";
    File dir2 = LittleFS.open("/fa/products");
    while (true) {
      File entry = dir2.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
          fileName = fileName.substring(lastSlash + 1);
        }
        if (fileName == "null.txt") {
          entry.close();
          continue;
        }
        String text = entry.readString();
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex != -1) {
          fileName = fileName.substring(0, dotIndex);
        }
        result += "{\"id\":\"" + fileName + "\",";
        result += "\"name\":\"" + splitString(text)[0] + "\",";
        result += "\"category\":\"" + splitString(text)[1] + "\",";
        result += "\"price\":\"" + splitString(text)[2] + "\",";
        result += "\"discount\":\"" + splitString(text)[3] + "\",";
        result += "\"enable\":\"" + splitString(text)[4] + "\"},";
      }
      entry.close();
    }
    dir2.close();
    if (result.endsWith(",")) {
      result.remove(result.length() - 1);
    }
    result += "],";
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/fa/login/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/login.htm", "text/html");
  });
  
  server.on("/fa/login/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        request->send(200, "application/json", "{\"status\":\"admin\"}");
      }
      else if (isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        request->send(200, "application/json", "{\"status\":\"user\"}");
      }
      else {
        request->send(200, "application/json", "{\"status\":\"invalid\"}");
      }
    }
    else if (request->hasParam("username", true) && request->hasParam("password", true)) {
      String username = request->getParam("username", true)->value();
      String password = request->getParam("password", true)->value();
      String username1 = readFile("/configs/username1.txt");
      String password1 = readFile("/configs/password1.txt");
      String username2 = readFile("/configs/username2.txt");
      String password2 = readFile("/configs/password2.txt");
      
      if (username == "" || password == "") {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
      }
      else if (username == username1 && password == password1) {
        String token = generateToken();
        saveToken(token, "admin", request);
        request->send(200, "application/json", "{\"status\":\"ok\", \"token\":\"" + token + "\"}");
      }
      else if (username == username2 && password == password2) {
        String token = generateToken();
        saveToken(token, "user", request);
        request->send(200, "application/json", "{\"status\":\"ok\", \"token\":\"" + token + "\"}");
      }
      else {
        request->send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid credentials\"}");
      }
    }
    else {
      File name = LittleFS.open("/fa/name.txt", "r");
      String result = "{\"name\":\"" + name.readString() + "\",";
      name.close();
      result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
      result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
      result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
      result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
      result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
      request->send(200, "application/json; charset=utf-8", result);
    }
  });
  
  server.on("/fa/logout/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/logout.htm", "text/html");
  });
  
  server.on("/fa/logout/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        for (int i = 0; i < 5; i++) {
          if (request->getParam("token", true)->value() == session[i].token) {
            session[i].token = "";
            session[i].ip = IPAddress();
            session[i].expire = 0;
          }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      }
      else {
        request->send(200, "application/json", "{\"status\":\"invalid\"}");
      }
    }
    else {
      File name = LittleFS.open("/fa/name.txt", "r");
      String result = "{\"name\":\"" + name.readString() + "\",";
      name.close();
      result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
      result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
      result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
      result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
      result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
      request->send(200, "application/json; charset=utf-8", result);
    }
  });
  
  server.on("/fa/users/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/users.htm", "text/html");
  });
  
  server.on("/fa/users/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("phone", true)) {
            String phone = request->getParam("phone", true)->value();
            if (phone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/fa/users/" + phone + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("oldPhone", true) && request->hasParam("name", true)  && request->hasParam("phone", true)  && request->hasParam("address", true)) {
            String oldPhone = request->getParam("oldPhone", true)->value();
            if (oldPhone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String name = request->getParam("name", true)->value();
            String phone = request->getParam("phone", true)->value();
            String address = request->getParam("address", true)->value();
            if (name == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (phone == "") {
              request->send(200, "application/json", "{\"status\":\"phone\"}");
            }
            else {
              if (address == "")
                address = "-";
              if (oldPhone == phone) {
                String fileName = "/fa/users/" + phone + ".txt";
                File file = LittleFS.open(fileName, "w");
                file.println(name);
                file.println(address);
                file.close();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
              }
              else {
                String oldFileName = "/fa/users/" + oldPhone + ".txt";
                String fileName = "/fa/users/" + phone + ".txt";
                if (LittleFS.exists(fileName)) {
                  request->send(200, "application/json", "{\"status\":\"exists\"}");
                }
                else {
                  if (LittleFS.exists(oldFileName)) {
                    LittleFS.remove(oldFileName);
                  }
                  File file = LittleFS.open(fileName, "w");
                  file.println(name);
                  file.println(address);
                  file.close();
                  request->send(200, "application/json", "{\"status\":\"ok\"}");
                }
              }
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("phone", true)  && request->hasParam("address", true)) {
            String name = request->getParam("name", true)->value();
            String phone = request->getParam("phone", true)->value();
            if (phone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String address = request->getParam("address", true)->value();
            if (name == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (phone == "") {
              request->send(200, "application/json", "{\"status\":\"phone\"}");
            }
            else {
              if (address == "")
                address = "-";
              String fileName = "/fa/users/" + phone + ".txt";
              if (LittleFS.exists(fileName)) {
                request->send(200, "application/json", "{\"status\":\"exists\"}");
              }
              else {
                File file = LittleFS.open(fileName, "w");
                file.println(name);
                file.println(address);
                file.close();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
              }
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/fa/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"users\":[";
          File dir = LittleFS.open("/fa/users");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + String(index++) + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"phone\":\"" + fileName + "\",";
              result += "\"address\":\"" + splitString(text)[1] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/fa/orders/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/orders.htm", "text/html");
  });
  
  server.on("/fa/orders/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("order_id", true)) {
            String order_id = request->getParam("order_id", true)->value();
            if (order_id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/fa/orders/" + order_id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("orderId", true) && request->hasParam("customerName", true) && request->hasParam("customerPhone", true) && request->hasParam("totalPrice", true)) {
            String orderId = request->getParam("orderId", true)->value();
            if (orderId == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("customerName", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerName\"}");
            }
            else if (request->getParam("customerPhone", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
            }
            else if (request->getParam("totalPrice", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"totalPrice\"}");
            }
            else {
              String fileName = "/fa/orders/" + orderId + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("customerName", true)->value());
              file.println(request->getParam("customerPhone", true)->value());
              String address = "-";
              if (LittleFS.exists("/fa/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
                String fileName2 = "/fa/users/" + request->getParam("customerPhone", true)->value() + ".txt";
                File file2 = LittleFS.open(fileName2, "r");
                address = splitString(file2.readString())[1];
                file2.close();
              }
              file.println(address);
              for (int i = 1; request->hasParam("id" + String(i), true); i++) {
                if (request->getParam("id" + String(i), true)->value() != "")
                  file.println(request->getParam("id" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("name" + String(i), true)->value() != "")
                  file.println(request->getParam("name" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("quantity" + String(i), true)->value() != "")
                  file.println(request->getParam("quantity" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("price" + String(i), true)->value() != "")
                  file.println(request->getParam("price" + String(i), true)->value());
                else
                  file.println("-");
              }
              file.println(request->getParam("totalPrice", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"address\":\"" + address + "\"}");
            }
          }
          else if (action == "add" && request->hasParam("customerName", true) && request->hasParam("customerPhone", true) && request->hasParam("totalPrice", true)) {
            if (request->getParam("customerName", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerName\"}");
            }
            else if (request->getParam("customerPhone", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
            }
            else if (request->getParam("totalPrice", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"totalPrice\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/fa/orders/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/fa/orders/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("customerName", true)->value());
              file.println(request->getParam("customerPhone", true)->value());
              String address = "-";
              if (LittleFS.exists("/fa/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
                String fileName2 = "/fa/users/" + request->getParam("customerPhone", true)->value() + ".txt";
                File file2 = LittleFS.open(fileName2, "r");
                address = splitString(file2.readString())[1];
                file2.close();
              }
              file.println(address);
              for (int i = 1; request->hasParam("id" + String(i), true); i++) {
                if (request->getParam("id" + String(i), true)->value() != "")
                  file.println(request->getParam("id" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("name" + String(i), true)->value() != "")
                  file.println(request->getParam("name" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("quantity" + String(i), true)->value() != "")
                  file.println(request->getParam("quantity" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("price" + String(i), true)->value() != "")
                  file.println(request->getParam("price" + String(i), true)->value());
                else
                  file.println("-");
              }
              file.println(request->getParam("totalPrice", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\",\"address\":\"" + address + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/fa/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"orders\":[";
          File dir = LittleFS.open("/fa/orders");
          bool firstOrder = true;
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              std::vector<String> parts = splitString(text);
              int totalLines = parts.size();
              if (!firstOrder) {
                result += ",";
              }
              firstOrder = false;
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"customerId\":\"" + String(index++) + "\",";
              result += "\"customerName\":\"" + parts[0] + "\",";
              result += "\"customerPhone\":\"" + parts[1] + "\",";
              result += "\"customerAddress\":\"" + parts[2] + "\",";
              result += "\"products\":[";
              int productCount = (totalLines - 4) / 4;
              for (int i = 0; i < productCount; i++) {
                int baseIdx = 3 + (i * 4);
                if (baseIdx + 3 < totalLines) {
                  result += "{\"id\":\"" + parts[baseIdx] + "\",";
                  result += "\"name\":\"" + parts[baseIdx + 1] + "\",";
                  result += "\"quantity\":" + parts[baseIdx + 2] + ",";
                  result += "\"unit_price\":" + parts[baseIdx + 3] + "}";
                  if (i < productCount - 1) result += ",";
                }
              }
              result += "],\"totalPrice\":" + parts[totalLines - 1] + "}";
            }
            entry.close();
          }
          dir.close();
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
    else if (request->hasParam("action", true) && request->hasParam("customerName", true) && request->hasParam("customerPhone", true)) {
      if (request->getParam("action", true)->value() == "gest") {
        if (request->getParam("customerName", true)->value() == "") {
          request->send(200, "application/json", "{\"status\":\"customerName\"}");
        }
        else if (request->getParam("customerPhone", true)->value() == "") {
          request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
        }
        else {
          int number = 1;
          while (LittleFS.exists("/fa/orders/" + String(number) + ".txt")) {
            number++;
          }
          String fileName = "/fa/orders/" + String(number) + ".txt";
          File file = LittleFS.open(fileName, "w");
          file.println(request->getParam("customerName", true)->value());
          file.println(request->getParam("customerPhone", true)->value());
          String address = "-";
          if (LittleFS.exists("/fa/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
            String fileName2 = "/fa/users/" + request->getParam("customerPhone", true)->value() + ".txt";
            File file2 = LittleFS.open(fileName2, "r");
            address = splitString(file2.readString())[1];
            file2.close();
          }
          file.println(address);
          unsigned long sum = 0;
          for (int i = 1; request->hasParam("id" + String(i), true); i++) {
            if (LittleFS.exists("/fa/products/" + request->getParam("id" + String(i), true)->value() + ".txt")) {
              String fileName3 = "/fa/products/" + request->getParam("id" + String(i), true)->value() + ".txt";
              File file3 = LittleFS.open(fileName3, "r");
              String text = file3.readString();
              if (request->getParam("id" + String(i), true)->value() != "")
                file.println(request->getParam("id" + String(i), true)->value());
              else
                file.println("-");
              file.println(splitString(text)[0]);
              if (request->getParam("quantity" + String(i), true)->value() != "")
                file.println(request->getParam("quantity" + String(i), true)->value());
              else
                file.println("-");
              file.println(splitString(text)[2]);
              sum += splitString(text)[2].toInt() * request->getParam("quantity" + String(i), true)->value().toInt();
              file3.close();
            }
          }
          file.println(sum);
          file.close();
          request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
      }
    }
  });
  
  server.on("/fa/categorys/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/categorys.htm", "text/html");
  });
  
  server.on("/fa/categorys/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("id", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/fa/categorys/" + id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("id", true) && request->hasParam("name", true) && request->hasParam("position", true) && request->hasParam("status", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("position", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"position\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              String fileName = "/fa/categorys/" + id + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("position", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("position", true) && request->hasParam("status", true)) {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("position", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"position\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/fa/categorys/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/fa/categorys/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("position", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/fa/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"categorys\":[";
          File dir = LittleFS.open("/fa/categorys");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"position\":\"" + splitString(text)[1] + "\",";
              result += "\"status\":\"" + splitString(text)[2] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/fa/products/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/products.htm", "text/html");
  });
  
  server.on("/fa/products/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("id", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/fa/products/" + id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("id", true) && request->hasParam("name", true) && request->hasParam("categoryId", true) && request->hasParam("price", true) && request->hasParam("discount", true) && request->hasParam("status", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("categoryId", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"categoryId\"}");
            }
            else if (request->getParam("price", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"price\"}");
            }
            else if (request->getParam("discount", true)->value() == "") {
              request->send(200, "application/json", "{\"discount\":\"discount\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              String fileName = "/fa/products/" + id + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("categoryId", true)->value());
              file.println(request->getParam("price", true)->value());
              file.println(request->getParam("discount", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("categoryId", true) && request->hasParam("price", true) && request->hasParam("discount", true) && request->hasParam("status", true)) {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("categoryId", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"categoryId\"}");
            }
            else if (request->getParam("price", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"price\"}");
            }
            else if (request->getParam("discount", true)->value() == "") {
              request->send(200, "application/json", "{\"discount\":\"discount\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/fa/products/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/fa/products/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("categoryId", true)->value());
              file.println(request->getParam("price", true)->value());
              file.println(request->getParam("discount", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/fa/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"products\":[";
          File dir = LittleFS.open("/fa/products");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"categoryId\":\"" + splitString(text)[1] + "\",";
              if (LittleFS.exists("/fa/categorys/" + splitString(text)[1] + ".txt")) {
                File file2 = LittleFS.open("/fa/categorys/" + splitString(text)[1] + ".txt", "r");
                result += "\"categoryName\":\"" + splitString(file2.readString())[0] + "\",";
                file2.close();
              }
              else {
                result += "\"categoryName\":\"\",";
              }
              result += "\"price\":\"" + splitString(text)[2] + "\",";
              result += "\"discount\":\"" + splitString(text)[3] + "\",";
              result += "\"status\":\"" + splitString(text)[4] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/fa/settings/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/settings.htm", "text/html");
  });
  
  server.on("/fa/settings/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("unit", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"unit\"}");
            }
            else if (request->getParam("bgcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"bgcolor\"}");
            }
            else if (request->getParam("fgcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"fgcolor\"}");
            }
            else if (request->getParam("navcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"navcolor\"}");
            }
            else if (request->getParam("catcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"catcolor\"}");
            }
            else if (request->getParam("procolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"procolor\"}");
            }
            else {
              if (LittleFS.exists("/fa/name.txt")) {
                File file = LittleFS.open("/fa/name.txt", "w");
                file.print(request->getParam("name", true)->value());
                file.close();
              }
              if (LittleFS.exists("/fa/unit.txt")) {
                File file = LittleFS.open("/fa/unit.txt", "w");
                file.print(request->getParam("unit", true)->value());
                file.close();
              }
              writeFile("/configs/bgcolor.txt", request->getParam("bgcolor", true)->value());
              writeFile("/configs/fgcolor.txt", request->getParam("fgcolor", true)->value());
              writeFile("/configs/navcolor.txt", request->getParam("navcolor", true)->value());
              writeFile("/configs/catcolor.txt", request->getParam("catcolor", true)->value());
              writeFile("/configs/procolor.txt", request->getParam("procolor", true)->value());
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/fa/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          File unit = LittleFS.open("/fa/unit.txt", "r");
          result += "\"unit\":\"" + unit.readString() + "\",";
          unit.close();
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/fa/modules/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/modules.htm", "text/html");
  });
  
  server.on("/fa/modules/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            if (request->getParam("ap", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"ap\"}");
            }
            else if (request->getParam("maxcon", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"maxcon\"}");
            }
            else if (request->getParam("networkType", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"networkType\"}");
            }
            else if (request->getParam("ip", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"ip\"}");
            }
            else if (request->getParam("gateway", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"gateway\"}");
            }
            else if (request->getParam("subnet", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"subnet\"}");
            }
            else if (request->getParam("dns", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"dns\"}");
            }
            else {
              writeFile("/configs/ap.txt", request->getParam("ap", true)->value());
              writeInt("/configs/maxcon.txt", request->getParam("maxcon", true)->value().toInt());
              writeFile("/configs/ssid.txt", request->getParam("ssid", true)->value());
              writeFile("/configs/key.txt", request->getParam("key", true)->value());
              if (request->getParam("networkType", true)->value() == "dhcp")
                writeBool("/configs/dhcp.txt", true);
              else if (request->getParam("networkType", true)->value() == "manual")
                writeBool("/configs/dhcp.txt", false);
              writeIP("/configs/ip.txt", IPAddress());
              writeIP("/configs/gateway.txt", IPAddress());
              writeIP("/configs/subnet.txt", IPAddress());
              writeIP("/configs/dns.txt", IPAddress());
              
              IPAddress ip, gateway, subnet, dns;
              ip.fromString(request->getParam("ip", true)->value());
              gateway.fromString(request->getParam("gateway", true)->value());
              subnet.fromString(request->getParam("subnet", true)->value());
              dns.fromString(request->getParam("dns", true)->value());
              writeIP("/configs/ip.txt", ip);
              writeIP("/configs/gateway.txt", gateway);
              writeIP("/configs/subnet.txt", subnet);
              writeIP("/configs/dns.txt", dns);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/fa/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"ap\":\"" + readFile("/configs/ap.txt") + "\",";
          result += "\"maxcon\":\"" + String(readInt("/configs/maxcon.txt")) + "\",";
          result += "\"ssid\":\"" + readFile("/configs/ssid.txt") + "\",";
          result += "\"key\":\"" + readFile("/configs/key.txt") + "\",";
          if (readBool("/configs/dhcp.txt")) {
            result += "\"networkType\":\"dhcp\",";
            result += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
            result += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
            result += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
            result += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
          }
          else {
            result += "\"networkType\":\"manual\",";
            result += "\"ip\":\"" + readIP("/configs/ip.txt").toString() + "\",";
            result += "\"gateway\":\"" + readIP("/configs/gateway.txt").toString() + "\",";
            result += "\"subnet\":\"" + readIP("/configs/subnet.txt").toString() + "\",";
            result += "\"dns\":\"" + readIP("/configs/dns.txt").toString() + "\",";
          }
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/fa/account/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/account.htm", "text/html");
  });
  
  server.on("/fa/account/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            String username1 = readFile("/configs/username1.txt");
            String username2 = readFile("/configs/username2.txt");
            String password1 = readFile("/configs/password1.txt");
            String password2 = readFile("/configs/password2.txt");
            
            if (request->getParam("username", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"username\"}");
            }
            else if (request->getParam("username", true)->value() != username1 && request->getParam("username", true)->value() != username2) {
              request->send(200, "application/json", "{\"status\":\"wrongUsername\"}");
            }
            else if (request->getParam("currentPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"currentPassword\"}");
            }
            else if (request->getParam("newPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"newPassword\"}");
            }
            else if (request->getParam("confirmPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"confirmPassword\"}");
            }
            else if (request->getParam("currentPassword", true)->value() != password1 && request->getParam("currentPassword", true)->value() != password2) {
              request->send(200, "application/json", "{\"status\":\"wrongPassword\"}");
            }
            else if (request->getParam("newPassword", true)->value() != request->getParam("confirmPassword", true)->value()) {
              request->send(200, "application/json", "{\"status\":\"noMatch\"}");
            }
            else if (request->getParam("newPassword", true)->value().length() < 8) {
              request->send(200, "application/json", "{\"status\":\"shortPassword\"}");
            }
            else {
              if (username1 == request->getParam("username", true)->value())
                writeFile("/configs/password1.txt", request->getParam("newPassword", true)->value());
              else if (username2 == request->getParam("username", true)->value())
                writeFile("/configs/password2.txt", request->getParam("newPassword", true)->value());
              for (int i = 0; i < 5; i++) {
                session[i].token = "";
                session[i].ip = IPAddress();
                session[i].expire = 0;
              }
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/fa/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/fa/about/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/about.htm", "text/html");
  });
  
  server.on("/fa/about/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/fa/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File info = LittleFS.open("/fa/info.txt", "r");
    result += "\"info\":\"" + info.readString() + "\",";
    info.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/fa/contact/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/contact.htm", "text/html");
  });
  
  server.on("/fa/contact/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/fa/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File address = LittleFS.open("/fa/address.txt", "r");
    result += "\"address\":\"" + address.readString() + "\",";
    address.close();
    File phone = LittleFS.open("/fa/phone.txt", "r");
    result += "\"phone\":\"" + phone.readString() + "\",";
    phone.close();
    File email = LittleFS.open("/fa/email.txt", "r");
    result += "\"email\":\"" + email.readString() + "\",";
    email.close();
    File instagram = LittleFS.open("/fa/instagram.txt", "r");
    result += "\"instagram\":\"" + instagram.readString() + "\",";
    instagram.close();
    File telegram = LittleFS.open("/fa/telegram.txt", "r");
    result += "\"telegram\":\"" + telegram.readString() + "\",";
    telegram.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/fa/home/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/fa/home.htm", "text/html");
  });
  
  server.on("/fa/home/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/fa/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/en/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/menu.htm", "text/html");
  });
  
  server.on("/en/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/en/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File unit = LittleFS.open("/en/unit.txt", "r");
    result += "\"unit\":\"" + unit.readString() + "\",";
    unit.close();
    result += "\"categorys\":[";
    File dir = LittleFS.open("/en/categorys");
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
          fileName = fileName.substring(lastSlash + 1);
        }
        if (fileName == "null.txt") {
          entry.close();
          continue;
        }
        String text = entry.readString();
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex != -1) {
          fileName = fileName.substring(0, dotIndex);
        }
        result += "{\"id\":\"" + fileName + "\",";
        result += "\"name\":\"" + splitString(text)[0] + "\",";
        result += "\"side\":\"" + splitString(text)[1] + "\",";
        result += "\"enable\":\"" + splitString(text)[2] + "\"},";
      }
      entry.close();
    }
    dir.close();
    if (result.endsWith(",")) {
      result.remove(result.length() - 1);
    }
    result += "],";
    result += "\"products\":[";
    File dir2 = LittleFS.open("/en/products");
    while (true) {
      File entry = dir2.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
          fileName = fileName.substring(lastSlash + 1);
        }
        if (fileName == "null.txt") {
          entry.close();
          continue;
        }
        String text = entry.readString();
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex != -1) {
          fileName = fileName.substring(0, dotIndex);
        }
        result += "{\"id\":\"" + fileName + "\",";
        result += "\"name\":\"" + splitString(text)[0] + "\",";
        result += "\"category\":\"" + splitString(text)[1] + "\",";
        result += "\"price\":\"" + splitString(text)[2] + "\",";
        result += "\"discount\":\"" + splitString(text)[3] + "\",";
        result += "\"enable\":\"" + splitString(text)[4] + "\"},";
      }
      entry.close();
    }
    dir2.close();
    if (result.endsWith(",")) {
      result.remove(result.length() - 1);
    }
    result += "],";
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/en/login/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/login.htm", "text/html");
  });
  
  server.on("/en/login/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        request->send(200, "application/json", "{\"status\":\"admin\"}");
      }
      else if (isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        request->send(200, "application/json", "{\"status\":\"user\"}");
      }
      else {
        request->send(200, "application/json", "{\"status\":\"invalid\"}");
      }
    }
    else if (request->hasParam("username", true) && request->hasParam("password", true)) {
      String username = request->getParam("username", true)->value();
      String password = request->getParam("password", true)->value();
      String username1 = readFile("/configs/username1.txt");
      String password1 = readFile("/configs/password1.txt");
      String username2 = readFile("/configs/username2.txt");
      String password2 = readFile("/configs/password2.txt");
      
      if (username == "" || password == "") {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
      }
      else if (username == username1 && password == password1) {
        String token = generateToken();
        saveToken(token, "admin", request);
        request->send(200, "application/json", "{\"status\":\"ok\", \"token\":\"" + token + "\"}");
      }
      else if (username == username2 && password == password2) {
        String token = generateToken();
        saveToken(token, "user", request);
        request->send(200, "application/json", "{\"status\":\"ok\", \"token\":\"" + token + "\"}");
      }
      else {
        request->send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid credentials\"}");
      }
    }
    else {
      File name = LittleFS.open("/en/name.txt", "r");
      String result = "{\"name\":\"" + name.readString() + "\",";
      name.close();
      result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
      result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
      result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
      result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
      result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
      request->send(200, "application/json; charset=utf-8", result);
    }
  });
  
  server.on("/en/logout/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/logout.htm", "text/html");
  });
  
  server.on("/en/logout/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        for (int i = 0; i < 5; i++) {
          if (request->getParam("token", true)->value() == session[i].token) {
            session[i].token = "";
            session[i].ip = IPAddress();
            session[i].expire = 0;
          }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      }
      else {
        request->send(200, "application/json", "{\"status\":\"invalid\"}");
      }
    }
    else {
      File name = LittleFS.open("/en/name.txt", "r");
      String result = "{\"name\":\"" + name.readString() + "\",";
      name.close();
      result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
      result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
      result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
      result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
      result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
      request->send(200, "application/json; charset=utf-8", result);
    }
  });
  
  server.on("/en/users/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/users.htm", "text/html");
  });
  
  server.on("/en/users/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("phone", true)) {
            String phone = request->getParam("phone", true)->value();
            if (phone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/en/users/" + phone + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("oldPhone", true) && request->hasParam("name", true)  && request->hasParam("phone", true)  && request->hasParam("address", true)) {
            String oldPhone = request->getParam("oldPhone", true)->value();
            if (oldPhone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String name = request->getParam("name", true)->value();
            String phone = request->getParam("phone", true)->value();
            String address = request->getParam("address", true)->value();
            if (name == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (phone == "") {
              request->send(200, "application/json", "{\"status\":\"phone\"}");
            }
            else {
              if (address == "")
                address = "-";
              if (oldPhone == phone) {
                String fileName = "/en/users/" + phone + ".txt";
                File file = LittleFS.open(fileName, "w");
                file.println(name);
                file.println(address);
                file.close();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
              }
              else {
                String oldFileName = "/en/users/" + oldPhone + ".txt";
                String fileName = "/en/users/" + phone + ".txt";
                if (LittleFS.exists(fileName)) {
                  request->send(200, "application/json", "{\"status\":\"exists\"}");
                }
                else {
                  if (LittleFS.exists(oldFileName)) {
                    LittleFS.remove(oldFileName);
                  }
                  File file = LittleFS.open(fileName, "w");
                  file.println(name);
                  file.println(address);
                  file.close();
                  request->send(200, "application/json", "{\"status\":\"ok\"}");
                }
              }
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("phone", true)  && request->hasParam("address", true)) {
            String name = request->getParam("name", true)->value();
            String phone = request->getParam("phone", true)->value();
            if (phone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String address = request->getParam("address", true)->value();
            if (name == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (phone == "") {
              request->send(200, "application/json", "{\"status\":\"phone\"}");
            }
            else {
              if (address == "")
                address = "-";
              String fileName = "/en/users/" + phone + ".txt";
              if (LittleFS.exists(fileName)) {
                request->send(200, "application/json", "{\"status\":\"exists\"}");
              }
              else {
                File file = LittleFS.open(fileName, "w");
                file.println(name);
                file.println(address);
                file.close();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
              }
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/en/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"users\":[";
          File dir = LittleFS.open("/en/users");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + String(index++) + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"phone\":\"" + fileName + "\",";
              result += "\"address\":\"" + splitString(text)[1] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/en/orders/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/orders.htm", "text/html");
  });
  
  server.on("/en/orders/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("order_id", true)) {
            String order_id = request->getParam("order_id", true)->value();
            if (order_id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/en/orders/" + order_id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("orderId", true) && request->hasParam("customerName", true) && request->hasParam("customerPhone", true) && request->hasParam("totalPrice", true)) {
            String orderId = request->getParam("orderId", true)->value();
            if (orderId == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("customerName", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerName\"}");
            }
            else if (request->getParam("customerPhone", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
            }
            else if (request->getParam("totalPrice", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"totalPrice\"}");
            }
            else {
              String fileName = "/en/orders/" + orderId + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("customerName", true)->value());
              file.println(request->getParam("customerPhone", true)->value());
              String address = "-";
              if (LittleFS.exists("/en/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
                String fileName2 = "/en/users/" + request->getParam("customerPhone", true)->value() + ".txt";
                File file2 = LittleFS.open(fileName2, "r");
                address = splitString(file2.readString())[1];
                file2.close();
              }
              file.println(address);
              for (int i = 1; request->hasParam("id" + String(i), true); i++) {
                if (request->getParam("id" + String(i), true)->value() != "")
                  file.println(request->getParam("id" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("name" + String(i), true)->value() != "")
                  file.println(request->getParam("name" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("quantity" + String(i), true)->value() != "")
                  file.println(request->getParam("quantity" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("price" + String(i), true)->value() != "")
                  file.println(request->getParam("price" + String(i), true)->value());
                else
                  file.println("-");
              }
              file.println(request->getParam("totalPrice", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"address\":\"" + address + "\"}");
            }
          }
          else if (action == "add" && request->hasParam("customerName", true) && request->hasParam("customerPhone", true) && request->hasParam("totalPrice", true)) {
            if (request->getParam("customerName", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerName\"}");
            }
            else if (request->getParam("customerPhone", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
            }
            else if (request->getParam("totalPrice", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"totalPrice\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/en/orders/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/en/orders/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("customerName", true)->value());
              file.println(request->getParam("customerPhone", true)->value());
              String address = "-";
              if (LittleFS.exists("/en/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
                String fileName2 = "/en/users/" + request->getParam("customerPhone", true)->value() + ".txt";
                File file2 = LittleFS.open(fileName2, "r");
                address = splitString(file2.readString())[1];
                file2.close();
              }
              file.println(address);
              for (int i = 1; request->hasParam("id" + String(i), true); i++) {
                if (request->getParam("id" + String(i), true)->value() != "")
                  file.println(request->getParam("id" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("name" + String(i), true)->value() != "")
                  file.println(request->getParam("name" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("quantity" + String(i), true)->value() != "")
                  file.println(request->getParam("quantity" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("price" + String(i), true)->value() != "")
                  file.println(request->getParam("price" + String(i), true)->value());
                else
                  file.println("-");
              }
              file.println(request->getParam("totalPrice", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\",\"address\":\"" + address + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/en/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"orders\":[";
          File dir = LittleFS.open("/en/orders");
          bool firstOrder = true;
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              std::vector<String> parts = splitString(text);
              int totalLines = parts.size();
              if (!firstOrder) {
                result += ",";
              }
              firstOrder = false;
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"customerId\":\"" + String(index++) + "\",";
              result += "\"customerName\":\"" + parts[0] + "\",";
              result += "\"customerPhone\":\"" + parts[1] + "\",";
              result += "\"customerAddress\":\"" + parts[2] + "\",";
              result += "\"products\":[";
              int productCount = (totalLines - 4) / 4;
              for (int i = 0; i < productCount; i++) {
                int baseIdx = 3 + (i * 4);
                if (baseIdx + 3 < totalLines) {
                  result += "{\"id\":\"" + parts[baseIdx] + "\",";
                  result += "\"name\":\"" + parts[baseIdx + 1] + "\",";
                  result += "\"quantity\":" + parts[baseIdx + 2] + ",";
                  result += "\"unit_price\":" + parts[baseIdx + 3] + "}";
                  if (i < productCount - 1) result += ",";
                }
              }
              result += "],\"totalPrice\":" + parts[totalLines - 1] + "}";
            }
            entry.close();
          }
          dir.close();
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
    else if (request->hasParam("action", true) && request->hasParam("customerName", true) && request->hasParam("customerPhone", true)) {
      if (request->getParam("action", true)->value() == "gest") {
        if (request->getParam("customerName", true)->value() == "") {
          request->send(200, "application/json", "{\"status\":\"customerName\"}");
        }
        else if (request->getParam("customerPhone", true)->value() == "") {
          request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
        }
        else {
          int number = 1;
          while (LittleFS.exists("/en/orders/" + String(number) + ".txt")) {
            number++;
          }
          String fileName = "/en/orders/" + String(number) + ".txt";
          File file = LittleFS.open(fileName, "w");
          file.println(request->getParam("customerName", true)->value());
          file.println(request->getParam("customerPhone", true)->value());
          String address = "-";
          if (LittleFS.exists("/en/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
            String fileName2 = "/en/users/" + request->getParam("customerPhone", true)->value() + ".txt";
            File file2 = LittleFS.open(fileName2, "r");
            address = splitString(file2.readString())[1];
            file2.close();
          }
          file.println(address);
          unsigned long sum = 0;
          for (int i = 1; request->hasParam("id" + String(i), true); i++) {
            if (LittleFS.exists("/en/products/" + request->getParam("id" + String(i), true)->value() + ".txt")) {
              String fileName3 = "/en/products/" + request->getParam("id" + String(i), true)->value() + ".txt";
              File file3 = LittleFS.open(fileName3, "r");
              String text = file3.readString();
              if (request->getParam("id" + String(i), true)->value() != "")
                file.println(request->getParam("id" + String(i), true)->value());
              else
                file.println("-");
              file.println(splitString(text)[0]);
              if (request->getParam("quantity" + String(i), true)->value() != "")
                file.println(request->getParam("quantity" + String(i), true)->value());
              else
                file.println("-");
              file.println(splitString(text)[2]);
              sum += splitString(text)[2].toInt() * request->getParam("quantity" + String(i), true)->value().toInt();
              file3.close();
            }
          }
          file.println(sum);
          file.close();
          request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
      }
    }
  });
  
  server.on("/en/categorys/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/categorys.htm", "text/html");
  });
  
  server.on("/en/categorys/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("id", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/en/categorys/" + id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("id", true) && request->hasParam("name", true) && request->hasParam("position", true) && request->hasParam("status", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("position", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"position\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              String fileName = "/en/categorys/" + id + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("position", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("position", true) && request->hasParam("status", true)) {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("position", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"position\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/en/categorys/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/en/categorys/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("position", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/en/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"categorys\":[";
          File dir = LittleFS.open("/en/categorys");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"position\":\"" + splitString(text)[1] + "\",";
              result += "\"status\":\"" + splitString(text)[2] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/en/products/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/products.htm", "text/html");
  });
  
  server.on("/en/products/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("id", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/en/products/" + id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("id", true) && request->hasParam("name", true) && request->hasParam("categoryId", true) && request->hasParam("price", true) && request->hasParam("discount", true) && request->hasParam("status", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("categoryId", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"categoryId\"}");
            }
            else if (request->getParam("price", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"price\"}");
            }
            else if (request->getParam("discount", true)->value() == "") {
              request->send(200, "application/json", "{\"discount\":\"discount\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              String fileName = "/en/products/" + id + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("categoryId", true)->value());
              file.println(request->getParam("price", true)->value());
              file.println(request->getParam("discount", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("categoryId", true) && request->hasParam("price", true) && request->hasParam("discount", true) && request->hasParam("status", true)) {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("categoryId", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"categoryId\"}");
            }
            else if (request->getParam("price", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"price\"}");
            }
            else if (request->getParam("discount", true)->value() == "") {
              request->send(200, "application/json", "{\"discount\":\"discount\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/en/products/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/en/products/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("categoryId", true)->value());
              file.println(request->getParam("price", true)->value());
              file.println(request->getParam("discount", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/en/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"products\":[";
          File dir = LittleFS.open("/en/products");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"categoryId\":\"" + splitString(text)[1] + "\",";
              if (LittleFS.exists("/en/categorys/" + splitString(text)[1] + ".txt")) {
                File file2 = LittleFS.open("/en/categorys/" + splitString(text)[1] + ".txt", "r");
                result += "\"categoryName\":\"" + splitString(file2.readString())[0] + "\",";
                file2.close();
              }
              else {
                result += "\"categoryName\":\"\",";
              }
              result += "\"price\":\"" + splitString(text)[2] + "\",";
              result += "\"discount\":\"" + splitString(text)[3] + "\",";
              result += "\"status\":\"" + splitString(text)[4] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/en/settings/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/settings.htm", "text/html");
  });
  
  server.on("/en/settings/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("unit", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"unit\"}");
            }
            else if (request->getParam("bgcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"bgcolor\"}");
            }
            else if (request->getParam("fgcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"fgcolor\"}");
            }
            else if (request->getParam("navcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"navcolor\"}");
            }
            else if (request->getParam("catcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"catcolor\"}");
            }
            else if (request->getParam("procolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"procolor\"}");
            }
            else {
              if (LittleFS.exists("/en/name.txt")) {
                File file = LittleFS.open("/en/name.txt", "w");
                file.print(request->getParam("name", true)->value());
                file.close();
              }
              if (LittleFS.exists("/en/unit.txt")) {
                File file = LittleFS.open("/en/unit.txt", "w");
                file.print(request->getParam("unit", true)->value());
                file.close();
              }
              writeFile("/configs/bgcolor.txt", request->getParam("bgcolor", true)->value());
              writeFile("/configs/fgcolor.txt", request->getParam("fgcolor", true)->value());
              writeFile("/configs/navcolor.txt", request->getParam("navcolor", true)->value());
              writeFile("/configs/catcolor.txt", request->getParam("catcolor", true)->value());
              writeFile("/configs/procolor.txt", request->getParam("procolor", true)->value());
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/en/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          File unit = LittleFS.open("/en/unit.txt", "r");
          result += "\"unit\":\"" + unit.readString() + "\",";
          unit.close();
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/en/modules/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/modules.htm", "text/html");
  });
  
  server.on("/en/modules/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            if (request->getParam("ap", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"ap\"}");
            }
            else if (request->getParam("maxcon", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"maxcon\"}");
            }
            else if (request->getParam("networkType", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"networkType\"}");
            }
            else if (request->getParam("ip", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"ip\"}");
            }
            else if (request->getParam("gateway", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"gateway\"}");
            }
            else if (request->getParam("subnet", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"subnet\"}");
            }
            else if (request->getParam("dns", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"dns\"}");
            }
            else {
              writeFile("/configs/ap.txt", request->getParam("ap", true)->value());
              writeInt("/configs/maxcon.txt", request->getParam("maxcon", true)->value().toInt());
              writeFile("/configs/ssid.txt", request->getParam("ssid", true)->value());
              writeFile("/configs/key.txt", request->getParam("key", true)->value());
              if (request->getParam("networkType", true)->value() == "dhcp")
                writeBool("/configs/dhcp.txt", true);
              else if (request->getParam("networkType", true)->value() == "manual")
                writeBool("/configs/dhcp.txt", false);
              
              IPAddress ip, gateway, subnet, dns;
              ip.fromString(request->getParam("ip", true)->value());
              gateway.fromString(request->getParam("gateway", true)->value());
              subnet.fromString(request->getParam("subnet", true)->value());
              dns.fromString(request->getParam("dns", true)->value());
              writeIP("/configs/ip.txt", ip);
              writeIP("/configs/gateway.txt", gateway);
              writeIP("/configs/subnet.txt", subnet);
              writeIP("/configs/dns.txt", dns);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/en/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"ap\":\"" + readFile("/configs/ap.txt") + "\",";
          result += "\"maxcon\":\"" + String(readInt("/configs/maxcon.txt")) + "\",";
          result += "\"ssid\":\"" + readFile("/configs/ssid.txt") + "\",";
          result += "\"key\":\"" + readFile("/configs/key.txt") + "\",";
          if (readBool("/configs/dhcp.txt")) {
            result += "\"networkType\":\"dhcp\",";
            result += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
            result += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
            result += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
            result += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
          }
          else {
            result += "\"networkType\":\"manual\",";
            result += "\"ip\":\"" + readIP("/configs/ip.txt").toString() + "\",";
            result += "\"gateway\":\"" + readIP("/configs/gateway.txt").toString() + "\",";
            result += "\"subnet\":\"" + readIP("/configs/subnet.txt").toString() + "\",";
            result += "\"dns\":\"" + readIP("/configs/dns.txt").toString() + "\",";
          }
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/en/account/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/account.htm", "text/html");
  });
  
  server.on("/en/account/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            String username1 = readFile("/configs/username1.txt");
            String username2 = readFile("/configs/username2.txt");
            String password1 = readFile("/configs/password1.txt");
            String password2 = readFile("/configs/password2.txt");
            
            if (request->getParam("username", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"username\"}");
            }
            else if (request->getParam("username", true)->value() != username1 && request->getParam("username", true)->value() != username2) {
              request->send(200, "application/json", "{\"status\":\"wrongUsername\"}");
            }
            else if (request->getParam("currentPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"currentPassword\"}");
            }
            else if (request->getParam("newPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"newPassword\"}");
            }
            else if (request->getParam("confirmPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"confirmPassword\"}");
            }
            else if (request->getParam("currentPassword", true)->value() != password1 && request->getParam("currentPassword", true)->value() != password2) {
              request->send(200, "application/json", "{\"status\":\"wrongPassword\"}");
            }
            else if (request->getParam("newPassword", true)->value() != request->getParam("confirmPassword", true)->value()) {
              request->send(200, "application/json", "{\"status\":\"noMatch\"}");
            }
            else if (request->getParam("newPassword", true)->value().length() < 8) {
              request->send(200, "application/json", "{\"status\":\"shortPassword\"}");
            }
            else {
              if (username1 == request->getParam("username", true)->value())
                writeFile("/configs/password1.txt", request->getParam("newPassword", true)->value());
              else if (username2 == request->getParam("username", true)->value())
                writeFile("/configs/password2.txt", request->getParam("newPassword", true)->value());
              for (int i = 0; i < 5; i++) {
                session[i].token = "";
                session[i].ip = IPAddress();
                session[i].expire = 0;
              }
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/en/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/en/about/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/about.htm", "text/html");
  });
  
  server.on("/en/about/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/en/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File info = LittleFS.open("/en/info.txt", "r");
    result += "\"info\":\"" + info.readString() + "\",";
    info.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/en/contact/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/contact.htm", "text/html");
  });
  
  server.on("/en/contact/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/en/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File address = LittleFS.open("/en/address.txt", "r");
    result += "\"address\":\"" + address.readString() + "\",";
    address.close();
    File phone = LittleFS.open("/en/phone.txt", "r");
    result += "\"phone\":\"" + phone.readString() + "\",";
    phone.close();
    File email = LittleFS.open("/en/email.txt", "r");
    result += "\"email\":\"" + email.readString() + "\",";
    email.close();
    File instagram = LittleFS.open("/en/instagram.txt", "r");
    result += "\"instagram\":\"" + instagram.readString() + "\",";
    instagram.close();
    File telegram = LittleFS.open("/en/telegram.txt", "r");
    result += "\"telegram\":\"" + telegram.readString() + "\",";
    telegram.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/en/home/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/en/home.htm", "text/html");
  });
  
  server.on("/en/home/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/en/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/ar/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/menu.htm", "text/html");
  });
  
  server.on("/ar/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/ar/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File unit = LittleFS.open("/ar/unit.txt", "r");
    result += "\"unit\":\"" + unit.readString() + "\",";
    unit.close();
    result += "\"categorys\":[";
    File dir = LittleFS.open("/ar/categorys");
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
          fileName = fileName.substring(lastSlash + 1);
        }
        if (fileName == "null.txt") {
          entry.close();
          continue;
        }
        String text = entry.readString();
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex != -1) {
          fileName = fileName.substring(0, dotIndex);
        }
        result += "{\"id\":\"" + fileName + "\",";
        result += "\"name\":\"" + splitString(text)[0] + "\",";
        result += "\"side\":\"" + splitString(text)[1] + "\",";
        result += "\"enable\":\"" + splitString(text)[2] + "\"},";
      }
      entry.close();
    }
    dir.close();
    if (result.endsWith(",")) {
      result.remove(result.length() - 1);
    }
    result += "],";
    result += "\"products\":[";
    File dir2 = LittleFS.open("/ar/products");
    while (true) {
      File entry = dir2.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String fileName = entry.name();
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash != -1) {
          fileName = fileName.substring(lastSlash + 1);
        }
        if (fileName == "null.txt") {
          entry.close();
          continue;
        }
        String text = entry.readString();
        int dotIndex = fileName.lastIndexOf('.');
        if (dotIndex != -1) {
          fileName = fileName.substring(0, dotIndex);
        }
        result += "{\"id\":\"" + fileName + "\",";
        result += "\"name\":\"" + splitString(text)[0] + "\",";
        result += "\"category\":\"" + splitString(text)[1] + "\",";
        result += "\"price\":\"" + splitString(text)[2] + "\",";
        result += "\"discount\":\"" + splitString(text)[3] + "\",";
        result += "\"enable\":\"" + splitString(text)[4] + "\"},";
      }
      entry.close();
    }
    dir2.close();
    if (result.endsWith(",")) {
      result.remove(result.length() - 1);
    }
    result += "],";
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/ar/login/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/login.htm", "text/html");
  });
  
  server.on("/ar/login/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        request->send(200, "application/json", "{\"status\":\"admin\"}");
      }
      else if (isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        request->send(200, "application/json", "{\"status\":\"user\"}");
      }
      else {
        request->send(200, "application/json", "{\"status\":\"invalid\"}");
      }
    }
    else if (request->hasParam("username", true) && request->hasParam("password", true)) {
      String username = request->getParam("username", true)->value();
      String password = request->getParam("password", true)->value();
      String username1 = readFile("/configs/username1.txt");
      String password1 = readFile("/configs/password1.txt");
      String username2 = readFile("/configs/username2.txt");
      String password2 = readFile("/configs/password2.txt");
      
      if (username == "" || password == "") {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing parameters\"}");
      }
      else if (username == username1 && password == password1) {
        String token = generateToken();
        saveToken(token, "admin", request);
        request->send(200, "application/json", "{\"status\":\"ok\", \"token\":\"" + token + "\"}");
      }
      else if (username == username2 && password == password2) {
        String token = generateToken();
        saveToken(token, "user", request);
        request->send(200, "application/json", "{\"status\":\"ok\", \"token\":\"" + token + "\"}");
      }
      else {
        request->send(401, "application/json", "{\"status\":\"error\",\"message\":\"Invalid credentials\"}");
      }
    }
    else {
      File name = LittleFS.open("/ar/name.txt", "r");
      String result = "{\"name\":\"" + name.readString() + "\",";
      name.close();
      result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
      result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
      result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
      result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
      result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
      request->send(200, "application/json; charset=utf-8", result);
    }
  });
  
  server.on("/ar/logout/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/logout.htm", "text/html");
  });
  
  server.on("/ar/logout/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        for (int i = 0; i < 5; i++) {
          if (request->getParam("token", true)->value() == session[i].token) {
            session[i].token = "";
            session[i].ip = IPAddress();
            session[i].expire = 0;
          }
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      }
      else {
        request->send(200, "application/json", "{\"status\":\"invalid\"}");
      }
    }
    else {
      File name = LittleFS.open("/ar/name.txt", "r");
      String result = "{\"name\":\"" + name.readString() + "\",";
      name.close();
      result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
      result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
      result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
      result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
      result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
      request->send(200, "application/json; charset=utf-8", result);
    }
  });
  
  server.on("/ar/users/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/users.htm", "text/html");
  });
  
  server.on("/ar/users/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("phone", true)) {
            String phone = request->getParam("phone", true)->value();
            if (phone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/ar/users/" + phone + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("oldPhone", true) && request->hasParam("name", true)  && request->hasParam("phone", true)  && request->hasParam("address", true)) {
            String oldPhone = request->getParam("oldPhone", true)->value();
            if (oldPhone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String name = request->getParam("name", true)->value();
            String phone = request->getParam("phone", true)->value();
            String address = request->getParam("address", true)->value();
            if (name == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (phone == "") {
              request->send(200, "application/json", "{\"status\":\"phone\"}");
            }
            else {
              if (address == "")
                address = "-";
              if (oldPhone == phone) {
                String fileName = "/ar/users/" + phone + ".txt";
                File file = LittleFS.open(fileName, "w");
                file.println(name);
                file.println(address);
                file.close();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
              }
              else {
                String oldFileName = "/ar/users/" + oldPhone + ".txt";
                String fileName = "/ar/users/" + phone + ".txt";
                if (LittleFS.exists(fileName)) {
                  request->send(200, "application/json", "{\"status\":\"exists\"}");
                }
                else {
                  if (LittleFS.exists(oldFileName)) {
                    LittleFS.remove(oldFileName);
                  }
                  File file = LittleFS.open(fileName, "w");
                  file.println(name);
                  file.println(address);
                  file.close();
                  request->send(200, "application/json", "{\"status\":\"ok\"}");
                }
              }
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("phone", true)  && request->hasParam("address", true)) {
            String name = request->getParam("name", true)->value();
            String phone = request->getParam("phone", true)->value();
            if (phone == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String address = request->getParam("address", true)->value();
            if (name == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (phone == "") {
              request->send(200, "application/json", "{\"status\":\"phone\"}");
            }
            else {
              if (address == "")
                address = "-";
              String fileName = "/ar/users/" + phone + ".txt";
              if (LittleFS.exists(fileName)) {
                request->send(200, "application/json", "{\"status\":\"exists\"}");
              }
              else {
                File file = LittleFS.open(fileName, "w");
                file.println(name);
                file.println(address);
                file.close();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
              }
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/ar/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"users\":[";
          File dir = LittleFS.open("/ar/users");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + String(index++) + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"phone\":\"" + fileName + "\",";
              result += "\"address\":\"" + splitString(text)[1] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/ar/orders/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/orders.htm", "text/html");
  });
  
  server.on("/ar/orders/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("order_id", true)) {
            String order_id = request->getParam("order_id", true)->value();
            if (order_id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/ar/orders/" + order_id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("orderId", true) && request->hasParam("customerName", true) && request->hasParam("customerPhone", true) && request->hasParam("totalPrice", true)) {
            String orderId = request->getParam("orderId", true)->value();
            if (orderId == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("customerName", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerName\"}");
            }
            else if (request->getParam("customerPhone", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
            }
            else if (request->getParam("totalPrice", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"totalPrice\"}");
            }
            else {
              String fileName = "/ar/orders/" + orderId + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("customerName", true)->value());
              file.println(request->getParam("customerPhone", true)->value());
              String address = "-";
              if (LittleFS.exists("/ar/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
                String fileName2 = "/ar/users/" + request->getParam("customerPhone", true)->value() + ".txt";
                File file2 = LittleFS.open(fileName2, "r");
                address = splitString(file2.readString())[1];
                file2.close();
              }
              file.println(address);
              for (int i = 1; request->hasParam("id" + String(i), true); i++) {
                if (request->getParam("id" + String(i), true)->value() != "")
                  file.println(request->getParam("id" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("name" + String(i), true)->value() != "")
                  file.println(request->getParam("name" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("quantity" + String(i), true)->value() != "")
                  file.println(request->getParam("quantity" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("price" + String(i), true)->value() != "")
                  file.println(request->getParam("price" + String(i), true)->value());
                else
                  file.println("-");
              }
              file.println(request->getParam("totalPrice", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"address\":\"" + address + "\"}");
            }
          }
          else if (action == "add" && request->hasParam("customerName", true) && request->hasParam("customerPhone", true) && request->hasParam("totalPrice", true)) {
            if (request->getParam("customerName", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerName\"}");
            }
            else if (request->getParam("customerPhone", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
            }
            else if (request->getParam("totalPrice", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"totalPrice\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/ar/orders/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/ar/orders/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("customerName", true)->value());
              file.println(request->getParam("customerPhone", true)->value());
              String address = "-";
              if (LittleFS.exists("/ar/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
                String fileName2 = "/ar/users/" + request->getParam("customerPhone", true)->value() + ".txt";
                File file2 = LittleFS.open(fileName2, "r");
                address = splitString(file2.readString())[1];
                file2.close();
              }
              file.println(address);
              for (int i = 1; request->hasParam("id" + String(i), true); i++) {
                if (request->getParam("id" + String(i), true)->value() != "")
                  file.println(request->getParam("id" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("name" + String(i), true)->value() != "")
                  file.println(request->getParam("name" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("quantity" + String(i), true)->value() != "")
                  file.println(request->getParam("quantity" + String(i), true)->value());
                else
                  file.println("-");
                if (request->getParam("price" + String(i), true)->value() != "")
                  file.println(request->getParam("price" + String(i), true)->value());
                else
                  file.println("-");
              }
              file.println(request->getParam("totalPrice", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\",\"address\":\"" + address + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/ar/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"orders\":[";
          File dir = LittleFS.open("/ar/orders");
          bool firstOrder = true;
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              std::vector<String> parts = splitString(text);
              int totalLines = parts.size();
              if (!firstOrder) {
                result += ",";
              }
              firstOrder = false;
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"customerId\":\"" + String(index++) + "\",";
              result += "\"customerName\":\"" + parts[0] + "\",";
              result += "\"customerPhone\":\"" + parts[1] + "\",";
              result += "\"customerAddress\":\"" + parts[2] + "\",";
              result += "\"products\":[";
              int productCount = (totalLines - 4) / 4;
              for (int i = 0; i < productCount; i++) {
                int baseIdx = 3 + (i * 4);
                if (baseIdx + 3 < totalLines) {
                  result += "{\"id\":\"" + parts[baseIdx] + "\",";
                  result += "\"name\":\"" + parts[baseIdx + 1] + "\",";
                  result += "\"quantity\":" + parts[baseIdx + 2] + ",";
                  result += "\"unit_price\":" + parts[baseIdx + 3] + "}";
                  if (i < productCount - 1) result += ",";
                }
              }
              result += "],\"totalPrice\":" + parts[totalLines - 1] + "}";
            }
            entry.close();
          }
          dir.close();
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
    else if (request->hasParam("action", true) && request->hasParam("customerName", true) && request->hasParam("customerPhone", true)) {
      if (request->getParam("action", true)->value() == "gest") {
        if (request->getParam("customerName", true)->value() == "") {
          request->send(200, "application/json", "{\"status\":\"customerName\"}");
        }
        else if (request->getParam("customerPhone", true)->value() == "") {
          request->send(200, "application/json", "{\"status\":\"customerPhone\"}");
        }
        else {
          int number = 1;
          while (LittleFS.exists("/ar/orders/" + String(number) + ".txt")) {
            number++;
          }
          String fileName = "/ar/orders/" + String(number) + ".txt";
          File file = LittleFS.open(fileName, "w");
          file.println(request->getParam("customerName", true)->value());
          file.println(request->getParam("customerPhone", true)->value());
          String address = "-";
          if (LittleFS.exists("/ar/users/" + request->getParam("customerPhone", true)->value() + ".txt")) {
            String fileName2 = "/ar/users/" + request->getParam("customerPhone", true)->value() + ".txt";
            File file2 = LittleFS.open(fileName2, "r");
            address = splitString(file2.readString())[1];
            file2.close();
          }
          file.println(address);
          unsigned long sum = 0;
          for (int i = 1; request->hasParam("id" + String(i), true); i++) {
            if (LittleFS.exists("/ar/products/" + request->getParam("id" + String(i), true)->value() + ".txt")) {
              String fileName3 = "/ar/products/" + request->getParam("id" + String(i), true)->value() + ".txt";
              File file3 = LittleFS.open(fileName3, "r");
              String text = file3.readString();
              if (request->getParam("id" + String(i), true)->value() != "")
                file.println(request->getParam("id" + String(i), true)->value());
              else
                file.println("-");
              file.println(splitString(text)[0]);
              if (request->getParam("quantity" + String(i), true)->value() != "")
                file.println(request->getParam("quantity" + String(i), true)->value());
              else
                file.println("-");
              file.println(splitString(text)[2]);
              sum += splitString(text)[2].toInt() * request->getParam("quantity" + String(i), true)->value().toInt();
              file3.close();
            }
          }
          file.println(sum);
          file.close();
          request->send(200, "application/json", "{\"status\":\"ok\"}");
        }
      }
    }
  });
  
  server.on("/ar/categorys/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/categorys.htm", "text/html");
  });
  
  server.on("/ar/categorys/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("id", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/ar/categorys/" + id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("id", true) && request->hasParam("name", true) && request->hasParam("position", true) && request->hasParam("status", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("position", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"position\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              String fileName = "/ar/categorys/" + id + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("position", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("position", true) && request->hasParam("status", true)) {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("position", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"position\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/ar/categorys/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/ar/categorys/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("position", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/ar/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"categorys\":[";
          File dir = LittleFS.open("/ar/categorys");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"position\":\"" + splitString(text)[1] + "\",";
              result += "\"status\":\"" + splitString(text)[2] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/ar/products/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/products.htm", "text/html");
  });
  
  server.on("/ar/products/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin" || isTokenValid(request->getParam("token", true)->value(), request) == "user") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "delete" && request->hasParam("id", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            String fileName = "/ar/products/" + id + ".txt";
            if (LittleFS.exists(fileName)) {
              LittleFS.remove(fileName);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else {
              request->send(200, "application/json", "{\"status\":\"failed\"}");
            }
          }
          else if (action == "edit" && request->hasParam("id", true) && request->hasParam("name", true) && request->hasParam("categoryId", true) && request->hasParam("price", true) && request->hasParam("discount", true) && request->hasParam("status", true)) {
            String id = request->getParam("id", true)->value();
            if (id == "null") {
              request->send(200, "application/json", "{\"status\":\"forbidden\"}");
              return;
            }
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("categoryId", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"categoryId\"}");
            }
            else if (request->getParam("price", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"price\"}");
            }
            else if (request->getParam("discount", true)->value() == "") {
              request->send(200, "application/json", "{\"discount\":\"discount\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              String fileName = "/ar/products/" + id + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("categoryId", true)->value());
              file.println(request->getParam("price", true)->value());
              file.println(request->getParam("discount", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
          else if (action == "add" && request->hasParam("name", true) && request->hasParam("categoryId", true) && request->hasParam("price", true) && request->hasParam("discount", true) && request->hasParam("status", true)) {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("categoryId", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"categoryId\"}");
            }
            else if (request->getParam("price", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"price\"}");
            }
            else if (request->getParam("discount", true)->value() == "") {
              request->send(200, "application/json", "{\"discount\":\"discount\"}");
            }
            else if (request->getParam("status", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"status\"}");
            }
            else {
              int number = 1;
              while (LittleFS.exists("/ar/products/" + String(number) + ".txt")) {
                number++;
              }
              String fileName = "/ar/products/" + String(number) + ".txt";
              File file = LittleFS.open(fileName, "w");
              file.println(request->getParam("name", true)->value());
              file.println(request->getParam("categoryId", true)->value());
              file.println(request->getParam("price", true)->value());
              file.println(request->getParam("discount", true)->value());
              file.println(request->getParam("status", true)->value());
              file.close();
              request->send(200, "application/json", "{\"status\":\"ok\",\"id\":\"" + String(number) + "\"}");
            }
          }
        }
        else {
          int index = 1;
          File name = LittleFS.open("/ar/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"products\":[";
          File dir = LittleFS.open("/ar/products");
          while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
              String fileName = entry.name();
              int lastSlash = fileName.lastIndexOf('/');
              if (lastSlash != -1) {
                fileName = fileName.substring(lastSlash + 1);
              }
              if (fileName == "null.txt") {
                entry.close();
                continue;
              }
              String text = entry.readString();
              int dotIndex = fileName.lastIndexOf('.');
              if (dotIndex != -1) {
                fileName = fileName.substring(0, dotIndex);
              }
              result += "{\"id\":\"" + fileName + "\",";
              result += "\"name\":\"" + splitString(text)[0] + "\",";
              result += "\"categoryId\":\"" + splitString(text)[1] + "\",";
              if (LittleFS.exists("/ar/categorys/" + splitString(text)[1] + ".txt")) {
                File file2 = LittleFS.open("/ar/categorys/" + splitString(text)[1] + ".txt", "r");
                result += "\"categoryName\":\"" + splitString(file2.readString())[0] + "\",";
                file2.close();
              }
              else {
                result += "\"categoryName\":\"\",";
              }
              result += "\"price\":\"" + splitString(text)[2] + "\",";
              result += "\"discount\":\"" + splitString(text)[3] + "\",";
              result += "\"status\":\"" + splitString(text)[4] + "\"},";
            }
            entry.close();
          }
          dir.close();
          if (result.endsWith(",")) {
            result.remove(result.length() - 1);
          }
          result += "],";
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/ar/settings/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/settings.htm", "text/html");
  });
  
  server.on("/ar/settings/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            if (request->getParam("name", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"name\"}");
            }
            else if (request->getParam("unit", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"unit\"}");
            }
            else if (request->getParam("bgcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"bgcolor\"}");
            }
            else if (request->getParam("fgcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"fgcolor\"}");
            }
            else if (request->getParam("navcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"navcolor\"}");
            }
            else if (request->getParam("catcolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"catcolor\"}");
            }
            else if (request->getParam("procolor", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"procolor\"}");
            }
            else {
              if (LittleFS.exists("/ar/name.txt")) {
                File file = LittleFS.open("/ar/name.txt", "w");
                file.print(request->getParam("name", true)->value());
                file.close();
              }
              if (LittleFS.exists("/ar/unit.txt")) {
                File file = LittleFS.open("/ar/unit.txt", "w");
                file.print(request->getParam("unit", true)->value());
                file.close();
              }
              writeFile("/configs/bgcolor.txt", request->getParam("bgcolor", true)->value());
              writeFile("/configs/fgcolor.txt", request->getParam("fgcolor", true)->value());
              writeFile("/configs/navcolor.txt", request->getParam("navcolor", true)->value());
              writeFile("/configs/catcolor.txt", request->getParam("catcolor", true)->value());
              writeFile("/configs/procolor.txt", request->getParam("procolor", true)->value());
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/ar/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          File unit = LittleFS.open("/ar/unit.txt", "r");
          result += "\"unit\":\"" + unit.readString() + "\",";
          unit.close();
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/ar/modules/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/modules.htm", "text/html");
  });
  
  server.on("/ar/modules/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            if (request->getParam("ap", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"ap\"}");
            }
            else if (request->getParam("maxcon", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"maxcon\"}");
            }
            else if (request->getParam("networkType", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"networkType\"}");
            }
            else if (request->getParam("ip", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"ip\"}");
            }
            else if (request->getParam("gateway", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"gateway\"}");
            }
            else if (request->getParam("subnet", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"subnet\"}");
            }
            else if (request->getParam("dns", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"dns\"}");
            }
            else {
              writeFile("/configs/ap.txt", request->getParam("ap", true)->value());
              writeInt("/configs/maxcon.txt", request->getParam("maxcon", true)->value().toInt());
              writeFile("/configs/ssid.txt", request->getParam("ssid", true)->value());
              writeFile("/configs/key.txt", request->getParam("key", true)->value());
              if (request->getParam("networkType", true)->value() == "dhcp")
                writeBool("/configs/dhcp.txt", true);
              else if (request->getParam("networkType", true)->value() == "manual")
                writeBool("/configs/dhcp.txt", false);
              
              IPAddress ip, gateway, subnet, dns;
              ip.fromString(request->getParam("ip", true)->value());
              gateway.fromString(request->getParam("gateway", true)->value());
              subnet.fromString(request->getParam("subnet", true)->value());
              dns.fromString(request->getParam("dns", true)->value());
              writeIP("/configs/ip.txt", ip);
              writeIP("/configs/gateway.txt", gateway);
              writeIP("/configs/subnet.txt", subnet);
              writeIP("/configs/dns.txt", dns);
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/ar/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"ap\":\"" + readFile("/configs/ap.txt") + "\",";
          result += "\"maxcon\":\"" + String(readInt("/configs/maxcon.txt")) + "\",";
          result += "\"ssid\":\"" + readFile("/configs/ssid.txt") + "\",";
          result += "\"key\":\"" + readFile("/configs/key.txt") + "\",";
          if (readBool("/configs/dhcp.txt")) {
            result += "\"networkType\":\"dhcp\",";
            result += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
            result += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
            result += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
            result += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
          }
          else {
            result += "\"networkType\":\"manual\",";
            result += "\"ip\":\"" + readIP("/configs/ip.txt").toString() + "\",";
            result += "\"gateway\":\"" + readIP("/configs/gateway.txt").toString() + "\",";
            result += "\"subnet\":\"" + readIP("/configs/subnet.txt").toString() + "\",";
            result += "\"dns\":\"" + readIP("/configs/dns.txt").toString() + "\",";
          }
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/ar/account/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/account.htm", "text/html");
  });
  
  server.on("/ar/account/", HTTP_POST, [](AsyncWebServerRequest * request) {
    if (request->hasParam("token", true)) {
      if (isTokenValid(request->getParam("token", true)->value(), request) == "admin") {
        if (request->hasParam("action", true)) {
          String action = request->getParam("action", true)->value();
          if (action == "set") {
            String username1 = readFile("/configs/username1.txt");
            String username2 = readFile("/configs/username2.txt");
            String password1 = readFile("/configs/password1.txt");
            String password2 = readFile("/configs/password2.txt");
            
            if (request->getParam("username", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"username\"}");
            }
            else if (request->getParam("username", true)->value() != username1 && request->getParam("username", true)->value() != username2) {
              request->send(200, "application/json", "{\"status\":\"wrongUsername\"}");
            }
            else if (request->getParam("currentPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"currentPassword\"}");
            }
            else if (request->getParam("newPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"newPassword\"}");
            }
            else if (request->getParam("confirmPassword", true)->value() == "") {
              request->send(200, "application/json", "{\"status\":\"confirmPassword\"}");
            }
            else if (request->getParam("currentPassword", true)->value() != password1 && request->getParam("currentPassword", true)->value() != password2) {
              request->send(200, "application/json", "{\"status\":\"wrongPassword\"}");
            }
            else if (request->getParam("newPassword", true)->value() != request->getParam("confirmPassword", true)->value()) {
              request->send(200, "application/json", "{\"status\":\"noMatch\"}");
            }
            else if (request->getParam("newPassword", true)->value().length() < 8) {
              request->send(200, "application/json", "{\"status\":\"shortPassword\"}");
            }
            else {
              if (username1 == request->getParam("username", true)->value())
                writeFile("/configs/password1.txt", request->getParam("newPassword", true)->value());
              else if (username2 == request->getParam("username", true)->value())
                writeFile("/configs/password2.txt", request->getParam("newPassword", true)->value());
              for (int i = 0; i < 5; i++) {
                session[i].token = "";
                session[i].ip = IPAddress();
                session[i].expire = 0;
              }
              request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
          }
        }
        else {
          File name = LittleFS.open("/ar/name.txt", "r");
          String result = "{\"name\":\"" + name.readString() + "\",";
          name.close();
          result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
          result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
          result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
          result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
          result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
          request->send(200, "application/json; charset=utf-8", result);
        }
      }
    }
  });
  
  server.on("/ar/about/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/about.htm", "text/html");
  });
  
  server.on("/ar/about/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/ar/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File info = LittleFS.open("/ar/info.txt", "r");
    result += "\"info\":\"" + info.readString() + "\",";
    info.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/ar/contact/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/contact.htm", "text/html");
  });
  
  server.on("/ar/contact/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/ar/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    File address = LittleFS.open("/ar/address.txt", "r");
    result += "\"address\":\"" + address.readString() + "\",";
    address.close();
    File phone = LittleFS.open("/ar/phone.txt", "r");
    result += "\"phone\":\"" + phone.readString() + "\",";
    phone.close();
    File email = LittleFS.open("/ar/email.txt", "r");
    result += "\"email\":\"" + email.readString() + "\",";
    email.close();
    File instagram = LittleFS.open("/ar/instagram.txt", "r");
    result += "\"instagram\":\"" + instagram.readString() + "\",";
    instagram.close();
    File telegram = LittleFS.open("/ar/telegram.txt", "r");
    result += "\"telegram\":\"" + telegram.readString() + "\",";
    telegram.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.on("/ar/home/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(LittleFS, "/ar/home.htm", "text/html");
  });
  
  server.on("/ar/home/", HTTP_POST, [](AsyncWebServerRequest * request) {
    File name = LittleFS.open("/ar/name.txt", "r");
    String result = "{\"name\":\"" + name.readString() + "\",";
    name.close();
    result += "\"bgcolor\":\"" + readFile("/configs/bgcolor.txt") + "\",";
    result += "\"fgcolor\":\"" + readFile("/configs/fgcolor.txt") + "\",";
    result += "\"navcolor\":\"" + readFile("/configs/navcolor.txt") + "\",";
    result += "\"catcolor\":\"" + readFile("/configs/catcolor.txt") + "\",";
    result += "\"procolor\":\"" + readFile("/configs/procolor.txt") + "\"}";
    request->send(200, "application/json; charset=utf-8", result);
  });
  
  server.begin();
  dnsServer.start(53, "*", WiFi.softAPIP());
}

void loop() {
  dnsServer.processNextRequest();
  delay(250);
}
