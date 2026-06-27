#include "Timer.h"
#include <algorithm>

Timer::Timer(int limit) 
    : timeLimit(limit), running(false), paused(false), lastSecond(-1) {
}

void Timer::start() {
    startTime = std::chrono::steady_clock::now();
    running = true;
    paused = false;
    lastSecond = -1;
}

void Timer::stop() {
    running = false;
    paused = false;
}

void Timer::pause() {
    if (running && !paused) {
        pauseTime = std::chrono::steady_clock::now();
        paused = true;
    }
}

void Timer::resume() {
    if (running && paused) {
        auto pauseDuration = std::chrono::steady_clock::now() - pauseTime;
        startTime += pauseDuration;
        paused = false;
    }
}

void Timer::reset() {
    start();
}

void Timer::reset(int newLimit) {
    timeLimit = newLimit;
    start();
}

int Timer::getRemainingTime() const {
    if (!running) {
        return timeLimit;
    }
    
    if (paused) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            pauseTime - startTime).count();
        int remaining = timeLimit - (int)elapsed;
        return remaining > 0 ? remaining : 0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - startTime).count();
    int remaining = timeLimit - (int)elapsed;
    return remaining > 0 ? remaining : 0;
}

int Timer::getElapsedTime() const {
    if (!running) {
        return 0;
    }
    
    if (paused) {
        return (int)std::chrono::duration_cast<std::chrono::seconds>(
            pauseTime - startTime).count();
    }
    
    auto now = std::chrono::steady_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::seconds>(
        now - startTime).count();
}

bool Timer::isTimeout() const {
    return running && getRemainingTime() == 0;
}

int Timer::getProgress() const {
    if (timeLimit == 0) return 100;
    
    int remaining = getRemainingTime();
    int progress = (remaining * 100) / timeLimit;
    return std::max(0, std::min(100, progress));
}

bool Timer::shouldPlayCountdown(int& second) {
    if (!running || paused) {
        return false;
    }
    
    int remaining = getRemainingTime();
    
    // 0-9ÃëÊ±²¥·Å¶ÁÃëÒôÐ§
    if (remaining >= 0 && remaining <= 9) {
        if (remaining != lastSecond) {
            second = remaining;
            lastSecond = remaining;
            return true;
        }
    } else {
        lastSecond = -1;
    }
    
    return false;
}
