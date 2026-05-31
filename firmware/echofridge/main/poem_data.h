#ifndef POEM_DATA_H
#define POEM_DATA_H

#include "esp_err.h"

struct PoemDisplayData {
    bool loaded;
    char line1[80];
    char line2[80];
    char meta[80];
};

esp_err_t LoadRandomPoemFromSd(PoemDisplayData *poem);

#endif
