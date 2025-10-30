
# 📦 SPMC Queue: Giải thích Thay đổi Thuật toán

Tài liệu này giải thích các thay đổi quan trọng đối với hàm `spmc_queue_dequeue` trong quá trình phát triển, đặc biệt nhấn mạnh sự khác biệt so với thuật toán FFQ (Fetch-and-Fetch Queue) ban đầu. Mục tiêu chính là khắc phục lỗi chương trình bị treo do vòng lặp vô hạn.

---

## 1. Thuật toán dequeue Gốc và Vấn đề

**Thuật toán ban đầu:**

```c
function FFQ_DEQUEUE:
  rank ← fetch-and-inc(head)
  while TRUE:
    c ← cells[rank mod N]
    if c.rank == rank:
      // Thành công, lấy dữ liệu và thoát
      data ← c.data
      c.rank ← -1
      return data
    else if c.gap >= rank and c.rank != rank:
      // Phát hiện "gap", lấy ticket mới và thử lại
      rank ← fetch-and-inc(head)
    else:
      // Chờ producer ghi dữ liệu
      wait()
```

**Vấn đề cốt lõi:**

- Nếu producer đã dừng, consumer vẫn có thể nhận được một giá trị `rank` mới từ head.
- Nếu cell tương ứng không bao giờ được ghi dữ liệu (do producer đã kết thúc), consumer sẽ liên tục lấy ticket mới (`102, 103, ...`) mà không bao giờ được phục vụ.
- Điều này dẫn đến vòng lặp vô hạn, khiến chương trình bị treo sau khi benchmark kết thúc.

---

## 2. Giải pháp: Loại bỏ Vòng lặp Vô hạn và Timeout

Để khắc phục, phiên bản mới của hàm dequeue đã được điều chỉnh như sau:

```c
// Phiên bản đã sửa trong spmc_queue.c
int spmc_queue_dequeue(spmc_queue_t *queue) {
    // ...
    // Lấy một "ticket" (my_rank) duy nhất cho mỗi lần gọi
    MPI_TRY(mpi_fetch_and_op(&one, &my_rank, MPI_INT, 0, 0, MPI_SUM, &queue->win_head));

    // Vòng lặp polling giới hạn số lần thử
    while (poll_attempts < MAX_POLL_ATTEMPTS) {
        // ...
        // Trường hợp 1: Thành công
        if (c.rank == my_rank) {
            // ... Lấy dữ liệu và trả về MPI_SUCCESS
        }
        // Trường hợp 2: Phát hiện "gap"
        else if (c.gap >= my_rank && c.rank != my_rank) {
            // Ticket này sẽ không bao giờ được phục vụ, thoát ngay
            return -1; // Thất bại
        }
        // Trường hợp 3: Đợi producer
        else {
            usleep(10);
            poll_attempts++;
        }
    }

    // Nếu hết thời gian chờ, thoát
    return -1;
}
```

### 🔑 Các thay đổi chính và lợi ích

- **Loại bỏ vòng lặp vô hạn:** Không còn vòng lặp `while(1)`. Mỗi lần gọi dequeue chỉ xử lý một ticket duy nhất.
- **Xử lý gap dứt khoát:** Khi phát hiện gap, hàm trả về -1 ngay lập tức thay vì thử lấy ticket mới.
- **Giới hạn thời gian chờ:** Vòng lặp polling chỉ chạy số lần cố định (`MAX_POLL_ATTEMPTS`). Nếu producer không ghi dữ liệu kịp, hàm sẽ timeout và trả về lỗi.
- **Chuyển trách nhiệm retry:** Việc quyết định thử lại được chuyển ra ngoài hàm dequeue (ví dụ: vòng lặp trong hàm main), giúp dequeue ổn định và dễ kiểm soát hơn.

---

## 📝 Kết luận

Sự điều chỉnh này, dù khác biệt với thuật toán gốc, là cần thiết để đảm bảo tính ổn định và đúng đắn của chương trình trong thực tế. Nó loại bỏ hoàn toàn nguy cơ treo chương trình, giúp thư viện và các benchmark trở nên tin cậy hơn.