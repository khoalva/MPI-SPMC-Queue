#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include "spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Demonstration of nbdFFQ SPMC queue.
 * Each enqueue/dequeue transfers a full batch of BATCH_SIZE items.
 */
int main(int argc, char *argv[]) {
    spmc_queue_t queue;

    MPI_TRY(spmc_queue_init(&queue, argc, argv));

    mpi_print_info(&queue.mpi_ctx);
    spmc_queue_print_stats(&queue);

    printf("\n=== Concurrent Producer/Consumer Operation (nbdFFQ, batch=%d) ===\n",
           BATCH_SIZE);

    if (spmc_queue_is_enqueuer(&queue)) {
        printf("Rank %d: Starting as PRODUCER\n", mpi_get_rank(&queue.mpi_ctx));

        int num_batches = 10;
        int batch[BATCH_SIZE];

        for (int b = 0; b < num_batches; b++) {
            for (int i = 0; i < BATCH_SIZE; i++)
                batch[i] = b * BATCH_SIZE + i;

            MPI_TRY(spmc_queue_enqueue_batch(&queue, batch, BATCH_SIZE));
            printf("Rank %d: Enqueued batch #%d [%d..%d]\n",
                   mpi_get_rank(&queue.mpi_ctx), b,
                   batch[0], batch[BATCH_SIZE - 1]);
            usleep(25000);
        }

        printf("Rank %d: Producer done (%d batches)\n",
               mpi_get_rank(&queue.mpi_ctx), num_batches);

    } else {
        printf("Rank %d: Starting as CONSUMER\n", mpi_get_rank(&queue.mpi_ctx));

        int batch_size    = spmc_queue_get_deq_batch_size(&queue);
        int *buffer       = malloc(batch_size * sizeof(int));
        int items_consumed = 0;
        int max_attempts  = 50;

        if (!buffer) {
            fprintf(stderr, "Rank %d: malloc failed\n", mpi_get_rank(&queue.mpi_ctx));
            spmc_queue_destroy(&queue);
            return 1;
        }

        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int count = spmc_queue_dequeue(&queue, buffer, batch_size);
            if (count > 0) {
                items_consumed += count;
                printf("Rank %d: Dequeued %d items: [%d..%d]\n",
                       mpi_get_rank(&queue.mpi_ctx), count,
                       buffer[0], buffer[count - 1]);
                usleep(40000);
            } else {
                usleep(20000);
            }
        }

        free(buffer);
        printf("Rank %d: Consumer done, consumed %d items\n",
               mpi_get_rank(&queue.mpi_ctx), items_consumed);
    }

    printf("Rank %d: Waiting for all processes...\n", mpi_get_rank(&queue.mpi_ctx));
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));

    if (spmc_queue_is_enqueuer(&queue)) {
        printf("\n=== Final Statistics ===\n");
        spmc_queue_print_stats(&queue);
        printf("nbdFFQ demonstration completed successfully!\n");
    }

    spmc_queue_destroy(&queue);
    return 0;
}
