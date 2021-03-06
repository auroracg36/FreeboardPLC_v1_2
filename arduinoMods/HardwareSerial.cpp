/*
  HardwareSerial.cpp - Hardware serial library for Wiring
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
  
  Modified 23 November 2006 by David A. Mellis
  Modified 28 September 2010 by Mark Sproul
  Modified 14 August 2012 by Alarus
  Modified 3 December 2013 by Matthijs Kooijman
  Modified 11 September 2014 by Bouni (Added 9bit serial support)
  Modified 20 October 2016 by aayaffe@github (merged with arduino core 1.6.14)
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "Arduino.h"

#include "HardwareSerial.h"
#include "HardwareSerial_private.h"

// this next line disables the entire HardwareSerial.cpp, 
// this is so I can support Attiny series and any other chip without a uart
#if defined(HAVE_HWSERIAL0) || defined(HAVE_HWSERIAL1) || defined(HAVE_HWSERIAL2) || defined(HAVE_HWSERIAL3)

// SerialEvent functions are weak, so when the user doesn't define them,
// the linker just sets their address to 0 (which is checked below).
// The Serialx_available is just a wrapper around Serialx.available(),
// but we can refer to it weakly so we don't pull in the entire
// HardwareSerial instance if the user doesn't also refer to it.
#if defined(HAVE_HWSERIAL0)
  void serialEvent() __attribute__((weak));
  bool Serial0_available() __attribute__((weak));
#endif

#if defined(HAVE_HWSERIAL1)
  void serialEvent1() __attribute__((weak));
  bool Serial1_available() __attribute__((weak));
#endif

#if defined(HAVE_HWSERIAL2)
  void serialEvent2() __attribute__((weak));
  bool Serial2_available() __attribute__((weak));
#endif

#if defined(HAVE_HWSERIAL3)
  void serialEvent3() __attribute__((weak));
  bool Serial3_available() __attribute__((weak));
#endif

void serialEventRun(void)
{
#if defined(HAVE_HWSERIAL0)
  if (Serial0_available && serialEvent && Serial0_available()) serialEvent();
#endif
#if defined(HAVE_HWSERIAL1)
  if (Serial1_available && serialEvent1 && Serial1_available()) serialEvent1();
#endif
#if defined(HAVE_HWSERIAL2)
  if (Serial2_available && serialEvent2 && Serial2_available()) serialEvent2();
#endif
#if defined(HAVE_HWSERIAL3)
  if (Serial3_available && serialEvent3 && Serial3_available()) serialEvent3();
#endif
}

// Actual interrupt handlers //////////////////////////////////////////////////////////////

void HardwareSerial::_tx_udr_empty_irq(void)
{
  // If interrupts are enabled, there must be more data in the output
  // buffer. Send the next byte
  
  if(bit_is_set(*_ucsrb, UCSZ02)) {
    // If Uart is configured for 9 bit mode
    unsigned char mb = _tx_buffer[_tx_buffer_tail];
    unsigned char c = _tx_buffer[_tx_buffer_tail + 1];
    _tx_buffer_tail = (_tx_buffer_tail + 2) % SERIAL_TX_BUFFER_SIZE;
    if(mb & 0x01) {
      sbi(*_ucsrb, TXB80);
    } else {
      cbi(*_ucsrb, TXB80);
    }
    *_udr = c;
  } else {
    // UART is configured for 5 to 8 bit modes 
    unsigned char c = _tx_buffer[_tx_buffer_tail];
    _tx_buffer_tail = (_tx_buffer_tail + 1) % SERIAL_TX_BUFFER_SIZE;

    *_udr = c;
  }

  // clear the TXC bit -- "can be cleared by writing a one to its bit
  // location". This makes sure flush() won't return until the bytes
  // actually got written
  sbi(*_ucsra, TXC0);

  if (_tx_buffer_head == _tx_buffer_tail) {
    // Buffer empty, so disable interrupts
    cbi(*_ucsrb, UDRIE0);
  }
}

// Public Methods //////////////////////////////////////////////////////////////

void HardwareSerial::begin(unsigned long baud, uint16_t config)
{
  // Try u2x mode first
  uint16_t baud_setting = (F_CPU / 4 / baud - 1) / 2;
  *_ucsra = 1 << U2X0;

  // hardcoded exception for 57600 for compatibility with the bootloader
  // shipped with the Duemilanove and previous boards and the firmware
  // on the 8U2 on the Uno and Mega 2560. Also, The baud_setting cannot
  // be > 4095, so switch back to non-u2x mode if the baud rate is too
  // low.
  if (((F_CPU == 16000000UL) && (baud == 57600)) || (baud_setting >4095))
  {
    *_ucsra = 0;
    baud_setting = (F_CPU / 8 / baud - 1) / 2;
  }

  // assign the baud_setting, a.k.a. ubrr (USART Baud Rate Register)
  *_ubrrh = baud_setting >> 8;
  *_ubrrl = baud_setting;

  _written = false;

  //set the data bits, parity, and stop bits
#if defined(__AVR_ATmega8__)
  config |= 0x80; // select UCSRC register (shared with UBRRH)
#endif

  if(config & 0x100) {
    sbi(*_ucsrb, UCSZ02);
  }
  *_ucsrc = (uint8_t) config;

  sbi(*_ucsrb, RXEN0);
  sbi(*_ucsrb, TXEN0);
  sbi(*_ucsrb, RXCIE0);
  cbi(*_ucsrb, UDRIE0);
}

void HardwareSerial::end()
{
  // wait for transmission of outgoing data
  flush();

  cbi(*_ucsrb, RXEN0);
  cbi(*_ucsrb, TXEN0);
  cbi(*_ucsrb, RXCIE0);
  cbi(*_ucsrb, UDRIE0);
  
  // clear any received data
  _rx_buffer_head = _rx_buffer_tail;
}

int HardwareSerial::available(void)
{
  unsigned int a = (unsigned int) (SERIAL_RX_BUFFER_SIZE + _rx_buffer_head - _rx_buffer_tail) % SERIAL_RX_BUFFER_SIZE;
  if(bit_is_set(*_ucsrb, UCSZ02)) {
    // If Uart is in 9 bit mode return only the half, because we use two bytes per 9 bit "byte".
    return a / 2;
  }
  else {
    // For 5 - 8 bit modes simply return the number
    return a;
  }
}

int HardwareSerial::peek(void)
{
  if (_rx_buffer_head == _rx_buffer_tail) {
    return -1;
  } else {
    if(bit_is_set(*_ucsrb, UCSZ02)) {
      // If Uart is in 9 bit mode read two bytes and merge them
      return (_rx_buffer[_rx_buffer_tail] << 8) | _rx_buffer[_rx_buffer_tail + 1 % SERIAL_RX_BUFFER_SIZE];
    } else {
      return _rx_buffer[_rx_buffer_tail];
    }
  }
}

int HardwareSerial::read(void)
{
  // if the head isn't ahead of the tail, we don't have any characters
  if (_rx_buffer_head == _rx_buffer_tail) {
    return -1;
  } else {
    if(bit_is_set(*_ucsrb, UCSZ02)) {
      // If Uart is in 9 bit mode read two bytes and merge them
      unsigned char mb = _rx_buffer[_rx_buffer_tail];
      unsigned char c = _rx_buffer[_rx_buffer_tail + 1];
      _rx_buffer_tail = (rx_buffer_index_t)(_rx_buffer_tail + 2) % SERIAL_RX_BUFFER_SIZE;
      return ((mb << 8) | c);
    } else {
      unsigned char c = _rx_buffer[_rx_buffer_tail];
      _rx_buffer_tail = (rx_buffer_index_t)(_rx_buffer_tail + 1) % SERIAL_RX_BUFFER_SIZE;
      return c;
    }
  }
}

int HardwareSerial::availableForWrite(void)
{
#if (SERIAL_TX_BUFFER_SIZE>256)
  uint8_t oldSREG = SREG;
  cli();
#endif
  tx_buffer_index_t head = _tx_buffer_head;
  tx_buffer_index_t tail = _tx_buffer_tail;
#if (SERIAL_TX_BUFFER_SIZE>256)
  SREG = oldSREG;
#endif
  if (head >= tail) return SERIAL_TX_BUFFER_SIZE - 1 - head + tail;
  return tail - head - 1;
}

void HardwareSerial::flush()
{
  // If we have never written a byte, no need to flush. This special
  // case is needed since there is no way to force the TXC (transmit
  // complete) bit to 1 during initialization
  if (!_written)
    return;

  while (bit_is_set(*_ucsrb, UDRIE0) || bit_is_clear(*_ucsra, TXC0)) {
    if (bit_is_clear(SREG, SREG_I) && bit_is_set(*_ucsrb, UDRIE0))
	// Interrupts are globally disabled, but the DR empty
	// interrupt should be enabled, so poll the DR empty flag to
	// prevent deadlock
	if (bit_is_set(*_ucsra, UDRE0))
	  _tx_udr_empty_irq();
  }
  // If we get here, nothing is queued anymore (DRIE is disabled) and
  // the hardware finished tranmission (TXC is set).
}

size_t HardwareSerial::write(uint16_t c)
{
  // If the buffer and the data register is empty, just write the byte
  // to the data register and be done. This shortcut helps
  // significantly improve the effective datarate at high (>
  // 500kbit/s) bitrates, where interrupt overhead becomes a slowdown.
  if (_tx_buffer_head == _tx_buffer_tail && bit_is_set(*_ucsra, UDRE0)) {
    if(bit_is_set(*_ucsrb, UCSZ02)) {
      // in 9 bit mode set TXB8 bit if necessary
      if(c & 0x100) {
        sbi(*_ucsrb, TXB80);
      } else {
        cbi(*_ucsrb, TXB80);
      }    
    }
    *_udr = (uint8_t) c;
    sbi(*_ucsra, TXC0);
    return 1;
  }
  
  tx_buffer_index_t i;

  if(bit_is_set(*_ucsrb, UCSZ02)) {
    i = ((_tx_buffer_head + 2) % SERIAL_TX_BUFFER_SIZE);
  } else {
    i = ((_tx_buffer_head + 1) % SERIAL_TX_BUFFER_SIZE);
  }
  // If the output buffer is full, there's nothing for it other than to 
  // wait for the interrupt handler to empty it a bit
  while (i == _tx_buffer_tail) {
    if (bit_is_clear(SREG, SREG_I)) {
      // Interrupts are disabled, so we'll have to poll the data
      // register empty flag ourselves. If it is set, pretend an
      // interrupt has happened and call the handler to free up
      // space for us.
      if(bit_is_set(*_ucsra, UDRE0))
	_tx_udr_empty_irq();
    } else {
      // nop, the interrupt handler will free up space for us
    }
  }


  if(bit_is_set(*_ucsrb, UCSZ02)) {
    _tx_buffer[_tx_buffer_head] = (uint8_t) (c >> 8) & 0x01;
    _tx_buffer[_tx_buffer_head + 1] = (uint8_t) c;
  } else {
    _tx_buffer[_tx_buffer_head] = (uint8_t) c;
  }
  _tx_buffer_head = i;
	
  sbi(*_ucsrb, UDRIE0);
  _written = true;
  
  return 1;
}
//Necessary only to maintain compatibility to FreeBoardPLC
size_t HardwareSerial::write9(uint8_t n, bool c)
{
	return write((c<<8) | n);
}


#endif // whole file
