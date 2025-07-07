# Hướng Dẫn Sử Dụng Benchmark SPMC Queue

## Tổng Quan
Dự án này có hệ thống benchmark để đo hiệu năng của SPMC (Single Producer Multiple Consumer) Queue sử dụng MPI. Benchmark giúp bạn đánh giá:

- **Throughput**: Số lượng items được xử lý mỗi giây
- **Latency**: Thời gian thực hiện các thao tác enqueue/dequeue
- **Scalability**: Hiệu năng khi tăng số lượng processes
- **Load Balancing**: Phân phối công việc giữa các consumer

## 🚀 Cách Sử Dụng Cơ Bản

### 1. Build Benchmark
```bash
# Di chuyển vào thư mục benchmark
cd benchmark

# Build tất cả (thư viện + examples)
make all

# Hoặc build từng phần
make static    # Thư viện tĩnh
make shared    # Thư viện động
make examples  # Chương trình benchmark
```

### 2. Chạy Test Nhanh (Khuyến nghị cho người mới)
```bash
# Test nhanh với 3 processes (mặc định)
./quick_test.sh

# Test với số processes khác
./quick_test.sh 4        # 4 processes
./quick_test.sh 6 quick  # 6 processes, test type = quick
```

### 3. Chạy Benchmark Chi Tiết
```bash
# Script chính với nhiều tùy chọn
./run_benchmarks.sh [OPTIONS] [TEST_TYPE]

# Ví dụ:
./run_benchmarks.sh quick           # Test nhanh
./run_benchmarks.sh throughput -p 4 # Test throughput với 4 processes
./run_benchmarks.sh suite           # Chạy tất cả tests
```

## 📊 Các Loại Test

### 1. **Quick Test** (Khuyến nghị bắt đầu)
- Mục đích: Kiểm tra nhanh hệ thống có hoạt động không
- Thời gian: ~10-30 giây
- Items: 1,000
```bash
./quick_test.sh
# hoặc
./run_benchmarks.sh quick
```

### 2. **Throughput Test**
- Mục đích: Đo tốc độ xử lý items/giây
- Thời gian: ~1-2 phút
- Items: 10,000
```bash
./run_benchmarks.sh throughput
```

### 3. **Latency Test**
- Mục đích: Đo thời gian chi tiết cho từng thao tác
- Thời gian: ~2-3 phút
- Đo độ chính xác microsecond
```bash
./run_benchmarks.sh latency
```

### 4. **Scalability Test**
- Mục đích: Xem hiệu năng thay đổi khi tăng số processes
- Test với nhiều process counts khác nhau
```bash
./run_benchmarks.sh scalability
```

### 5. **Suite Test**
- Mục đích: Chạy tất cả tests để có báo cáo toàn diện
- Thời gian: 10-15 phút
```bash
./run_benchmarks.sh suite
```

## ⚙️ Tùy Chọn Nâng Cao

### Các Tham Số Quan Trọng:
```bash
# Số lượng MPI processes
-p, --processes NUM     # Mặc định: 3

# Xuất kết quả ra CSV để phân tích
--export-csv

# Hiển thị output chi tiết
-v, --verbose

# Chạy test nhanh hơn (ít tests)
--quick-suite

# Ví dụ:
./run_benchmarks.sh throughput -p 6 --export-csv -v
```

## 📁 Kết Quả và Log Files

### Thư Mục Quan Trọng:
- `results/`: Chứa file CSV với kết quả đo
- `logs/`: Chứa log chi tiết của từng lần chạy
- `examples/`: Chứa executable `spmc_benchmark`

### Đọc Kết Quả:
```bash
# Xem kết quả mới nhất
ls -la results/

# Xem log file
tail -20 logs/benchmark_quick_3procs_*.log
```

## 🔧 Troubleshooting

### Lỗi Thường Gặp:

1. **"Benchmark executable not found"**
```bash
cd benchmark
make clean && make all
```

2. **"Permission denied" trên WSL**
- Script tự động thêm `--allow-run-as-root`
- Nếu vẫn lỗi, chạy với sudo

3. **MPI không tìm thấy**
```bash
# Ubuntu/Debian
sudo apt install mpich

# CentOS/RHEL
sudo yum install mpich
```

4. **Test chạy quá lâu**
- Dùng `quick_test.sh` thay vì `run_benchmarks.sh suite`
- Giảm số processes: `-p 2`

## 💡 Tips for Beginners

### Bắt Đầu Đơn Giản:
1. **Chạy test đầu tiên:**
```bash
cd benchmark
make all
./quick_test.sh
```

2. **Xem kết quả:**
```bash
# Kết quả hiển thị ngay trên terminal
# Hoặc xem file CSV
cat results/benchmark_quick_3procs.csv
```

3. **Thử nghiệm với số processes khác:**
```bash
./quick_test.sh 2  # 2 processes
./quick_test.sh 4  # 4 processes
./quick_test.sh 6  # 6 processes
```

### Hiểu Kết Quả:
- **Throughput**: Cao = tốt (items/second)
- **Latency**: Thấp = tốt (microseconds)
- **Load Balance**: Gần 100% = tốt
- **Memory Usage**: Thấp = tốt

## 🎯 Next Steps

Sau khi làm quen với benchmark cơ bản:

1. **Analyze Results**: Mở file CSV với Excel/LibreOffice
2. **Compare Configurations**: Test với nhiều process counts
3. **Optimize**: Dựa vào kết quả để tối ưu code
4. **Custom Tests**: Chỉnh sửa parameters trong code C

## 📞 Cần Trợ Giúp?

- Xem file `benchmark/README.md` để biết API chi tiết
- Check logs trong `benchmark/logs/` khi có lỗi
- Dùng `--verbose` để debug
