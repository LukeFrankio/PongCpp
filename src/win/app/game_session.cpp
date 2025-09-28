#include "game_session.h"
#include "../../core/game_core.h"
GameSession::GameSession() : corePtr(new GameCore()) {}
GameSession::~GameSession() { delete corePtr; }
void GameSession::update(double dt) { if(corePtr) corePtr->update(dt); }
GameCore& GameSession::core() { return *corePtr; }
