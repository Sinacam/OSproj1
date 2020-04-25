#include<unistd.h>
#include<sched.h>
#include<errno.h>
#include<sys/wait.h>

#include<stdlib.h>
#include<stdio.h>
#include<stdint.h>
#include<time.h>
#include<string.h>

FILE* kmsg;

// Do a unit time's worth of nothingness.
// Because science.
void doUnitTime()
{
    for(volatile unsigned long i = 0; i < 1000000UL; i++)
        ;
}

// C really has no concept of usability huh?
struct timespec now()
{
    struct timespec t;
    int res = clock_gettime(CLOCK_MONOTONIC, &t);
    if(res != 0)
    {
        perror("now");
        return t;
    }
    return t;
}

struct timespec timeAdd(struct timespec tp, struct timespec dur)
{
    struct timespec ret = {tp.tv_sec + dur.tv_sec, tp.tv_nsec + dur.tv_nsec};
    if(ret.tv_nsec > 1000000000)
    {
        ret.tv_sec++;
        ret.tv_nsec -= 1000000000;
    }
    return ret;
}

// One unit of time in nanoseconds.
struct timespec getUnitTime()
{
    struct timespec start = now();
    doUnitTime();
    struct timespec end = now();

    struct timespec ret = {end.tv_sec - start.tv_sec, end.tv_nsec - start.tv_nsec};
    if(ret.tv_nsec < 0)
    {
        ret.tv_sec--;
        ret.tv_nsec += 1000000000;
    }
    return ret;
}

// Info for simulated process.
typedef struct
{
    char name[32];      // lol "less than 32"
    int ready, exec;
    pid_t pid;
} procinfo_t;

void assignCPU(pid_t pid, int core)
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);
    if(sched_setaffinity(pid, sizeof(mask), &mask) < 0)
    {
        perror("assignCPU");
        exit(1);
    }
}

// Performs setup for scheduler to work properly.
void setup()
{
    pid_t pid = getpid();
    struct sched_param params;
    params.sched_priority = 0;     // realtime
    int policy = sched_setscheduler(pid, SCHED_OTHER | SCHED_RESET_ON_FORK, &params);
    if(policy < 0)
    {
        perror("setup");
        return;
    }
    assignCPU(pid, 0);

    kmsg = fopen("/dev/kmsg", "w");
    if(!kmsg)
    {
        perror("setup");
        return;
    }
}

// "schedule" a process to be ran
void procSchedule(procinfo_t* procinfo)
{
    struct sched_param params;
    params.sched_priority = 0;
    int policy = sched_setscheduler(procinfo->pid, SCHED_OTHER, &params);
    if(policy < 0)
    {
        perror("procSchedule");
        exit(1);
    }
}

// "preempt" a process
void procPreempt(procinfo_t* procinfo)
{
    struct sched_param params;
    params.sched_priority = 0;
    int policy = sched_setscheduler(procinfo->pid, SCHED_IDLE, &params);
    if(policy < 0)
    {
        perror("procPreempt");
        exit(1);
    }
}

// Creates a process but doesn't start it.
// Child process will never return.
void procCreate(procinfo_t* procinfo)
{
    pid_t pid = fork();
    if(pid == -1)
    {
        perror("procCreate");
        exit(1);
    }
    else if(pid == 0)
    {
        struct timespec start = now();

        pid_t child = getpid();
        printf("%s %d\n", procinfo->name, child);
        while(procinfo->exec > 0)
        {
            doUnitTime();
            procinfo->exec--;
        }

        struct timespec end = now();
        fprintf(kmsg, "[Project1] %d %lld.%ld %lld.%ld\n",
                child, (long long)start.tv_sec, start.tv_nsec, (long long)end.tv_sec, end.tv_nsec);
        exit(0);
    }
    else
    {
        procinfo->pid = pid;
        assignCPU(pid, 1);
    }
}

int readycmp(const void* x, const void* y)
{
    return ((procinfo_t*)x)->ready - ((procinfo_t*)y)->ready;
}

// This hurts my soul btw. All. Of. This.
// To whomever thinks C is great, just read this.
typedef struct {
    procinfo_t** data;
    int begin, end, cap;
} queue_t;

queue_t queueMake(int cap)
{
    return queue_t{
        (procinfo_t**)malloc(cap * sizeof(procinfo_t*)),
        0, 0, cap,
    };
}

procinfo_t* queuePop(queue_t* queue)
{
    procinfo_t* ret = queue->data[queue->begin];
    queue->begin = (queue->begin + 1) % queue->cap;
    return ret;
}

void queuePush(queue_t* queue, procinfo_t* procinfo)
{
    queue->data[queue->end] = procinfo;
    queue->end = (queue->end + 1) % queue->cap;
}

int queueSize(const queue_t* queue)
{
    return (queue->end - queue->begin + queue->cap) % queue->cap;
}

typedef struct {
    procinfo_t** data;
    int sz, cap;
} heap_t;

heap_t heapMake(int cap)
{
    return heap_t{
        (procinfo_t**)malloc(cap * sizeof(procinfo_t*)),
        0, cap,
    };
}

void heapUp(heap_t* heap, int i)
{
    // TODO: check data race
    while(true)
    {
        int parent = (i - 1) >> 1;
        if(i == 0 || heap->data[i]->exec >= heap->data[parent]->exec)
            break;

        procinfo_t* tmp = heap->data[i];
        heap->data[i] = heap->data[parent];
        heap->data[parent] = tmp;
        i = parent;
    }
}

void heapDown(heap_t* heap, int i)
{
    while(true)
    {
        int lchild = i * 2 + 1, rchild = i * 2 + 2;
        int min = i;
        if(lchild < heap->sz && heap->data[min]->exec > heap->data[lchild]->exec)
            min = lchild;
        if(rchild < heap->sz && heap->data[min]->exec > heap->data[rchild]->exec)
            min = rchild;
        if(min == i)
            break;

        procinfo_t* tmp = heap->data[i];
        heap->data[i] = heap->data[min];
        heap->data[min] = tmp;
        i = min;
    }
}

procinfo_t* heapPop(heap_t* heap)
{
    procinfo_t* ret = heap->data[0];
    heap->data[0] = heap->data[heap->sz];
    heap->sz--;
    heapDown(heap, 0);
    return ret;
}

void heapPush(heap_t* heap, procinfo_t* procinfo)
{
    heap->data[heap->sz++] = procinfo;
    heapUp(heap, heap->sz);
}

int main()
{
    setup();

    char policy[5];
    int processCount;
    if(scanf("%4s\n%d", policy, &processCount) != 2)
    {
        printf("invalid input format\n");
        return 1;
    }

    procinfo_t* procinfos = (procinfo_t*)malloc((processCount + 1) * sizeof(procinfo_t));
    for(int i = 0; i < processCount; i++)
    {
        if(scanf("%32s %d %d", procinfos[i].name, &procinfos[i].ready,
                 &procinfos[i].exec) != 3)
         {
             printf("invalid input format\n");
             return 1;
         }
        procinfos[i].pid = -1;
    }
    procinfos[processCount].ready = -1;    // sentinel

    // Q: why don't you refactor these, they look so similar?
    // A: stfu, refactoring these is excrutiating wihtout closures
    // in fact, an explicit decision was made to copy paste
    if(!strcmp(policy, "FIFO"))
    {
        qsort(procinfos, processCount, sizeof(procinfo_t), readycmp);
        int pushed = 0;

        procinfo_t* runningproc = NULL;
        int time = 0, ran = 0;

        // time loop, each iteration moves forward by one unit of time
        while(true)
        {
            if(ran == processCount)
                break;

            while(procinfos[pushed].ready == time)
            {
                procCreate(&procinfos[pushed]);
                pushed++;
            }

            if(!runningproc && ran < pushed)
            {
                runningproc = &procinfos[ran++];
                procSchedule(runningproc);
            }

            // clear exited process
            if(runningproc)
            {
                int status;
                waitpid(runningproc->pid, &status,  WNOHANG);
                if(WIFEXITED(status))
                    runningproc = NULL;
            }

            time++;
            doUnitTime();
        }
    }
    else if(!strcmp(policy, "RR"))
    {
        qsort(procinfos, processCount, sizeof(procinfo_t), readycmp);
        int pushed = 0;

        queue_t queue = queueMake(processCount);
        procinfo_t* runningproc = NULL;
        int time = 0, slice = 0;

        // time loop, each iteration moves forward by one unit of time
        while(true)
        {
            if(queueSize(&queue) == 0 && pushed == processCount)
                break;

            while(procinfos[pushed].ready == time)
            {
                procCreate(&procinfos[pushed]);
                queuePush(&queue, &procinfos[pushed]);
                pushed++;
            }

            // time slice expired
            if(slice == 0)
            {
                if(runningproc)
                {
                    procPreempt(runningproc);
                    queuePush(&queue, runningproc);
                }

                if(queueSize(&queue) > 0)
                {
                    runningproc = queuePop(&queue);
                    procSchedule(runningproc);
                    slice = 500;
                }
            }

            // clear exited process
            if(runningproc)
            {
                int status;
                waitpid(runningproc->pid, &status,  WNOHANG);
                if(WIFEXITED(status))
                    runningproc = NULL;
            }

            time++;
            slice--;
            doUnitTime();
        }

        free(queue.data);
    }
    else if(!strcmp(policy, "SJF"))
    {
        qsort(procinfos, processCount, sizeof(procinfo_t), readycmp);
        int pushed = 0;

        heap_t heap = heapMake(processCount);
        procinfo_t* runningproc = NULL;
        int time = 0;

        while(true)
        {
            if(heap.sz == 0 && pushed == processCount)
                break;

            while(procinfos[pushed].ready == time)
            {
                procCreate(&procinfos[pushed]);
                heapPush(&heap, &procinfos[pushed]);
                pushed++;
            }

            if(!runningproc && heap.sz > 0)
            {
                runningproc = heapPop(&heap);
                procSchedule(runningproc);
            }

            // clear exited process
            if(runningproc)
            {
                int status;
                waitpid(runningproc->pid, &status,  WNOHANG);
                if(WIFEXITED(status))
                    runningproc = NULL;
            }

            time++;
            doUnitTime();
        }

        free(heap.data);
    }
    else if(!strcmp(policy, "PSJF"))
    {
        qsort(procinfos, processCount, sizeof(procinfo_t), readycmp);
        int pushed = 0;

        heap_t heap = heapMake(processCount);
        procinfo_t* runningproc = NULL;
        int time = 0;

        while(true)
        {
            if(heap.sz == 0 && pushed == processCount)
                break;

            while(procinfos[pushed].ready == time)
            {
                procCreate(&procinfos[pushed]);
                heapPush(&heap, &procinfos[pushed]);
                pushed++;
            }


            if(heap.sz > 0)
            {
                if(!runningproc)
                {
                    runningproc = heapPop(&heap);
                    procSchedule(runningproc);
                }
                // TODO: check data race
                else if(heap.data[0]->exec < runningproc->exec)
                {
                    heapPush(&heap, runningproc);
                    procPreempt(runningproc);

                    runningproc = heapPop(&heap);
                    procSchedule(runningproc);
                }
            }

            // clear exited process
            if(runningproc)
            {
                int status;
                waitpid(runningproc->pid, &status,  WNOHANG);
                if(WIFEXITED(status))
                    runningproc = NULL;
            }

            time++;
            doUnitTime();
        }

        free(heap.data);
    }
    else
    {
        printf("Invalid policy %s\n", policy);
        return 1;
    }

    free(procinfos);
    fclose(kmsg);
    return 0;
}
