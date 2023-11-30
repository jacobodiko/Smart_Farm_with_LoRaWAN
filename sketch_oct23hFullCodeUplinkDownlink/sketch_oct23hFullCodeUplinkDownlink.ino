#include <DHT.h>
#include <DHT_U.h>

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#define DHTPIN 5
#define DHTTYPE DHT11
DHT_Unified dht(DHTPIN, DHTTYPE);
//Soil Moisture
const int sensor_pin = A0; /* Soil moisture sensor O/P pin */
// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui(u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8] = { 0x70, 0x1F, 0x06, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
void os_getDevEui(u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endinness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
static const u1_t PROGMEM APPKEY[16] = { 0xF3, 0xB6, 0x15, 0xBE, 0x10, 0x19, 0xE9, 0x84, 0xE0, 0x16, 0x0F, 0xBB, 0xDB, 0xBD, 0x88, 0x4D };
void os_getDevKey(u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}

static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 30;  // 1 minute

// Pin mapping
const lmic_pinmap lmic_pins = {
  .nss = 10,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 4,
  .dio = { 2, 3, LMIC_UNUSED_PIN },
};
void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Serial.println(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      Serial.println(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      Serial.println(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      Serial.println(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING"));
      break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED"));

      // Disable link check validation (automatically enabled
      // during join, but not supported by TTN at this time).
      LMIC_setLinkCheckMode(0);
      break;
    case EV_RFU1:
      Serial.println(F("EV_RFU1"));
      break;
    case EV_JOIN_FAILED:
      Serial.println(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      Serial.println(F("EV_REJOIN_FAILED"));
      break;
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
      if (LMIC.txrxFlags & TXRX_ACK)
        Serial.println(F("Received ack"));
      if (LMIC.dataLen) {
        Serial.println(F("Received "));
        Serial.println(LMIC.dataLen);
        Serial.println(F(" bytes of payload"));
        uint8_t result = LMIC.frame[LMIC.dataBeg + 0];
        Serial.println(result);

        if (result == 0) {
          digitalWrite(7, HIGH);
        } else {
          digitalWrite(7, LOW);
        }
      }
      // Schedule next transmission
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      break;
    case EV_LOST_TSYNC:
      Serial.println(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      Serial.println(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      Serial.println(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    default:
      Serial.println(F("Unknown event"));
      break;
  }
}

void do_send(osjob_t* j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    // Prepare upstream data transmission at the next possible time.
    Serial.println("Sending Data");

    //Temperature and Humidity
    sensors_event_t event;
    dht.temperature().getEvent(&event);
    int temperature = int((event.temperature) * 100);
    dht.humidity().getEvent(&event);
    int humidity = int((event.relative_humidity) * 100);
    int sensor_analog;
    sensor_analog = analogRead(sensor_pin);
    int moisture = (100 - ((sensor_analog / 1023.00) * 100));  //Moisture level
    //Voltage
    int voltage = getAccurateVcc();

    Serial.println(temperature);
    Serial.println(humidity);
    Serial.println(voltage);
    Serial.println(moisture);

    LMIC.frame[0] = temperature >> 8;
    LMIC.frame[1] = temperature;
    LMIC.frame[2] = humidity >> 8;
    LMIC.frame[3] = humidity;
    LMIC.frame[4] = voltage >> 8;
    LMIC.frame[5] = voltage;
    LMIC.frame[6] = moisture >> 8;
    LMIC.frame[7] = moisture;

    LMIC_setTxData2(1, LMIC.frame, 8, 0);
    Serial.println(F("Packet queued"));
  }
  // Next TX is scheduled after TX_COMPLETE event.
}
void setup() {
  Serial.begin(115200);
  Serial.println(F("Starting"));
  dht.begin();
  pinMode(7, OUTPUT);

  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Use with Arduino Pro Mini ATmega328P 3.3V 8 MHz
  // Let LMIC compensate for +/- 1% clock error
  LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);

  // TTN uses SF9 for its RX2 window.
  LMIC.dn2Dr = DR_SF9;

  // Start job (sending automatically starts OTAA too)
  do_send(&sendjob);
}

void loop() {
  os_runloop_once();
}

//Voltage
int getAccurateVcc() {
  getVcc();
  return getVcc();
}

int getVcc(void) {

  int Vcc;

  ADCSRA = (1 << ADEN);
  ADCSRA |= (1 << ADPS0) | (1 << ADPS1) | (1 << ADPS2);
  ADMUX = (1 << REFS0) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1);
  delay(1);  // wait for ADC and VREF to settle

  ADCSRA |= (1 << ADSC);  // start conversion
  while (bit_is_set(ADCSRA, ADSC))
    ;  // wait until done
  Vcc = ADC;

  //1126400 = 1.1*1024*1000 = 1126400UL
  Vcc = 1126400UL / (unsigned long)Vcc;
  return Vcc;  // Vcc in millivolts
}