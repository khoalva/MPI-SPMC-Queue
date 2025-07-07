#!/bin/bash

# Demo Script - Hướng dẫn chạy benchmark từng bước
# Script này sẽ hướng dẫn bạn qua các bước cơ bản

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

BENCHMARK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  DEMO: Hướng Dẫn Sử Dụng Benchmark    ${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Hàm hỏi user có muốn tiếp tục không
ask_continue() {
    echo -e "${YELLOW}Nhấn Enter để tiếp tục, hoặc Ctrl+C để thoát...${NC}"
    read
}

# Bước 1: Kiểm tra và build
echo -e "${GREEN}Bước 1: Kiểm tra và build benchmark${NC}"
echo "Đầu tiên chúng ta cần build benchmark executable..."
ask_continue

cd "$BENCHMARK_DIR"

if [[ ! -f "examples/spmc_benchmark" ]]; then
    echo "Building benchmark..."
    make clean && make all
    echo -e "${GREEN}✓ Build thành công!${NC}"
else
    echo -e "${GREEN}✓ Benchmark executable đã tồn tại${NC}"
fi

echo ""

# Bước 2: Chạy test đầu tiên
echo -e "${GREEN}Bước 2: Chạy test đầu tiên (Quick Test)${NC}"
echo "Chúng ta sẽ chạy một test nhanh với 3 processes..."
echo "Test này chỉ mất khoảng 10-30 giây."
ask_continue

echo "Chạy lệnh: ./quick_test.sh"
./quick_test.sh

echo ""
echo -e "${GREEN}✓ Test đầu tiên hoàn thành!${NC}"
echo ""

# Bước 3: Xem kết quả
echo -e "${GREEN}Bước 3: Xem kết quả${NC}"
echo "Kết quả được lưu trong thư mục results/ và logs/"
ask_continue

echo "Danh sách file kết quả:"
ls -la results/ 2>/dev/null || echo "Chưa có file kết quả"

echo ""
echo "Danh sách file log:"
ls -la logs/ | tail -5

echo ""

# Bước 4: Test với các cấu hình khác
echo -e "${GREEN}Bước 4: Thử nghiệm với số processes khác${NC}"
echo "Bây giờ chúng ta sẽ test với 4 processes để so sánh..."
ask_continue

echo "Chạy lệnh: ./quick_test.sh 4"
./quick_test.sh 4

echo ""
echo -e "${GREEN}✓ Test với 4 processes hoàn thành!${NC}"
echo ""

# Bước 5: So sánh kết quả
echo -e "${GREEN}Bước 5: So sánh kết quả${NC}"
echo "Hãy xem sự khác biệt giữa 3 processes và 4 processes:"
ask_continue

echo ""
echo "=== Kết quả với 3 processes ==="
if [[ -f "results/benchmark_quick_3procs.csv" ]]; then
    head -5 results/benchmark_quick_3procs.csv
else
    echo "Không tìm thấy file kết quả 3 processes"
fi

echo ""
echo "=== Kết quả với 4 processes ==="
if [[ -f "results/benchmark_quick_4procs.csv" ]]; then
    head -5 results/benchmark_quick_4procs.csv
else
    echo "Không tìm thấy file kết quả 4 processes"
fi

echo ""

# Bước 6: Test nâng cao (tùy chọn)
echo -e "${GREEN}Bước 6: Test nâng cao (Tùy chọn)${NC}"
echo "Bạn có muốn thử test throughput để đo hiệu năng chi tiết hơn không?"
echo "Test này sẽ mất khoảng 1-2 phút..."
echo -e "${YELLOW}Nhấn 'y' để chạy, hoặc Enter để bỏ qua:${NC}"
read -r response

if [[ "$response" == "y" || "$response" == "Y" ]]; then
    echo ""
    echo "Chạy lệnh: ./run_benchmarks.sh throughput -p 4 --verbose"
    ./run_benchmarks.sh throughput -p 4 --verbose
    echo ""
    echo -e "${GREEN}✓ Throughput test hoàn thành!${NC}"
fi

echo ""

# Kết thúc
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}             DEMO HOÀN THÀNH            ${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "${GREEN}Chúc mừng! Bạn đã hoàn thành demo benchmark.${NC}"
echo ""
echo "Những gì bạn đã học:"
echo "• Cách build benchmark"
echo "• Cách chạy quick test"
echo "• Cách xem kết quả"
echo "• Cách so sánh hiệu năng với số processes khác nhau"
echo ""
echo "Bước tiếp theo:"
echo "1. Đọc file HUONG_DAN_BENCHMARK.md để hiểu thêm"
echo "2. Thử các loại test khác: latency, scalability"
echo "3. Phân tích file CSV với Excel hoặc tools khác"
echo "4. Tối ưu code dựa trên kết quả benchmark"
echo ""
echo -e "${YELLOW}File kết quả trong: results/${NC}"
echo -e "${YELLOW}File log trong: logs/${NC}"
echo ""
