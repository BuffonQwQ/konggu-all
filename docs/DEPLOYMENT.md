 部署指南（DEPLOYMENT）

本文基于对源码启动流程与通信方式的真实分析编写，覆盖：

- 采集客户端：`main.cpp`（机芯 JX007 + USB 可见光摄像头，C++）
- 推理服务端：`konggu_server.py` + `KongguProcessFiles.py`（Flask + YOLO）

两端的“粘合协议”只有两条 HTTP 接口 + 文件名约定：

| 接口 | 方向 | 说明 |
|---|---|---|
| `POST /upload` | 客户端 → 服务端 | multipart 表单，字段名固定为 `file`，文件名格式 `{n}_temp.jpg` / `{n}_camera.jpg` |
| `GET  /clean`  | 客户端 → 服务端 | 客户端启动时请求清空服务端缓存/队列，复位配对状态 |

---

## 1. 部署模式总览

```
┌─────────────────────────┐         HTTP /upload、/clean          ┌─────────────────────────┐
│ 采集机（边缘/树莓派）      │ ───────────────────────────────────► │ 推理工作台（x86 + GPU）    │
│  红外机芯 JX007 (USB)    │       局域网或同一台机               │  Flask 0.0.0.0:8088      │
│  可见光摄像头 /dev/video0 │ ◄─────────────────────────────────── │  YOLO 推理 + imshow 展示  │
└─────────────────────────┘                                        └─────────────────────────┘
```

- **推荐**：采集机与服务端分开（采集机放在被测电动自行车/工位现场，服务端放在有人值守的工作台，
  因为服务端用 `cv2.imshow` 弹出窗口）。
- **单机模式**：同一台 Linux 机器上同时跑两端也完全可以——把客户端的 `server_url`
  指向 `http://127.0.0.1:8088` 即可（见第 3 节）。注意两端相对路径互不冲突即可
  （客户端缓存 `data/`，服务端 `uploads_konggu/`、`out_konggu/`）。

> 部署前请先运行 `client/tools/camera_test`（对应 `TestCamera/camera_main.cpp`）确认
> 可见光摄像头能被 OpenCV 打开；`camera_main` 会在当前目录生成 `Camera.jpg` 作为自检产物。

---

## 2. 服务端部署（推理/展示端）

### 2.1 目录与文件就位

```
server/
├── konggu_server.py          # 入口（启动时交互选择模型）
├── KongguProcessFiles.py
├── pairAlign.py
├── MergeImage.py             # 未接入主流程（可选保留）
├── konggu_models/            # cold-best.pt / hot-best.pt（大文件，不入 git）
├── uploads_konggu/           # 运行期自动创建
└── out_konggu/               # 运行期自动创建
```

### 2.2 首次运行（前台验证）

```bash
cd server
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

python konggu_server.py
# 键入 1 → 加载低温模型 cold-best.pt
# 键入 2 → 加载高温模型 hot-best.pt
```

看到 Flask 输出 `Running on http://0.0.0.0:8088` 即成功。此时应能看到一个标题为
`result window` 的 OpenCV 窗口（说明：该窗口在收到第一对可处理图片后才显示画面）。

### 2.3 无人工选择的自动启动（可选改造）

当前模型选择依赖终端 `input()`，无法 `nohup` 后台直接跑通。两种办法：

- **保留交互**：用 `tmux` / `screen` 起会话，在会话内交互后保持运行；
- **改造**（推荐，改动很小）：把 `konggu_server.py` 顶部 `while True: ... input() ...`
  改为读取环境变量，例如：

  ```python
  import os
  choice = os.environ.get("KONGGU_MODEL", "1")   # "1"=cold, "2"=hot
  model_path = ("konggu_models/cold-best.pt" if choice == "1"
                else "konggu_models/hot-best.pt")
  ```

  之后便可用环境变量 + 守护进程无人工启动。

### 2.4 后台运行与开机自启

无 systemd 的简易后台（配合 2.3 改造后使用）：

```bash
cd server
nohup python konggu_server.py > server.log 2>&1 &
echo $! > server.pid
# 查看日志: tail -f server.log
# 停止: kill $(cat server.pid)
```

推荐 systemd 服务（`/etc/systemd/system/konggu-server.service`）：

```ini
[Unit]
Description=Konggu inference server (Flask + YOLO)
After=network-online.target

[Service]
WorkingDirectory=/opt/konggu/server
Environment=KONGGU_MODEL=1
ExecStart=/opt/konggu/server/.venv/bin/python konggu_server.py
Restart=on-failure
RestartSec=3
# 可选：如果采集机想远程看结果目录，可用 nginx 反代 /out_konggu/ 为静态目录

[Install]
WantedBy=multi-user.target
```

启用：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now konggu-server
```

### 2.5 关于 `cv2.imshow`（重要）

`KongguProcessFiles.py` 每处理一对图就调用 `cv2.imshow('result window', ...)`。
若以**无桌面环境**运行（纯 SSH 服务器），该调用会抛异常 → 该文件对处理失败并留在队列反复重试。

选项：

- 现场有人值守、有显示器/远程桌面 → 正常使用；
- 无头环境 → 在 `process_file_pair()` 中给 `imshow`/`waitKey` 加"是否启用 GUI"开关（建议改造），
  只保留 `out_konggu/` 落盘结果，再用浏览器/静态服务查看 `out_konggu/*.jpg`。

---

## 3. 客户端部署（采集端）

### 3.1 修改服务端地址

客户端把地址**硬编码**在 `main.cpp` 顶部：

```cpp
const string &url = "http://192.168.26.215:8088";
```

部署到不同网络时，把它改成服务端 IP：如单机为 `http://127.0.0.1:8088`；
局域网为 `http://<服务端IP>:8088`。`upload_url`、`clean_url` 会自动拼接，无需再改。
（注意 `main-backup.cpp` 是历史备份，不要改错文件。）

改完重新编译（见 README「安装与编译」）。

### 3.2 编译并放置运行环境

```bash
cd client
cmake -S . -B build
cmake --build build -j
```

运行前确认：

- 热成像机芯通过 USB 接入且被系统识别（JX007；SDK 依赖 libusb）；
- 可见光摄像头为 `/dev/video0`（否则改 `cv::VideoCapture cap(0)` 的编号）；
- SDK 私有 `.so` 可被找到：

```bash
cd build
export LD_LIBRARY_PATH=../lib:$LD_LIBRARY_PATH
```

### 3.3 前台运行

```bash
cd client/build
./konggu_client          # 注意：程序内部使用相对路径 ../data 作为缓存目录
```

预期启动过程：

1. 请求服务端 `GET /clean`；
2. 清空本地 `../data/` 旧图；
3. 打开可见光摄像头 → `UT_Init(JX007)`；
4. 约 5 秒预热后 `DEV_OUT_IMAGE` 出图；
5. 控制台持续打印 `Center = <中心温度> framecount <n>`，随后每 10 帧输出一对图并上传。

> 客户端当前**没有优雅退出**：主循环是 `while(1)`，停止/释放 SDK 的代码位于循环之后
> （不可达）。用 `Ctrl+C` 或 `kill` 结束进程即可。

### 3.4 后台运行与开机自启（systemd）

`/etc/systemd/system/konggu-client.service`：

```ini
[Unit]
Description=Konggu capture client (JX007 + camera)
After=network-online.target

[Service]
WorkingDirectory=/opt/konggu/client/build
Environment=LD_LIBRARY_PATH=/opt/konggu/client/lib
ExecStart=/opt/konggu/client/build/konggu_client
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now konggu-client
# 查看输出: journalctl -u konggu-client -f
```

---

## 4. 网络与防火墙

- 服务端需**监听所有网卡**（源码已是 `host='0.0.0.0'`），放行 **TCP 8088**：
  - `sudo ufw allow 8088/tcp`（如有 ufw），或云安全组放行该端口。
- 两端可先互 ping 验证连通；再在采集机上：
  ```bash
  curl -X POST -F "file=@test.jpg" http://<服务端IP>:8088/upload   # 应返回 {"result":1}
  curl http://<服务端IP>:8088/clean                                 # 应返回 Successfully cleaned!
  ```
- 无鉴权提示：`/upload`、`/clean` 均无认证，仅适合可信内网；跨公网请加访问控制/反向代理鉴权
  （详见 `docs/CODE_REVIEW_NOTES.md`）。

---

## 5. 多台采集机共享一个服务端的注意事项

服务端以**文件名前缀**配对（`{n}_temp.jpg` 与 `{n}_camera.jpg`），若多台采集机同时上传，
序号会冲突导致配对错乱。当前代码**不支持多采集机并发**。建议：

- 每台采集机独占一台服务端进程/端口；或
- 改造文件名携带设备标识（如 `devA_123_temp.jpg`）并同步改服务端配对逻辑。

---

## 6. 常见故障排查

| 现象 | 排查方向 |
|---|---|
| 客户端 `curl_easy_perform() failed` | 服务端是否启动、IP/端口是否一致、防火墙是否放行 8088 |
| 客户端打印 `UT_GetFrame ERROR` | 机芯未出图：确认 `DEV_OUT_IMAGE` 已置 true、预热完成、USB 连接正常 |
| 服务端窗口不弹图 / 卡住 | 检查 `uploads_konggu/` 是否有图；确认 `temp`/`camera` 成对到达（命名同前缀）；`imshow` 在无桌面环境下会失败 |
| 配对后只处理一遍就停 | `processed_files`/`unprocessed_files` 逻辑：某张失败会一直滞留重试；重启服务或调用 `/clean` 复位 |
| 结果图里热图/可见光没对齐 | `pairAlign.py` 的 `rescale_factor/trans_x/trans_y` 是固定安装标定值，重新标定后修改并重启服务 |
| 启动时交互选择模型被卡住 | 使用了后台启动但未做第 2.3 节的自动选择改造；改用 tmux 或改造代码 |

---

## 7. 一键复位

服务端队列/目录状态异常时，调用（客户端每次启动也会自动调用）：

```bash
curl http://<服务端IP>:8088/clean
```

等价于 `KongguProcessor.clean()`：清空 `uploads_konggu/` 与 `out_konggu/`、复位配对集合与报警位。
