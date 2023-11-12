#include "rl_lock_library.h"
#include <stdint.h>

#define NB_FILES            256
#define NB_FD               512
#define NEXT_NULL           -2
#define NEXT_LAST           -1
#define FILE_UNK            -1
#define RES_ERR             -1

#define SHARED_NAME_MAX_LEN 64
#define SHARED_MEM_FORMAT   "/%c_%ld_%ld"
#define SHARED_PREFIX_MEM 'f'
#define SHARED_PREFIX_SEM 's'

//https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_(Select_Graphic_Rendition)_parameters
#define KNRM                "\x1B[0m\n"
#define KRED                "\x1B[31m"
#define KGRN                "\x1B[32m"
#define KYEL                "\x1B[33m"
#define KBLU                "\x1B[34m"
#define KMAG                "\x1B[35m"
#define KCYN                "\x1B[36m"
#define KWHT                "\x1B[37m"


#if !defined(MIN)
    #define MIN(x, y) x < y ? x: y
#endif    

#if !defined(MAX)
    #define MAX(x, y) x > y ? x: y
#endif    

/* ==================================== MACRO FUNCTIONS ============================================================= */

#define CLOSE_FILE(File) if (File > 0) { close(File); File = -1; }
#define KILL_SEMATHORE(Sem) if (Sem)  { sem_close(Sem); sem_destroy(Sem); }
#define UNLINK_SEMATHORE(Name) if (Name) { sem_unlink(Name); }
#define FREE_MMAP(Mem, len) if (Mem) { munmap(Mem, len); }
#define FREE_MEM(Mem) if (Mem) { free(Mem); Mem = NULL; }

#define PROC_ERROR(Message) { fprintf(stderr, "%s : error {%s} in file {%s} on line {%d}\n", Message, strerror(errno), __FILE__, __LINE__); }

#define LOCK_ERROR(Code) if (Code != 0) { fprintf(stderr, "%s : error {%d} in file {%s} on line {%d}\n", "mutex_lock() failure", strerror(Code), __FILE__, __LINE__); }
#define UNLOCK_ERROR(Code) if (Code != 0) { fprintf(stderr, "%s : error {%d} in file {%s} on line {%d}\n", "mutex_unlock() failure", strerror(Code), __FILE__, __LINE__); }

static struct 
{
    int             nb_files;
    rl_open_file   *tab_open_files[NB_FILES];
    pthread_mutex_t mutex; //protect multi-threading access for library
} rl_all_files;

static bool  g_is_initialized = false;

/* ================================  AUXILIARY FUNCTIONS DEFINITIONS  =============================================== */

/**
 * check ability to add new owner
 * @param own owner
 * @param f file descriptor
 * @return 1 si oui, 0 si own non dans table, -1 si capacité max
 */
static int can_add_new_owner(owner own, rl_open_file *f);

/**
 * add new owner
 * @param own current owner
 * @param new_owner new owner
 * @param f file descriptor
 * @return −1 in case of error, 0 - success
 */
static int add_new_owner(owner own, owner new_owner, rl_open_file *f);

/**
 * check ability to add new owner by pid
 * @param parent parent pid
 * @param f file descriptor
 * @return −1 in case of error, 0 - success
 */
static int can_add_new_owner_by_pid(pid_t parent, rl_open_file *f);

/**
 * add new owner by pid
 * @param parent parent pid
 * @param parent child pid
 * @param f file descriptor
 * @return −1 in case of error, 0 - success
 */
static int add_new_owner_by_pid(pid_t parent, pid_t fils, rl_open_file *f);

/**
 * Initialize a mutex.
 * @param pMutex pointer to mutex
 * @return 0 if succesful, otherwise, an error number shall be returned to indicate the error
 */
static int init_mutex(pthread_mutex_t *pMutex);

/**
 * Initialize a condition variable attributes object.
 * @param pCond pointer to condition
 * @return 0 if succesful, otherwise, an error number shall be returned to indicate the error
 */
static int init_cond(pthread_cond_t *pCond);

/**
 * Test equality between two owners
 * @param o1 owner
 * @param o2 owener
 * @return true if equals, false otherwise
 */
static bool is_owners_are_equal(owner o1, owner o2);

/**
 * Compose shared memory object name according format "/f_dev_ino", basing on file's stat information 
 * @param filePath [in] file path
 * @param type [in] shared object type: SHARED_PREFIX_MEM or SHARED_PREFIX_SEM
 * @param name [out] generated name
 * @param maxLen [in] maximum name length in characters
 * @return true - OK, false - error
 */
static bool make_shared_name_by_path(const char *filePath, char type, char *name, size_t maxLen);

/**
 * Compose shared memory object name according format "/f_dev_ino", basing on file's stat information 
 * @param fd [in] file descriptor
 * @param type [in] shared object type: SHARED_PREFIX_MEM or SHARED_PREFIX_SEM
 * @param name [out] generated name
 * @param maxLen [in] maximum name length in characters
 * @return true - OK, false - error
 */
static bool make_shared_name_by_fd(int fd, char type, char *name, size_t maxLen);

/**
 * Get file size
 * @return file size in bytes
 */
static uint64_t get_file_size(int fd);

/**
 * Get current file position
 * @return file position
 */
static uint64_t get_current_position(int fd);

/**
 * removes all locks if owners aren't alive
 * @param lfd [in] file descriptor
 */
static void rl_clear_dead_owners(rl_descriptor lfd);

/**
 * delete lock by index
 * @param f file descriptor
 * @param index lock index
 */
static void delete_lock(rl_open_file *f, int index);

/**
 * delete owner
 * @param f rl file descriptor
 * @param index lock index
 * @param d file descriptor
 */
static void delete_owner(rl_open_file *f, int index, int d);

/**
 * delete lock region
 * @param lfd rl descriptor
 * @param lck lock descriptor
 * @return −1 in case of error, 0 - success
 */
static int delete_lock_region(rl_descriptor lfd, struct flock *lck);

/**
 * add lock region for writing
 * @param lfd rl descriptor
 * @param lck lock descriptor
 * @return −1 in case of error, 0 - success
 */
static int add_write_lock_region(rl_descriptor lfd, struct flock *lck);

/**
 * add lock region for reading
 * @param lfd rl descriptor
 * @param lck lock descriptor
 * @return −1 in case of error, 0 - success
 */
static int add_read_lock_region(rl_descriptor lfd, struct flock *lck);

/**
 * add new lock
 * @param f rl file descriptor
 * @param lck lock descriptor
 * @param d file descriptor
 * @param type lock type
 * @return −1 in case of error, 0 - success
 */
static int add_lock(rl_open_file *f, struct flock *lck, int d, int type);

/**
 * add new lock owner
 * @param lfd rl descriptor
 * @param lck lock descriptor
 * @return −1 in case of error, 0 - success
 */
static int add_owner(rl_descriptor lfd, rl_lock *lck);

/**
 * check new lock and current locks compatibility
 * @param lfd rl descriptor
 * @param lck new lock descriptor
 * @return true - compatible, false - otherwise
 */
static bool is_rl_compatible(rl_descriptor lfd, struct flock *lck);

/**
 * check if lock has other owners than d
 * @param d file descriptor
 * @param lck lock descriptor
 * @return true - it has, false - it hasn't
 */
static bool is_other_owner(int d, rl_lock *lck);

/**
 * check that lock has owner
 * @param d file descriptor
 * @param lck lock descriptor
 * @return true - it has, false - it hasn't
 */
static bool is_owner(int d, rl_lock *lck);

/**
 * check that regions are matching
 * @param offset region offset
 * @param len region len
 * @param lck lock descriptor
 * @return true - equal, false - not
 */
static bool is_region_equal(off_t offset, off_t len, rl_lock *lck);

/**
 * check that region has intersections or neighbours 
 * @param offset region offset
 * @param len region len
 * @param lck lock descriptor
 * @return true - there is(are), false - not
 */
static bool is_region_intersection_or_neighbour(off_t offset, off_t len, rl_lock *lck);

/**
 * check that region has intersections
 * @param offset region offset
 * @param len region len
 * @param lck lock descriptor
 * @return true - there is(are), false - not
 */
static bool is_region_intersection(off_t offset, off_t len, rl_lock *lck);

/**
 * check that region has intersections
 * @param l lock
 * @param o owner
 * @return true - lock has owner, false - otherwise
 */
bool has_owner(rl_lock *l, owner *o);


///////////////////////////////////         RL_LIBRARY FUNCTIONS       /////////////////////////////////////////////////
//////////                                                                                                      ////////

int rl_init_library() {
    if (g_is_initialized)
    {
        return 0;
    }

    int code = -1;    
    if ((code = init_mutex(&rl_all_files.mutex)) != 0)
    {
        PROC_ERROR(strerror(code));
        return code;
    }
    rl_all_files.nb_files = 0;
    memset(rl_all_files.tab_open_files, 0, sizeof(rl_open_file*) * NB_FILES);   

    g_is_initialized = true;

    return code;
}


rl_descriptor rl_open(const char *path, int oflag, ...)
{    
    va_list        parameters;
    mode_t         mode                   = -1;
    int            fdFile, fdSharedMemory = -1;
    char           pSharedMemName[SHARED_NAME_MAX_LEN];
    char           pSharedSemName[SHARED_NAME_MAX_LEN];
    sem_t         *sharedSem              = NULL;
    rl_open_file*  pRlOpenFile            = NULL;
    rl_descriptor  stRlDescriptor         = {.d = -1, .f = NULL};
    bool           isNewFile              = true;
    bool           isError                = false;
    
    // ======================================== get arguments ==========================================================
    
    if (oflag & O_CREAT)
    {
        va_start(parameters, oflag);
        mode = (mode_t)va_arg(parameters, int);
        va_end(parameters);
    }

    pthread_mutex_lock(&rl_all_files.mutex);

    if (NB_FILES == rl_all_files.nb_files)
    {
        PROC_ERROR("Unable to proceed rl_open because the library's limit on the number of open files (NB_FILES) has been reached");
        isError = true;
        goto lExit;
    }

    //first open the file, if we can't - everything else is useless 
    fdFile = open(path, oflag, mode);
    if (fdFile < 0)
    {
        PROC_ERROR("open() file failure");
        isError = true;
        goto lExit;
    }

   
    // =================== open or create shared memory object =========================================================
    if (    (!make_shared_name_by_path(path, SHARED_PREFIX_MEM, pSharedMemName, SHARED_NAME_MAX_LEN))
         || (!make_shared_name_by_path(path, SHARED_PREFIX_SEM, pSharedSemName, SHARED_NAME_MAX_LEN))
       )
    {
        PROC_ERROR("making shared names failure!");
        isError = true;
        goto lExit;
    }

    printf("{%s} shared name %s\n", path, pSharedMemName);

    // use semaphore to protect process of creation shared memory
    sharedSem = sem_open(pSharedSemName, O_CREAT | O_EXCL, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH, 0);
    if (NULL == sharedSem)
    {
        sharedSem = sem_open(pSharedSemName, 0);
        if (NULL == sharedSem)
        {
            PROC_ERROR("sem_open() failed");
            isError = true;
            goto lExit;  
        }
        if (0 > sem_wait(sharedSem))
        {
            PROC_ERROR("sem_wait() error");
            isError = true;
            goto lExit;  
        }
    }

    fdSharedMemory = shm_open(pSharedMemName, 
                              O_CREAT | O_RDWR | O_EXCL,  
                              S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);

    if (0 <= fdSharedMemory)
    {
        printf("new shared file %s!\n", pSharedMemName);
    }
    else
    {
        printf("existing shared file %s!\n", pSharedMemName);
        isNewFile = false;

        fdSharedMemory = shm_open(pSharedMemName, 
                                  O_RDWR, 
                                  S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);

        if (0 > fdSharedMemory)
        {
            PROC_ERROR("shm_open() failure");
            isError = true;
            goto lExit;  
        }
    }

    if ((isNewFile) && (0 > ftruncate(fdSharedMemory, sizeof(rl_open_file))))
    {
        PROC_ERROR("ftruncate() failure");
        isError = true;
        goto lExit;  
    }

    // map object to memory
    pRlOpenFile = mmap(0, sizeof(rl_open_file), PROT_READ|PROT_WRITE, MAP_SHARED, fdSharedMemory, 0);
    if (MAP_FAILED == (void *)pRlOpenFile)
    {
        PROC_ERROR("mmap() failure");
        isError = true;
        goto lExit;  
    }

    if (isNewFile) 
    {
        memset(pRlOpenFile, 0, sizeof(rl_open_file));
        init_mutex(&pRlOpenFile->mutex);

        init_cond(&pRlOpenFile->cond);
        pRlOpenFile->blockCnt = 0;

        pRlOpenFile->first = NEXT_NULL;
        for (int i =0; i < NB_LOCKS; i++)
        {
            pRlOpenFile->lock_table[i].next_lock = NEXT_NULL;
        }
    }

    // ============================== register new rl_open_file in rl_all_files ========================================
    rl_all_files.tab_open_files[rl_all_files.nb_files] = pRlOpenFile;
    rl_all_files.nb_files++;

    pRlOpenFile->refCnt++;
    printf("Open: RC : %d\n", pRlOpenFile->refCnt);

lExit:

    pthread_mutex_unlock(&rl_all_files.mutex);

    if (isError)
    {
        CLOSE_FILE(fdFile);
        fdFile = FILE_UNK;

        FREE_MMAP(pRlOpenFile, sizeof(rl_open_file));
        pRlOpenFile = NULL;

        if (isNewFile)
        {
            shm_unlink(pSharedMemName);
        }
    }

    CLOSE_FILE(fdSharedMemory);

    if (sharedSem)
    {
        sem_post(sharedSem);
        sem_close(sharedSem);
        if ((isError) && (isNewFile))
        {
            sem_unlink(pSharedSemName);
        }
    }

    stRlDescriptor.d = fdFile;
    stRlDescriptor.f = pRlOpenFile;    
    return stRlDescriptor;
}


int rl_close(rl_descriptor lfd)
{
    char    pSharedMemName[SHARED_NAME_MAX_LEN];
    char    pSharedSemName[SHARED_NAME_MAX_LEN];
    sem_t  *sharedSem      = NULL;
    bool    isError        = false;
    int     rc             = -1;
    bool    isLastRef      = false;
    int     lockIdx        = NEXT_NULL;

    if ((lfd.d == FILE_UNK) || (!lfd.f))
    {
        PROC_ERROR("wrong input");
        return RES_ERR;
    }

    if (    (!make_shared_name_by_fd(lfd.d, SHARED_PREFIX_MEM, pSharedMemName, SHARED_NAME_MAX_LEN))
         || (!make_shared_name_by_fd(lfd.d, SHARED_PREFIX_SEM, pSharedSemName, SHARED_NAME_MAX_LEN))
       )
    {
        PROC_ERROR("making shared names failure!");
        isError = true;
        goto lExit;
    }

    sharedSem = sem_open(pSharedSemName, 0);
    if (NULL == sharedSem)
    {
        PROC_ERROR("sem_open() failed");
        isError = true;
        goto lExit;  
    }
    if (0 > sem_wait(sharedSem))
    {
        PROC_ERROR("sem_wait() error");
        isError = true;
        goto lExit;  
    }

    pthread_mutex_lock(&lfd.f->mutex);

    printf("looking through locks...\n");
    lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        int nextLock = lfd.f->lock_table[lockIdx].next_lock;
        delete_owner(lfd.f, lockIdx, lfd.d);
        lockIdx = nextLock;
    }

    lfd.f->refCnt --;
    printf("Close: RC : %d!\n", lfd.f->refCnt);
    if (lfd.f->refCnt <= 0)
    {
        isLastRef = true;    
    }

    rc = lfd.f->refCnt;

    for (int i = 0; i < rl_all_files.nb_files; i++)    
    {
        if (lfd.f == rl_all_files.tab_open_files[i])
        {
            if ((i+1) < rl_all_files.nb_files)
            {
                memmove(&rl_all_files.tab_open_files[i],    // erase open file from array by shifting all next ones
                        &rl_all_files.tab_open_files[i+1],
                        sizeof(rl_open_file*) * (rl_all_files.nb_files - (i+1)) );
                i--;
            }
            rl_all_files.nb_files--;    
            break;
        }
    }

    pthread_mutex_unlock(&lfd.f->mutex);

lExit:
    CLOSE_FILE(lfd.d);

    if (isLastRef)
    {
        printf("last ref!\n");

        if (lfd.f->first >= 0)
        {
            PROC_ERROR("Last reference deleted, but file locks aren't deleted!");
        }

        pthread_cond_destroy(&lfd.f->cond);
        pthread_mutex_destroy(&lfd.f->mutex);
        FREE_MMAP(lfd.f, sizeof(rl_open_file));
        shm_unlink(pSharedMemName);
        CLOSE_FILE(lfd.d);
    }

    if (sharedSem)
    {
        sem_post(sharedSem);
        sem_close(sharedSem);
        if (isLastRef)
        {
            sem_unlink(pSharedSemName);
        }
    }

    return isError ? -1 : rc;
}

//==============================================================================================================

rl_descriptor rl_dup(rl_descriptor lfd){
    rl_descriptor ret       = {.d   = -1,    .f    = NULL    };
    owner         new_owner = {.des = -1,    .proc = getpid()};
    owner         own       = {.des = lfd.d, .proc = getpid()};

    if ((lfd.d == FILE_UNK) || (!lfd.f))
    {
        PROC_ERROR("wrong input");
        return ret;
    }

    pthread_mutex_lock(&lfd.f->mutex);

    if (NB_FILES <= rl_all_files.nb_files)
    {
        PROC_ERROR("Unable to proceed rl_open because the library's limit on the number of open files (NB_FILES) has been reached");
        goto lExit;
    }

    // Gestion erreur
    int add = can_add_new_owner(own, lfd.f);
    if(add == -1) 
    {
        PROC_ERROR("rl_dup() failure NB_OWNERS at max");
        goto lExit;
    }

    new_owner.des = dup(lfd.d);
    if(new_owner.des == -1) 
    {
        PROC_ERROR("dup() failure"); // no close of newd (ref man dup)
        goto lExit;
    }

    if(add == 1) 
    {
        add_new_owner(own, new_owner, lfd.f);
    }

    ret.d = new_owner.des;
    ret.f = lfd.f;
    lfd.f->refCnt++;
    rl_all_files.tab_open_files[rl_all_files.nb_files] = lfd.f;
    rl_all_files.nb_files++;
    
    printf("Dup: RC : %d Fd:%d\n", lfd.f->refCnt, new_owner.des);

lExit:    
    pthread_mutex_unlock(&lfd.f->mutex);

    return ret;
}


rl_descriptor rl_dup2(rl_descriptor lfd, int newd) {
    rl_descriptor ret       = {.d   = -1,    .f    = NULL    };
    owner         new_owner = {.des = newd,  .proc = getpid()};
    owner         own       = {.des = lfd.d, .proc = getpid()};
    
    if ((lfd.d == FILE_UNK) || (!lfd.f))
    {
        PROC_ERROR("wrong input");
        return ret;
    }

    pthread_mutex_lock(&lfd.f->mutex);

    if (NB_FILES <= rl_all_files.nb_files)
    {
        PROC_ERROR("Unable to proceed rl_open because the library's limit on the number of open files (NB_FILES) has been reached");
        goto lExit;
    }

    // Gestion erreur
    int add = can_add_new_owner(own, lfd.f);
    if(add == -1) 
    {
        PROC_ERROR("rl_dup() failure NB_OWNERS at max");
        goto lExit;
    }

    if(dup2(lfd.d, newd) == -1) 
    {
        PROC_ERROR("dup() failure"); // no close of newd (ref man dup)
        goto lExit;
    }

    if(add == 1) 
    {
        add_new_owner(own, new_owner, lfd.f);
    }

    ret.d = new_owner.des;
    ret.f = lfd.f;
    lfd.f->refCnt++;

    rl_all_files.tab_open_files[rl_all_files.nb_files] = lfd.f;
    rl_all_files.nb_files++;

    printf("Dup2: RC : %d\n", lfd.f->refCnt);

lExit:    
    pthread_mutex_unlock(&lfd.f->mutex);

    return ret;
}

pid_t rl_fork() 
{
    for(int i = 0; i < rl_all_files.nb_files; i++)
    {
        pthread_mutex_lock(&rl_all_files.tab_open_files[i]->mutex);
        if(can_add_new_owner_by_pid(getppid(), rl_all_files.tab_open_files[i]) == -1) 
        {
            pthread_mutex_unlock(&rl_all_files.tab_open_files[i]->mutex);
            PROC_ERROR("rl_fork() failure NB_OWNERS at max");
            return -1;
        }
        pthread_mutex_unlock(&rl_all_files.tab_open_files[i]->mutex);
    }

    pid_t pid;
    switch(pid = fork()) 
    {
        case 0 :
            for(int i = 0; i < rl_all_files.nb_files; i++)
            {
                pthread_mutex_lock(&rl_all_files.tab_open_files[i]->mutex);
                add_new_owner_by_pid(getppid(), getpid(), rl_all_files.tab_open_files[i]);
                rl_all_files.tab_open_files[i]->refCnt++;
                printf("[%d] Fork: RC : %d\n", getpid(), rl_all_files.tab_open_files[i]->refCnt);
                pthread_mutex_unlock(&rl_all_files.tab_open_files[i]->mutex);
            }
    }
    return pid;
}


int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck)
{
    if ((lfd.d == FILE_UNK) || (!lfd.f) || (F_GETLK == cmd) || (!lck))
    {
        PROC_ERROR("wrong input");
        return -1;
    }

    struct flock lc         = *lck;
    int          ret        = 0;
    bool         isBlocking = (F_SETLKW == cmd);

    //align start & len to make common way
    if (lc.l_whence == SEEK_CUR)      { lc.l_start = (__off_t)get_current_position(lfd.d) + lc.l_start;  }
    else if (lc.l_whence == SEEK_END) { lc.l_start = (__off_t)get_file_size(lfd.d) + lc.l_start;         }
    if (lc.l_len == 0)                { lc.l_len   = (__off_t)get_file_size(lfd.d) - lc.l_start;         }
    if (lc.l_len < 0)                 { lc.l_start += lc.l_len; lc.l_len = -lc.l_len;                  }
    
    lc.l_pid = getpid();
    lc.l_whence = SEEK_SET; 

    pthread_mutex_lock(&lfd.f->mutex);

    rl_clear_dead_owners(lfd);


    if (lc.l_type == F_UNLCK)
    {
        ret = delete_lock_region(lfd, &lc);

        //if any process is waiting -> unblock
        if (lfd.f->blockCnt)
        {
            printf("!!!SIGNALLED!!!\n");

            //clear block counter and notify all
            lfd.f->blockCnt = 0;
            pthread_cond_broadcast(&lfd.f->cond);
        }
    }
    else
    {
        if (isBlocking)
        {
            while (1)
            {
                if (!is_rl_compatible(lfd, &lc))
                {
                    printf("!!!BLOCKED!!!\n");                    
                    lfd.f->blockCnt ++;                    
                    pthread_cond_wait(&lfd.f->cond, &lfd.f->mutex);
                }
                else
                {
                    printf("!!!UNBLOCKED!!!\n");
                    break;
                }
            }
        }
        else
        {
            if (!is_rl_compatible(lfd, &lc))
            {
                ret = -1;
                PROC_ERROR("Lock isn't compatible");
                errno = EAGAIN;
                goto lExit;
            }
        }

        if (lc.l_type == F_RDLCK)
        {
            ret = add_read_lock_region(lfd, &lc);
        }
        else if (lc.l_type == F_WRLCK)
        {
            ret = add_write_lock_region(lfd, &lc);
        }
    }


lExit:    
    pthread_mutex_unlock(&lfd.f->mutex);

    return ret;
}


void rl_print(rl_descriptor lfd)
{
    if ((lfd.d == -1) || (!lfd.f))
    {
        PROC_ERROR("wrong input");
        return;
    }

    printf(KRED "> RL d:%d, references %d" KNRM, lfd.d, lfd.f->refCnt);
    
    int lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        printf(KGRN " > Lock [%ld..%ld], %s, owners %zu" KNRM, 
               lfd.f->lock_table[lockIdx].starting_offset,
               lfd.f->lock_table[lockIdx].starting_offset + lfd.f->lock_table[lockIdx].len - 1,
               lfd.f->lock_table[lockIdx].type == F_RDLCK ? "RD" : "WR",
               lfd.f->lock_table[lockIdx].nb_owners
              );
        for (size_t i = 0; i < lfd.f->lock_table[lockIdx].nb_owners; i++)
        {
            printf(KBLU "   > Owner %d:%d" KNRM, 
                lfd.f->lock_table[lockIdx].lock_owners[i].des,
                lfd.f->lock_table[lockIdx].lock_owners[i].proc);
        }
        lockIdx = lfd.f->lock_table[lockIdx].next_lock;
    }
    printf(KNRM);
}

////////////////////////////////////         AUXILIARY FONCTIONS       /////////////////////////////////////////////////
//////////                                                                                                      ////////

static int init_mutex(pthread_mutex_t *pMutex)
{
    pthread_mutexattr_t mutexAttr;
    int code = pthread_mutexattr_init(&mutexAttr);
    if (code != 0)
    {
        return code;
    }
    code = pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_SHARED);
    if (code != 0)
    {
        return code;
    }
    code = pthread_mutex_init(pMutex, &mutexAttr);
    return code;
}


static int init_cond(pthread_cond_t *pCond)
{
    pthread_condattr_t condAttr;
    int code;
    code = pthread_condattr_init(&condAttr);

    if (code != 0)
    {
        return code;
    }
    
    code = pthread_condattr_setpshared(&condAttr, PTHREAD_PROCESS_SHARED);
    if (code != 0)
    {
        return code;
    }
    return pthread_cond_init(pCond, &condAttr);
}


static bool make_shared_name_by_path(const char *filePath, char type, char *name, size_t maxLen)
{        
    int returnValue = -1;
    struct stat statBuffer;
    
    if ((!filePath) || (!name))
    {
        PROC_ERROR("wrong arguments");
        return NULL;
    }

    returnValue = stat(filePath, &statBuffer);
    if (returnValue < 0)
    {
        PROC_ERROR("stat() failure");
        return NULL;
    }

    if (0 > snprintf(name, maxLen, SHARED_MEM_FORMAT, type, statBuffer.st_dev, statBuffer.st_ino))
    {
        PROC_ERROR("name formatting error! not enough space?");
        return NULL;
    }
    return true;
}


static bool make_shared_name_by_fd(int fd, char type, char *name, size_t maxLen)
{
    int returnValue = -1;
    struct stat statBuffer;
    
    if (!name)
    {
        PROC_ERROR("wrong arguments");
        return false;
    }

    returnValue = fstat(fd, &statBuffer);
    if (returnValue < 0)
    {
        PROC_ERROR("stat() failure");
        return NULL;
    }
 
    if (0 > snprintf(name, maxLen, SHARED_MEM_FORMAT, type, statBuffer.st_dev, statBuffer.st_ino))
    {
        PROC_ERROR("name formatting error! not enough space?");
        return NULL;
    }
    return true;
}


static bool is_owners_are_equal(owner o1, owner o2){
    return ((o1.des == o2.des) && (o1.proc == o2.proc));
}


static int can_add_new_owner(owner own, rl_open_file *f){
    int ind = f->first;
    int res = 0;
    //ind can be NEXT_NULL(-2) or NEXT_LAST(-1)
    while(ind >= 0) { 
        for(size_t i = 0; i < f->lock_table[ind].nb_owners; i++){
            if(is_owners_are_equal(f->lock_table[ind].lock_owners[i], own)) {
                if (f->lock_table[ind].nb_owners >= NB_OWNERS) { 
                    return -1; 
                }
                res = 1;
            }
        }
        ind = f->lock_table[ind].next_lock;
    }
    return res;
}

bool has_owner(rl_lock *l, owner *o)
{
    for(size_t i = 0; i < l->nb_owners; i++)
    {
        if(is_owners_are_equal(l->lock_owners[i], *o))
        {
            return true;
        }
    }

    return false;
}



static int add_new_owner(owner own, owner new_owner, rl_open_file *f)
{
    int ind = f->first;
    //ind can be NEXT_NULL(-2) or NEXT_LAST(-1)
    while(ind >= 0) 
    {
        if (f->lock_table[ind].type == F_RDLCK)
        {
            for(size_t i = 0; i < f->lock_table[ind].nb_owners; i++)
            {
                if(is_owners_are_equal(f->lock_table[ind].lock_owners[i], own))
                {
                    if (!has_owner(&f->lock_table[ind], &new_owner))
                    {
                        f->lock_table[ind].lock_owners[f->lock_table[ind].nb_owners] = new_owner;
                        f->lock_table[ind].nb_owners ++;
                    }
                }
            }
        }
        ind = f->lock_table[ind].next_lock;
    }
    return 0;
}

static int can_add_new_owner_by_pid(pid_t parent, rl_open_file *f)
{
    int ind = f->first;
    
    // loop through lock_table
    //ind can be NEXT_NULL(-2) or NEXT_LAST(-1)
    while(ind >= 0){ 
        // loop through lock_owners
        for(size_t i = 0; i < f->lock_table[ind].nb_owners; i++){
            if(f->lock_table[ind].lock_owners[i].proc == parent 
            && f->lock_table[ind].nb_owners >= NB_OWNERS) {
                return -1;
            }
            
        }
        ind = f->lock_table[ind].next_lock;
    }
    return 0;
}

static int add_new_owner_by_pid(pid_t parent, pid_t fils, rl_open_file *f){
    int ind = f->first;
    int res = 0;
    while(ind >= 0)
    {
        if (f->lock_table[ind].type == F_RDLCK)
        {
            for(size_t i = 0; i < f->lock_table[ind].nb_owners; i++)
            {
                if(f->lock_table[ind].lock_owners[i].proc == parent)
                {
                    if (f->lock_table[ind].nb_owners < NB_OWNERS) 
                    {
                        owner new_owner = {.des = f->lock_table[ind].lock_owners[i].des, .proc = fils};

                        if (!has_owner(&f->lock_table[ind], &new_owner))
                        {
                            f->lock_table[ind].lock_owners[f->lock_table[ind].nb_owners] = new_owner;
                            f->lock_table[ind].nb_owners ++;
                        }
                    }
                    else 
                    {
                        PROC_ERROR("add_new_owner_by_pid() failure NB_OWNERS at max");
                        res = -1;
                    }
                }
            }
        }
        ind = f->lock_table[ind].next_lock;
    }
    return res;
}

static void delete_owner(rl_open_file *f, int index, int d)
{
    for (int i = 0; i < f->lock_table[index].nb_owners; i++)
    {
        if (    (f->lock_table[index].lock_owners[i].proc == getpid())   //if this proc has lock.s for this fd
             && (f->lock_table[index].lock_owners[i].des  == d)
           )
        {
            if ((i+1) < f->lock_table[index].nb_owners)
            {
                memmove(&f->lock_table[index].lock_owners[i],    // erase him from owners and shift left the rest
                        &f->lock_table[index].lock_owners[i+1],
                        sizeof(owner) * (f->lock_table[index].nb_owners - (i+1)) );
                i--;
            }
            f->lock_table[index].nb_owners--;    
        }
    }

    if (!f->lock_table[index].nb_owners)
    {
        delete_lock(f, index);
    }
}

static void delete_lock(rl_open_file *f, int index)
{
    int prevIdx = NEXT_NULL;
    int lockIdx = f->first;
    while (lockIdx >= 0)
    {
        if (index == lockIdx)
        {
            if (prevIdx != NEXT_NULL)
            {
                f->lock_table[prevIdx].next_lock = f->lock_table[lockIdx].next_lock;
            }
            else 
            {
                f->first = f->lock_table[lockIdx].next_lock;    
            }

            f->lock_table[lockIdx].nb_owners       = 0;
            f->lock_table[lockIdx].next_lock       = NEXT_NULL;
            f->lock_table[lockIdx].len             = 0;
            f->lock_table[lockIdx].starting_offset = 0;
            f->lock_table[lockIdx].type            = 0;
            return;
        }

        prevIdx = lockIdx;
        lockIdx = f->lock_table[lockIdx].next_lock;    
    }
}



static uint64_t get_file_size(int fd)
{
    struct stat l_sStat;
    l_sStat.st_size = 0;
    if (0 == fstat(fd, &l_sStat))
    {
        return (uint64_t)l_sStat.st_size;
    }

    return 0;
}

static uint64_t get_current_position(int fd)
{
    return (uint64_t)lseek(fd, 0, SEEK_CUR);    
}

static bool is_region_intersection(off_t offset, off_t len, rl_lock *lck)
{
    if ((offset >= lck->starting_offset) && (offset < (lck->starting_offset + lck->len))) return true;
    if (((offset + len) > lck->starting_offset) && ((offset + len) <= (lck->starting_offset + lck->len))) return true;
    if ((offset <= lck->starting_offset) && ((offset + len) >= (lck->starting_offset + lck->len))) return true;

    return false;
}

static bool is_region_intersection_or_neighbour(off_t offset, off_t len, rl_lock *lck)
{
    if ((offset >= lck->starting_offset) && (offset <= (lck->starting_offset + lck->len))) return true;
    if (((offset + len) >= lck->starting_offset) && ((offset + len) <= (lck->starting_offset + lck->len))) return true;
    if ((offset <= lck->starting_offset) && ((offset + len) >= (lck->starting_offset + lck->len))) return true;

    return false;
}

static bool is_region_equal(off_t offset, off_t len, rl_lock *lck)
{
    return (offset == lck->starting_offset) && (len == lck->len);
}

static bool is_rl_compatible(rl_descriptor lfd, struct flock *lck)
{
    int lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        if (    (is_region_intersection(lck->l_start, lck->l_len, &lfd.f->lock_table[lockIdx]))
             && (is_other_owner(lfd.d, &lfd.f->lock_table[lockIdx]))
           )
        {
            if (lck->l_type == F_WRLCK)    
            {
                return false;
            }
            if ((lck->l_type == F_RDLCK) && (lfd.f->lock_table[lockIdx].type == F_WRLCK))
            {
                return false;
            }
        }

        lockIdx = lfd.f->lock_table[lockIdx].next_lock;    
    }

    return true;
}


static bool is_owner(int d, rl_lock *lck)
{
    pid_t curPid = getpid();
    for (size_t szI = 0; szI < lck->nb_owners; szI ++)    
    {
        if ((lck->lock_owners[szI].des == d) && (lck->lock_owners[szI].proc == curPid)) return true;
    }

    return false;
}

static bool is_other_owner(int d, rl_lock *lck)
{
    pid_t curPid = getpid();
    for (size_t szI = 0; szI < lck->nb_owners; szI ++)    
    {
        if ((lck->lock_owners[szI].des != d) || (lck->lock_owners[szI].proc != curPid)) return true;
    }

    return false;
}

static void rl_clear_dead_owners(rl_descriptor lfd)
{
    int lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        for (int i = 0; i < lfd.f->lock_table[lockIdx].nb_owners; i++)
        {
            if (0 != kill(lfd.f->lock_table[lockIdx].lock_owners[i].proc, 0)) //process is dead
            {
                if ((i+1) < lfd.f->lock_table[lockIdx].nb_owners)
                {
                    memmove(&lfd.f->lock_table[lockIdx].lock_owners[i],    // erase him from owners and shift left the rest
                            &lfd.f->lock_table[lockIdx].lock_owners[i+1],
                            sizeof(owner) * (lfd.f->lock_table[lockIdx].nb_owners - (i+1)) );
                    i--;
                }
                lfd.f->lock_table[lockIdx].nb_owners--;    
            }
        }

        if (!lfd.f->lock_table[lockIdx].nb_owners) //if there is no more owners for lock -> remove lock from lock_table
        {
            int nextLock = lfd.f->lock_table[lockIdx].next_lock;
            lfd.f->lock_table[lockIdx].next_lock = NEXT_NULL;
            
            if (lfd.f->first == lockIdx)
            {
                lfd.f->first = nextLock;
            }
            lockIdx = nextLock;
        }
        else
        {
            lockIdx = lfd.f->lock_table[lockIdx].next_lock;    
        }
    }
}

static int add_owner(rl_descriptor lfd, rl_lock *lck)
{
    if (lck->nb_owners >= NB_OWNERS)
    {
        PROC_ERROR("Lock is full");
        errno = EAGAIN;
        return -1;
    }

    owner o;
    o.des = lfd.d;
    o.proc = getpid();

    if (!has_owner(lck, &o))
    {
        lck->lock_owners[lck->nb_owners] = o;
        lck->nb_owners ++;
    }

    return 0;
}

static int add_lock(rl_open_file *f, struct flock *lck, int d, int type)
{
    for (size_t szI = 0; szI < NB_LOCKS; szI ++)
    {
        if (f->lock_table[szI].len == 0)
        {
            f->lock_table[szI].next_lock           = f->first;
            f->lock_table[szI].starting_offset     = lck->l_start;
            f->lock_table[szI].len                 = lck->l_len;
            f->lock_table[szI].type                = type;
            f->lock_table[szI].lock_owners[0].proc = getpid();
            f->lock_table[szI].lock_owners[0].des  = d;
            f->lock_table[szI].nb_owners           = 1;

            f->first = szI;
            return 0;
        }
    }

    PROC_ERROR("Lock has no free space");
    errno = EAGAIN;
    return -1;
}

static int add_read_lock_region(rl_descriptor lfd, struct flock *lck)
{
    //search for exact segment
    int lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        if (is_region_equal(lck->l_start, lck->l_len, &lfd.f->lock_table[lockIdx]))
        {
            if (is_owner(lfd.d, &lfd.f->lock_table[lockIdx]))
            {
                return 0;
            }
            else
            {
                return add_owner(lfd, &lfd.f->lock_table[lockIdx]);
            }
        }
        lockIdx = lfd.f->lock_table[lockIdx].next_lock;    
    }

    //search for all intersections where lfd is owner
    lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        if (    (is_region_intersection_or_neighbour(lck->l_start, lck->l_len, &lfd.f->lock_table[lockIdx]))
             && (is_owner(lfd.d, &lfd.f->lock_table[lockIdx]))
           )
        {
            off_t newStart = MIN(lck->l_start, lfd.f->lock_table[lockIdx].starting_offset);
            off_t newEnd1  = lck->l_start + lck->l_len;
            off_t newEnd2  = lfd.f->lock_table[lockIdx].starting_offset + lfd.f->lock_table[lockIdx].len;
            off_t newLen   = MAX(newEnd1, newEnd2) - newStart; 

            lck->l_start = newStart;
            lck->l_len   = newLen;

            int nextIdx = lfd.f->lock_table[lockIdx].next_lock;    
            delete_owner(lfd.f, lockIdx, lfd.d);
            lockIdx = nextIdx;
        }
        else
        {
            lockIdx = lfd.f->lock_table[lockIdx].next_lock;    
        }
    }

    return add_lock(lfd.f, lck, lfd.d, F_RDLCK);
}

static int add_write_lock_region(rl_descriptor lfd, struct flock *lck)
{
    int lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        if (    (    (    (is_region_intersection_or_neighbour(lck->l_start, lck->l_len, &lfd.f->lock_table[lockIdx]))
                       && (lfd.f->lock_table[lockIdx].type == lck->l_type)
                     )
                  || (is_region_intersection(lck->l_start, lck->l_len, &lfd.f->lock_table[lockIdx]))
                ) 
             && (is_owner(lfd.d, &lfd.f->lock_table[lockIdx]))
           )
        {
            off_t newStart = MIN(lck->l_start, lfd.f->lock_table[lockIdx].starting_offset);
            off_t newEnd1  = lck->l_start + lck->l_len;
            off_t newEnd2  = lfd.f->lock_table[lockIdx].starting_offset + lfd.f->lock_table[lockIdx].len;
            off_t newLen   = MAX(newEnd1, newEnd2) - newStart; 

            lck->l_start = newStart;
            lck->l_len   = newLen;

            int nextIdx = lfd.f->lock_table[lockIdx].next_lock;    
            delete_owner(lfd.f, lockIdx, lfd.d);
            lockIdx = nextIdx;
        }
        else
        {
            lockIdx = lfd.f->lock_table[lockIdx].next_lock;    
        }
    }

    return add_lock(lfd.f, lck, lfd.d, F_WRLCK);
}

static int delete_lock_region(rl_descriptor lfd, struct flock *lck)
{
    int lockIdx = lfd.f->first;
    while (lockIdx >= 0)
    {
        if (    (is_region_intersection(lck->l_start, lck->l_len, &lfd.f->lock_table[lockIdx]))
             && (is_owner(lfd.d, &lfd.f->lock_table[lockIdx]))
           )
        {
            off_t unlStart = lck->l_start;
            off_t unlEnd   = lck->l_start + lck->l_len;
            off_t lckStart = lfd.f->lock_table[lockIdx].starting_offset;
            off_t lckEnd   = lfd.f->lock_table[lockIdx].starting_offset + lfd.f->lock_table[lockIdx].len;

            //if lock region is include in unlock region
            if ((unlStart <= lckStart) && (unlEnd >= lckEnd))
            {
                int nextIdx = lfd.f->lock_table[lockIdx].next_lock;    
                delete_owner(lfd.f, lockIdx, lfd.d);
                lockIdx = nextIdx;
            }
            //unlock region is include in lock region -> remove owner, make 2 and start from beginning because we add
            //new region to head
            else if ((unlStart > lckStart) && (unlEnd < lckEnd))
            {
                struct flock lckLeft  = *lck;
                struct flock lckRight = *lck;
                lckLeft.l_start = lckStart;
                lckLeft.l_len   = unlStart - lckStart;

                lckRight.l_start = unlEnd;
                lckRight.l_len   = lckEnd - unlEnd;

                if (    (0 != add_lock(lfd.f, &lckLeft,  lfd.d, lfd.f->lock_table[lockIdx].type))
                     || (0 != add_lock(lfd.f, &lckRight, lfd.d, lfd.f->lock_table[lockIdx].type))
                   )
                {
                    return -1;
                }

                delete_owner(lfd.f, lockIdx, lfd.d);
                lockIdx = lfd.f->first;
            }
            //unlock region has right intersection with lock region, remove owner, split and start from beginning because we add
            //new region to head
            else if ((unlStart <= lckStart) && (unlEnd <= lckEnd))
            {
                struct flock lckRight  = *lck;
                lckRight.l_start = unlEnd;
                lckRight.l_len   = lckEnd - unlEnd;

                if (0 != add_lock(lfd.f, &lckRight, lfd.d, lfd.f->lock_table[lockIdx].type))
                {
                    return -1;
                }

                delete_owner(lfd.f, lockIdx, lfd.d);
                lockIdx = lfd.f->first;
            }            
            //unlock region has right intersection with lock region, remove owner, split and start from beginning because we add
            //new region to head
            else if ((unlStart >= lckStart) && (unlEnd >= lckEnd))
            {
                struct flock lckLeft  = *lck;
                lckLeft.l_start = lckStart;
                lckLeft.l_len   = unlStart - lckStart;

                if (0 != add_lock(lfd.f, &lckLeft, lfd.d, lfd.f->lock_table[lockIdx].type))
                {
                    return -1;
                }

                delete_owner(lfd.f, lockIdx, lfd.d);
                lockIdx = lfd.f->first;
            }            
        }
        else
        {
            lockIdx = lfd.f->lock_table[lockIdx].next_lock;    
        }
    }

    return 0;
}

