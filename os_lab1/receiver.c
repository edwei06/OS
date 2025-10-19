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
    if (k == -1) {
        perror("ftok");
        exit(1);
    }
    return k;
}
// 比起sender多一個參數用來回傳shmid以便清理
static void open_shared_resources_receiver(mailbox_t *mb, int *out_shmid) {
    key_t key = get_ipc_key();
    int shmid = shmget(key, sizeof(message_t), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    void *addr = shmat(shmid, NULL, 0);
    if (addr == (void*)-1) {
        perror("shmat");
        exit(1);
    }
    mb->storage.shm_addr = (char*)addr;
    if (out_shmid) *out_shmid = shmid;

    // 初始化empty=1, full=0
    SEM_EMPTY = sem_open("/ipc_empty", O_CREAT, 0666, 1);
    if (SEM_EMPTY == SEM_FAILED) {
        perror("sem_open /ipc_empty");
        exit(1);
    }
    SEM_FULL = sem_open("/ipc_full", O_CREAT, 0666, 0);
    if (SEM_FULL == SEM_FAILED) {
        perror("sem_open /ipc_full");
        exit(1);
    }
}
// same as sender
static void open_msg_queue_receiver(mailbox_t *mb) {
    key_t key = get_ipc_key();
    int msqid = msgget(key, 0666 | IPC_CREAT);
    if (msqid == -1) {
        perror("msgget");
        exit(1);
    }
    mb->storage.msqid = msqid;
}

void receive(message_t* message_ptr, mailbox_t* mailbox_ptr){
    // 用 flag 判斷機制
    // 依照機制接收訊息，並只量測通訊動作（msgrcv / memcpy）
    if (mailbox_ptr->flag == MSG_PASSING) {
        long long t0 = now_us();
        // msgrcv 的 size 不包含 mType
        if (msgrcv(mailbox_ptr->storage.msqid, message_ptr,
                   sizeof(message_ptr->msgText), 0, 0) == -1) {
            perror("msgrcv");
            exit(1);
        }
        long long t1 = now_us();
        COMM_US += (unsigned long long)(t1 - t0);
    } else if (mailbox_ptr->flag == SHARED_MEM) {
        if (!SEM_EMPTY || !SEM_FULL) {
            fprintf(stderr, "Semaphores not opened.\n");
            exit(1);
        }
        if (sem_wait(SEM_FULL) == -1) {
            perror("sem_wait full");
            exit(1);
        }

        long long t0 = now_us();
        memcpy(message_ptr, mailbox_ptr->storage.shm_addr, sizeof(message_t)); // 只算讀取
        long long t1 = now_us();
        COMM_US += (unsigned long long)(t1 - t0);

        if (sem_post(SEM_EMPTY) == -1) {
            perror("sem_post empty");
            exit(1);
        }
    } else {
        fprintf(stderr, "Unknown mailbox flag: %d\n", mailbox_ptr->flag);
        exit(1);
    }
}

int main(int argc, char *argv[]){
    // 排除輸入參數錯誤
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mechanism:1|2>\n", argv[0]);
        return 1;
    }
    // 讀取並設定參數
    int mechanism = atoi(argv[1]);
    mailbox_t mailbox;
    mailbox.flag = mechanism;

    int shmid = -1; // 用於清理共享記憶體
    if (mechanism == MSG_PASSING) {
        open_msg_queue_receiver(&mailbox);
        printf("[receiver] mechanism = Message Passing\n");
    } else if (mechanism == SHARED_MEM) {
        open_shared_resources_receiver(&mailbox, &shmid);
        printf("[receiver] mechanism = Shared Memory\n");
    } else {
        fprintf(stderr, "Invalid mechanism. Use 1 (Message Passing) or 2 (Shared Memory).\n");
        return 1;
    }

    int count = 0;
    while (1) {
        message_t msg;
        memset(&msg, 0, sizeof(msg));
        receive(&msg, &mailbox);

        if (msg.mType == EXIT_TYPE || strcmp(msg.msgText, "EXIT") == 0) {
            printf("[receiver] EXIT received.\n");
            break;
        } else {
            printf("[receiver] received: %s\n", msg.msgText);
            count++;
        }
    }

    printf("[receiver] total messages: %d\n", count);
    printf("[receiver] total COMM time (receive only): %llu us\n", COMM_US);

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