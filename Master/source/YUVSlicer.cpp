#include "YUVSlicer.h"

/*

Read input file here and rearrange the YUV data in the buffer so it's already in slices.
This means that for each slave the corresponding slice can be read directly from offset to offset + slicesize without caring about YUV structure anymore.

  |------------|
  |yyyyyyyyyyyy|
  |yyyyyyyyyyyy|
  |yyyyyyyyyyyy|
  |yyyyyyyyyyyy|    read buffer
  |uuuuuuuuuuuu|
  |vvvvvvvvvvvv|
  |------------|
        ||
        ||      Parse data slice wise for two slaves
        \/
  |------------|
  |yyyyyyyyyyyy|
  |yyyyyyyyyyyy|
  |uuuuuuvvvvvv|
  |yyyyyyyyyyyy|    parsed slice buffer
  |yyyyyyyyyyyy|
  |uuuuuuvvvvvv|
  |------------|

*/

static void ParseSliceToBuffer(std::vector<Slave*>* slaveVector, char* inputBufferPointer, char* targetBuffer, uint32_t sendCounter) {
  uint64_t bufferIterator = 0;
  // For each slave copy the corresponding slice from the inputbuffer to the target buffer
  // Target buffer will contain full slices (YUV channels) for each slave one after another
  for (Slave* slave : *slaveVector) {
    slave->bufferOffset = bufferIterator;
    uint64_t offset = (slave->roi->x * 64) + (slave->roi->y * 64) * video.resolution.first;
    // Copy Y channel to the buffer
    memcpy(&targetBuffer[bufferIterator], &inputBufferPointer[offset], slave->roi->data_len);
    bufferIterator += slave->roi->data_len;
    offset = video.lumaSize + ((slave->roi->x * 64) + (slave->roi->y * 64) * video.resolution.first) / 4;
    // Copy U channel to the buffer
    memcpy(&targetBuffer[bufferIterator], &inputBufferPointer[offset], slave->roi->data_len / 4);
    bufferIterator += slave->roi->data_len / 4;
    offset += video.lumaSize / 4;
    // Copy V channel to the buffer
    memcpy(&targetBuffer[bufferIterator], &inputBufferPointer[offset], slave->roi->data_len / 4);
    bufferIterator += slave->roi->data_len / 4;
  }
}

void Reader(std::vector<Slave*>* slaveVector, std::pair<std::pair<char*, bool>, std::pair<char*, bool>>* inputBuffer, bool* EOF_Flag_reader) {
  std::unique_lock<std::mutex> lock(mutex.readerMutex);
  bool bufferSwitcher = false;
  char* readBuffer = new char[video.frameSize];
#if LOG_TIMES == true
  LARGE_INTEGER start, end, elapsed, frequency;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
#endif
  if (fread(readBuffer, 1, video.frameSize, video.in) != video.frameSize) {
    *EOF_Flag_reader = true;
    // Notify network thread
    mutex.cv_network.notify_one();
    return;
  }
  ParseSliceToBuffer(slaveVector, readBuffer, (bufferSwitcher ? inputBuffer->first.first : inputBuffer->second.first), video.framesRead);
#if LOG_TIMES
  QueryPerformanceCounter(&end);
  elapsed.QuadPart = end.QuadPart - start.QuadPart;
  elapsed.QuadPart *= 1000000;
  elapsed.QuadPart /= frequency.QuadPart;
  timeLogStruct.firstReadTime = elapsed.QuadPart;
#endif
  (bufferSwitcher ? inputBuffer->first.second : inputBuffer->second.second) = true;
  video.framesRead++;
  mutex.cv_network.notify_one();
  bufferSwitcher = !bufferSwitcher;
  while (*EOF_Flag_reader == false) {
    if (fread(readBuffer, 1, video.frameSize, video.in) != video.frameSize) {
      *EOF_Flag_reader = true;
      // Notify network thread
      mutex.cv_network.notify_one();
      return;
    }
    ParseSliceToBuffer(slaveVector, readBuffer, (bufferSwitcher ? inputBuffer->first.first : inputBuffer->second.first), video.framesRead);
    (bufferSwitcher ? inputBuffer->first.second : inputBuffer->second.second) = true;
    video.framesRead++;
    mutex.cv_network.notify_one();
    mutex.status[0] = true;
    mutex.cv_reader.wait(lock);
    mutex.status[0] = false;
    bufferSwitcher = !bufferSwitcher;
  }
}