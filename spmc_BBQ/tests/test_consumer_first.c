#define _POSIX_C_SOURCE 199309L
#include "../src/spmc_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>

/**
 * Test Case: Consumer runs before Producer
 * 
 * Mục đích: Kiểm tra hành vi của queue khi consumer bắt đầu dequeue
 * trước khi producer enqueue bất kỳ giá trị nào.
 * 
 * Phát hiện quan trọng:
 * Test này đã phát hiện ra rằng MPI passive target RMA (MPI_Win_lock_all)
 * VẪN CẦN target process (producer) phải "make progress" để xử lý các
 * remote memory requests! Khi producer sleep() và không gọi MPI functions,
 * consumer sẽ BỊ BLOCK tại các MPI RMA operations (fetch_and_op, get, etc.)
 * cho đến khi producer thức dậy và gọi MPI functions.
 * 
 * Kịch bản:
 * 1. Consumer bắt đầu ngay lập tức và gọi dequeue
 * 2. Producer delay trước khi enqueue
 * 3. Consumer BỊ BLOCK tại MPI operations trong dequeue
 * 4. Producer thức dậy và bắt đầu enqueue -> MPI runtime xử lý pending requests
 * 5. Consumer nhận được kết quả từ dequeue
 * 
 * Kỳ vọng:
 * - Consumer BỊ BLOCK khi gọi dequeue trong khi producer không active
 * - Khi producer bắt đầu enqueue (gọi MPI operations), consumer được unblock
 * - Các giá trị được enqueue đều phải được dequeue (không mất dữ liệu)
 * - Thứ tự dequeue có thể không giống thứ tự enqueue do
 *   linearization point là lúc fetch-and-add head
 * 
 * Kết luận:
 * Trong SPMC queue với MPI RMA, producer PHẢI luôn "make progress"
 * (gọi MPI functions định kỳ) để consumer có thể dequeue!
 */
void test_consumer_runs_first(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("=== Test Case: Consumer Runs Before Producer ===\n");
    fflush(stdout);
    
    // KHÔNG dùng barrier ở đây vì mục đích là để consumer chạy trước!
    // Mỗi process bắt đầu ngay lập tức theo logic riêng của nó
    
    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    if (spmc_queue_is_enqueuer(queue)) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                        (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        printf("[%.3fs] Rank %d (PRODUCER): Starting with intentional delay\n", elapsed, rank);
        fflush(stdout);
        
        // Producer chờ NHƯNG phải "make progress" để MPI RMA hoạt động
        // KHÔNG dùng sleep() vì nó sẽ block consumer's MPI operations!
        printf("[%.3fs] Producer: Waiting 3 seconds (with MPI progress) before starting enqueue...\n", elapsed);
        fflush(stdout);
        
        // Busy-wait với MPI_Barrier để đảm bảo MPI runtime có thể xử lý RMA requests
        struct timespec wait_start;
        clock_gettime(CLOCK_MONOTONIC, &wait_start);
        while (1) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed_wait = (now.tv_sec - wait_start.tv_sec) + 
                                 (now.tv_nsec - wait_start.tv_nsec) / 1e9;
            if (elapsed_wait >= 3.0) break;
            
            // Call MPI function để cho MPI runtime xử lý pending RMA operations
            // Sử dụng MPI_Iprobe (non-blocking) để make progress mà không block
            int flag;
            MPI_Status status;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, queue->mpi_ctx.comm, &flag, &status);
            
            usleep(10000); // 10ms sleep giữa các lần probe
        }
        
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                  (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        printf("[%.3fs] Producer: Starting to enqueue values NOW\n", elapsed);
        fflush(stdout);
        
        // Enqueue một số lượng giá trị để test
        int num_values = 20;
        int successful_enqueues = 0;
        
        for (int i = 1; i <= num_values; i++) {
            int value = i * 100; // Giá trị dễ nhận dạng (100, 200, 300, ...)
            int result = spmc_queue_enqueue(queue, value);
            
            if (result == MPI_SUCCESS) {
                successful_enqueues++;
                printf("Producer: Enqueued value %d (total: %d/%d)\n", 
                       value, successful_enqueues, num_values);
            } else {
                printf("Producer: WARNING - Failed to enqueue value %d\n", value);
            }
            
            // Thêm delay nhỏ giữa các lần enqueue để test tính đúng đắn
            usleep(50000); // 50ms
        }
        
        printf("Producer: Finished enqueuing. Total successful: %d/%d\n", 
               successful_enqueues, num_values);
        
        // Chờ thêm để consumer có thời gian dequeue hết
        sleep(2);
        
    } else {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                        (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        printf("[%.3fs] Rank %d (CONSUMER %d): Starting immediately (before producer)\n", 
               elapsed, rank, rank - 1);
        fflush(stdout);
        
        // Phase 1: Consumer thử dequeue từ queue rỗng
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                  (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        printf("[%.3fs] Consumer %d: Phase 1 - Testing dequeue from empty queue\n", elapsed, rank - 1);
        fflush(stdout);
        int empty_dequeue_attempts = 10;
        int empty_count = 0;
        
        for (int i = 0; i < empty_dequeue_attempts; i++) {
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                      (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            printf("[%.3fs] Consumer %d: About to call dequeue (attempt %d)...\n", 
                   elapsed, rank - 1, i + 1);
            fflush(stdout);
            
            int value = spmc_queue_dequeue(queue);
            
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            elapsed = (current_time.tv_sec - start_time.tv_sec) + 
                      (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            printf("[%.3fs] Consumer %d: Dequeue returned: %d\n", 
                   elapsed, rank - 1, value);
            fflush(stdout);
            if (value == -1) {
                empty_count++;
                printf("Consumer %d: Empty queue (attempt %d/%d)\n", 
                       rank - 1, i + 1, empty_dequeue_attempts);
            } else {
                printf("Consumer %d: Unexpected value %d during empty phase!\n", 
                       rank - 1, value);
            }
            usleep(100000); // 100ms giữa các lần thử
        }
        
        printf("Consumer %d: Phase 1 complete - Got %d empty results as expected\n", 
               rank - 1, empty_count);
        
        // Phase 2: Consumer tiếp tục dequeue và nên bắt đầu nhận giá trị
        printf("Consumer %d: Phase 2 - Waiting for producer to start enqueuing\n", 
               rank - 1);
        
        int values_dequeued = 0;
        int consecutive_empty = 0;
        int max_consecutive_empty = 20; // Dừng sau 20 lần liên tiếp không có giá trị
        int max_total_attempts = 100;
        int dequeued_values[100]; // Lưu các giá trị đã dequeue để kiểm tra
        
        for (int attempt = 0; attempt < max_total_attempts; attempt++) {
            int value = spmc_queue_dequeue(queue);
            
            if (value != -1) {
                if (values_dequeued < 100) {
                    dequeued_values[values_dequeued] = value;
                }
                values_dequeued++;
                consecutive_empty = 0;
                
                // Chỉ log giá trị, không kiểm tra thứ tự vì thứ tự có thể không đảm bảo
                // do linearization point tại fetch-and-add head
                printf("Consumer %d: Dequeued value %d (total: %d)\n", 
                       rank - 1, value, values_dequeued);
                
            } else {
                consecutive_empty++;
                
                // Dừng nếu queue rỗng quá lâu (producer đã hoàn thành)
                if (consecutive_empty >= max_consecutive_empty) {
                    printf("Consumer %d: No more values after %d attempts, stopping\n", 
                           rank - 1, max_consecutive_empty);
                    break;
                }
                
                usleep(50000); // 50ms delay khi queue rỗng
            }
        }
        
        printf("Consumer %d: Phase 2 complete - Dequeued %d values\n", 
               rank - 1, values_dequeued);
        
        // Kiểm tra tính duy nhất của các giá trị (không có duplicate)
        int duplicates = 0;
        for (int i = 0; i < values_dequeued && i < 100; i++) {
            for (int j = i + 1; j < values_dequeued && j < 100; j++) {
                if (dequeued_values[i] == dequeued_values[j]) {
                    duplicates++;
                    printf("Consumer %d: WARNING - Duplicate value detected: %d\n", 
                           rank - 1, dequeued_values[i]);
                }
            }
        }
        
        // In ra kết quả tổng hợp
        printf("\n=== Consumer %d Summary ===\n", rank - 1);
        printf("  Empty dequeues (Phase 1): %d\n", empty_count);
        printf("  Values dequeued (Phase 2): %d\n", values_dequeued);
        printf("  Duplicate detections: %d\n", duplicates);
        if (values_dequeued > 0) {
            printf("  First value received: %d\n", dequeued_values[0]);
            printf("  Last value received: %d\n", dequeued_values[values_dequeued - 1]);
        }
        
        // Kiểm tra kết quả
        if (values_dequeued > 0 && duplicates == 0) {
            printf("  Result: PASS - Consumer successfully received values without duplicates\n");
        } else if (values_dequeued > 0 && duplicates > 0) {
            printf("  Result: PARTIAL - Consumer received values but found duplicates\n");
        } else {
            printf("  Result: FAIL - Consumer did not receive any values\n");
        }
    }
}

/**
 * Test Case: Multiple consumers start before producer
 * Test với nhiều consumer cùng chạy trước producer
 */
void test_multiple_consumers_first(spmc_queue_t *queue) {
    int rank = mpi_get_rank(&queue->mpi_ctx);
    int size = mpi_get_size(&queue->mpi_ctx);
    
    printf("\n=== Test Case: Multiple Consumers Before Producer ===\n");
    fflush(stdout);
    
    // KHÔNG dùng barrier - để consumers tự nhiên chạy trước producer
    
    if (spmc_queue_is_enqueuer(queue)) {
        printf("Rank %d (PRODUCER): Delay before enqueuing (multi-consumer test)\n", rank);
        
        sleep(4); // Delay dài hơn để tất cả consumer thử dequeue từ queue rỗng
        
        printf("Producer: Starting to enqueue for multiple consumers\n");
        int num_values = 30;
        
        for (int i = 1; i <= num_values; i++) {
            int value = 5000 + i; // Giá trị khác với test trước
            spmc_queue_enqueue(queue, value);
            printf("Producer: Enqueued %d\n", value);
            usleep(30000); // 30ms
        }
        
        printf("Producer: Finished enqueuing %d values\n", num_values);
        sleep(2);
        
    } else {
        printf("Rank %d (CONSUMER %d): Starting early in multi-consumer scenario\n", 
               rank, rank - 1);
        
        // Tất cả consumer đều thử dequeue ngay lập tức
        int values_found = 0;
        int empty_attempts = 0;
        int max_attempts = 80;
        
        for (int i = 0; i < max_attempts; i++) {
            int value = spmc_queue_dequeue(queue);
            
            if (value != -1) {
                values_found++;
                printf("Consumer %d: Got value %d (total: %d)\n", 
                       rank - 1, value, values_found);
            } else {
                empty_attempts++;
                if (empty_attempts > 30) {
                    printf("Consumer %d: Stopping after %d consecutive empty attempts\n", 
                           rank - 1, empty_attempts);
                    break;
                }
            }
            
            usleep(50000); // 50ms
        }
        
        printf("Consumer %d: Final count - %d values dequeued\n", 
               rank - 1, values_found);
    }
}

int main(int argc, char *argv[]) {
    spmc_queue_t queue;
    
    struct timespec init_start, init_end;
    clock_gettime(CLOCK_MONOTONIC, &init_start);
    
    // Initialize queue
    if (spmc_queue_init(&queue, argc, argv) != MPI_SUCCESS) {
        fprintf(stderr, "Failed to initialize SPMC queue\n");
        return -1;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &init_end);
    double init_time = (init_end.tv_sec - init_start.tv_sec) + 
                      (init_end.tv_nsec - init_start.tv_nsec) / 1e9;
    
    int rank = mpi_get_rank(&queue.mpi_ctx);
    int size = mpi_get_size(&queue.mpi_ctx);
    
    printf("[Rank %d] spmc_queue_init took %.3f seconds\n", rank, init_time);
    fflush(stdout);
    
    printf("\n");
    fflush(stdout);
    printf("========================================\n");
    fflush(stdout);
    printf("  Consumer-First Test Suite\n");
    fflush(stdout);
    printf("  Rank %d/%d\n", rank, size);
    fflush(stdout);
    printf("========================================\n");
    fflush(stdout);
    printf("\n");
    fflush(stdout);
    
    if (size < 2) {
        fprintf(stderr, "ERROR: This test requires at least 2 processes (1 producer, 1+ consumer)\n");
        spmc_queue_destroy(&queue);
        return -1;
    }
    
    // Test 1: Single consumer runs before producer
    printf("\n>>> Running Test 1: Single Consumer First <<<\n\n");
    fflush(stdout);
    test_consumer_runs_first(&queue);
    
    // Synchronize all processes before next test
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    if (rank == 0) {
        printf("\n>>> Test 1 Complete <<<\n");
    }
    
    // Wait between tests
    sleep(1);
    
    // Test 2: Multiple consumers run before producer (if we have multiple consumers)
    if (size > 2) {
        MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
        printf("\n>>> Running Test 2: Multiple Consumers First <<<\n\n");
        test_multiple_consumers_first(&queue);
        MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
        
        if (rank == 0) {
            printf("\n>>> Test 2 Complete <<<\n");
        }
    } else {
        if (rank == 0) {
            printf("\nSkipping Test 2 (requires 3+ processes for multiple consumers)\n");
        }
    }
    
    // Final synchronization
    MPI_TRY(mpi_barrier(queue.mpi_ctx.comm));
    
    // Print final statistics
    if (rank == 0) {
        printf("\n");
        printf("========================================\n");
        printf("  All Tests Completed\n");
        printf("========================================\n");
        printf("\n");
        spmc_queue_print_stats(&queue);
    }
    
    // Cleanup
    spmc_queue_destroy(&queue);
    
    return 0;
}
