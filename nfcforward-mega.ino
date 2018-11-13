#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <MFRC522.h>

const byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};
IPAddress ip(192, 168, 1, 177);
IPAddress dnsserver(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(192, 168, 1, 1);

EthernetServer server(80);
String serverAddress;

#define SD_SS_PIN 4

#define RST_PIN         9          // Configurable, see typical pin layout above
#define SS_PIN          7         // Configurable, see typical pin layout above
#define DEBUG true
#define SREQUEST_LENGTH 19

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

void readConfig(){
  if (!SD.begin(SD_SS_PIN)) {
    Serial.println(F("SD error!"));
    while (1);
  }
  if (!SD.exists("cfg.ini")) {
    Serial.println(F("Config file not exist error!"));
    while (1);
  }
  File myFile = SD.open(F("cfg.ini"), FILE_READ);
  if (myFile) {
    String row;
    int rowNumber = 1;
    while (myFile.available()) {
      char c = myFile.read();
      if (c != '\r' && c != '\n')
        row += c;
      else if (c == '\n'){
        if (rowNumber == 1) 
          serverAddress = row;
        row = "";
        rowNumber++;
      }
    }
    myFile.close();
  }
  else {
    Serial.println(F("Config file open error!"));
  }
}

void initRC522() {
  mfrc522.PCD_Init();   // Init MFRC522
#ifdef DEBUG
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
#endif
}

void initEthernet() {
  Ethernet.begin(mac, ip, dnsserver, gateway, subnet);
  server.begin();
#ifdef DEBUG
  Serial.print("server is at ");
  Serial.println(Ethernet.localIP());
#endif
}

void initPins() {
  pinMode(10,OUTPUT);
  digitalWrite(10,HIGH);
  pinMode(7,OUTPUT);
  digitalWrite(7,HIGH);
  pinMode(4,OUTPUT);
  digitalWrite(4,HIGH);
}

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
#ifdef DEBUG
  while (!Serial) {
    ;
  }
#endif
  initPins();
  readConfig();
  initEthernet();
  initRC522();
  Serial.println(F("Setup ready"));
}


EthernetClient webClient;
String command;
bool dataSent = false;
bool firstLine = true;

void loop() {
  
  EthernetClient webServerClient = server.available();
  if (webServerClient) {
    boolean currentLineIsBlank = true;
    boolean inAction = false;
    while (webServerClient.connected()) {
      if (webServerClient.available()) {
        char c = webServerClient.read();
#ifdef DEBUG
        Serial.print(c);
#endif
        if (c != '\r' && firstLine) {
          if (c!=' ' && inAction) {
            command += c;
          } else if (c==' ' && !inAction)
            inAction = true;
            else if (c==' ' && inAction)
              inAction = false;
        } else
          firstLine = false;
        
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          processRequest(webServerClient);
          firstLine = true;
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    command = "";
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    webServerClient.stop();
  }

  if (webClient.available()) {
    byte buff[64];
    int cnt = webClient.read(buff,sizeof(buff));
#ifdef DEBUG
    Serial.write(buff,cnt);
#endif
  }

  if (!webClient.connected() && dataSent) {
    webClient.stop();
    dataSent = false;
  }

  // Look for new cards
  if ( mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {  
    char charArrayOfAddress[serverAddress.length()+1];
    serverAddress.toCharArray(charArrayOfAddress,serverAddress.length()+1);
#ifdef DEBUG
    Serial.print(F("connect to:'"));
    Serial.print(charArrayOfAddress);
    Serial.println("'");
#endif
    if (webClient.connect(charArrayOfAddress, 80)) {
      webClient.print(F("GET /log.php?nfcid="));
      for ( uint8_t i = 0; i < 4; i++) {  //
        byte readCard = mfrc522.uid.uidByte[i];
        webClient.print(readCard, HEX);
      }  
      mfrc522.PICC_HaltA(); // Stop reading
      webClient.println(F(" HTTP/1.1"));
      webClient.print(F("Host: "));
      webClient.print(charArrayOfAddress); //charArrayOfAddress
      webClient.println();
      webClient.println(F("Connection: close"));
      webClient.println();
      dataSent = true;
    } else {
#ifdef DEBUG
      Serial.println(F("connection failed"));
#endif
    }
  }
}

void printHeaderToWebClient(EthernetClient client, String code, String type) {
    client.print(F("HTTP/1.1 "));
    client.println(code);
    client.print(F("Content-Type: "));
    if (type != "")
      client.println(type);
    client.println(F("Connection: close"));  // the connection will be closed after completion of the response
    client.println();
}

void sendFile(String fileName, EthernetClient client, String type) {
  if (!SD.exists(fileName)) {
    printHeaderToWebClient(client,F("404 File does not exists"),type);
  } else {
    File myFile = SD.open(fileName, FILE_READ);
    if (myFile) {
      printHeaderToWebClient(client,F("200 OK"),type);
      byte buff[128];
      while (myFile.available()) {
        int count = myFile.read(buff,sizeof(buff));
        client.write(buff,count);
      }
      myFile.close();
    } else {
      printHeaderToWebClient(client,F("500 Server internal error"),F("text/html"));
      client.print(F("error opening file ['"));
      client.print(fileName);
      client.println(F("']"));
    }
  }

}

void processRequest(EthernetClient client) {
  int idx = command.indexOf(F("/index.html?server="));
  if (idx==-1) {
    int idxEnd = command.indexOf("?");
    if (idxEnd>-1)
      command = command.substring(1,idxEnd);
    else
      command = command.substring(1);
    
    if (command=="")
      command = F("index.htm");
    
    String fileType;
    if (command.endsWith(F(".css")))
      fileType = F("text/css");
    else if (command.endsWith(F(".png")))
      fileType = F("image/png");
    else
     fileType = F("text/html");
    sendFile(command,client, fileType);
  } else {
    String val = command.substring(idx+SREQUEST_LENGTH);
    if (val.length()>0) {
      sendFile(F("index.htm"),client,F("text/html"));
    }
  }
}
