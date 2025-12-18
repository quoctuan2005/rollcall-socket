(() => {
    // Lấy các element từ HTML
    const startBtn = document.getElementById('startBtn');
    const mssvInput = document.getElementById('mssv');
    const loginDiv = document.getElementById('login');
    const quizDiv = document.getElementById('quiz');
    const resultDiv = document.getElementById('result');

    let visitorId = null;
    let ws = null;

    // 1. Kết nối WebSocket
    function initWebSocket() {
        ws = new WebSocket('ws://localhost:8080');

        ws.onopen = () => {
            console.log('Connected to Server');
            updateStartBtn();
        };

        ws.onmessage = (ev) => {
            let data = {};
            try { data = JSON.parse(ev.data); } catch (_) { }

            // LOGIC QUAN TRỌNG: Server trả về OK -> Vào thi ngay
            if (data.status === 'OK') {
                loginDiv.style.display = 'none';
                quizDiv.style.display = 'block';
                renderQuiz(); // Vẽ câu hỏi ra màn hình
            }
            else if (data.status === 'BLOCKED') {
                alert('CHẶN: ' + data.msg);
                window.location.reload();
            }
        };

        ws.onclose = () => {
            alert('Mất kết nối tới Server!');
        };
    }

    // 2. Lấy Fingerprint thiết bị
    async function initFingerprint() {
        try {
            // Chờ thư viện load xong
            while (!window.FingerprintJS) await new Promise(r => setTimeout(r, 100));
            const fp = await FingerprintJS.load();
            const result = await fp.get();
            visitorId = result.visitorId;
            startBtn.textContent = "Bắt đầu làm bài";
            updateStartBtn();
        } catch (e) {
            startBtn.textContent = "Lỗi xác thực thiết bị";
        }
    }

    function updateStartBtn() {
        // Chỉ cho bấm nút khi đã có Fingerprint và kết nối mạng
        startBtn.disabled = !(visitorId && ws && ws.readyState === WebSocket.OPEN);
    }

    // 3. Xử lý nút Bắt đầu
    startBtn.addEventListener('click', () => {
        const mssv = mssvInput.value.trim();
        if (!mssv) return alert('Vui lòng nhập MSSV!');

        startBtn.disabled = true;
        startBtn.textContent = "Đang kiểm tra...";

        // Gửi gói tin LOGIN
        ws.send(JSON.stringify({
            type: 'LOGIN',
            mssv: mssv,
            fingerprint: visitorId
        }));
    });

    // 4. Hàm hiển thị câu hỏi (Mockup)
    function renderQuiz() {
        quizDiv.innerHTML = `
            <h2>Bài kiểm tra nhanh</h2>
            <p><strong>Câu 1:</strong> 1 + 1 bằng mấy?</p>
            <label class="quiz-option"><input type="radio" name="q1" value="A"> A. 1</label>
            <label class="quiz-option"><input type="radio" name="q1" value="B"> B. 2</label>
            <label class="quiz-option"><input type="radio" name="q1" value="C"> C. 3</label>
            <label class="quiz-option"><input type="radio" name="q1" value="D"> D. 4</label>
            <br>
            <button id="submitBtn">Nộp bài</button>
        `;

        document.getElementById('submitBtn').addEventListener('click', submitQuiz);
    }

    // 5. Hàm nộp bài
    function submitQuiz() {
        const checked = document.querySelector('input[name="q1"]:checked');
        if (!checked) return alert('Bạn chưa chọn đáp án!');

        // Gửi gói tin SUBMIT
        ws.send(JSON.stringify({
            type: 'SUBMIT',
            mssv: mssvInput.value.trim(),
            answer: checked.value
        }));

        // Chuyển sang màn hình kết quả
        quizDiv.style.display = 'none';
        resultDiv.style.display = 'block';
    }

    // Khởi chạy
    initWebSocket();
    initFingerprint();
})();