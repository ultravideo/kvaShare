#pragma once

#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <fcntl.h>
#include <io.h>
#include <ws2tcpip.h>
#define WIN32_LEAN_AND_MEAN
#pragma comment(lib,"ws2_32.lib")
#else
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>
#include <errno.h>
#define INVALID_SOCKET -1;
#define SOCKET_ERROR -1;
#endif // _WIN32

#define LOG_TIMES false

// Function for argument splitting
std::vector<std::string> Split(std::string line, char mark);

void PrintError(std::string info, bool getLastError = true);
void CloseSocket(SOCKET* socket);

int uniSend(SOCKET socket, char* buf, int len);
int uniRecv(SOCKET socket, char* buf, int len);

void Quit(SOCKET* sendSocket, SOCKET* recvSocket, std::string errorMessage = "");

void logTimes(std::string tag, uint64_t totalEncodingTime, int xOff, int yOff);