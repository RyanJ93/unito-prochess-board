#define _POSIX_SOURCE

#include "player.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "board.h"
#include "pawn.h"
#include "communicator.h"
#include "types.h"

/**
 * Sends a simple numeric signal to the master process.
 *
 * @param game_board The reference to the game board.
 * @param type An integer number containing the signal code.
 *
 * @private
 */
void send_signal_message_to_master(board_t* game_board, unsigned short type){
    message_t message;

    /* Prepare the message struct. */
    message.message_type = type;
    message.player_pseudo_name = 0;
    /* Send the message to the master process's message queue. */
    send_message(game_board->coordinator_mq_id, &message);
}

/**
 * Signals to the master process the player has placed a single pawn or all of them.
 *
 * @param game_board The reference to the game board.
 * @param all_placed If set to "1" the master process will be informed that all the available pawns have been placed.
 *
 * @private
 */
void end_placement(board_t* game_board, boolean all_placed){
    unsigned short message_type;

    message_type = all_placed == 1 ? 4 : 3;
    send_signal_message_to_master(game_board, message_type);
}

/**
 * Informs the master process the player is ready to enter the game and place its pawns.
 *
 * @param game_board The reference to the game board.
 *
 * @private
 */
void ready_up(board_t* game_board){
    send_signal_message_to_master(game_board, 1);
}

/**
 * Tell to the master process that the player is ready to start the round.
 *
 * @param game_board The reference to the game board.
 *
 * @private
 */
void organization_completed(board_t* game_board){
    send_signal_message_to_master(game_board, 6);
}

/**
 * Informs all the pawns contained in the given list that they must end their processes.
 *
 * @param pawn_list The reference to the list of all the pawns owned by a player.
 * @param pawn_count An integer number representing he amount of generated pawns.
 *
 * @private
 */
void destroy_pawns(pawn_t* pawn_list, unsigned int pawn_count){
    unsigned int i;

    /* Send the message in order to inform about the end of their processes. */
    broadcast_signal_to_pawns(pawn_list, pawn_count, 11);
    /* Deallocate the message queue assigned to the pawns. */
    for ( i = 0 ; i < pawn_count ; i++ ){
        close_message_queue(pawn_list[i].mq_id);
    }
}

/**
 * Signals a given player that it can place a single pawn on the game board.
 *
 * @param player The reference to the player the signal will be sent to.
 */
void allow_pawn_placing(player_t* player){
    message_t message;

    /* Prepare the message struct. */
    message.message_type = 2;
    message.player_pseudo_name = 0;
    /* Send the message to the player's message queue. */
    send_message(player->mq_id, &message);
}

/**
 * Generates the player processes.
 *
 * @param player_list The reference to the list where player information will be stored in.
 * @param game_board_shm_id The ID of the shared memory segment where the game board is stored in.
 * @param player_count An integer number representing the amount of players to spawn.
 * @param pawn_count An integer number representing the amount of pawns each player should spawn.
 * @param max_pawn_moves An integer number representing the amount of moves each pawn can do.
 */
void spawn_players(player_t* player_list, int game_board_shm_id, unsigned int player_count, int pawn_count, unsigned int max_pawn_moves){
    pid_t player_pid;
    char pseudo_name;
    int player_mq_id;
    unsigned int i;

    for ( i = 0 ; i < player_count ; i++ ){
        pseudo_name = i + 65;
        /* Allocate a new message queue for current player. */
        player_mq_id = generate_message_queue();
        /* Create the player process. */
        player_pid = fork();
        if ( player_pid == -1 ){
            printf("Cannot fork process, aborting.\n");
            exit(3);
        }else if ( player_pid == 0 ){
            pawn_t pawn_list[pawn_count];
            board_t* game_board;
            int remaining_pawns;
            message_t message;

            printf("Player %d (%c) has entered the game.\n", i + 1, pseudo_name);
            remaining_pawns = pawn_count - 1;
            /* Attach the game board to current process memory. */
            game_board = get_board(game_board_shm_id);
            /* Signal the master process this player is ready to place its pawns. */
            ready_up(game_board);
            /* Start listening for incoming messages. */
            while (1){
                /* Pull a message from the message queue. */
                message = receive_message(player_mq_id);
                switch ( message.message_type ){
                    case 2: {
                        if ( remaining_pawns >= 0 ){
                            /* There are still pawns to place, place another pawn. */
                            pawn_list[remaining_pawns] = spawn_pawn(game_board, pseudo_name, game_board_shm_id, max_pawn_moves);
                            remaining_pawns--;
                            /* Inform the master process a pawn has been placed. */
                            end_placement(game_board, 0);
                        }else{
                            /* Inform the master process that all the pawns have been placed. */
                            end_placement(game_board, 1);
                        }
                    }break;
                    case 5: {
                        /* Inform the master process that this player is ready to play current round. */
                        organization_completed(game_board);
                    }break;
                    case 7: {
                        broadcast_signal_to_pawns(pawn_list, pawn_count, 8);
                    }break;
                    case 9: {
                        broadcast_signal_to_pawns(pawn_list, pawn_count, 9);
                    }break;
                    case 11:{
                        /* Signal all the player's pawns that they must terminate their processes. */
                        destroy_pawns(pawn_list, pawn_count);
                        exit(0);
                    }
                    case 12: {
                        /**/
                        broadcast_signal_to_pawns(pawn_list, pawn_count, 12);
                    }break;
                }
            }
        }else{
            /* Setup player's information. */
            player_list[i].mq_id = player_mq_id;
            player_list[i].pid = player_pid;
            player_list[i].pseudo_name = pseudo_name;
            player_list[i].available_moves = pawn_count * max_pawn_moves;
            player_list[i].total_moves = player_list[i].available_moves;
            player_list[i].total_score = player_list[i].global_score = 0;
        }
    }
}

/**
 * Sends a given message to all the players contained in the given player list.
 *
 * @param player_list A reference to the list of all the players the given message will be sent to.
 * @param player_count The amount of players in the given list.
 * @param message A reference to the message to send.
 */
void broadcast_message_to_players(player_t* player_list, unsigned int player_count, message_t* message){
    unsigned int i;

    for ( i = 0 ; i < player_count ; i++ ){
        send_message(player_list[i].mq_id, message);
    }
}

/**
 * Sends a message containing a plain numeric signal to all the players contained in the given player list.
 *
 * @param player_list A reference to the list of all the players the given signal will be sent to.
 * @param player_count The amount of players in the given list.
 * @param type The signal to send.
 */
void broadcast_signal_to_players(player_t* player_list, unsigned int player_count, unsigned short type){
    message_t message;
    unsigned int i;

    /* Prepare the message properties. */
    message.message_type = type;
    message.player_pseudo_name = 0;
    for ( i = 0 ; i < player_count ; i++ ){
        send_message(player_list[i].mq_id, &message);
    }
}

/**
 * Looks up a player in a given list by the given pseudo name.
 *
 * @param player_list A reference to the list of all the player the given signal will be sent to.
 * @param player_count The amount of players in the given list.
 * @param player_pseudo_name The pseudo name to lookup.
 *
 * @return The array index where the player was found.
 */
unsigned int get_player_index(player_t* player_list, unsigned int player_count, char player_pseudo_name){
    unsigned int i, index;

    index = -1;
    i = 0;
    while ( index == -1 && i < player_count ){
        if ( player_list[i].pseudo_name == player_pseudo_name ){
            index = i;
        }
        i++;
    }
    return index;
}

/**
 * Returns the score of the given player.
 *
 * @param game_board The reference to the game board.
 * @param player_pseudo_name The pseudo name of the player.
 *
 * @return THe sum of the scores of all the flags conquered by the given player.
 */
unsigned int get_player_score(board_t* game_board, char player_pseudo_name){
    unsigned int i, length, score;

    length = game_board->width * game_board->height;
    score = 0;
    for ( i = 0 ; i < length ; i++ ){
        if ( game_board->cells[i].player_pseudo_name == player_pseudo_name ){
            score += game_board->cells[i].flag_score;
        }
    }
    return score;
}

/**
 * Updates the score of each player contained in the given list.
 *
 * @param game_board The reference to the game board.
 * @param player_list A reference to the list of all the player the given signal will be sent to.
 * @param player_count The amount of players in the given list.
 * @param update_glob If set to "1" the "global_score" property will be incremented by the total value computed.
 */
void update_players_score(board_t* game_board, player_t* player_list, unsigned int player_count, boolean update_glob){
    unsigned int i;

    for ( i = 0 ; i < player_count ; i++ ){
        player_list[i].total_score = get_player_score(game_board, player_list[i].pseudo_name);
    }
    if ( update_glob == 1 ){
        for ( i = 0 ; i < player_count ; i++ ){
            player_list[i].global_score += player_list[i].total_score;
        }
    }
}
