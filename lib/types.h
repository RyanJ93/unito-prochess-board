#ifndef PROCHESS_TYPES_H
#define PROCHESS_TYPES_H

#include <stdlib.h>
#include <sys/types.h>
#include <semaphore.h>

/**
 * I just miss booleans.
 */
typedef unsigned short boolean;

/**
 * Represents a position.
 */
typedef struct {
    unsigned int x;
    unsigned int y;
    unsigned int index;
} coords_t;

/**
 * Represents a single cell in the game board.
 */
typedef struct {
    char player_pseudo_name;
    unsigned int flag_score;
    unsigned short occupant_type;
    pid_t occupant_pid;
    sem_t mutex;
} cell_t;

/**
 * Represents the whole game board.
 */
typedef struct {
    int width;
    int height;
    int coordinator_mq_id;
    long waiting_time;
    pid_t coordinator_pid;
    cell_t cells[];
} board_t;

/**
 * Represents a single player's pawn.
 */
typedef struct {
    int owner_mq_id;
    int mq_id;
    pid_t pid;
} pawn_t;

/**
 * Represents a player.
 */
typedef struct {
    int mq_id;
    pid_t pid;
    char pseudo_name;
    unsigned int available_moves;
    unsigned int total_moves;
    unsigned int total_score;
    unsigned int global_score;
} player_t;

/**
 * Represents a message.
 */
typedef struct {
    unsigned short message_type;
    char player_pseudo_name;
    char payload[128];
} message_t;

#endif
