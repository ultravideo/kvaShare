#include "HEVCParser.h"

/* 

Parser waits until it's notified that all slave responses for a frame has been received or the local slice is ready.
Upon notify the parser checks if all slices for a full frame has been received and if so, it parses the frame.
After the frame has been parsed the corresponding slices are deleted from the buffer.

*/

void Parser(std::vector<Slave*>* slaveVector, bool* EOF_Flag_parser) {
  std::unique_lock<std::mutex> lock(mutex.parserMutex);
  int parsedFrames = 0;
  bool fullFrame;
  bool firstFrame = true;
  // Loop until end of file is signaled from Network thread and then until all responces are parsed
  while (!(*EOF_Flag_parser) || (info.totalFrames != video.framesRead)) {
    // If end of file parser flag is set it means that all frames are in buffer and there is only need for parsing.
    if (!(*EOF_Flag_parser)) {
      mutex.status[3] = true;
      mutex.cv_parser.wait(lock);
      mutex.status[3] = false;
    }
    fullFrame = true;
    for (int i = 0; i < slaveVector->size(); i++) {
      if (slaveVector->at(i)->frameBuffer.size() == 0) {
        fullFrame = false;
        break;
      } else if (slaveVector->at(i)->frameBuffer.front().second == 0) {
        fullFrame = false;
        break;
      }
    }
    if (fullFrame) {
      // Write full frame to the output file
      for (int i = 0; i < slaveVector->size(); i++) {
        if (firstFrame) {
          // Special case, where headers have to be written properly
          if (i == 0) {
            int derp = fwrite(slaveVector->at(i)->frameBuffer.front().first,
              sizeof(char),
              slaveVector->at(i)->frameBuffer.front().second,
              video.out
            );
            if (derp != slaveVector->at(i)->frameBuffer.front().second) {
              fprintf(stderr, "File write failed with error %i\n", ferror(video.out));
              exit(1);
            }
            info.totalData += slaveVector->at(i)->frameBuffer.front().second;
          } else {
            // Other frames than the first one should have their start headers skipped (NAL 32, 33, 34 and 39)
            char character;
            int combo = 0;
            int type;
            uint64_t startPosition = 0;
            uint64_t position = 0;
            char* matchPosition;
            char pattern[] = { 0x00, 0x00, 0x01 };
            // Search through NAL units and write first non-header to the file
            while (true) {
              // Find NAL unit start
              matchPosition = std::search(&slaveVector->at(i)->frameBuffer.front().first[startPosition],
                                          &slaveVector->at(i)->frameBuffer.front().first[slaveVector->at(i)->frameBuffer.front().second],
                                          &pattern[0],
                                          &pattern[0] + 3);
              // Check if something is found
              if (matchPosition != &slaveVector->at(i)->frameBuffer.front().first[slaveVector->at(i)->frameBuffer.front().second]) {
                position = matchPosition - &slaveVector->at(i)->frameBuffer.front().first[0];
                // Parse NAL unit type
                character = slaveVector->at(i)->frameBuffer.front().first[position + 3];
                type = character << 1;
                type = type >> 2;
                
                // If read type is not a header write it to the file
                if (type != 32 && type != 33 && type != 34 && type != 39) {
                  if (fwrite( &slaveVector->at(i)->frameBuffer.front().first[position],
                              sizeof(char),
                              slaveVector->at(i)->frameBuffer.front().second - position,
                              video.out
                            ) != slaveVector->at(i)->frameBuffer.front().second - position) {
                    fprintf(stderr, "File write failed with error %i\n", ferror(video.out));
                    exit(1);
                  }
                  info.totalData += slaveVector->at(i)->frameBuffer.front().second - position;
                  break;
                }
                else { // If type was header, search for the next NAL start
                  startPosition = position + 3;
                }
              }
            } // End of header parse loop
          }
        } // End of first frame writing
        else {
          if (fwrite( slaveVector->at(i)->frameBuffer.front().first,
                      sizeof(char),
                      slaveVector->at(i)->frameBuffer.front().second,
                      video.out
                    ) != slaveVector->at(i)->frameBuffer.front().second) {
            fprintf(stderr, "File write failed with error %i\n", ferror(video.out));
            exit(1);
          }
          info.totalData += slaveVector->at(i)->frameBuffer.front().second;
        }
      } // End of overall write
      fflush(video.out);
      info.totalFrames++;
      fprintf(stderr, "Frame (%i) parsed\n", info.totalFrames);
      fflush(stderr);
      // Clear buffers
      mutex.status[1] = true;
      mutex.outputBufferMutex.lock();
      for (int j = 0; j < slaveVector->size(); j++) {
        delete[] slaveVector->at(j)->frameBuffer.front().first;
        slaveVector->at(j)->frameBuffer.front().first = NULL;
        slaveVector->at(j)->frameBuffer.front().second = 0;
        slaveVector->at(j)->frameBuffer.pop_front();
      }
      mutex.outputBufferMutex.unlock();
      mutex.status[1] = false;
      firstFrame = false;
    } // if( fullFrame )
  } //  while( !(*EOF_Flag_parser) )
}