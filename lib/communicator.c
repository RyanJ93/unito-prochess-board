#include "communicator.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/msg.h>
#include <errno.h>
#include <string.h>

#include "types.h"

/**
 * Initializes a new message queue.
 *
 * @return An integer number representing the message queue ID.
 */
int generate_message_queue(){
    int message_queue_id;

    /* Allocate a new message queue. */
    message_queue_id = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if ( message_queue_id == -1 ) {
        printf("Cannot initialize a new message queue, aborting.\n");
        printf("Reported error: %s.\n", strerror(errno));
        exit(2);
    }
    return message_queue_id;
}

/**
 * Sends a given message to the given message queue.
 *
 * @param mq_id An integer number representing he ID of the message queue the message will be sent to.
 * @param msg The reference to the message to send.
 */
void send_message(int mq_id, message_t* msg){
    size_t size;
    int result;

    /* Get the length of the payload string. */
    size = strlen(msg->payload) + 1;
    /* Send the message. */
    result = msgsnd(mq_id, msg, size, 0);
    if ( result == -1 ){
        printf("Cannot send the message, aborting.\n");
        printf("Reported error: %s.\n", strerror(errno));
        exit(4);
    }
}

/**
 * Pops a message from the given message queue.
 *
 * @param mq_id An integer number representing he ID of the message queue.
 *
 * @return The message extracted.
 */
message_t receive_message(int mq_id){
    message_t msg;
    int result;

    /* Receive the message from the message queue. */
    result = msgrcv(mq_id, &msg, 8196, 0, 0);
    if ( result == -1 ){}
    return msg;
}

/**
 * Destroy a given message queue.
 *
 * @param mq_id An integer number representing he ID of the message queue.
 */
void close_message_queue(int mq_id){
    /* Destroy the message queue. */
    if ( msgctl(mq_id, IPC_RMID, NULL) == -1 ){
        printf("Cannot close the message queue, aborting.\n");
        printf("Reported error: %s.\n", strerror(errno));
        exit(5);
    }
}
