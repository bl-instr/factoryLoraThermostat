boolean printDiagnostics = false;

#define NODEADDRESS 10
#define GATEWAYADDRESS 10
#define DHTPIN 13
#define REDPIN 10
#define GRNPIN 12
#define YELPIN 11
#define RLYPIN 7
#define DHTTYPE DHT11   
#define CHSPIN 17          // LoRa radio chip select
#define RSTPIN 14       // LoRa radio reset
#define IRQPIN 15         // LoRa radio IRQ
#define LORSBW 62e3
#define LORSPF 9
#define LORFRQ 868E6

#include "DHT.h"
#include "CRC16.h"
#include <SPI.h>
#include <LoRa.h>

DHT     dht(DHTPIN, DHTTYPE);
CRC16   crc;
unsigned long lastPublishTime;
unsigned long publishInterval = 2000;
unsigned long lastRelayTime;
unsigned long relayInterval = 20000;
unsigned long lastGreenTime;
unsigned long greenInterval = 200;
boolean greenLed = false;
unsigned long yellowInterval = 1000;
unsigned long lastYellowTime = 200;
boolean yellowLed = false;
boolean redLed = false;
const long loraFreq = LORFRQ;  // LoRa Frequency


struct LoraDataHeader
{
  uint16_t icrc;
  int16_t istate;
  int16_t inodeAddr;
  int16_t igatewayAddr;
  int16_t iwatchdog;
  int16_t inewData;  
  int16_t irssi;  
  int16_t isnr;  
}; 
struct LoraData
{
  int16_t imode;           //  0=OFF, 1=ON, 2=AUTO;
  int16_t ipubInterval;    //  0.10 sec
  int16_t itemp;           //  0.01 degC
  int16_t ihumid;          //  0.01 %
  int16_t irelay;          //  0=OFF, 1=ON
  int16_t isetTemp;        //  0.01 degC
  int16_t iwindowTemp;     //  0.01 degC
  int16_t irelayInterval;  //  0.10 sec
}; 
    
union LoraNode
{
  struct
  {
    LoraDataHeader header;
    LoraData data;
  };
  uint8_t buffer[32];
};
LoraNode loraNode;
uint8_t sizeOfLoraNode = 32;

void setup() 
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(REDPIN, OUTPUT);
  pinMode(YELPIN, OUTPUT);
  pinMode(GRNPIN, OUTPUT);
  pinMode(RLYPIN, OUTPUT);
  if (printDiagnostics) Serial.begin(9600);
  for (int ii = 0; ii < 25; ++ii)
  {
    digitalWrite(LED_BUILTIN, HIGH);   
    digitalWrite(REDPIN, HIGH);   
    digitalWrite(YELPIN, HIGH);   
    digitalWrite(GRNPIN, HIGH);   
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);   
    digitalWrite(REDPIN, LOW);   
    digitalWrite(YELPIN, LOW);   
    digitalWrite(GRNPIN, LOW);   
    delay(100);
  }
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);   
  digitalWrite(REDPIN, LOW);   
  digitalWrite(YELPIN, false);   
  digitalWrite(GRNPIN, false);   
  digitalWrite(RLYPIN, LOW); 
  dht.begin();

  loraNode.header.istate = 1;
  loraNode.header.inewData = 0;
  loraNode.header.inodeAddr = NODEADDRESS;
  loraNode.header.igatewayAddr = GATEWAYADDRESS;
  loraNode.header.iwatchdog = 0;
  loraNode.header.irssi = 0;  
  loraNode.header.isnr = 0;  
  loraNode.data.imode = 0;
  loraNode.data.ipubInterval = 100;
  loraNode.data.itemp = 0;
  loraNode.data.ihumid = 0;
  loraNode.data.irelay = 0;
  loraNode.data.isetTemp = 1000;
  loraNode.data.iwindowTemp = 200;
  loraNode.data.irelayInterval = 600;
  publishInterval = loraNode.data.ipubInterval * 100;
  lastPublishTime = millis();
  relayInterval = loraNode.data.irelayInterval * 100;
  lastRelayTime = lastPublishTime;
  lastGreenTime = lastPublishTime;

  crc.restart();
  for (int ii = 2; ii < sizeOfLoraNode; ii++)
  {
    crc.add(loraNode.buffer[ii]);
  }
  loraNode.header.icrc = crc.calc();

  LoRa.setPins(CHSPIN, RSTPIN, IRQPIN);

  if (!LoRa.begin(loraFreq)) 
  {
    if (printDiagnostics) Serial.println("LoRa init failed. Check your connections.");
    while (true);                       // if failed, do nothing
  }
  LoRa.setSpreadingFactor(LORSPF);
  LoRa.setSignalBandwidth(LORSBW);
  if (printDiagnostics)
  {
    Serial.println("LoRa init succeeded.");
    Serial.println();
    Serial.println("LoRa Simple Node");
    Serial.println("Only receive messages from gateways");
    Serial.println("Tx: invertIQ disable");
    Serial.println("Rx: invertIQ enable");
    Serial.println();    
  }
  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  LoRa_rxMode();
  
}

void loop() 
{
  unsigned long nowTime = millis();
  if (greenLed)
  {
    if ((nowTime - lastGreenTime) > greenInterval)
    {
      greenLed = false;
      digitalWrite(GRNPIN, greenLed);
    }
  }
  if (yellowLed)
  {
    if ((nowTime - lastYellowTime) > yellowInterval)
    {
      yellowLed = false;
      digitalWrite(YELPIN, yellowLed);
    }
  }
  if ((nowTime - lastPublishTime) > publishInterval)
  {
    lastPublishTime = nowTime;
    float humid = dht.readHumidity();
    float temp = dht.readTemperature(); 
    if (isnan(humid) || isnan(temp)) 
    {
      if (printDiagnostics) Serial.println(F("Failed to read from DHT sensor!"));
      humid = 0;
      temp = -20;
    }
    else
    {
      if (printDiagnostics) 
      {
        Serial.print(F("Humidity: "));
        Serial.print(humid);
        Serial.print(F("%  Temperature: "));
        Serial.print(temp);
        Serial.println(F("°C "));
      }
    }
    humid = humid * 100;
    temp = temp * 100;
    loraNode.data.ihumid = (int16_t) humid;
    loraNode.data.itemp  = (int16_t) temp;
    if (loraNode.data.imode == 0)
    {
      digitalWrite(RLYPIN, LOW);
      digitalWrite(REDPIN, LOW);
      loraNode.data.irelay = 0;
    }
    if (loraNode.data.imode == 1) 
    {
      digitalWrite(RLYPIN, HIGH);
      digitalWrite(REDPIN, HIGH);
      loraNode.data.irelay = 1;
    }
    if (loraNode.data.imode == 2)
    {
      int16_t irelay = 0;
      if (((nowTime - lastRelayTime) > relayInterval) && (humid > 0))
      {
        if (loraNode.data.itemp > (loraNode.data.isetTemp + loraNode.data.iwindowTemp)) irelay = 0;
        if (loraNode.data.itemp < (loraNode.data.isetTemp - loraNode.data.iwindowTemp)) irelay = 1;
        if (irelay != loraNode.data.irelay)
        {
          loraNode.data.irelay = irelay;
          if (loraNode.data.irelay == 0)
          {
            digitalWrite(RLYPIN, LOW);
            digitalWrite(REDPIN, LOW);
          }
          if (loraNode.data.irelay == 1)
          {
            digitalWrite(RLYPIN, HIGH);
            digitalWrite(REDPIN, HIGH);
          }
          lastRelayTime = nowTime;
        }
      }
    }
    loraNode.header.iwatchdog = loraNode.header.iwatchdog + 1;
    if (loraNode.header.iwatchdog > 32765) loraNode.header.iwatchdog = 0;
    crc.restart();
    for (int ii = 2; ii < sizeOfLoraNode; ii++)
    {
      crc.add(loraNode.buffer[ii]);
    }
    loraNode.header.icrc = crc.calc();
    greenLed = true;
    digitalWrite(GRNPIN, greenLed);
    lastGreenTime = nowTime;
    lastYellowTime = nowTime;
    loraNode.header.irssi = 0;  
    loraNode.header.isnr = 0;  
    LoRa_sendMessage(loraNode.buffer, sizeOfLoraNode);
    
  }
}

void LoRa_rxMode()
{
  LoRa.enableInvertIQ();                // active invert I and Q signals
  LoRa.receive();                       // set receive mode
}

void LoRa_txMode()
{
  LoRa.idle();                          // set standby mode
  LoRa.disableInvertIQ();               // normal mode
}

void LoRa_sendMessage(uint8_t *buffer, uint8_t size) 
{
  LoRa_txMode();                        // set tx mode
  LoRa.beginPacket();                   // start packet
  LoRa.write(buffer, (size_t) size);    // add payload
  LoRa.endPacket(true);                 // finish packet and send it
}

void onReceive(int packetSize) 
{
  uint8_t numBytes = 0;
  LoraNode loraNodeReceive;
  
  if (printDiagnostics) Serial.print("Received LoRa data at: ");
  if (printDiagnostics) Serial.println(millis());
  while (LoRa.available() )
  {
    numBytes = LoRa.readBytes(loraNodeReceive.buffer, sizeOfLoraNode);
  }
  if (numBytes != sizeOfLoraNode)
  {
    if (printDiagnostics)
    {
      Serial.print("LoRa bytes do not match. Bytes Received: ");
      Serial.print(numBytes);
      Serial.print(", Bytes expected: ");
      Serial.println(sizeOfLoraNode);
    }
    return;
  }
  
  crc.restart();
  for (int ii = 2; ii < sizeOfLoraNode; ii++)
  {
    crc.add(loraNodeReceive.buffer[ii]);
  }
  uint16_t crcCalc = crc.calc();
  if (crcCalc != loraNodeReceive.header.icrc) 
  {
    if (printDiagnostics)
    {
      Serial.print("LoRa CRC does not match. CRC Received: ");
      Serial.print(loraNodeReceive.header.icrc);
      Serial.print(", CRC expected: ");
      Serial.println(crcCalc);
    }
    return;
  }

  if (loraNodeReceive.header.igatewayAddr != GATEWAYADDRESS) 
  {
    if (printDiagnostics)
    {
      Serial.println("LoRa Gateway address do not match. Addr Received: ");
      Serial.print(loraNodeReceive.header.igatewayAddr);
      Serial.print(", Addr expected: ");
      Serial.println(GATEWAYADDRESS);
    }
    return;
  }
  
  if (loraNodeReceive.header.inodeAddr != NODEADDRESS) 
  {
    if (printDiagnostics)
    {
      Serial.println("LoRa Node address do not match. Addr Received: ");
      Serial.print(loraNodeReceive.header.inodeAddr);
      Serial.print(", Addr expected: ");
      Serial.println(NODEADDRESS);
    }
    return;
  }

  if (printDiagnostics)
  {
    Serial.print("Node Receive: ");
    Serial.println(numBytes);
    Serial.print("icrc           : ");
    Serial.println(loraNodeReceive.header.icrc);
  }
  if (loraNode.header.istate == 0) loraNode.data.imode = loraNodeReceive.data.imode;
  loraNode.data.ipubInterval    = loraNodeReceive.data.ipubInterval;
  loraNode.data.isetTemp        = loraNodeReceive.data.isetTemp;
  loraNode.data.iwindowTemp     = loraNodeReceive.data.iwindowTemp;
  loraNode.data.irelayInterval  = loraNodeReceive.data.irelayInterval;
  loraNode.header.istate = 0;

  
  publishInterval = loraNode.data.ipubInterval * 100;
  relayInterval = loraNode.data.irelayInterval * 100;
  lastPublishTime = 1;
  
  yellowLed = true;
  digitalWrite(YELPIN, yellowLed);
  lastYellowTime = millis();
  
}

void onTxDone() 
{
  if (printDiagnostics) Serial.println("TxDone");
  LoRa_rxMode();
}
