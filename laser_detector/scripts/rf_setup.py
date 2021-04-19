import pigpio
import time
import struct

from nrf24 import *

pi = pigpio.pi("127.0.0.1", 8888)

if not pi.connected:
	print("Couldn't connect to Raspberry Pi daemon, exiting...")
	exit()

send_address = b'\xAA\x44\x33\x22\x11'

nrf = NRF24(pi, ce=25, payload_size=RF24_PAYLOAD.DYNAMIC, channel=52, data_rate=RF24_DATA_RATE.RATE_2MBPS, pa_level=RF24_PA.MAX)

nrf.set_address_bytes(len(send_address))
nrf.set_retransmission(15, 15)


nrf.open_writing_pipe(send_address)

time.sleep(1)

nrf.reset_packages_lost()

print("About to send:")
nrf._nrf_write_reg(NRF24.DYNPD, 0x3F)
nrf._nrf_write_reg(NRF24.FEATURE, 0x6)
nrf.show_registers()

payload = b'From Josh\x00'
nrf.send(payload)

time.sleep(1)

if nrf.get_packages_lost() == 1:
	print("1 NOT RECEIVED")
else:
	print("Works!")

print("After sending:")
nrf.show_registers()

