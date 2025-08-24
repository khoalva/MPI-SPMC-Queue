# Phân tích các Hạn chế của Thuật toán Hàng đợi Wait-free

Tài liệu này phân tích các hạn chế nghiêm trọng của thuật toán hàng đợi **SPMC (Single-Producer, Multi-Consumer) wait-free**, dựa trên giả mã được đề xuất trong bài báo khoa học gốc. Mặc dù là một thành tựu ấn tượng về mặt lý thuyết, việc hiện thực hóa thuật toán này đã phơi bày những lỗ hổng chí mạng, khiến nó không phù hợp cho các ứng dụng thực tế nếu không có những sửa đổi phức tạp.

---

## 1. Tổng quan: Mâu thuẫn giữa Lý thuyết và Thực tế

Mục tiêu của thuật toán gốc là để chứng minh sự tồn tại của một hàng đợi **wait-free**, và để làm cho việc chứng minh trở nên đơn giản, các tác giả đã đưa ra một giả định quan trọng: **hàng và cột là vô tận**. Tuy nhiên, trong thực tế, chúng ta phải sử dụng một mảng hữu hạn, và chính sự khác biệt này đã tạo ra các vấn đề nghiêm trọng.

| Đặc tính             | Trên Lý thuyết (Vô hạn)                   | Trong Thực tế (Hữu hạn)                  |
|----------------------|--------------------------------------------|------------------------------------------|
| **Đảm bảo Thời gian** | Wait-free (Mọi thao tác đều kết thúc)     | Đúng, chương trình không bị treo         |
| **Tính đúng đắn**     | An toàn (Producer không bao giờ hết chỗ)  | Thất bại, gây mất dữ liệu hàng loạt      |
| **Tính thứ tự**       | Ngầm giả định là FIFO                     | Thất bại, hoạt động như một "túi đồ"     |
| **Hiệu quả Bộ nhớ**   | Giả định là vô hạn                        | Rất kém (Độ phức tạp O(N²))              |

---

## 2. Lỗ hổng Cốt lõi: Hạn chế của Mảng Hữu hạn và Cơ chế Chuyển tầng

Vấn đề không nằm ở một cuộc chạy đua đơn lẻ, mà nằm ở **chính cơ chế được thiết kế để xử lý việc mảng không thể vô hạn**.

### a. Sự thích ứng cần thiết: Marker `T` ở cuối hàng

Vì không thể có một hàng vô tận, chúng ta đã phải thêm một cơ chế để báo hiệu "hết chỗ". Một giá trị đặc biệt (`T`, tức `-2`) được đặt vào ô nhớ cuối cùng của mỗi hàng.

- Khi **producer** thực hiện `Swap` và lấy ra giá trị `T`, nó hiểu rằng hàng đã đầy và **phải chuyển sang hàng tiếp theo**.
- Đây là một sự thích ứng cần thiết nhưng **vô tình tạo ra một điểm yếu mới**.

### b. Thảm họa "Bỏ rơi": Khi Producer chạy quá nhanh

Nguyên nhân trực tiếp gây ra **mất dữ liệu hàng loạt**, đặc biệt trong các bài test có tải trọng cao.

#### Chuỗi sự kiện:

1. **Producer lấp đầy một tầng**: Producer (P) nhanh chóng lấp đầy toàn bộ tầng X (ví dụ: 1027 món hàng).
2. **Producer chuyển tầng**: Khi chạm vào marker `T`, P chuyển sang tầng X+1.
3. **Hành động nguy hiểm**: P cập nhật biến `ROW` thành X+1, yêu cầu mọi consumer cũng chuyển tầng.

#### Sự bỏ rơi vĩnh viễn:

- Tại thời điểm này, các **consumer có thể chưa xử lý xong tầng X**.
- Tuy nhiên, vì `ROW` đã đổi, mọi lệnh `dequeue` mới đều chỉ đến tầng X+1.
- Không có cách nào để **quay lại xử lý các item còn sót lại trên tầng X**.

> **Kết quả**: Các item chưa được tiêu thụ ở tầng X **bị bỏ rơi vĩnh viễn**. Trong bài test "Throughput", chỉ có **4438 trong 10000 món hàng được tiêu thụ**.

---

## 3. Các Hệ quả khác

### a. Phá vỡ Quy tắc FIFO (First-In, First-Out)

Thao tác `Fetch&Increment` trên mảng `HEAD` chỉ cấp phát "vé" (chỉ số cột) một cách tuần tự, **không đảm bảo thứ tự xử lý**. Do đó, các consumer có thể **lấy item ra không đúng thứ tự**, gây lộn xộn.

### b. Lãng phí Bộ nhớ (Độ phức tạp O(N²))

Thuật toán sử dụng mảng hai chiều `ITEMS` với kích thước `N x N` để tách rời các tầng. Mặc dù đạt được **tính chất wait-free**, cái giá phải trả là **rất lớn về bộ nhớ**.

### c. Hạn chế về mặt Lý thuyết (Giới hạn 2 Consumer)

Chính tác giả cũng thừa nhận: thuật toán **chỉ được chứng minh là đúng với tối đa 2 consumer**, do các primitive nguyên tử (`Fetch&Add`, `Swap`) có **số consensus là 2**.

---

> 📌 **Kết luận**: Mặc dù thuật toán mang tính đột phá về mặt lý thuyết, nhưng nếu không có những cải tiến đáng kể về cấu trúc dữ liệu và đồng bộ hóa, **nó không thể áp dụng vào thực tế một cách an toàn và hiệu quả**.
