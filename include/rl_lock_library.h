#pragma once

#include <stdlib.h>
#include <stdio.h>          /* for srand, rand */
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/mman.h>       /* for shm_open */
#include <sys/stat.h>       /* for mode constants */
#include <fcntl.h>          /* for O_* constants and fcntl */
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdarg.h>         /* for functions take a variable number of arguments */

/* ==================================== MACRO VARIABLES ============================================================= */

#define NB_OWNERS           20
#define NB_LOCKS            10

/* ======================================= STRUCTURES =============================================================== */

typedef struct
{
    pid_t   proc;     /* pid of process */
    int     des;      /* file descripor */
} owner;

typedef struct
{
    int             next_lock;
    off_t           starting_offset;
    off_t           len;
    short           type;   //F_RDLCK or F_WRLCK
    size_t          nb_owners;
    owner           lock_owners[NB_OWNERS];
} rl_lock;

typedef struct
{
    int             first;
    rl_lock         lock_table[NB_LOCKS];
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             blockCnt;
    int             refCnt;
} rl_open_file;


typedef struct
{
    int             d;
    rl_open_file    *f;
} rl_descriptor;

///////////////////////////////////         RL_LIBRARY FUNCTIONS       /////////////////////////////////////////////////
//////////                                                                                                      ////////

/**
 * Initializes the library's static structure.
 * @param None
 * @return 0 - success, −1 otherwise
 */ 
int rl_init_library();

/**
 * Opens the file specified by pathname.
 * Possible usage:
 *     int open(const char *pathname, int flags);
 *     int open(const char *pathname, int flags, mode_t mode);
 * @param path pathname of file
 * @param flags one of the following access modes: O_RDONLY, O_WRONLY or O_RDWR
 * @param mode modes specified by standard: https://man7.org/linux/man-pages/man2/open.2.html
 * @return rl_descriptor with 'd' field equal of file descriptor opened, or −1 otherwise
 */
rl_descriptor rl_open(const char *path, int oflag, ...);


/**
 * Closes file descriptor.
 * @param lfd file rl_descriptor
 * @return 0 - success, −1 otherwise
 */
int rl_close(rl_descriptor lfd);


/**
 * Allocates a new file descriptor (as the lowest-numbered file descriptor available) 
 * that refers to the same open file description as the descriptor lfd. 
 * @param lfd old rl_descriptor 
 * @return new rl_descriptor with 'd' field equal of file descriptor created, or −1 otherwise
 */
rl_descriptor rl_dup(rl_descriptor lfd);

/**
 * Allocates a new file descriptor (number specified in newd) that refers to the same open file description 
 * as the descriptor lfd. If newd was previously opened, it will be closed before being reused.
 * @param lfd old rl_descriptor 
 * @param newd new file descriptor
 * @return new rl_descriptor with 'd' field equal of file descriptor created, or −1 otherwise 
 */
rl_descriptor rl_dup2(rl_descriptor lfd, int newd);


/**
 * Creates a new process by duplicating the calling process.
 * @param None 
 * @return if sucess: to parent process - pid of child process created, to child process - 0;  −1 otherwise 
 */
pid_t rl_fork();

/**
 * Performs an advisory lock. Used to get or release a region lock 
 * (does not provide the F_GETLK functionality of the fcntl function)
 * @param lfd rl library file descriptor
 * @param cmd command to execute by fcntl: 
 * F_SETLK (if a conflicting lock is held by another process, returns -1) 
 * or 
 * F_SETLKW (if a conflicting lock is held on the file, then wait for that lock to be released))
 * @param lck pointer to lock structure
 * @return 0 - success, −1 otherwise
 */
int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck);


/**
 * Print internal structures
 * @param lfd file descriptor
 */
void rl_print(rl_descriptor lfd);
