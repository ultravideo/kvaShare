#include "Misc.h"

#if LOG_TIMES == true
std::vector<uint64_t> sendTimes = {};
uint64_t totalSendTime;
#endif

std::vector<std::string> Split(std::string line, char mark) {
  std::vector<std::string> returnValue;
  std::size_t lastFound = 0;
  std::size_t found = 0;
  while (found != std::string::npos) {
    found = line.find_first_of(mark, lastFound);
    returnValue.push_back(line.substr(lastFound, found - lastFound));
    lastFound = found + 1;
  }
  return returnValue;
}

void PrintError(std::string info, bool getLastError) {
	if (getLastError) {
#ifdef _WIN32
		fprintf(stderr, "%s with error: %d\n", info.c_str(), WSAGetLastError());
#else
		fprintf(stderr, "%s with error: %m\n", info.c_str());
#endif // _WIN32
	}
	else {
		fprintf(stderr, "%s\n", info.c_str());
	}
}

void CloseSocket(SOCKET* socket) {
#ifdef _WIN32
	if (*socket != INVALID_SOCKET) {
		shutdown(*socket, SD_BOTH);
		closesocket(*socket);
	}
#else
	if (*socket != INVALID_SOCKET) {
		shutdown(*socket, SHUT_RDWR);
		close(*socket);
	}
#endif
}

int uniSend(SOCKET socket, char* buf, int len) {
  int sendStatus = 0;
#if LOG_TIMES == true
#ifdef _WIN32
  LARGE_INTEGER start, end, elapsed, frequency;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
#endif //_WIN32
#endif //LOG_TIMES == true
#ifdef _WIN32
  sendStatus = send(socket, buf, len, 0);
#else
  sendStatus = write(socket, buf, len);
#endif

#if LOG_TIMES == true
#ifdef _WIN32
  QueryPerformanceCounter(&end);
  elapsed.QuadPart = end.QuadPart - start.QuadPart;
  elapsed.QuadPart *= 1000000;
  elapsed.QuadPart /= frequency.QuadPart;
  totalSendTime += elapsed.QuadPart;
  if (len > 4) {
    sendTimes.push_back(elapsed.QuadPart);
  }
#endif //_WIN32
#endif //LOG_TIMES == true
  if (sendStatus < 0) {
    fprintf(stderr, "WSA error code (send): %i", WSAGetLastError());
  }
  return sendStatus;
}

int uniRecv(SOCKET socket, char* buf, int len) {
  int receiveStatus = 0;
  int totalReceived = 0;
  do {
#ifdef _WIN32
    receiveStatus = recv(socket, &buf[totalReceived], len - totalReceived, 0);
#else
    receiveStatus = read(socket, &buf[totalReceived], len - totalReceived);
#endif
    if (receiveStatus < 0) {
      fprintf(stderr, "WSA error code (recv): %i", WSAGetLastError());
      return -1;
    } else if (receiveStatus == 0) {
      return 0;
    } else {
      totalReceived += receiveStatus;
    }
  } while (totalReceived < len);
  return totalReceived;
}

void Quit(SOCKET* sendSocket, SOCKET* recvSocket, std::string errorMessage) {
  if (*sendSocket != -1) {
#ifdef _WIN32
    shutdown(*sendSocket, SD_BOTH);
#else
    shutdown(*sendSocket, SHUT_RDWR);
    close(*sendSocket);
#endif //_WIN32
  }
  if (*recvSocket != -1) {
#ifdef _WIN32
    shutdown(*recvSocket, SD_BOTH);
#else
    shutdown(*recvSocket, SHUT_RDWR);
    close(*recvSocket);
#endif //_WIN32
  }
#ifdef _WIN32
  WSACleanup();
#endif //_WIN32
  if (errorMessage != "") {
    fprintf(stderr, "Error: %s\n", errorMessage.c_str());
  }
  exit(1);
}

#if LOG_TIMES == true
void logTimes(std::string tag, uint64_t totalEncodingTime, int xOff, int yOff) {
  std::string timeLogName = "TimeLogs/timeLog_" + tag + std::to_string(xOff) + "_" + std::to_string(yOff) + ".txt";
  FILE* timeLog = fopen(timeLogName.c_str(), "ab");
  if (timeLog == NULL) {
    fprintf(stderr, "Failed to open time log file\n");
    return;
  }
  uint64_t maxTime = 0;
  for (int i = 0; i < sendTimes.size(); i++) {
    if (sendTimes[i] > maxTime) maxTime = sendTimes[i];
    if (i != 0) fprintf(timeLog, ", ");
    fprintf(timeLog, "%lli", sendTimes[i]);
  }
  fprintf(timeLog, "\n Max send time: %.2f ms\n", maxTime / 1000.0);
  fprintf(timeLog, " Sum of sending times: %.2f ms\n", (totalSendTime / 1000.0));
  fprintf(timeLog, " Slave (%s) encoding time: %lli ms\n", (std::to_string(xOff) + "_" + std::to_string(yOff)).c_str(), totalEncodingTime);
  fprintf(timeLog, "########################################\n\n");
  fflush(timeLog);
  fclose(timeLog);
}
#endif // LOG_TIMES == true