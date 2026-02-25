#!/usr/bin/env python3
import argparse
import base64
import html
import os
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

try:
    import serial  # type: ignore
except Exception:
    print("Missing dependency: pyserial", file=sys.stderr)
    print("Install with: python3 -m pip install pyserial", file=sys.stderr)
    sys.exit(1)


BEGIN_RE = re.compile(r"USB_(IMAGE|AUDIO)_BEGIN.*bytes=(\d+).*b64=(\d+)")
END_RE = re.compile(r"USB_(IMAGE|AUDIO)_END")
BASE64_RE = re.compile(r"[A-Za-z0-9+/=]+")


def open_image(path: Path) -> None:
    if sys.platform == "darwin":
        subprocess.run(["open", str(path)], check=False)
    elif sys.platform.startswith("linux"):
        subprocess.run(["xdg-open", str(path)], check=False)
    elif sys.platform.startswith("win"):
        os.startfile(str(path))  # type: ignore[attr-defined]


def open_url(url: str) -> None:
    if sys.platform == "darwin":
        subprocess.run(["open", url], check=False)
    elif sys.platform.startswith("linux"):
        subprocess.run(["xdg-open", url], check=False)
    elif sys.platform.startswith("win"):
        os.startfile(url)  # type: ignore[attr-defined]


def refresh_preview_document(path: Path) -> None:
    if sys.platform != "darwin":
        return
    script = f'''
tell application "Preview"
    if it is running then
        repeat with d in documents
            try
                if (path of d as text) is equal to "{str(path)}" then
                    close d saving no
                    exit repeat
                end if
            end try
        end repeat
    end if
    open POSIX file "{str(path)}"
    activate
end tell
'''
    subprocess.run(["osascript", "-e", script], check=False)


def write_atomic(path: Path, data: bytes) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_bytes(data)
    tmp.replace(path)


def start_live_server(out_dir: Path, port: int) -> tuple[ThreadingHTTPServer, str]:
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == "/" or self.path.startswith("/index.html"):
                latest_rel = "latest.jpg"
                page = f"""<!doctype html>
<html>
<head><meta charset="utf-8"><title>PIR Live</title></head>
<body style="margin:0;background:#111;color:#eee;font-family:monospace">
<div style="padding:8px">Live image: {html.escape(str(out_dir / latest_rel))}</div>
<img id="img" src="/{latest_rel}" style="display:block;max-width:100vw;max-height:calc(100vh - 40px);margin:auto" />
<div style="padding:8px">Latest audio: {html.escape(str(out_dir / "latest.wav"))}</div>
<button id="enableAudio" style="margin:0 8px 8px 8px;padding:6px 10px">Enable Audio</button>
<audio id="aud" controls style="display:block;margin:0 8px 12px 8px;width:calc(100% - 16px)"></audio>
<script>
const img = document.getElementById('img');
const aud = document.getElementById('aud');
const enableAudioBtn = document.getElementById('enableAudio');
let lastWavMtime = 0;
let audioEnabled = false;
enableAudioBtn.addEventListener('click', async () => {{
  audioEnabled = true;
  try {{
    await aud.play();
  }} catch (_) {{}}
  enableAudioBtn.textContent = 'Audio Enabled';
}});
setInterval(() => {{
  img.src = '/{latest_rel}?t=' + Date.now();
}}, 400);
setInterval(async () => {{
  try {{
    const r = await fetch('/meta.json?t=' + Date.now(), {{cache: 'no-store'}});
    if (!r.ok) return;
    const meta = await r.json();
    if (meta.wav_mtime_ms && meta.wav_mtime_ms !== lastWavMtime) {{
      lastWavMtime = meta.wav_mtime_ms;
      aud.src = '/latest.wav?t=' + Date.now();
      aud.load();
      if (audioEnabled) {{
        aud.play().catch(() => {{}});
      }}
    }}
  }} catch (_) {{}}
}}, 700);
</script>
</body></html>"""
                data = page.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return

            if self.path.startswith("/latest.jpg"):
                p = out_dir / "latest.jpg"
                if not p.exists():
                    self.send_error(404, "latest.jpg not found yet")
                    return
                data = p.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Cache-Control", "no-store, max-age=0")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return

            if self.path.startswith("/latest.wav"):
                p = out_dir / "latest.wav"
                if not p.exists():
                    self.send_error(404, "latest.wav not found yet")
                    return
                data = p.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Cache-Control", "no-store, max-age=0")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return

            if self.path.startswith("/meta.json"):
                img = out_dir / "latest.jpg"
                wav = out_dir / "latest.wav"
                img_mtime = int(img.stat().st_mtime_ns / 1_000_000) if img.exists() else 0
                wav_mtime = int(wav.stat().st_mtime_ns / 1_000_000) if wav.exists() else 0
                body = (
                    "{"
                    f"\"img_mtime_ms\":{img_mtime},"
                    f"\"wav_mtime_ms\":{wav_mtime}"
                    "}"
                ).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Cache-Control", "no-store, max-age=0")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return

            self.send_error(404, "Not found")

        def log_message(self, fmt: str, *args) -> None:
            return

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, f"http://127.0.0.1:{port}/"


def main() -> int:
    parser = argparse.ArgumentParser(description="Serial monitor that decodes USB base64 images/audio")
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out", default="captures")
    parser.add_argument("--keep-all", action="store_true", help="Also keep timestamped image files")
    parser.add_argument("--no-open", action="store_true", help="Do not auto-open each image")
    parser.add_argument("--refresh-preview", action="store_true", help="Force Preview to reload latest.jpg each capture (macOS)")
    parser.add_argument("--live-web", action="store_true", help="Open one browser tab that auto-refreshes latest.jpg")
    parser.add_argument("--web-port", type=int, default=8765)
    args = parser.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    def open_serial() -> serial.Serial:
        while True:
            try:
                s = serial.Serial(args.port, args.baud, timeout=0.2)
                print(f"Listening on {args.port} @ {args.baud}, saving to {out_dir}")
                return s
            except Exception as exc:
                print(f"[serial] open failed ({exc}), retrying in 1s...", file=sys.stderr)
                time.sleep(1.0)

    ser = open_serial()

    web_server = None
    if args.live_web:
        web_server, url = start_live_server(out_dir, args.web_port)
        print(f"Live web viewer: {url}")
        if not args.no_open:
            open_url(url)

    collecting = False
    collecting_kind = ""
    b64_chunks = []
    expected_bytes = None
    expected_b64 = None
    latest_image_path = out_dir / "latest.jpg"
    latest_audio_path = out_dir / "latest.wav"
    opened_latest = False

    while True:
        try:
            raw = ser.readline()
        except Exception as exc:
            print(f"[serial] read error ({exc}), reconnecting...", file=sys.stderr)
            try:
                ser.close()
            except Exception:
                pass
            ser = open_serial()
            collecting = False
            collecting_kind = ""
            b64_chunks = []
            expected_bytes = None
            expected_b64 = None
            continue
        if not raw:
            continue

        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        print(line)

        if not collecting:
            m = BEGIN_RE.search(line)
            if m:
                collecting = True
                collecting_kind = m.group(1)
                b64_chunks = []
                expected_bytes = int(m.group(2))
                expected_b64 = int(m.group(3))
            continue

        m_end = END_RE.search(line)
        if m_end:
            if m_end.group(1) != collecting_kind:
                print(f"[decode] mismatched end marker for {m_end.group(1)} while collecting {collecting_kind}", file=sys.stderr)
                collecting = False
                collecting_kind = ""
                b64_chunks = []
                expected_bytes = None
                expected_b64 = None
                continue

            b64_data = "".join(b64_chunks)
            try:
                decoded = base64.b64decode(b64_data, validate=False)
            except Exception as exc:
                print(f"[decode] failed: {exc}", file=sys.stderr)
                collecting = False
                collecting_kind = ""
                b64_chunks = []
                continue

            is_image = collecting_kind == "IMAGE"
            suffix = "jpg" if is_image else "wav"
            latest_path = latest_image_path if is_image else latest_audio_path
            write_atomic(latest_path, decoded)

            ts_path = None
            if args.keep_all:
                ts = int(time.time() * 1000)
                ts_path = out_dir / f"{collecting_kind.lower()}_{ts}.{suffix}"
                ts_path.write_bytes(decoded)

            print(
                f"[saved] latest={latest_path}"
                + (f" ts={ts_path}" if ts_path else "")
                + " "
                f"(decoded={len(decoded)} bytes, expected={expected_bytes}, b64_len={len(b64_data)}, expected_b64={expected_b64})"
            )

            if is_image and not args.no_open and not args.live_web:
                if not opened_latest:
                    open_image(latest_image_path)
                    opened_latest = True
                elif args.refresh_preview:
                    refresh_preview_document(latest_image_path)

            collecting = False
            collecting_kind = ""
            b64_chunks = []
            expected_bytes = None
            expected_b64 = None
            continue

        # Keep only base64 content while in image block.
        tokens = BASE64_RE.findall(line)
        if tokens:
            b64_chunks.extend(tokens)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
