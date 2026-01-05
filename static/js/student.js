// ===== RECEIVER CODE (Student) =====
let analyser = null;
let animationId = null;
let mediaStream = null;
let isReceiving = false;
let receivedBits = [];

async function startReceiving() {
    try {
        mediaStream = await navigator.mediaDevices.getUserMedia({
            audio: {
                echoCancellation: false,
                noiseSuppression: false,
                autoGainControl: false,
            },
        });

        const ctx = initAudioContext();

        if (ctx.state === 'suspended') {
            await ctx.resume();
        }

        let source;
        if (typeof ctx.createMediaStreamAudioSource === 'function') {
            source = ctx.createMediaStreamAudioSource(mediaStream);
        } else if (typeof ctx.createMediaStreamSource === 'function') {
            source = ctx.createMediaStreamSource(mediaStream);
        } else {
            throw new Error('AudioContext không hỗ trợ Media Stream');
        }

        analyser = ctx.createAnalyser();
        analyser.fftSize = 4096;

        source.connect(analyser);

        isReceiving = true;
        receivedBits = [];
        $('recvBtn').disabled = true;
        $('stopRecvBtn').disabled = false;
        $('receiverStatus').classList.add('active');
        $('receiverStatusText').textContent = 'Đang nhận...';
        $('receivedBits').textContent = '';
        $('receivedBits').classList.add('empty');

        startFrequencyDetection();
    } catch (error) {
        console.error('Lỗi:', error);
        alert('❌ Lỗi: ' + error.message + '\n\nVui lòng thử reload trang và cho phép quyền truy cập microphone.');
        stopReceiving();
    }
}

function startFrequencyDetection() {
    const ctx = initAudioContext();
    const bufferLength = analyser.frequencyBinCount;
    const dataArray = new Uint8Array(bufferLength);
    const freq0 = parseInt($('recvFreq0').value);
    const freq1 = parseInt($('recvFreq1').value);
    const sensitivity = parseFloat($('sensitivity').value);

    // Reduce lingering energy between frames (helps prevent duplicate detections)
    analyser.smoothingTimeConstant = 0;

    let lastSampleTime = 0;
    // Add a small margin because the emitter has scheduling overhead (~50ms)
    const detectionInterval =
        parseInt($('duration').value) + parseInt($('bitDelay').value) + 80;

    const detect = () => {
        if (!isReceiving) return;

        analyser.getByteFrequencyData(dataArray);

        const binWidth = ctx.sampleRate / analyser.fftSize;
        const bin0 = Math.round(freq0 / binWidth);
        const bin1 = Math.round(freq1 / binWidth);

        const energy0 = dataArray[bin0] / 255;
        const energy1 = dataArray[bin1] / 255;

        const currentTime = Date.now();

        // Per-frame detection result
        let detectedBit = null;
        let detectedFreq = null;

        // Sample at a fixed cadence to avoid counting the same long tone multiple times.
        if (currentTime - lastSampleTime >= detectionInterval) {
            if (energy0 > sensitivity && energy0 > energy1) {
                detectedBit = '0';
                detectedFreq = freq0;
            } else if (energy1 > sensitivity && energy1 > energy0) {
                detectedBit = '1';
                detectedFreq = freq1;
            }

            if (detectedBit !== null) {
                receivedBits.push(detectedBit);
                $('receivedBits').textContent = receivedBits.join('');
                $('receivedBits').classList.remove('empty');
            }

            // Always advance the sample clock, even if no bit was detected,
            // otherwise the sampler can drift and later "burst" multiple detections.
            lastSampleTime = currentTime;
        }

        if (detectedFreq !== null) {
            $('detectedFrequency').textContent =
                `${detectedFreq} Hz (0: ${energy0.toFixed(2)}, 1: ${energy1.toFixed(2)})`;
            $('detectedFrequency').classList.remove('empty');
        } else {
            $('detectedFrequency').textContent =
                `- (0: ${energy0.toFixed(2)}, 1: ${energy1.toFixed(2)})`;
            $('detectedFrequency').classList.add('empty');
        }

        animationId = requestAnimationFrame(detect);
    };

    detect();
}

function stopReceiving() {
    isReceiving = false;
    if (mediaStream) {
        mediaStream.getTracks().forEach((track) => track.stop());
        mediaStream = null;
    }
    if (animationId) {
        cancelAnimationFrame(animationId);
    }
    $('recvBtn').disabled = false;
    $('stopRecvBtn').disabled = true;
    $('receiverStatus').classList.remove('active');
    $('receiverStatusText').textContent = 'Sẵn sàng';
}

function clearReceivedBits() {
    receivedBits = [];
    $('receivedBits').textContent = 'Chờ dữ liệu...';
    $('receivedBits').classList.add('empty');
    $('detectedFrequency').textContent = '-';
    $('detectedFrequency').classList.add('empty');

    if ($('submitStatus')) {
        $('submitStatus').textContent = '-';
        $('submitStatus').classList.add('empty');
    }
}

async function submitAttendance() {
    try {
        const studentIdEl = $('studentId');
        const statusEl = $('submitStatus');
        const bits = receivedBits.join('');
        const studentId = (studentIdEl?.value || '').trim();

        if (!studentId) {
            alert('⚠️ Vui lòng nhập mã số sinh viên.');
            return;
        }
        if (!bits) {
            alert('⚠️ Chưa có dữ liệu bit để gửi. Hãy bắt đầu nhận trước.');
            return;
        }

        if (statusEl) {
            statusEl.textContent = 'Đang gửi...';
            statusEl.classList.remove('empty');
        }

        const resp = await fetch('/api/attendance/submit', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ student_id: studentId, bits }),
        });

        let data = null;
        try {
            data = await resp.json();
        } catch {
            // ignore
        }

        if (!resp.ok) {
            const err = (data && data.error) ? String(data.error) : `HTTP ${resp.status}`;
            if (statusEl) {
                statusEl.textContent = `❌ Thất bại: ${err}`;
                statusEl.classList.remove('empty');
            }
            return;
        }

        if (statusEl) {
            statusEl.textContent = '✅ Điểm danh thành công!';
            statusEl.classList.remove('empty');
        }
    } catch (e) {
        console.error('submitAttendance error:', e);
        const statusEl = $('submitStatus');
        if (statusEl) {
            statusEl.textContent = '❌ Lỗi khi gửi điểm danh.';
            statusEl.classList.remove('empty');
        }
    }
}

// ===== EVENT LISTENERS =====
window.addEventListener('load', () => {
    $('recvFreq0').addEventListener('input', (e) => {
        $('recvFreq0Display').textContent = e.target.value + ' Hz';
    });

    $('recvFreq1').addEventListener('input', (e) => {
        $('recvFreq1Display').textContent = e.target.value + ' Hz';
    });

    $('sensitivity').addEventListener('input', (e) => {
        $('sensitivityDisplay').textContent = parseFloat(e.target.value).toFixed(2);
    });

    $('duration').addEventListener('input', (e) => {
        $('durationDisplay').textContent = e.target.value + ' ms';
    });

    $('bitDelay').addEventListener('input', (e) => {
        $('bitDelayDisplay').textContent = e.target.value + ' ms';
    });
});
