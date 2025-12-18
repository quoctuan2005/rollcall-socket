(() => {
    const statusEl = document.getElementById('status');
    const startBtn = document.getElementById('startBtn');
    const mssvInput = document.getElementById('mssv');
    const loginDiv = document.getElementById('login');
    const quizDiv = document.getElementById('quiz');

    let visitorId = null;
    let ws = null;
    let wsReady = false;

    function log(msg) {
        const time = new Date().toLocaleTimeString();
        statusEl.textContent += `[${time}] ${msg}\n`;
        statusEl.scrollTop = statusEl.scrollHeight;
    }

    function initWebSocket() {
        try {
            ws = new WebSocket('ws://localhost:8080');
        } catch (e) {
            log('Không thể khởi tạo WebSocket: ' + e.message);
            return;
        }

        ws.onopen = () => {
            wsReady = true;
            log('WebSocket đã kết nối.');
            updateStartBtn();
        };
        ws.onclose = (ev) => {
            wsReady = false;
            log(`WebSocket đã đóng (code=${ev.code}).`);
            updateStartBtn();
        };
        ws.onerror = (e) => {
            log('Lỗi WebSocket: xem console.');
            console.error(e);
        };
        ws.onmessage = (ev) => {
            let data = {};
            try { data = JSON.parse(ev.data); } catch (_) { }
            if (data && data.status) {
                if (data.status === 'BLOCKED') {
                    alert('Gian lận thiết bị! Giảng viên đã được báo cáo.');
                    log('BLOCKED: ' + (data.msg || ''));
                } else if (data.status === 'OK') {
                    log('Xác thực thiết bị thành công.');
                    loginDiv.style.display = 'none';
                    quizDiv.style.display = 'block';
                } else {
                    log('Trạng thái không xác định: ' + data.status);
                }
            } else {
                log('Dữ liệu nhận được: ' + ev.data);
            }
        };
    }

    function updateStartBtn() {
        startBtn.disabled = !(visitorId && wsReady);
    }

    function waitForFPJS() {
        return new Promise((resolve) => {
            if (window.FingerprintJS) return resolve();
            log('FingerprintJS chưa sẵn sàng.');
            const check = () => {
                if (window.FingerprintJS) resolve();
                else setTimeout(check, 150);
            };
            check();
        });
    }

    async function initFingerprint() {
        try {
            await waitForFPJS();
            const fp = await FingerprintJS.load();
            const result = await fp.get();
            visitorId = result.visitorId;
            log('Fingerprint sẵn sàng: ' + visitorId);
            updateStartBtn();
        } catch (e) {
            log('Lỗi FingerprintJS: ' + e.message);
        }
    }

    startBtn.addEventListener('click', () => {
        startBtn.disabled = true;
        const mssv = (mssvInput.value || '').trim();
        if (!mssv) {
            alert('Vui lòng nhập MSSV.');
            return;
        }
        if (!ws || !wsReady) {
            alert('WebSocket chưa kết nối.');
            return;
        }
        if (!visitorId) {
            alert('Fingerprint chưa sẵn sàng.');
            return;
        }
        const payload = {
            type: 'LOGIN',
            mssv,
            fingerprint: visitorId,
            // For server compatibility (server.cpp expects visitorId + mssv)
            visitorId
        };
        ws.send(JSON.stringify(payload));
        log('Đã gửi LOGIN: ' + JSON.stringify(payload));
    });

    // Boot
    initWebSocket();
    // Initialize fingerprint after DOM is ready; the script tags are deferred
    window.addEventListener('DOMContentLoaded', initFingerprint);
})();
