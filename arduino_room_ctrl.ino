#include <IRremote.h>
#include <SPI.h>
#include <Ethernet.h>
#include <wiring_private.h>
#include <utility/socket.h>

/* pin confiration:
    pin 2: PIR sensor, configured as input
    pins 3, 5, 6: b, g, r color control channels, configured as output
    pin 7: white lights relay control
    pin 4: rgb lights relay control
*/

//IR remote control
#define IR_TX 3
#define FAN_LIGHTS_OFF 0
#define FAN_LIGHTS_ON 1
#define FAN_OFF 2
#define FAN_LOW 3
#define FAN_MED 4
#define FAN_HIGH 5
#define SEND_FAN_CODE(code_index) irsend.sendRaw(rawCodes[code_index], FAN_CODE_LENGTH, 38)
unsigned int rawCodes[6][24] = {0xFFA25D, 0xFFE21D, 0xFFE01F, 0xFFE01F, 0xFF18E7, 0xFF5AA5};
IRsend irsend;

//network stuff, load config from EEPROM, which looks like:
byte imac[] = {0xC8, 0xE0, 0xEB, 0x3F, 0x29, 0x7B};
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xFE};
byte ip[] = {192, 168, 1, 79};
EthernetServer server(80);

//setup
void setup()
{
  DDRD |= 0xf8; //set 7, 6, 5, 4, 3 as output
  PORTD |= 0x90; //set 7, 4 as HIGH

  //5:OCR0A, 6:OCR0B, 3: OCR2A
  TCCR0A = TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
  TCCR0B = TCCR2B = _BV(CS22);
  OCR0A = 255; //green maximum
  OCR0B = 255; //red maximum
  OCR2A = 255; //blue maximum

  Ethernet.begin(mac, ip);
  server.begin();
}

/*sample request is:
  GET /abc HTTP/1.1
  longest string is going to be GET /f255 HTTP/1.1 */
#define BUFFER_SIZE 20

typedef struct
{
  unsigned int now:1;
  unsigned int before:1;
} PIRState;

void loop()
{
  static PIRState presence = {0, 0};

  //everything arduino-controlled is enabled only with presence.
  //sernarios: no presence -> presence: open lights, and enable web server
  //           presence -> presence   : keep web serving only
  //           presence -> no presence: kill lights, fan, etc
  presence.now = (PIND & B00000100 ? 1 : 0);
  if (presence.before == 0 && presence.now == 1)
  {
      PORTD |= 0x90; //7,4->HIGH
      SendWOLMagicPacket(imac); //wake up mac

      /// \todo When reopening lights we have to remember last state
      /// \todo Different actions for day and night
  }
  else if (presence.before == 1 && presence.now == 0)
  {
      PORTD &= 0x6f; //7,4->LOW
      /// \TODOkill fan
  }
  if (presence.now == 1)
  {
      EthernetClient client = server.available();
      /// \todo Read directly
      if (client)
      {
          char buffer[BUFFER_SIZE];
          int offset;

          offset = 0;

          while (client.connected())
            if (client.available())
            {
              buffer[offset] = client.read();
              if ((buffer[offset] == '\n') || (offset == BUFFER_SIZE - 2))
              {
                buffer[++offset] = '\0';
                break;
              }
              ++offset;
            }

          //switch() is expensive :(
          if       (buffer[5] == '1') PORTD &= 0x7f; //turn off white lights
          else if  (buffer[5] == '2') PORTD |= 0x80; //turn on white lights
          else if  (buffer[5] == '3') PORTD &= 0xef; //turn off rgb lights
          else if  (buffer[5] == '4') PORTD |= 0x10; //turn on rgb lights
          else if  (buffer[5] == '5') SEND_FAN_CODE(FAN_LIGHTS_OFF); // turn off fan lights
          else if  (buffer[5] == '6') SEND_FAN_CODE(FAN_LIGHTS_ON); // turn off fan lights
          else if  (buffer[5] == 'r') OCR0B = 17*hex2dec(buffer[6]); //set red intensity
          else if  (buffer[5] == 'g') OCR0A = 17*hex2dec(buffer[6]); //set green intensity
          else if  (buffer[5] == 'b') OCR2A = 17*hex2dec(buffer[6]); //set blue intensity
          else if  (buffer[5] == 'o') SEND_FAN_CODE(FAN_OFF); //turn off fan
          else if  (buffer[5] == 's') SEND_FAN_CODE(FAN_LOW); //slow fan speed
          else if  (buffer[5] == 'm') SEND_FAN_CODE(FAN_MED); //medium fan speed
          else if  (buffer[5] == 'h') SEND_FAN_CODE(FAN_HIGH); //high fan speed

          client.stop();
      }
  }
  presence.before = presence.now;
}

void SendWOLMagicPacket(byte * pMacAddress)
{
  // The magic packet data sent to wake the remote machine. Target machine's
  // MAC address will be composited in here.
  const int nMagicPacketLength = 102;
  byte abyMagicPacket[nMagicPacketLength] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  byte abyTargetIPAddress[] = { 255, 255, 255, 255 }; // don't seem to need a real ip address.
  const int nWOLPort = 7;
  const int nLocalPort = 8888; // to "listen" on (only needed to initialize udp)

  // Compose magic packet to wake remote machine.
  for (int i=6; i < 102; i++)
    abyMagicPacket[i]=pMacAddress[i%6];

  if (UDP_RawSendto(abyMagicPacket, nMagicPacketLength, nLocalPort,
  abyTargetIPAddress, nWOLPort) != nMagicPacketLength)
    Serial.println("Error sending WOL packet");
}

int UDP_RawSendto(byte* pDataPacket, int nPacketLength, int nLocalPort, byte* pRemoteIP, int nRemotePort)
{
  int nResult;
  int nSocketId; // Socket ID for Wiz5100

  // Find a free socket id.
  nSocketId = MAX_SOCK_NUM;
  for (int i = 0; i < MAX_SOCK_NUM; i++)
  {
    uint8_t s = W5100.readSnSR(i);
    if (s == SnSR::CLOSED || s == SnSR::FIN_WAIT)
	  {
      nSocketId = i;
      break;
    }
  }

  if (nSocketId == MAX_SOCK_NUM)
    return 0; // couldn't find one.

  if (socket(nSocketId, SnMR::UDP, nLocalPort, 0))
  {
    nResult = sendto(nSocketId,(unsigned char*)pDataPacket,nPacketLength,(unsigned char*)pRemoteIP,nRemotePort);
    close(nSocketId);
  } else
    nResult = 0;

  return nResult;
}

int hex2dec(char hex)
{
    if(hex>='0' && hex<='9')    return (hex-'0');
    if(hex >='A' && hex <='F')  return (hex-55);
    if(hex >='a' && hex <='f')  return (hex-87);
}
