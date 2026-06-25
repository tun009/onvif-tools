#include "mock/MockPtzController.h"
#include <algorithm>
#include <cstdio>

MockPtzController::MockPtzController() {
    PTZState def{};
    for (auto& tok : {"profile_main", "profile_sub1", "profile_sub2"}) {
        states_[tok]        = def;
        homePositions_[tok] = {0.0f, 0.0f, 0.0f};
    }
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

bool MockPtzController::absoluteMove(const std::string& token,
                                      const PTZVector& pos,
                                      const PTZVector& /*speed*/) {
    auto& s = states_[token];
    s.position.pan  = clampf(pos.pan,  -1.0f, 1.0f);
    s.position.tilt = clampf(pos.tilt, -1.0f, 1.0f);
    s.position.zoom = clampf(pos.zoom,  0.0f, 1.0f);
    s.moving        = false;
    s.moveMode      = PTZMoveMode::IDLE;
    printf("[PTZ] AbsoluteMove %s pan=%.2f tilt=%.2f zoom=%.2f\n",
           token.c_str(), s.position.pan, s.position.tilt, s.position.zoom);
    return true;
}

bool MockPtzController::relativeMove(const std::string& token,
                                      const PTZVector& trans,
                                      const PTZVector& /*speed*/) {
    auto& s = states_[token];
    s.position.pan  = clampf(s.position.pan  + trans.pan,  -1.0f, 1.0f);
    s.position.tilt = clampf(s.position.tilt + trans.tilt, -1.0f, 1.0f);
    s.position.zoom = clampf(s.position.zoom + trans.zoom,  0.0f, 1.0f);
    s.moving        = false;
    s.moveMode      = PTZMoveMode::IDLE;
    printf("[PTZ] RelativeMove %s → pan=%.2f tilt=%.2f zoom=%.2f\n",
           token.c_str(), s.position.pan, s.position.tilt, s.position.zoom);
    return true;
}

bool MockPtzController::continuousMove(const std::string& token,
                                        const PTZVector& velocity) {
    auto& s    = states_[token];
    s.velocity = velocity;
    s.moving   = true;
    s.moveMode = PTZMoveMode::CONTINUOUS;
    printf("[PTZ] ContinuousMove %s vel=(%.2f,%.2f,%.2f)\n",
           token.c_str(), velocity.pan, velocity.tilt, velocity.zoom);
    return true;
}

bool MockPtzController::stop(const std::string& token,
                              bool /*panTilt*/, bool /*zoom*/) {
    auto& s  = states_[token];
    s.moving = false;
    s.moveMode = PTZMoveMode::IDLE;
    s.velocity = {0.0f, 0.0f, 0.0f};
    printf("[PTZ] Stop %s\n", token.c_str());
    return true;
}

PTZStatus MockPtzController::getStatus(const std::string& token) {
    auto& s = states_[token];
    PTZStatus st;
    st.position      = s.position;
    st.moveStatus    = s.moving ? MoveStatus::MOVING : MoveStatus::IDLE;
    st.panTiltStatus = st.moveStatus;
    st.zoomStatus    = MoveStatus::IDLE;
    return st;
}

bool MockPtzController::gotoHome(const std::string& token) {
    auto it = homePositions_.find(token);
    PTZVector home = (it != homePositions_.end())
                     ? it->second : PTZVector{0,0,0};
    return absoluteMove(token, home, {1,1,1});
}

bool MockPtzController::setHome(const std::string& token) {
    homePositions_[token] = states_[token].position;
    printf("[PTZ] SetHome %s at (%.2f,%.2f,%.2f)\n",
           token.c_str(),
           homePositions_[token].pan,
           homePositions_[token].tilt,
           homePositions_[token].zoom);
    return true;
}
