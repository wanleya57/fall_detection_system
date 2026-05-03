#!/usr/bin/env python3
"""
pose_detect.py - YOLOv8n-pose inference subprocess (memory-optimized)
"""
import sys
import json
import gc
import traceback
import numpy as np

def log(msg):
    sys.stderr.write(f"[pose_detect] {msg}\n")
    sys.stderr.flush()

def main():
    log("Starting...")

    try:
        from ultralytics import YOLO
    except ImportError as e:
        log(f"FATAL: Cannot import ultralytics: {e}")
        sys.stdout.write("READY\n")
        sys.stdout.flush()
        return

    model_path = "yolov8n-pose.pt"
    log(f"Loading model: {model_path}")

    try:
        model = YOLO(model_path)
        log("Model loaded successfully")
    except Exception as e:
        log(f"FATAL: Model load failed: {e}")
        sys.stdout.write("READY\n")
        sys.stdout.flush()
        return

    sys.stdout.write("READY\n")
    sys.stdout.flush()
    log("Sent READY, entering main loop")

    frame_count = 0

    while True:
        try:
            # Read binary header: [4B width][4B height]
            hdr = sys.stdin.buffer.read(8)
            if not hdr or len(hdr) < 8:
                log("stdin closed, exiting")
                break

            import struct
            w, h = struct.unpack('<II', hdr)
            frame_size = w * h * 3 // 2

            yuv = b""
            while len(yuv) < frame_size:
                chunk = sys.stdin.buffer.read(frame_size - len(yuv))
                if not chunk:
                    log("stdin EOF mid-frame")
                    return
                yuv += chunk

            frame_count += 1

            # YUV -> RGB (in-place where possible to reduce memory)
            yuv_arr = np.frombuffer(yuv, dtype=np.uint8)
            frame_len = w * h
            uv_size = (w // 2) * (h // 2)

            # Extract Y, U, V as float32 views
            y_f = yuv_arr[:frame_len].astype(np.float32).reshape(h, w)
            u_half = yuv_arr[frame_len:frame_len + uv_size].astype(np.float32).reshape(h // 2, w // 2)
            v_half = yuv_arr[frame_len + uv_size:frame_len + 2 * uv_size].astype(np.float32).reshape(h // 2, w // 2)

            # Upsample using repeat (faster than kron, less memory)
            u_up = np.repeat(np.repeat(u_half, 2, axis=0), 2, axis=1)[:h, :w]
            v_up = np.repeat(np.repeat(v_half, 2, axis=0), 2, axis=1)[:h, :w]

            # Free half-size arrays early
            del u_half, v_half

            # Compute RGB
            rgb = np.empty((h, w, 3), dtype=np.uint8)
            rgb[:, :, 0] = np.clip(y_f + 1.402 * (v_up - 128.0), 0, 255).astype(np.uint8)
            rgb[:, :, 1] = np.clip(y_f - 0.344136 * (u_up - 128.0) - 0.714136 * (v_up - 128.0), 0, 255).astype(np.uint8)
            rgb[:, :, 2] = np.clip(y_f + 1.772 * (u_up - 128.0), 0, 255).astype(np.uint8)

            del y_f, u_up, v_up, yuv_arr

            if frame_count <= 3:
                log(f"Frame {frame_count}: {w}x{h}, rgb mean={rgb.mean():.1f}")

            # Run YOLO
            results = model(rgb, verbose=False)
            del rgb

            output = {"persons": []}
            if results and len(results) > 0:
                r = results[0]
                if r.keypoints is not None and len(r.keypoints.xy) > 0:
                    for i in range(len(r.keypoints.xy)):
                        kps = r.keypoints.xy[i].cpu().numpy()
                        kps_conf = r.keypoints.conf[i].cpu().numpy() if r.keypoints.conf is not None else np.ones(17)
                        bbox = r.boxes.xyxy[i].cpu().numpy() if r.boxes is not None else np.zeros(4)
                        conf = float(r.boxes.conf[i]) if r.boxes is not None and r.boxes.conf is not None else 0.0

                        person = {
                            "bbox": [float(bbox[0]), float(bbox[1]), float(bbox[2]), float(bbox[3])],
                            "conf": conf,
                            "keypoints": [{"x": float(kps[k][0]), "y": float(kps[k][1]), "score": float(kps_conf[k])} for k in range(17)]
                        }
                        output["persons"].append(person)

            n_persons = len(output['persons'])
            if frame_count <= 3 or frame_count % 30 == 0:
                log(f"Frame {frame_count}: {n_persons} persons")

            sys.stdout.write(json.dumps(output) + "\n")
            sys.stdout.flush()

            # Force GC every 50 frames to prevent memory buildup
            if frame_count % 50 == 0:
                gc.collect()

        except Exception as e:
            log(f"Frame {frame_count} ERROR: {e}")
            traceback.print_exc(file=sys.stderr)
            sys.stdout.write(json.dumps({"persons": []}) + "\n")
            sys.stdout.flush()
            gc.collect()

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        log(f"FATAL: {e}")
        traceback.print_exc(file=sys.stderr)
