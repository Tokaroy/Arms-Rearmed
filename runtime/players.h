// Players 2 to 4 (runtime/hle/input.c): when the game sets up more than one player (its "connect controllers" screen,
// nn::hid::ShowControllerSupport, runtime/hle/applets.c) each player gets a controller of their own. Player 1 is the
// keyboard plus the controller they play with, as in single player.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_MAX_PLAYERS 4

// The game asks for min..max players: sets them up and returns true with *count players (the controllers connected
// decide how many), or false when there aren't enough controllers (a notice says how many to connect).
bool input_setup_players(int min, int max, int *count);
// Players set up (1: single player).
int input_players(void);
// What player p (0-based) plays with, e.g. "Keyboard + Xbox Controller" ("" when nothing).
void input_player_device(int p, char *out, size_t cap);
// Controllers connected (gamepads SDL knows).
int input_controllers(void);
// Requests (the menu's Players rows; applied on the UI thread): give player p the next controller (swapping with the
// player who has it), and make every player's controller vibrate briefly with a notice of who is who.
void input_player_next(int p);
void input_players_identify(void);

#ifdef __cplusplus
}
#endif
