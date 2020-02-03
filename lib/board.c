#include "board.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/shm.h>
#include <sys/ipc.h>

#if __APPLE__
    #include <limits.h>
#else
    #include <linux/limits.h>
#endif

#include "communicator.h"
#include "types.h"
#include "player.h"

/**
 * Generates an unique key used to allocate a shared memory segment.
 *
 * @param id An integer number representing the segment ID.
 *
 * @return The generated unique key.
 *
 * @private
 */
key_t generate_shm_key(int id){
    char current_path[PATH_MAX];
    key_t shm_key;

    /* Get the application path. */
    getcwd(current_path, sizeof(current_path));
    /* Generates the segment key. */
    shm_key = ftok(current_path, id);
    return shm_key;
}

/**
 * Allocates a shared memory segment according to a given size.
 *
 * @param size An integer number representing the segment size in bytes.
 * @param id An integer number used as a namespace to separate different memory segments having the same key.
 *
 * @return An integer number representing the shared memory segment ID.
 *
 * @private
 */
int generate_shared_memory_segment(size_t size, int id){
    key_t shm_key;
    int shm_id;

    /* Generate an unique shared memory segment key. */
    shm_key = generate_shm_key(id);
    /* Allocate the shared memory segment. */
    shm_id = shmget(shm_key, size, IPC_CREAT | 0660);
    if ( shm_id < 0 ){
        printf("Cannot allocate the memory segment, aborting.\n");
        printf("Reported error: %s.\n", strerror(errno));
        exit(1);
    }
    return shm_id;
}

/**
 * Allocates the whole game board as a shared memory segment.
 *
 * @param width An integer number representing the chess board width.
 * @param height An integer number representing the chess board height.
 *
 * @return An integer number representing the shared memory segment ID.
 *
 * @private
 */
int allocate_board(int width, int height){
    size_t size;
    int shm_id;

    /* Calculate the size of the memory segment to allocate. */
    size = sizeof(board_t) + ( sizeof(cell_t) * height * width );
    /* Allocate the memory segment. */
    shm_id = generate_shared_memory_segment(size, 1);
    return shm_id;
}

/**
 * Returns the uni-dimensional array index based on given bi-dimensional coordinates.
 *
 * @param game_board The reference to the game board.
 * @param coords The coordinates to convert.
 *
 * @return An integer number representing the equivalent array index.
 */
unsigned int compute_index(board_t* game_board, coords_t* coords){
    return coords->x * game_board->height + coords->y;
}

/**
 * Returns the uni-dimensional array index based on given coordinates a integer numbers.
 *
 * @param game_board The reference to the game board.
 * @param x The coordinate value on the x axis.
 * @param y The coordinate value on the y axis.
 *
 * @return An integer number representing the equivalent array index.
 */
unsigned int compute_index_from_params(board_t* game_board, unsigned int x, unsigned int y){
    return x * game_board->height + y;
}

/**
 * Generate the game board as a shared memory segment.
 *
 * @param width An integer number representing the chess board width.
 * @param height An integer number representing the chess board height.
 *
 * @return An integer number representing the ID of the shared memory segment where the game board has been allocated at.
 */
int generate_board(int width, int height){
    unsigned int x, y, index;
    board_t* game_board;
    int shm_id;

    shm_id = allocate_board(width, height);
    game_board = get_board(shm_id);
    /* Set basic board attributes. */
    game_board->width = width;
    game_board->height = height;
    game_board->coordinator_mq_id = generate_message_queue();
    game_board->coordinator_pid = getpid();
    game_board->waiting_time = game_board->round_in_progress = 0;
    /* Initialize each board cell. */
    for ( x = 0 ; x < width ; x++ ){
        for ( y = 0 ; y < height ; y++ ){
            index = compute_index_from_params(game_board, x, y);
            game_board->cells[index].flag_score = 0;
            game_board->cells[index].occupant_type = 0;
            game_board->cells[index].player_pseudo_name = 0;
            sem_init(&game_board->cells[index].mutex, 0, 1);
        }
    }
    return shm_id;
}

/**
 * Returns the reference to the game board according to a given shared memory segment ID.
 *
 * @param shm_id An integer number representing he shared memory segment ID.
 *
 * @return A pointer to the game board that has been attached.
 */
board_t* get_board(int shm_id){
    board_t* game_board;
    void* shm_ptr;

    /* Attach the allocated shared memory segment. */
    shm_ptr = shmat(shm_id, NULL, 0);
    if ( (int)shm_ptr == -1 ){
        printf("Cannot attach the shared memory segment, aborting.\n");
        printf("Reported error: %s.\n", strerror(errno));
        exit(2);
    }
    game_board = (board_t *)shm_ptr;
    return game_board;
}

/**
 * Returns a couple of coordinates designating a free position in the game board picked randomly.
 *
 * @param game_board The reference to the game board.
 * @param allow_occupied_by_flags If set to "true" cells occupied by a flag will be considered as well as free ones.
 *
 * @return The coordinates found.
 */
coords_t get_random_position(board_t* game_board, boolean allow_occupied_by_flags){
    unsigned short min_occupant_type, current_occupant_type;
    pid_t current_pid;
    coords_t coords;

    min_occupant_type = allow_occupied_by_flags == 1 ? 1 : 0;
    current_pid = getpid();
    srand(current_pid);
    do {
        /* Loop until a cell having entity type zero (no entity present) is found. */
        coords.x = (int)lrand48() % game_board->width;
        coords.y = (int)lrand48() % game_board->height;
        /* Convert a 2D matrix index into a 1D array index. */
        coords.index = compute_index(game_board, &coords);
        current_occupant_type = game_board->cells[coords.index].occupant_type;
    } while( current_occupant_type > min_occupant_type );
    return coords;
}

/**
 * Places a pawn on a given cell.
 *
 * @param game_board The reference to the game board.
 * @param position The position where the pawn should be placed at.
 * @param player_pseudo_name The pseudo name associated to the player this pawn belongs to.
 *
 * @return If a flag is already present in the given position it will be conquered and "1" will be returned.
 */
boolean place_pawn(board_t* game_board, coords_t* position, char player_pseudo_name){
    boolean has_conquered_flag;

    has_conquered_flag = 0;
    sem_wait(&game_board->cells[position->index].mutex);
    if ( game_board->cells[position->index].occupant_type == 0 ){
        game_board->cells[position->index].occupant_type = 2;
        game_board->cells[position->index].player_pseudo_name = player_pseudo_name;
    }else if ( game_board->cells[position->index].occupant_type == 1 ){
        game_board->cells[position->index].occupant_type = 2;
        game_board->cells[position->index].player_pseudo_name = player_pseudo_name;
        has_conquered_flag = 1;
    }
    sem_post(&game_board->cells[position->index].mutex);
    return has_conquered_flag;
}

/**
 * Checks if a pawn can be placed in a given position.
 *
 * @param game_board The reference to the game board.
 * @param position The position where the pawn is going to be placed at.
 *
 * @return If the pawn can be placed will be returned "1".
 */
boolean is_allowed_position(board_t* game_board, coords_t* position){
    boolean is_allowed;

    sem_wait(&game_board->cells[position->index].mutex);
    is_allowed = game_board->cells[position->index].occupant_type <= 1 ? 1 : 0;
    sem_post(&game_board->cells[position->index].mutex);
    return is_allowed;
}

/**
 * Moves a pawn from the given current position to a new one.
 *
 * @param game_board
 * @param old_position Current pawn position.
 * @param new_position The position where the pawn should be moved to.
 * @param player_pseudo_name The pseudo name associated to the player this pawn belongs to.
 *
 * @return IF the pawn has been moved to a cell where a flag was present will be returned "1" as that flag has been captured.
 */
boolean move_pawn(board_t* game_board, coords_t* old_position, coords_t* new_position, char player_pseudo_name){
    boolean has_conquered_flag;
    struct timespec wait;

    has_conquered_flag = 0;
    wait.tv_sec = 0;
    wait.tv_nsec = game_board->waiting_time;
    if ( is_allowed_position(game_board, new_position) == 1 ){
        sem_wait(&game_board->cells[old_position->index].mutex);
        game_board->cells[old_position->index].occupant_type = 0;
        game_board->cells[old_position->index].player_pseudo_name = 0;
        sem_post(&game_board->cells[old_position->index].mutex);
        has_conquered_flag = place_pawn(game_board, new_position, player_pseudo_name);
    }
    nanosleep(&wait, NULL);
    return has_conquered_flag;
}

/**
 * Spawns the flags on the game board.
 *
 * @param game_board The reference to the game board.
 * @param min The minimum number of flags that should be placed.
 * @param max The maximum number of flags that should be placed.
 * @param max_score An integer number representing he sum of the scores of all the generated flags.
 *
 * @return The number of generated flags.
 */
unsigned int spawn_flags(board_t* game_board, unsigned int min, unsigned int max, unsigned int max_score){
    unsigned int i, flag_count, n, score;
    coords_t position;

    /* Generate the flag count. */
    flag_count = (int)lrand48() % ( max + 1 - min ) + min;
    n = flag_count;
    for ( i = 0 ; i < flag_count ; i++ ){
        /* Generate the score value for this flag. */
        score = (int)lrand48() % ( max_score - n ) + 1;
        max_score -= score;
        n--;
        /* Generate a random position where this flag should be placed at. */
        position = get_random_position(game_board, 0);
        /* Place the flag on the board, dont use a semaphore as this method is used when no pawn is moving. */
        game_board->cells[position.index].occupant_type = 1;
        game_board->cells[position.index].player_pseudo_name = 0;
        game_board->cells[position.index].flag_score = score;
    }
    return flag_count;
}

/**
 * Prints out the whole game board and all the entities on it.
 *
 * @param game_board The reference to the game board.
 */
void print_board(board_t* game_board){
    unsigned int x, y, index;

    /* Print the x axis. */
    printf("\n    ");
    for ( x = 1 ; x <= game_board->width ; x++ ){
        if ( x < 100 && x >= 10 ){
            printf("| 0%d", x);
        }else if ( x < 10 ){
            printf("| 00%d", x);
        }else{
            printf("| %d", x);
        }
    }
    printf("|\n    |");
    /* Print the separator after the x axis header. */
    for ( x = 1 ; x <= game_board->width ; x++ ){
        printf(x == 1 ? "----" : "-----");
    }
    printf("|\n");
    for ( y = 0 ; y < game_board->height ; y++ ){
        /* Print the left block of the y axis. */
        if ( y < 99 && y >= 9 ){
            printf(" 0%d", y + 1);
        }else if ( y < 9 ){
            printf(" 00%d", y + 1);
        }else{
            printf(" %d", y + 1);
        }
        /* Print a whole row. */
        for ( x = 0 ; x < game_board->width ; x++ ){
            index = compute_index_from_params(game_board, x, y);
            switch ( game_board->cells[index].occupant_type ){
                case 0: {
                    /* This is an empty cell. */
                    printf("|    ");
                }break;
                case 1: {
                    /* This cell contains a flag. */
                    if ( game_board->cells[index].player_pseudo_name > 0 ){
                        /* This flag has been conquered by a process. */
                        printf("|\033[1;34♟\033[0m-\033[31m⚑%c \033[0m", game_board->cells[index].player_pseudo_name);
                    }else{
                        printf("|  \033[34m⚑\033[0m ");
                    }
                }break;
                case 2: {
                    /* This cell contains a pawn. */
                    printf("| \033[1;34♟\033[0m%c ", game_board->cells[index].player_pseudo_name);
                }break;
            }
        }
        printf("|\n    |");
        /* Print the row separator. */
        for ( x = 0 ; x <= game_board->width ; x++ ){
            printf(x == 0 ? "" : ( x == 2 ? "----" : "-----" ));
        }
        printf("|\n");
    }
    printf("\n");
}

/**
 * Prints out the round stats.
 *
 * @param game_board The reference to the game board.
 * @param player_list THe pointer to the list of all the players spawned.
 * @param player_count An integer number representing the number of players spawned.
 */
void print_stats(board_t* game_board, player_t* player_list, unsigned int player_count){
    unsigned int x, y, i, index, player_index;
    unsigned int scores[player_count];

    /* Iterate the whole game board. */
    for ( y = 0 ; y < game_board->height ; y++ ){
        for ( x = 0 ; x < game_board->width ; x++ ){
            index = compute_index_from_params(game_board, x, y);
            /* Check if current cell is owned by a player. */
            if ( game_board->cells[index].player_pseudo_name != 0 ){
                /* Find out which player own current cell. */
                player_index = get_player_index(player_list, player_count, game_board->cells[index].player_pseudo_name);
                if ( player_index != -1 ){
                    /* Increment the player found's score. */
                    scores[player_index] += game_board->cells[index].flag_score;
                }
            }
        }
    }
    printf("Round stats: \n");
    /* Print the stats for each player. */
    for ( i = 0 ; i < player_count ; i++ ){
        printf("Player %c:\n", player_list[i].pseudo_name);
        printf("\tScore: %d.\n", scores[i]);
        printf("\tRemaining moves: %d.\n\n", scores[i]);
    }
    printf("\n");
}

/**
 * Prints the game status including the whole game board representation and players' stats.
 *
 * @param game_board The reference to the game board.
 * @param player_list THe pointer to the list of all the players spawned.
 * @param player_count An integer number representing the number of players spawned.
 */
void print_status(board_t* game_board, player_t* player_list, unsigned int player_count){
    print_board(game_board);
    print_stats(game_board, player_list, player_count);
}

/**
 * Prints out whole game metrics.
 *
 * @param game_board The reference to the game board.
 * @param player_list THe pointer to the list of all the players spawned.
 * @param player_count An integer number representing the number of players spawned.
 * @param rounds AN integer number representing the number of rounds played.
 * @param total_playing_time An integer number representing the amount of seconds played.
 */
void print_metrics(player_t* player_list, unsigned int player_count, unsigned int rounds, unsigned int total_playing_time){
    float ratio, used_moves, total_score;
    unsigned int i;

    printf("Metrics: \n");
    printf("Total rounds played: %d.\n", rounds);
    printf("Moves ratio: \n");
    /* Compute and print each player's moves ratio. */
    for ( i = 0 ; i < player_count ; i++ ){
        ratio = (float)player_list[i].available_moves / (float)player_list[i].total_moves;
        printf("\tPlayer %c's moves ratio: %f.\n", player_list[i].pseudo_name, ratio);
    }
    printf("Score/moves ratio: \n");
    total_score = 0;
    for ( i = 0 ; i < player_count ; i++ ){
        used_moves = (float)player_list[i].total_moves - (float)player_list[i].available_moves;
        ratio = (float)player_list[i].global_score / used_moves;
        total_score += (float)player_list[i].global_score;
        printf("\tPlayer %c's score/moves ratio: %f.\n", player_list[i].pseudo_name, ratio);
    }
    if ( total_playing_time > 0 ){
        ratio = total_score / (float)total_playing_time;
        printf("Score/time ratio: %f.\n", ratio);
    }
}

/**
 * Deallocates each semaphore assigned to cells and the message queue assigned to the master process.
 *
 * @param game_board The reference to the game board.
 */
void destroy_board(board_t* game_board){
    unsigned int length, i;

    length = game_board->width * game_board->height;
    /* Iterate each board cell and deallocate the corresponding semaphore. */
    for ( i = 0 ; i < length ; i++ ){
        if ( sem_destroy(&game_board->cells[i].mutex) == -1 ){
            printf("Cannot destroy the semaphore, aborting.\n");
            printf("Reported error: %s.\n", strerror(errno));
            exit(5);
        }
    }
    /* Deallocate the message queue assigned to the master process. */
    close_message_queue(game_board->coordinator_mq_id);
}

/**
 * Removes all the flags that have been placed on the game board.
 *
 * @param game_board The reference to the game board.
 */
void remove_flags(board_t* game_board){
    unsigned int length, i;

    length = game_board->width * game_board->height;
    for ( i = 0 ; i < length ; i++ ){
        if ( game_board->cells[i].occupant_type == 1 ){
            game_board->cells[i].occupant_type = 0;
        }
        /* Remove the score assigned to the cell (flag or conquered flag). */
        game_board->cells[i].flag_score = 0;
    }
}
