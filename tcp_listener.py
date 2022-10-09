# This example code is in the Public Domain (or CC0 licensed, at your option.)

# Unless required by applicable law or agreed to in writing, this
# software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied.

# -*- coding: utf-8 -*-

from __future__ import print_function, unicode_literals

import os
import re
import socket
import sys


# -----------  Config  ----------
PORT = 3333
# -------------------------------


def tcp_client(address):
    print("called")
    for res in socket.getaddrinfo(address, PORT, socket.AF_UNSPEC,
                                  socket.SOCK_STREAM, 0, socket.AI_PASSIVE):
        family_addr, socktype, proto, canonname, addr = res
    try:
        sock = socket.socket(family_addr, socket.SOCK_STREAM)
        sock.settimeout(60.0)
    except socket.error as msg:
        print('Could not create socket: ' + str(msg[0]) + ': ' + msg[1])
        raise
    try:
        sock.connect(addr)
    except socket.error as msg:
        print('Could not open socket: ', msg)
        sock.close()
        raise
    
    try:
        while True:
            print("inside")
            data = sock.recv(1024)
            if data:
                #val = int.from_bytes(data, 'little')
                val = data.decode()
                print(f'Recieved : {val}')
    except KeyboardInterrupt:
        print("ending recieve")
    sock.close()
    




if __name__ == '__main__':
    if sys.argv[1:]:    # if two arguments provided:
        # Usage: example_test.py <server_address> <message_to_send_to_server>
        tcp_client(sys.argv[1])
    
    else:
        print("unknown arguments")