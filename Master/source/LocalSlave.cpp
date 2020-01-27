#include "LocalSlave.h"
#include "kvazaar.h"
#include "global.h"
#pragma comment(lib,"kvazaar_lib.lib")

// Kvazaar stuff
const kvz_api *api_;
kvz_config *config_;
kvz_encoder *enc;

struct bufferCounter {
	bufferCounter() : previous(63), current(0), buffer_max(64) {}
	void next() {
		previous = current;
		if (++current == buffer_max) { current = 0; }
	}
	uint8_t previous;
	uint8_t current;
	uint8_t buffer_max;
};
bufferCounter readCounter;
bufferCounter writeCounter;
bufferCounter encodedCounter;


// States (0=free, 1=unused data, 2=data in encoder pipeline)
std::vector<std::pair<kvz_picture*, uint8_t>> inputBuffer;
bool endOfFile = false;

// Kvazaar api init
void initLocal(Slave* slave, int threads, uint8_t multi) {
#if defined(KVZ_PARAMETERS)
	fprintf(stderr, "\nKvazaar parameters:\n");
	fprintf(stderr, "preset = %s\n", parameters.preset.c_str());
	fprintf(stderr, "qp = %i\n", parameters.qp);
	fprintf(stderr, "intra = %i\n", parameters.intra);
	fprintf(stderr, "mv_constraint = frametilemargin\n");
	fprintf(stderr, "slices = tiles\n");
	fprintf(stderr, "subme = 4\n");
	fprintf(stderr, "hash = none\n");
	fprintf(stderr, "owf = %i\n", parameters.owf);
	fprintf(stderr, "slicer = %i,%i,%i,%i\n", slave->roi->x, slave->roi->y, video.resolution.first, video.resolution.second);
	if (parameters.tiles != "") {
		fprintf(stderr, "tiles = %s\n", parameters.tiles.c_str());
    fprintf(stderr, "wpp = off\n");
	}
  fprintf(stderr, "wpp = on\n");
  fprintf(stderr, "threads = %i\n\n", threads);
  fflush(stderr);
#endif

	// Init kvazaar
	api_ = kvz_api_get(8);
	if (!api_) {
    fprintf(stderr, "kvz_api_get failed\n");
		exit(1);
	}
	config_ = api_->config_alloc();
	if (!config_)
	{
    fprintf(stderr, "kvz config_alloc failed\n");
		exit(1);
	}
	api_->config_init(config_);
	api_->config_parse(config_, "preset", parameters.preset.c_str());
	api_->config_parse(config_, "hash", "none");
	api_->config_parse(config_, "subme", "4");
	api_->config_parse(config_, "mv-constraint", "frametilemargin");
	if (parameters.tiles != "") {
		api_->config_parse(config_, "tiles", parameters.tiles.c_str());
    api_->config_parse(config_, "slices", "tiles");
		config_->wpp = 0;
	}
	else {
		config_->wpp = 1;
	}
	config_->width = slave->roi->width;
	config_->height = slave->roi->height;
	if (threads >= 0) {
		config_->threads = threads;
	}
	config_->qp = parameters.qp;
	if (parameters.intra >= 0) {
		config_->intra_period = parameters.intra;
	}
  if (parameters.owf >= 0) {
    config_->owf = parameters.owf;
  }
	config_->slicer.fullWidth = video.resolution.first;
	config_->slicer.fullHeight = video.resolution.second;
	config_->slicer.startCTU_x = slave->roi->x;
	config_->slicer.startCTU_y = slave->roi->y;

	if (parameters.intra != 0 && config_->intra_period < config_->gop_len) {
    fprintf(stderr, "Gop changed to 'lp-g4d4t1', because intra period was shorter than gop length.\n");
		api_->config_parse(config_, "gop", "lp-g4d4t1");
	}

  if (!parameters.simd) {
    api_->config_parse(config_, "cpuid", "0");
  }

	enc = api_->encoder_open(config_);

  for (int i = 0; i < readCounter.buffer_max; i++) {
    inputBuffer.push_back({ api_->picture_alloc(slave->roi->width, slave->roi->height), false });
  }

}

void getBuffer(const char* buffer, Slave* slave) {
  std::unique_lock<std::mutex> lock(mutex.localMutex);
  if (buffer == NULL) {
    endOfFile = true;
    writeCounter.next();
    mutex.cv_local_out.notify_one();
    return;
  }
#if LOG_TIMES==true
    LARGE_INTEGER start, end, elapsed, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
#endif
  while (inputBuffer[writeCounter.current].second != 0) {
    mutex.cv_local_out.notify_one();
    mutex.cv_local_in.wait(lock);
  }
  memcpy(inputBuffer[writeCounter.current].first->fulldata, buffer, slave->sliceSize);
  inputBuffer[writeCounter.current].second = 1;
  writeCounter.next();
  mutex.cv_local_out.notify_one();
#if LOG_TIMES==true
  QueryPerformanceCounter(&end);
  elapsed.QuadPart = end.QuadPart - start.QuadPart;
  elapsed.QuadPart *= 1000000;
  elapsed.QuadPart /= frequency.QuadPart;
  slave->sendDebug.sendTime += elapsed.QuadPart;
  timeLogStruct.localSendTimes.push_back(elapsed.QuadPart);
#endif
}

void LocalSlave(Slave* slave, bool* EOF_Flag_parser) {
  std::unique_lock<std::mutex> lock(mutex.localMutex);
  kvz_data_chunk *data_out = NULL;
  uint32_t data_len = 0;
  kvz_frame_info info;
  uint32_t chunkCounter = 0;
  int32_t encodingCounter = 0;
  int32_t readyCounter = 0;
#if LOG_TIMES == true
  bool firstFrame = true;
  Timer timer;
#endif

  while (true) {
    // Wait if not end of file and next slot of buffer does not have unused data
    if (!endOfFile && inputBuffer[readCounter.current].second != 1) {
      mutex.cv_local_out.wait(lock);
    }
    // If next slot of buffer has unused data, push it to Kvazaar pipeline
    if (inputBuffer[readCounter.current].second == 1) {
#if LOG_TIMES == true
      if (firstFrame) {
        timer.startTimer();
        firstFrame = false;
      }
#endif
      api_->encoder_encode(enc, inputBuffer[readCounter.current].first, &data_out, &data_len, NULL, NULL, &info);
      inputBuffer[readCounter.current].second = 2;
      readCounter.next();
    } else {
      api_->encoder_encode(enc, NULL, &data_out, &data_len, NULL, NULL, &info);
    }
    if (data_out != NULL) {
      inputBuffer[encodedCounter.current].second = 0;
      mutex.cv_local_in.notify_one();
      encodedCounter.next();
      mutex.outputBufferMutex.lock();
      slave->frameBuffer.push_back({ new char[data_len], 0 });
      mutex.outputBufferMutex.unlock();
      chunkCounter = 0;
      
      for (kvz_data_chunk *chunk = data_out; chunk != NULL; chunk = chunk->next)
      {
      	memcpy(&slave->frameBuffer.back().first[chunkCounter], chunk->data, chunk->len);
      	chunkCounter += chunk->len;
      }
      
      slave->frameBuffer.back().second = data_len;
      api_->chunk_free(data_out);
      mutex.cv_parser.notify_one();
      readyCounter++;
    }
    if (endOfFile && (readyCounter >= video.framesRead)) break;
  }
#if LOG_TIMES == true
  timer.stopTimer();
  timeLogStruct.localEncodingTime = timer.totalTime;
#endif
  if (onlyLocal) {
    *EOF_Flag_parser = true;
    mutex.cv_parser.notify_one();
  }
  for (int i = 0; i < readCounter.buffer_max; i++) {
    api_->picture_free(inputBuffer.back().first);
    inputBuffer.pop_back();
  }
}