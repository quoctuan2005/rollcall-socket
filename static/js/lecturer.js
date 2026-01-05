// ===== EMITTER CODE (Lecturer) =====
let dataToEmit = [];
let currentBitIndex = 0;
let isEmitting = false;

function updateSessionInfoText(data) {
    const el = $('sessionInfo');
    if (!el) return;

    const sessionId = data && data.session_id ? String(data.session_id) : '';
    const ttl = data && data.ttl_ms != null ? String(data.ttl_ms) : '';
    const counter = data && data.counter != null ? String(data.counter) : '';

    if (!sessionId) {
        el.textContent = 'Chưa khởi tạo';
        el.classList.add('empty');
        return;
    }

    el.textContent = `session_id=${sessionId} | ttl_ms=${ttl || '?'} | counter=${counter || '?'}`;
    el.classList.remove('empty');
}

async function startSession() {
    try {
        const resp = await fetch('/api/session/start', { method: 'POST' });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const data = await resp.json();
        updateSessionInfoText(data);
        await refreshAttendance();
        await generateBits();
    } catch (e) {
        console.error('startSession error:', e);
        alert('❌ Không thể bắt đầu buổi điểm danh. Hãy kiểm tra backend/Python server đang chạy.');
    }
}

function setAttendanceUI({ countText, listText, isEmpty }) {
    const countEl = $('attendanceCount');
    const listEl = $('attendanceList');
    if (countEl) {
        countEl.textContent = countText;
        if (isEmpty) countEl.classList.add('empty');
        else countEl.classList.remove('empty');
    }
    if (listEl) {
        listEl.textContent = listText;
        if (isEmpty) listEl.classList.add('empty');
        else listEl.classList.remove('empty');
    }
}

async function refreshAttendance() {
    try {
        const resp = await fetch('/api/attendance/list');
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const data = await resp.json();

        const attendees = Array.isArray(data.attendees) ? data.attendees : [];
        const count = typeof data.count === 'number' ? data.count : attendees.length;

        if (!attendees.length) {
            setAttendanceUI({ countText: String(count), listText: 'Chưa có dữ liệu...', isEmpty: true });
            return;
        }

        const lines = attendees.map((e, idx) => {
            const sid = e && e.student_id ? String(e.student_id) : '?';
            const at = e && typeof e.at_ms === 'number' ? new Date(e.at_ms) : null;
            const time = at ? at.toLocaleTimeString('vi-VN') : '-';
            return `${idx + 1}. ${sid} (${time})`;
        });

        setAttendanceUI({ countText: String(count), listText: lines.join('\n'), isEmpty: false });
    } catch (e) {
        console.error('refreshAttendance error:', e);
        setAttendanceUI({ countText: '—', listText: 'Không tải được danh sách. Kiểm tra backend đang chạy.', isEmpty: true });
    }
}

async function generateBits() {
    const userInput = $('dataToSend').value.trim();

    if (userInput) {
        if (!/^[01]+$/.test(userInput)) {
            alert('❌ Vui lòng nhập chỉ các chữ số 0 và 1');
            return;
        }
        dataToEmit = userInput.split('');
    } else {
        const count = parseInt($('bitCount').value) || 8;
        try {
            const resp = await fetch(`/api/token?bits=${encodeURIComponent(count)}`);
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            const data = await resp.json();
            const bits = String(data.bits || '');
            if (!/^[01]+$/.test(bits)) throw new Error('Invalid bits from server');
            updateSessionInfoText(data);
            dataToEmit = bits.split('');
        } catch (e) {
            console.warn('Fallback to local random bits:', e);
            dataToEmit = Array.from({ length: count }, () => (Math.random() < 0.5 ? '0' : '1'));
        }
    }

    $('generatedBits').textContent = dataToEmit.join('');
    $('generatedBits').classList.remove('empty');
}

function getFrequency(bit) {
    if (bit === '0') {
        return parseInt($('freq0').value);
    }
    return parseInt($('freq1').value);
}

async function emitBit(bit) {
    const ctx = initAudioContext();
    const freq = getFrequency(bit);
    const duration = parseInt($('duration').value) / 1000;
    const amplitude = parseFloat($('amplitude').value);

    const osc = ctx.createOscillator();
    const gain = ctx.createGain();

    osc.frequency.value = freq;
    osc.type = 'sine';

    gain.gain.setValueAtTime(amplitude, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + duration);

    osc.connect(gain);
    gain.connect(ctx.destination);

    osc.start(ctx.currentTime);
    osc.stop(ctx.currentTime + duration);

    return new Promise((resolve) => {
        setTimeout(resolve, duration * 1000 + 50);
    });
}

async function startEmitting() {
    // Always sync data right before emitting.
    // - If input is non-empty: uses the typed bits.
    // - If input is empty: fetches a fresh token from backend.
    await generateBits();

    if (dataToEmit.length === 0) {
        alert('⚠️ Hãy tạo dữ liệu trước!');
        return;
    }

    isEmitting = true;
    currentBitIndex = 0;

    $('emitBtn').disabled = true;
    $('stopEmitBtn').disabled = false;
    $('emitterStatus').classList.add('active');
    $('emitterStatusText').textContent = 'Đang phát...';

    const bitDelay = parseInt($('bitDelay').value);

    while (isEmitting && currentBitIndex < dataToEmit.length) {
        const bit = dataToEmit[currentBitIndex];
        await emitBit(bit);

        if (bitDelay > 0) {
            await new Promise((resolve) => setTimeout(resolve, bitDelay));
        }

        currentBitIndex++;
    }

    stopEmitting();
}

function stopEmitting() {
    isEmitting = false;
    $('emitBtn').disabled = false;
    $('stopEmitBtn').disabled = true;
    $('emitterStatus').classList.remove('active');
    $('emitterStatusText').textContent = 'Sẵn sàng';
}

// ===== EVENT LISTENERS =====
window.addEventListener('load', () => {
    $('freq0').addEventListener('input', (e) => {
        $('freq0Display').textContent = e.target.value + ' Hz';
    });

    $('freq1').addEventListener('input', (e) => {
        $('freq1Display').textContent = e.target.value + ' Hz';
    });

    $('duration').addEventListener('input', (e) => {
        $('durationDisplay').textContent = e.target.value + ' ms';
    });

    $('amplitude').addEventListener('input', (e) => {
        $('amplitudeDisplay').textContent = parseFloat(e.target.value).toFixed(2);
    });

    $('bitDelay').addEventListener('input', (e) => {
        $('bitDelayDisplay').textContent = e.target.value + ' ms';
    });

    // Initialize session display (no reset) if backend is reachable.
    fetch('/api/session')
        .then((r) => (r.ok ? r.json() : null))
        .then((data) => updateSessionInfoText(data))
        .catch(() => { });

    refreshAttendance();

    generateBits();
});
