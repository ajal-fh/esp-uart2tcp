
#define RXp2 16
#define TXp2 17

HardwareSerial Receiver(2);

void setup() {
  Serial.begin(115200);
  Receiver.begin(9600, SERIAL_8N1, RXp2, TXp2);

}

void loop() {
  if(Receiver.available()){
    char read_string = Receiver.read();
    Serial.print(read_string);
    //Serial.println("in receiving loop");
  }
  
}
