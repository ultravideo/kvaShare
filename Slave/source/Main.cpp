#include "Misc.h"

#include <stdio.h>	// FILE, fopen, fseek, etc...
#include <thread>
#include <stdlib.h>	// atoi, malloc
#include <vector>
#include <map>
#include <condition_variable>
#include <mutex>


#ifdef _WIN32
SOCKET sendSocket = INVALID_SOCKET;
SOCKET recvSocket = INVALID_SOCKET;
#else
int sendSocket = INVALID_SOCKET;
int recvSocket = INVALID_SOCKET;
#endif // _WIN32

#include "kvazaar.h"
#pragma comment(lib,"kvazaar_lib.lib")
const kvz_api *api_;
kvz_config *config_;
kvz_encoder *enc;

std::mutex mutex, read_mutex, write_mutex;
std::condition_variable read_cv, write_cv, doge_cv, broadcast_cv, receive_cv, slave_cv;
uint32_t SLICE_WIDTH = 0;
uint32_t SLICE_HEIGHT = 0;
uint8_t multiframe = 1;
int16_t framesInQueue = 0;
bool endOfFile = false;
bool clear = false;
bool silent = false;
int YUV_size = 0;
int framesDone = -1;
std::string tag = "";
std::vector<int> slicer;

uint32_t cycle = 0;

// Ring buffer stuff
std::vector<std::pair<kvz_picture *, uint8_t>> input_pics;	// Data pointer, State (0=free, 1=unused data, 2=data in encoder pipeline)
struct BufferCounter {
	BufferCounter() : previous(63), current(0), buffer_max(64) {}
	void next() {
		previous = current;
		if (++current == buffer_max) { current = 0; }
	}
  void setSize(uint8_t size) {
    if (size == 0) {
      Quit(&sendSocket, &recvSocket, "Buffer size cannot be 0");
    }
    buffer_max = size;
    previous = buffer_max - 1;
  }
	uint8_t previous;
	uint8_t current;
	uint8_t buffer_max;
};
BufferCounter bufferCounter_in_read;
BufferCounter bufferCounter_in_write;
BufferCounter bufferCounter_out;

std::map<std::string, std::string> help({
	{ "-b  -buffer", "(int) Set buffer size {64}" },
	{ "-help  ", "(none) Print help" },
	{ "-p  -port", "(int) Set initial port where options can be sent {8800}" },
	{ "-s  -silent", "(none) Minimal print mode {false}" },
	{ "-thread  -threads", "(int) Set number of threads to be used {auto}" },
});

// Parse data from the kvz_chunks and add data len at the start of the buffer (first 32 bits)
static char* ParseData(uint32_t data_len, kvz_data_chunk * data_out) {
  char* data_ptr = new char[data_len];
  uint32_t chunkCounter = 0;
  if (data_ptr == NULL) {
    fprintf(stderr, "Buffer init failed!\n");
    Quit(&sendSocket, &recvSocket);
  }
  // Parse kvazaar chunks into one frameful of data
  for (kvz_data_chunk *chunk = data_out; chunk != NULL; chunk = chunk->next)
  {
    std::memcpy(&data_ptr[chunkCounter], chunk->data, chunk->len);
    chunkCounter += chunk->len;
  }
  return data_ptr;
}

// Send encoded data to the master
static void SendData(char* data_ptr, uint32_t buffer_size) {
	// Send HEVC coded frame to the master
  if (uniSend(sendSocket, data_ptr, buffer_size) != buffer_size) {
    PrintError("Send function failed");
    Quit(&sendSocket, &recvSocket);
  } else {
#ifdef DEBUG
    for (uint32_t m = sentData; m < sentData + sendStatus; m++) {
      fputc(data_ptr[m], HEVC);
    }
#endif
  }
  delete[] data_ptr;
	return;
}

static void ReceiveFrames(bool* endOfStream) {
  std::mutex receiveMutex;
  std::unique_lock<std::mutex> lock(receiveMutex);
  uint32_t recvSize = YUV_size;
  int32_t receiveStatus;
  char* trueChar = new char;
  *trueChar = 1;
  while (true) {
    // If buffer still has data, which is not yet fully processed by Kvazaar, flush the kvz pipeline
    while (input_pics[bufferCounter_in_write.current].second != 0) {
      slave_cv.notify_one();
      receive_cv.wait_for(lock, std::chrono::milliseconds(2));
    }
    receiveStatus = uniRecv(recvSocket, (char*)input_pics[bufferCounter_in_write.current].first->fulldata, recvSize);
    if (receiveStatus < 0) Quit(&sendSocket, &recvSocket);
    if (receiveStatus != recvSize) {
      // All receive errors are handled as end of file
      *endOfStream = true;
      slave_cv.notify_one();
      if (uniSend(recvSocket, trueChar, 1) < 0) {
        Quit(&sendSocket, &recvSocket);
      }
      closesocket(recvSocket);
      return;
    }
    else {
      if (uniSend(recvSocket, trueChar, 1) < 0) {
        Quit(&sendSocket, &recvSocket);
      }
      input_pics[bufferCounter_in_write.current].second = 1;
      slave_cv.notify_one();
      bufferCounter_in_write.next();
    }
  }
}

static void Slave(bool* endOfStream) {
  std::mutex slaveMutex;
  std::unique_lock<std::mutex> lock(slaveMutex);
  uint32_t bufferSize = 0;
  uint32_t recvSize = 0;
  int32_t zero = htonl(0);
  char* zeroChar = (char*)&zero;
  kvz_data_chunk *data_out = NULL;
  uint32_t data_len;
#if LOG_TIMES==true
#ifdef _WIN32
  bool firstFrame = true;
  ULONGLONG startTime=0, totalTime=0;
#endif //_WIN32
#endif //LOG_TIMES

  while (true) {
    if (!(*endOfStream) && input_pics[bufferCounter_in_read.current].second != 1) slave_cv.wait(lock);
    if (input_pics[bufferCounter_in_read.current].second == 1) {
#if LOG_TIMES==true
      if (firstFrame) {
#ifdef _WIN32
        startTime = GetTickCount64();
        firstFrame = false;
#endif //_WIN32
      }
#endif //LOG_TIMES
      api_->encoder_encode(enc, input_pics[bufferCounter_in_read.current].first, &data_out, &data_len, NULL, NULL, NULL);
      input_pics[bufferCounter_in_read.current].second = 2;
      framesInQueue++;
      bufferCounter_in_read.next();
    } else {
      api_->encoder_encode(enc, NULL, &data_out, &data_len, NULL, NULL, NULL);
    }
    if (data_out != NULL) {
      input_pics[bufferCounter_out.current].second = 0;
      receive_cv.notify_one();
      bufferCounter_out.next();
      if (uniSend(sendSocket, (char*)&data_len, sizeof(uint32_t)) != sizeof(uint32_t)) {
        Quit(&sendSocket, &recvSocket, "Response size send failed");
      }
      if (uniRecv(sendSocket, (char*)&recvSize, sizeof(uint32_t)) != sizeof(uint32_t)) {
        Quit(&sendSocket, &recvSocket, "Response size receive failed");
      }
      SendData(ParseData(data_len, data_out), data_len);
      api_->chunk_free(data_out);
      framesInQueue--;
    } else if (*endOfStream && framesInQueue <= 0) {
#if LOG_TIMES==true
#ifdef _WIN32
      totalTime = GetTickCount64() - startTime;
      logTimes(tag, totalTime, slicer[0], slicer[1]);
#endif //_WIN32
#endif //LOG_TIMES
      return;
    }
  }
}

static bool InitSocket(int port) {
  struct sockaddr_in send_socket_addr, recv_socket_addr;

  // Open init connection
#ifdef _WIN32
  SOCKET listenSocketSend = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  SOCKET listenSocketRecv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
  SOCKET listenSocketSend = socket(AF_INET, SOCK_STREAM, 0);
  SOCKET listenSocketRecv = socket(AF_INET, SOCK_STREAM, 0);
#endif
  send_socket_addr.sin_family = AF_INET;
  send_socket_addr.sin_addr.s_addr = INADDR_ANY;
  send_socket_addr.sin_port = htons(port+1);
  recv_socket_addr.sin_family = AF_INET;
  recv_socket_addr.sin_addr.s_addr = INADDR_ANY;
  recv_socket_addr.sin_port = htons(port);

  // Bind the port
  if (bind(listenSocketSend, (struct sockaddr*) &send_socket_addr, sizeof(send_socket_addr)) < 0) {
    Quit(&sendSocket, &recvSocket, "Binding failed");
  }
  else if (!silent) {
    fprintf(stderr, "Binding success! (%d)\n", port+1);
  }
  if (bind(listenSocketRecv, (struct sockaddr*) &recv_socket_addr, sizeof(recv_socket_addr)) < 0) {
    Quit(&recvSocket, &recvSocket, "Binding failed");
  }
  else if (!silent) {
    fprintf(stderr, "Binding success! (%d)\n", port);
  }

  // Accept connection to {port}
  if (listen(listenSocketRecv, 5) == SOCKET_ERROR) {
    Quit(&recvSocket, &recvSocket, "Listen function failed");
  }
  recvSocket = accept(listenSocketRecv, NULL, NULL);
  setsockopt(recvSocket, IPPROTO_TCP, TCP_NODELAY, (char*)(int)1, sizeof(int));
  // Accept connection to {port + 1}
  if (listen(listenSocketSend, 5) == SOCKET_ERROR) {
    Quit(&sendSocket, &recvSocket, "Listen function failed");
  }
  sendSocket = accept(listenSocketSend, NULL, NULL);
  setsockopt(sendSocket, IPPROTO_TCP, TCP_NODELAY, (char*)(int)1, sizeof(int));
  if (sendSocket == INVALID_SOCKET || recvSocket == INVALID_SOCKET) {
    fprintf(stderr, "Socket initialization failed\n");
    return false;
  }
  return true;
}

int main(int argc, char *argv[]) {
	std::string argString;
	int port = 8800;
	int y_block = 0;

	// Kvazaar parameters
	std::string preset = "ultrafast";
	std::string tiles = "";
  int qp = 22;
	int intraPeriod = -1; // -1 means use default kvazaar values
	int threads = -1;
  int owf = -1;
	

	// Command line argument parse
	for (int k = 1; k < argc; k++) {
		argString = argv[k];
		if (argString == "-b" || argString == "-buffer") {	// Buffer parse
			if (++k < argc) {
        if (atoi(argv[k]) <= 0 || atoi(argv[k]) > 255) {
          Quit(&sendSocket, &recvSocket, "Buffer size must be greater than 0 and less than 256");
        }
				bufferCounter_in_read.setSize(atoi(argv[k]));
        bufferCounter_in_write.setSize(atoi(argv[k]));
        bufferCounter_out.setSize(atoi(argv[k]));
			}
			else {
				fprintf(stderr, "Invalid buffer argument\n");
				return 1;
			}
		}
		else if (argString == "-help") {	// Help parse
			for (auto value : help) {
				fprintf(stderr, "%-20.20s %s\n", value.first.c_str(), value.second.c_str());
			}
			return 0;
		}
		else if (argString == "-p" || argString == "-port") {	// Port number parse
			if (++k < argc) {
				port = atoi(argv[k]);
			}
			else {
				fprintf(stderr, "Invalid port argument\n");
				return 1;
			}
		}
		else if (argString == "-s" || argString == "-silent") {	// Silent parse
			silent = true;
		}
		else if (argString == "-thread" || argString == "-threads") {	// Thread parse
			if (++k < argc) {
				threads = atoi(argv[k]);
			}
			else {
				fprintf(stderr, "Invalid thread argument\n");
				return 1;
			}
		}
		else {	// Unknown command
			fprintf(stderr, "Unknown command: %s\n", argString.c_str());
			return 1;
		}
	}

#ifdef  _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#else
  int socketfd;
#endif //  _WIN32
#ifdef _WIN32	// Wizardry for winsocket
  WSADATA wsaData;
  int iResult;
  // Initialize Winsock
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    fprintf(stderr, "WSAStartup failure\n");
    return false;
  }
#endif

  if (!InitSocket(port)) {
    Quit(&sendSocket, &recvSocket);
  }
	
	char* type = new char[4];
	
	int error = 0;
	int errlen = sizeof(error);

	while (true) {	// Loop until options has been received	
    if (uniRecv(recvSocket, type, 4) != 4) {
      std::string errorString = "Init receive failed (";
      errorString += type;
      errorString += ")";
      Quit(&sendSocket, &recvSocket, errorString);
    }
		if (std::strstr(type,"opts") != NULL) {
      if (uniSend(recvSocket, "ready", 5) != 5) {
        Quit(&sendSocket, &recvSocket, "Self status send failed");
      }
			break;
		}
	}
	delete[] type;

	// Receive rest of the options from the master
	unsigned int recv_total = 0;
	uint32_t optSize;
	char* options;

	if (uniRecv(recvSocket, (char*)&optSize, sizeof(optSize)) != sizeof(optSize)) {
		PrintError("Option size receive function failed");
		Quit(&sendSocket, &recvSocket, "Option size receive failed");
	}

	if (!silent) {
		fprintf(stderr, "Options size = %d\n", optSize);
	}
	options = new char[optSize];
	if (uniRecv(recvSocket, options, optSize) != optSize) {
		PrintError("Option receive function failed");
		Quit(&sendSocket, &recvSocket, "Option receive failed");
	}
	broadcast_cv.notify_one();

	if (!silent) {
		fprintf(stderr, "Options: %s\n", options);
	}

	// Parse options sent from master
	std::vector<std::string> optVector = Split(options, ':');
	for (auto opt : optVector) {
		std::vector<std::string> option = Split(opt, '=');
		if (option.front() == "preset") {
			preset = option.back();
		}
		else if (option.front() == "intra") {
			intraPeriod = stoi(option.back());
		}
		else if (option.front() == "port") {
			port = stoi(option.back());
		}
		else if (option.front() == "qp") {
			qp = stoi(option.back());
		}
		else if (option.front() == "resolution") {
			std::vector<std::string> resolution = Split(option.back(), 'x');
			if (resolution.size() == 2) {
				SLICE_WIDTH = std::stoi(resolution.front());
				SLICE_HEIGHT = std::stoi(resolution.back());
			}
			else {
				fprintf(stderr, "Invalid resolution! resolution vector size = %zd\n", resolution.size());
				Quit(&sendSocket, &recvSocket);
			}
		}
		else if (option.front() == "slicer") {
			std::vector<std::string> tempVector = Split(option.back(), '!');
			if (tempVector.size() == 4) {
				for (auto value : tempVector) {
					slicer.push_back(stoi(value));
				}
			}
			else {
				fprintf(stderr, "Invalid slicer options! slicer vector size = %zd\n", tempVector.size());
				Quit(&sendSocket, &recvSocket);
			}
		}
		else if (option.front() == "multiframe") {
			multiframe = stoi(option.back());
		}
    else if (option.front() == "owf") {
      owf = stoi(option.back());
    }
    else if (option.front() == "tag") {
      tag = option.back();
    }
	}
	delete[] options;
	if (!silent) {
		fprintf(stderr, "Options received!\n");
	}
	//broadcaster.join();
	YUV_size = SLICE_WIDTH*SLICE_HEIGHT + (SLICE_WIDTH*SLICE_HEIGHT >> 1);
	y_block = SLICE_WIDTH*SLICE_HEIGHT;

	// Init kvazaar
	api_ = kvz_api_get(8);
	if (!api_)
	{
		fprintf(stderr, "kvz_api_get failed\n");
		return EXIT_FAILURE;
	}
	config_ = api_->config_alloc();

	if (!config_)
	{
    fprintf(stderr, "kvz config_alloc failed\n");
		return EXIT_FAILURE;
	}
	api_->config_init(config_);
	api_->config_parse(config_, "preset", preset.c_str());
	api_->config_parse(config_, "hash", "none");
	api_->config_parse(config_, "subme", "4");
	api_->config_parse(config_, "mv-constraint", "frametilemargin");
	if (tiles != "") {
		api_->config_parse(config_, "tiles", tiles.c_str());
    api_->config_parse(config_, "slices", "tiles");
		config_->wpp = 0;
  } else {
    config_->wpp = 1;
  }
	config_->width = SLICE_WIDTH;
	config_->height = SLICE_HEIGHT;
	if (threads >= 0) {
		config_->threads = threads;
	}
	config_->qp = qp;
	if (intraPeriod >= 0) {
		config_->intra_period = intraPeriod;
	}
  if (owf >= 0) {
    config_->owf = owf;
  }
	//config_->sao_enable = 1;
	config_->partial_coding.startCTU_x = slicer[0];
	config_->partial_coding.startCTU_y = slicer[1];
	config_->partial_coding.fullWidth = slicer[2];
	config_->partial_coding.fullHeight = slicer[3];

	if (intraPeriod != 0 && config_->intra_period < (int)config_->gop_len) {
		api_->config_parse(config_, "gop", "lp-g4d4t1");
	}
	enc = api_->encoder_open(config_);

	// Init buffer
	for (uint8_t count = 0; count < bufferCounter_in_read.buffer_max; count++) {
		input_pics.push_back(std::make_pair(api_->picture_alloc(SLICE_WIDTH, SLICE_HEIGHT), 0));
	}

	if (slicer.size() != 4) {
		fprintf(stderr, "Invalid slice options!\n");
		return 1;
	}
	if (SLICE_WIDTH == 0 || SLICE_HEIGHT == 0) {
		if (slicer.size() == 0) {
      fprintf(stderr, "No resolution provided!\n");
			return 1;
		}
	}

	if (silent) {
    fprintf(stderr, "Encoding...\n");
	}

  bool endOfStream = false;

  std::thread receiver(ReceiveFrames, &endOfStream);
  std::thread slave(Slave, &endOfStream);

  receiver.join();
  slave.join();

	for (uint8_t count = 0; count < bufferCounter_in_read.buffer_max; count++) {
		api_->picture_free(input_pics[count].first);
	}
  Quit(&sendSocket, &recvSocket);
	return 0;
}