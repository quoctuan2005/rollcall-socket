// ===== EMITTER CODE (Lecturer) =====
let dataToEmit = [];
let currentBitIndex = 0;
let isEmitting = false;

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
    const userInput = $('dataToSend').value.trim();
    if (!userInput) {
        // Auto mode: refresh token right before emitting to avoid expiry.
        await generateBits();
    }

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

    generateBits();
});
