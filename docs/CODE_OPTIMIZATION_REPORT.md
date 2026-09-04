# Konggu 代码优化建议报告

> **适用范围**：`Konggu/main.cpp`、`konggu-server/konggu_server.py`、`konggu-server/KongguProcessFiles.py`（分析时顺带参考了被调用的 `pairAlign.py`）。
> **分析基线**：项目软硬件 Demo 已在真实场景 7×24 稳定运行，本报告以"最小改动 + 加固优先"为原则，所有修改建议均**保留默认行为**，不引入大规模重构（不换线程模型、不换 Web 框架、不改文件组织）。
> **行号说明**：源码无行尾注释、又经历多次编辑，文中行号为当前文件中的**大致位置**，改动前请以函数名/关键字定位为准。

---

## 高优先级（安全漏洞 / 并发未定义行为 / 崩溃 / 数据丢失）

### H1【C++】`curl_global_init` / `curl_global_cleanup` 被并发、反复调用（未定义行为）

- **位置**：`Konggu/main.cpp`
  - `uploadFile()` ≈L150-231（`curl_global_init` L171、`curl_global_cleanup` L229）
  - `cleanServer()` ≈L412-450（init L419、cleanup L448）
- **问题描述**：libcurl 规定 `curl_global_init` 应在程序启动时调用**一次**，`curl_global_cleanup` 必须在**所有** curl 调用结束后、且不能与其它线程中正在进行的 curl 操作并发执行。当前实现把 init/cleanup 放进了每个函数内部，而 `uploadFile` 又由两个 `detach` 线程并发调用。
- **潜在风险**：当线程 A 正在执行 `curl_easy_perform`，线程 B 恰好执行 `curl_global_cleanup`，属于**未定义行为**，可能造成偶发崩溃或全局状态损坏；7×24 长跑时发生概率随运行时间上升。这属于本报告风险最高的一条。
- **修改方案**（最小改动：init 上移到 `main()` 开头一次，函数内删除 init/cleanup）：

改动前（`uploadFile`，L168-172 / L227-231）：
```cpp
bool uploadFile(const string filepath)
{
    // init curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    ...
    // clean curl
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return true;
}
```
改动后：
```cpp
bool uploadFile(const string filepath)
{
    // init 已上移到 main() 启动处，此处不再调用 curl_global_init
    CURL *curl = curl_easy_init();
    if (!curl) { std::cerr << "curl_easy_init failed" << std::endl; return false; }
    ...
    curl_easy_cleanup(curl);
    return true;
}
```
`main()` 顶部（`int main()` 第一行前）增加一次：
```cpp
int main(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT); // 全局只初始化一次
    cleanServer();
    ...
```
`cleanServer()` 内的 `curl_global_init`（L419）与 `curl_global_cleanup`（L448）同样删除；进程常驻 `while(1)` 不退出，`curl_global_cleanup` 可直接省略（由 OS 回收），无需在 main 末尾调用。

- **回归评估**：**不影响现有流程**。网络请求的 URL、回调、上传参数均未改变；仅把 libcurl 的全局初始化时机从"每次上传"提前到"进程启动一次"，消除并发 UB。

---

### H2【C++】`uploadFile` 失败路径泄漏 CURL 句柄；`curl_easy_init` 返回空未判空即使用

- **位置**：`Konggu/main.cpp` `uploadFile()` ≈L150-231
- **问题描述**：
  1. `curl_easy_init()` 返回值未判空，紧接着就对 `curl` 调用 `curl_easy_setopt`；
  2. 当 `curl_easy_perform` 返回失败时，代码在 `curl_mime_free(mime); return false;` 处**提前返回**，跳过了后面的 `curl_easy_cleanup(curl)`——每次失败都泄漏一个 CURL 句柄与连接缓存。
- **潜在风险**：地面站重启、WiFi 抖动等异常时段，上传会成批失败，句柄随之累积泄漏；长时间运行存在句柄/内存耗尽风险，且该路径伴随 H4 的线程堆积问题。
- **修改方案**（统一清理出口，不改成功路径）：

改动前（L174-228 中段）：
```cpp
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_mime *mime = curl_mime_init(curl);
        curl_mimepart *part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, filepath.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_mime_free(mime);
            return false;                       // ← 提前返回，泄漏 curl 句柄
        }
        ...
        curl_mime_free(mime);
    }
    curl_easy_cleanup(curl);                    // 仅在成功路径执行
    return true;
}
```
改动后（用 `bool ok` 记录结果，统一走到函数末尾清理）：
```cpp
    bool ok = true;
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_mime *mime = curl_mime_init(curl);
        curl_mimepart *part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, filepath.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            ok = false;                          // 只标记失败，不再提前 return
        }
        else
        {
            try { json j = json::parse(readBuffer); }
            catch (json::parse_error &ex) { std::cerr << "parse error " << ex.what() << std::endl; }
        }
        curl_mime_free(mime);
    }
    curl_easy_cleanup(curl);
    return ok;
```
> 若更倾向零结构改动，也可仅在 `return false;` 前补一行 `curl_easy_cleanup(curl);`（效果相同，只是重复两处清理代码）。

- **回归评估**：**不影响现有流程**。成功路径与解析逻辑完全一致；失败路径从"泄漏句柄"变为"释放句柄后返回 false"。

---

### H3【C++】上传与 `/clean` 请求未设置超时，网络异常时 detach 线程无限挂起

- **位置**：`Konggu/main.cpp`
  - `uploadFile()` ≈L150-231（未设置 `CURLOPT_TIMEOUT` / `CURLOPT_CONNECTTIMEOUT`）
  - `cleanServer()` ≈L412-450（同上）
- **问题描述**：libcurl 默认不设超时。当服务端不可达、WiFi 断开或服务端响应缓慢时，`curl_easy_perform` 会长时间阻塞在连接/传输上。
- **潜在风险**：
  - 无人机飞出信号区时，上传线程长期挂起 → 线程累积（叠加 H4）；
  - 客户端开机即调 `cleanServer()`，若地面站未就绪，启动流程会一直卡在 GET `/clean`。
- **修改方案**（在两次请求上增加连接/总超时，取较大值避免误伤正常上传）：

改动前（`uploadFile` 内设置 URL 之后）：
```cpp
    curl_easy_setopt(curl, CURLOPT_URL, upload_url.c_str());
```
改动后：
```cpp
    curl_easy_setopt(curl, CURLOPT_URL, upload_url.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L); // 5s 建立连接
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);       // 15s 总超时（含传输）
```
`cleanServer()` 的 GET `/clean` 同理追加上述两行（可仅加 `CURLOPT_TIMEOUT_MS`）。

- **回归评估**：正常网络下**不影响现有流程**（上传通常远小于 15s）；仅在网络异常时不再无限挂起，属**有意的加固**。若担心慢速链路，可把总超时放宽到 30s。

---

### H4【C++】`detach` 上传线程无并发上限、无失败计数（长跑资源耗尽）

- **位置**：`Konggu/main.cpp` 采集主循环 ≈L576-596（`count>30` 后每 10 帧新建两个 `std::thread` 并 `detach()`）
- **问题描述**：上传线程只创建、不 join、无上限、不检查 `uploadFile` 返回值；上传速度跟不上采集速度时线程数单调增长。
- **潜在风险**：服务端卡顿或网络故障期间，线程/内存/文件句柄持续累积，最终可能导致进程崩溃或系统资源耗尽。与 H3 叠加时风险更高。
- **修改方案**（最小加固：用全局原子计数限制"在途上传线程数"，超过阈值即丢帧计数，避免无限 spawn；不改变成功路径）：

在文件顶部全局区（`frame_cnt` 附近）增加：
```cpp
#include <atomic>
std::atomic<int> g_inflight_uploads{0};
const int kMaxInflightUploads = 8;   // 在途上传上限（含两张图）
```
`uploadFile` 入口/出口维护计数（改动前后对照）：
```cpp
// uploadFile() 开头：
bool uploadFile(const string filepath)
{
    g_inflight_uploads.fetch_add(1);          // 进入即占位
    ...  // 原有逻辑不变（含 H2 的清理）
    g_inflight_uploads.fetch_sub(1);
    return ok;
}

// 主循环创建线程处（原 L590-596）：
if (count > 30)
{
    if (g_inflight_uploads.load() < kMaxInflightUploads)
    {
        std::thread t1(uploadFile, path);
        std::thread t2(uploadFile, camerapath);
        t1.detach();
        t2.detach();
    }
    else
    {
        // 超出上限：本次丢帧并计数（至少不无限堆积）
        static int dropped = 0;
        if (++dropped % 100 == 1) std::cerr << "[upload] dropped frame pair, total=" << dropped << std::endl;
    }
}
```
> 说明：上限值 8 是按"每帧两线程"估算的宽松值；正常时远达不到，只有异常时才触发丢帧保护。更保守的方案是把 `kMaxInflightUploads` 调大（如 16）只防极端堆积。

- **回归评估**：**不影响现有流程**（正常上传速度下计数几乎为 0~2，永远不会丢帧）；仅在服务端异常堆积超过 8 个在途线程时开始丢帧并打印告警，避免进程被拖垮。若产品要求"一帧都不能丢"，可把上限调大并配合 H3 超时让线程尽快退出，而不是改采集逻辑。

---

### H5【C++】`UT_Init` 失败路径对**未初始化指针**调用 `UT_Uninit`（崩溃风险）

- **位置**：`Konggu/main.cpp` `main()` ≈L480-489
  ```cpp
  void * pInstance;                       // ← 未初始化
  UT_RESULT res = UT_Init(&pInstance, UT_JX007);
  cout<<"100"<<endl;
  if (res != UT_OK)
  {
      UT_Uninit(pInstance);               // ← res!=OK 时 pInstance 内容未知
      return -1;
  }
  ```
- **问题描述**：`pInstance` 声明后未赋初值。若 `UT_Init` 失败（机芯未连接、USB 未就绪等），传入 `UT_Uninit` 的是未初始化指针（甚至可能是成功分支才写入的有效句柄），属未定义行为。
- **潜在风险**：开机时热像机芯未就绪/线缆松动是真实场景，此路径会向 SDK 传入垃圾句柄，轻则异常退出、重则崩溃或污染 SDK 内部状态。
- **修改方案**（初始化 + 判空后清理）：

改动前：
```cpp
    void * pInstance;
    UT_RESULT res = UT_Init(&pInstance,UT_JX007);
```
改动后：
```cpp
    void * pInstance = nullptr;
    UT_RESULT res = UT_Init(&pInstance,UT_JX007);
    ...
    if (res != UT_OK)
    {
        if (pInstance) UT_Uninit(pInstance);   // 仅当句柄有效才清理
        return -1;
    }
```
- **回归评估**：**不影响现有流程**（成功路径行为不变）；只修复失败路径的未定义行为。

---

### H6【Python】`/upload` 未净化文件名，存在**路径穿越**（任意文件写入）

- **位置**：`konggu-server/konggu_server.py` `upload_file()` L88-98
  ```python
  @app.route('/upload', methods=['POST'])
  def upload_file():
      if 'file' not in request.files:
          return 'No file part', 400
      file = request.files['file']
      if file.filename == '':
          return 'No selected file', 400
      os.makedirs(upload_dir, exist_ok=True)
      sav_p = os.path.join(upload_dir, file.filename)   # ← 未净化
      file.save(sav_p)
  ```
- **问题描述**：`file.filename` 由客户端（或攻击者）完全控制。`os.path.join(upload_dir, file.filename)` 若遇到 `../../xxx.jpg` 或绝对路径，会把文件写到 `uploads_konggu/` **之外**的任意位置。
- **潜在风险**：服务监听 `0.0.0.0:8088` 且 `/upload`、`/clean` 无鉴权。一旦接入不可信网络或出现异常设备，攻击者可覆盖服务器任意可写文件（如模型目录、启动脚本），甚至结合其它漏洞提权；即使只是误配置也会污染文件系统。
- **修改方案**（取纯文件名 + 后缀白名单校验，最小加固）：

改动前（L92-97）：
```python
    file = request.files['file']
    if file.filename == '':
        return 'No selected file', 400
    os.makedirs(upload_dir, exist_ok=True)
    sav_p = os.path.join(upload_dir, file.filename)
    file.save(sav_p)
```
改动后：
```python
    import os
    file = request.files['file']
    if file.filename == '':
        return 'No selected file', 400
    fname = os.path.basename(file.filename)          # 去掉任何目录成分
    if not (fname.endswith('_temp.jpg') or fname.endswith('_camera.jpg')):
        return 'Illegal filename', 400               # 只收约定命名
    os.makedirs(upload_dir, exist_ok=True)
    sav_p = os.path.join(upload_dir, fname)
    file.save(sav_p)
```
> 若担心历史/自研客户端文件名不满足 `_temp.jpg/_camera.jpg` 约定，可放宽为 `fname.endswith('.jpg')`，但**必须**保留 `os.path.basename` 一层，这是防穿越的关键。

- **回归评估**：**不影响现有流程**。仓库客户端上传名恒为 `{n}_temp.jpg` / `{n}_camera.jpg`，`basename` 不改变这些合法名；仅拦截带目录成分或非法后缀的请求。

---

### H7【Python】无显示器环境 `cv2.imshow`/`waitKey` 崩溃，且该文件对被永久卡死

- **位置**：`konggu-server/KongguProcessFiles.py` `process_file_pair()` L78-79
  ```python
      cv2.imshow('result window', cv2.resize(orig_img, (new_width, new_height)))
      cv2.waitKey(30)
  ```
- **问题描述**：`cv2.imshow`/`waitKey` 需要图形环境。在无 DISPLAY 的 Linux/SSH 环境（或 Windows 服务会话）中会抛 `cv2.error`；该异常被函数末尾的 `except Exception` 捕获并返回 `False`，于是 `process_file_pairs` 里的 `if not ...: continue` 分支**不删除**这对文件 → 每轮监控都重试、每次都失败，该文件对永久滞留并反复刷异常。
- **潜在风险**：一旦把地面站改造成无人值守/远程部署，整条推理链因一个窗口调用瘫痪，且异常文件对永远无法自我恢复。
- **修改方案**（加显示开关，默认保持现状；`__init__` 中探测一次）：

改动前（L78-79）：
```python
            cv2.imshow('result window', cv2.resize(orig_img, (new_width, new_height)))
            cv2.waitKey(30)
```
改动后（并在 `__init__` 中加开关）：
```python
            # __init__ 中： self.enable_show = os.environ.get('KONGGU_SHOW', '1') != '0'
            if self.enable_show:
                cv2.imshow('result window', cv2.resize(orig_img, (new_width, new_height)))
                cv2.waitKey(30)
```
同时给 `except Exception`（L~86-88）补堆栈便于定位：
```python
        except Exception as e:
            import traceback; traceback.print_exc()   # 打印完整堆栈，不再静默
            return False
```
- **回归评估**：**不影响现有流程**（默认 `KONGGU_SHOW=1` 行为与现状完全一致）；仅当显式设置 `KONGGU_SHOW=0`（无头部署）时跳过窗口，结果文件照常落盘。

---

## 中优先级（可维护性 / 可读性 / 可移植性）

### M1【C++】启动时 `input()` 阻塞 → 支持环境变量/命令行选择模型（无人值守启动）

- **位置**：`konggu-server/konggu_server.py` L16-29（模块顶层 `while True: input()`）
- **问题描述**：模型选择通过阻塞式 `input()` 完成，且位于模块顶层。无人值守部署（systemd/计划任务/开机自启）时进程会停在交互处等待键盘输入；作为库被 import 时也会直接卡住。
- **潜在风险**：把地面端改为远程/自动启动后，服务无法自起；7×24 运行如需自动重启即会失败。
- **修改方案**（支持环境变量 `KONGGU_MODEL=1|2`，提供时跳过交互；未提供时完全保持原样）：

改动前（L16-29）：
```python
while True:
    user_input = input("提供低温版本和高温版本模型，请根据使用场景选择适用的模型：键入1为低温，键入2为高温: ")
    if user_input == '1':
        model_path="konggu_models/cold-best.pt"; print("已加载低温版本模型A.pt"); break
    elif user_input == '2':
        model_path = "konggu_models/hot-best.pt"; print("已加载高温版本模型B.pt"); break
    else:
        print("无效的输入，请键入1或2来选择模型。")
```
改动后：
```python
import os
auto_model = os.environ.get('KONGGU_MODEL', '')   # 无人值守：KONGGU_MODEL=1/2
if auto_model in ('1', '2'):
    user_input = auto_model
else:
    while True:
        user_input = input("...键入1为低温，键入2为高温: ")
        if user_input not in ('1', '2'):
            print("无效的输入，请键入1或2来选择模型。"); continue
        break
if user_input == '1':
    model_path = "konggu_models/cold-best.pt"; print("已加载低温版本模型")
else:
    model_path = "konggu_models/hot-best.pt"; print("已加载高温版本模型")
```
- **回归评估**：**不影响现有流程**（不设环境变量时交互行为与现状一致）；仅当设置 `KONGGU_MODEL` 时跳过人工输入。

---

### M2【C++】调试输出刷屏（每帧 `cout<<res`、中心温度 printf、`100`/`gua`、删除成功打印）

- **位置**：`Konggu/main.cpp`
  - 采集主循环每帧 `cout<<res<<endl`（≈L530）
  - Y16 分支每帧 `printf("Center = %f framecount %d\n", ...)`（≈L556）
  - 预热区 `cout<<"100"<<endl`（≈L500/504/520）、`cout<<"gua"<<endl`（≈L580）
  - `deleteFilesInDirectory` 每个文件打印"删除文件成功"（≈L55）
- **问题描述**：这些是调机期残留的无意义/高频率输出；7×24 运行会持续写终端/日志。
- **潜在风险**：日志体积无限增长（树莓派 SD 卡被写满）；printf/cout 在每帧热路径上带来不必要开销；干扰真实故障排查。
- **修改方案**（删除无意义输出，中心温度降频或保留但注明用途；以下为删除示意）：
```cpp
// 改动前（主循环每帧）
cout<<res<<endl;
// 改动后：删除本行

// 改动前（Y16 分支）
printf("Center = %f framecount %d\n", ...);
// 改动后（如需保留温度监控，改为每 N 帧打印一次；否则删除）
if (frame_cnt % 30 == 0) printf("Center = %f framecount %d\n", ...);

// 改动前：cout<<"100"<<endl; 与 cout<<"gua"<<endl;
// 改动后：删除
```
- **回归评估**：**不影响现有流程**（纯输出清理，不动任何逻辑/时序）。

---

### M3【C++】硬编码参数集中为命名常量（保留现有值）

- **位置**：`Konggu/main.cpp`
  - 服务端地址 `url`（L61）
  - `count % 10 == 0` 存图节拍（≈L573）、`count > 30` 上传起点（≈L577）
  - 调色板 `UT_PALETTE_IRON`（≈L516）、数据目录 `"../data"`（多处）
- **问题描述**：这些"魔法值"散落各处，改参数需全局搜索且易漏。
- **潜在风险**：更换服务端 IP/端口、调整采集节拍时改错/漏改导致联调失败。
- **修改方案**（文件顶部集中定义，默认值 = 现值；不改程序读取逻辑）：
```cpp
// 改动前：const string &url = "http://192.168.26.215:8088";  // 全局 L61
// 改动后：
const string SERVER_URL   = "http://192.168.26.215:8088";  // 默认值保持原样
const string upload_url   = SERVER_URL + "/upload";
const string clean_url    = SERVER_URL + "/clean";
const int    SAVE_EVERY_N_FRAMES = 10;   // 原 count%10
const int    UPLOAD_AFTER_FRAME   = 30;  // 原 count>30
```
主循环中 `if (count % SAVE_EVERY_N_FRAMES == 0)`、`if (count > UPLOAD_AFTER_FRAME)`。
- **回归评估**：**不影响现有流程**（常量值 = 原值）。后续如需接入 `config.example.ini`（见 L2）可作为过渡。

---

### M4【Python】`/clean` 无鉴权：可被任意可达设备触发全量清空（数据丢失面）

- **位置**：`konggu-server/konggu_server.py` L72-86（`clean()` 路由）
- **问题描述**：`GET /clean` 会删除 `uploads_konggu/` 与 `out_konggu/` 全部文件并重置状态，但无任何鉴权/来源校验；服务监听 `0.0.0.0:8088`。
- **潜在风险**：一旦该端口暴露到不可信网络，任何人 `curl http://host:8088/clean` 即可清空全部推理结果与待处理缓存，造成**数据丢失**。
- **修改方案**（轻量来源限制，保留现有调用方）：

改动前（L72）：
```python
@app.route('/clean', methods=['GET'])
def clean():
```
改动后（对局域网私有网段放行 + 可选的固定 Token 校验）：
```python
from functools import wraps
TRUSTED_TOKEN = os.environ.get('KONGGU_CLEAN_TOKEN', '')   # 默认空=不启用

def require_clean_auth(f):
    @wraps(f)
    def wrapper(*a, **kw):
        if TRUSTED_TOKEN and request.headers.get('X-Clean-Token') != TRUSTED_TOKEN:
            return 'Forbidden', 403
        return f(*a, **kw)
    return wrapper

@app.route('/clean', methods=['GET'])
@require_clean_auth
def clean():
```
（客户端 `cleanServer()` 若启用 Token，需在 GET 请求头加 `X-Clean-Token`；默认 `KONGGU_CLEAN_TOKEN` 为空时行为与现状完全一致。）
- **回归评估**：默认（不设 Token）**不影响现有流程**；启用 Token 后仅拒绝无 Token 的 `/clean`，客户端需配套加一个请求头。

---

### M5【Python】监控线程长时间持锁执行 YOLO/写盘，`/clean` 会被阻塞

- **位置**：`konggu-server/KongguProcessFiles.py`
  - `monitor_directory()` ≈L138-143：`with self.list_lock:` 包住"扫描 + 全部文件对处理（含 YOLO 推理、imshow、imwrite）"
  - `clean()` ≈L145-152：同样需要 `list_lock`
- **问题描述**：推理属于重活，却在持锁期间完成；一旦某个周期内积压多对文件，锁被长时间占用，`/clean`（客户端开机调用）会一直等待。
- **潜在风险**：客户端启动时若服务端恰好积压推理任务，`GET /clean` 响应变慢甚至看起来"卡住"；`/upload` 虽不碰这把锁（只写目录），但推理线程独占 CPU 也会拖慢上传处理。
- **修改方案**（缩小加锁粒度：锁内只做"发现新文件 + 取走待处理清单"，锁外做推理。需要给处理函数传入清单并谨慎维护状态；属结构性小改）：

改动前（L138-143）：
```python
    def monitor_directory(self):
        while True:
            with self.list_lock:
                self.check_for_new_files()
                self.process_file_pairs()
            time.sleep(1)
```
改动后（示意：先拷贝待处理对，锁外处理）：
```python
    def monitor_directory(self):
        while True:
            with self.list_lock:
                self.check_for_new_files()
                pending = [f for f in self.unprocessed_files]  # 快照
            for filename in pending:
                with self.list_lock:
                    if filename not in self.unprocessed_files:
                        continue
                self.process_one(filename)          # 把"配对一个+推理"抽成单文件方法
            time.sleep(1)
```
> 注意：`process_file_pair` 会读写 `unprocessed_files/processed_files`，抽出后需在每个状态变更点重新取锁，**改动面比前几条大**。若当前 7×24 运行未出现 `/clean` 长时间阻塞，本项可暂缓，作为可维护性改进排期。

- **回归评估**：处理逻辑与文件命名规则不变时，**不改变功能结果**；但并发时序略有变化（clean 可与推理并行完成），上线前建议回归验证一遍"推理中触发 /clean"的场景。

---

### M6【Python】死代码与误导性残留清理

- **位置**：
  - `konggu-server/konggu_server.py`
    - `show_data()` L47-61（对应线程已在 L127 注释停用，未启动）
    - `resize_and_align()` L63-66（空函数体，无调用）
    - `monitor_last_time()` L109-120（无任何启动调用；其中调用的 `merge_images_with_overlap` 也只在 L134 注释中出现）
    - `upload_file()` 内维护的 `temp_file_path_list/camera_file_path_list`（L102-107）——现在由 `KongguProcessor` 直接扫目录，这两个列表仅被已停用的 `show_data`/`clean` 引用，属于旧架构残留
    - 未使用 import：`scipy`、`natsort`、`re`（若清理上述函数后）
  - `KongguProcessFiles.py` 注释残留旧领域语义：`xywh_to_newxywh_and_ltrb` 文档串"to contain the battery"、`# 识别出来是电动车的座位`（≈L62）、`# 将框往下延伸一点，包住电池所在区域`
- **问题描述**：死代码让维护者误以为存在"双队列 + 配对线程 + 全景线程"机制；误导性注释与本项目"墙体缺陷检测"的语义不符。
- **潜在风险**：后人按注释理解系统结构导致误改；增加审查成本。
- **修改方案**（删除未启动函数与相应 import；把旧领域注释改写为墙体缺陷语义）：
```python
# konggu_server.py：删除 show_data / resize_and_align / monitor_last_time 三个定义
#                   及其 import（scipy、natsort、re，若确认无其它引用）
# KongguProcessFiles.py：把"电动车的座位/电池所在区域"注释改为——
#   # 类别 0：缺陷目标（此处对检测框做向下延展标注，参数见 xywh_to_newxywh_and_ltrb）
```
- **回归评估**：**不影响现有流程**（上述函数/线程本就未被启动；删除后行为不变）。删除前请再全局搜索确认无引用。

---

### M7【Python】上传目录与结果目录只增不删，长跑磁盘占用无界

- **位置**：`konggu-server/KongguProcessFiles.py`（`upload_dir` 文件处理成功后**不删除**，仅从内存 `unprocessed` 移除）；清理只发生在 `/clean`
- **问题描述**：7×24 运行时，`uploads_konggu/` 与 `out_konggu/` 每帧落盘持续累积，只有客户端重启触发 `/clean` 才清空。
- **潜在风险**：若客户端常驻不重启，磁盘最终写满 → 上传保存失败/系统故障，且期间数据只增不减。
- **修改方案**（可选保留策略，需产品确认"是否保留原始图/结果图"后再定）：
  - 若不需要保留原始图：`process_file_pairs` 处理成功后 `os.remove` 这对源文件（删除 `_temp/_camera.jpg` 源文件），保留 `out_konggu` 结果；
  - 若结果图也需定期清理：在 `monitor_directory` 中按 `out_dir` 内文件总数/总大小做阈值，超过即删除最旧结果（或通知运维执行 `/clean`）。
- **回归评估**：**会改变现有行为**（当前保留所有原始图与结果）。实施前请先确认保留策略；若希望严格不变，可仅作为监控告警项（磁盘用量超阈值时日志告警），不改删除逻辑。

---

### M8【Python】推理输入是"可见光裁剪区"而非热像图（语义待与算法确认）

- **位置**：`konggu-server/KongguProcessFiles.py` L55 `results = self.model(rgb_img)`；`rgb_img` 来自 `pair_align_func_konggu` 返回的 `covered_visible_img`（可见光裁剪区，见 `pairAlign.py` L83 注释 "裁剪后的可见光图像区域"）
- **问题描述**：源码中作者自己也留了疑问注释 `#####? 为什么这里模型推理使用的是rgb_img？ #####`（L54）。结合"Demo 已把推理输入切到热像图"的事实，仓库参考实现与最终形态存在差异。
- **潜在风险**：若按本仓库代码误认为"模型吃可见光"，会导致后续人员照搬错误配置。
- **修改方案**：本项**不改代码**，仅建议在代码注释/文档中明确标注"此为早期参考实现；当前 Demo 的推理输入为热成像图像（见 README 八）"，避免误导。
- **回归评估**：仅注释说明，**不影响现有流程**。

---

## 低优先级（性能 / 扩展准备，可酌情实施）

### L1【C++】手写 `rgb2bgr` 像素循环 + `cv::flip` 改用 OpenCV 原子操作

- **位置**：`Konggu/main.cpp` `rgb2bgr()` ≈L452-466；主循环 ≈L583-588
- **问题描述**：手工逐像素交换 R/B（640×512×3 三重循环）后再 `cv::Mat` + `cv::flip`；可用 `cv::cvtColor` 一步完成，代码更短且可调用 SIMD。
- **修改方案**：
```cpp
// 改动前：
rgb2bgr(imagergb);
cv::Mat img(IMAGE_H, IMAGE_W, CV_8UC3, imagergb.data());
cv::flip(img, img, -1);
// 改动后：
cv::Mat img(IMAGE_H, IMAGE_W, CV_8UC3, imagergb.data());
cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
cv::flip(img, img, -1);
// 之后可删除 rgb2bgr() 定义
```
- **回归评估**：输出像素完全一致，**不影响现有流程**；仅省一次遍历的耗时。

### L2【Python】`orig_img` 被 resize 两次（存盘一次、显示一次）

- **位置**：`KongguProcessFiles.py` L75-79
- **问题描述**：`cv2.imwrite(..., cv2.resize(...))` 与 `cv2.imshow(..., cv2.resize(...))` 对同一 `orig_img` 各 resize 一次，浪费 CPU。
- **修改方案**：先 `resized = cv2.resize(orig_img, (new_width, new_height))`，存盘与显示都用 `resized`。
- **回归评估**：输出一致，**不影响现有流程**。

### L3【Python】异常处理打印完整堆栈（可观测性）

- **位置**：`KongguProcessFiles.py` `except Exception as e: print(e); return False`（≈L86-88）
- **问题描述**：仅 `print(e)` 看不到出错函数与行号；7×24 运行中偶发异常难以定位。
- **修改方案**：`import traceback; traceback.print_exc()`（见 H7 附注），或将本文件接入 `logging`。
- **回归评估**：**不影响现有流程**；仅增强日志。

### L4【C++】C 风格 `char path[1024]; sprintf(...)` 改为 `std::string`

- **位置**：`Konggu/main.cpp` 保存/上传路径构造 ≈L575-578 等
- **修改方案**：`std::string path = "../data/" + std::to_string(count) + "_temp.jpg";`（上传线程已按值传 `string`，安全）。
- **回归评估**：路径字符串一致，**不影响现有流程**。

### L5【Python】空函数 `resize_and_align` 补实现或删除

- **位置**：`konggu_server.py` L63-66
- **建议**：确认无历史调用后删除（并入 M6），或按 docstring 补全实现。

### L6【全仓】接入 `config.example.ini`（参数外部化）作为后续迭代

- **位置**：仓库根目录已有 `config.example.ini`（模板已写好，但程序不读取）
- **建议**：把 M3 的命名常量、M1 的模型选择、对齐参数 `rescale_factor=1.5/trans_x=100/trans_y=-10`（`KongguProcessFiles.py` L45）统一抽到配置，默认值 = 现值，读取失败时回退默认。
- **回归评估**：读取失败/无配置文件时**回退默认值 = 现状**，保证不破坏现有部署。

### L7【Python】前缀提取用更稳的方式

- **位置**：`KongguProcessFiles.py` L50/75 的 `re.split("[./_]", camera_file)[-3]`
- **建议**：命名约定固定为 `{n}_temp.jpg`，可改 `camera_file.split('_')[0]` 更直观；或抽成 `_prefix_of(name)` 小函数。
- **回归评估**：当前命名下结果一致，**不影响现有流程**。

---

## 最终实施优先级清单

| 优先级 | 编号 | 文件 | 一句话摘要 |
|---|---|---|---|
| 🔴 高 | H1 | main.cpp | curl 全局 init/cleanup 并发调用 → 上移 `main()` 只初始化一次 |
| 🔴 高 | H2 | main.cpp | uploadFile 失败路径泄漏 CURL 句柄 / init 判空 |
| 🔴 高 | H3 | main.cpp | 上传与 /clean 增加连接/总超时，防无限挂起 |
| 🔴 高 | H4 | main.cpp | detach 线程加在途计数上限，防资源耗尽 |
| 🔴 高 | H5 | main.cpp | `pInstance` 初始化 + 失败路径判空再 `UT_Uninit` |
| 🔴 高 | H6 | konggu_server.py | /upload 文件名 basename 净化 + 后缀白名单（防路径穿越） |
| 🔴 高 | H7 | KongguProcessFiles.py | `cv2.imshow` 加 `KONGGU_SHOW` 开关 + 异常打印堆栈 |
| 🟡 中 | M1 | konggu_server.py | 模型选择支持环境变量 `KONGGU_MODEL`（无人值守） |
| 🟡 中 | M2 | main.cpp | 清理每帧 `cout<<res`/`100`/`gua` 等刷屏调试输出 |
| 🟡 中 | M3 | main.cpp | 硬编码参数集中为命名常量（值不变） |
| 🟡 中 | M4 | konggu_server.py | /clean 可选 Token 鉴权（默认关闭=现状） |
| 🟡 中 | M5 | KongguProcessFiles.py | 缩小监控线程加锁粒度，避免阻塞 /clean |
| 🟡 中 | M6 | 两 Python 文件 | 删除死代码函数 + 修正"电池/座位"误导注释 |
| 🟡 中 | M7 | KongguProcessFiles.py | 上传/结果目录增长策略（需先确认保留策略） |
| 🟡 中 | M8 | KongguProcessFiles.py | 注明"推理输入为可见光"是早期形态，不改代码 |
| 🟢 低 | L1 | main.cpp | 手写 RGB 转换改用 `cvtColor` |
| 🟢 低 | L2 | KongguProcessFiles.py | `orig_img` resize 复用，去掉重复 resize |
| 🟢 低 | L3 | KongguProcessFiles.py | 异常打印完整堆栈 |
| 🟢 低 | L4 | main.cpp | `char[1024]+sprintf` → `std::string` |
| 🟢 低 | L5 | konggu_server.py | 删除空函数 `resize_and_align` |
| 🟢 低 | L6 | 全仓 | 后续接入 `config.example.ini`（失败回退默认） |
| 🟢 低 | L7 | KongguProcessFiles.py | 前缀提取改用 `split('_')[0]` |

> **建议实施顺序**：先 H1→H6 七个加固点（全部可在不改成功路径前提下完成，建议在现有 7×24 环境旁开一次灰度验证）；再按团队节奏消化 M1-M8；L 级可随重构顺手处理。M5/M7 涉及并发与保留策略，需先与算法/运维确认再动。

---

*本报告仅基于仓库代码静态分析生成，未对运行中系统做侵入式改动；所有"改动后"代码均需在 Demo 环境回归验证后合入。*
