/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// A host stand-in for the Arduino core and the PicoCalc libraries that
// ulisp-picocalc.ino includes, enough to run the unmodified interpreter as
// the reference for the language corpus: the serial port is the standard
// streams, the display, keyboard, pins and buses do nothing, and the SD
// card is the current directory.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <dirent.h>
typedef unsigned int uint;
#define PSTR(s) (s)
#define PROGMEM
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define HIGH 1
#define LOW 0
#define LED_BUILTIN 25
#define MSBFIRST 1
#define LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define SPI_HALF_SPEED 0
#define TFT_BLACK 0
#define TFT_WHITE 0xffff
#define TFT_GREEN 0x07e0
#define TFT_DISPOFF 0x28
#define TFT_DISPON 0x29
#define GPIO_FUNC_PWM 4
#define WL_CONNECTED 3
#define O_READ O_RDONLY
#define O_WRITE O_WRONLY
#define tee ulisp_tee
typedef int BitOrder;
#define clk_sys 0
static inline unsigned clock_get_hz(int){ return 125000000u; }
static inline unsigned long micros(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000ul+t.tv_nsec/1000; }
static inline unsigned long millis(void){ return micros()/1000; }
static inline void delay(unsigned long){ }
static inline void pinMode(int,int){} static inline void digitalWrite(int,int){} static inline int digitalRead(int){return 0;}
static inline int analogRead(int){return 0;} static inline void analogWrite(int,int){}
static inline void analogReadResolution(int){} static inline void analogWriteResolution(int){}
static inline long random(long n){ return n ? rand()%n : 0; } static inline void randomSeed(unsigned long){}
struct HardwareSerial {
  void begin(int){} void end(){} void flush(){ fflush(stdout); }
  operator bool(){ return true; }
  int available(){ return 0; } int avail_in(){ return 1; }
  int read(){ int c = getchar(); if (c == EOF) { fflush(stdout); exit(0); } return c; }
  void write(char c){ putchar(c); } void print(char c){ (void)c; }
};
static HardwareSerial Serial1, Serial2;
struct TwoWire { void end(){} void setSDA(int){} void setSCL(int){} void begin(){} void setClock(int){}
  int read(){return 0;} void write(uint8_t){} void beginTransmission(uint8_t){} int endTransmission(bool=true){return 0;}
  int requestFrom(uint8_t,int,bool=true){return 0;} };
static TwoWire Wire, Wire1;
struct SPISettings { SPISettings(unsigned long,int,int){} };
struct SPIClass { int transfer(int){return 0;} void begin(){} void beginTransaction(SPISettings){} void endTransaction(){} };
static SPIClass SPI, SPI1;
struct PCKeyboard { enum State { StateIdle, StatePress }; struct KeyEvent { char key; State state; };
  int keyCount(){return 0;} KeyEvent keyEvent(){ KeyEvent k = {0, StateIdle}; return k; } void begin(int, TwoWire*){} };
struct TFT_eSPI { TFT_eSPI(int,int){}
  void init(){} void writecommand(int){} void invertDisplay(int){} void fillScreen(int){}
  void drawPixel(int,int,int){} void drawLine(int,int,int,int,int){} void drawRect(int,int,int,int,int){} void fillRect(int,int,int,int,int){}
  void drawCircle(int,int,int,int){} void fillCircle(int,int,int,int){} void drawRoundRect(int,int,int,int,int,int){} void fillRoundRect(int,int,int,int,int,int){}
  void drawTriangle(int,int,int,int,int,int,int){} void fillTriangle(int,int,int,int,int,int,int){}
  void drawChar(int,int,int,int,int,int){} void setCursor(int,int){} void setTextColor(int,int=0){} void setTextSize(int){} void setTextWrap(bool){}
  void setRotation(int){} int readPixel(int,int){return 0;} void write(char){} };
struct File { FILE *f = 0; DIR *d = 0; char nm[64] = "";
  operator bool(){ return f || d; }
  size_t write(uint8_t c){ return fputc(c, f) == EOF ? 0 : 1; } size_t write(const uint8_t *b, size_t n){ return fwrite(b,1,n,f); }
  int read(){ int c = fgetc(f); return c; } int read(uint8_t *b, size_t n){ return fread(b,1,n,f); }
  int available(){ int c = fgetc(f); if (c == EOF) return 0; ungetc(c,f); return 1; }
  void close(){ if (f) fclose(f); f = 0; if (d) closedir(d); d = 0; }
  File openNextFile(){ File e; struct dirent *de; while (d && (de = readdir(d))) { if (de->d_name[0]=='.') continue; e.f = stdin; strncpy(e.nm, de->d_name, 63); return e; } return e; }
  const char *name(){ return nm; } bool isDirectory(){ return false; } };
struct FSClass { bool begin(...){return true;} void end(){}
  File open(const char *n, int fl = O_RDONLY){ File x; if (n[0]=='/' && !n[1]) { x.d = opendir("."); return x; } if (n[0]=='/') n++; x.f = fopen(n, (fl&O_APPEND)?"a":(fl&(O_WRONLY|O_RDWR))?"w":"r"); return x; }
  File open(const char *n, const char *m){ File x; if (n[0]=='/') n++; x.f = fopen(n,m); return x; } };
static FSClass SD, LittleFS;
struct pwm_config { int x; };
static inline void gpio_set_function(int,int){} static inline uint pwm_gpio_to_slice_num(int){return 0;}
static inline pwm_config pwm_get_default_config(){ pwm_config c = {0}; return c; }
static inline void pwm_config_set_clkdiv(pwm_config*, float){} static inline void pwm_config_set_wrap(pwm_config*, int){}
static inline void pwm_init(uint, pwm_config*, bool){} static inline void pwm_set_gpio_level(int,int){}
#define bitRead(v,b) (((v)>>(b))&1)
#define SIO_BASE 0xd0000000u
#define SIO_GPIO_IN_OFFSET 0x04
#define SIO_GPIO_OUT_OFFSET 0x10
#define SIO_GPIO_OUT_SET_OFFSET 0x14
#define SIO_GPIO_OUT_CLR_OFFSET 0x18
#define SIO_GPIO_OUT_XOR_OFFSET 0x1c
#define SIO_GPIO_OE_OFFSET 0x20
#define SIO_GPIO_OE_SET_OFFSET 0x24
#define SIO_GPIO_OE_CLR_OFFSET 0x28
#define SIO_GPIO_OE_XOR_OFFSET 0x2c
