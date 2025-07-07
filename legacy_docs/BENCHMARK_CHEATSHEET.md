# 🚀 SPMC Benchmark - Cheat Sheet

## 🆕 **THAY ĐỔI MỚI: Tổ chức kết quả theo loại SPMC**

### Tự động phát hiện loại SPMC
- Script sẽ tự động phát hiện các implementation SPMC có sẵn
- Không cần hard-code tên loại SPMC nữa
- Hỗ trợ bất kỳ tên thư mục SPMC nào (spmc_2004, spmc_2005, custom_spmc, v.v.)

### Kết quả được tổ chức theo thư mục
```bash
benchmark/results/
├── spmc_2004/          # Kết quả cho SPMC 2004
├── spmc_2005/          # Kết quả cho SPMC 2005 (nếu có)
└── custom_spmc/        # Kết quả cho implementation tùy chỉnh
```

### Công cụ phân tích kết quả mới
```bash
./analyze_results.sh list        # Liệt kê tất cả kết quả
./analyze_results.sh summary     # Tóm tắt kết quả
./analyze_results.sh compare     # So sánh các loại SPMC
./analyze_results.sh details spmc_2004  # Chi tiết cho loại cụ thể
./analyze_results.sh clean       # Dọn dẹp file cũ
```

## Lệnh Cơ Bản (Copy & Paste)

### Build Benchmark
```bash
cd benchmark
make all
```

### Test Nhanh Nhất
```bash
./quick_test.sh          # 3 processes, ~10-30 giây
```

### Test Với Các Cấu Hình Khác
```bash
./quick_test.sh 2        # 2 processes
./quick_test.sh 4        # 4 processes  
./quick_test.sh 6        # 6 processes
```

### Các Loại Test Chi Tiết
```bash
./run_benchmarks.sh quick           # Test nhanh
./run_benchmarks.sh throughput      # Đo tốc độ xử lý
./run_benchmarks.sh latency         # Đo độ trễ
./run_benchmarks.sh scalability     # Đo khả năng mở rộng
./run_benchmarks.sh suite           # Chạy tất cả (10-15 phút)
```

### Test Với Tùy Chọn
```bash
./run_benchmarks.sh throughput -p 4 --verbose    # 4 processes, hiển thị chi tiết
./run_benchmarks.sh latency --export-csv         # Xuất CSV
./run_benchmarks.sh suite --quick-suite          # Suite nhanh
```

## Xem Kết Quả

### Kết Quả Realtime
```bash
# Kết quả hiển thị ngay trên terminal sau khi chạy
```

### File Kết Quả
```bash
ls results/                    # Xem file CSV
cat results/*.csv              # Đọc tất cả CSV
tail -20 logs/*.log            # Xem log mới nhất
```

### Phân Tích Excel
```bash
# Mở file .csv trong results/ bằng Excel hoặc LibreOffice
# Columns: Process_Rank, Items_Processed, Throughput, Latency_Avg, etc.
```

## Demo Hướng Dẫn Từng Bước
```bash
./demo_benchmark.sh      # Script hướng dẫn interactive
```

## Hiểu Kết Quả

| Metric | Đơn Vị | Tốt | Ý Nghĩa |
|--------|--------|-----|---------|
| Throughput | items/sec | Cao | Tốc độ xử lý |
| Latency | microseconds | Thấp | Độ trễ thao tác |
| Load Balance | % | ~100% | Phân phối đều |
| Memory Usage | MB | Thấp | Tiêu thụ bộ nhớ |

## Troubleshooting 1-Liner

```bash
# Build lại nếu có lỗi
make clean && make all

# Kiểm tra MPI
mpirun --version

# Chạy với 2 processes nếu lỗi resource
./quick_test.sh 2

# Xem log lỗi cuối
tail -20 logs/*.log | grep -i error
```

## Workflow Khuyến Nghị

1. **Lần đầu:**
```bash
cd benchmark
make all
./demo_benchmark.sh
```

2. **Hàng ngày:**
```bash
./quick_test.sh              # Quick check
./quick_test.sh 4             # Compare performance  
```

3. **Phân tích sâu:**
```bash
./run_benchmarks.sh suite --export-csv
# Mở results/*.csv bằng Excel
```

## Tips Pro

- Dùng `quick_test.sh` cho development hàng ngày
- Dùng `suite` cho benchmark cuối tuần  
- So sánh kết quả trước/sau khi optimize code
- Chạy test với nhiều process counts (2,3,4,6,8)
- Export CSV để vẽ graph trong Excel/Python

## Liên Hệ
- Đọc `HUONG_DAN_BENCHMARK.md` để hiểu chi tiết
- Check `benchmark/README.md` cho API documentation
