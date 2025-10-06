#include "../actionhelpers.h"
#include <stdio.h>

// AUTOINJECT
void SeparateNumber(uint score, char* scoreText) {

    int millions = (score / 1000) / 1000;
    int thousands = (score / 1000) % 1000;
    int units = score % 1000;

    if(millions) {
        sprintf(scoreText, "%d,%.3d,%.3d", millions, thousands, units);
        return;
    }

    if(thousands) {
        sprintf(scoreText, "%d,%.3d", thousands, units);
        return;
    }

    sprintf(scoreText, "%d", units);
    return;
}