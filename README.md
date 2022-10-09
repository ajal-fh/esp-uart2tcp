#### UART2TCP

send incoming uart data over TCP socket 

The uart data is collected for the specified tcp packet data size. 
Once the packet is completly received, it is send via TCP socket to clients. 
If there are no TCP clients connected to the esp32 access point, then the packet is dropped.

#### Note

- for building and flashing, kindly refer (esp-idf)[https://docs.espressif.com/projects/esp-idf/en/v4.4.2/esp32/get-started/index.html]
- uart2 of esp32 is used here. 
- gpio pin 16, 17 are used here.
- the `uart_examples` folder contains simple sender and reciever code for debugging.
- calling `tcp_listener.py 192.168.4.1` listens for tcp packets on port 3333, which can be used as a client for the esp32.

