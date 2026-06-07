import csv
import datetime as dt
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import pandas as pd
import serial
from serial.tools import list_ports


BAUDRATE = 115200
MODES = {"制冷": 0, "制热": 1, "送风": 2, "关机": 3}
MODE_NAMES = {value: key for key, value in MODES.items()}
FAN_SPEEDS = {"自动": 0, "1 档": 1, "2 档": 2, "3 档": 3, "4 档": 4, "5 档": 5}
FAN_SPEED_NAMES = {value: key for key, value in FAN_SPEEDS.items()}


class SerialWorker:
    def __init__(self, event_queue):
        self.event_queue = event_queue
        self.serial = None
        self.thread = None
        self.stop_event = threading.Event()

    @property
    def connected(self):
        return self.serial is not None and self.serial.is_open

    def connect(self, port):
        self.disconnect()
        self.serial = serial.Serial(port=port, baudrate=BAUDRATE, timeout=0.2)
        self.stop_event.clear()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def disconnect(self):
        self.stop_event.set()
        if self.serial:
            try:
                self.serial.close()
            except serial.SerialException:
                pass
        self.serial = None

    def send(self, command):
        if not self.connected:
            raise RuntimeError("串口未连接")
        self.serial.write((command.strip() + "\n").encode("utf-8"))

    def _reader(self):
        while not self.stop_event.is_set() and self.connected:
            try:
                raw = self.serial.readline()
            except serial.SerialException as exc:
                self.event_queue.put(("error", str(exc)))
                break
            if raw:
                text = raw.decode("utf-8", errors="replace").strip()
                if text:
                    self.event_queue.put(("line", text))


class HitachiApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("空调实验控制系统")
        self.geometry("1120x760")
        self.minsize(980, 680)

        self.events = queue.Queue()
        self.serial_worker = SerialWorker(self.events)
        self.log_rows = []

        self.port_var = tk.StringVar()
        self.status_var = tk.StringVar(value="未连接")
        self.temp_var = tk.DoubleVar(value=26.0)
        self.mode_var = tk.StringVar(value="制冷")
        self.fan_var = tk.StringVar(value="自动")
        self.sched_mode_var = tk.StringVar(value="制冷")
        self.sched_fan_speed_var = tk.StringVar(value="自动")
        self.start_time_var = tk.StringVar(value="09:00")
        self.end_time_var = tk.StringVar(value="18:00")
        self.interval_var = tk.IntVar(value=10)
        self.start_temp_var = tk.DoubleVar(value=26.0)
        self.step_var = tk.DoubleVar(value=0.0)

        self._build_ui()
        self.refresh_ports()
        self.after(100, self._poll_events)

    def _build_ui(self):
        root = ttk.Frame(self, padding=(10, 4, 10, 10))
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(0, weight=1)
        root.columnconfigure(1, weight=0)
        root.rowconfigure(1, weight=1)

        self._build_connection(root)
        self._build_realtime(root)
        self._build_schedule(root)
        self._build_log(root)

    def _build_connection(self, parent):
        frame = ttk.LabelFrame(parent, text="连接管理", padding=8)
        frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8), pady=(0, 8))
        frame.columnconfigure(9, weight=1)

        ttk.Label(frame, text="COM口").grid(row=0, column=0, padx=(0, 6))
        self.port_combo = ttk.Combobox(frame, textvariable=self.port_var, state="readonly", width=20)
        self.port_combo.grid(row=0, column=1, sticky="w")
        ttk.Button(frame, text="扫描", command=self.refresh_ports).grid(row=0, column=2, padx=(6, 0))
        ttk.Button(frame, text="连接", command=self.connect).grid(row=0, column=3, padx=(6, 0))
        ttk.Button(frame, text="断开", command=self.disconnect).grid(row=0, column=4, padx=(6, 0))
        ttk.Button(frame, text="同步时间", command=self.sync_time).grid(row=0, column=5, padx=(6, 0))
        ttk.Button(frame, text="查询状态", command=lambda: self.send_command("STATUS")).grid(row=0, column=6, padx=(6, 0))
        ttk.Label(frame, textvariable=self.status_var).grid(row=0, column=7, padx=(12, 0), sticky="w")

    def _build_realtime(self, parent):
        frame = ttk.LabelFrame(parent, text="实时控制", padding=8)
        frame.grid(row=0, column=1, sticky="nsew", pady=(0, 8))

        ttk.Label(frame, text="温度").grid(row=0, column=0, sticky="w")
        ttk.Spinbox(
            frame,
            from_=16.0,
            to=30.0,
            increment=0.5,
            format="%.1f",
            textvariable=self.temp_var,
            width=8,
        ).grid(row=0, column=1, padx=6)
        ttk.Scale(
            frame,
            from_=16.0,
            to=30.0,
            variable=self.temp_var,
            orient=tk.HORIZONTAL,
            length=160,
            command=self._snap_temp,
        ).grid(row=0, column=2)

        ttk.Label(frame, text="模式").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Combobox(frame, textvariable=self.mode_var, values=list(MODES), state="readonly", width=8).grid(
            row=1, column=1, padx=6, pady=(8, 0)
        )
        
        ttk.Label(frame, text="风速").grid(row=2, column=0, sticky="w", pady=(8, 0))
        ttk.Combobox(frame, textvariable=self.fan_var, values=list(FAN_SPEEDS), state="readonly", width=8).grid(
            row=2, column=1, padx=6, pady=(8, 0)
        )
        
        ttk.Button(frame, text="立即发送", command=self.send_set).grid(row=1, column=2, rowspan=2, pady=(8, 0), sticky="nsew")

    def _build_schedule(self, parent):
        frame = ttk.LabelFrame(parent, text="计划表", padding=8)
        frame.grid(row=1, column=0, sticky="nsew", padx=(0, 8))
        frame.rowconfigure(1, weight=1)
        frame.columnconfigure(0, weight=1)

        tools = ttk.Frame(frame)
        tools.grid(row=0, column=0, sticky="ew", pady=(0, 8))

        ttk.Label(tools, text="起始").grid(row=0, column=0, padx=2, pady=2)
        ttk.Entry(tools, textvariable=self.start_time_var, width=6).grid(row=0, column=1, padx=2, pady=2)
        ttk.Label(tools, text="结束").grid(row=0, column=2, padx=2, pady=2)
        ttk.Entry(tools, textvariable=self.end_time_var, width=6).grid(row=0, column=3, padx=2, pady=2)
        ttk.Label(tools, text="间隔").grid(row=0, column=4, padx=2, pady=2)
        ttk.Combobox(tools, textvariable=self.interval_var, values=[1, 5, 10, 15, 30, 60], state="readonly", width=4).grid(
            row=0, column=5, padx=2, pady=2
        )
        ttk.Label(tools, text="模式").grid(row=0, column=6, padx=2, pady=2)
        ttk.Combobox(tools, textvariable=self.sched_mode_var, values=list(MODES), state="readonly", width=5).grid(
            row=0, column=7, padx=2, pady=2
        )
        ttk.Label(tools, text="温度").grid(row=0, column=8, padx=2, pady=2)
        ttk.Spinbox(
            tools,
            from_=16.0,
            to=30.0,
            increment=0.5,
            format="%.1f",
            textvariable=self.start_temp_var,
            width=5,
        ).grid(row=0, column=9, padx=2, pady=2)
        ttk.Label(tools, text="阶梯").grid(row=0, column=10, padx=2, pady=2)
        ttk.Spinbox(
            tools,
            from_=-5.0,
            to=5.0,
            increment=0.5,
            format="%.1f",
            textvariable=self.step_var,
            width=5,
        ).grid(row=0, column=11, padx=2, pady=2)
        ttk.Label(tools, text="风速").grid(row=0, column=12, padx=2, pady=2)
        ttk.Combobox(tools, textvariable=self.sched_fan_speed_var, values=list(FAN_SPEEDS), state="readonly", width=6).grid(
            row=0, column=13, padx=2, pady=2
        )
        ttk.Button(tools, text="生成", command=self.generate_schedule, width=6).grid(row=0, column=14, padx=6, pady=2)

        columns = ("time", "temperature", "mode", "fan_speed")
        self.schedule_tree = ttk.Treeview(frame, columns=columns, show="headings", selectmode="browse")
        self.schedule_tree.heading("time", text="时间")
        self.schedule_tree.heading("temperature", text="温度")
        self.schedule_tree.heading("mode", text="模式")
        self.schedule_tree.heading("fan_speed", text="风速")
        self.schedule_tree.column("time", width=100, anchor=tk.CENTER)
        self.schedule_tree.column("temperature", width=80, anchor=tk.CENTER)
        self.schedule_tree.column("mode", width=100, anchor=tk.CENTER)
        self.schedule_tree.column("fan_speed", width=100, anchor=tk.CENTER)
        self.schedule_tree.grid(row=1, column=0, sticky="nsew")
        self.schedule_tree.bind("<Double-1>", self.edit_schedule_cell)

        buttons = ttk.Frame(frame)
        buttons.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        buttons.columnconfigure((0, 1, 2), weight=1)
        
        ttk.Button(buttons, text="添加一行", command=self.add_schedule_row).grid(row=0, column=0, padx=4, pady=2, sticky="ew")
        ttk.Button(buttons, text="删除选中", command=self.delete_selected_schedule).grid(row=0, column=1, padx=4, pady=2, sticky="ew")
        ttk.Button(buttons, text="清空本地", command=self.clear_local_schedule).grid(row=0, column=2, padx=4, pady=2, sticky="ew")
        ttk.Button(buttons, text="上传设备", command=self.upload_schedule).grid(row=1, column=0, padx=4, pady=2, sticky="ew")
        ttk.Button(buttons, text="读取设备", command=lambda: self.send_command("LIST")).grid(row=1, column=1, padx=4, pady=2, sticky="ew")
        ttk.Button(buttons, text="导出Excel", command=self.export_schedule_excel).grid(row=1, column=2, padx=4, pady=2, sticky="ew")
        ttk.Button(buttons, text="清除设备", command=self.clear_device_schedule).grid(row=2, column=0, padx=4, pady=2, sticky="ew")
        self.sched_count_var = tk.StringVar(value="设备存储: 未连接 / 120")
        self.sched_count_lbl = ttk.Label(buttons, textvariable=self.sched_count_var, anchor="center")
        self.sched_count_lbl.grid(row=2, column=1, columnspan=2, padx=4, pady=2, sticky="ew")

    def _build_log(self, parent):
        frame = ttk.LabelFrame(parent, text="日志", padding=8)
        frame.grid(row=1, column=1, sticky="nsew")
        frame.rowconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)

        self.log_text = tk.Text(frame, height=18, width=30, wrap=tk.NONE)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=self.log_text.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scroll.set)

        buttons = ttk.Frame(frame)
        buttons.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Button(buttons, text="导出日志", command=self.export_log_text).pack(side=tk.LEFT)
        ttk.Button(buttons, text="清空日志", command=self.clear_log).pack(side=tk.LEFT, padx=8)

    def refresh_ports(self):
        ports = [port.device for port in list_ports.comports()]
        self.port_combo["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])

    def connect(self):
        port = self.port_var.get()
        if not port:
            messagebox.showwarning("提示", "请选择COM口")
            return
        try:
            self.serial_worker.connect(port)
        except serial.SerialException as exc:
            messagebox.showerror("连接失败", str(exc))
            return
        self.status_var.set(f"已连接 {port}")
        self.append_log(f"CONNECTED,{port}")
        self.after(500, lambda: self.send_command("STATUS"))

    def disconnect(self):
        self.serial_worker.disconnect()
        self.status_var.set("未连接")
        self.append_log("DISCONNECTED")

    def send_command(self, command):
        try:
            self.serial_worker.send(command)
            self.append_log(f"> {command}")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def send_set(self):
        temp = self._format_temp(self.temp_var.get())
        mode = MODES[self.mode_var.get()]
        fan = FAN_SPEEDS[self.fan_var.get()]
        self.send_command(f"SET,{temp},{mode},{fan}")

    def sync_time(self):
        now = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.send_command(f"TIME,{now}")

    def add_schedule_row(self):
        self.schedule_tree.insert("", tk.END, values=("09:00", "26", "制冷", "自动"))

    def edit_schedule_cell(self, event):
        region = self.schedule_tree.identify_region(event.x, event.y)
        if region != "cell":
            return
        
        column = self.schedule_tree.identify_column(event.x)
        item = self.schedule_tree.identify_row(event.y)
        if not column or not item:
            return

        col_index = int(column[1:]) - 1
        x, y, w, h = self.schedule_tree.bbox(item, column)
        
        values = list(self.schedule_tree.item(item, "values"))
        current_value = values[col_index]

        if col_index == 2:
            editor = ttk.Combobox(self.schedule_tree, values=list(MODES), state="readonly")
            editor.set(current_value)
        elif col_index == 3:
            editor = ttk.Combobox(self.schedule_tree, values=list(FAN_SPEEDS), state="readonly")
            editor.set(current_value)
        else:
            editor = ttk.Entry(self.schedule_tree)
            editor.insert(0, current_value)

        editor.place(x=x, y=y, width=w, height=h)
        editor.focus_set()

        def save_edit(e=None):
            # Check if editor still exists to prevent Double-Destroy errors
            if not editor.winfo_exists():
                return
            new_value = editor.get().strip()
            if col_index == 0:
                try:
                    self._parse_hhmm(new_value)
                except ValueError:
                    new_value = current_value
            elif col_index == 1:
                try:
                    new_value = self._format_temp(float(new_value))
                except ValueError:
                    new_value = current_value
            
            values[col_index] = new_value
            self.schedule_tree.item(item, values=values)
            editor.destroy()

        editor.bind("<Return>", save_edit)
        editor.bind("<FocusOut>", save_edit)
        editor.bind("<Escape>", lambda e: editor.destroy() if editor.winfo_exists() else None)

    def delete_selected_schedule(self):
        for item in self.schedule_tree.selection():
            self.schedule_tree.delete(item)

    def clear_local_schedule(self):
        for item in self.schedule_tree.get_children():
            self.schedule_tree.delete(item)

    def clear_device_schedule(self):
        if messagebox.askyesno("确认", "确定要清除设备上存储的所有计划吗？"):
            self.send_command("CLEAR")

    def generate_schedule(self):
        try:
            start = self._parse_hhmm(self.start_time_var.get())
            end = self._parse_hhmm(self.end_time_var.get())
            interval = int(self.interval_var.get())
            temp = float(self.start_temp_var.get())
            step = float(self.step_var.get())
        except ValueError as exc:
            messagebox.showerror("参数错误", str(exc))
            return

        if interval <= 0:
            messagebox.showerror("参数错误", "间隔必须大于0")
            return

        if end < start:
            messagebox.showerror("参数错误", "结束时间不能早于起始时间")
            return

        self.clear_local_schedule()
        for index, minute in enumerate(range(start, end + 1, interval)):
            current_temp = max(16.0, min(30.0, temp + index * step))
            self.schedule_tree.insert(
                "",
                tk.END,
                values=(
                    f"{minute // 60:02d}:{minute % 60:02d}",
                    self._format_temp(current_temp),
                    self.sched_mode_var.get(),
                    self.sched_fan_speed_var.get(),
                ),
            )

    def upload_schedule(self):
        items = self.schedule_tree.get_children()
        if not items:
            messagebox.showwarning("提示", "计划表为空")
            return
        if len(items) > 120:
            if not messagebox.askyesno("数量超限", f"当前计划表共有 {len(items)} 条，已超过设备存储上限（120条）。\n是否只上传前 120 条？"):
                return
            items = items[:120]
        rows = [self.schedule_tree.item(item, "values") for item in items]

        def _do_upload():
            import time
            try:
                self.serial_worker.send("CLEAR")
                self.events.put(("line", "> CLEAR"))
                time.sleep(0.15)
                for time_text, temp, mode_name, fan_speed_name in rows:
                    cmd = f"SCHEDULE,{time_text},{temp},{MODES[mode_name]},{FAN_SPEEDS[fan_speed_name]}"
                    self.serial_worker.send(cmd)
                    self.events.put(("line", f"> {cmd}"))
                    time.sleep(0.08)
                time.sleep(0.2)
                self.serial_worker.send("STATUS")
            except Exception as exc:
                self.events.put(("line", f"ERROR,upload_failed,{exc}"))

        threading.Thread(target=_do_upload, daemon=True).start()

    def export_schedule_excel(self):
        items = self.schedule_tree.get_children()
        if not items:
            messagebox.showwarning("提示", "计划表为空，无数据可导出")
            return
        path = filedialog.asksaveasfilename(
            title="导出计划表",
            defaultextension=".xlsx",
            filetypes=[("Excel 文件", "*.xlsx")],
        )
        if not path:
            return
        data = []
        for item in items:
            time_text, temp, mode_name, fan_speed_name = self.schedule_tree.item(item, "values")
            data.append({
                "时间": time_text,
                "温度": temp,
                "模式": mode_name,
                "风速": fan_speed_name
            })
        try:
            pd.DataFrame(data).to_excel(path, index=False)
            messagebox.showinfo("完成", f"计划表已成功导出：{path}")
        except Exception as exc:
            messagebox.showerror("导出失败", str(exc))

    def export_log_text(self):
        log_content = self.log_text.get("1.0", tk.END).strip()
        if not log_content:
            messagebox.showwarning("提示", "日志为空，无数据可导出")
            return
        path = filedialog.asksaveasfilename(
            title="导出日志",
            defaultextension=".txt",
            filetypes=[("文本文件", "*.txt"), ("所有文件", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(log_content)
            messagebox.showinfo("完成", f"日志已成功导出：{path}")
        except Exception as exc:
            messagebox.showerror("导出失败", str(exc))

    def clear_log(self):
        self.log_text.delete("1.0", tk.END)
        self.log_rows.clear()

    def append_log(self, text):
        stamp = dt.datetime.now().strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{stamp}] {text}\n")
        self.log_text.see(tk.END)

    def _poll_events(self):
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "line":
                # 过滤 IRRAW 超长调试行，但保留 IRGPIO 捕获输出显示在日志里
                if not payload.startswith("IRRAW,"):
                    self.append_log(payload)
                self._handle_device_line(payload)
            elif kind == "error":
                self.append_log(f"ERROR,{payload}")
                self.status_var.set("连接异常")
        self.after(100, self._poll_events)

    def _handle_device_line(self, line):
        if line.startswith("LOG,"):
            self._record_log_row(line)
        elif line.startswith("ITEM,"):
            self._record_device_item(line)
        elif line.startswith("OK,STATUS,"):
            self._parse_status_line(line)
        elif line.startswith("OK,CLEAR") or line.startswith("OK,SCHEDULE") or line.startswith("OK,DELETE"):
            self.send_command("STATUS")

    def _parse_status_line(self, line):
        try:
            parts = line.split(",")
            if "schedules" in parts:
                idx = parts.index("schedules")
                count = int(parts[idx + 1])
                remaining = 120 - count
                self.sched_count_var.set(f"设备存储: {count} / 120 (可用: {remaining})")
        except Exception as e:
            print(f"解析状态失败: {e}")

    def _record_log_row(self, line):
        try:
            parts = next(csv.reader([line]))
        except csv.Error:
            return
        if len(parts) < 9:
            return
        try:
            mode = int(parts[6])
            fan = int(parts[8])
        except ValueError:
            return
        row = {
            "时间戳": parts[1],
            "执行类型": parts[2],
            "温度": parts[4],
            "模式": MODE_NAMES.get(mode, str(mode)),
            "风速": FAN_SPEED_NAMES.get(fan, str(fan)),
        }
        self.log_rows.append(row)
        try:
            import os
            file_exists = os.path.exists("ac_control_log.csv")
            with open("ac_control_log.csv", "a", encoding="utf-8-sig", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=["时间戳", "执行类型", "温度", "模式", "风速"])
                if not file_exists:
                    writer.writeheader()
                writer.writerow(row)
        except Exception as e:
            print(f"实时写入日志文件失败: {e}")

    def _record_device_item(self, line):
        try:
            parts = next(csv.reader([line]))
        except csv.Error:
            return
        if len(parts) < 7:
            return
        time_text = parts[2]
        temp = parts[3]
        mode = MODE_NAMES.get(int(parts[4]), parts[4])
        fan = FAN_SPEED_NAMES.get(int(parts[6]), parts[6])
        self.schedule_tree.insert("", tk.END, values=(time_text, temp, mode, fan))

    def _snap_temp(self, value):
        snapped = round(float(value) * 2) / 2
        if abs(self.temp_var.get() - snapped) > 0.001:
            self.temp_var.set(snapped)

    @staticmethod
    def _format_temp(value):
        value = round(float(value) * 2) / 2
        if value.is_integer():
            return str(int(value))
        return f"{value:.1f}"

    @staticmethod
    def _parse_hhmm(text):
        try:
            hour_text, minute_text = text.split(":", 1)
            hour = int(hour_text)
            minute = int(minute_text)
        except ValueError as exc:
            raise ValueError("时间格式应为 HH:MM") from exc
        if hour < 0 or hour > 23 or minute < 0 or minute > 59:
            raise ValueError("时间范围应为 00:00 到 23:59")
        return hour * 60 + minute


if __name__ == "__main__":
    app = HitachiApp()
    app.mainloop()
