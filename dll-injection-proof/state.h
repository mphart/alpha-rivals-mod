#pragma once

#include "framework.h"

// player state (0 <= player < 4)
double ReadPlayerOn(int player);
double ReadPlayerPercent(int player);
double ReadPlayerStock(int player);
double ReadPlayerX(int player);
double ReadPlayerY(int player);
double ReadPlayerVelX(int player); // TODO
double ReadPlayerVelY(int player); // TODO
double ReadPlayerAnim(int player);
double ReadPlayerAnimSprite(int player);
double ReadPlayerFramesLeft(int player); // TODO not found yet
double ReadPlayerCharacter(int player);
double ReadPlayerTeam(int player);
double ReadPlayerUsedAirDodge(int player);
double ReadPlayerJumpsLeft(int player); // TODO not found yet

// character-specific player state
double ReadPlayerOnFire(int player);

// environment state
double ReadGameSpeed();
double ReadGameStage();
double ReadGameClock();
double ReadGameTeamsEnabled(); // TODO

// function to get the entire game state
std::string BuildGameStateJson();

// menu state
double ReadPlayerCursorY(int player);