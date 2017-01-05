/**
 * @file life-server.c
 *
 * Сервер принимает команды от клиента и проводит распараллеливание
 * операций по процесам-рабочим. Все типы операций описаны в
 * заголовочном файле "life.h".
 */

#include "life.h"

/** @brief число клеток во "вселенной" по горизонтали*/
int N;
/** @brief число клеток во "вселенной" по вертикали*/
int M;
/** @brief число процессов-рабочих*/
int K;

FILE *logfile;

/** @brief идентификатор процесса-сервера*/
pid_t  pid_server = 0;
/** @brief идентификатор процесса-клиента*/
pid_t  pid_client = 0;
/** @brief массив идентификаторов процессов-рабочих*/
pid_t *pid_worker;
/** @brief карта распараллеливания столбцов "вселенной" между рабочими*/
int  *pid_worker_map;

/** @brief IPC ключ для очереди сообщений, семафоров и разделяемой памяти*/
key_t key;
/** @brief идентификатор очереди сообщений*/
int   msgid;
/** @brief массив идентификаторов семафоров для каждого сегмента
 * разделяемой памяти*/
int  *semid;
/** @brief массив идентификаторов разделяемой памяти для каждой из
 * границ областей "вселенной"*/
int  *shmid;
/** @brief ширина одной полосы*/
int   width;
/** @brief число поколений, которых предстоит еще построить*/
int   steps = 0;
/** @brief число уведомлений, пришедших от рабочих*/
int counter = 0;

/**
 * Принять сообщение от рабочего.
 *
 * @param[in] c включает флаг IPC_NOWAIT
 * @return При успешном завершении системный вызов возвращает
 * действительную длину сообщения, скопированного в поле mtext. При
 * ошибке возвращается -1, а в переменную errno записывается код ошибки.
 */
ssize_t rcv_worker_message(char c) {
    return msgrcv(msgid, (struct msgbuf*)(&message), sizeof(message), pid_server, c*IPC_NOWAIT);
}

/**
 * Принять сообщение от клиента.
 *
 * @param[in] c включает флаг IPC_NOWAIT
 * @return При успешном завершении системный вызов возвращает
 * действительную длину сообщения, скопированного в поле mtext. При
 * ошибке возвращается -1, а в переменную errno записывается код ошибки.
 */
ssize_t rcv_client_message(char c) {
    return msgrcv(msgid, (struct msgbuf*)(&message), sizeof(message), pid_server, c*IPC_NOWAIT);
}

/**
 * Отправить рабочему сообщение.
 *
 * @param[in] i номер рабочего
 * @param[in] c включает флаг IPC_NOWAIT
 * @return При успешном завершении возвращает 0, а при ошибке — -1, и в
 * переменную errno записывается код ошибки.
 */
int snd_worker_message(int i, char c) {
    message.mtype = pid_worker[i];
    return msgsnd(msgid, (struct msgbuf*)(&message), sizeof(message), c*IPC_NOWAIT);
}

/**
 * Отправить информационное сообщение рабочему. Сообщение содержит:
 *   -# номер рабочего;
 *   -# число клеток полосы, обрабатываемой рабочим, по вертикали;
 *   -# число клеток полосы, обрабатываемой рабочим, по горизонтали.
 *
 * @param[in] i номер рабочего
 * @param[in] c включает флаг IPC_NOWAIT
 * @return При успешном завершении возвращает 0, а при ошибке — -1, и в
 * переменную errno записывается код ошибки.
 */
int snd_worker_info(int i, char c) {
    message.op    = i;
    message.prm1  = M;
    message.prm2  = (i == K-1 && N % width) ? N % width: width;
    return snd_worker_message(i, c);
}

/**
 * Отправить сообщение клиенту.
 *
 * @param[in] msg текстовое содержание сообщения
 * @return При успешном завершении возвращает 0, а при ошибке — -1, и в
 * переменную errno записывается код ошибки.
 */
int snd_client_message(char msg[]) {
    message.mtype = pid_client;
    sprintf(message.mtext, "%s", msg);
    return msgsnd(msgid, (struct msgbuf*)(&message), sizeof(message), 0);
}

/**
 * Сервер ожидает подтверждения того, что рабочий закончил выполнение
 * команды
 * @param[in] c включает флаг IPC_NOWAIT
 * @return При успешном завершении системный вызов возвращает
 * действительную длину сообщения, скопированного в поле mtext. При
 * ошибке возвращается -1, а в переменную errno записывается код ошибки.
 */
ssize_t server_waiting_worker(char c) {
    return msgrcv(msgid, (struct msgbuf*)(&message), sizeof(message), worker_being_ready, c*IPC_NOWAIT);
}

/**
 * Инициализация сервера. Сервер
 *   -# динамически выделяет память под массивы, описанные в глобальной
 * области кода;
 *   -# подключает очередь сообщений;
 *   -# создает файлов "worker-left" и "worker-right", отвечающих за
 * семафоры и разделяемую память;
 *   -# вызывает K рабочих и отправляет им информационное сообщение.
 */
void server_init(void) {
    key = ftok("server", 's');
    msgid = msgget(key, 0666);

    width = (N % K) ? N/K + 1: N/K;

    pid_worker = (pid_t *) calloc(K, sizeof(pid_t));
    pid_worker_map = (int *) calloc(N, sizeof(int));

    int fd;
    fd = open("worker-left",  O_CREAT); close(fd);
    fd = open("worker-right", O_CREAT); close(fd);

    semid = (int *) calloc (2*K, sizeof(int));
    shmid = (int *) calloc (2*K, sizeof(int));

    for (int i = 0; i < K; i++) {
        key = ftok("worker-left", i);
        semid[2*i] = semget(key, 1, 0666 | IPC_CREAT);
        shmid[2*i] = shmget(key, M, 0666 | IPC_CREAT);

        key = ftok("worker-right", i);
        semid[2*i+1] = semget(key, 1, 0666 | IPC_CREAT);
        shmid[2*i+1] = shmget(key, M, 0666 | IPC_CREAT);

        for (int j = i*width; j < (i+1)*width && j < N; j++) {
            pid_worker_map[j] = i;
        }

        if (!(pid_worker[i] = fork())) {
            char arg3[STRSIZE];
            sprintf(arg3, "%d", K);
            execlp("./life-worker", "./life-worker", arg3, NULL);
            kill(pid_server, SIGTERM);
            quit_message("ERROR: Can't run life-worker.");
            exit(1);
        }
    }

    counter = 0;
    for (int i = 0; i < K; i++) {
        while (snd_worker_info(i, 1) == -1) {
            while (server_waiting_worker(1) != -1) counter++;
        }
    }
    while (counter++ < K) server_waiting_worker(0);
}

/**
 * Cервер отправляет рабочему с командой добавить/удалить клетку во/из
 * вселенную/ой.
 * @param[in] x номер строки вселенной
 * @param[in] y номер строки вселенной
 * @param[in] c выбор операции: 1 - добавить клетку, 0 - удалить клетку
 */
void server_add(int x, int y, char c) {
    char msg[STRSIZE];

    if (!(1 <= x && x <= M && 1 <= y && y <= N)) {
        snd_client_message("ERROR: The cell is out of universe's borders.");
        sprintf(msg, "The cell (%d,%d) is out of universe's borders.", x, y);
        write_log(logfile, msg);
        return;
    }

    int i = pid_worker_map[y-1];
    message.op   = (c) ? O_ADD: O_DEL;
    message.prm1 = x;
    message.prm2 = (y-1) % width + 1;
    snd_worker_message(i, 0);
    server_waiting_worker(0);
    snd_client_message("OK");

    if (c) {
        sprintf(msg, "The cell (%d,%d) is added.", x, y);
    } else sprintf(msg, "The cell (%d,%d) is deleted.", x, y);
    write_log(logfile, msg);
}

/**
 * Cервер отправляет сообщения рабочим с командой очистить вселенную
 */
void server_clear(void) {
    if (steps > 0) {
        snd_client_message("ERROR: The server is working now.");
        write_log(logfile, "The server is working now...");
        return;
    }

    counter = 0;
    for (int i = 0; i < K; i++) {
        message.op = O_CLEAR;
        while (snd_worker_message(i, 1) == -1) {
            while (server_waiting_worker(1) != -1) counter++;
        }
    }
    while (counter++ < K) server_waiting_worker(0);

    snd_client_message("OK");
    write_log(logfile, "Universe is cleaned.");
}

/**
 * Cервер устанавливает счетчик поколений "steps"
 */
void server_start(void) {
    if (steps > 0) {
        snd_client_message("ERROR: The server is working now.");
        write_log(logfile, "The server is working now...");
        return;
    }

    if (message.prm1 < 1) {
        snd_client_message("ERROR: The number of generation should be postive one.");
        write_log(logfile, "The number of generation should be postive one.");
        return;
    }

    steps = message.prm1;
    snd_client_message("OK");
    write_log(logfile, "Simulation is started.");
}

/**
 * Cервер отправляет сообщения рабочим с командой построить следующее
 * поколение
 */
void server_next_generation(void) {
    counter = 0;
    for (int i = 0; i < K; i++) {
        message.op = O_START;
        while (snd_worker_message(i, 1) == -1) {
            while (server_waiting_worker(1) != -1) counter++;
        }
    }
    while (counter++ < K) server_waiting_worker(0);
}

/**
 * Cервер сбрасывает счетчик поколений "steps"
 */
void server_stop(void) {
    if (steps == 0) {
        snd_client_message("ERROR: The server is NOT working now.");
        write_log(logfile, "The server is NOT working now...");
    } else {
        steps = 0;
        snd_client_message("OK");
        write_log(logfile, "Simulation is stopped.");
    }
}

/**
 * Cервер отправляет сообщения рабочим с командой сделать скриншот
 * текущего состояния вселенной, а затем пересылает его клиенту
 */
void server_snap(void) {
    snd_client_message("OK");
    char msg[STRSIZE];
    for (int j = 1; j <= M; j++) {
        for (int i = 0; i < K; i++) {
            message.op   = O_SNAP;
            message.prm1 = j;
            snd_worker_message(i, 0);

            rcv_worker_message(0);
            int offset = message.prm1 * width;
            int len = message.prm2;
            memcpy(&msg[offset], message.mtext, len);
            msg[N] = '\0';
        }
        message.prm1 = j;
        snd_client_message(msg);
    }
    write_log(logfile, "Snapshot is made.");
}

/**
 * Cервер завершает свою работу:
 *   -# посылает сообщения рабочим с командой завершить работу;
 *   -# удаляет разделяемую память и семафоры;
 *   -# освобождение динамической памяти;
 *   -# отключает очередь сообщений;
 *   -# отправляет уведомление клиенту.
 */
void server_quit(void) {
    for (int i = 0; i < K; i++) {
        message.mtype = pid_worker[i];
        message.op    = O_QUIT;
        msgsnd(msgid, (struct msgbuf*)(&message), sizeof(message), 0);
    }

    while (wait(NULL) > 0);

    for (int i = 0; i < K; i++) {
        shmctl(shmid[2*i], IPC_RMID, NULL);
        semctl(semid[2*i], 0, IPC_RMID, (int) 0);
        shmctl(shmid[2*i+1], IPC_RMID, NULL);
        semctl(semid[2*i+1], 0, IPC_RMID, (int) 0);
    }
    free(shmid);
    free(semid);

    free(pid_worker);
    free(pid_worker_map);

    remove("worker-left");
    remove("worker-right");

    snd_client_message("OK: Server is OFF.");
    write_log(logfile, "Server is OFF.");
    fclose(logfile);
}

/**
 * Функция обработчик. Обрабатывает приход сигнала SIGTERM,
 * свидетельствующий о неудачном запуске одного из процессов-рабочих
 * "life-server".
 *
 * @param[in] signo сигнал
 */
void handler(int signo) {
    server_quit();
    exit(1);
}

/**
 * Основная функция сервера. Сервер
 *   -# получает параметры для вселенной,
 *   -# осуществляет обмен данных с клиентом.
 */
int main(int argc, char *argv[]) {
    signal(SIGTERM, handler);
    logfile = fopen("plife.log", "w");

    if (argc != 4) {
        write_log(logfile, "Wrong number of parameters.");
        quit_message("ERROR: Wrong number of parameters.");
    }

    sscanf(argv[1], "%d", &M);
    sscanf(argv[2], "%d", &N);
    sscanf(argv[3], "%d", &K);

    server_init();

    pid_server = getpid();
    pid_client = getppid();

    snd_client_message("OK: Server is ON.");
    write_log(logfile, "Server is ON.");

    while (1) {
        if (steps > 0 && rcv_client_message(1) == -1) {
            server_next_generation();
            if (!(--steps)) write_log(logfile, "Simulation is finished.");
            continue;
        }

        if (steps == 0) rcv_client_message(0);

        if (message.op == O_QUIT) {
            break;
        }

        switch (message.op) {
            case O_ADD:   server_add(message.prm1, message.prm2, 1); break;
            case O_DEL:   server_add(message.prm1, message.prm2, 0); break;
            case O_CLEAR: server_clear(); break;
            case O_START: server_start(); break;
            case O_STOP:  server_stop(); break;
            case O_SNAP:  server_snap(); break;
            default: ;
        }
    }

    server_quit();
    return 0;
}
