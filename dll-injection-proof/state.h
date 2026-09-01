#pragma once

#include "framework.h"

// player state (0 <= player < 4)
double ReadPlayerOn(int player);
double ReadPlayerPercent(int player);
double ReadPlayerStock(int player);
double ReadPlayerX(int player);
double ReadPlayerY(int player);
//double ReadPlayerVelX(int player); not found yet
//double ReadPlayerVelY(int player); not found yet
double ReadPlayerAnim(int player);
//double ReadPlayerFramesLeft(int player); not found yet
double ReadPlayerCharacter(int player);
double ReadPlayerTeam(int player);
double ReadPlayerUsedAirDodge(int player);
//double ReadPlayerJumpsLeft(int player); not found yet
//double ReadPlayerIsInvulnerable(int player); not found yet, may be included in player anim

// environment state
double ReadGameSpeed();
double ReadGameStage();
double ReadGameClock();
//double ReadGameTeamsEnabled(); not found yet

// still need location of objects and projectiles


// function to get the entire game state
std::string BuildGameStateJson();