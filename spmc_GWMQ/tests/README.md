# SPMC Queue Test Suite

Bộ test toàn diện cho SPMC (Single Producer Multiple Consumer) Queue waitfree unbounded.

## Mô tả

Bộ test này được thiết kế để kiểm tra tính đúng đắn, hiệu suất và tính chất waitfree của SPMC queue implementation. Các test case cover các khía cạnh quan trọng của concurrent data structure.

## Cấu trúc Test Cases

### 1. Basic Functionality Test (`test_spmc_basic.c`)
**Mục đích**: Kiểm tra chức năng cơ bản của enqueue và dequeue
- Test enqueue/dequeue tuần tự
- Verification cơ bản của data integrity
- Minimum process requirement: 2 (1 producer + 1 consumer)

**Test scenarios**:
- Producer enqueue các giá trị tuần tự {1, 2, 3, 4, 5}
- Consumer dequeue và verify các giá trị nhận được
- Check success/failure rates

### 2. Multiple Consumer Competition Test (`test_spmc_competition.c`)
**Mục đích**: Test tính chất multiple consumer với competition
- Multiple consumers cạnh tranh cho cùng items
- Test fairness giữa các consumers
- Minimum process requirement: 3 (1 producer + 2+ consumers)

**Test scenarios**:
- Producer tạo 50 items với distinctive values (10, 20, 30, ...)
- Multiple consumers cạnh tranh dequeue
- Tracking số lượng items mỗi consumer nhận được
- Verification không có duplicate items

### 3. High Throughput Stress Test (`test_spmc_stress.c`)
**Mục đích**: Test hiệu suất và stability dưới high load
- Maximum throughput testing
- Performance metrics collection
- Minimum process requirement: 2

**Test scenarios**:
- Producer enqueue 1000 items với maximum speed
- Consumers dequeue với maximum speed
- Measure throughput (items/second)
- Monitor system stability

### 4. Edge Cases Test (`test_spmc_edge_cases.c`)
**Mục đích**: Test boundary conditions và special cases
- Test với special values (0, negative, INT_MAX, INT_MIN)
- Test empty queue behavior
- Test burst patterns và timing variations

**Test scenarios**:
- Enqueue special values: {0, -1, -100, INT_MIN, INT_MAX}
- Rapid burst enqueue patterns
- Delayed enqueue patterns với variable timing
- Consumer persistence testing dưới varying conditions

### 5. Waitfree Property Test (`test_spmc_waitfree.c`)
**Mục đích**: Verification của waitfree property
- Measure operation timing để ensure bounded completion time
- No blocking operations
- Consistent performance characteristics

**Test scenarios**:
- Time measurement cho từng enqueue/dequeue operation
- Statistical analysis: average, maximum operation time
- Verification rằng không có operation nào exceed reasonable time bounds (100ms)
- Detection của potential blocking behavior

### 6. Correctness Verification Test (`test_spmc_correctness.c`)
**Mục đích**: Comprehensive data integrity testing
- No data loss
- No duplicate items
- Proper sequencing

**Test scenarios**:
- Producer generate known sequence {1000, 1001, 1002, ..., 1099}
- Consumers verify received values
- Check for duplicates across all consumers
- Verify value ranges và data integrity
- Use sentinel values để mark end of sequences

### 7. Comprehensive Test Suite (`test_spmc_comprehensive.c`)
**Mục đích**: Combined testing của multiple scenarios
- Sequential execution của different test patterns
- System behavior under varying conditions
- Overall system validation

**Test scenarios**:
- Basic functionality test
- Burst performance test
- Consumer fairness test với equal timing
- System resilience test với variable production rates

## Build và Run Instructions

### Prerequisites
```bash
# Ensure MPI library is available
make -C .. check-deps

# Build the main SPMC queue library first
make -C ..
```

### Build Tests
```bash
cd tests
make all                    # Build tất cả test executables
make check-deps            # Verify dependencies
```

### Run Individual Tests
```bash
make run-basic             # 2 processes: basic functionality
make run-competition       # 4 processes: multiple consumer competition  
make run-stress           # 4 processes: high throughput stress
make run-edge-cases       # 2 processes: edge cases
make run-waitfree         # 3 processes: waitfree property verification
make run-correctness      # 3 processes: correctness verification
make run-comprehensive    # 4 processes: comprehensive suite
```

### Run Complete Test Suite
```bash
make run-all              # Run tất cả tests sequentially
make run-scalability      # Test với different process counts (2,4,6,8)
```

### Manual Execution
```bash
# Example: Run basic test với 2 processes
LD_LIBRARY_PATH=../../mpi_lib/lib:$LD_LIBRARY_PATH mpirun -np 2 ./bin/test_spmc_basic

# Example: Run competition test với 4 processes
LD_LIBRARY_PATH=../../mpi_lib/lib:$LD_LIBRARY_PATH mpirun -np 4 ./bin/test_spmc_competition
```

## Expected Results

### Success Criteria
1. **Basic Test**: All enqueued items successfully dequeued
2. **Competition Test**: Fair distribution, no duplicates, no data loss
3. **Stress Test**: High throughput achieved, no system crashes
4. **Edge Cases**: Proper handling của special values and conditions
5. **Waitfree Test**: All operations complete trong bounded time (< 100ms)
6. **Correctness Test**: Zero duplicates, proper value ranges, complete sequences

### Performance Benchmarks
- **Throughput**: Target > 1000 items/second per consumer
- **Latency**: Individual operations < 100ms (waitfree requirement)
- **Fairness**: No consumer should be starved > 50 consecutive attempts
- **Correctness**: 100% data integrity, zero duplicates

## Troubleshooting

### Common Issues
1. **MPI Library Not Found**: Ensure `LD_LIBRARY_PATH` includes MPI library path
2. **Insufficient Processes**: Check minimum process requirements cho mỗi test
3. **Timeout Issues**: Increase delay values nếu network latency cao
4. **Build Errors**: Verify all dependencies và header files available

### Debug Options
```bash
# Build với debug information
make CFLAGS="-Wall -Wextra -g -O0 -DDEBUG" all

# Run với MPI debugging
mpirun -np 4 --debug ./bin/test_spmc_stress
```

## Test Output Interpretation

### Producer Output
- `Enqueued X items successfully`: Số lượng items produced
- `Producer completed enqueuing`: Production phase finished
- Performance metrics: items/second throughput

### Consumer Output  
- `Consumer X dequeued: Y`: Individual item consumption
- `Consumer X consumed Z items`: Total consumption count
- `PASS/FAIL`: Correctness verification results

### System Output
- MPI initialization information
- Queue statistics và configuration
- Memory usage và performance metrics
- Final cleanup status

## Extensions

### Custom Test Development
Để create custom tests:
1. Follow existing test structure trong `test_spmc_*.c` files
2. Include necessary headers: `spmc_queue.h`, standard libraries
3. Use MPI_TRY macro cho error handling
4. Add test target vào Makefile
5. Document test purpose và expected results

### Performance Profiling
Integrate với profiling tools:
```bash
# Memory profiling
valgrind --tool=memcheck mpirun -np 4 ./bin/test_spmc_stress

# Performance profiling  
perf record mpirun -np 4 ./bin/test_spmc_stress
perf report
```

Bộ test suite này provides comprehensive coverage của SPMC queue functionality, ensuring reliability, performance, và correctness của waitfree implementation.