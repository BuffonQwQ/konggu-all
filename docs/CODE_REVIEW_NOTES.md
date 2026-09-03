 代码审查备注（CODE REVIEW NOTES）

> 本文件是**记录性文档**：列出源码中缺少注释、存在潜在风险（安全 / 并发 / 稳定性）的位置。
> 按约定不在此修改代码，仅作为后续维护与重构的待办清单。
>
> 覆盖范围：`client/`（C++）与 `server/`（Python）。行号基于整理时的源码版本，
> 随改动可能漂移，请以函数/符号名定位为准。

---

## 一、安全相关

### 1.1 上传接口存在路径穿越风险（server/konggu_server.py → upload）
`upload_file()` 直接用客户端上报的 `file.filename` 拼路径：

```python
sav_p = os.path.join(upload_dir, file.filename)
file.save(sav_p)
```

`file.filename` 未经任何校验/清洗。若 filename 含 `../` 或子目录（如
`../../etc/xxx.jpg`），可写到 `uploads_konggu` 之外（受限于运行用户权限，但仍是越权写）。
**建议**：仅保留安全基底名 `os.path.basename()`，并对扩展名白名单（`.jpg`）校验。

### 1.2 /upload、/clean 无任何鉴权
两者都是裸 HTTP：
- `/clean` 会清空目录并复位状态，任何人可触发（配合 1.1 可作为破坏手段）；
- `/upload` 可被任意人灌入文件触发 YOLO 推理（资源消耗 / DoS）。

**建议**：可信内网部署（当前用途），或加简单 token/API-Key、或用反向代理做访问控制。
客户端调用处（`main.cpp` 的 `cleanServer()`/`uploadFile()`）也要相应携带凭据。

### 1.3 无 TLS / 无加密
图像内容与接口均明文 HTTP，跨公网会泄露画面。属内网/实验性部署可接受。

---

## 二、并发与稳定性（客户端 C++）

### 2.1 上传线程无上限、无失败重传（client/main.cpp）
每 10 帧创建两个 `std::thread` 并 `detach()`：

```cpp
std::thread t1(uploadFile, path);
std::thread t2(uploadFile, camerapath);
t1.detach();
t2.detach();
```

- 无线程池/信号量/任务上限：网络慢或服务端不可达时，未完成的上传线程会不断堆积；
- `uploadFile` 失败仅打印错误即返回，**图片不会重传** → 该帧数据直接丢失；
- 线程回调里 `WriteCallback` 追加到**各自独立的** `readBuffer`（局部变量），无共享写竞争，
  但 `curl_global_init/cleanup` 在每线程内成对调用——libcurl 要求 `curl_global_*` 全局只调用一次，
  多线程并发调用存在未定义行为风险。
  **建议**：`curl_global_init` 移到 `main()` 一次；用固定数量的上传线程 + 有界任务队列；
  或用阻塞式主循环上传（代价是会拖慢取帧）。

### 2.2 全局摄像头对象在 main 前构造
```cpp
cv::VideoCapture cap(0);
```
`cap` 是**文件作用域全局对象**，在 `main()` 之前构造。若开机时摄像头尚未枚举完成，
`cap.isOpened()` 可能为假且后续无法重试（程序直接 return -1）。
**建议**：移到 `main()` 内构造，并允许失败后重试若干次。

### 2.3 清理/退出代码不可达
主循环是 `while (1)`，其后的 `DEV_OUT_IMAGE(false)`、`UT_Uninit` 永远不会执行。
进程靠 kill 结束，机芯/摄像头资源不做优雅释放。长期后台运行（配合 `Restart=always`）
靠重启兜底，尚可接受；若要优雅停机需引入信号处理（SIGINT/SIGTERM → 置位 → 跳出循环）。

### 2.4 启动时序靠 `sleep(1)` 硬等
通过 `count` 到 5（约 5 秒）才 `DEV_OUT_IMAGE`，注释说明是"连接机芯有时间差"。
硬编码延时在机芯枚举变慢时可能失败；**建议**改为轮询设备状态/错误码而非固定 sleep。

### 2.5 大量历史/死代码与调试输出
- `main.cpp` 顶部保留整段未启用的 `circular_queue` 结构、`parse_data_run`/`read_drame_run`
  被注释的实现（对应旧线程模型，参考 `main-backup.cpp`）；
- `main.cpp` 混用 `count` 既作预热计数又作帧计数，`count>30`、`count%10` 等魔数无命名；
- 调试输出：`cout<<"100"`、`printf("gua")`、`cout<<res` 等遍布主循环，建议收敛为日志开关；
- `UTDF.h` 内注释存在编码乱码（GBK 内容被以其它编码写入），不影响编译但影响阅读；
- `saveIntArrayToMatFile`、`dump_to_file`、`enqueue/dequeue`、`initializeQueue` 等已无人调用。

### 2.6 RGB→BGR 手工转换 + flip 有性能与正确性隐患
`rgb2bgr()` 逐像素手工换通道后再 `cv::flip(img,img,-1)`：
- 手工像素循环较慢（可用 `cv::cvtColor(..., COLOR_RGB2BGR)`）；
- `flip(-1)`（水平+垂直镜像）是**针对当前安装方向写死**的，换安装方向会镜像错位；
  该镜像逻辑与服务端对齐参数（pairAlign 的 trans）共同依赖"固定安装位姿"，文档已注明需重标定。

### 2.7 温度数据未利用
Y16→`UT_AnalysisTempFrame` 得到全幅 float 温度，只打印中心像素温度，其余计算被浪费
（但保住了"帧率"需求）。若目标是热异常判定，属于未完成功能而非缺陷，记录在案。

---

## 三、并发与稳定性（服务端 Python）

### 3.1 监控线程长时间持锁（KongguProcessFiles.py）
```python
def monitor_directory(self):
    while True:
        with self.list_lock:          # ← 持锁区间内含 YOLO 推理 + imshow
            self.check_for_new_files()
            self.process_file_pairs()
        time.sleep(1)
```
`process_file_pairs` → `process_file_pair` 内包含**模型推理与磁盘写**（耗时可达数百 ms~秒级），
全部在 `self.list_lock` 临界区内执行。后果：
- `/clean` 路由也要抢同一把锁，会**被推理阻塞**，表现为 clean 接口卡顿；
- 上传线程（Flask 每请求一线程）在 `with lock:` 里只做 list append，临界区短，尚安全；
- 若把锁粒度放小（只保护文件集合增删），可避免长临界区，但需保证"配对+处理"的原子性。

### 3.2 无配对超时/清理（与 README「已知限制」一致）
`unprocessed_files` 中凑不齐一对（缺 temp 或缺 camera）的文件会**永久滞留**并每秒重扫一次。
长时间运行会累积。**建议**：按文件 mtime 超过阈值即标记为坏对并清理/告警。

### 3.3 cv2.imshow 在无头环境抛异常
`process_file_pair` 中 `cv2.imshow`/`cv2.waitKey` 失败会抛异常 → 函数返回 False →
文件留在队列反复重试（配合 3.2 会恶性循环）。无头部署需要 GUI 开关（见 DEPLOYMENT 第 2.5 节）。

### 3.4 clean 与 monitor 的竞态（整体流程）
`/clean` 在 `processor.clean()` 里同时取 `list_lock` 和 `alarm_lock`；监控线程只取 `list_lock`。
锁顺序上 clean 先 list 后 alarm，暂无反向获取 alarm→list 的路径，目前无死锁；
但 `clean()` 里 `cv2.destroyAllWindows()` 与推理线程的 `imshow` 并发调用 OpenCV GUI，
可能跨线程冲突。建议 GUI 调用集中在同一线程。

### 3.5 死代码 / 未启用功能
- `konggu_server.py`：`show_data()`（注释“有使用吗？？”）、`monitor_last_time()`（注释
  “哪里用上了？？”）与模块级 `temp_file_path_list/camera_file_path_list/cur_*` 变量，
  主流程只用到列表做接收缓冲，`monitor_last_time` 的"2 秒无新图 → 全景拼接"逻辑**从未被启动**；
- `MergeImage.py` 的全景拼接功能整体未接入主流程；
- `scipy`、`natsort` 被 import 但未使用；
- `test_flask.py` / `test_camera.py` / `main.py` 为早期原型/测试入口，非主流程文件。
  **建议**：要么接入要么删除，减少误导（多处代码注释自己都在问"用在哪"）。

### 3.6 推理输入用可见光图而非热图
`self.model(rgb_img)`：`pair_align_func_konggu` 返回的 `covered_visible_img`（可见光区域）
被送去 YOLO，`aligned_infrared_img`（对齐后的热图）**没有参与推理**，只用于保存
`{n}_blended.jpg` 融合图。源码留有疑问注释。若目标是"热图上的电池/座位框"，这里逻辑需复核。
另：`xywh_to_newxywh_and_ltrb` 的 `h_zoom_in_scale=1` 传参使"框延展"目前实际不放大。

### 3.7 目录/文件清理是"删整个目录"
`cleanFolder()` 遍历并 `os.remove` 目录内所有文件（含 `out_konggu` 下的推理结果）。
若推理结果需要归档，目前没有任何留存机制，clean 即全丢。

### 3.8 结果覆盖
`cv2.imwrite(f'{out_dir}/{前缀}.jpg', ...)` 用**序号前缀**做文件名：不同采集轮次若序号
相同会互相覆盖（与 3.2 / DEPLOYMENT 第 5 节"多机不支持"相关）。

---

## 四、代码可读性与注释缺口（供整理）

| 位置 | 现状 |
|---|---|
| `client/main.cpp` 顶部 url/端口 | 无注释说明是"服务端地址，部署必改"（已在模板文档补充） |
| `client/main.cpp` 中 `#if 1` 分支 | `#if 1/#else` 两分支宽度高度完全相同，疑似遗留开关，建议删掉 |
| `client/main.cpp` `count` 变量 | 预热计数与帧计数复用，缺注释 |
| `client/main.cpp` `saveIntArrayToMatFile` | 注释与实现（imwrite int 数组为图）用法不明，疑似调试残留 |
| `server/KongguProcessFiles.py` class 0 | 注释"识别出来是电动车的座位"，模型类别字典未集中定义，0/类别编号为魔数 |
| `server/pairAlign.py` | `trans_x=100, trans_y=-10` 等标定参数含义仅见调用处注释，建议抽取为常量+单位说明 |
| `server/konggu_server.py` 顶部 while/input | 缺少"模型文件必须存在、否则给出错误"的检查（路径错误时 YOLO 抛错较晦涩） |
| 多处 | 中文注释混杂少量乱码（`UTDF.h`）、全角/半角混用 |

---

## 五、小结（优先级建议）

1. **必改（安全）**：上传文件名清洗（1.1）；`/clean`、`/upload` 加最小鉴权或限定内网（1.2）。
2. **应改（稳定性）**：上传线程模型收敛（2.1）；`curl_global_init` 只调一次（2.1）；
   无头 GUI 开关（3.3）；配对超时清理（3.2）。
3. **可改（健壮/维护）**：优雅停机（2.3）；去掉死代码或接入全景拼接（3.5）；
   复核推理输入图选择（3.6）；把固定标定参数集中化并注释（表格末行）。
4. **记录性质**：温度数据未参与决策（2.7）、镜像方向写死（2.6）、结果覆盖（3.8）。
