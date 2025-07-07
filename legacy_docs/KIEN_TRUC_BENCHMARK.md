# 🏗️ Kiến Trúc Benchmark SPMC - Cách Hoạt Động

## 📋 Tổng Quan Kiến Trúc

### 🔄 Luồng Hoạt Động Chính:
```
[spmc_2004/] → [benchmark/] → [results/]
     ↓              ↓             ↓
 SPMC Queue  →  Benchmark   →   Metrics
 Implementation   Library       & CSV
```

## 🧩 Các Thành Phần Chính

### 1. **SPMC Queue Implementation** (`spmc_2004/`)
```c
// spmc_queue.h - Interface chính
typedef struct {
    mpi_context_t mpi_ctx;    // MPI context 
    int *head;                // Head pointer
    int *items;               // Queue items
    int row;                  // Current row
    // ... các field khác
} spmc_queue_t;

// Các hàm cốt lõi:
int spmc_queue_enqueue(spmc_queue_t *queue, int value);  // Producer thêm item
int spmc_queue_dequeue(spmc_queue_t *queue);             // Consumer lấy item
int spmc_queue_is_enqueuer(spmc_queue_t *queue);         // Check producer/consumer
```

### 2. **Benchmark Library** (`benchmark/`)
```c
// benchmark.h - Wrapper để đo performance
typedef struct {
    double total_time_sec;           // Tổng thời gian
    double throughput_items_per_sec; // Items/giây
    double avg_enqueue_latency_us;   // Độ trễ enqueue
    long total_items_produced;       // Tổng items produced
    // ... metrics khác
} benchmark_results_t;

// Macro để đo timing:
BENCHMARK_RECORD_ENQUEUE(&ctx, spmc_queue_enqueue(queue, value));
BENCHMARK_RECORD_DEQUEUE(&ctx, value = spmc_queue_dequeue(queue));
```

### 3. **Test Runner** (`benchmark/examples/spmc_benchmark.c`)
```c
// Kết nối SPMC Queue với Benchmark Library
int main(int argc, char *argv[]) {
    spmc_queue_t queue;                    // 1. Tạo SPMC queue
    spmc_queue_init(&queue, argc, argv);   // 2. Init với MPI
    
    benchmark_ctx_t bench_ctx;             // 3. Tạo benchmark context
    benchmark_init(&bench_ctx, ...);       // 4. Init benchmark
    
    if (spmc_queue_is_enqueuer(&queue)) {  // 5. Producer logic
        for (int i = 0; i < num_items; i++) {
            BENCHMARK_RECORD_ENQUEUE(&bench_ctx, 
                spmc_queue_enqueue(&queue, values[i]));
        }
    } else {                               // 6. Consumer logic  
        while (...) {
            BENCHMARK_RECORD_DEQUEUE(&bench_ctx,
                value = spmc_queue_dequeue(&queue));
        }
    }
    
    benchmark_aggregate_results(&bench_ctx, ...); // 7. Thu thập kết quả
    benchmark_print_report(&bench_ctx);           // 8. In báo cáo
}
```

## 🔗 Kết Nối spmc_2004 với Benchmark

### Hiện Tại - Cách Hoạt Động:

1. **Build Process:**
```bash
cd benchmark/
make all  # Build benchmark library + examples/spmc_benchmark
```

2. **Linking:** 
- `benchmark/examples/spmc_benchmark.c` #include `spmc_queue.h`
- Makefile link với `../spmc_2004/` để lấy SPMC queue implementation
- Tạo executable `examples/spmc_benchmark`

3. **Execution:**
```bash
mpirun -np 3 ./examples/spmc_benchmark quick
```

### 🎯 Luồng Hoạt Động Chi Tiết:

#### Bước 1: MPI Init & Process Role Assignment
```c
spmc_queue_init(&queue, argc, argv);  // Init MPI + queue

// MPI tự động phân vai trò:
// - Rank 0 = Producer (enqueuer)  
// - Rank 1,2,3... = Consumer (dequeuer)
```

#### Bước 2: Producer Process (Rank 0)
```c
if (spmc_queue_is_enqueuer(&queue)) {  // True for rank 0
    // Producer enqueue items và benchmark đo:
    // - Thời gian enqueue
    // - Throughput 
    // - Latency per operation
    for (int i = 0; i < 1000; i++) {
        BENCHMARK_RECORD_ENQUEUE(&ctx, spmc_queue_enqueue(&queue, i));
    }
}
```

#### Bước 3: Consumer Processes (Rank 1,2,3...)
```c
else {  // Consumer processes
    // Consumer dequeue items và benchmark đo:
    // - Thời gian dequeue
    // - Items consumed per consumer
    // - Load balancing
    while (items_consumed < target) {
        BENCHMARK_RECORD_DEQUEUE(&ctx, value = spmc_queue_dequeue(&queue));
    }
}
```

#### Bước 4: Aggregation & Results
```c
MPI_Barrier(...);                      // Sync all processes
benchmark_aggregate_results(&ctx, ...); // Gather metrics from all processes
benchmark_print_report(&ctx);          // Print summary (rank 0 only)
benchmark_export_csv(&ctx, "results.csv"); // Export to CSV
```

## 📊 Metrics Thu Thập Được

### Per-Process Metrics:
- **Throughput**: Items processed/second cho từng process
- **Latency**: Thời gian trung bình cho enqueue/dequeue operations  
- **Items Count**: Số items mỗi process xử lý
- **Memory Usage**: Memory peak của từng process

### Aggregated Metrics:
- **Total Throughput**: Tổng throughput của toàn hệ thống
- **Load Balance Score**: Mức độ phân phối công việc đều giữa consumers
- **Scalability**: Hiệu năng thay đổi như thế nào khi tăng số processes

## 🛠️ Tùy Chỉnh cho spmc_2004

### Để Chạy Benchmark với spmc_2004:

1. **Check Dependencies:**
```bash
# Đảm bảo spmc_2004 đã build
cd spmc_2004/
make  # Build queue implementation

# Build benchmark 
cd ../benchmark/
make clean && make all
```

2. **Chạy Test:**
```bash
# Quick test với 3 processes
./quick_test.sh

# Hoặc manual:
mpirun -np 3 ./examples/spmc_benchmark quick
```

3. **Hiểu Kết Quả:**
```
=== BENCHMARK RESULTS ===
Rank 0 (Producer): Enqueued 1000 items in 0.245s (4081 items/sec)
Rank 1 (Consumer): Dequeued 334 items in 0.245s (1363 items/sec)  
Rank 2 (Consumer): Dequeued 333 items in 0.245s (1359 items/sec)
Rank 3 (Consumer): Dequeued 333 items in 0.245s (1359 items/sec)

Total Throughput: 4081 items/sec
Load Balance Score: 99.7% (very good)
```

## 🔍 Debugging & Troubleshooting

### Check Linking:
```bash
# Xem dependencies
ldd benchmark/examples/spmc_benchmark

# Xem symbols
nm benchmark/examples/spmc_benchmark | grep spmc
```

### Test SPMC Queue Độc Lập:
```bash
cd spmc_2004/
mpirun -np 3 ./queue_spmc  # Test queue không có benchmark
```

### Verbose Execution:
```bash
./quick_test.sh 3 quick true  # Enable verbose output
```

## 💡 Key Insights

**Benchmark KHÔNG thay đổi logic của SPMC queue** - nó chỉ wrap các function calls để đo performance:

- `spmc_queue_enqueue()` → `BENCHMARK_RECORD_ENQUEUE()`
- `spmc_queue_dequeue()` → `BENCHMARK_RECORD_DEQUEUE()`

**Mỗi test type có cấu hình khác nhau:**
- `quick`: 1000 items, fast test
- `throughput`: 10000 items, focus on items/sec
- `latency`: detailed timing analysis
- `scalability`: test với nhiều process counts

**MPI roles tự động:**
- Process 0 = Producer (enqueuer)
- Process 1,2,3... = Consumers (dequeuers)
