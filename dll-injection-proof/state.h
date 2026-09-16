#pragma once

#include "framework.h"

// Custom GML vars + builtin x/y from a live oPlayer CInstance.
struct OPlayerState {
    bool valid;
    float x;
    float y;
    double url;
    double state;
    double state_timer;
    double track_player;
    double prev_state;
    double prev_prev_state;
    double attack;
    double spr_dir;
    double hsp;
    double vsp;
    double has_walljump;
    double has_airdodge;
    double djumps;
    double attack_invince;
    double respawn_invince_time;
    double hitstop;
    double hitstop_full;
    double strong_charge;
    double window;
    double window_timer;
    double burn_timer;
    double player;
    uint32_t have; // bit i set => field i was a numeric RValue
};

// Walk the room instance list for oPlayer (object_index 3) and fill out[4]
// indexed by player (fallback: track_player), both 1-based. Returns how many slots were written.
int ReadOPlayerInstances(OPlayerState out[4]);

struct ProjectileState {
    bool valid;
    float x;
    float y;
    double hsp;
    double vsp;
    double spr_dir;
    double player;
    uint32_t have;
};

struct GroundFireState {
    bool valid;
    float x;
    float y;
    double player;
    uint32_t have;
};

int ReadProjectileInstances(ProjectileState* out, int maxCount);
int ReadGroundFireInstances(GroundFireState* out, int maxCount);

struct BubbleState {
    bool valid;
    float x;
    float y;
    double hsp;
    double vsp;
    double player;
    uint32_t have;
};

struct PuddleState {
    bool valid;
    float x;
    float y;
    double player;
    uint32_t have;
};

int ReadBubbleInstances(BubbleState* out, int maxCount);
int ReadPuddleInstances(PuddleState* out, int maxCount);

// player state (0 <= player < 4)
double ReadPlayerOn(int player);
double ReadPlayerChoice(int player);
bool TryReadPlayerChoice(int player, double* out);
double ReadPlayerPercent(int player);
double ReadPlayerStock(int player);
double ReadPlayerCursorY(int player);

double WritePlayerOn(int player, double val);
bool WritePlayerChoice(int player, double val);
double WritePlayerPercent(int player, double val);
double WritePlayerStock(int player, double val);

// Debug: scan CE player doubles for CSS character-id candidates.
std::string ScanPlayerGlobalCandidates(int player);

// Debug: dump a named global custom var (and array elems if kind==array).
std::string DumpGlobalVar(const char* name);
std::string ResolvePlayerChoiceAddr(int player);

// environment state
double ReadGameSpeed();
double ReadGameStage();
double ReadGameClock();
double ReadGameStocks();
double ReadGameTeamsEnabled(); // TODO

// Proxy for "match is currently running". Not a real GML game_is_running bool;
// true when a live gameplay_parent or oPlayer exists in the current room.
bool ReadGameIsRunning();

// Proxy for stage/map select (vs character select). True when a live
// ss_stagebox_obj or ss_stage_header_obj exists; CSS uses cs_* objects instead.
bool ReadIsMapSelection();

// Proxy for the versus results screen. True when a live draw_result_screen
// or result_screen_box exists. game_stage is not always 1039 here.
bool ReadIsPostMatch();

// Global gameplay_time (same value get_gameplay_time() returns).
bool TryReadGameplayTime(double* out);

// Frames left on the 3-2-1-GO countdown, estimated from gameplay_time.
// 0 when not in a match, after GO, or if the timer cannot be read.
double ReadCountdownRemaining();

// True when a running match is actually accepting fight inputs: in a
// gameplay room, not on results, gameplay has not stopped, and at least
// one oPlayer has left PS_SPAWN (the countdown lock).
bool ReadCanMakeInputs();

// function to get the entire game state
std::string BuildGameStateJson();

