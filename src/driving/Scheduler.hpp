

class Scheduler {

public:
    void* s_oncePerGameLoop;
    void* s_SimRate;
    void* s_halfSimRate;
    void* s_quarterSimRate;
    void* listOfSchedules;
    int lastTickCount;
    int maybeElapsedTime;
    int cinematicModeSkippingAndStuff;
    float timeScale;

    void Run(int i);
};


static_assert(sizeof(Scheduler) == 0x24, "Bad size for Scheduler");