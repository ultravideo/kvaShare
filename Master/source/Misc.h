#pragma once

#include "Globals.h"
#include <winsock.h>

// Splits the given string on every found mark and returns vector of split strings
std::vector<std::string> Split(std::string line, char mark);
// Checks if the given data is formed according to the given form
bool ValidateData(std::vector<std::string> data, std::string form, bool IPTest);
// Calls shutdown on the given socket and then closes it.
void CloseSocketWrapper(SOCKET socket);
// Calls CloseSockets for both sender and receiver sockets and then quits the program with error code 1
void Quit(std::vector<Slave*>* slaves, const char* errorMessage = "");
// Prints the helpMap on the command line in an easy-to-read form.
// LineLength is the maximum lenght of one line in characters.
void PrintHelp(std::map<std::string, std::string>* helpMap, int lineLength);
int uniSend(SOCKET socket, char* buf, int len, Debug* debug = NULL);
int uniRecv(SOCKET socket, char* buf, int len, Debug* debug = NULL);

void DeleteSlave(Slave* slave);
void logTimes(std::string outPath, std::string tag, std::vector<Slave*> slaves, uint64_t totalTime);
void debugPrintStatus(bool* endOfFile_parser);
void parseResolution(std::string inName);
void initROIs(std::vector<std::string>* ratio, std::vector<std::string>* heights, std::vector<Slave*>* slaveVector, uint8_t slices);
void printInfo(Timer* timer, std::vector<Slave*>* slaveVector, std::string tag);