#include "Globals.h"

int main(int argc, char *argv[]) {
	// Socket stuff
	std::vector<SOCKET> socketfds;
	std::vector<SOCKET> reply_socketfds;
	std::vector<struct sockaddr_in*> socketaddrs;
	std::vector<struct sockaddr_in*> reply_socketaddrs;
	std::vector<std::string> IPVector;
  int port = 8800;
	
	// Slave conf
	std::vector<std::string> ratio;
	std::vector<std::string> heights;
	std::thread* localSlave = NULL;
	uint8_t multiframe = 1;
	int localThreads = -1; // Defaults to auto
  uint8_t localSlice = 255;
	
	std::string argString;
	std::string outName;
	std::string infoName = "";
	std::string inName;
	std::string inPrefix;
	std::string outPrefix;
	std::locale locale;
  std::string tag = "-";
	Timer timer;
	bool useLocal = true; // Local slave enabled/disabled
	uint8_t slaves = 0;
	uint8_t slices = 0;


  std::vector<Slave*> slaveVector;
  std::pair<std::pair<char*, bool>, std::pair<char*, bool>> inputBuffer;
  bool EOF_Flag_reader = false;
  bool EOF_Flag_parser = false;
  std::vector<ROI*> roiVector;

	std::map<std::string, std::string> help{
		{ "-i -input", "(string) Provide input file *.yuv or '-' for stdin." },
		{ "-o -output", "(string) Provide output file *.265 or '-' for stdout." },
		{ "-input_res", "(<int>x<int>) Provide input resolution in form <width>x<height>." },
		{ "-pi -path_in","(string) Path to the input file's folder." },
		{ "-po -path_out","(string) Path to the output file's folder." },
		{ "-pb -path_both","(string) If both input and output paths are same, this can be used." },
		{ "-nolocal","(none) Disables local slave. IP+ratio overrides this command." },
		{ "-ip","(<string>!<string>!...) Slave IPs divided with '!'." },
		{ "-p -port","(int) Init port. Data port = Init port + 1. {8800}" },
		{ "-r -ratio","(<int>:<int>:...) How big portion of the frame goes to which slave. 0 = disable slave. {1 for all slaves}" },
		{ "-h -heights","(<int>:<int>:...) Custom height values for slaves divided with ':'." },
		{ "-roi","(<int>,<int>,<int>,<int>:...) Custom ROI values in form <x>,<y>,<width>,<height>. ROIs divided with ':'." },
		{ "-info","(string) Info file *.txt." },
		{ "-infotype","(string) Determines in what kind of format info is saved. Choises 'txt' and 'excel'. {txt}" },
		{ "-p -port","(int) Init port. Data port = Init port + 1. {8800}" },
		{ "-m -multiframe","(int) Number of YUV frames to be sent at a time. {1}" },
    { "-tag","(string) Adds a tag to the infofile." },
		{ "Using Kvazaar","for more information see https://github.com/ultravideo/kvazaar" },
		{ "-preset","(string) To see available presets pass '-preset' without argument. {ultrafast}" },
		{ "-qp","(int) Quantization parameter. {22}" },
		{ "-intra","(int) Intra period. 0 = only first frame is intra, 1 = full intra and N = every Nth picture is intra. {auto}" },
		{ "-owf","(int) Frame-level parallelism. Process N-1 frames at a time. {auto}" },
		{ "-t -threads","(int) Number of threads to be used. {auto}" },
    { "-no_simd","(none) Disables SIMD optimizations of Kvazaar." },
	};

#ifdef _WIN32
	// Windows pipe init thingy
	int setReturn;
	setReturn = _setmode(_fileno(stdin), _O_BINARY);
	if (setReturn == -1) {
    fprintf(stderr, "stdin pipeline init failed!\n");
	}
	setReturn = _setmode(_fileno(stdout), _O_BINARY);
	if (setReturn == -1) {
    fprintf(stderr, "stdout pipeline init failed!\n");
	}

	WSADATA wsaData;
	int iResult;
	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
    fprintf(stderr, "WSAStartup failure\n");
		return EXIT_FAILURE;
	}
#endif

	// Required arguments: input, output, input resolution (if not in name), IP, and slice/IP ratio or heights
	// Commands: -i, -o, -input_res, -IP, -ratio/-heights, -fullauto

	// Command line argument parse
	for (int k = 1; k < argc; k++) {
		argString = argv[k];
		for (std::string::size_type charCounter = 0; charCounter < argString.length(); charCounter++) {
			argString[charCounter] = std::toupper(argString[charCounter], locale);
		}
		// Address parse
		if (argString == "-IP") {
			if (++k < argc) {
				IPVector = Split(argv[k], '!');
				if (!ValidateData(IPVector, "%u.%u.%u.%u", true)) {
          fprintf(stderr, "Invalid IP detected!\n");
					Quit(&slaveVector);
				}
			}
			else {
        fprintf(stderr, "Invalid IP argument\n");
				Quit(&slaveVector);
			}
		}
		// Height parse
		else if (argString == "-H" || argString == "-HEIGHTS") {
			if (++k < argc) {
				heights = Split(argv[k], ':');
				if (!ValidateData(heights, "%*u", false)) {
          fprintf(stderr, "Invalid heights\n");
					Quit(&slaveVector);
				}
			}
			else {
        fprintf(stderr, "Invalid height argument\n");
				Quit(&slaveVector);
			}
		}
		// Help parse
		else if (argString == "-HELP") {
			int helpLength = 60;
			if (++k < argc) {
				helpLength = atoi(argv[k]);
			}
			PrintHelp(&help, helpLength);
			Quit(&slaveVector);
		}
		// Info parse
		else if (argString == "-INFO") {
			if (++k < argc) {
				infoName = std::string(argv[k]);
			}
			else {
        fprintf(stderr, "Invalid info argument\n");
				Quit(&slaveVector);
			}
		}
		// Info type parse
		else if (argString == "-INFOTYPE") {
			if (++k < argc) {
				argString = argv[k];
				if (argString == "excel") {
					info.type = argString;
				}
				else if (argString == "txt") {
					info.type = argString;
				}
				else {
          fprintf(stderr, "Unknown info type! supported types are 'excel' and 'txt'\n");
				}
			}
			else {
        fprintf(stderr, "Invalid infotype argument\n");
				Quit(&slaveVector);
			}
		}
		// Input parse
		else if (argString == "-I" || argString == "-INPUT") {
			if (++k < argc) {
        argString = std::string(argv[k]);
				inName = argString;
				if (argString == "-") { video.in = stdin; }
				else {
					std::size_t hit = inName.find_first_of('.');
					if (hit == std::string::npos) {
						// Add file type ending .yuv if no ending was found
						inName += ".yuv";
            fprintf(stderr, "Added '.yuv' as input file ending\n");
					}
				}
			}
			else {
        fprintf(stderr, "Invalid input argument\n");
				Quit(&slaveVector);
			}
		}	
		// Input resolution parse
		else if (argString == "-INPUT_RES") {
			if (++k < argc) {
				std::vector<std::string> resVector = Split(argv[k], 'x');
				if (resVector.size() != 2) {
          fprintf(stderr, "Invalid input resolution. insert resolution in form <width>x<height>\n");
					Quit(&slaveVector);
				}
				video.resolution.first = std::stoi(resVector.at(0));
				video.resolution.second = std::stoi(resVector.at(1));
			}
			else {
        fprintf(stderr, "Invalid input_res argument\n");
				Quit(&slaveVector);
			}
		}
		// Intra parse
		else if (argString == "-INTRA") {
			if (++k < argc) {
        parameters.intra = atoi(argv[k]);
			}
			else {
        fprintf(stderr, "Invalid intra argument\n");
				Quit(&slaveVector);
			}
		}
		// Multiframe parse
		else if (argString == "-M" || argString == "-MULTIFRAME") {
			if (++k < argc) {
				multiframe = atoi(argv[k]);
				if (multiframe < 1) {
          fprintf(stderr, "Multiframe cannot be less than 1\n");
					multiframe = 1;
				}
			}
			else {
        fprintf(stderr, "Invalid multiframe argument\n");
				Quit(&slaveVector);
			}
		}
		// No local parse
		else if (argString == "-NOLOCAL") {
			useLocal = false;
		}
    // Disable simd in local kvazaar
    else if (argString == "-NO_SIMD") {
      parameters.simd = false;
    }
		// Output parse
		else if (argString == "-O" || argString == "-OUTPUT") {
			if (++k < argc) {
				outName = argv[k];
				if (outName != "-") {
					// Search for file type ending '.*' assuming that no dot is used otherwise in file names
					std::size_t hit = outName.find_first_of('.');
					if (hit == std::string::npos) {
						// Add file type ending .265 if no ending was found
						outName.append(".265");
            fprintf(stderr, "Added '.265' as output file ending\n");
					}
				}
				else {
          fprintf(stderr, "Piping output to stdout\n");
					video.out = stdout;
				}
			}
			else {
        fprintf(stderr, "Invalid output argument\n");
				Quit(&slaveVector);
			}
		}
		// OWF parse
		else if (argString == "-OWF") {
			if (++k < argc) {
        parameters.owf = atoi(argv[k]);
				if (parameters.owf < 0) {
          fprintf(stderr, "Invalid OWF value. OWF must be positive integer.\n");
				}
			}
			else {
        fprintf(stderr, "Invalid OWF argument\n");
				Quit(&slaveVector);
			}
		}
		// Path parse (both paths)
		else if (argString == "-PB" || argString == "-PATH_BOTH") {
			if (++k < argc) {
				argString = argv[k];
				if (argString.back() != '/') {
					argString.append("/");
				}
				inPrefix = argString;
				outPrefix = argString;
			}
			else {
        fprintf(stderr, "Invalid path argument (both paths)\n");
				Quit(&slaveVector);
			}
		}
		// Path parse (input path)
		else if (argString == "-PI" || argString == "-PATH_IN") {
			if (++k < argc) {
				argString = argv[k];
				if (argString.back() != '/') {
					argString.append("/");
				}
				inPrefix = argString;
			}
			else {
        fprintf(stderr, "Invalid input path argument\n");
				Quit(&slaveVector);
			}
		}
		// Path parse (output path)
		else if (argString == "-PO" || argString == "-PATH_OUT") {
			if (++k < argc) {
				argString = argv[k];
				if (argString.back() != '/') {
					argString.append("/");
				}
				outPrefix = argString;
			}
			else {
        fprintf(stderr, "Invalid output path argument\n");
				Quit(&slaveVector);
			}
		}
		// Port parse
		else if (argString == "-P" || argString == "-PORT") {
			if (++k < argc) {
				port = atoi(argv[k]);
			}
			else {
        fprintf(stderr, "Invalid port argument\n");
				Quit(&slaveVector);
			}
		}
		// Preset parse
		else if (argString == "-PRESET") {
			if (++k < argc) {
				argString = argv[k];
				// Parse preset name
				if (argString == "uf" || argString == "ultrafast") {
          parameters.preset = "ultrafast";
				}
				else if (argString == "sf" || argString == "superfast") {
          parameters.preset = "superfast";
				}
				else if (argString == "vf" || argString == "veryfast") {
          parameters.preset = "veryfast";
				}
				else if (argString == "fr" || argString == "faster") {
          parameters.preset = "faster";
				}
				else if (argString == "ft" || argString == "fast") {
          parameters.preset = "fast";
				}
				else if (argString == "md" || argString == "medium") {
          parameters.preset = "medium";
				}
				else if (argString == "sw" || argString == "slow") {
          parameters.preset = "slow";
				}
				else if (argString == "sr" || argString == "slower") {
          parameters.preset = "slower";
				}
				else if (argString == "vs" || argString == "veryslow") {
          parameters.preset = "veryslow";
				}
				else if (argString == "pl" || argString == "placebo") {
          parameters.preset = "placebo";
				}
				else {
					fprintf(stderr, "Invalid preset argument: \"%s\"\n", argString.c_str());
					fprintf(stderr, "Available presets :\n");
					fprintf(stderr, " ultrafast (uf)\n");
					fprintf(stderr, " superfast (sf)\n");
					fprintf(stderr, " veryfast  (vf)\n");
					fprintf(stderr, " faster    (fr)\n");
					fprintf(stderr, " fast      (ft)\n");
					fprintf(stderr, " medium    (md)\n");
					fprintf(stderr, " slow      (sw)\n");
					fprintf(stderr, " slower    (sr)\n");
					fprintf(stderr, " veryslow  (vs)\n");
					fprintf(stderr, " placebo   (pl)\n");
					Quit(&slaveVector);
				}
			}
			else {
				fprintf(stderr, "Invalid preset argument\n");
				fprintf(stderr, "Available presets:\n");
				fprintf(stderr, " ultrafast (uf)\n");
				fprintf(stderr, " superfast (sf)\n");
				fprintf(stderr, " veryfast  (vf)\n");
				fprintf(stderr, " faster    (fr)\n");
				fprintf(stderr, " fast      (ft)\n");
				fprintf(stderr, " medium    (md)\n");
				fprintf(stderr, " slow      (sw)\n");
				fprintf(stderr, " slower    (sr)\n");
				fprintf(stderr, " veryslow  (vs)\n");
				fprintf(stderr, " placebo   (pl)\n");
				Quit(&slaveVector);
			}
		}
		// Qp parse
		else if (argString == "-QP") {
			if (++k < argc) {
        parameters.qp = atoi(argv[k]);
			}
			else {
        fprintf(stderr, "Invalid qp argument\n");
				Quit(&slaveVector);
			}
		}
		// Ratio parse
		else if (argString == "-R" || argString == "-RATIO") {
			if (++k < argc) {
				ratio = Split(argv[k], ':');
				if (!ValidateData(ratio, "%d", false)) {
          fprintf(stderr, "Invalid ratio provided!\n");
					Quit(&slaveVector);
				}
			}
			else {
        fprintf(stderr, "Invalid ratio argument\n");
				Quit(&slaveVector);
			}
		}
		// ROI parse
		else if (argString == "-ROI") {
			if (k < argc) {
				std::vector<std::string> tempRoiVector = Split(argv[k], ',');
				ROI* tempRoi = NULL;
				for (auto roi : tempRoiVector) {
					// ROIn values Split with ':'
					std::vector<std::string> tempVector = Split(roi, ':');
					if (tempVector.size() != 4) {
						fprintf(stderr, "Invalid tempRoi arguments\n");
						fprintf(stderr, "Actual number of arguments were: %zd when expected 4\n", tempVector.size());
						return 1;
					}
					tempRoi = new ROI;
					tempRoi->x = std::stoi(tempVector[0]);
					tempRoi->y = std::stoi(tempVector[1]);
					tempRoi->width = std::stoi(tempVector[2]);
					tempRoi->height = std::stoi(tempVector[3]);
					roiVector.push_back(tempRoi);
				}
			}
			else {
        fprintf(stderr, "Invalid Roi argument\n");
				Quit(&slaveVector);
			}
		}
		// Thread parse
		else if (argString == "-T" || argString == "-THREADS") {
			if (++k < argc) {
				localThreads = atoi(argv[k]);
			}
			else {
        fprintf(stderr, "Invalid thread argument\n");
				Quit(&slaveVector);
			}
		}
    // Tag to mark runs within group
    else if (argString == "-TAG") {
      if (++k < argc) {
        tag = (std::string)(argv[k]);
      }
      else {
        fprintf(stderr, "Invalid thread argument\n");
        Quit(&slaveVector);
      }
    }
		// Unknown command
		else {
      fprintf(stderr, "Unknown command: %s\n", argString.c_str());
			Quit(&slaveVector);
		}
	}
	// Check that all required parameters are set
	if (inName == "" && video.in == NULL) {
    fprintf(stderr, "Invalid input name! No input arguments given!\n");
    Quit(&slaveVector);
	}
  if (outName == "") {
    fprintf(stderr, "Invalid output name! No output arguments given!\n");
    Quit(&slaveVector);
  }
	if (video.resolution.first == 0 || video.resolution.second == 0) {
    fprintf(stderr, "Invalid resolution argument! Trying to parse resolution from input name...\n");
    parseResolution(inName);
		if (video.resolution.first == 0 || video.resolution.second == 0) {
      fprintf(stderr, "Resolution parse failed!\n");
			Quit(&slaveVector);
		}
  }
  video.lumaSize = video.resolution.first * video.resolution.second;
  video.frameSize = video.lumaSize * 3 / 2;

	// Scale resolution to be divisible by 8
	if (video.resolution.second % 8 != 0) {
		video.resolution.second -= (video.resolution.second % 8);
    fprintf(stderr, "Video height scaled to %d\n", video.resolution.second);
	}
  // Open input and output files if stdin and stdout are not used
  if (inName != "-") {
    video.in = fopen((inPrefix + inName).c_str(), "rb");
    if (video.in == NULL) {
      fprintf(stderr, "Failed to open input stream (%s)\n", (inPrefix + inName).c_str());
      Quit(&slaveVector);
    }
  }
  if (outName != "-") {
    video.out = fopen((outPrefix + outName).c_str(), "wb");
    if (video.out == NULL) {
      fprintf(stderr, "Failed to open output stream (%s)\n", (outPrefix + outName).c_str());
      Quit(&slaveVector);
    }
  }
  // Open info file, if it's in use
  if (infoName != "") {
    info.infoFile = fopen((outPrefix + infoName).c_str(), "ab");
    if (info.infoFile == NULL) {
      fprintf(stderr, "Failed to open info stream\n");
      Quit(&slaveVector);
    }
  }
	if (IPVector.size() == 0) {
    fprintf(stderr, "Invalid IP arguments! usage: -IP <IP address>. multiple addresses divided with '!' \n");
		Quit(&slaveVector);
	}
  else {
    initSockets(&IPVector, &ratio, &slaveVector, useLocal, slaves, port);
  }
	if (ratio.size() == 0 && heights.size() == 0) {
    fprintf(stderr, "No ratio or heights provided! using all available slaves equally.\n");
		for (int i = 0; i < IPVector.size(); i++) {
			ratio.push_back("1");
		}
	} else if (heights.size() != 0) {
    // Convert slave usage from heights to ratios
    ratio.clear();
    for (int heightIndex = 0; heightIndex < heights.size(); heightIndex++) {
      if (heights[heightIndex] != "0") {
        ratio.push_back("1");
      }
      else {
        ratio.push_back("0");
      }
    }
  }

  // Loop through ratios, delete unused slaves and count number of slaves in use
  slices = 0;
  for (int ratioIndex = 0; ratioIndex < ratio.size(); ratioIndex++) {
    // If slave is not used, delete it from the slaveVector
    if (ratio[ratioIndex] == "0") {
      for (int slaveIndex = 0; slaveIndex < slaveVector.size(); slaveIndex++) {
        if (slaveVector[slaveIndex]->slaveID == ratioIndex) {
          DeleteSlave(slaveVector[slaveIndex]);
          slaveVector.erase(slaveVector.begin() + slaveIndex);
          break;
        }
      }
    } else {
      slices++;
    }
  }

  // Delete "0" values from the ratio vector
  if (ratio.size() != 0) {
    std::vector<std::string>::iterator it = ratio.begin();
    while (it != ratio.end()) {
      if (*it == "0") {
        it = ratio.erase(it);
      } else {
        it++;
      }
    }
  }

	// Init ROI, unless given in arguments
  if (roiVector.size() == 0) {
    initROIs(&ratio, &heights, &slaveVector, slices);
  } else {
    if (roiVector.size() > slaveVector.size()) {
      fprintf(stderr, "Too many ROIs compared to valid slaves ( %zd > %zd )\n", roiVector.size(), slaveVector.size());
      Quit(&slaveVector);
    }
    for (int i = 0; i < roiVector.size(); i++) {
      slaveVector.at(i)->roi = roiVector[i];
    }
    // Delete unused slaves
    if (slaveVector.size() > roiVector.size()) {
      for (int i = (int)roiVector.size(); i < (int)slaveVector.size(); i++) {
        DeleteSlave(slaveVector[i]);
      }
      slaveVector.resize(roiVector.size());
    }
  }

  // Initialize slaves that have been enabled by setting heights or ratio value to non-zero
	std::string options = "preset=" + parameters.preset + ":" + "intra=" + std::to_string(parameters.intra) + ":" + "qp=" + std::to_string(parameters.qp);
	for (unsigned int i = 0; i < slaveVector.size(); i++) {
		if (IPVector[i] == "127.0.0.1") {
      // ToDo: update local slave functions
      slaveVector[i]->slaveSocketSend = 0;
			initLocal(slaveVector[i], localThreads, multiframe);

			localSlave = new std::thread(LocalSlave, slaveVector[i], &EOF_Flag_parser);
		}
		else {
      timeLogStruct.sendTimeMap.insert(std::pair<int, std::vector<uint64_t>>((int)slaveVector[i]->slaveSocketSend, {}));
			// Parse and send options to slaves
			std::string sliceOptions = options;
			sliceOptions += ":resolution=" + std::to_string(slaveVector[i]->roi->width) + "x" + std::to_string(slaveVector[i]->roi->height);
			sliceOptions += ":slicer=0!" + std::to_string(slaveVector[i]->roi->y) + "!" + std::to_string(video.resolution.first) + "!" + std::to_string(video.resolution.second);
			sliceOptions += ":multiframe=" + std::to_string(multiframe);
			sliceOptions += ":port=" + std::to_string(port);
      sliceOptions += ":owf=" + std::to_string(parameters.owf);
      sliceOptions += ":tag=" + tag;

			uint32_t optLength = (uint32_t)sliceOptions.size();
#if !defined(NO_PRINT)
			fprintf(stderr, "Options: %s  %i\n", sliceOptions.c_str(), optLength);
#endif
      if (!SendOptions(slaveVector[i]->slaveSocketSend, &sliceOptions, optLength)) {
        Quit(&slaveVector);
      }
		}
	}
#if !defined(NO_PRINT)
  fprintf(stderr, "All options sent!\n");
#endif

	// Write some info to the info file. (timestamp and options in use)
	if (info.infoFile != NULL) {
		InitInfoWriter(info.infoFile);
		if (info.type == "txt") {
			WriteInfo(" " + inName, false);
			WriteInfo("preset=" + parameters.preset + " qp=" + std::to_string(parameters.qp) + " intra=" + std::to_string(parameters.intra)
				+ " resolution=" + std::to_string(video.resolution.first) + "x" + std::to_string(video.resolution.second));
			if (ratio.size() != 0) {
				ParseInfoFromVector(ratio, "ratio");
			}
			else if(heights.size() != 0){
				ParseInfoFromVector(heights, "heights");
			}
		}
	}

#if defined(NO_PRINT)
  fprintf(stderr, "Encoding in progress...\n");
#endif

  if (slaveVector.size() == 0) {
    Quit(&slaveVector, "No slaves detected.");
  }

  inputBuffer = { {new char[video.frameSize], false}, {new char[video.frameSize], false} };
  std::thread* receiverThread = NULL;

  mutex.status.assign(4, false);
#if PRINT_LOCK_STATUS==true
  std::thread debug(debugPrintStatus, &EOF_Flag_parser);
#endif

	timer.startTimer(); // Start encoding timer

  // Start network thread
  std::thread networkThread(NetworkHandler, &inputBuffer, &EOF_Flag_reader, &slaveVector, &EOF_Flag_parser);

  // Start  receiver threads
  for (int i = 0; i < slaveVector.size(); i++) {
    // Only create receiver thread if there is other slaves than local in use
    if (slaveVector[i]->slaveSocketSend != 0) {
      onlyLocal = false;
      receiverThread = new std::thread (recvSlaveResponces, &slaveVector, &EOF_Flag_reader, &EOF_Flag_parser);
      break;
    }
  }
  std::thread* slicerThread = NULL;

	// YUVSlicer {slice yuv data into slices that will be coded on server side}
  slicerThread = new std::thread(Reader, &slaveVector, &inputBuffer, &EOF_Flag_reader);

	// HEVCParser {parse received HEVC data to form whole picture every frame}
  std::thread parserThread(Parser, &slaveVector, &EOF_Flag_parser);

	slicerThread->join();
  networkThread.join();
  if (localSlave != NULL) {
    localSlave->join();
  }
  if (receiverThread != NULL) {
    receiverThread->join();
  }
  parserThread.join();
	timer.stopTimer();
#if PRINT_LOCK_STATUS==true
  debug.join();
#endif
#if LOG_TIMES==true
  logTimes(outPrefix, tag, slaveVector, timer.totalTime);
#endif
  printInfo(&timer, &slaveVector, tag);

	// Clean up
  delete[] inputBuffer.first.first;
  delete[] inputBuffer.second.first;
	Quit(&slaveVector);
}