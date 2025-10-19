#include "sender.h"
#include <errno.h>
// 定義結束訊息是255, receiver收到就能終止,正常資料使用1
#define EXIT_TYPE   255L
#define NORMAL_TYPE 1L

// 共享記憶體的兩個訊號，empty 初始為1(write)，full 初始為0(read)
// sender: wait(empty) -> write -> post(full)
// receiver: wait(full)  -> read  -> post(empty)
static sem_t *SEM_EMPTY = NULL;
static sem_t *SEM_FULL  = NULL;

// 只量測通訊相關動作（msgsnd / memcpy）的總時間（us）
static unsigned long long COMM_US = 0;

static inline long long now_us(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}
// 將換行的字串去除把\n\r都去掉
static void trim_newline(char *s){
    if (!s) return;
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

static key_t get_ipc_key(void) {
    // System V IPC 物件用 key_t 對應
    // makefile讓 sender/receiver 在同一目錄裡能產生一致的 key。
    key_t k = ftok("makefile", 'M');
    return k;
}
// 共享記憶體function
static void open_shared_resources_sender(mailbox_t *mb) {
    key_t key = get_ipc_key();
    // 使用shmget 建立共享記憶體段，大小是 message_t
    int shmid = shmget(key, sizeof(message_t), 0666 | IPC_CREAT);
    // 將共享記憶體段映射到位址空間，回傳就是buffer
    void *addr = shmat(shmid, NULL, 0);
    mb->storage.shm_addr = (char*)addr;
    // 初始化empty=1, full=0，防止deadlock,receiver也可以透過相同名稱使用它
    SEM_EMPTY = sem_open("/ipc_empty", O_CREAT, 0666, 1);
    SEM_FULL = sem_open("/ipc_full", O_CREAT, 0666, 0);
}
// 使用msgget取得訊息id，雙方只要key一樣就能取得相同id
static void open_msg_queue_sender(mailbox_t *mb) {
    key_t key = get_ipc_key();
    int msqid = msgget(key, 0666 | IPC_CREAT);
    mb->storage.msqid = msqid;
}

void send(message_t message, mailbox_t* mailbox_ptr){
    //  用 flag 判斷機制
    // 依照機制送出訊息，並只量測通訊動作（msgsnd / memcpy）時間
    if (mailbox_ptr->flag == MSG_PASSING) {
        long long t0 = now_us();
        // msgsnd 的 size 不包含 mType，system V 規定，第三參數是資料區大小
        (void)msgsnd(mailbox_ptr->storage.msqid, &message, sizeof(message.msgText), 0);
        //測量msgsnd時間加到COMM_US
        long long t1 = now_us();
        COMM_US += (unsigned long long)(t1 - t0);
    } else if (mailbox_ptr->flag == SHARED_MEM) {
        (void)sem_wait(SEM_EMPTY);
        long long t0 = now_us();
        memcpy(mailbox_ptr->storage.shm_addr, &message, sizeof(message_t));
        long long t1 = now_us();
        COMM_US += (unsigned long long)(t1 - t0);
        (void)sem_post(SEM_FULL);
    }
}

int main(int argc, char *argv[]){
    //讀取並設定參數
    int mechanism = atoi(argv[1]);
    const char *input_path = argv[2];

    mailbox_t mailbox;
    mailbox.flag = mechanism;
    // 透過 flag 決定使用哪種通訊機制
    if (mechanism == MSG_PASSING) {
        open_msg_queue_sender(&mailbox);
        printf("Message Passing\n");
    } else if (mechanism == SHARED_MEM) {
        open_shared_resources_sender(&mailbox);
        printf("Shared Memory\n");
    }

    FILE *fp = fopen(input_path, "r");

    char line[1100];
    // 讀檔送訊息（每行一則），跳過空行
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if (line[0] == '\0') continue; // 不送空訊息

        message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.mType = NORMAL_TYPE;
        strncpy(msg.msgText, line, sizeof(msg.msgText)-1);

        send(msg, &mailbox);
        printf("Sending message: %s\n", msg.msgText);

    }
    fclose(fp);

    // 檔案結束後送 EXIT
    message_t exit_msg;
    memset(&exit_msg, 0, sizeof(exit_msg));
    exit_msg.mType = EXIT_TYPE;
    strcpy(exit_msg.msgText, "EXIT");
    send(exit_msg, &mailbox);
    printf("End of input file! exit! \n");
    printf("Total time taken in sending msg: %llu us\n", COMM_US);

    // 資源釋放（共享記憶體這邊只 detach/close，不 unlink）
    if (mechanism == SHARED_MEM) {
        if (SEM_EMPTY) sem_close(SEM_EMPTY);
        if (SEM_FULL)  sem_close(SEM_FULL);
        if (mailbox.storage.shm_addr) shmdt(mailbox.storage.shm_addr);
    }
    return 0;
}