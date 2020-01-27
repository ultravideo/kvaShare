#pragma once

#include "Globals.h"

void Reader(std::vector<Slave*>* slaveVector, std::pair<std::pair<char*, bool>, std::pair<char*, bool>>* inputBuffer, bool* EOF_Flag_reader);