#pragma once
#include "interface/types/PtzTypes.h"
#include <string>
#include <map>

struct PTZState {
    PTZVector  position;
    PTZVector  velocity;
    bool       moving   = false;
    PTZMoveMode moveMode = PTZMoveMode::IDLE;
};

class MockPtzController {
public:
    MockPtzController();

    bool      absoluteMove(const std::string& token,
                           const PTZVector& pos,
                           const PTZVector& speed);
    bool      relativeMove(const std::string& token,
                           const PTZVector& trans,
                           const PTZVector& speed);
    bool      continuousMove(const std::string& token,
                             const PTZVector& velocity);
    bool      stop(const std::string& token,
                   bool panTilt, bool zoom);
    PTZStatus getStatus(const std::string& token);
    bool      gotoHome(const std::string& token);
    bool      setHome(const std::string& token);

private:
    std::map<std::string, PTZState>   states_;
    std::map<std::string, PTZVector>  homePositions_;
};
