#include "pawn.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "board.h"
#include "communicator.h"
#include "types.h"

/**
 * Returns the position where a pawn should be moved to.
 *
 * @param game_board The reference to the game board.
 * @param current_position The reference to the current position of the pawn to move.
 *
 * @return The suggested position.
 *
 * @private
 */
coords_t get_next_position(board_t* game_board, coords_t* current_position){
    coords_t position;
    int orientation;
    boolean valid;

    do{
        valid = 1;
        /* Pick a random direction. */
        orientation = (int)lrand48() % 5;
        switch ( orientation ){
            case 1: {
                if ( current_position->y == 0 ){
                    /* Position would be out of the board (top). */
                    valid = 0;
                }else{
                    position.x = current_position->x;
                    position.y = current_position->y - 1;
                }
            }break;
            case 2: {
                if ( current_position->x == game_board->width ){
                    /* Position would be out of the board (right). */
                    valid = 0;
                }else{
                    position.x = current_position->x + 1;
                    position.y = current_position->y;
                }
            }break;
            case 3: {
                if ( current_position->y == game_board->height ){
                    /* Position would be out of the board (bottom). */
                    valid = 0;
                }else{
                    position.x = current_position->x;
                    position.y = current_position->y + 1;
                }
            }break;
            case 4: {
                if ( current_position->x == 0 ){
                    /* Position would be out of the board (left). */
                    valid = 0;
                }else{
                    position.x = current_position->x - 1;
                    position.y = current_position->y;
                }
            }break;
            default: {
                valid = 0;
            }break;
        }
    }while( valid == 0 );
    /* Set the 1D based index corresponding to the generated position. */
    position.index = compute_index(game_board, &position);
    return position;
}

/**
 * Informs the master process that a flag has been conquered.
 *
 * @param game_board The reference to the game board.
 * @param player_pseudo_name The pawn owner player's pseudo name.
 *
 * @private
 */
void signal_achievement(board_t* game_board, char player_pseudo_name){
    message_t message;

    /* Create the message. */
    message.message_type = 9;
    message.player_pseudo_name = player_pseudo_name;
    send_message(game_board->coordinator_mq_id, &message);
}

/**
 * Informs the master process that the pawn has moved.
 *
 * @param game_board The reference to the game board.
 * @param player_pseudo_name The pawn owner player's pseudo name.
 *
 * @private
 */
void notify_movement(board_t* game_board, char player_pseudo_name){
    message_t message;

    /* Create the message. */
    message.message_type = 10;
    message.player_pseudo_name = player_pseudo_name;
    send_message(game_board->coordinator_mq_id, &message);
}

/**
 * Generates and place the given pawns.
 *
 * @param game_board The reference to the game board.
 * @param player_pseudo_name The pseudo name associated to the player pawn will belong to.
 * @param game_board_shm_id The ID of the shared memory segment where the game board has been allocated at.
 * @param max_moves The maximum number of moves a pawn can do during a round.
 *
 * @return A structure representing the pawn spawned.
 */
pawn_t spawn_pawn(board_t* game_board, char player_pseudo_name, int game_board_shm_id, unsigned int max_moves){
    pid_t pawn_pid;
    int pawn_mq_id;
    pawn_t pawn;

    /* Allocate a new message queue for the pawn that is going to be generated. */
    pawn_mq_id = generate_message_queue();
    pawn_pid = fork();
    if ( pawn_pid == -1 ){
        printf("Cannot fork process, aborting.\n");
        exit(5);
    }else if ( pawn_pid == 0 ){
        unsigned int available_moves;
        boolean has_conquered_flag;
        board_t* local_game_board;
        coords_t next_position;
        message_t message;
        coords_t position;

        available_moves = max_moves;
        /* Attach the game board to current process memory. */
        local_game_board = get_board(game_board_shm_id);
        /* Pick a random position where the pawn will be placed to. */
        position = get_random_position(game_board, 0);
        /* Place the pawn on the game board according tot he generated random position. */
        place_pawn(local_game_board, &position, player_pseudo_name);
        while(1){
            message = receive_message(pawn_mq_id);
            switch ( message.message_type ){
                case 8: {
                    while ( available_moves > 0 ){
                        /* Get the position where the pawn should be moved to. */
                        next_position = get_next_position(local_game_board, &position);
                        /* Move the pawn and check if a flag is present in its new position. */
                        has_conquered_flag = move_pawn(local_game_board, &position, &next_position, player_pseudo_name);
                        position = next_position;
                        available_moves--;
                        /* Inform the master process the pawn has moved. */
                        notify_movement(local_game_board, player_pseudo_name);
                        if ( has_conquered_flag == 1 ){
                            available_moves = 0;
                            /* Signal the master process a flag has been captured. */
                            signal_achievement(local_game_board, player_pseudo_name);
                        }
                    }
                }break;
                case 11: {
                    exit(0);
                }
                case 12: {
                    available_moves = max_moves;
                }break;
            }
        }
    }else{
        /* Setup pawn's information. */
        pawn.mq_id = pawn_mq_id;
        pawn.pid = pawn_pid;
    }
    return pawn;
}

/**
 * Sends a given message to all the pawns contained in the given pawn list.
 *
 * @param pawn_list A reference to the list of all the pawns the given message will be sent to.
 * @param pawn_count The amount of pawns in the given list.
 * @param message A reference to the message to send.
 */
void broadcast_message_to_pawns(pawn_t* pawn_list, unsigned int pawn_count, message_t* message){
    unsigned int i;

    for ( i = 0 ; i < pawn_count ; i++ ){
        send_message(pawn_list[i].mq_id, message);
    }
}

/**
 * Sends a message containing a plain numeric signal to all the pawns contained in the given pawn list.
 *
 * @param pawn_list A reference to the list of all the pawns the given signal will be sent to.
 * @param pawn_count The amount of pawns in the given list.
 * @param message A reference to the message to send.
 */
void broadcast_signal_to_pawns(pawn_t* pawn_list, unsigned int pawn_count, unsigned short type){
    message_t message;
    unsigned int i;

    /* Prepare the message properties. */
    message.message_type = type;
    message.player_pseudo_name = 0;
    for ( i = 0 ; i < pawn_count ; i++ ){
        send_message(pawn_list[i].mq_id, &message);
    }
}
