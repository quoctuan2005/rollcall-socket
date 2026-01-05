// ===== AUDIO CONTEXT (shared) =====
let audioContext = null;

function initAudioContext() {
    if (!audioContext) {
        audioContext = new (window.AudioContext || window.webkitAudioContext)();
    }
    return audioContext;
}

function $(id) {
    return document.getElementById(id);
}
