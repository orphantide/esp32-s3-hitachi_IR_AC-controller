# ESP32-S3 日立空调控制器 

基于 ESP32-S3 的日立空调控制器。

## 项目布局

* `firmware/`：适用于 ESP32-S3 的 ESP-IDF v5.5.4 固件。
* `pc_gui/`：用于串口控制、日程编辑、日志记录和 Excel 导出的 Python tkinter GUI。

## 硬件引脚

| 模块 | ESP32-S3 引脚 | 说明 |
| --- | --- | --- |
| DS3231 SDA | GPIO20 | I2C 数据 |
| DS3231 SCL | GPIO21 | I2C 时钟 |
| VS1838B OUT | GPIO15 | 红外接收器（默认禁用以节省 CPU） |
| KY-005 S | GPIO4 | 红外发射器 |

## 协议说明

日立 29 字节红外协议在 `firmware/main/ir_hitachi.c` 中已完整实现并进行了优化。
* **单帧自适应翻转位发送**：降低延迟，避免遥控信号冲突。
* **标准日立校验和**：对未移位的 28 字节数组进行计算。
* **支持温度、模式和风速控制**。

## 串口协议

命令以换行符结束（`\r\n` 或 `\n`）：

* `STATUS` — 查询当前设备时间和日程状态。
* `SET,temperature,mode,fan_speed` — 直接控制。
* `SCHEDULE,HH:MM,temperature,mode,fan_speed` — 添加日程事件。
* `DELETE,index` — 根据索引删除日程。
* `CLEAR` — 从 NVS 中清除所有日程事件。
* `LIST` — 打印控制器中存储的所有日程事件。
* `TIME,YYYY-MM-DD HH:MM:SS` — 同步 RTC 时间。

### 参数映射

**模式值 (Mode)：**
* `0`: Cool (制冷)
* `1`: Heat (制热)
* `2`: Fan (送风)
* `3`: Off (关机)

**风速值 (Fan Speed)：**
* `0`: Auto (自动)
* `1`: 1st Gear (1 档)
* `2`: 2nd Gear (2 档)
* `3`: 3rd Gear (3 档)
* `4`: 4th Gear (4 档)
* `5`: 5th Gear (5 档)

**温度值 (Temperature)：**
支持 `0.5` 度步长（例如：`16.0` 到 `30.0`°C）。

**日志输出格式：**
```text
LOG,YYYY-MM-DD HH:MM:SS,type,temperature,26,mode,0,fan_speed,0
```

---

## 构建与烧录固件

1. 打开 ESP-IDF 命令提示符或 PowerShell (v5.5.4)。
2. 构建项目：
   ```powershell
   cd firmware
   idf.py set-target esp32s3
   idf.py build
   ```
3. 烧录并监听串口（将 `COMx` 替换为你的实际端口，例如 `COM4`）：
   ```powershell
   idf.py -p COM4 flash monitor
   ```

---

## 运行 GUI

1. 打开终端并导航至 `pc_gui`。
2. 安装 Python 依赖：
   ```powershell
   cd pc_gui
   python -m pip install -r requirements.txt
   ```
3. 运行 GUI：
   ```powershell
   python hitachi_ac_gui.py
   ```
   * *注意：日志跟踪数据将自动保存至 `pc_gui/ac_control_log.csv`。*
