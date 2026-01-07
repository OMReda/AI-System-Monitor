import asyncio
import json
import socket
import threading
import time
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Response
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from starlette.requests import Request
import io

from backend_ai.analyzer import SystemAnalyzer

app = FastAPI()

# serve static files if needed, but we might just inline JS for simplicity or use CDN
# app.mount("/static", StaticFiles(directory="static"), name="static")

templates = Jinja2Templates(directory="dashboard_web/templates")

import subprocess

# Global state
latest_data = {}
analyzer = SystemAnalyzer()
clients = []
latest_gpu1 = 0.0
latest_gpu2 = 0.0
gpu_luid_map = {} # Map LUID to index (0 or 1)

def gpu_monitor_loop():
    global latest_gpu1, latest_gpu2, gpu_luid_map
    while True:
        try:
            # Fetch all GPU engine utilization samples
            cmd = "(Get-Counter '\\GPU Engine(*)\\Utilization Percentage' -ErrorAction SilentlyContinue).CounterSamples | Select-Object -Property InstanceName, CookedValue | ConvertTo-Json"
            process = subprocess.Popen(["powershell", "-Command", cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            stdout, stderr = process.communicate()
            
            if stdout.strip():
                try:
                    data = json.loads(stdout.strip())
                    if not isinstance(data, list):
                        data = [data]
                    
                    # Group by LUID
                    # Instance format: pid_1234_luid_0x00000000_0x00009A4A_phys_0_eng_0_engtype_3D
                    gpu_data = {}
                    for item in data:
                        name = item.get("InstanceName", "")
                        # Extract LUID (e.g., 0x00009A4A)
                        luid = "default"
                        if "luid_" in name:
                            parts = name.split("luid_")[-1].split("_")
                            if len(parts) >= 2:
                                luid = f"{parts[0]}_{parts[1]}"
                        
                        gpu_data[luid] = gpu_data.get(luid, 0.0) + item.get("CookedValue", 0.0)
                    
                    # Maintain consistent index for each LUID
                    current_luids = sorted(gpu_data.keys())
                    for luid in current_luids:
                        if luid not in gpu_luid_map and len(gpu_luid_map) < 2:
                            gpu_luid_map[luid] = len(gpu_luid_map)
                    
                    # Reset values
                    v1, v2 = 0.0, 0.0
                    for luid, val in gpu_data.items():
                        idx = gpu_luid_map.get(luid)
                        if idx == 0: v1 = min(val, 100.0)
                        elif idx == 1: v2 = min(val, 100.0)
                        elif idx is None and len(gpu_luid_map) < 2:
                            # Dynamic assignment if map not yet full
                            idx = len(gpu_luid_map)
                            gpu_luid_map[luid] = idx
                            if idx == 0: v1 = min(val, 100.0)
                            else: v2 = min(val, 100.0)

                    latest_gpu1 = v1
                    latest_gpu2 = v2
                except Exception as e:
                    print(f"GPU Parse Error: {e}")
            else:
                latest_gpu1 = 0.0
                latest_gpu2 = 0.0
        except Exception as e:
            print(f"GPU Error: {e}")

            # Independent NVIDIA check (Works even if Get-Counter is empty/idle)
            try:
                # Running nvidia-smi directly
                nv_process = subprocess.Popen(["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                nv_stdout, _ = nv_process.communicate()
                if nv_stdout.strip():
                    nv_val = float(nv_stdout.strip())
                    # Prefer nvidia-smi for the second GPU slot if it's an NVIDIA card
                    latest_gpu2 = max(latest_gpu2, nv_val)
            except:
                pass 
                
            time.sleep(1.0)

def c_client_thread():
    global latest_data
    host = '127.0.0.1'
    port = 5000
    
    while True:
        try:
            print(f"Connecting to C engine at {host}:{port}...")
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((host, port))
            print("Connected to C engine.")
            
            buffer = ""
            while True:
                data = s.recv(1024)
                if not data:
                    break
                
                buffer += data.decode('utf-8')
                
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if not line: continue
                    
                    try:
                        metrics = json.loads(line)
                        
                        # Run AI Analysis
                        analysis = analyzer.update(metrics['cpu'], metrics['ram_percent'], metrics.get('disk_percent', 0))
                        
                        # Merge metrics and GPU
                        full_data = {
                            **metrics, 
                            **analysis, 
                            "gpu": latest_gpu1, # legacy support
                            "gpu1": latest_gpu1, 
                            "gpu2": latest_gpu2, 
                            "timestamp": time.time()
                        }
                        latest_data = full_data
                        
                        # Broadcast to websockets (handled in main loop via asyncio, 
                        # but we need a bridge. Simplest is thread updates global, 
                        # async loop polls or we use an async queue if we were purely async.
                        # For simplicity, we'll let the websocket endpoint poll or use an event.)
                        
                    except json.JSONDecodeError:
                        print("JSON Error:", line)
                        
        except Exception as e:
            print(f"Connection error: {e}. Retrying in 5s...")
            time.sleep(5)

# Start background threads
t = threading.Thread(target=c_client_thread, daemon=True)
t.start()
threading.Thread(target=gpu_monitor_loop, daemon=True).start()

@app.get("/", response_class=HTMLResponse)
async def get(request: Request):
    return templates.TemplateResponse("index.html", {"request": request})

@app.get("/export/csv")
async def export_csv():
    df = analyzer.get_history_dataframe()
    if df.empty:
        return Response(content="No data collected yet", media_type="text/plain")
    
    stream = io.StringIO()
    df.to_csv(stream, index=False)
    return Response(content=stream.getvalue(), media_type="text/csv", headers={"Content-Disposition": "attachment; filename=system_metrics.csv"})

@app.get("/export/json")
async def export_json():
    df = analyzer.get_history_dataframe()
    if df.empty:
        return JSONResponse(content={"error": "No data yet"})
    
    return JSONResponse(content=df.to_dict(orient="records"))

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        last_time = 0
        while True:
            # Push data if new
            if latest_data and latest_data.get("timestamp", 0) != last_time:
                last_time = latest_data["timestamp"]
                await websocket.send_json(latest_data)
            await asyncio.sleep(0.1)
    except WebSocketDisconnect:
        pass
