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

    let lastDetectedTime = 0;
    const detectionInterval =
        parseInt($('duration').value) + parseInt($('bitDelay').value);

    const detect = () => {
        if (!isReceiving) return;

        analyser.getByteFrequencyData(dataArray);

        const binWidth = ctx.sampleRate / analyser.fftSize;
        const bin0 = Math.round(freq0 / binWidth);
        const bin1 = Math.round(freq1 / binWidth);

        const energy0 = dataArray[bin0] / 255;
        const energy1 = dataArray[bin1] / 255;

        const currentTime = Date.now();

        if (currentTime - lastDetectedTime > detectionInterval) {
            let detectedBit = null;
            let detectedFreq = null;

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
                lastDetectedTime = currentTime;

                $('detectedFrequency').textContent =
                    `${detectedFreq} Hz (0: ${energy0.toFixed(2)}, 1: ${energy1.toFixed(2)})`;
            }
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
