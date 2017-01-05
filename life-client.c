/**
 * @file life-client.c
 *
 * Клиент принимает команды со стандартного потока ввода и пердает эти
 * команды в понятном для сервера виде. Все типы операций описаны в
 * заголовочном файле "life.h".
 */

#include "life.h"

/** @brief идентификатор процесса-сервера */
pid_t pid_server = 0;
/** @brief идентификатор процесса-клиента */
pid_t pid_client = 0;
/** @brief IPC ключ для создания очереди сообщений */
key_t key = 0;
/** @brief идентификатор очереди сообщений */
int   msgid = 0;

/**
 * Клиент завершает свою работу
 */
void quit_client(void) {
    while (wait(NULL) > 0);
    msgctl(msgid, IPC_RMID, 0);
    remove("server");
}

/**
 * Функция обработчик. Обрабатывает приход сигнала SIGTERM,
 * свидетельствующий о неудачном запуске процесса-сервера "life-server".
 *
 * @param[in] signo сигнал
 */
void handler(int signo) {
    kill(pid_server, SIGTERM);
    quit_client();
    exit(1);
}

/**
 * Проверка на правильность разбиения.
 *
 * @param[in] N число клеток "вселенной" по горизонтали
 * @param[in] K чило процессов-рабочих
 *
 * @return Новое число процессов-рабочих, если разбиение невозможно,
 * иначе - старое число рабочих.
 */
int client_check_partition(int N, int K) {
    char flag = 0;

    if (K > N) {
        printf("ERROR: You want to create too many processes.\n");
        printf("The number of processes was set as %d.\n", N);
        return N;
    }

    do {
        int width = (N % K) ? N/K + 1: N/K;
        if (width * (K-1) <= N) break;
        flag = 1;
        K--;
    } while (K > 0);

    if (flag) {
        printf("ERORR: Such partition is not available.\n");
        if (K > 0) {
            printf("The number of processes was set as %d.\n", K);
        } else {
            printf("The number of processes was set as %d.\n", N);
            return N;
        }
    }
    return K;
}

/**
 * Отправить сообщение серверу.
 *
 * @param[in] op тип операции
 * @param[in] p1 первый параметр операции (опционально)
 * @param[in] p2 второй параметр операции (опционально)
 * @return При успешном завершении возвращает 0, а при ошибке — -1, и в
 * переменную errno записывается код ошибки.
 */
int snd_server_message(int op, int p1, int p2) {
    message.mtype = pid_server;
    message.op    = op;
    message.prm1  = p1;
    message.prm2  = p2;
    return msgsnd(msgid, (struct msgbuf*)(&message), sizeof(message), 0);
}

/**
 * Принять сообщение от сервера.
 *
 * @param[in] c включает флаг IPC_NOWAIT
 * @return При успешном завершении системный вызов возвращает
 * действительную длину сообщения, скопированного в поле mtext. При
 * ошибке возвращается -1, а в переменную errno записывается код ошибки.
 */
ssize_t rcv_server_message(char c) {
    ssize_t p = msgrcv(msgid, (struct msgbuf*)(&message), sizeof(message), pid_client, c*IPC_NOWAIT);
    if (p) printf("%s\n", message.mtext);
    return p;
}

/**
 * Основная функция клиента. Здесь
 *   -# производится чтение параметров N, M, K из командной строки,
 *   -# проверяется частичная корректность входных параметров,
 *   -# включает аппарат очереди сообщений IPC,
 *   -# включает сервер и осуществляет обмен данных с сервером.
 */
int main(int argc, char *argv[]) {
    if (argc != 4)
        quit_message("ERROR: Wrong number of parameters.");

    int N, M, K;
    sscanf(argv[1], "%d", &M);
    sscanf(argv[2], "%d", &N);
    sscanf(argv[3], "%d", &K);

    if (!(N > 0 && M > 0 && K > 0))
        quit_message("ERROR: Parameters should be positive.");

    K = client_check_partition(N, K);

    pid_client = getpid();

    int fd = open("server", O_CREAT); close(fd);

    key = ftok("server", 's');
    msgid = msgget(key, 0666 | IPC_CREAT);
    signal(SIGINT,  handler);
    signal(SIGTERM, handler);

    if (!(pid_server = fork())) {
        char arg1[25], arg2[25], arg3[25];
        sprintf(arg1, "%d", M);
        sprintf(arg2, "%d", N);
        sprintf(arg3, "%d", K);
        execlp("./life-server", "./life-server", arg1, arg2, arg3, NULL);
        kill(pid_client, SIGTERM);
        quit_message("ERROR: Failed to run the server.");
    }

    rcv_server_message(0);

    char cmd[10];
    while (1) {
        if (scanf("%s", cmd) != 1) break;

        if (strcmp(cmd, "add") == 0) {
            int x, y;
            scanf("%d%d", &x, &y);
            snd_server_message(O_ADD, x, y);
            rcv_server_message(0);
            continue;
        }

        if (strcmp(cmd, "del") == 0) {
            int x, y;
            scanf("%d%d", &x, &y);
            snd_server_message(O_DEL, x, y);
            rcv_server_message(0);
            continue;
        }

        if (strcmp(cmd, "clear") == 0) {
            snd_server_message(O_CLEAR, 0, 0);
            rcv_server_message(0);
            continue;
        }

        if (strcmp(cmd, "start") == 0) {
            int gen;
            scanf("%d", &gen);
            snd_server_message(O_START, gen, 0);
            rcv_server_message(0);
            continue;
        }

        if (strcmp(cmd, "stop") == 0) {
            snd_server_message(O_STOP, 0, 0);
            rcv_server_message(0);
            continue;
        }

        if (strcmp(cmd, "snapshot") == 0) {
            snd_server_message(O_SNAP, 0, 0);
            rcv_server_message(0);
            for (int i = 0; i < M; i++)
                rcv_server_message(0);
            continue;
        }

        if (strcmp(cmd, "quit") == 0) {
            snd_server_message(O_QUIT, 0, 0);
            rcv_server_message(0);
            msgctl(msgid, IPC_RMID, NULL);
            break;
        }

        if (strcmp(cmd, "sleep") == 0) {
            int time = 0;
            scanf("%d", &time);
            sleep(time);
            continue;
        }

        printf("ERROR: Such operation is not supported.\n");
    }

    quit_client();
    return 0;
}
