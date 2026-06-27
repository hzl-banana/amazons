#pragma once
#include <chrono>

class Timer {
private:
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point pauseTime;
    int timeLimit;
    bool running;
    bool paused;
    int lastSecond;

public:
    Timer(int limit = 20);
    void start();
    void stop();
    void pause();
    void resume();
    void reset();
    void reset(int newLimit);
    int getRemainingTime() const;
    int getElapsedTime() const;
    bool isTimeout() const;
    bool isRunning() const { return running; }
    bool isPaused() const { return paused; }
    void setTimeLimit(int limit) { timeLimit = limit; }
    int getTimeLimit() const { return timeLimit; }
    int getProgress() const;
    bool shouldPlayCountdown(int& second);
};
