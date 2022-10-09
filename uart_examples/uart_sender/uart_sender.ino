
#define RXp2 16
#define TXp2 17

HardwareSerial Sender(2);

void setup() {
  Serial.begin(115200);
  Sender.begin(115200, SERIAL_8N1, RXp2, TXp2);

}

void loop() {
  while(Sender.available()){
    Serial.println("in loop");
    Sender.write("Hello world");
    delay(1000);
  }
  
}
