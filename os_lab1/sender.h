#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <time.h>
// 讓 mailbox.flag 用數字選擇機制
#define MSG_PASSING 1
#define SHARED_MEM 2

typedef struct {
    int flag;      // 1 for message passing, 2 for shared memory
    union{
        int msqid; // for System V message queue
        char* shm_addr; // shared memory base address
    }storage;
} mailbox_t;

typedef struct {
    long mType;
    char msgText[1024];
} message_t;

void send(message_t message, mailbox_t* mailbox_ptr);
