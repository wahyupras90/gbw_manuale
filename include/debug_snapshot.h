#pragma once
#include <Arduino.h>

struct DebugSnapshot {
    long rawAdc;
    long offsetActive;
    float scaleActive;
    float weightGrams;
    bool hasSample;
    bool flowValid;
    float flowRateGps;

    const char* resetReasonStr;
    unsigned long homeGestureCount;
    unsigned long touchRecoveryCount;

    String lastCheckpoint;
    unsigned long lastCheckpointMs;

    // Last Grind Data
    float lastGrindWeightAtMotorStop;  // berat saat motor berhenti (g)
    float lastGrindActualCoast;        // berat naik setelah motor OFF (g)
    float lastGrindEarlyStopG;         // early stop gram yang dipakai
    float lastGrindFinalWeightG;       // berat akhir (g)
    int   lastGrindPulseCount;         // jumlah pulse correction
};

extern DebugSnapshot grind_get_debug_snapshot();
