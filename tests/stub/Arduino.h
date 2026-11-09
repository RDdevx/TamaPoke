#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
using std::min; using std::max;
static inline long random(long n){return rand()%n;}
static inline uint32_t millis(){return 0;}
struct SerialStub{template<class...A>void printf(const char*,A...){} void println(const char*){}};
static SerialStub Serial;
