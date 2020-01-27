#include "Info.h"

FILE* infoFile;
std::string fullInfo;

void WriteInfo(std::string infoData, bool newLine) {
	if (infoFile == NULL) { return; }
	if (newLine) {
		if (info.type == "txt") {
			fullInfo += "\n ";
		}
	}
	fullInfo += infoData;
}

void ParseInfoFromVector(std::vector<std::string> dataVector, std::string title, char splitter) {
	if (infoFile == NULL) { return; }
	if (dataVector.size() == 0) { return; }
	fullInfo += " " + title + "=";
	for (auto line : dataVector) {
		fullInfo += line + splitter;
	}
	// Delete the extra splitter character from the end
	fullInfo = fullInfo.substr(0, fullInfo.length() - 1);
}

void FlushInfo() {
	if (infoFile == NULL) { return; }
	if(info.type == "txt"){ fullInfo += "\n"; }
	fullInfo += "\n";
	fputs(fullInfo.c_str(), infoFile);
	fflush(infoFile);
}

void InitInfoWriter(FILE* file) {
	infoFile = file;
	if (info.type == "txt") {
#ifdef _WIN32
		SYSTEMTIME timestamp;
		GetLocalTime(&timestamp);
		fullInfo = std::to_string(timestamp.wDay) + "." + std::to_string(timestamp.wMonth) + "." + std::to_string(timestamp.wYear)
			+ " " + std::to_string(timestamp.wHour) + ":" + std::to_string(timestamp.wMinute) + ":" + std::to_string(timestamp.wSecond);
#endif // _WIN32
	}
}