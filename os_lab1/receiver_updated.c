#include "receiver.h"
#include <errno.h>
// 與 sender.c 相同的設定
#define EXIT_TYPE   255L
#define NORMAL_TYPE 1L
static sem_t *SEM_EMPTY = NULL;
static sem_t *SEM_FULL  = NULL;
static unsigned long long COMM_US = 0;
static inline long long now_us(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}
static key_t get_ipc_key(void) {
    key_t k = ftok("makefile", 'M');
    return k;
}
// 比起sender多一個參數用來回傳shmid以便清理
static void open_shared_resources_receiver(mailbox_t *mb, int *out_shmid) {
    key_t key = get_ipc_key();
    int shmid = shmget(key, sizeof(message_t), 0666 | IPC_CREAT);
    void *addr = shmat(shmid, NULL, 0);
    mb->storage.shm_addr = (char*)addr;
    // 初始化empty=1, full=0
    SEM_EMPTY = sem_open("/ipc_empty", O_CREAT, 0666, 1);
    SEM_FULL = sem_open("/ipc_full", O_CREAT, 0666, 0);
}
// same as sender
static void open_msg_queue_receiver(mailbox_t *mb) {
    key_t key = get_ipc_key();
    int msqid = msgget(key, 0666 | IPC_CREAT);
    mb->storage.msqid = msqid;
}

void receive(message_t* message_ptr, mailbox_t* mailbox_ptr){
    // 用 flag 判斷機制
    // 依照機制接收訊息，並只量測通訊動作（msgrcv / memcpy）
    if (mailbox_ptr->flag == MSG_PASSING) {
        long long t0 = now_us();
        (void)msgrcv(mailbox_ptr->storage.msqid, message_ptr, sizeof(message_ptr->msgText), 0, 0);
        long long t1 = now_us();
        COMM_US += (unsigned long long)(t1 - t0);
    } else if (mailbox_ptr->flag == SHARED_MEM) {
        (void)sem_wait(SEM_FULL);
        long long t0 = now_us();
        memcpy(message_ptr, mailbox_ptr->storage.shm_addr, sizeof(message_t));
        long long t1 = now_us();
        COMM_US += (unsigned long long)(t1 - t0);
        (void)sem_post(SEM_EMPTY);
    }
}

int main(int argc, char *argv[]){
    // 讀取並設定參數
    int mechanism = atoi(argv[1]);
    mailbox_t mailbox;
    mailbox.flag = mechanism;

    int shmid = -1; // 用於清理共享記憶體
    if (mechanism == MSG_PASSING) {
        open_msg_queue_receiver(&mailbox);
        printf("Message Passing\n");
    } else if (mechanism == SHARED_MEM) {
        open_shared_resources_receiver(&mailbox, &shmid);
        printf("Shared Memory\n");
    }
    while (1) {
        message_t msg;
        memset(&msg, 0, sizeof(msg));
        receive(&msg, &mailbox);

        if (msg.mType == EXIT_TYPE || strcmp(msg.msgText, "EXIT") == 0) {
            printf("Sender exit! \n");
            break;
        } else {
            printf("Receiving message: %s\n", msg.msgText);
        }
    }
    printf("Total time taken in receiving msg: %llu us\n", COMM_US);

    // 清理資源
    if (mechanism == MSG_PASSING) {
        if (msgctl(mailbox.storage.msqid, IPC_RMID, NULL) == -1) {
            perror("msgctl IPC_RMID");
        }
    } else if (mechanism == SHARED_MEM) {
        if (SEM_EMPTY) { sem_close(SEM_EMPTY); sem_unlink("/ipc_empty"); }
        if (SEM_FULL)  { sem_close(SEM_FULL);  sem_unlink("/ipc_full");  }
        if (mailbox.storage.shm_addr) shmdt(mailbox.storage.shm_addr);
        if (shmid != -1) {
            if (shmctl(shmid, IPC_RMID, NULL) == -1) {
                perror("shmctl IPC_RMID");
            }
        }
    }
    return 0;
}