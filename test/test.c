#include "rl_lock_library.h"
#include <unistd.h>

#define SHR_TEST_SEM        "/rl_test_shared_sem"

#define PROC_ERROR(Message) { fprintf(stderr, "%s : error {%s} in file {%s} on line {%d}\n", \
                                                Message, strerror(errno), __FILE__, __LINE__); }\

//https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_(Select_Graphic_Rendition)_parameters
#define KNRM                "\x1B[0m\n"
#define KRED                "\x1B[31m"
#define KGRN                "\x1B[32m"
#define KYEL                "\x1B[33m"
#define KBLU                "\x1B[34m"
#define KMAG                "\x1B[35m"
#define KCYN                "\x1B[36m"
#define KWHT                "\x1B[37m"

#define SHR_TEST_SEM        "/rl_test_shared_sem"
#define TEST_ALL            0
static int indexTest = 0;


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//IMPORTANT !!! If test is failed need manually remove shared objects from !
//                           /dev/shm                                      !
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!



#define TEST_EXEC(result, desc, testIndex)\
    if ((testIndex == indexTest) || (indexTest == TEST_ALL))\
    {\
        printf("\x1B[92;100m[%d]>>Execute test {%s} -------------------------------------------------" KNRM, getpid(), desc);\
        if (!result)\
        {\
            printf("\x1B[30;101m[%d]>>Test {%s} failed" KNRM, getpid(), desc);\
            res = -1;\
            goto lExit;\
        }\
        else\
        {\
            printf("\x1B[30;102m[%d]>>Test {%s} success" KNRM, getpid(), desc);\
        }\
    }\

    


bool test_reference_counter(const char *fileName)
{
    printf("file to open : %s\n", fileName);

    // open
    rl_descriptor rl_fd1 = rl_open(fileName, O_RDWR, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);

    if (rl_fd1.f == NULL)
    {
        return false;
    }

    // dup
    rl_descriptor rl_fd2 = rl_dup(rl_fd1);
    if (rl_fd2.f == NULL)
    {
        return false;
    }

    sem_t *sharedSem = sem_open(SHR_TEST_SEM, O_CREAT, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH, 0);

    // fork
    pid_t pid = rl_fork();
    if (-1 == pid)
    {
        return false;
    }
    if (0 == pid)
    {
        sharedSem = sem_open(SHR_TEST_SEM, 0);

        rl_print(rl_fd2);

        rl_close(rl_fd2);
        rl_close(rl_fd1);
        
        sem_post(sharedSem);
        sem_close(sharedSem);
        indexTest = 1; // only this test

        return true;
    }
    else
    {
        sem_wait(sharedSem);
        sem_close(sharedSem);
        sem_unlink(SHR_TEST_SEM);

        rl_close(rl_fd2);

        return (0 == rl_close(rl_fd1));
    }
    return false;
}


bool test_regions(const char *fileName)
{
    rl_descriptor rl_fd1 = rl_open(fileName, O_RDWR, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);
    rl_descriptor rl_fd2 = rl_open(fileName, O_RDWR, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);

    struct flock lck;
    lck.l_start  = 0;
    lck.l_len    = 100; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_RDLCK;

    if (0 != rl_fcntl(rl_fd1, F_SETLK, &lck))
    {
        return false;
    }

    rl_print(rl_fd1);
    printf("**********************************************\n");

    lck.l_start  = 50;
    lck.l_len    = 100; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_RDLCK;

    if (0 == rl_fcntl(rl_fd1, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd1);
    printf("**********************************************\n");
    lck.l_start  = 50;
    lck.l_len    = 50; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_UNLCK;
    if (0 != rl_fcntl(rl_fd1, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd1);
    printf("**********************************************\n");

    return true;
    //checking for region joining: we have NB_LOCKS = 10 so trying to push more
    //has to generate error if inside we are not joining them
    /*for (int i = 0; i < 2* NB_LOCKS; i++)
    {
        lck.l_start  = i*50;
        lck.l_len    = 50; 
        lck.l_pid    = getpid();
        lck.l_whence = SEEK_SET;
        lck.l_type   = F_WRLCK;

        if (0 != rl_fcntl(rl_fd1, F_SETLK, &lck))
        {
            return false;
        }
    }

    rl_print(rl_fd1); */

    lck.l_start  = 0;
    lck.l_len    = 1000; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_UNLCK;
    if (0 != rl_fcntl(rl_fd1, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd1);
    printf("**********************************************\n");

    lck.l_start  = 0;
    lck.l_len    = 500; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_RDLCK;
    if (0 != rl_fcntl(rl_fd1, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd1);
    printf("**********************************************\n");

    lck.l_start  = 1000;
    lck.l_len    = 500; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_RDLCK;
    if (0 != rl_fcntl(rl_fd1, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd1);
    printf("**********************************************\n");
    //transofrm to write 2 prev locks
    lck.l_start  = 0;
    lck.l_len    = 1500; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_WRLCK;
    if (0 != rl_fcntl(rl_fd1, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd1);
    printf("**********************************************\n");
    //must fail = region [0..1500] is busy by WR exclusive lock rl_fd1
    lck.l_start  = 0;
    lck.l_len    = 500; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_RDLCK;
    if (0 == rl_fcntl(rl_fd2, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd2);
    printf("**********************************************\n");
    //must fail = region [0..1500] is busy by WR exclusive lock rl_fd1
    lck.l_start  = 500;
    lck.l_len    = 500; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_RDLCK;
    if (0 == rl_fcntl(rl_fd2, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd2);
    printf("**********************************************\n");

    //must fail = region [0..1500] is busy by WR exclusive lock rl_fd1
    lck.l_start  = 1000;
    lck.l_len    = 500; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_RDLCK;
    if (0 == rl_fcntl(rl_fd2, F_SETLK, &lck))
    {
        return false;
    }
    rl_print(rl_fd2);
    printf("**********************************************\n");

    rl_print(rl_fd1);

    rl_close(rl_fd1);
    rl_close(rl_fd2);

    return true;
}

bool test_cross_process(const char *fileName, int test)
{
    rl_descriptor rl_fd = rl_open(fileName, O_RDWR, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);

    printf(">> test_cross_process call from %d\n",  getpid());

    sem_t *sharedSem = NULL;
    if (test == TEST_ALL)  //parent
    {
        struct flock lck;
        lck.l_start  = 0;
        lck.l_len    = 100; 
        lck.l_pid    = getpid();
        lck.l_whence = SEEK_SET;
        lck.l_type   = F_RDLCK;

        if (0 != rl_fcntl(rl_fd, F_SETLK, &lck))
        {
            return false;
        }

        lck.l_start  = 100;
        lck.l_len    = 100; 
        lck.l_pid    = getpid();
        lck.l_whence = SEEK_SET;
        lck.l_type   = F_WRLCK;

        if (0 != rl_fcntl(rl_fd, F_SETLK, &lck))
        {
            return false;
        }

        sharedSem = sem_open(SHR_TEST_SEM, O_CREAT, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH, 0);

        if (0 == fork()) 
        {
            if (-1 == execl("./bin/rl_lock_test", "rl_lock_test", fileName, "3", NULL)) 
            {
                perror("child process execve failed");
                return false;
            }
        }

        sem_wait(sharedSem);
        sem_close(sharedSem);
        sem_unlink(SHR_TEST_SEM);

        rl_print(rl_fd);

        return (0 == rl_close(rl_fd));
    }
    else
    {
        bool bError = false;
        sharedSem = sem_open(SHR_TEST_SEM, 0);

        struct flock lck;
        lck.l_start  = 0;
        lck.l_len    = 100; 
        lck.l_pid    = getpid();
        lck.l_whence = SEEK_SET;
        lck.l_type   = F_RDLCK;

        if (0 != rl_fcntl(rl_fd, F_SETLK, &lck))
        {
            bError = true;
        }

        lck.l_start  = 100;
        lck.l_len    = 100; 
        lck.l_pid    = getpid();
        lck.l_whence = SEEK_SET;
        lck.l_type   = F_WRLCK;

        if (0 == rl_fcntl(rl_fd, F_SETLK, &lck))
        {
            bError = true;
        }

        rl_print(rl_fd);

        sem_post(sharedSem);
        sem_close(sharedSem);

        if (!bError)
        {
            rl_close(rl_fd);            
        }
    }

    return true;
}


bool test_record_blocking_request(const char *fileName)
{
    printf("file to open : %s\n", fileName);

    rl_descriptor rl_fd = rl_open(fileName, O_RDWR, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH);

    if (rl_fd.f == NULL)
    {
        return false;
    }

    struct flock lck;
    lck.l_start  = 0;
    lck.l_len    = 800; 
    lck.l_pid    = getpid();
    lck.l_whence = SEEK_SET;
    lck.l_type   = F_WRLCK;
    if (0 != rl_fcntl(rl_fd, F_SETLK, &lck))
    {
        return false;
    }

    sem_t *sharedSem = sem_open(SHR_TEST_SEM, O_CREAT, S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH, 0);

    pid_t pid = rl_fork();
    if (-1 == pid)
    {
        return false;
    }
    if (0 == pid)
    {
        sharedSem = sem_open(SHR_TEST_SEM, 0);

        sem_post(sharedSem);

        lck.l_start  = 200;
        lck.l_len    = 200; 
        lck.l_pid    = getpid();
        lck.l_whence = SEEK_SET;
        lck.l_type   = F_WRLCK;
        if (0 != rl_fcntl(rl_fd, F_SETLKW, &lck))
        {
            return false;
        }

        rl_print(rl_fd);

        rl_close(rl_fd);
        
        sem_post(sharedSem);
        sem_close(sharedSem);
        indexTest = 4; // only this test

        return true;
    }
    else
    {
        sem_wait(sharedSem); //wait process to start
        sleep(1); //sleep for 1 second to let other process to enter to blocking mode

        //release block region to let other process to take it
        lck.l_start  = 200;
        lck.l_len    = 200; 
        lck.l_pid    = getpid();
        lck.l_whence = SEEK_SET;
        lck.l_type   = F_UNLCK;
        if (0 != rl_fcntl(rl_fd, F_SETLK, &lck))
        {
            return false;
        }

        sem_wait(sharedSem);
        sem_close(sharedSem);
        sem_unlink(SHR_TEST_SEM);

        rl_print(rl_fd);

        return (0 == rl_close(rl_fd));
    }

    return false;
}


int main(int argc, const char *argv[])
{
    if (argc != 3)
    {
        PROC_ERROR("wrong input, fist argument - name of the file, second - test index (0 == all tests)");
        return EXIT_FAILURE;
    }

    int res = EXIT_SUCCESS;    
    indexTest = atoi(argv[2]);

    if (rl_init_library() != 0)
    {
        res = -1;
        printf("\x1B[30;101m[%d]>>Test initialization failed" KNRM, getpid());
    }

    printf("[%d] file to Process : %s, test : %d\n", getpid(), argv[1], indexTest);

    TEST_EXEC(test_reference_counter(argv[1]), "test_reference_counter", 1);
    TEST_EXEC(test_regions(argv[1]), "test_regions", 2);
    TEST_EXEC(test_cross_process(argv[1], indexTest), "test_cross_process", 3);
    TEST_EXEC(test_record_blocking_request(argv[1]), "test_record_blocking_request", 4);

lExit:
    printf("[%d] exit process\n", getpid());
    return res;
}


