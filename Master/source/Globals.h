#pragma once

// All the necessary includes
#include <vector>
#include <utility>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <locale>
#include <thread>
#include <future>
#include <map>
#include <string>
#include <deque>
#include <atomic>
#include <mutex>
#include <cmath>
#include <sys/types.h>
#include <memory>
#include <cstdio>
#include <stdint.h>
#include <limits>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <ws2tcpip.h>
#include <winsock2.h>
#include <fcntl.h>
#include <io.h>
#include <Windows.h> // For getting time
#include <dshow.h>
#pragma comment(lib,"ws2_32.lib")
#else
#include <sys/socket.h>
#endif // _WIN32

// Debug parameters
// If true, the program will print mutex lock status every second. Used for debugging deadlocks.
#define PRINT_LOCK_STATUS false
// If true, the program logs send and receive times, prints totals for each slave and writes a log file with all wait times
#define LOG_TIMES false

struct Debug {
  uint64_t sendTime;
  uint64_t recvTime;
  FILE* debugYUV = NULL;
  Debug(): sendTime(0), recvTime(0) {};
};
struct ROI {
	unsigned int x;
	unsigned int y;
	long width;
	long height;
	long data_len;
};
struct Timer {
	ULONGLONG startTime = 0;
	ULONGLONG totalTime = 0;
	void startTimer() { startTime = GetTickCount64(); }
	void stopTimer() { totalTime = GetTickCount64() - startTime; }
};
struct YUV {
	int y;
	int u;
	int v;
	void calculate(std::pair<int, int>* index, int sliceSize, int width) {
		y = index->first + index->second * width;
		u = sliceSize + (index->first >> 1) + (index->second >> 1) * (width >> 1);
		v = sliceSize + (sliceSize >> 2) + (index->first >> 1) + (index->second >> 1) * (width >> 1);
	}
};
struct Video {
  std::pair<int, int> resolution; // Full width, full height
  int lumaSize;   // Y channel size ( video width * video height )
  int frameSize;  // Frame size including all channels YUV ( lumaSize * 1.5 )
  int framesRead = 0;
  FILE* out = NULL;
  FILE* in = NULL;
};
struct Slave {
  uint8_t slaveID;
  std::deque<std::pair<char*, uint64_t>> frameBuffer;
  ROI* roi;
  int sliceSize;
  int frameCounter = 0;
  uint64_t bufferOffset;
  SOCKET slaveSocketSend;
  SOCKET slaveSocketRecv;
  std::string slaveIP;
  Debug sendDebug;
  Debug recvDebug;
};
struct Info {
  FILE* infoFile;
  std::string type;
  uint64_t totalData;
  unsigned int totalFrames;
  unsigned int fpsCounter;
  std::deque<std::chrono::system_clock::time_point> startTime;

  Info() : infoFile(NULL), type("txt"), totalData(0), totalFrames(0), fpsCounter(0) {};
};
struct Mutex {
  std::condition_variable cv_reader, cv_network, cv_parser, cv_local_in, cv_local_out;
  std::mutex readerMutex, outputBufferMutex, networkMutex, parserMutex, localMutex;
  std::vector<bool> status;
};
struct TimeLogStruct {
  std::map<int, std::vector<uint64_t>> sendTimeMap;
  std::vector<uint64_t> localSendTimes;
  uint64_t firstReadTime;
  uint64_t localEncodingTime;
  uint64_t totalTime;
  
  TimeLogStruct() : localSendTimes({}), firstReadTime(0), localEncodingTime(0), totalTime(0) {};
};
struct Parameters {
  std::string preset;
  int8_t owf; // -1 means use default
  int intra;
  int qp;
  bool simd;
  std::string tiles;

  Parameters() : preset("ultrafast"), owf(-1), intra(-1), qp(22), simd(true), tiles("") {};
};


extern Video video;
extern Info info;
extern Mutex mutex;
extern bool onlyLocal;
extern TimeLogStruct timeLogStruct;
extern Parameters parameters;

#include "HEVCParser.h"
#include "YUVSlicer.h"
#include "LocalSlave.h"
#include "Info.h"
#include "Misc.h"
#include "Network.h"