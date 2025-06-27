#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

#define MAX_ROWS 1000
#define MAX_COLS 1000
#define EMPTY -1
#define TAKEN -2
#define N_DEQUEUERS 4

typedef struct {
    atomic_int value;
} SwapObject;

typedef struct {
    atomic_int counter;
} FetchAndIncrement;

// Global shared data
SwapObject ITEMS[MAX_ROWS][MAX_COLS];
FetchAndIncrement HEAD[MAX_ROWS];
atomic_int ROW = 0;

// Enqueuer state
int tail = 0;
int enq_row = 0;

// Helper function for Swap (atomic exchange)
int swap(atomic_int *loc, int new_val) {
    return atomic_exchange(loc, new_val);
}

// Helper function for Fetch&Increment
int fetch_and_increment(atomic_int *counter) {
    return atomic_fetch_add(counter, 1);
}

// Enqueue operation (run by one thread)
void Enqueue(int x) {
    int val = swap(&ITEMS[enq_row][tail].value, x);
    if (val == TAKEN) {
        enq_row++;
        tail = 0;
        swap(&ITEMS[enq_row][tail].value, x);
        atomic_store(&ROW, enq_row);
    }
    tail++;
}

// Dequeue operation (run by many threads)
int Dequeue() {
    int deq_row = atomic_load(&ROW);
    int head = fetch_and_increment(&HEAD[deq_row].counter);
    int val = swap(&ITEMS[deq_row][head].value, TAKEN);
    if (val == EMPTY) {
        return -999;  // ε: nothing to return
    } else {
        return val;
    }
}

// Initialization
void init() {
    for (int i = 0; i < MAX_ROWS; ++i) {
        atomic_init(&HEAD[i].counter, 0);
        for (int j = 0; j < MAX_COLS; ++j) {
            atomic_init(&ITEMS[i][j].value, EMPTY);
        }
    }
    atomic_init(&ROW, 0);
}

// Example usage
void* dequeuer_thread(void* arg) {
    int id = *(int*)arg;
    for (int i = 0; i < 5; ++i) {
        int val = Dequeue();
        if (val != -999)
            printf("Dequeuer %d got value %d\n", id, val);
    }
    return NULL;
}

void* enqueuer_thread(void* arg) {
    for (int i = 1; i <= 10; ++i) {
        Enqueue(i);
        printf("Enqueued %d\n", i);
    }
    return NULL;
}

int main() {
    init();

    // Tạo enqueuer thread
    pthread_t enq_thread;
    pthread_create(&enq_thread, NULL, enqueuer_thread, NULL);

    // Tạo các dequeuer threads
    pthread_t threads[N_DEQUEUERS];
    int ids[N_DEQUEUERS];
    for (int i = 0; i < N_DEQUEUERS; ++i) {
        ids[i] = i + 1;
        pthread_create(&threads[i], NULL, dequeuer_thread, &ids[i]);
    }

    // Chờ thread enqueue xong
    pthread_join(enq_thread, NULL);

    // Chờ các thread dequeue xong
    for (int i = 0; i < N_DEQUEUERS; ++i) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}

