#define _POSIX_SOURCE

#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "lib/board.h"
#include "lib/communicator.h"
#include "lib/player.h"
#include "lib/types.h"

/* DEV */
/*
#define SO_NUM_G 2
#define SO_NUM_P 10
#define SO_MAX_TIME 3
#define SO_BASE 24
#define SO_ALTEZZA 18
#define SO_FLAG_MIN 5
#define SO_FLAG_MAX 5
#define SO_ROUND_SCORE 10
#define SO_N_MOVES 1
#define SO_MIN_HOLD_NSEC 10000000
 */

/* EASY */

#define SO_NUM_G 2
#define SO_NUM_P 10
#define SO_MAX_TIME 3
#define SO_BASE 60
#define SO_ALTEZZA 20
#define SO_FLAG_MIN 5
#define SO_FLAG_MAX 5
#define SO_ROUND_SCORE 10
#define SO_N_MOVES 20
#define SO_MIN_HOLD_NSEC 100000000


/* HARD */
/*
#define SO_NUM_G 4
#define SO_NUM_P 400
#define SO_MAX_TIME 1
#define SO_BASE 120
#define SO_ALTEZZA 40
#define SO_FLAG_MIN 5
#define SO_FLAG_MAX 40
#define SO_ROUND_SCORE 200
#define SO_N_MOVES 200
#define SO_MIN_HOLD_NSEC 100000000
*/

unsigned int ready_players, current_placing_player, current_round, total_playing_time, conquered_flags, flag_count;
player_t player_list[SO_NUM_G];
time_t round_start_time;
int game_board_shm_id;
board_t* game_board;

void signal_handler(int signo);
void exec_round();
void handle_message(message_t* message);
void end_round();
void end_game();
void kill_em_all();
void start_over_again();

int main() {
    message_t message;

    printf("Starting up...\n");
    current_round = total_playing_time = 0;
    printf("Generating the game board...\n");
    /* Generate, allocate and then attach the whole game board. */
    game_board_shm_id = generate_board(SO_BASE, SO_ALTEZZA);
    game_board = get_board(game_board_shm_id);
    game_board->waiting_time = SO_MIN_HOLD_NSEC;
    printf("Generated a %dx%d board.\n", SO_BASE, SO_ALTEZZA);
    /* Setup the signal handler used to handle SIGALRM whenever a timer expire. */
    signal(SIGALRM, signal_handler);
    printf("Spawning players...\n");
    /* Spawn the players' processes. */
    spawn_players(player_list, game_board_shm_id, SO_NUM_G, SO_NUM_P, SO_N_MOVES);
    if ( game_board->coordinator_pid == getpid() ){
        printf("Spawned %d players.\n", SO_NUM_G);
        /* Start listening for incoming messages. */
        while (1){
            /* Pull a message from the message queue. */
            message = receive_message(game_board->coordinator_mq_id);
            handle_message(&message);
        }
    }
    return 0;
}

/**
 * Handles a messages sent by a player or pawn.
 *
 * @param message The reference to the message to handle.
 */
void handle_message(message_t* message){
    unsigned int index;

    switch ( message->message_type ){
        case 1: {
            /* A player is ready to place his pawns. */
            ready_players++;
            if ( ready_players == SO_NUM_G ){
                current_placing_player = ready_players = 0;
                /* Signal players they can place their pawns (one for each player). */
                allow_pawn_placing(&player_list[current_placing_player]);
            }
        }break;
        case 3: {
            /* A player has placed a pawn. */
            current_placing_player++;
            if ( current_placing_player == SO_NUM_G ){
                current_placing_player = 0;
            }
            /* Allow players to place another pawn. */
            allow_pawn_placing(&player_list[current_placing_player]);
        }break;
        case 4: {
            /* A player has placed all its pawns, as they are synchronized, other players did the same. */
            exec_round();
        }break;
        case 6: {
            /* A player is ready to start playing the round. */
            ready_players++;
            if ( ready_players == SO_NUM_G ){
                ready_players = 0;
                round_start_time = time(NULL);
                /* Set the timer that will stop the game if flags are not all conquered. */
                alarm(SO_MAX_TIME);
                /* Signal the players the round has started. */
                broadcast_signal_to_players(player_list, SO_NUM_G, 7);
            }
        }break;
        case 9:{
            /* A pawn has conquered a flag. */
            printf("Flag conquered by %c!\n", message->player_pseudo_name);
            /* Propagate the event to other players. */
            broadcast_signal_to_players(player_list, SO_NUM_G, 9);
            conquered_flags++;
            if ( conquered_flags == flag_count ){
                printf("Every flag has been conquered, ending current round.\n");
                /* Start a new round. */
                end_round();
                print_status(game_board, player_list, SO_NUM_G);
                start_over_again();
            }
        }break;
        case 10:{
            /* A pawn has moved, update moves counter. */
            index = get_player_index(player_list, SO_NUM_G, message->player_pseudo_name);
            if ( index != -1 ){
                player_list[index].available_moves--;
            }
        }break;
    }
}

/**
 * Execute a new round.
 */
void exec_round(){
    printf("Starting a new round!\n");
    conquered_flags = 0;
    current_round++;
    game_board->round_in_progress = 1;
    /* Spawn a random number of flags on the game board. */
    flag_count = spawn_flags(game_board, SO_FLAG_MIN, SO_FLAG_MAX, SO_ROUND_SCORE);
    printf("Spawned %d flags.\n", flag_count);
    /* Print out a graphic representation of the game board. */
    print_board(game_board);
    printf("Game start!\n");
    /* Warn the players a new round is about to start. */
    broadcast_signal_to_players(player_list, SO_NUM_G, 5);
}

/**
 * Handles the "SIGALRM" signal.
 *
 * @param signo The signal to handle.
 */
void signal_handler(int signo){
    if ( signo == SIGALRM ){
        /* Time has expired, end the game. */
        end_game();
    }
}

/**
 * Ends current round.
 */
void end_round(){
    game_board->round_in_progress = 0;
    /* Update the score counter for each player. */
    update_players_score(game_board, player_list, SO_NUM_G, 1);
    total_playing_time += time(NULL) - round_start_time;
    /* Stop the game timer. */
    alarm(0);
}

/**
 * Ends the whole game.
 */
void end_game(){
    /* Stop current round. */
    end_round();
    /* Kill each player/pawn processes. */
    kill_em_all();
    printf("GAME OVER (time out)!\n");
    /* Print out the game board representation, players stats and game metrics. */
    print_status(game_board, player_list, SO_NUM_G);
    print_metrics(player_list, SO_NUM_G, current_round, total_playing_time);
    printf("Deallocating resources and ending the game.\n");
    /* Deallocate all the resources. */
    destroy_board(game_board);
    printf("Bye bye!\n");
    exit(0);
}

/**
 * Kills each player/pawn processes.
 */
void kill_em_all(){
    unsigned int i;

    /* Inform the player processes that they must terminate. */
    broadcast_signal_to_players(player_list, SO_NUM_G, 11);
    for ( i = 0 ; i < SO_NUM_G ; i++ ){
        /* Remove the message queues associated to the players. */
        close_message_queue(player_list[i].mq_id);
    }
}

/**
 * Start a new round.
 */
void start_over_again(){
    unsigned int i, moves;

    moves = SO_NUM_P * SO_N_MOVES;
    /* Restore the moves count for each player. */
    for ( i = 0 ; i < SO_NUM_G ; i++ ){
        player_list[i].available_moves = moves;
    }
    /* Remove old flags from the game board. */
    remove_flags(game_board);
    /* Inform the players a new round is about to start. */
    broadcast_signal_to_players(player_list, SO_NUM_G, 12);
    /* Start a new round. */
    exec_round();
}
