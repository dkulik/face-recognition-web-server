const statusEl = document.getElementById('status');
const localVideo = document.getElementById('localVideo');
const remoteImage = document.getElementById('remoteImage');

const canvas = document.createElement('canvas');
const ctx = canvas.getContext('2d', { alpha: false });

let uploadBusy = false;
let downloadBusy = false;
let lastObjectUrl = '';
const MAX_UPLOAD_WIDTH = 640;
const JPEG_QUALITY = 0.6;
const UPLOAD_INTERVAL_MS = 100;
const DOWNLOAD_INTERVAL_MS = 100;

function toJpegBlob() {
  return new Promise((resolve) => canvas.toBlob(resolve, 'image/jpeg', JPEG_QUALITY));
}

function getUserMediaCompat(constraints) {
  if (navigator.mediaDevices && typeof navigator.mediaDevices.getUserMedia === 'function') {
    return navigator.mediaDevices.getUserMedia(constraints);
  }

  const legacyGetUserMedia =
    navigator.getUserMedia ||
    navigator.webkitGetUserMedia ||
    navigator.mozGetUserMedia ||
    navigator.msGetUserMedia;

  if (typeof legacyGetUserMedia === 'function') {
    return new Promise((resolve, reject) => {
      legacyGetUserMedia.call(navigator, constraints, resolve, reject);
    });
  }

  const contextHint = window.isSecureContext
    ? ''
    : ' This page must be opened on https:// or localhost.';
  throw new Error(`getUserMedia is not available in this browser.${contextHint}`);
}

async function uploadFrame() {
  if (uploadBusy || !localVideo.videoWidth || !localVideo.videoHeight) {
    return;
  }

  uploadBusy = true;
  try {
    const scale = Math.min(1, MAX_UPLOAD_WIDTH / localVideo.videoWidth);
    canvas.width = Math.max(1, Math.round(localVideo.videoWidth * scale));
    canvas.height = Math.max(1, Math.round(localVideo.videoHeight * scale));
    ctx.drawImage(localVideo, 0, 0, canvas.width, canvas.height);

    const blob = await toJpegBlob();
    if (!blob) {
      return;
    }

    await fetch('/api/frame', {
      method: 'POST',
      headers: { 'Content-Type': 'image/jpeg' },
      body: blob,
      cache: 'no-store',
    });
  } catch (error) {
    statusEl.textContent = 'Upload error';
  } finally {
    uploadBusy = false;
  }
}

async function downloadFrame() {
  if (downloadBusy) {
    return;
  }

  downloadBusy = true;
  try {
    const response = await fetch('/api/frame', { cache: 'no-store' });
    if (response.status !== 200) {
      return;
    }

    const blob = await response.blob();
    const nextUrl = URL.createObjectURL(blob);
    remoteImage.src = nextUrl;

    if (lastObjectUrl) {
      URL.revokeObjectURL(lastObjectUrl);
    }
    lastObjectUrl = nextUrl;
  } catch (error) {
    statusEl.textContent = 'Download error';
  } finally {
    downloadBusy = false;
  }
}

async function start() {
  try {
    const stream = await getUserMediaCompat({ video: true, audio: false });
    localVideo.srcObject = stream;
    await localVideo.play();

    statusEl.textContent = 'Streaming through server';
    setInterval(uploadFrame, UPLOAD_INTERVAL_MS);
    setInterval(downloadFrame, DOWNLOAD_INTERVAL_MS);
  } catch (error) {
    statusEl.textContent = `Camera error: ${error?.message || error}`;
  }
}

start();
