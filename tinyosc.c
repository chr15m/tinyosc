/**
 * Copyright (c) 2015, Martin Roth (mhroth@gmail.com)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <netinet/in.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "tinyosc.h"

// http://opensoundcontrol.org/spec-1_0
int tosc_read(tosc_tinyosc *o, const char *buffer, const int len) {
  // extract the address
  o->address = buffer; // address string is null terminated
  // NOTE(mhroth): if there's a comma in the address, that's weird

  int i = 0;
  while (i < len && buffer[i] != ',') ++i; // find the format comma
  // TODO(mhroth): does not check for null terminated address string
  if (i == len) return -1; // error while looking for format string
  // format string is null terminated
  o->format = buffer + i + 1; // format starts after comma

  while (i < len && buffer[i] != '\0') ++i;
  if (i == len) return -2; // format string not null terminated

  i = (i + 4) & ~0x3; // advance to the next multiple of 4 after trailing '\0'
  o->marker = buffer + i;

  o->buffer = buffer;
  o->len = len;

  return 0;
}

int32_t tosc_getNextInt32(tosc_tinyosc *o) {
  // convert from big-endian (network btye order)
  const int32_t i = (int32_t) ntohl(*((uint32_t *) o->marker));
  o->marker += 4;
  return i;
}

float tosc_getNextFloat(tosc_tinyosc *o) {
  // convert from big-endian (network btye order)
  const uint32_t i = ntohl(*((uint32_t *) o->marker));
  o->marker += 4;
  return *((float *) (&i));
}

const char *tosc_getNextString(tosc_tinyosc *o) {
  int i = (int) strlen(o->marker);
  if (o->marker + i >= o->address + o->len) return NULL;
  const char *s = o->marker;
  i = (i + 4) & ~0x3; // advance to next multiple of 4 after trailing '\0'
  o->marker += i;
  return s;
}

void tosc_getNextBlob(tosc_tinyosc *o, const char **buffer, int *len) {
  int i = (int) ntohl(*((uint32_t *) o->marker)); // get the blob length
  if (o->marker + 4 + i <= o->buffer + o->len) {
    *len = i; // length of blob
    *buffer = o->marker + 4;
    i = (i + 7) & ~0x3;
    o->marker += i;
  } else {
    *len = 0;
    *buffer = NULL;
  }
}

int tosc_write(char *buffer, const int len,
    const char *address, const char *format, ...) {
  va_list ap;
  va_start(ap, format);

  memset(buffer, 0, len); // clear the buffer
  int i = (int) strlen(address);
  if (address == NULL || i >= len) return -1;
  strcpy(buffer, address);
  i = (i + 4) & ~0x3;
  buffer[i++] = ',';
  int s_len = (int) strlen(format);
  if (format == NULL || (i + s_len) >= len) return -2;
  strcpy(buffer+i, format);
  i = (i + 4 + s_len) & ~0x3;

  for (int j = 0; format[j] != '\0'; ++j) {
    switch (format[j]) {
      case 'b': {
        const uint32_t n = (uint32_t) va_arg(ap, int); // length of blob
        if (i + 4 + n > len) return -3;
        char *b = (char *) va_arg(ap, void *); // pointer to binary data
        *((uint32_t *) (buffer+i)) = htonl(n); i += 4;
        memcpy(buffer+i, b, n);
        i = (i + 3 + n) & ~0x3;
        break;
      }
      case 'f': {
        if (i + 4 > len) return -3;
        const float f = (float) va_arg(ap, double);
        *((uint32_t *) (buffer+i)) = htonl(*((uint32_t *) &f));
        i += 4;
        break;
      }
      case 'i': {
        if (i + 4 > len) return -3;
        const uint32_t k = (uint32_t) va_arg(ap, int);
        *((uint32_t *) (buffer+i)) = htonl(k);
        i += 4;
        break;
      }
      case 's': {
        const char *str = (const char *) va_arg(ap, void *);
        s_len = (int) strlen(str);
        if (i + s_len >= len) return -3;
        strcpy(buffer+i, str);
        i = (i + 4 + s_len) & ~0x3;
        break;
      }
      case 'T': // true
      case 'F': // false
      case 'N': // nil
      case 'I': // infinitum
          break;
      default: return -4; // unknown type
    }
  }

  va_end(ap);
  return i; // return the total number of bytes written
}

void tosc_printOscBuffer(const char *buffer, const int len) {
  // parse the buffer contents (the raw OSC bytes)
  // a return value of 0 indicates no error
  tosc_tinyosc osc;
  const int err = tosc_read(&osc, buffer, len);
  if (err == 0) {
    printf("[%i bytes] %s %s",
        len, // the number of bytes in the OSC message
        osc.address, // the OSC address string, e.g. "/button1"
        osc.format); // the OSC format string, e.g. "f"

    for (int i = 0; osc.format[i] != '\0'; i++) {
      switch (osc.format[i]) {
        case 'b': {
          const char *b = NULL; // will point to binary data
          int n = 0; // takes the length of the blob
          tosc_getNextBlob(&osc, &b, &n);
          printf(" [%i]", n); // print length of blob
          for (int j = 0; j < n; j++) printf("%02X", b[j] & 0xFF); // print blob bytes
          break;
        }
        case 'f': printf(" %g", tosc_getNextFloat(&osc)); break;
        case 'i': printf(" %i", tosc_getNextInt32(&osc)); break;
        case 's': printf(" %s", tosc_getNextString(&osc)); break;
        case 'F': printf(" false"); break;
        case 'I': printf(" inf"); break;
        case 'N': printf(" nil"); break;
        case 'T': printf(" true"); break;
        default: printf(" Unknown format: '%c'", osc.format[i]); break;
      }
    }
    printf("\n");
  } else {
    printf("Error while reading OSC buffer: %i\n", err);
  }
}
