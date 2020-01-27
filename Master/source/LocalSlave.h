#pragma once

#include "Globals.h"

void initLocal(Slave* slave, int threads, uint8_t multi);
void getBuffer(const char* buffer, Slave* slave);
void LocalSlave(Slave* slave, bool* EOF_Flag_parser);
