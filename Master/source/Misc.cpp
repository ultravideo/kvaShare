#include "Misc.h"
#include <chrono>

// Split function for command line argument parser
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

bool ValidateData(std::vector<std::string> data, std::string form, bool IPTest) {
	if (IPTest) {
		std::vector<int> tester;
		tester.assign(4, 0);
		for (int i = 0; i < data.size(); i++) {
			if (sscanf(data[i].c_str(), form.c_str(), &tester[0], &tester[1], &tester[2], &tester[3]) != 4) {
				return false;
			}
		}
	}
	else {
		int testerInt;
		for (int i = 0; i < data.size(); i++) {
			if (sscanf(data[i].c_str(), form.c_str(), &testerInt) != 1) {
				return false;
			}
		}
	}
	return true;
}

void CloseSocketWrapper(SOCKET socket) {
	if (socket == NULL) { return; }
	if (socket != SOCKET_ERROR) {
		shutdown(socket, SD_BOTH);
		closesocket(socket);
	}
}

// Custom quit function which takes care of closing the sockets
void Quit(std::vector<Slave*>* slaves, const char* errorMessage) {
  for (int i = 0; i < slaves->size(); i++) {
    CloseSocketWrapper(slaves->at(i)->slaveSocketSend);
    CloseSocketWrapper(slaves->at(i)->slaveSocketRecv);
    for (int j = 0; j < slaves->at(i)->frameBuffer.size(); j++) {
      delete[] slaves->at(i)->frameBuffer[j].first;
    }
    slaves->at(i)->frameBuffer.clear();
  }
#ifdef _WIN32
	WSACleanup();
#endif // _WIN32
  if (errorMessage != "") {
    fprintf(stderr, "Program exited with error: %s\n", errorMessage);
  }
	exit(1);
}

void PrintHelp(std::map<std::string, std::string>* helpMap, int lineLength) {
	fprintf(stderr, "\n%-20.20s%s\n", "Commands:", "(type) info {default}");
	for (auto line : *helpMap) {
		fprintf(stderr, "%-20.20s", line.first.c_str());
		if (line.second.length() > lineLength) {
			size_t space;
			int i = 0;
			while (i < line.second.length()) {
        // If the whole text won't fit in one line
				if (i + lineLength < line.second.length()) {
					space = line.second.substr(i,lineLength).find_last_of(' ');
					if (space == std::string::npos || space < 2) {
						fprintf(stderr, "%s\n%*c", line.second.substr(i, lineLength - (i==0 ? 0 : 3)).c_str(), 20+2, ' ');
						i += lineLength - (i == 0 ? 0 : 3);
					}
					else {
						fprintf(stderr, "%s\n%*c", line.second.substr(i, space).c_str(), 20+1, ' ');
						i += (int)space;
					}
				}
				else {
					fprintf(stderr, "%s\n", line.second.substr(i).c_str());
					break;
				}
			}
		}
		else {
			fprintf(stderr, "%s\n", line.second.c_str());
		}
	}
	fflush(stderr);
}

// Custom send function which wraps both linux and windows sending functions and adds the time logging
int uniSend(SOCKET socket, char* buf, int len, Debug* debug) {
  int sendStatus = 0;
  int totalSent = 0;
#if LOG_TIMES==true
  LARGE_INTEGER start, end, elapsed, frequency;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
#endif

#ifdef _WIN32
  sendStatus = send(socket, buf, len, 0);
#else
  sendStatus = write(socket, buf, len);
#endif

#if LOG_TIMES==true
  if (debug != NULL) {
    QueryPerformanceCounter(&end);
    elapsed.QuadPart = end.QuadPart - start.QuadPart;
    elapsed.QuadPart *= 1000000;
    elapsed.QuadPart /= frequency.QuadPart;
    debug->sendTime += elapsed.QuadPart;
    if (len > 4) {
      timeLogStruct.sendTimeMap[socket].push_back(elapsed.QuadPart);
    }
  }
#endif
  if (sendStatus < 0) {
    fprintf(stderr, "Send failed with WSA error code: %i\n", WSAGetLastError());
  }
  return sendStatus;
}

// Custom receiving function which wraps both linux and windows receiving functions and adds the time logging
int uniRecv(SOCKET socket, char* buf, int len, Debug* debug) {
  int receiveStatus = 0;
  int totalReceived = 0;
  do {
#ifdef _WIN32
    receiveStatus = recv(socket, &buf[totalReceived], len - totalReceived, 0);
#else
    receiveStatus = read(socket, &buf[totalReceived], len - totalReceived);
#endif
    if (receiveStatus < 0) {
      fprintf(stderr, "Recv failed with WSA error code: %i\n", WSAGetLastError());
      return -1;
    } else {
      totalReceived += receiveStatus;
    }
  } while (totalReceived < len);
  return totalReceived;
}

// Closes sockets and deletes all of the information of the slave
void DeleteSlave(Slave* slave) {
  CloseSocketWrapper(slave->slaveSocketSend);
  CloseSocketWrapper(slave->slaveSocketRecv);
  if (slave->frameBuffer.size() != 0) {
    for (int i = 0; i < slave->frameBuffer.size(); i++) {
      if (slave->frameBuffer[i].first != NULL) {
        delete[] slave->frameBuffer[i].first;
      }
    }
    slave->frameBuffer.clear();
  }
}

// Function that goes through all stored time log data and stores it to timelog file.
void logTimes(std::string outPath, std::string tag, std::vector<Slave*> slaves, uint64_t totalTime){
  std::string TimeLogName = outPath + "TimeLogs/timeLog_" + tag + ".txt";
  FILE* timeLog = fopen(TimeLogName.c_str(), "ab");
  if (timeLog == NULL) {
    fprintf(stderr, "Failed to open time log file\n");
    return;
  }
  uint64_t maxTime;
  for (auto slaveTimeLog : timeLogStruct.sendTimeMap) {
    maxTime = 0;
    int currentSlave = 0;
    for (int k = 0; k < slaves.size(); k++) {
      if (slaveTimeLog.first == slaves[k]->slaveSocketSend) {
        fprintf(timeLog, "Slave %s ((%s)) sending times\n ", slaves[k]->slaveIP.c_str(), (std::to_string(slaves[k]->roi->x) + "_" + std::to_string(slaves[k]->roi->y)).c_str());
        currentSlave = k;
        break;
      }
    }
    for (int i = 0; i < slaveTimeLog.second.size(); i++) {
      if (slaveTimeLog.second[i] > maxTime) maxTime = slaveTimeLog.second[i];
      if (i != 0) fprintf(timeLog, ", ");
      fprintf(timeLog, "%lli", slaveTimeLog.second[i]);
    }
    fprintf(timeLog, "\n Max send time: %.2f ms\n", maxTime / 1000.0);
    fprintf(timeLog, " Sum of sending times: %.2f ms\n", (slaves[currentSlave]->sendDebug.sendTime / 1000.0));
    fprintf(stderr, "Slave %s sum of sending times: %.2f ms\n", slaves[currentSlave]->slaveIP.c_str(), (slaves[currentSlave]->sendDebug.sendTime / 1000.0));
  }
  maxTime = 0;
  
  if(timeLogStruct.localSendTimes.size() == 0){
    fprintf(timeLog, "# Local slave had no input waiting times\n");
  }
  else {
    fprintf(timeLog, "Local slave input waiting and copying times\n ");
    for (int j = 0; j < timeLogStruct.localSendTimes.size(); j++) {
      if (timeLogStruct.localSendTimes[j] > maxTime) maxTime = timeLogStruct.localSendTimes[j];
      if (j != 0) fprintf(timeLog, ", ");
      fprintf(timeLog, "%lli", timeLogStruct.localSendTimes[j]);
    }
    fprintf(timeLog, "\n Max send time: %.2f ms\n", maxTime / 1000.0);
    for (auto slave : slaves) {
      if (slave->slaveSocketSend == 0) {
        fprintf(timeLog, " Sum of sending times: %.2f ms\n", (slave->sendDebug.sendTime / 1000.0));
        fprintf(stderr, "Slave %s sum of sending times: %.2f ms\n", slave->slaveIP.c_str(), (slave->sendDebug.sendTime / 1000.0));
        break;
      }
    }
  }
  if (timeLogStruct.localEncodingTime != 0) {
    fprintf(timeLog, "Local encoding time: %lli ms\n", timeLogStruct.localEncodingTime);
  }
  fprintf(timeLog, "Time spent for reading and slicing the first frame: %.2f ms\n", (timeLogStruct.firstReadTime / 1000.0));
  fprintf(timeLog, "Total encoding time: %lli ms\n", totalTime);
  fprintf(timeLog, "########################################\n\n");
  fflush(timeLog);
  fclose(timeLog);
}

// Debug function that prints out mutex statuses.
void debugPrintStatus(bool* endOfFile_parser) {
  std::condition_variable cv;
  std::mutex debugMutex;
  std::unique_lock<std::mutex> lock(debugMutex);
  while (!(*endOfFile_parser)) {
    fprintf(stderr, "read[%s] output[%s] network[%s] parser[%s]\n",
      mutex.status[0] ? "true" : "false",
      mutex.status[1] ? "true" : "false",
      mutex.status[2] ? "true" : "false",
      mutex.status[3] ? "true" : "false");
    cv.wait_for(lock, std::chrono::seconds(1));
  }
}

// Search for resolution information from the input name. Assumes that the resolution is given in form <width>x<height>
void parseResolution(std::string inName) {
  std::vector<std::string> splitName = Split(inName, '_');
  for (int nameIndex = 0; nameIndex < splitName.size(); nameIndex++) {
    std::size_t firstNumber = splitName[nameIndex].find_first_of("1234567890");
    while (firstNumber != std::string::npos) {
      std::size_t firstNonNumber = splitName[nameIndex].substr(firstNumber).find_first_not_of("1234567890");
      if (firstNonNumber == std::string::npos) break;
      if (splitName[nameIndex].substr(firstNumber + firstNonNumber, 1) == "x" || splitName[nameIndex].substr(firstNumber + firstNonNumber, 1) == "X") {
        if (sscanf(splitName[nameIndex].substr(firstNumber).c_str(), "%dx%d%*s", &video.resolution.first, &video.resolution.second) == 2) {
          fprintf(stderr, "Resolution parsed from file name: %ix%i\n", video.resolution.first, video.resolution.second);
          break;
        }
      }
      size_t nextNumber = splitName[nameIndex].substr(firstNumber + firstNonNumber).find_first_of("1234567890");
      if (nextNumber == std::string::npos) break;
      firstNumber += nextNumber + firstNonNumber;
    }
  }
}

// Calculates the slice sizes for the slaves
void initROIs(std::vector<std::string>* ratio, std::vector<std::string>* heights, std::vector<Slave*>* slaveVector, uint8_t slices) {
  unsigned int lastY = 0;
  long lastHeight = 0;
  if (heights->size() == 0) {
    uint8_t valid_ratios = 0;
    for (unsigned int i = 0; i < ratio->size(); i++) {
      ROI* temp_roi = new ROI;
      temp_roi->x = 0;
      temp_roi->y = (unsigned int)floor(((lastY * 64) + lastHeight) / 64);
      temp_roi->width = video.resolution.first;
      temp_roi->height = ((i % 2 != 0) ? (long)ceil((double)video.resolution.second / slices / 64) : (long)floor((double)video.resolution.second / slices / 64)) * 64;
      if (valid_ratios == slices - 1) {
        temp_roi->height = video.resolution.second - (temp_roi->y * 64);
      }
      temp_roi->data_len = temp_roi->width * temp_roi->height;
      valid_ratios++;
      slaveVector->at(i)->roi = temp_roi;
      slaveVector->at(i)->sliceSize = temp_roi->data_len * 3 / 2;
      lastY = temp_roi->y;
      lastHeight = temp_roi->height;
    }
  }
  else {
    int deletedSlaves = 0;
    // Use heights to create ROIs
    for (unsigned int i = 0; i < heights->size(); i++) {
      if (stoi((*heights)[i]) == 0) {
        for (int slaveIndex = 0; slaveIndex < slaveVector->size(); slaveIndex++) {
          if ((*slaveVector)[slaveIndex]->slaveID == i) {
            DeleteSlave((*slaveVector)[slaveIndex]);
            slaveVector->erase(slaveVector->begin() + slaveIndex);
            break;
          }
        }
        deletedSlaves++;
        continue;
      }
      ROI* temp_roi = new ROI;
      temp_roi->x = 0;
      temp_roi->y = (unsigned int)floor(((lastY * 64) + lastHeight) / 64);
      temp_roi->width = video.resolution.first;
      temp_roi->height = stoi((*heights)[i]);
      temp_roi->data_len = temp_roi->width * temp_roi->height;
      slaveVector->at(i - deletedSlaves)->roi = temp_roi;
      slaveVector->at(i - deletedSlaves)->sliceSize = temp_roi->data_len * 3 / 2;
      lastY = temp_roi->y;
      lastHeight = temp_roi->height;
    }
  }
}

void printInfo(Timer* timer, std::vector<Slave*>* slaveVector, std::string tag) {
  fprintf(stderr, "%i frames processed!\ntotal time: %lli milliseconds\nfps: %.4f\n", (int)info.totalFrames, timer->totalTime, double(double(info.totalFrames) / (double(timer->totalTime) / 1000)));
  fflush(stderr);
  // Write encoding info to the file (file size, frames, enc time, fps)
  if (info.infoFile != NULL) {
    std::stringstream fps_ss;
    std::stringstream time_ss;
    time_ss << std::fixed << std::setprecision(3) << (double(timer->totalTime) / 1000);
    fps_ss << std::fixed << std::setprecision(4) << (double(double(info.totalFrames) / (double(timer->totalTime) / 1000)));
    if (info.type == "txt") {
      WriteInfo("Slice heights");
      for (auto slave : (*slaveVector)) {
        WriteInfo(": " + std::to_string(slave->roi->height) + " ", false);
      }
      WriteInfo("Average PSNR: " + tag);
      WriteInfo(std::to_string(info.totalFrames) + " frames processed");
      WriteInfo("Total time: " + time_ss.str() + " seconds");
      WriteInfo("FPS: " + fps_ss.str());
      FlushInfo();
    }
    else if (info.type == "excel") {
      WriteInfo(time_ss.str() + "," + fps_ss.str(), false);
      FlushInfo();
    }
  }
}