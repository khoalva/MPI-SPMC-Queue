# 🔧 Hướng Dẫn Thêm SPMC Implementation Mới Vào Benchmark

## 🎯 Khi bạn tạo folder SPMC mới (ví dụ: `spmc_2025/`)

### **Bước 1: Chuẩn bị SPMC Implementation**

Đảm bảo folder SPMC mới có interface tương tự `spmc_2004/`:

```c
// spmc_new/spmc_queue.h
typedef struct {
    mpi_context_t mpi_ctx;    // Phải có MPI context
    // ... các field khác
} spmc_queue_t;

// Các hàm bắt buộc:
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue);
int spmc_queue_is_enqueuer(spmc_queue_t *queue);
void spmc_queue_cleanup(spmc_queue_t *queue);
```

### **Bước 2: Thêm target mới vào benchmark/Makefile**

```makefile
# Thêm vào benchmark/Makefile

# Target cho SPMC implementation mới
spmc_new_benchmark: $(EXAMPLEDIR)/spmc_benchmark.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -I$(INCDIR) -I../spmc_new -I../mpi_lib/include \
		$(EXAMPLEDIR)/spmc_benchmark.c ../spmc_new/spmc_queue.o \
		$(LIBDIR)/$(STATIC_LIB) ../mpi_lib/lib/libmpi_wrapper.a \
		-lm -o $(EXAMPLEDIR)/spmc_new_benchmark

# Test targets cho implementation mới
test-new-quick: spmc_new_benchmark
	@echo "Running quick benchmark test for SPMC NEW..."
	cd $(EXAMPLEDIR) && mpirun --allow-run-as-root -np 3 ./spmc_new_benchmark quick

test-new-throughput: spmc_new_benchmark
	@echo "Running throughput benchmark test for SPMC NEW..."
	cd $(EXAMPLEDIR) && mpirun --allow-run-as-root -np 4 ./spmc_new_benchmark throughput

# So sánh performance
compare-implementations: spmc_benchmark spmc_new_benchmark
	@echo "=== Comparing SPMC Implementations ==="
	@echo "Testing spmc_2004..."
	cd $(EXAMPLEDIR) && mpirun --allow-run-as-root -np 3 ./spmc_benchmark quick > results_2004.txt
	@echo "Testing spmc_new..."
	cd $(EXAMPLEDIR) && mpirun --allow-run-as-root -np 3 ./spmc_new_benchmark quick > results_new.txt
	@echo "Results saved to: results_2004.txt, results_new.txt"
```

### **Bước 3: Chạy benchmark**

```bash
# Build benchmark cho implementation mới
cd benchmark/
make spmc_new_benchmark

# Chạy test
make test-new-quick
make test-new-throughput

# So sánh với implementation cũ
make compare-implementations
```

## 🔄 **Cách 2: Tạo script tự động**

Tạo script để dễ dàng switch giữa các SPMC implementations:

```bash
# benchmark/switch_implementation.sh
#!/bin/bash

IMPL_NAME=$1
if [ -z "$IMPL_NAME" ]; then
    echo "Usage: $0 <implementation_name>"
    echo "Available: spmc_2004, spmc_new, spmc_optimized"
    exit 1
fi

# Update Makefile dynamically
sed -i "s|../spmc_[^/]*/|../${IMPL_NAME}/|g" Makefile
sed -i "s|spmc_[^_]*_benchmark|${IMPL_NAME}_benchmark|g" Makefile

echo "Switched to $IMPL_NAME implementation"
make clean && make all
```

## 📊 **Cách 3: Benchmark tự động cho tất cả implementations**

```bash
# benchmark/benchmark_all.sh
#!/bin/bash

IMPLEMENTATIONS=("spmc_2004" "spmc_new" "spmc_optimized")
TESTS=("quick" "throughput" "latency")

echo "=== Benchmarking All SPMC Implementations ==="

for impl in "${IMPLEMENTATIONS[@]}"; do
    if [ -d "../$impl" ]; then
        echo "\\n--- Testing $impl ---"
        
        # Build cho implementation này
        make clean
        sed -i "s|../spmc_[^/]*/|../${impl}/|g" Makefile
        make all
        
        # Chạy tất cả tests
        for test in "${TESTS[@]}"; do
            echo "Running $test test for $impl..."
            cd examples && mpirun -np 3 ./spmc_benchmark $test > "../results/${impl}_${test}.csv"
            cd ..
        done
    fi
done

echo "\\nAll results saved to results/ directory"
```

## ⚡ **Tips để implement SPMC mới:**

### 1. **Giữ interface consistency:**
```c
// Đảm bảo các hàm này có signature giống nhau
int spmc_queue_init(spmc_queue_t *queue, int argc, char *argv[]);
int spmc_queue_enqueue(spmc_queue_t *queue, int value);
int spmc_queue_dequeue(spmc_queue_t *queue);
```

### 2. **Sử dụng cùng MPI pattern:**
```c
// Rank 0 = Producer, Rank 1+ = Consumer
int spmc_queue_is_enqueuer(spmc_queue_t *queue) {
    return queue->mpi_ctx.rank == 0;
}
```

### 3. **Build system consistency:**
```makefile
# Trong spmc_new/Makefile
spmc_queue.o: spmc_queue.c spmc_queue.h
	$(CC) $(CFLAGS) -c spmc_queue.c -o spmc_queue.o
```

## 🎯 **Kết quả mong đợi:**

Sau khi setup, bạn sẽ có thể:

```bash
# Test implementation cũ
make test-quick              # spmc_2004

# Test implementation mới  
make test-new-quick          # spmc_new

# So sánh performance
make compare-implementations # Cả hai

# Test tất cả
./benchmark_all.sh          # Tất cả implementations
```

## 📈 **So sánh kết quả:**

Benchmark sẽ cho bạn metrics để so sánh:

```
=== SPMC_2004 Results ===
Total Throughput: 4081 items/sec
Load Balance: 99.7%

=== SPMC_NEW Results ===  
Total Throughput: 5234 items/sec  ← Tốt hơn!
Load Balance: 98.9%

=== Performance Gain ===
Improvement: +28.2% throughput
```

Như vậy benchmark system rất linh hoạt, chỉ cần thêm vài dòng Makefile là có thể test implementation mới!
