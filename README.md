 Konggu（空鼓/热隐患巡检）：红外 + 可见光双模态采集与 YOLO 推理预警原型

> **一句话概述**：`Konggu` 是一个面向电动自行车场景（座位/电池区域热隐患）的
> **边缘采集客户端（C++）+ 服务端推理展示（Python/Flask + YOLO）** 原型系统——
> 客户端驱动红外热成像机芯（UThermalLib SDK，型号 JX007）与 USB 可见光摄像头，
> 将"伪彩色热图 + 可见光图"成对上传；服务端负责对齐融合、YOLO 目标检测（可选低温/高温模型）
> 并在本地窗口实时展示标注结果。

> ⚠️ 状态说明：本项目由教学/调试用途代码演化而来，属于**工作原型**而非生产级产品。
> 目录中保留了较多注释性/历史性代码（详见 `docs/CODE_REVIEW_NOTES.md`）。

---

## 一、系统架构

采集与推理被拆分为两个独立程序，通过 HTTP 通信：

```mermaid
flowchart LR
    subgraph client["采集客户端 (client/ - C++ 边缘设备)"]
        A["红外机芯 UT-JX007<br/>(UThermalLib SDK)"]
        E["USB 可见光摄像头<br/>(OpenCV VideoCapture)"]
        A -->|"Y16 温度帧"| B["温度解析<br/>仅打印中心温度"]
        A -->|"Y8 RGB 帧"| C["RGB→BGR + 镜像<br/>640 x 512"]
        C --> D["每 10 帧存 {n}_temp.jpg"]
        E --> F["count>30 后每 10 帧<br/>抓拍 {n}_camera.jpg"]
        D --> G["本地 data/ 缓存目录"]
        F --> G
        G --> H["libcurl 多部分表单上传<br/>(每次两张各开一个 detach 线程)"]
    end

    H -- "POST /upload  (字段名 file)" --> S["Flask 服务 0.0.0.0:8088<br/>(konggu_server.py)"]
    S --> I["uploads_konggu/ 收图<br/>按后缀进 temp/camera 两个队列"]
    S --> J["监控线程<br/>每 1s 轮询目录配对"]

    subgraph server["推理/展示服务端 (server/ - Python 工作台)"]
        J -- "同前缀 {n}_temp.jpg / {n}_camera.jpg" --> K["pairAlign.py 对齐融合<br/>(可见光放大1.5x + 平移对齐)"]
        K --> L["YOLO 推理 (cold/hot-best.pt)"]
        L --> M["标注座位框并保存<br/>out/{n}.jpg + out/{n}_blended.jpg"]
        M --> N["cv2.imshow 实时窗口展示"]
    end

    S -- "GET /clean (客户端启动时调用)" --> client
```

**设计要点**（均来自代码实际行为）：

- 客户端把**温度数据（Y16→温度数组）仅用于打印中心温度**，并不上传；上传的是伪彩色热图（铁红 palette）与可见光图。
- 服务端推理输入实际是**对齐后裁剪出的可见光图区域**（`covered_visible_img`），而非热图（源码中留有该疑问注释）。
- 客户端上传是"每 10 帧、两张图各自 `std::thread().detach()`"的临时策略，服务端则依赖**文件名前缀配对**，二者通过约定的 `${序号}_temp.jpg` / `${序号}_camera.jpg` 命名衔接。

---

## 二、数据流时序图

```mermaid
sequenceDiagram
    autonumber
    participant C as 客户端 main.cpp
    participant S as 服务端 Flask
    participant T as 监控/配对线程

    Note over C: 启动流程
    C->>S: GET /clean  (请求清空服务端缓存)
    S-->>C: Successfully cleaned!
    C->>C: 删除本地 data/ 旧图、打开可见光摄像头
    C->>C: UT_Init(JX007) → 注册回调
    loop 预热: sleep(1) x5
    end
    C->>C: DEV_OUT_IMAGE 开启出图 + 设置 IRON 调色板

    loop 采集主循环 (每帧)
        C->>C: UT_GetFrame 取帧
        alt 帧类型 = Y16 (温度)
            C->>C: UT_AnalysisTempFrame → 打印中心温度
        else 帧类型 = Y8 (图像)
            C->>C: UT_AnalysisImageFrameRGB → BGR + flip
            Note over C: 每当 count % 10 == 0:
            C->>C: 保存 data/{count}_temp.jpg
            alt count > 30 (预热期结束)
                C->>C: 从可见光摄像头抓拍 data/{count}_camera.jpg
                C->>S: POST /upload  ({count}_temp.jpg)
                C->>S: POST /upload  ({count}_camera.jpg)
                S-->>C: HTTP 200 {result:1}
            end
        end
    end

    Note over S: /upload 处理器按后缀入队
    S->>S: temp_file_path_list / camera_file_path_list
    loop 监控线程每 1s 轮询
        T->>T: 扫描 uploads_konggu/ 找出未处理 jpg
        T->>T: 按前缀匹配文件对 (n_temp.jpg, n_camera.jpg)
        T->>T: pairAlign 缩放/平移/裁剪对齐 → blend
        T->>T: YOLO 推理 → class 0 座位框延展标注
        T->>T: 输出 out/{n}.jpg、out/{n}_blended.jpg
        T->>N: cv2.imshow 窗口展示
    end
```

---

## 三、目录结构

> 现有源码目录（`Konggu/`、`konggu-server/`、`TestCamera/`）与下述标准 GitHub 布局的
> 对应关系，见文末"从现有目录迁移"一节。

```
konggu-all/
├── README.md                      # 本文件：概述、架构、依赖、构建运行
├── .gitignore                     # 忽略编译产物 / 缓存 / 临时上传
├── config.example.ini             # 运行时参数模板（当前为硬编码参考）
│
├── client/                        # ── 采集/上传端（C++，嵌入式/Linux）──
│   ├── CMakeLists.txt             # 构建脚本示例（自动查找 OpenCV/CURL/json 等）
│   ├── main.cpp                   # ← 源码来自 Konggu/main.cpp（JX007 主程序）
│   ├── main-backup.cpp            # ← 源码来自 Konggu/main-backup.cpp（旧线程版，仅参考）
│   ├── include/                   # ← Konggu/include/  UT SDK 头文件
│   │   ├── UThermalLib.h
│   │   ├── UTDF.h
│   │   └── UTERROR.h
│   ├── lib/                       # ← Konggu/lib/  供应商 SDK 预编译 .so
│   ├── data/                      # 运行时：本地图片缓存（自动清空/写入）
│   └── tools/
│       └── camera_main.cpp        # ← TestCamera/camera_main.cpp 摄像头自检工具
│
├── server/                        # ── 推理/展示端（Python/Flask + YOLO）──
│   ├── requirements.txt           # Python 依赖清单
│   ├── konggu_server.py           # ← konggu-server/konggu_server.py  服务入口
│   ├── KongguProcessFiles.py      # ← 目录监控 + 文件对配对 + YOLO 推理
│   ├── pairAlign.py               # ← 红外/可见光对齐、裁剪、融合
│   ├── MergeImage.py              # ← 全景拼接工具（当前主流程未调用）
│   ├── konggu_models/             # ← YOLO 权重（cold-best.pt / hot-best.pt）
│   ├── uploads_konggu/            # 运行时：接收的图片（自动创建）
│   └── out_konggu/                # 运行时：推理结果输出（自动创建）
│
└── docs/                          # ── 文档 ──
    ├── DEPLOYMENT.md              # 多机/单机部署、后台运行、开机自启
    └── CODE_REVIEW_NOTES.md       # 代码注释缺失点 / 潜在风险记录（不改代码）
```

各目录用途小结：

| 目录 | 用途 |
|---|---|
| `client/` | 运行在采集侧（树莓派/嵌入式 Linux）的 C++ 程序：初始化热成像机芯、抓拍可见光、成对上传 HTTP。 |
| `server/` | 运行在工作台的 Python 服务：接收上传、轮询配对、图像对齐融合、YOLO 检测、窗口展示、结果落盘。 |
| `docs/` | 部署指南与代码审查记录。 |
| `data/`、`uploads_konggu/`、`out_konggu/` | 运行期产物目录，不入库（见 `.gitignore`）。 |

---

## 四、硬件与软件依赖

### 4.1 硬件

| 组件 | 说明 | 出处 |
|---|---|---|
| 红外热成像机芯 | 型号 **UT-JX007**（`UT_Init(&pInstance, UT_JX007)`），通过 UThermalLib SDK 驱动 | `client/main.cpp` |
| 可见光摄像头 | USB 摄像头 `/dev/video0`（OpenCV `VideoCapture(0)`） | `client/main.cpp`、`TestCamera/camera_main.cpp` |
| 采集端主控 | 运行 Linux 的边缘设备（源码含 RK 平台/树莓派交叉编译线索：`rknn_api`、rockchip 工具链注释） | `client/CMakeLists.txt`（注释） |
| 推理端主机 | 运行 Python + PyTorch/YOLO 的工作站（需 CUDA 显卡以利用 `torch.cuda` 加速） | `server/konggu_server.py` |

> 注意：CMake 注释中残留了 aarch64（JX003）与 rockchip830 交叉编译路径，且链接了
> `rknn_api`，暗示该 SDK 历史上有多个平台（JX002/003/004/007）与 NPU 加速的使用经历；
> 本模板**不假定**你手头是哪块板子，仅以 JX007 + `lib/` 内 .so 为准。

### 4.2 客户端软件依赖（C++）

| 依赖 | 用途 | 获取方式 |
|---|---|---|
| CMake ≥ 3.10 | 构建 | 发行版包管理器 |
| GCC/G++（支持 C++11） | 编译 | 发行版包管理器 |
| **OpenCV**（`cv::VideoCapture`、`cv::imwrite`） | 可见光取图/图像读写 | `find_package(OpenCV)` |
| **libcurl**（多部分表单上传 / GET） | HTTP 上传与 `/clean` | `find_package(CURL)` |
| **nlohmann/json** | 解析服务端 JSON 响应 | `find_package(nlohmann_json)` 或头文件 |
| pthread | 上传线程 | `find_package(Threads)` |
| UThermalLib SDK 私有库 | 机芯驱动 | 随项目 `lib/` 分发（供应商提供） |
| `libunitcam.so`、`libusb-1.0.so` | 机芯通信底层 | 同上 |
| `libThermaltool.so`、`libimage_algorithms_lite.so` | 温度/图像算法 | 同上 |
| `librknn_api.so` | RKNN NPU 运行时（部分平台需要） | 同上 / 瑞芯微 SDK |

### 4.3 服务端软件依赖（Python）

由代码 import 提取（详见 `server/requirements.txt`）：

- `torch` + `torchvision`（代码导入 `torch.cuda`；建议装 CUDA 版）
- `ultralytics`（YOLO 推理，`from ultralytics import YOLO`）
- `opencv-python`（图像处理/显示）
- `flask`（HTTP 服务）
- `numpy`
- `Pillow`（`pairAlign.py`/`test_flask.py` 用到）
- `matplotlib`（`pairAlign.py` 用到 cm/colors）
- `scipy`、`natsort`（`konggu_server.py` 中导入但**当前代码并未真正使用**，历史遗留）
- 推理权重：`server/konggu_models/cold-best.pt`（低温）、`hot-best.pt`（高温）

---

## 五、安装与编译

### 5.1 客户端（C++）

Ubuntu/Debian 类系统为例，安装系统依赖：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libopencv-dev libcurl4-openssl-dev nlohmann-json3-dev
# 若发行版无 nlohmann-json 包，可将 nlohmann/json.hpp 放入 SDK include 目录
```

确认 SDK 私有库与头文件就位后编译：

```bash
cd client
cmake -S . -B build
cmake --build build -j
# 若 SD 库不在 ./lib，可用 -DKONGGU_SDK_DIR=/绝对/路径 指定
```

运行（需在有显示/取流权限的采集机上，从 `build/` 内执行以匹配相对路径 `../data`）：

```bash
cd build
export LD_LIBRARY_PATH=../lib:$LD_LIBRARY_PATH   # 运行时能找到私有 .so
./main
```

> 交叉编译：把 `client/CMakeLists.txt` 中注释的交叉工具链区段取消注释并改成你的编译器即可。

### 5.2 服务端（Python）

建议使用虚拟环境：

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate            # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

若装有 CUDA 环境，可先单独安装匹配版本的 GPU 版 PyTorch（详见 PyTorch 官方安装页），
再执行上面的 `pip install -r requirements.txt`（`ultralytics` 会自动补齐其余依赖）。

启动（需在 `server/` 目录内执行，模型路径与上传/输出目录均为相对路径）：

```bash
python konggu_server.py
# 启动时会交互询问：键入 1 加载低温模型，键入 2 加载高温模型
```

---

## 六、如何修改运行时参数

> ⚠️ **重要提示**：当前版本几乎所有参数都是**写死在源码中**的，程序**并不会读取**
> `config.example.ini`。该配置文件仅作为"可调参数清单 + 未来改造目标"提供给维护者。
> 修改参数后需要重新编译（C++）或重启服务（Python）。

### 6.1 客户端（`client/main.cpp`）

| 想改什么 | 找到哪里 | 说明 |
|---|---|---|
| 服务端地址/端口 | 全局 `url` 常量（`main.cpp` 顶部） | 形如 `http://192.168.x.x:8088`；`/upload`、`/clean` 基于它拼接 |
| 图像分辨率 | `IMAGE_W` / `IMAGE_H`（`#define`） | 当前恒为 `640x512`，需与机芯输出一致 |
| 保存/上传节拍 | `count % 10 == 0` | 改为 `% N` 可调"每 N 帧存一对图" |
| 上传起始帧 | `if (count > 30)` | 前 30 帧仅本地存热图、不上传（预热） |
| 调色板 | `UT_PALETTE_TYPE pt = UT_PALETTE_IRON` | 可换成 `WHITEHOT`、`RAINBOW` 等（见 `UTDF.h`） |
| 摄像头编号 | `cv::VideoCapture cap(0)` | 0 = `/dev/video0` |
| 本地缓存目录 | `"../data"`（含删除/保存路径） | 注意是相对 `build/` 运行目录 |

### 6.2 服务端（Python）

| 想改什么 | 找到哪里 | 说明 |
|---|---|---|
| 监听地址/端口 | `app.run(host='0.0.0.0', port=8088)`（`konggu_server.py`） | 客户端 `url` 需与之一致 |
| 模型 | 启动时的 `input()` 分支，映射到 `konggu_models/xxx-best.pt` | 目前必须人工 1/2 选择，无法从外部传入 |
| 上传/输出目录 | 模块级 `upload_dir='uploads_konggu'`、`out_dir='out_konggu'` | 相对运行目录 |
| 轮询间隔 | `time.sleep(1)`（`monitor_directory`） | 扫描上传目录的频率 |
| 图像对齐参数 | `pair_align_func_konggu(..., rescale_factor=1.5, trans_x=100, trans_y=-10)`（`KongguProcessFiles.py`） | 与红外/可见光的**固定安装位姿**相关，更换安装后需重标定 |
| 输出图宽 | `new_width = 640`（`KongguProcessFiles.py`） | 推理结果保存宽度 |
| 融合透明度 / 全景重叠 | `pairAlign.py` 中 `addWeighted(...,0.5,0.5,...)`；`MergeImage.py` 的 `overlap_length=50` | 后一个当前未接入主流程 |

---

## 七、已知限制与设计权衡

（逐条对应当前代码的真实取舍；更完整的安全/并发清单见 `docs/CODE_REVIEW_NOTES.md`）

1. **"每 10 帧丢两个 detach 线程上传"是稳定性权宜之计**：`main.cpp` 为不阻塞采集主循环，
   每次上传都新建 `std::thread` 并 `detach()`，**没有线程池/上限/失败重传**。网络抖动时线程可能堆积。
   早期版本（`main-backup.cpp`）用"读线程 + 解析线程 + 环形队列"的解耦方案，但被简化为当前同步循环，
   故 `main.cpp` 中保留了循环队列等一整套未启用的结构体与注释代码（历史遗留）。
2. **服务端依赖文件名配对且无超时机制**：只有 `{n}_temp.jpg` 或只有 `{n}_camera.jpg` 到达时，
   该文件会一直滞留在 `unprocessed_files` 等待另一半，不会自动清理/告警。
3. **推理输入是可见光图而非热图**：`KongguProcessFiles.py` 中 `self.model(rgb_img)` 传入的是
   裁剪后的可见光区域，源码里也留有"为什么推理用 rgb_img"的疑问注释。若业务目标是热异常检测，
   这属于**待确认的设计取舍**。
4. **`cv2.imshow` 需要图形桌面**：服务端在无显示环境（SSH/无头服务器）下会因 imshow 抛异常，
   导致该文件对处理失败并反复重试。当前无"无头模式"开关。
5. **温度数据只打印不上传**：客户端解析出整幅温度数组，仅打印中心温度；检测决策完全依赖视觉模型。
6. **存在未接入的死代码**：`konggu_server.py` 中 `show_data()`（注释"有使用吗??"）、
   `monitor_last_time()`（注释"哪里用上了??"）以及 `MergeImage.py`（全景拼接）定义了但**从未被启动线程调用**。
7. **调试期残留**：客户端大量 `cout<<"100"`、`printf("gua")` 等调试输出；`UTDF.h` 部分注释存在
   编码乱码；`main-backup.cpp` 与 `Konggu/main-backup.cpp` 为历史版本备份。

---

## 八、License

> 项目基于内部/教学代码整理，暂未指定开源许可证。若需对外发布，请先在仓库添加
> `LICENSE` 文件，并确认 SDK（UThermalLib 等私有库与模型权重）的分发许可。
