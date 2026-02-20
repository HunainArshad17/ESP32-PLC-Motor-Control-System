import tkinter as tk
from tkinter import ttk
import serial
import threading
import time

# ======= SET YOUR ESP32 PORT HERE =======
PORT = "/dev/cu.usbserial-130"
BAUD = 115200

# ======= SERIAL =======
ser = serial.Serial(PORT, BAUD, timeout=0.1)

# ======= APP STATE =======
st = {
    "mode": "MANUAL",
    "run": 0,
    "fault": 0,
    "relay": 0,
    "speed": 10,
    "steps": 0,
    "arun": 0,
    "arunms": 4000,
    "astopms": 2000,
}

def send(cmd: str):
    try:
        ser.write((cmd.strip() + "\n").encode())
    except Exception as e:
        log_line(f"[WRITE ERR] {e}")

def log_line(msg: str):
    log.insert(tk.END, msg + "\n")
    log.see(tk.END)

def set_lamp(canvas, color):
    canvas.itemconfig("lamp", fill=color)

def refresh_lamps():
    # FAULT has priority
    if st["fault"]:
        set_lamp(lamp_fault, "orange")
        set_lamp(lamp_run, "gray25")
        set_lamp(lamp_stop, "gray25")
    else:
        set_lamp(lamp_fault, "gray25")
        if st["run"]:
            set_lamp(lamp_run, "green")
            set_lamp(lamp_stop, "gray25")
        else:
            set_lamp(lamp_run, "gray25")
            set_lamp(lamp_stop, "red")

    set_lamp(lamp_relay, "green" if st["relay"] else "gray25")

    mode_var.set(st["mode"])
    steps_var.set(str(st["steps"]))
    speed_var.set(st["speed"])
    auto_running_var.set("ON" if st["arun"] else "OFF")
    auto_runms_var.set(str(st["arunms"]))
    auto_stopms_var.set(str(st["astopms"]))

def parse_status(line: str):
    # Example:
    # STATUS MODE=AUTO RUN=1 FAULT=0 RELAY=1 SPEED=10 STEPS=123 ARUN=1 ARUNMS=4000 ASTOPMS=2000
    if not line.startswith("STATUS"):
        return

    parts = line.split()
    for p in parts[1:]:
        if "=" not in p:
            continue
        k, v = p.split("=", 1)
        k = k.upper().strip()
        v = v.strip()

        if k == "MODE":
            st["mode"] = v.upper()
        elif k == "RUN":
            st["run"] = 1 if v == "1" else 0
        elif k == "FAULT":
            st["fault"] = 1 if v == "1" else 0
        elif k == "RELAY":
            st["relay"] = 1 if v == "1" else 0
        elif k == "SPEED":
            try: st["speed"] = int(v)
            except: pass
        elif k == "STEPS":
            try: st["steps"] = int(v)
            except: pass
        elif k == "ARUN":
            st["arun"] = 1 if v == "1" else 0
        elif k == "ARUNMS":
            try: st["arunms"] = int(v)
            except: pass
        elif k == "ASTOPMS":
            try: st["astopms"] = int(v)
            except: pass

    refresh_lamps()

def reader():
    while True:
        try:
            line = ser.readline().decode(errors="ignore").strip()
            if line:
                root.after(0, log_line, line)
                root.after(0, parse_status, line)
        except Exception as e:
            root.after(0, log_line, f"[READ ERR] {e}")
            time.sleep(0.5)

def poll():
    send("status")
    root.after(300, poll)  # ~3x/sec

# ======= ACTIONS =======
def do_start(): send("start")
def do_stop(): send("stop")
def do_fault(): send("fault")
def do_reset(): send("reset")

def set_mode_auto(): send("mode auto")
def set_mode_manual(): send("mode manual")

def auto_start(): send("auto start")
def auto_stop(): send("auto stop")

def jog_press(_): send("jog on")
def jog_release(_): send("jog off")

def on_speed(val):
    try:
        send(f"speed {int(float(val))}")
    except:
        pass

def steps_reset(): send("steps reset")

def set_auto_params():
    try:
        r = int(auto_runms_entry.get().strip())
        s = int(auto_stopms_entry.get().strip())
        send(f"autoparam runms {r} stopms {s}")
    except:
        log_line("[GUI] enter numbers for run/stop ms")

# ======= GUI =======
root = tk.Tk()
root.title("ESP32 Mini-PLC HMI (AUTO/MANUAL)")
root.geometry("640x640")

top = ttk.Frame(root, padding=10)
top.pack(fill="x")

def make_lamp(parent, label):
    f = ttk.Frame(parent)
    f.pack(side="left", padx=12)
    ttk.Label(f, text=label).pack()
    c = tk.Canvas(f, width=34, height=34, bg="gray15", highlightthickness=0)
    c.pack()
    c.create_oval(6, 6, 28, 28, fill="gray25", outline="black", tags="lamp")
    return c

lamp_run = make_lamp(top, "RUN")
lamp_stop = make_lamp(top, "STOP")
lamp_fault = make_lamp(top, "FAULT")
lamp_relay = make_lamp(top, "RELAY")

info = ttk.Frame(root, padding=10)
info.pack(fill="x")
mode_var = tk.StringVar(value="MANUAL")
steps_var = tk.StringVar(value="0")
auto_running_var = tk.StringVar(value="OFF")
auto_runms_var = tk.StringVar(value="4000")
auto_stopms_var = tk.StringVar(value="2000")

ttk.Label(info, text="Mode:").grid(row=0, column=0, sticky="w")
ttk.Label(info, textvariable=mode_var).grid(row=0, column=1, sticky="w", padx=8)

ttk.Label(info, text="Steps:").grid(row=0, column=2, sticky="w")
ttk.Label(info, textvariable=steps_var).grid(row=0, column=3, sticky="w", padx=8)

ttk.Label(info, text="Auto:").grid(row=0, column=4, sticky="w")
ttk.Label(info, textvariable=auto_running_var).grid(row=0, column=5, sticky="w", padx=8)

controls = ttk.LabelFrame(root, text="Manual Controls", padding=10)
controls.pack(fill="x", padx=10, pady=8)

ttk.Button(controls, text="START", command=do_start).grid(row=0, column=0, padx=8, pady=6, sticky="ew")
ttk.Button(controls, text="STOP", command=do_stop).grid(row=0, column=1, padx=8, pady=6, sticky="ew")
ttk.Button(controls, text="FAULT", command=do_fault).grid(row=1, column=0, padx=8, pady=6, sticky="ew")
ttk.Button(controls, text="RESET", command=do_reset).grid(row=1, column=1, padx=8, pady=6, sticky="ew")

jog_btn = ttk.Button(controls, text="JOG (hold)")
jog_btn.grid(row=2, column=0, columnspan=2, padx=8, pady=10, sticky="ew")
jog_btn.bind("<ButtonPress-1>", jog_press)
jog_btn.bind("<ButtonRelease-1>", jog_release)

ttk.Button(controls, text="STEPS RESET", command=steps_reset).grid(row=3, column=0, columnspan=2, padx=8, pady=6, sticky="ew")

mode_frame = ttk.LabelFrame(root, text="Mode", padding=10)
mode_frame.pack(fill="x", padx=10, pady=8)

ttk.Button(mode_frame, text="MANUAL", command=set_mode_manual).grid(row=0, column=0, padx=8, pady=6, sticky="ew")
ttk.Button(mode_frame, text="AUTO", command=set_mode_auto).grid(row=0, column=1, padx=8, pady=6, sticky="ew")

auto_frame = ttk.LabelFrame(root, text="Auto Controls", padding=10)
auto_frame.pack(fill="x", padx=10, pady=8)

ttk.Button(auto_frame, text="AUTO START", command=auto_start).grid(row=0, column=0, padx=8, pady=6, sticky="ew")
ttk.Button(auto_frame, text="AUTO STOP", command=auto_stop).grid(row=0, column=1, padx=8, pady=6, sticky="ew")

ttk.Label(auto_frame, text="Run ms:").grid(row=1, column=0, sticky="e")
auto_runms_entry = ttk.Entry(auto_frame, width=10)
auto_runms_entry.insert(0, "4000")
auto_runms_entry.grid(row=1, column=1, sticky="w", padx=6)

ttk.Label(auto_frame, text="Stop ms:").grid(row=2, column=0, sticky="e")
auto_stopms_entry = ttk.Entry(auto_frame, width=10)
auto_stopms_entry.insert(0, "2000")
auto_stopms_entry.grid(row=2, column=1, sticky="w", padx=6)

ttk.Button(auto_frame, text="SET AUTO PARAMS", command=set_auto_params).grid(row=3, column=0, columnspan=2, padx=8, pady=6, sticky="ew")

spd_frame = ttk.LabelFrame(root, text="Speed (RPM)", padding=10)
spd_frame.pack(fill="x", padx=10, pady=8)

speed_var = tk.IntVar(value=10)
speed_scale = tk.Scale(spd_frame, from_=1, to=18, orient="horizontal", length=520,
                       variable=speed_var, command=on_speed)
speed_scale.pack()

log = tk.Text(root, height=10, width=78)
log.pack(padx=10, pady=10)

log_line("HMI started.")
log_line("If you get 'Resource busy': close Arduino Serial Monitor/Plotter.")
log_line(f"Port: {PORT}")

refresh_lamps()

threading.Thread(target=reader, daemon=True).start()
root.after(300, poll)

root.mainloop()