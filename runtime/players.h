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
// The menu's Players rows: the players set up, else one per controller connected (up to 4) - who will get which
// controller when the game sets up 2 to 4 players.
int input_player_rows(void);
// What player p (0-based) plays with, e.g. "Keyboard + Xbox Controller" ("" when nothing). Controllers show the
// player's own name for them when they have one (input_controller_rename).
void input_player_device(int p, char *out, size_t cap);
// Controllers connected (gamepads SDL knows).
int input_controllers(void);
// Requests (the menu's Players rows; applied on the UI thread, every one of them): give player p the next (dir +1) or
// previous (-1) controller, swapping with the player who has it; and make every player's controller vibrate briefly
// with a notice of who is who. An arrangement made in the rows is kept for the next time the game sets up players.
void input_player_step(int p, int dir);
void input_players_identify(void);
// Controller names: the controllers connected (connection order); controller i's type ("Xbox Controller") and the
// player's name for it ("" when none); rename it ("" goes back to its type). Kept in controllers.ini.
int input_controller_count(void);
bool input_controller_info(int i, char *type, size_t tcap, char *custom, size_t ccap);
void input_controller_rename(int i, const char *name);

#ifdef __cplusplus
}
#endif
