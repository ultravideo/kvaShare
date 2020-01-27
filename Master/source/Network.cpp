#include "Network.h"

/*

Everything related to the networking is happening here

*/

// Sends YUV data from the buffer to the slaves. Goes through the slaves one by one until whole frame from the buffer is sent.
static bool sendDataToSlaves(std::vector<Slave*>* slaveVector, std::pair<char*, bool>* buffer, std::vector<bool>* slaveSentFlags) {
  bool allSent = true;
  int replySize = 0;
  for (int8_t i = 0; i < slaveVector->size(); i++) {
    if (slaveSentFlags->at(i) == true) continue;
    // If some slave does not have sentFlag set to true
    allSent = false;
    if (buffer->second == false) {
      if (slaveVector->at(i)->slaveSocketSend == 0) {
        // Notify local slave of end of file
        getBuffer(NULL, slaveVector->at(i));
      }
      else {
        // Send size of 0 to notify slave of the end of file
#ifdef _WIN32
        shutdown(slaveVector->at(i)->slaveSocketSend, SD_SEND);
#else
        shutdown(slaveVector->at(i)->slaveSocketSend, SHUT_SEND);
#endif //_WIN32
      }
      slaveSentFlags->at(i) = true;
    }
    else { // If there is something to send
      // Local slave
      if (slaveVector->at(i)->slaveSocketSend == 0) {
        // Add current buffer to the local slaves pipeline
        getBuffer(&buffer->first[slaveVector->at(i)->bufferOffset],
          slaveVector->at(i));
      }
      else {
        // Send the YUV data
        if (uniSend(slaveVector->at(i)->slaveSocketSend,
                    &buffer->first[slaveVector->at(i)->bufferOffset],
                    slaveVector->at(i)->sliceSize,
                    &(slaveVector->at(i)->sendDebug)) != slaveVector->at(i)->sliceSize)
        {
          std::string errorMessage = "YUV frame send failed ( " + slaveVector->at(i)->slaveIP + " )";
          Quit(slaveVector, errorMessage.c_str());
        }
      }
      slaveSentFlags->at(i) = true;
    }
  }
  return allSent;
}

void recvSlaveResponces(std::vector<Slave*>* slaveVector, bool* EOF_Flag_reader, bool* EOF_Flag_parser) {
  int replySize = 0;
  int32_t responseCounter = 0;
  bool allFinished;
  std::vector<bool> received;
  received.assign(slaveVector->size(), false);
  while (!(*EOF_Flag_reader) || responseCounter < video.framesRead) {
    allFinished = true;
    for (int8_t i = 0; i < slaveVector->size(); i++) {
      // Skip local slave
      if (slaveVector->at(i)->slaveSocketRecv == 0) continue;
      if (received[i] == true) continue;

      if (!(*EOF_Flag_reader) || slaveVector->at(i)->frameCounter <= video.framesRead) {
        allFinished = false;
        if (uniRecv(slaveVector->at(i)->slaveSocketRecv, (char*)&replySize, sizeof(int32_t), &(slaveVector->at(i)->recvDebug)) != sizeof(int32_t)) {
          Quit(slaveVector, "slice size receiving failed");
        }
        if (uniSend(slaveVector->at(i)->slaveSocketRecv, (char*)&replySize, sizeof(int32_t), &(slaveVector->at(i)->recvDebug)) != sizeof(int32_t)) {
          Quit(slaveVector, "slice size confirming failed");
        }
        if (replySize != 0) {
          mutex.status[1] = true;
          mutex.outputBufferMutex.lock();
          slaveVector->at(i)->frameBuffer.push_back({ new char[replySize], 0 });
          mutex.outputBufferMutex.unlock();
          mutex.status[1] = false;
          if (uniRecv(slaveVector->at(i)->slaveSocketRecv, slaveVector->at(i)->frameBuffer.back().first, replySize, &(slaveVector->at(i)->recvDebug)) != replySize) {
            Quit(slaveVector, "response receiving failed");
          }
          slaveVector->at(i)->frameBuffer.back().second = replySize;
          slaveVector->at(i)->frameCounter++;
          received[i] = true;
        }
      }
    }
    if (allFinished) {
      received.flip();
      mutex.cv_parser.notify_one();
      responseCounter++;
    }
  }
  *EOF_Flag_parser = true;
  mutex.cv_parser.notify_one();
  return;
}

void NetworkHandler(std::pair<std::pair<char*, bool>, std::pair<char*, bool>>* inputBuffer, bool* EOF_Flag_reader, std::vector<Slave*>* slaveVector, bool* EOF_Flag_parser) {
  std::unique_lock<std::mutex> lock(mutex.networkMutex);
  std::vector<bool> slaveSentFlags;
  slaveSentFlags.assign(slaveVector->size(), false);
  bool bufferSlot = false;
  uint32_t sendCounter = 0;
  char* tempChar = new char;

  // Wait until first frame is read by reader thread
  mutex.status[2] = true;
  mutex.cv_network.wait(lock);
  mutex.status[2] = false;
  while (true) {
    while (!sendDataToSlaves(slaveVector, &(bufferSlot ? inputBuffer->first : inputBuffer->second), &slaveSentFlags)) { 
      // Loop sending until YUV is sent to all slaves, then continue to receiving
    }
    // Set sentFlags to false
    slaveSentFlags.flip();
    sendCounter++;
    (bufferSlot ? inputBuffer->first.second : inputBuffer->second.second) = false;
    // Notify reader
    mutex.cv_reader.notify_one();
    if (*EOF_Flag_reader && ((bufferSlot ? inputBuffer->first.first : inputBuffer->second.first) == NULL)) {
      // Everything has been sent
      return;
    } else {
      for (int i = 0; i < slaveVector->size(); i++) {
        if (slaveVector->at(i)->slaveSocketSend == 0) continue;
        uniRecv(slaveVector->at(i)->slaveSocketSend, tempChar, 1);
      }
      // Wait until new frame is read
      mutex.status[2] = true;
      while ((!bufferSlot ? inputBuffer->first.second : inputBuffer->second.second) == false) {
        // If reader is locked eventhough it should be reading next frame, notify it
        if ((!bufferSlot ? inputBuffer->first.second : inputBuffer->second.second) == false && mutex.status[0] == true) {
          mutex.cv_reader.notify_one();
        } else if (*EOF_Flag_reader) {
          break;
        }
        mutex.cv_network.wait_for(lock, std::chrono::milliseconds(10));
      }
      mutex.status[2] = false;
    }
    if (*EOF_Flag_reader) {
      if ((bufferSlot ? inputBuffer->first.first : inputBuffer->second.first) != NULL) {
        delete[](bufferSlot ? inputBuffer->first.first : inputBuffer->second.first);
        (bufferSlot ? inputBuffer->first.first : inputBuffer->second.first) = NULL;
      }
    }
    bufferSlot = !bufferSlot;
  }
}

void initSockets(std::vector<std::string>* IPVector, std::vector<std::string>* ratio, std::vector<Slave*>* slaveVector, 
                 bool useLocal, uint8_t slaves, int port) {
  // Adds local IP to the IPVector if it is not yet there
  if (useLocal) {
    bool localSet = false;
    for (int i = 0; i < IPVector->size(); i++) {
      if ((*IPVector)[i] == "127.0.0.1") {
        localSet = true;
        break;
      }
    }
    if (!localSet) {
      IPVector->insert(IPVector->begin() + IPVector->size(), "127.0.0.1");
      if (ratio->size() != 0) {
        ratio->insert(ratio->begin() + IPVector->size(), "1");
      }
    }
  }
  int erasedElements = 0;
  if (ratio->size() < IPVector->size()) {
    ratio->resize(IPVector->size(), "0");
  }
  for (int i = 0; i < ratio->size(); i++) {
    if (ratio->size() > i) {
      if ((*ratio)[i] == "0") {
        IPVector->erase(IPVector->begin() + i - erasedElements);
        erasedElements++;
      }
    }
  }
  ratio->clear();
  ratio->assign(IPVector->size(), "1");

  // Create init sockets
  for (unsigned int i = 0; i < IPVector->size(); i++) {
    Slave* tempSlave = new Slave;
    tempSlave->slaveIP = (*IPVector)[i];
    if ((*IPVector)[i] != "127.0.0.1") {
      tempSlave->slaveSocketSend = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      setsockopt(tempSlave->slaveSocketSend, IPPROTO_TCP, TCP_NODELAY, (char*)1, sizeof(int));
      struct sockaddr_in* sendAddress = new (struct sockaddr_in);
      sendAddress->sin_family = AF_INET;
      sendAddress->sin_addr.s_addr = inet_addr((*IPVector)[i].c_str());
      sendAddress->sin_port = htons(port);
      connect(tempSlave->slaveSocketSend, (struct sockaddr *) sendAddress, sizeof(*sendAddress));

      tempSlave->slaveSocketRecv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      setsockopt(tempSlave->slaveSocketRecv, IPPROTO_TCP, TCP_NODELAY, (char*)1, sizeof(int));
      struct sockaddr_in* recvAddress = new (struct sockaddr_in);
      recvAddress->sin_family = AF_INET;
      recvAddress->sin_addr.s_addr = inet_addr((*IPVector)[i].c_str());
      recvAddress->sin_port = htons(port + 1);
      connect(tempSlave->slaveSocketRecv, (struct sockaddr *) recvAddress, sizeof(*recvAddress));
    }
    else {
      tempSlave->slaveSocketSend = 0;
      tempSlave->slaveSocketRecv = 0;
    }
    tempSlave->slaveID = i;
    slaveVector->push_back(tempSlave);
  }
}

bool SendOptions(SOCKET socket, std::string* sliceOptions, uint32_t optLength) {
  char* scanString = "opts";
  if (uniSend(socket, scanString, 4, NULL) == 4) {
    char* scanResponce = new char[5];
    if (uniRecv(socket, scanResponce, 5, NULL) == 5 && std::strstr(scanResponce, "ready") != NULL) {
      fprintf(stderr, "Slave ready to receive options\n");
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }

  // Send options size to the slave
  if (uniSend(socket, (char*)&optLength, sizeof(optLength), NULL) != sizeof(optLength)) {
    fprintf(stderr, "Option sending failed with error: %ld\n", WSAGetLastError());
    return false;
  }

  // Send actual options to the slave
  if (uniSend(socket, (char*)sliceOptions->c_str(), optLength, NULL) != optLength) {
    fprintf(stderr, "Option sending failed with error: %ld\n", WSAGetLastError());
    return false;
  }
  return true;
}