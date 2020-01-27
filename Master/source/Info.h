#pragma once

#include "Globals.h"

void InitInfoWriter(FILE* file);
void WriteInfo(std::string infoData, bool newLine = true);
void ParseInfoFromVector(std::vector<std::string> dataVector, std::string title, char splitter = ':');
void FlushInfo();