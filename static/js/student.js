// ===== RECEIVER CODE (Student) =====
let analyser = null;
let animationId = null;
let mediaStream = null;
let isReceiving = false;
let receivedBits = [];

// Some browsers/OSes allow only one tab/app to capture the mic at a time.
// Use a simple cross-tab lock to avoid "silent" failures.
const MIC_LOCK_KEY = 'rollcall_mic_lock_v1';
const MIC_LOCK_TTL_MS = 2 * 60 * 1000;
const TAB_ID = (typeof crypto !== 'undefined' && crypto.randomUUID) ? crypto.randomUUID() : String(Math.random()).slice(2);

function nowMs() {
    return Date.now();
}

function readMicLock() {
    try {
        const raw = localStorage.getItem(MIC_LOCK_KEY);
        if (!raw) return null;
        const obj = JSON.parse(raw);
        if (!obj || typeof obj !== 'object') return null;
        if (!obj.ts || typeof obj.ts !== 'number') return null;
        if (nowMs() - obj.ts > MIC_LOCK_TTL_MS) return null;
        return obj;
    } catch {
        return null;
    }
}

function canAcquireMicLock() {
    const lock = readMicLock();
    if (!lock) return true;
    return lock.tabId === TAB_ID;
}

function acquireMicLock() {
    localStorage.setItem(MIC_LOCK_KEY, JSON.stringify({ tabId: TAB_ID, ts: nowMs() }));
}

function releaseMicLock() {
    const lock = readMicLock();
    if (lock && lock.tabId === TAB_ID) {
        localStorage.removeItem(MIC_LOCK_KEY);
    }
}

let lastAuthToken = '';

async function refreshPasskeyStatus() {
    const studentId = String(($('studentId')?.value || '')).trim();
    if (!studentId) {
        setPasskeyStatus('Chưa xác thực', true);
        return;
    }
    try {
        const resp = await fetch(`/api/webauthn/status?student_id=${encodeURIComponent(studentId)}`);
        const data = await resp.json().catch(() => null);
        if (!resp.ok || !data || !data.ok) {
            setPasskeyStatus('Không kiểm tra được trạng thái Passkey', false);
            return;
        }
        if (data.registered) {
            setPasskeyStatus('✅ Đã đăng ký Passkey (hãy bấm Xác Thực khi điểm danh)', false);
        } else {
            const rp = data.rp_id ? String(data.rp_id) : '';
            setPasskeyStatus(`⚠️ Chưa đăng ký Passkey${rp ? ` (rp_id=${rp})` : ''}`, false);
        }
    } catch (e) {
        console.warn('refreshPasskeyStatus error:', e);
        setPasskeyStatus('Không kiểm tra được trạng thái Passkey', false);
    }
}

function setPasskeyStatus(text, isEmpty) {
    const el = $('passkeyStatus');
    if (!el) return;
    el.textContent = text;
    if (isEmpty) el.classList.add('empty');
    else el.classList.remove('empty');
}

function formatWebAuthnError(e) {
    if (!e) return 'Unknown error';
    const name = String(e.name || (e.constructor && e.constructor.name) || 'Error');
    const msg = String(e.message || e.toString() || '');

    let hint = '';
    if (name === 'NotAllowedError') {
        hint = ' (bạn đã hủy/timeout; thử lại và đảm bảo FaceID/TouchID/Passcode đang bật)';
    } else if (name === 'SecurityError') {
        hint = ' (origin/rpId không hợp lệ; trên Android thường KHÔNG cho dùng IP trực tiếp. Hãy dùng domain như <ip>.sslip.io hoặc <ip-dash>.nip.io và cert phải trusted)';
    } else if (name === 'InvalidStateError') {
        hint = ' (thiết bị có thể đã đăng ký Passkey cho tài khoản này; thử “Xác Thực” hoặc đổi student_id)';
    } else if (name === 'AbortError') {
        hint = ' (bị hủy giữa chừng)';
    }

    // Some browsers give unhelpful messages; include origin + secure-context for debugging.
    const origin = (typeof location !== 'undefined' && location.origin) ? ` | origin=${location.origin}` : '';
    const sc = (typeof window !== 'undefined') ? ` | secureContext=${String(!!window.isSecureContext)}` : '';
    return `${name}: ${msg}${hint}${origin}${sc}`;
}

function bufferToBase64Url(buf) {
    const bytes = new Uint8Array(buf);
    let binary = '';
    for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    const b64 = btoa(binary);
    return b64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/g, '');
}

function base64UrlToBuffer(s) {
    const b64 = String(s).replace(/-/g, '+').replace(/_/g, '/');
    const pad = b64.length % 4 ? '='.repeat(4 - (b64.length % 4)) : '';
    const binary = atob(b64 + pad);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes.buffer;
}

function preparePublicKeyForCreate(pk) {
    const out = { ...pk };
    out.challenge = base64UrlToBuffer(pk.challenge);
    out.user = { ...pk.user, id: base64UrlToBuffer(pk.user.id) };
    if (Array.isArray(pk.excludeCredentials)) {
        out.excludeCredentials = pk.excludeCredentials.map((c) => ({ ...c, id: base64UrlToBuffer(c.id) }));
    }
    return out;
}

function preparePublicKeyForGet(pk) {
    const out = { ...pk };
    out.challenge = base64UrlToBuffer(pk.challenge);
    if (Array.isArray(pk.allowCredentials)) {
        out.allowCredentials = pk.allowCredentials.map((c) => ({ ...c, id: base64UrlToBuffer(c.id) }));
    }
    return out;
}

function credentialToJSON(cred) {
    if (!cred) return null;
    return {
        id: cred.id,
        type: cred.type,
        rawId: bufferToBase64Url(cred.rawId),
        response: {
            clientDataJSON: bufferToBase64Url(cred.response.clientDataJSON),
            // create()
            attestationObject: cred.response.attestationObject ? bufferToBase64Url(cred.response.attestationObject) : undefined,
            // get()
            authenticatorData: cred.response.authenticatorData ? bufferToBase64Url(cred.response.authenticatorData) : undefined,
            signature: cred.response.signature ? bufferToBase64Url(cred.response.signature) : undefined,
            userHandle: cred.response.userHandle ? bufferToBase64Url(cred.response.userHandle) : undefined,
        },
    };
}

async function registerPasskey() {
    try {
        const studentId = String(($('studentId')?.value || '')).trim();
        if (!studentId) {
            alert('⚠️ Vui lòng nhập mã số sinh viên trước.');
            return;
        }
        if (!window.PublicKeyCredential || !navigator.credentials) {
            alert('❌ Trình duyệt/thiết bị không hỗ trợ Passkeys (WebAuthn).');
            return;
        }

        setPasskeyStatus('Đang tạo yêu cầu đăng ký...', false);

        const beginResp = await fetch('/api/webauthn/register/begin', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ student_id: studentId }),
        });
        const beginData = await beginResp.json().catch(() => null);
        if (!beginResp.ok || !beginData || !beginData.publicKey) {
            const err = beginData && beginData.error ? String(beginData.error) : `HTTP ${beginResp.status}`;
            setPasskeyStatus(`❌ Không thể bắt đầu đăng ký: ${err}`, false);
            return;
        }

        const publicKey = preparePublicKeyForCreate(beginData.publicKey);
        setPasskeyStatus('Hãy xác thực sinh trắc học để đăng ký...', false);
        const cred = await navigator.credentials.create({ publicKey });
        if (!cred) {
            setPasskeyStatus('❌ Đăng ký bị hủy (credential=null)', false);
            return;
        }

        const finishResp = await fetch('/api/webauthn/register/finish', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ student_id: studentId, credential: credentialToJSON(cred) }),
        });
        const finishData = await finishResp.json().catch(() => null);
        if (!finishResp.ok || !finishData || !finishData.ok) {
            const err = finishData && finishData.error ? String(finishData.error) : `HTTP ${finishResp.status}`;
            const detailType = finishData && finishData.detail_type ? String(finishData.detail_type) : '';
            const detail = finishData && finishData.detail ? String(finishData.detail) : '';
            const extra = detail ? ` (${detailType || 'detail'}: ${detail})` : '';
            setPasskeyStatus(`❌ Đăng ký thất bại: ${err}${extra}`, false);
            return;
        }

        setPasskeyStatus('✅ Đăng ký Passkey thành công', false);
        await refreshPasskeyStatus();
        
        // After successful registration, need to authenticate
        console.log('[Pipeline] Registration done - you can now authenticate to proceed');
    } catch (e) {
        console.error('registerPasskey error:', e);
        setPasskeyStatus(`❌ ${formatWebAuthnError(e)}`, false);
    }
}

async function authPasskey() {
    try {
        const studentId = String(($('studentId')?.value || '')).trim();
        if (!studentId) {
            alert('⚠️ Vui lòng nhập mã số sinh viên trước.');
            return;
        }
        if (!window.PublicKeyCredential || !navigator.credentials) {
            alert('❌ Trình duyệt/thiết bị không hỗ trợ Passkeys (WebAuthn).');
            return;
        }

        setPasskeyStatus('Đang yêu cầu xác thực...', false);

        const beginResp = await fetch('/api/webauthn/auth/begin', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ student_id: studentId }),
        });
        const beginData = await beginResp.json().catch(() => null);
        if (!beginResp.ok || !beginData || !beginData.publicKey) {
            const err = beginData && beginData.error ? String(beginData.error) : `HTTP ${beginResp.status}`;
            if (err === 'not_registered') {
                setPasskeyStatus('⚠️ Chưa đăng ký Passkey. Hãy bấm “Đăng Ký Passkey” (chỉ 1 lần cho mỗi host).', false);
            } else {
                setPasskeyStatus(`❌ Không thể bắt đầu xác thực: ${err}`, false);
            }
            return;
        }

        const publicKey = preparePublicKeyForGet(beginData.publicKey);
        setPasskeyStatus('Hãy xác thực sinh trắc học để điểm danh...', false);
        const cred = await navigator.credentials.get({ publicKey });
        if (!cred) {
            setPasskeyStatus('❌ Xác thực bị hủy (credential=null)', false);
            return '';
        }

        const finishResp = await fetch('/api/webauthn/auth/finish', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ student_id: studentId, credential: credentialToJSON(cred) }),
        });
        const finishData = await finishResp.json().catch(() => null);
        if (!finishResp.ok || !finishData || !finishData.ok || !finishData.auth_token) {
            const err = finishData && finishData.error ? String(finishData.error) : `HTTP ${finishResp.status}`;
            const detailType = finishData && finishData.detail_type ? String(finishData.detail_type) : '';
            const detail = finishData && finishData.detail ? String(finishData.detail) : '';
            const extra = detail ? ` (${detailType || 'detail'}: ${detail})` : '';
            setPasskeyStatus(`❌ Xác thực thất bại: ${err}${extra}`, false);
            return;
        }

        lastAuthToken = String(finishData.auth_token);
        setPasskeyStatus('✅ Đã xác thực Passkey (có hiệu lực ngắn)', false);
        
        // Notify pipeline that authentication succeeded
        if (typeof onPasskeyAuthenticated === 'function') {
            onPasskeyAuthenticated();
        }
        
        return lastAuthToken;
    } catch (e) {
        console.error('authPasskey error:', e);
        setPasskeyStatus(`❌ ${formatWebAuthnError(e)}`, false);
        
        // Notify pipeline that authentication failed
        if (typeof onPasskeyAuthFailed === 'function') {
            onPasskeyAuthFailed();
        }
        
        return '';
    }
}

async function startReceiving() {
    try {
        if (!canAcquireMicLock()) {
            alert('⚠️ Microphone đang được dùng bởi tab/cửa sổ khác.\n\nHãy quay lại tab đang nhận và bấm “Dừng”, hoặc đóng tab đó rồi thử lại.');
            return;
        }

        acquireMicLock();

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
        const name = error && error.name ? String(error.name) : 'Error';
        const msg = error && error.message ? String(error.message) : String(error);
        const hint = (name === 'NotReadableError' || name === 'AbortError')
            ? '\n\nGợi ý: mic có thể đang bị tab/app khác chiếm dụng. Hãy đóng tab khác hoặc dừng mic ở tab đó.'
            : '\n\nVui lòng thử reload trang và cho phép quyền truy cập microphone.';
        alert(`❌ ${name}: ${msg}${hint}`);
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

    releaseMicLock();
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

        // Layer 3: require Passkey auth before sending attendance
        if (!lastAuthToken) {
            const t = await authPasskey();
            if (!t) {
                if (statusEl) {
                    statusEl.textContent = '❌ Cần xác thực Passkey trước khi gửi.';
                    statusEl.classList.remove('empty');
                }
                return;
            }
        }

        const resp = await fetch('/api/attendance/submit', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                student_id: studentId,
                auth_token: lastAuthToken,
                bits,
                // Layer 2 fingerprint (flat keys)
                fp_platform: String(navigator.platform || ''),
                fp_tz: String(Intl.DateTimeFormat().resolvedOptions().timeZone || ''),
                fp_lang: String(navigator.language || ''),
                fp_sw: Number(window.screen?.width || 0),
                fp_sh: Number(window.screen?.height || 0),
                fp_dpr: Number(window.devicePixelRatio || 0),
                fp_hc: Number(navigator.hardwareConcurrency || 0),
                fp_dm: Number(navigator.deviceMemory || 0),
                fp_touch: Boolean((navigator.maxTouchPoints || 0) > 0),
            }),
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
                const extra = err === 'invalid_token' ? ` (bits_len=${bits.length})` : '';
                statusEl.textContent = `❌ Thất bại: ${err}${extra}`;
                statusEl.classList.remove('empty');
            }
            // Keep auth_token for quick retry unless server says Passkey is required.
            if (err === 'webauthn_required') lastAuthToken = '';
            return;
        }

        if (statusEl) {
            const fpStatus = data && data.fingerprint_status ? String(data.fingerprint_status) : '';
            const fpScore = data && data.fingerprint_score != null ? String(data.fingerprint_score) : '';
            statusEl.textContent = fpStatus ? `✅ Điểm danh thành công! (${fpStatus}, ${fpScore}%)` : '✅ Điểm danh thành công!';
            statusEl.classList.remove('empty');
        }

        // Consume the auth token after a successful submit
        lastAuthToken = '';
    } catch (e) {
        console.error('submitAttendance error:', e);
        const statusEl = $('submitStatus');
        if (statusEl) {
            statusEl.textContent = '❌ Lỗi khi gửi điểm danh.';
            statusEl.classList.remove('empty');
        }
        lastAuthToken = '';
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

    const sidEl = $('studentId');
    if (sidEl) {
        const saved = localStorage.getItem('rollcall_student_id') || '';
        if (!sidEl.value && saved) sidEl.value = saved;
        sidEl.addEventListener('input', () => {
            localStorage.setItem('rollcall_student_id', String(sidEl.value || ''));
            // Clear any previous auth token when identity changes
            lastAuthToken = '';
        });
        sidEl.addEventListener('change', () => {
            refreshPasskeyStatus();
        });
    }

    setPasskeyStatus('Chưa xác thực', true);
    refreshPasskeyStatus();
});

window.addEventListener('beforeunload', () => {
    try {
        stopReceiving();
    } catch {
        releaseMicLock();
    }
});
