#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define MAX_ROWS 700
#define MAX_COLS 100
#define MAX_CELL_LENGTH 10

#define ARRAY_SIZE MAX_ROWS *MAX_COLS
#define NUM_THREADS 5
#define BUFFER_SIZE 2

pthread_mutex_t flag_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int flag = 0;


double data[ARRAY_SIZE] = {0};
double updateddata[ARRAY_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
double totalNetChange = 0;


//Read data from CSV file
void generatedata()
{
    
    FILE *file = fopen("data.csv", "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error opening file\n");
        return;
    }

    char line[MAX_COLS * (MAX_CELL_LENGTH + 1)];
    int index = 0;

    while (fgets(line, sizeof(line), file) && index < MAX_ROWS * MAX_COLS)
    {
        char *token = strtok(line, ",");
        while (token != NULL && index < MAX_ROWS * MAX_COLS)
        {
            data[index++] = atoi(token);
            token = strtok(NULL, ",");
        }
    }

    fclose(file);
}


//Simulation of Data Parallelism
void *updateData(void *arg)
{
    int thread_id = *((int *)arg);
    int start = thread_id * (ARRAY_SIZE / NUM_THREADS);
    int end = start + (ARRAY_SIZE / NUM_THREADS);

    for (int i = start; i < end; i++)
    {
        updateddata[i] = 200;
        totalNetChange += updateddata[i] - data[i]; // Race condition Occured
    }

    return NULL;
}


//Handeling Race condition problem caused by data parallelism using Mutex
void *mutexCS(void *arg)
{
    int thread_id = *((int *)arg);
    int start = thread_id * (ARRAY_SIZE / NUM_THREADS);
    int end = start + (ARRAY_SIZE / NUM_THREADS);

    for (int i = start; i < end; i++)
    {
        updateddata[i] = 200;
        pthread_mutex_lock(&mutex);
        totalNetChange += updateddata[i] - data[i];
        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

sem_t me, empty, full;

void sigint_handler(int sig)
{
     sem_post(&full); // Signal consumer to wake up
    pthread_mutex_lock(&flag_mutex);
    flag = 1;
    pthread_mutex_unlock(&flag_mutex);
}



int buffer[BUFFER_SIZE]; 
int in = 0, out = 0;



void *producer(void *arg)
{
    int val;
    while (1)
    {
        pthread_mutex_lock(&flag_mutex);
        if (flag)
        {
            pthread_mutex_unlock(&flag_mutex);
            break;
        }
        pthread_mutex_unlock(&flag_mutex);

        val = rand() % 10;

        sem_wait(&empty);
        sem_wait(&me);

        buffer[in] = val;
        printf("Produced: %d\n", val);
        in = (in + 1) % BUFFER_SIZE;

        sem_post(&me);
        sem_post(&full);

        sleep(rand() % 2);
    }
}

void *consumer(void *arg)
{
    int val;
    double mean, sum = 0;
    int count=0;
    while (1)
    {
        pthread_mutex_lock(&flag_mutex);
        if (flag)
        {
            pthread_mutex_unlock(&flag_mutex);
            break;
        }
        pthread_mutex_unlock(&flag_mutex);

        if (sem_trywait(&full) != 0)
        {
            continue;
        }

        sem_wait(&me);

        val = buffer[out];
        printf("Consumed: %d\n", val);
        sum += val;
        count++;
        out = (out + 1) % BUFFER_SIZE;

        sem_post(&me);
        sem_post(&empty);

        sleep(rand() % 2);
    }

    mean = sum / count;
    printf("Mean: %.2lf\n", mean);
}

#define NUM_READERS 5
#define NUM_WRITERS 2
#define STRING_LENGTH 60
pthread_t readers[NUM_READERS], writers[NUM_WRITERS];
sem_t mut, rw_mutex;
int readers_count = 0;
int sharedArray[ARRAY_SIZE];

void *reader(void *arg)
{
    while (!flag)
    {
        sem_wait(&mut);
        readers_count++;
        if (readers_count == 1)
        {
            sem_wait(&rw_mutex);
        }
        sem_post(&mut);
        
        int result;
        if ((long)arg == 0)
        {
            result = sharedArray[0] + sharedArray[1];
        }
        else if ((long)arg == 1)
        {
            result = sharedArray[0] - sharedArray[1];
        }
        else if ((long)arg == 2)
        {
            result = sharedArray[0] * sharedArray[1];
        }
        else if ((long)arg == 3)
        {
            if (sharedArray[1] != 0)
            {
                result = sharedArray[0] / sharedArray[1];
            }
            else
            {
                result = 0;
            }
        }

        printf("Reader %ld: %d\n", (long)arg, result);

        sem_wait(&mut);
        readers_count--;
        if (readers_count == 0)
        {
            sem_post(&rw_mutex);
        }
        sem_post(&mut);

        usleep(1000000);
    }
    pthread_exit(NULL);
}

void *writer(void *arg)
{
    while (!flag)
    {
        sem_wait(&rw_mutex);

        sharedArray[0] = rand() % 100;
        sharedArray[1] = rand() % 100;

        printf("Writer %ld: %d, %d\n", (long)arg, sharedArray[0], sharedArray[1]);

        sem_post(&rw_mutex);
        usleep(1000000);
    }
    pthread_exit(NULL);
}

#define NUM_PHILOSOPHERS 4
#define DEADLOCK_TIMEOUT 5

pthread_mutex_t forks[NUM_PHILOSOPHERS];
pthread_cond_t deadlock_detected = PTHREAD_COND_INITIALIZER;
int deadlock_count = 0;

void *philosopher_deadlock(void *arg)
{
    int id = *((int *)arg);
    int left_fork = id;
    int right_fork = (id + 1) % NUM_PHILOSOPHERS;

    while (1)
    {
        pthread_mutex_lock(&forks[left_fork]);
        printf("Philosopher %d picked up left fork %d\n", id, left_fork);
        
        if (pthread_mutex_trylock(&forks[right_fork]) == 0)
        {
            printf("Philosopher %d picked up right fork %d\n", id, right_fork);
            printf("Philosopher %d is eating\n", id);
            pthread_mutex_unlock(&forks[right_fork]);
            printf("Philosopher %d put down right fork %d\n", id, right_fork);
        }
        else
        {
            deadlock_count++;
            printf("Deadlock detected! Count: %d\n", deadlock_count);

            while (1)
            {
                if (pthread_mutex_trylock(&forks[right_fork]) == 0)
                {
                    deadlock_count--;
                    printf("Deadlock released! Count: %d\n", deadlock_count);
                    printf("Philosopher %d picked up right fork %d\n", id, right_fork);
                    printf("Philosopher %d is eating\n", id);

                    pthread_mutex_unlock(&forks[right_fork]);
                    printf("Philosopher %d put down right fork %d\n", id, right_fork);
                    break;
                }

                else
                {
                    if(deadlock_count == NUM_PHILOSOPHERS)
                    {
                        printf("All philosophers deadlocked, waiting %d seconds\n", DEADLOCK_TIMEOUT);
                        sleep(DEADLOCK_TIMEOUT);
                        return NULL;
                    }
                }
            }
        }

        pthread_mutex_unlock(&forks[left_fork]);
        printf("Philosopher %d put down left fork %d\n", id, left_fork);
        
        printf("Philosopher %d is thinking\n", id);
        sleep(rand() % 1 + 1);
    }
}



void *philosopher_odd(void *arg)
{
    int id = *((int *)arg);
    int left_fork = id;
    int right_fork = (id + 1) % NUM_PHILOSOPHERS;

    while (!flag)
    {
        pthread_mutex_lock(&forks[left_fork]);
        printf("Philosopher %d picked up left fork %d\n", id, left_fork);

        pthread_mutex_lock(&forks[right_fork]);
        printf("Philosopher %d picked up right fork %d\n", id, right_fork);
        printf("Philosopher %d is eating\n", id);
        pthread_mutex_unlock(&forks[right_fork]);
        printf("Philosopher %d put down right fork %d\n", id, right_fork);

        pthread_mutex_unlock(&forks[left_fork]);
        printf("Philosopher %d put down left fork %d\n", id, left_fork);
 
        printf("Philosopher %d is thinking\n", id);
        sleep(rand() % 1 + 1);
    }
}

void *philosopher_even(void *arg)
{
    int id = *((int *)arg);
    int left_fork = id;
    int right_fork = (id + 1) % NUM_PHILOSOPHERS;

    while (!flag)
    {
        pthread_mutex_lock(&forks[right_fork]);
        printf("Philosopher %d picked up right fork %d\n", id, right_fork);

        pthread_mutex_lock(&forks[left_fork]);
        printf("Philosopher %d picked up right fork %d\n", id, left_fork);
        printf("Philosopher %d is eating\n", id);

        pthread_mutex_unlock(&forks[left_fork]);
        printf("Philosopher %d put down left fork %d\n", id, left_fork);

        pthread_mutex_unlock(&forks[right_fork]);
        printf("Philosopher %d put down right fork %d\n", id, right_fork);

        printf("Philosopher %d is thinking\n", id);
        sleep(rand() % 1 + 1);
    }
}

void synchronizationMenu()
{
    int choice;

    printf("Synchronization Options:\n");
    printf("1. Critical Section (Basic)\n");
    printf("2. Bounded Buffer\n");
    printf("3. Reader-Writer\n");
    printf("4. Dining Philosopher\n");
    printf("Enter your choice: ");
    scanf("%d", &choice);
    switch (choice)
    {
    case 1:
        printf("You chose Critical Section (Basic)\n");
        pthread_t threads[NUM_THREADS];
        int thread_args[NUM_THREADS];

        // PRODUCER producing DATA
        /*for (int i = 0; i < NUM_THREADS; i++)
        {
            thread_args[i] = i;
            pthread_create(&threads[i], NULL, generatedata, (void *)&thread_args[i]);
        }*/

        generatedata();

        /*for (int i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], NULL);
        }*/

        totalNetChange = 0;

        for (int i = 0; i < NUM_THREADS; i++)
        {
            thread_args[i] = i;
            pthread_create(&threads[i], NULL, updateData, (void *)&thread_args[i]);
        }

        for (int i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], NULL);
        }
        printf("Expected Value: %d\n", ARRAY_SIZE * 100);
        printf("Without Mutex Lock:\n");
        printf("Net Change: %.2lf\n", totalNetChange);

        totalNetChange = 0;

        for (int i = 0; i < NUM_THREADS; i++)
        {
            thread_args[i] = i;
            pthread_create(&threads[i], NULL, mutexCS, (void *)&thread_args[i]);
        }

        for (int i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], NULL);
        }
        printf("With Mutex Lock:\n");
        printf("Net Change: %0.2lf\n", totalNetChange);

        break;

    case 2:
        printf("You chose Bounded Buffer\n");
        flag = 0;
        pthread_t producer_thread, consumer_thread;
        sem_init(&me, 0, 1);
        sem_init(&empty, 0, BUFFER_SIZE);
        sem_init(&full, 0, 0);
        pthread_create(&producer_thread, NULL, producer, NULL);
        pthread_create(&consumer_thread, NULL, consumer, NULL);
        pthread_join(producer_thread, NULL);
        pthread_join(consumer_thread, NULL);
        sem_destroy(&me);
        sem_destroy(&empty);
        sem_destroy(&full);
        break;
    case 3:
        printf("You chose Reader-Writer\n");
        flag = 0;
        pthread_t readers[NUM_READERS], writers[NUM_WRITERS];

        sem_init(&rw_mutex, 0, 1);
        sem_init(&mut, 0, 1);

        for (long i = 0; i < NUM_READERS; i++)
        {
            pthread_create(&readers[i], NULL, reader, (void *)i);
        }

        for (long i = 0; i < NUM_WRITERS; i++)
        {
            pthread_create(&writers[i], NULL, writer, (void *)i);
        }

        for (int i = 0; i < NUM_READERS; i++)
        {
            pthread_join(readers[i], NULL);
        }

        for (int i = 0; i < NUM_WRITERS; i++)
        {
            pthread_join(writers[i], NULL);
        }

        sem_destroy(&rw_mutex);
        sem_destroy(&mut);

        break;

    case 4:
        flag = 0;
        pthread_t philosophers[NUM_PHILOSOPHERS];
        int ids[NUM_PHILOSOPHERS];

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            pthread_mutex_init(&forks[i], NULL);
        }

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            ids[i] = i;
            pthread_create(&philosophers[i], NULL, philosopher_deadlock, &ids[i]);
        }

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            pthread_join(philosophers[i], NULL);
        }

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            pthread_mutex_destroy(&forks[i]);
        }

        flag = 0;
        pthread_t philosophers1[NUM_PHILOSOPHERS];

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            pthread_mutex_init(&forks[i], NULL);
        }

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            ids[i] = i;

            if (i % 2 == 0)
            {
                pthread_create(&philosophers1[i], NULL, philosopher_even, &ids[i]);
            }
            else
            {
                pthread_create(&philosophers1[i], NULL, philosopher_odd, &ids[i]);
            }
        }

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            pthread_join(philosophers1[i], NULL);
        }

        for (int i = 0; i < NUM_PHILOSOPHERS; i++)
        {
            pthread_mutex_destroy(&forks[i]);
        }

        break;
    default:
        printf("Invalid choice\n");
    }
}

struct procData
{
    int processNo;
    int burstTime;
    int priority;
    int arrivalTime;
    int exitTime;
    int waitingTime;
    int turnAroundTime;
    int remainingBT;
};

void swap(struct procData *x, struct procData *y)
{
    struct procData temp = *x;
    *x = *y;
    *y = temp;
}

void bubbleSort(struct procData process[], int n)
{
    for (int i = 0; i < n - 1; i++)
    {
        for (int j = 0; j < n - i - 1; j++)
        {
            if (process[j].arrivalTime > process[j + 1].arrivalTime)
            {
                swap(&process[j], &process[j + 1]);
            }
        }
    }
}

void prioritySort(struct procData process[], int n)
{
    for (int i = 0; i < n - 1; i++)
    {
        for (int j = 0; j < n - i - 1; j++)
        {
            if (process[j].priority < process[j + 1].priority)
            {
                swap(&process[j], &process[j + 1]);
            }
        }
    }
}

void FCFS(struct procData process[], int n)
{
    bubbleSort(process, n);

    int time = process[0].arrivalTime;
    double sumWaiting = 0, sumTurnAround = 0;

    for (int i = 0; i < n; i++)
    {

        if (time < process[i].arrivalTime)
        {
            printf("\nCPU waits for %d seconds", process[i].arrivalTime - time);
            time = process[i].arrivalTime;
        }

        process[i].waitingTime = time - process[i].arrivalTime;
        printf("\nProcess %d running for %d seconds", process[i].processNo, process[i].burstTime);
        process[i].turnAroundTime = process[i].burstTime + process[i].waitingTime;
        time += process[i].burstTime;
        printf("\nProcess %d finishes at %d seconds\n", process[i].processNo, time);
        sumWaiting += process[i].waitingTime;
        sumTurnAround += process[i].turnAroundTime;
    }

    // printf("\n\nProcess %d finishes at %d seconds",i,time);
    printf("\nAverage Waiting Time: %.2lf seconds", sumWaiting / n);
    printf("\nAverage Turn Around Time:  %.2lf seconds", sumTurnAround / n);
}

int min(int a, int b)
{
    return (a < b) ? a : b;
}

void roundRobin(struct procData process[], int n, int timeQuantum)
{
    // bubbleSort(process,n);
    int remainingBurstTime[n];
    int currentTime = 0;

    for (int i = 0; i < n; ++i)
    {
        remainingBurstTime[i] = process[i].burstTime;
    }

    while (true)
    {
        bool allProcessesFinished = true;
        int count = 1;

        for (int i = 0; i < n; i++)
        {

            if (remainingBurstTime[i] > 0)
            {
                allProcessesFinished = false;

                if (currentTime >= process[i].arrivalTime)
                {
                    count = 0;

                    int executeTime = min(timeQuantum, remainingBurstTime[i]);
                    currentTime += executeTime;
                    remainingBurstTime[i] -= executeTime;

                    printf("\nProcess %d running for %d seconds", process[i].processNo, executeTime);

                    if (remainingBurstTime[i] == 0)
                    {
                        process[i].exitTime = currentTime;
                    }
                }
            }
        }

        if (count == 1 && !allProcessesFinished)
        {
            printf("CPU waits for 1 second\n");
            currentTime++;
        }
        if (allProcessesFinished)
        {
            break;
        }
    }

    double sumWaiting = 0, sumTurnAround = 0;
    for (int i = 0; i < n; ++i)
    {
        process[i].turnAroundTime = process[i].exitTime - process[i].arrivalTime;
        process[i].waitingTime = process[i].turnAroundTime - process[i].burstTime;
        sumWaiting += process[i].waitingTime;
        sumTurnAround += process[i].turnAroundTime;
    }

    printf("\nAverage Waiting Time: %.2lf seconds", sumWaiting / n);
    printf("\nAverage Turnaround Time: %.2lf seconds\n", sumTurnAround / n);
}

void preemptivePriority(struct procData process[], int n, int timeQuantum)
{
    prioritySort(process, n);

    int remainingBurstTime[n];
    int currentTime = 0;
    int count = 1;
    for (int i = 0; i < n; ++i)
    {
        remainingBurstTime[i] = process[i].burstTime;
    }

    while (true)
    {
        bool allProcessesFinished = true;
        count = 1;
        for (int i = 0; i < n; i++)
        {
            if (remainingBurstTime[i] > 0)
            {
                // printf("1");
                // printf("\nCurrent Time: %d",currentTime);
                // printf("\nprocess %d Arrival Time: %d",process[i].processNo,process[i].arrivalTime);
                allProcessesFinished = false;

                if (currentTime >= process[i].arrivalTime)
                {
                    // printf("2");
                    count = 0;
                    int executeTime = min(timeQuantum, remainingBurstTime[i]);
                    currentTime += executeTime;
                    remainingBurstTime[i] -= executeTime;

                    printf("\nProcess %d running for %d seconds", process[i].processNo, executeTime);

                    if (remainingBurstTime[i] == 0)
                    {
                        process[i].exitTime = currentTime;
                    }

                    break;
                }
            }
        }

        if (count == 1 && !allProcessesFinished)
        {
            printf("\nCPU waits for 1 second");
            currentTime++;
        }

        if (allProcessesFinished)
        {
            break;
        }
    }

    double sumWaiting = 0, sumTurnAround = 0;
    for (int i = 0; i < n; ++i)
    {
        process[i].turnAroundTime = process[i].exitTime - process[i].arrivalTime;
        process[i].waitingTime = process[i].turnAroundTime - process[i].burstTime;
        sumWaiting += process[i].waitingTime;
        sumTurnAround += process[i].turnAroundTime;
    }

    printf("\nAverage Waiting Time: %.2lf seconds", sumWaiting / n);
    printf("\nAverage Turnaround Time: %.2lf seconds\n", sumTurnAround / n);
}

void burstSort(struct procData process[], int n)
{
    for (int i = 0; i < n - 1; i++)
    {
        for (int j = 0; j < n - i - 1; j++)
        {
            if (process[j].remainingBT > process[j + 1].remainingBT)
            {
                swap(&process[j], &process[j + 1]);
            }
        }
    }
}

void SJF(struct procData process[], int n)
{
    burstSort(process, n);
    int currentTime = 0;
    // int remainingBurstTime[n];
    for (int i = 0; i < n; ++i)
    {
        process[i].remainingBT = process[i].burstTime;
    }

    int count = 1;
    double sumWaiting = 0, sumTurnAround = 0;
    while (true)
    {
        bool allProcessesFinished = true;
        for (int i = 0; i < n; i++)
        {
            if (process[i].remainingBT > 0)
            {
                allProcessesFinished = false;

                if (currentTime > process[i].arrivalTime)
                {
                    count = 0;

                    process[i].waitingTime = currentTime - process[i].arrivalTime;
                    sumWaiting += process[i].waitingTime;

                    printf("Process %d running for %d seconds\n", process[i].processNo, process[i].burstTime);
                    currentTime += process[i].burstTime;
                    process[i].remainingBT = 0;

                    process[i].turnAroundTime = currentTime - process[i].arrivalTime;
                    sumTurnAround += process[i].turnAroundTime;
                }
            }
        }

        if (count == 1 && !allProcessesFinished)
        {
            printf("CPU waits for 1 second\n");
            currentTime++;
        }

        if (allProcessesFinished)
        {
            break;
        }
    }
    printf("\nAverage Waiting Time: %.2lf seconds", sumWaiting / n);
    printf("\nAverage Turn Around Time: %.2lf seconds", sumTurnAround / n);
}

void SRTF(struct procData process[], int n, int timeQuantum)
{

    // int remainingBurstTime[n];
    int currentTime = 0;
    int count = 1;
    for (int i = 0; i < n; ++i)
    {
        process[i].remainingBT = process[i].burstTime;
    }

    while (true)
    {
        bool allProcessesFinished = true;
        burstSort(process, n);
        for (int i = 0; i < n; i++)
        {
            if (process[i].remainingBT > 0)
            {
                allProcessesFinished = false;
                count = 1;

                if (currentTime >= process[i].arrivalTime)
                {
                    count = 0;
                    int executeTime = min(timeQuantum, process[i].remainingBT);
                    currentTime += executeTime;
                    process[i].remainingBT -= executeTime;

                    printf("\nProcess %d running for %d seconds", process[i].processNo, executeTime);

                    if (process[i].remainingBT == 0)
                    {
                        process[i].exitTime = currentTime;
                    }

                    break;
                }
            }
        }

        if (count == 1 && !allProcessesFinished)
        {
            printf("CPU waits for 1 second\n");
            currentTime++;
        }

        if (allProcessesFinished)
        {
            break;
        }
    }

    double sumWaiting = 0, sumTurnAround = 0;
    for (int i = 0; i < n; ++i)
    {
        process[i].turnAroundTime = process[i].exitTime - process[i].arrivalTime;
        process[i].waitingTime = process[i].turnAroundTime - process[i].burstTime;
        sumWaiting += process[i].waitingTime;
        sumTurnAround += process[i].turnAroundTime;
    }

    printf("\nAverage Waiting Time: %.2lf seconds", sumWaiting / n);
    printf("\nAverage Turnaround Time: %.2lf seconds\n", sumTurnAround / n);
}

void schedulingMenu()
{

    int proc;
    printf("Enter number of processes: ");
    scanf("%d", &proc);

    struct procData process[proc];

    for (int i = 0; i < proc; i++)
    {
        process[i].processNo = i;
        printf("Enter Burst Time of Process %d: ", i);
        scanf("%d", &process[i].burstTime);

        printf("Enter priority of Process %d: ", i);
        scanf("%d", &process[i].priority);

        printf("Enter Arrival Time of Process %d: ", i);
        scanf("%d", &process[i].arrivalTime);
    }

    int choice;
    int timeQuantum;
    printf("Scheduling Options:\n");
    printf("1. Round Robin\n");
    printf("2. FCFS\n");
    printf("3. Priority Scheduling\n");
    printf("4. Shortest Job First\n");
    printf("5. Shortest Remaining Time First\n");
    printf("Enter your choice: ");
    scanf("%d", &choice);
    switch (choice)
    {
    case 1:
        printf("You chose Round Robin\n");

        printf("Enter Time Quantum: ");
        scanf("%d", &timeQuantum);
        roundRobin(process, proc, timeQuantum);
        break;
    case 2:
        printf("You chose FCFS\n");

        FCFS(process, proc);

        break;
    case 3:
        printf("You chose Priority Scheduling\n");
        printf("Enter Time Quantum: ");
        scanf("%d", &timeQuantum);
        preemptivePriority(process, proc, timeQuantum);

        break;

    case 4:
        printf("You chose Shortest Job Frst\n");
        SJF(process, proc);
        break;

    case 5:
        printf("You chose Shortest Remaining Time First\n");
        printf("Enter Time Quantum: ");
        scanf("%d", &timeQuantum);
        SRTF(process, proc, timeQuantum);

        break;
    default:
        printf("Invalid choice\n");
    }
}

int main()
{
    srand(time(NULL));
    signal(SIGINT, sigint_handler);
    int choice;
    do
    {
        printf("\nMain Menu:\n");
        printf("1. Synchronization\n");
        printf("2. Scheduling\n");
        printf("3. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
        switch (choice)
        {
        case 1:
            synchronizationMenu();
            break;
        case 2:
            schedulingMenu();
            break;
        case 3:
            printf("Exiting...\n");
            break;
        default:
            printf("Invalid choice\n");
        }
    } while (choice != 3);
    return 0;
}
