#include <Arduino.h>
#include <Homie.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <X9C.h>

const int      DEFAULT_TEMPERATURE_INTERVAL = 10;
const double   DEFAULT_TEMP_SETPOINT        = 0.0;
const double   DEFAULT_TEMP_HYSTERESIS      = 0.5;

unsigned long  lastTemperatureSent = 0;

float          setpoint;
float          hysteresis;
String         value  = "0";   // relay status 0 = off, 1 = on

bool           relay;
int            modulatie_teller = 0;
const int      DEFAULT_MODULATIE_CYCLUS   = 120;   // in seconden
const int      DEFAULT_MODULATIE_PROCENT  = 50;    // in procenten, bijvoorbeeld 0 is nooit aan, 50 is helft tijd van cyclus, 100 is altijd tijdens cyclus   


// Temperature sensor on D5, place pull-up resistor between 2.2K or 4.7K to 5V
#define        ONE_WIRE_BUS D5      //temp sensor DS18B20 on D5(5)

// Dallas temperature variables
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Relay
#define  PIN_RELAY    D6      //relay on pin D6(6)

// Analog
#define PIN_ANALOG  A0
int analog_val = 0;

// Digital potentiometer X9C
#define INC   D4   // D1 Mini D4(2)  - pulled up in H/W (10k) ->  chip pin 1
#define UD    D8   // D1 Mini D8(15)                          ->  chip pin 2
#define CS    D0   // D1 Mini D0(16) - pulled up in H/W (10k) ->  chip pin 7

// "up" and "down" make sense in relation to the wiper pin 5 [VW/RW] and the HIGH end of the pot
// i.e. pin 3 [VH/RH], leaving pin 6 [VL/RL] unused (floating). You can easily use pin 6 instead
// pin 3, but "min" will actually mean "max" and vice versa. Also, the "setPot" percentage will
// set the equivalent of 100-<value>, i.e. setPot(70) will set the resistance between pins 5 and 6
// to 30% of the maximum. (Of course in that case,the "unused" resistance between 5 and 3 will be 70%)
// Nothing to stop you using it as a full centre-tap potentiometer, the above example giving
// pin 3[H] -- 70% -- pin 5[W] -- 30% -- pin 6[L]

X9C pot;                               // create a pot controller
String    potperc;                     // zie ook potprocentSetting
String    potset;
const int DEFAULT_POT_PROCENT = 100;   //potmeter is helemaal open, minimaal mogelijk aansturing koeling



//Homie
HomieNode controlNode("control", "temperature", "temperature"); /* middelste parm toegevoegd bij upg naar Homie 3.0.0 */

// VOORBEELD:  mosquitto_pub -t 'homie/dev00x/$implementation/config/set' -m '{"settings":{"setpoint":12}}' -r
//  zorg er voor dat er al een "settings":{}  in de initiële config zit.
HomieSetting<long>   temperatureIntervalSetting("temperatureInterval", "temp interval in seconds");
HomieSetting<double>            setpointSetting("setpoint"           , "temp setpoint   ");
HomieSetting<double>          hysteresisSetting("hysteresis"         , "temp hysteresis ");
HomieSetting<double>           modcyclusSetting("modcyclus"          , "temp modulatie cyclus ");
HomieSetting<double>          modprocentSetting("modprocent"         , "temp modulatie procent ");
//
HomieSetting<double>          potprocentSetting("potprocent"         , "potentiometer procent ");

bool relayOnHandler(const HomieRange& range, const String& value) {
  if (value != "1" && value != "0") return false;
  relay = (value == "1");
  digitalWrite(PIN_RELAY, relay ? HIGH : LOW);
  controlNode.setProperty("relay").send(relay ? "1" : "0");
  //Homie.getLogger() << "relay is set (external) " << (relay ? "on (1)" : "off (0)") << endl;
  return true;
}

void loopHandler() {
  if (millis() - lastTemperatureSent >= temperatureIntervalSetting.get() * 1000UL || lastTemperatureSent == 0) {

    String HeapString = "heap free="+String(ESP.getFreeHeap())+",frag="+String(ESP.getHeapFragmentation());
    controlNode.setProperty("heap").send(HeapString);
    
    sensors.begin();
    sensors.requestTemperatures(); // Send the command to get temperatures, takes 1 second.
    float temp0 = sensors.getTempCByIndex(0);   //wijnkoelkast sensor A
    float temp1 = sensors.getTempCByIndex(1);   //wijnkoelkast sensor B
    float temp2 = sensors.getTempCByIndex(2);   //wijnkoelkast sensor C
    //Homie.getLogger() << "Temp 0 / 1 / 2 : " << temp0 << " °C " << temp1 <<  " °C" << temp2 <<  " °C" << endl;

    setpoint   = setpointSetting.get();
    hysteresis = hysteresisSetting.get();
    
    //Homie.getLogger() << "Setpoint Temp 0: " << setpoint << " °C    hysteresis +/- :" << hysteresis << " °C" << endl;

    // Moduleren betekent relay aan voor modprocent in modcyclus
    modulatie_teller = modulatie_teller + temperatureIntervalSetting.get();
    if (modulatie_teller  > modcyclusSetting.get() ) modulatie_teller = 0;
    //Homie.getLogger() << "modulatie_teller: " << modulatie_teller << endl;

    String mod;
    mod = "mod modcyc=";
    mod += String(modcyclusSetting.get());
    mod += ",modprc=";
    mod += String(modprocentSetting.get());
    controlNode.setProperty("modulation").send(mod);
    //Homie.getLogger() << "String 'mod' send to mqtt: [" << mod << "]" << endl;

    if (temp0 != -127.00 ) 
      {
      //Homie.getLogger() << "temp0   geldige meting (ongelijk -127.0)" << endl;
      
      if (temp0 < (setpoint - hysteresis) ) value = "1";
      if (temp0 > (setpoint + hysteresis) ) value = "0";

      relay = (value == "1") && (modulatie_teller < (modcyclusSetting.get() * modprocentSetting.get() / 100) );   //
      digitalWrite(PIN_RELAY, relay ? HIGH : LOW);
      controlNode.setProperty("relay").send(relay ? "1" : "0");
      //Homie.getLogger() << "relay is set (auto) " << (relay ? "on (1)" : "off (0)") << endl;
      }
    else {
      //Homie.getLogger() << "temp0 Ongeldige meting (  gelijk -127.0)" << endl;
    }

    String temperatures = "temperatures ";

    if (temp0 != -127.00 ) 
      {
      temperatures += "temp0=";
      temperatures += String(temp0);
      temperatures += ",";
      }    
    if (temp1 != -127.00 ) 
      {
      temperatures += "temp1=";
      temperatures += String(temp1);
      temperatures += ",";
      }    
    if (temp2 != -127.00 ) 
      {
      temperatures += "temp2=";
      temperatures += String(temp2);
      temperatures += ",";
      }        
    temperatures += "relay0=";
    temperatures += String(relay ? "1" : "0");
    temperatures += ",setp0=";
    temperatures += String(setpoint);
    temperatures += ",hyst0=";
    temperatures += String(hysteresis);

    controlNode.setProperty("degrees").send(temperatures);

    potperc = String(potprocentSetting.get());  // nu nog in formaat 0.00
    uint16_t potperc_int = potperc.toInt();     //
    potperc = String(potperc_int);              // nu als int 0
    pot.setPot(potperc_int,true);               // true=save, so pot will keep value after shutdown if you do nothing else...
    potset = "potset potperc=";
    potset += potperc;
    controlNode.setProperty("pot").send(potset);
    
    analog_val = analogRead(PIN_ANALOG);
    String analog_str = "analog analog=";
    analog_str += String(analog_val);
    controlNode.setProperty("analog").send(analog_str);

    //Homie.getLogger() << "Analog value =" << analog_val << " Pot is set to " << potset << "%" << endl;
    
    lastTemperatureSent = millis();
    
  }
}

void onHomieEvent(const HomieEvent& event) {
  switch (event.type) {
    case HomieEventType::STANDALONE_MODE:
      Serial << "Standalone mode started" << endl;
      break;
    case HomieEventType::CONFIGURATION_MODE:
      Serial << "Configuration mode started" << endl;
      break;
    case HomieEventType::NORMAL_MODE:
      Serial << "Normal mode started" << endl;
      break;
    case HomieEventType::OTA_STARTED:
      Serial << "OTA started" << endl;
      break;
    case HomieEventType::OTA_PROGRESS:
      Serial << "OTA progress, " << event.sizeDone << "/" << event.sizeTotal << endl;
      break;
    case HomieEventType::OTA_FAILED:
      Serial << "OTA failed" << endl;
      break;
    case HomieEventType::OTA_SUCCESSFUL:
      Serial << "OTA successful" << endl;
      break;
    case HomieEventType::ABOUT_TO_RESET:
      Serial << "About to reset" << endl;
      break;
    case HomieEventType::WIFI_CONNECTED:
      Serial << "Wi-Fi connected, IP: " << event.ip << ", gateway: " << event.gateway << ", mask: " << event.mask << endl;
      break;
    case HomieEventType::WIFI_DISCONNECTED:
      Serial << "Wi-Fi disconnected, reason: " << (int8_t)event.wifiReason << endl;
      break;
    case HomieEventType::MQTT_READY:
      Serial << "MQTT connected" << endl;
      break;
    case HomieEventType::MQTT_DISCONNECTED:
      Serial << "MQTT disconnected, reason: " << (int8_t)event.mqttReason << endl;
      break;
    case HomieEventType::MQTT_PACKET_ACKNOWLEDGED:
      // Serial << "MQTT packet acknowledged, packetId: " << event.packetId << endl;
      break;
    case HomieEventType::READY_TO_SLEEP:
      Serial << "Ready to sleep" << endl;
      break;
    case HomieEventType::SENDING_STATISTICS:
      Serial << "Sending statistics" << endl;
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial << endl << endl;

  Homie_setFirmware("control-temp-relay", "2.0.1");
  //1.1.3 - met nieuwe ESP8266 2.4.0-rc2
  //1.1.4 - eerste versie met light sensor er bij
  //1.1.5 - relay 'modulatie' verwarming te krachtig
  //1.1.6 - nu met 'officiele' BH1750 lib https://github.com/claws/BH1750 met aanpassing, zie library "BH1750erik - master"
  //1.1.7 - geldende modcyclus en modprocent naar 'modulation' topic
  //1.9.0 - correctie versie nummering en belangerijke aanpassingen:
  //        - digitale potmeter
  //        - verwijderen licht sensoren
  //        - analoge meting van voltage peltier element, gebruik weerstanden nul - 10k - 33k -> naar plus van peltier
  //          en verbind 10uF condensator over 10k weerstand, en sluit punt tussen 10k en 33k weerstand aan op A0.
  //1.9.1 - potprocent via settings ivm vasthouden waarde na herstart wemos
  //1.9.2 - aanpassen voor laatste versie libraries: ESP8266 van 2.4.0-rc2 naar 2.6.3 (Blijkbaar heet Wemos nu LOLIN(WEMOS) D1 R2 & Mini)
  //1.10.1 - 20200414:
  //         aanpassen voor Homie 3.0.0 
  //         ook nodig library ArduinoJSON van 5.11.2 naar 6.15.1
  //         update library Bounce2 v2.3 naar 2.53
  //         update OneWire van 2.3.3 naar 2.3.5
  //         update DallasTemperature van 3.7.6 naar 3.8.0
  //         update ESPAsyncTCP-master at version 1.1.0 ===> 1.2.2
  //         update ESPAsyncWebServer-master at version 1.1.0 ===> 1.2.3 
  //         update async-mqtt-client-master at version 0.8.1 ===> 0.8.2
  //2.0.0  - 20200414:
  //         Volledige temp regeling, V1.x is alleen regeling voor warmte element, koude is hard met potmeter ingesteld
  //         Add heap / memory topic and logging at start
  //2.0.1  - 20200515: Aanpassingen voor Platformio en Github
  //         20200917  + Homie.events() ivm debugging

  Homie.getLogger() << "Compiled: " << __DATE__ << " | " << __TIME__ << " | " << __FILE__ <<  endl;
  Homie.getLogger() << "ESP CoreVersion       : " << ESP.getCoreVersion() << endl;
  Homie.getLogger() << "ESP FreeSketchSpace   : " << ESP.getFreeSketchSpace() << endl;
  Homie.getLogger() << "ESP FreeHeap          : " << ESP.getFreeHeap() << endl;
  Homie.getLogger() << "ESP HeapFragmentation : " << ESP.getHeapFragmentation() << endl;
   
  // relay in begin state
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  Homie.setLoopFunction(loopHandler);
  controlNode.advertise("degrees").setName("Degrees").setDatatype("String").setUnit("ºC");
  controlNode.advertise("modulation").setName("Modulation").setDatatype("String").setUnit("sec");
  controlNode.advertise("analog").setName("Analog").setDatatype("String").setUnit("value");
  controlNode.advertise("pot").setName("Pot").setDatatype("String").setUnit("%");
  controlNode.advertise("relay").setName("Relay").setDatatype("Int").setUnit("value").settable(relayOnHandler);
  controlNode.advertise("heap").setName("Heap").setDatatype("String");

  temperatureIntervalSetting.setDefaultValue(DEFAULT_TEMPERATURE_INTERVAL).setValidator([] (long candidate) { return candidate > 0; });
  setpointSetting.setDefaultValue(DEFAULT_TEMP_SETPOINT).setValidator([] (double candidate) { return candidate >= 0;   });
  hysteresisSetting.setDefaultValue(DEFAULT_TEMP_HYSTERESIS).setValidator([] (double candidate) { return candidate >= 0.1; });
  modcyclusSetting.setDefaultValue(DEFAULT_MODULATIE_CYCLUS).setValidator([] (double candidate) { return candidate >= DEFAULT_TEMPERATURE_INTERVAL; });
  modprocentSetting.setDefaultValue(DEFAULT_MODULATIE_PROCENT).setValidator([] (double candidate) { return candidate >= 0 && candidate <= 100; });

  // digital potentiometer
  potprocentSetting.setDefaultValue(DEFAULT_POT_PROCENT).setValidator([] (double candidate) { return candidate >= 0 && candidate <= 100; });
  // Initialize Digital potentiometer X9C
  pot.begin(CS,INC,UD);
 
  Homie.onEvent(onHomieEvent);
  Homie.setup();
}

void loop() {
  Homie.loop();
}
