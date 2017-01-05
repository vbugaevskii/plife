/**
 * @file life-worker.c
 *
 * Рабочий принимает команды от сервера и производит моделирование
 * очередного поколения. Все типы операций описаны в заголовочном файле
 * "life.h".
 */

#include "life.h"

/** @brief число клеток области по вертикали */
int M = -1;
/** @brief число клеток области по горизонтали */
int N = -1;
/** @brief число процессов-рабочих */
int K = -1;
/** @brief индекс рабочего */
int id_worker = -1;
/** @brief индекс левого соседа рабочего */
int id_collab_left = -1;
/** @brief индекс правого соседа рабочего */
int id_collab_right = -1;
/** @brief идентификатор процесса-рабочего */
pid_t pid_worker = -1;
/** @brief идентификатор процесса-сервера */
pid_t pid_server = -1;

/** @brief карта текущего состояния "вселенной" */
char **map_state_curr = NULL;
/** @brief карта последнего смоделированного состояния "вселенной" */
char **map_state_prev = NULL;

/** @brief IPC-ключ */
key_t key = 0;
/** @brief идентификатор очереди сообщений */
int msgid;
/** @brief массив идентификаторов семафоров
 * -# semid[0] - правая граница левого соседа рабочего;
 * -# semid[1] - левая граница рабочего;
 * -# semid[2] - правая граница рабочего;
 * -# semid[3] - левая граница правого соседа рабочего;
 * */
int semid[4];
/** @brief массив идентификаторов разделяемой памяти
 * -# semid[0] - правая граница левого соседа рабочего;
 * -# semid[1] - левая граница рабочего;
 * -# semid[2] - правая граница рабочего;
 * -# semid[3] - левая граница правого соседа рабочего;
 * */
int shmid[4];
/** @brief массив для управления семафорами */
struct sembuf sops[4];
/** @brief массив указатель на начало адресного пространства
 разделяемой памяти */
char *shmad[4];

/**
 * Рабочий сообщает о том, что он выполнил операцию, посланную сервером.
 *
 * @return При успешном завершении возвращает 0, а при ошибке — -1, и в
 * переменную errno записывается код ошибки.
 */
int worker_is_ready(void) {
    message.mtype = worker_being_ready;
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
    return msgrcv(msgid, (struct msgbuf*)(&message), sizeof(message), pid_worker, c*IPC_NOWAIT);
}

/**
 * Принять информационное сообщение от сервера.
 *
 * @return При успешном завершении системный вызов возвращает
 * действительную длину сообщения, скопированного в поле mtext. При
 * ошибке возвращается -1, а в переменную errno записывается код ошибки.
 */
ssize_t rcv_worker_info(void) {
    ssize_t p = rcv_server_message(0);
    id_worker = message.op;
    M = message.prm1;
    N = message.prm2;
    return p;
}

/**
 * Отправить сообщение серверу.
 *
 * @return При успешном завершении возвращает 0, а при ошибке — -1, и в
 * переменную errno записывается код ошибки.
 */
int snd_server_message(void) {
    message.mtype = pid_server;
    return msgsnd(msgid, (struct msgbuf*)(&message), sizeof(message), 0);
}

/**
 * Определить индексы соседей рабочего.
 * @param[in] i индекс рабочего
 */
void worker_define_partners(int i) {
    if (K == 1) {
        id_collab_left  = 0;
        id_collab_right = 0;
        return;
    }

    if (i == 0) {
        id_collab_left  = K-1;
        id_collab_right = i+1;
    } else {
        if (i == K-1) {
            id_collab_left  = i-1;
            id_collab_right = 0;
        } else {
            id_collab_left  = i-1;
            id_collab_right = i+1;
        }
    }
}

/**
 * Инициализация рабочего. Рабочий
 *   -# подключает очередь сообщений, семафоры и разделяемую память;
 *   -# динамически выделяет память под массивы, описанные в глобальной
 * области кода;
 *   -# очищает таблицу текущего состояния.
 */
void worker_init(void) {
    pid_server = getppid();
    pid_worker = getpid();

    key = ftok("server", 's');
    msgid = msgget(key, 0666);

    rcv_worker_info();
    worker_define_partners(id_worker);

    for (int i = 0; i < 4; i++) {
        switch (i) {
            case 0: key = ftok("worker-right", id_collab_left);  break;
            case 1: key = ftok("worker-left",  id_worker);       break;
            case 2: key = ftok("worker-right", id_worker);       break;
            case 3: key = ftok("worker-left",  id_collab_right); break;
            default: ;
        }

        semid[i] = semget(key, 1, 0666);
        shmid[i] = shmget(key, M, 0666);
        shmad[i] = shmat(shmid[i], NULL, 0);
        memset(shmad[i], '.', M);

        sops[i].sem_num = 0;
        sops[i].sem_flg = 0;
    }

    map_state_curr = (char **) calloc(M+2, sizeof(char *));
    map_state_prev = (char **) calloc(M+2, sizeof(char *));

    for (int i = 0; i < M+2; i++) {
        map_state_curr[i] = (char *) calloc(N+2, sizeof(char));
        map_state_prev[i] = (char *) calloc(N+2, sizeof(char));
        for (int j = 0; j < N+2; j++) {
            map_state_curr[i][j] = '.';
        }
    }

    worker_is_ready();
}

/**
 * Рабочий завершает свою работу:
 *   -# отключает разделяемую память, семафоры и очередь сообщений;
 *   -# освобождение динамической памяти;
 *   -# отправляет уведомление серверу.
 */
void worker_quit(void) {
    for (int i = 0; i < M+2; i++) {
        free(map_state_curr[i]);
        free(map_state_prev[i]);
    }
    free(map_state_curr);
    free(map_state_prev);

    for (int i = 0; i < 4; i++) shmdt(shmad[i]);
}

/**
 * Рабочий добавляет клетку в свою область "вселенной".
 * @param[in] x номер строки
 * @param[in] y номер столбца
 */
void worker_add(int x, int y) {
    map_state_curr[x][y] = '*';
    if (y == 1) shmad[1][x-1] = '*';
    if (y == N) shmad[2][x-1] = '*';
    worker_is_ready();
}

/**
 * Рабочий удаляет клетку из своей области "вселенной".
 * @param[in] x номер строки
 * @param[in] y номер столбца
 */
void worker_del(int x, int y) {
    map_state_curr[x][y] = '.';
    if (y == 1) shmad[1][x-1] = '.';
    if (y == N) shmad[2][x-1] = '.';
    worker_is_ready();
}

/**
 * Рабочий освобождает свою область "вселенной".
 */
void worker_clear(void) {
    for (int i = 0; i < M+2; i++) {
        for (int j = 0; j < N+2; j++) {
            map_state_curr[i][j] = '.';
        }
    }

    for (int i = 0; i < M; i++)
        shmad[1][i] = (shmad[2][i] = '.');

    worker_is_ready();
}

/**
 * Посчитать количество соседей у данной клетки.
 * @param[in] x номер строки
 * @param[in] y номер столбца
 * @return число соседей
 */
int worker_count_neigbours(int x, int y) {
    int number = 0;
    for (int i = x-1; i <= x+1; i++) {
        for (int j = y-1; j <= y+1; j++) {
            if (!(i == x && j == y) && map_state_prev[i][j] == '*')
                number++;
        }
    }
    return number;
}

/**
 * Опустить семафор.
 * @param[in] i номер семафора
 */
void sem_down(int i) {
    sops[i].sem_op = -1;
    semop(semid[i], (struct sembuf *) &sops[i], 1);
}

/**
 * Поднять семафор.
 * @param[in] i номер семафора
 */
void sem_up(int i) {
    sops[i].sem_op = 1;
    semop(semid[i], (struct sembuf *) &sops[i], 1);
}

/**
 * Обновить карту последнего сгенерированного поколения.
 */
void worker_update_map(void) {
    for (int i = 0; i < M+2; i++)
        memcpy(map_state_prev[i], map_state_curr[i], N+2);

    for (int i = 0; i < M; i++) {
        map_state_prev[i+1][0]   = shmad[0][i];
        map_state_prev[i+1][N+1] = shmad[3][i];
    }

    sem_up(0);
    sem_up(3);

    memcpy(map_state_prev[0],   map_state_prev[M], N+2);
    memcpy(map_state_prev[M+1], map_state_prev[1], N+2);
}

/**
 * Обновить разделяемую память, соотвествующую границам рабочего.
 */
void worker_update_memory(void) {
    sem_down(1);
    sem_down(2);

    for (int i = 0; i < M; i++) {
        shmad[1][i] = map_state_curr[i+1][1];
        shmad[2][i] = map_state_curr[i+1][N];
    }
}

/**
 * Построить очередное поколение.
 */
void worker_start(void) {
    worker_update_map();

    for (int i = 1; i <= M; i++){
        for (int j = 1; j <= N; j++) {
            int number = worker_count_neigbours(i, j);

            if (map_state_prev[i][j] == '.') {
                if (number == 3) map_state_curr[i][j] = '*';
            } else {
                if (!(number == 2 || number == 3))
                    map_state_curr[i][j] = '.';
            }
        }
    }

    worker_update_memory();
    worker_is_ready();
}

/**
 * Сделать скриншот строки
 * @param[in] i номер строки
 */
int worker_snap(int i) {
    message.mtype = pid_server;
    message.op    = O_SNAP;
    message.prm1  = id_worker;
    message.prm2  = N;
    memcpy(message.mtext, &map_state_curr[i][1], N);
    message.mtext[N] = '\0';
    return snd_server_message();
}

/**
 * Основная функция рабочего. Сервер
 *   -# получает количество процессов-рабочих;
 *   -# осуществляет обмен данных с сервером.
 */
int main(int argc, char *argv[]) {
    sscanf(argv[1], "%d", &K);
    worker_init();

    while (1) {
		rcv_server_message(0);

        if (message.op == O_QUIT) {
            break;
        }

        switch (message.op) {
            case O_ADD:   worker_add(message.prm1, message.prm2); break;
            case O_DEL:   worker_del(message.prm1, message.prm2); break;
            case O_CLEAR: worker_clear(); break;
            case O_START: worker_start(); break;
            case O_SNAP:  worker_snap(message.prm1); break;
            default: ;
        }
	}

    worker_quit();
    return 0;
}
