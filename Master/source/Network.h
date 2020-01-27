#pragma once
#include "Globals.h"

void recvSlaveResponces(std::vector<Slave*>* slaveVector, bool* EOF_Flag_reader, bool* EOF_Flag_parser);
void NetworkHandler(std::pair<std::pair<char*, bool>, std::pair<char*, bool>>* inputBuffer, bool* EOF_Flag_reader, std::vector<Slave*>* slaveVector, bool* EOF_Flag_parser);
void initSockets(std::vector<std::string>* IPVector, std::vector<std::string>* ratio, std::vector<Slave*>* slaveVector,
                 bool useLocal, uint8_t slaves, int port);

// Send options string to the slave specified by id
bool SendOptions(SOCKET socket, std::string* sliceOptions, uint32_t optLength);