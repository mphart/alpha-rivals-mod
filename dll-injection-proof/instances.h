#pragma once

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

struct FireState {
    bool valid;
    float x;
    float y;
    double player;
    uint32_t have;
};

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

// Walk the room instance list for oPlayer (object_index 3) and fill out[4]
// indexed by player (fallback: track_player), both 1-based. Returns how many slots were written.
int ReadOPlayerInstances(OPlayerState out[4]);
int ReadProjectileInstances(ProjectileState* out, int maxCount);
int ReadGroundFireInstances(FireState* out, int maxCount);
int ReadBubbleInstances(BubbleState* out, int maxCount);
int ReadPuddleInstances(PuddleState* out, int maxCount);