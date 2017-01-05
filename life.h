/**
 * @file life.h
 *
 * Сервер принимает команды от клиента и проводит распараллеливание
 * операций по процесам-рабочим. Все типы операций описаны в
 * заголовочном файле "life.h".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

/** @brief добавить клетку из "вселенной"*/
#define O_ADD     1
/** @brief очистить "вселенную" */
#define O_CLEAR   2
/** @brief начать процесс моделирования */
#define O_START   3
/** @brief остановить процесс моделирования */
#define O_STOP    4
/** @brief сделать скриншот текущего состояния "вселенной" */
#define O_SNAP    5
/** @brief удалить клетку из "вселенной"*/
#define O_DEL     6
/** @brief завершить работу */
#define O_QUIT   13

/** @brief длина текстового сообщения */
#define STRSIZE 4096

/**
 * @brief сообщение
 *
 * Сообщения используются для обмена данными между
 *   -# клиентом и сервером;
 *   -# сервером и рабочими.
 */
struct msg_ {
    /** @brief тип сообщения
     * 
     * Это поле может быть равно:
     *   - pid_client,
     *   - pid_server,
     *   - pid_worker[i],
     *   - worker_being_ready.
     * */
    long mtype;
    /** @brief тип команды (операция)
     * 
     * Это поле может быть равно:
     *   - O_ADD
     *   - O_CLEAR
     *   - O_START
     *   - O_STOP
     *   - O_SNAP
     *   - M_ATTACH
     *   - O_DEL
     *   - O_QUIT
     * либо просто использоваться для передачи информации.
     * */
    int op;
    /** @brief первый параметр операции */
    int prm1;
    /** @brief второй параметр операции */
    int prm2;
    /** @brief текстовое содержание сообщения */
    char mtext[STRSIZE];
} message;

/**
 * Записать сообщение в лог-файл.
 *
 * @param[in] f лог-файл
 * @param[in] msg текстовое сообщение
 */
void write_log(FILE *f, char msg[]) {
    char       buffer[STRSIZE];
    time_t     curtime;
    struct tm *loctime;
    
    curtime = time(NULL);
    loctime = localtime(&curtime);
    strftime(buffer, STRSIZE, "%X", loctime);
    fprintf(f, "%s %s\n", buffer, msg);
}

/**
 * Сообщить об аварийном завершении работы и завершить работу.
 *
 * @param[in] str текстовое сообщение
 */
void quit_message(char str[]) {
    printf("%s\n", str);
    exit(1);
}

/** @brief тип сообщения
 * 
 * Данный тип сообщений используется для подтверждения рабочим того, что
 * он выполнил команду, посланную сервером.*/
#define worker_being_ready 15
