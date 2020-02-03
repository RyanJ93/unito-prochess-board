#ifndef PROCHESS_COMMUNICATOR_H
#define PROCHESS_COMMUNICATOR_H

#include <sys/types.h>

#include "types.h"

void send_message(int mq_id, message_t* msg);
message_t receive_message(int mq_id);
void close_message_queue(int mq_id);
int generate_message_queue();

#endif
