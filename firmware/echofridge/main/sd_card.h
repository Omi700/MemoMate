#ifndef SD_CARD_H
#define SD_CARD_H

#include "esp_err.h"

esp_err_t InitSdCard();
bool IsSdCardMounted();
void LogSdCardRoot();
void ProbeRemindersFile();
esp_err_t EnsureDefaultPoemsFile();
void ProbePoemsFile();

#endif
