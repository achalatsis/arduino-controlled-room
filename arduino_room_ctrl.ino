#include <IRremote.h>
#include <SPI.h>
#include <Ethernet.h>
#include <wiring_private.h>
#include <utility/socket.h>

/* pin confiration:
    pin 2: PIR sensor, configured as input
    pins 5, 6, 9: g, r, b color control channels, configured as output
    pin 7: white lights relay control
    pin 8: rgb lights relay control
*/

//IR remote control
#define IR_TX 3
#define FAN_LIGHTSOFF 0
#define FAN_LIGHTSON 1
#define FAN_OFF 2
#define FAN_LOW 3
#define FAN_MED 4
#define FAN_HIGH 5
#define FAN_CODE_LENGTH 24
#define SEND_FAN_CODE(code_index) irsend.sendRaw(rawCodes[code_index], FAN_CODE_LENGTH, 38)
//unsigned int rawCodes[6][24];
//IRsend irsend;

//network stuff, load config from EEPROM, which looks like:
byte imac[] = {0xC8, 0xE0, 0xEB, 0x3F, 0x29, 0x7B};
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xFE};
byte ip[] = {192, 168, 1, 79};
EthernetServer server(80);

//setup
void setup()
{
  DDRD = DDRD | B11100000; //set 7, 6, 5 as output, rest as input
  DDRB = DDRB | B00000011; //set 8, 9 as output, rest as input

  PORTD = PORTD | B11100000; //set 7 as HIGH, rest as LOW
  PORTB = PORTB | B00000011; //set 8 as HIGH, rest as LOW

  Ethernet.begin(mac, ip);
  server.begin();
  
  //5:OCR0A, 6:OCR0B, 9: OCR1A
  //TCCR0A = TCCR1A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
  //TCCR0B = TCCR1B = _BV(CS22);
  //OCR0A = 255;
  //OCR0B = 5;
  //OCR1A = 5;
  analogWrite(5, 255);
  analogWrite(6, 1);
  analogWrite(9, 255);
}  
  
/*sample request is:
  GET /abc HTTP/1.1
  longest string is going to be GET /f255,255,255 HTTP/1.1 */
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
      PORTD |= 0b10000000; //7->HIGH
      PORTB |= 0b00000011; //8->HIGH
      SendWOLMagicPacket(imac); //wake up mac
  }
  else if (presence.before == 1 && presence.now == 0)
  {
      PORTD &= 0b01111111; //7->LOW
      PORTB &= 0b11111110; //8->LOW
      //kill fan
  }
  if (presence.now == 1)
  {
      EthernetClient client = server.available();
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
          if       (buffer[5] == '1') PORTD &= 0b01111111; //turn off white lights
          else if  (buffer[5] == '2') PORTD |= 0b10000000; //turn on white lights
          else if  (buffer[5] == '3') PORTB &= 0b11111110; //turn off rgb lights
          else if  (buffer[5] == '4') PORTB |= 0b00000001; //turn on rgb lights
          else if  (buffer[5] == '5') ; // turn off fan
          else if  (buffer[5] == '6') ; // turn off fan
          else if  (buffer[5] == 'r') ; //set red intensity
          else if  (buffer[5] == 'g') ; //set green intensity
          else if  (buffer[5] == 'b') ; //set blue intensity
          else if  (buffer[5] == 's') ; //slow fan speed
          else if  (buffer[5] == 'm') ; //medium fan speed
          else if  (buffer[5] == 'h') ; //high fan speed
             
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
