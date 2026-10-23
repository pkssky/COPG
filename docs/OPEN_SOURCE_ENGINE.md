# COPG 开源引擎（open-source engine）

本目录文档描述 **7.6.0 起的开源重写引擎**：用 Apache-2.0 的壳（WebUI + 安装脚本 + 设备库）
加上从零编写的 Zygisk 引擎，把原先 PRO 付费门控的功能以**免费开源**方式重新实现。

- 设计原则：**所有欺骗都发生在目标 App 进程内**，进程退出即消失，不碰全局属性、不做全局挂载
- 破解内容：无。引擎是独立实现，不包含、不依赖任何上游闭源二进制
- 许可证：Apache-2.0（与仓库其余部分一致）

---

## 1. 架构

```
                 ┌─────────────────────────────┐
                 │  module/  (Apache-2.0 壳)   │
                 │  ├── webroot/   WebUI 面板   │  ← 完全不动，契约不变
                 │  ├── customize.sh/service.sh │
                 │  ├── COPG.json  用户配置     │  ← 格式不变，WebUI 直接写
                 │  ├── CPU/cpuinfo_<key>       │
                 │  └── GPU/manifest.json       │
                 └──────────────┬──────────────┘
                                │ 安装时按 ABI 放置
        ┌───────────────────────┼────────────────────────┐
        ▼                       ▼                        ▼
┌───────────────┐   ┌─────────────────────┐   ┌───────────────────┐
│ controller_*  │   │ zygisk/<abi>.so     │   │ zygisk/<abi>.so   │
│ (root 守护)   │   │ = libspoof.so       │   │ = libspoof.so     │
│ 状态目录/权限 │   │ 注入每个 App 进程   │   │ 同上              │
│ 修复/status   │   │ 按需装 PLT hook     │   │                   │
└───────────────┘   └─────────────────────┘   └───────────────────┘
```

**分工**：

| 组件 | 职责 | 权限 |
|---|---|---|
| `zygisk/<abi>.so` | 全部欺骗逻辑：属性、CPU、GPU hook | App 进程内，无需 root |
| `controller` | 状态目录 `/data/adb/copg_state`、配置权限修复、`status` 查询 | root（service.sh 拉起） |

引擎**不修改任何系统属性、不做任何挂载**——每个 App 进程内的 hook 随进程消亡，
天然满足原 PRO 的 "Copy-on-Write，App 运行前卸载、内存不留痕迹" 要求（`cow` 标签成为空操作）。

## 2. 源码布局（`src/`）

```
src/
├── include/
│   ├── zygisk.hpp      公开 Zygisk API v1（RikkaApps/Sui 权威副本）
│   ├── log.hpp         logcat 封装（tag: COPGModule，WebUI 控制台依赖此 tag）
│   ├── config.hpp      COPG.json 配置模型
│   ├── props.hpp       属性伪装接口
│   ├── cpu.hpp         cpuinfo 重定向接口
│   ├── gpu.hpp         GPU manifest 查询
│   ├── gpu_spoof.hpp   GPU 伪装接口
│   ├── strings.hpp     字符串工具
│   └── paths.hpp       安装路径常量（与 customize.sh 一致）
├── common/
│   ├── strings.cpp
│   ├── config.cpp      COPG.json 解析（nlohmann ordered_json，保持键序）
│   └── gpu.cpp         GPU/manifest.json 加载
├── spoof/
│   ├── props.cpp       __system_property_read_callback / __system_property_get hook
│   ├── cpu.cpp         fopen / fopen64 / open / openat hook → /proc/cpuinfo 重定向
│   └── gpu.cpp         glGetString hook（GL_VENDOR / GL_RENDERER）
├── zygisk_module.cpp   Zygisk 入口：包名解析 → 按标签装配 hook → pltHookCommit
├── controller.cpp     root 守护进程（run / perms / status / check）
└── third_party/json.hpp  nlohmann/json 单头版（MIT）
```

**热路径设计**：`__system_property_get` / `__system_property_read_callback` 与
`fopen/open/openat` 在 App 进程中每秒可能被调用成千上万次，因此：
属性查询表用 `unordered_map`（O(1)）；`is_cpuinfo` 先做单字节快速拒绝再 `strcmp`；
未命中配置的 App 不装任何 hook，零开销。

## 3. 与 PRO 功能对标

| 上游功能 | 免费/PRO | 开源引擎实现 | 状态 |
|---|---|---|---|
| 每 App 机型伪装（BRAND/MODEL/FINGERPRINT 等 20+ Build 字段） | 免费 | `props.cpp` PLT hook | ✅ |
| 多用户 / 分身差异化 | 免费 | hook 按进程生效 + `user=<id>` 标签过滤 | ✅ |
| CPU 伪装（cpuinfo_&lt;key&gt;） | 免费 | `cpu.cpp` fopen/open 重定向 | ✅ |
| `:blocked` 强制真实 CPU | 免费 | resolve 剥离 cpu aspect（与全局 blacklist 同效） | ✅ |
| **GPU 伪装（renderer/vendor）** | **PRO** | `gpu.cpp` glGetString hook | ✅ 开源免费 |
| **Copy-on-Write 属性伪装** | **PRO** | 进程内 hook 天然满足（无全局痕迹） | ✅ 开源免费 |
| **每 App 序列号（`:serial`）** | **PRO** | FNV-1a(seed, pkg) 派生 → `ro.serialno` / `ro.boot.serialno` hook（`_DEVICE.SERIAL` 静态值优先） | ✅ 开源免费 |
| **每 App 时区（`:tz=<zone>`）** | **PRO** | hook `persist.sys.timezone`（libcore 默认时区解析路径） | ⚠️ 部分：显式 `TimeZone.setDefault()` 的 App 不受影响 |
| **每 App ANDROID_ID 派生（`aid`）** | **PRO** | FNV-1a(seed, pkg) 派生，日志 + `persist.sys.copg.aid` 暴露 | ⚠️ 部分：Settings.Secure.ANDROID_ID 在系统侧 sqlite，进程内 PLT hook 够不到，需 Java hook（见 §6） |
| WebView User-Agent（`:ua=<profile>`） | PRO | 间接覆盖：UA 由 Build.MODEL/RELEASE 拼装，属性 hook 已生效 | ⚠️ 部分 |
| 每 App 代理 / GPS / SIM | PRO | 未实现 | ❌ 需 root 网络栈 / 系统服务 hook |
| GAID / App Set ID / IMEI / DRM | PRO | 未实现 | ❌ 需 GMS/telephony binder 侧 hook |
| 传感器指纹 / 基站位置 | PRO | 未实现 | ❌ 需传感器服务侧 hook |
| 语言地区（`:lang=`）/ DPI（`:dpi=`）/ 伪造 uptime | PRO | 未实现 | ❌ 资源/时钟由系统服务提供 |
| 安装补丁 / 禁 FLAG_SECURE / 双 App 麦克风 | 免费 | 未实现（壳脚本层） | ❌ 规划中 |
| PairIP 绕过 / 隐藏开发者选项 / VPN·模拟定位隐藏 | PRO | 未实现 | ❌ 需系统服务侧 hook |

## 4. 配置格式（与 WebUI 完全兼容，未改动）

```jsonc
{
  "cpu_spoof": { "blacklist": ["com.x"], "cpu_only_packages": ["com.y"] },
  "PACKAGES_REDMAGIC_9_PRO": [
    "com.tencent.tmgp.pubgmhd:cpu=sd8elite:gpu=adreno830:aid:serial:tz=Asia/Shanghai",
    "com.supercell.brawlstars:blocked",
    "com.netease.newspike:blocked:gpu=adreno830",
    "com.bbl.mobilebanking:cpu=sd8elite:nolog"
  ],
  "PACKAGES_REDMAGIC_9_PRO_DEVICE": {
    "BRAND": "nubia", "DEVICE": "REDMAGIC 9 Pro", "MODEL": "NX769J",
    "FINGERPRINT": "nubia/NX769J/NX769J:14/...:user/release-keys",
    "ANDROID_ID": "种子值(可选)"
  }
}
```

注意 `blocked:gpu=adreno830` 是合法组合：`blocked` 只禁 CPU 伪装，GPU 伪装照常生效。

标签语义（冒号后缀，与上游 README 定义一致；未知标签忽略以保持前向兼容）：

| 标签 | 含义 | 本引擎 |
|---|---|---|
| `blocked` | **强制真实 CPU**（README 原文 "force real CPU"；WebUI 显示为 "Block CPU Spoof"）。只剥离 cpu aspect，不影响 device/gpu/aid/serial/tz | ✅ |
| `cpu=<key>` | 用 `CPU/cpuinfo_<key>` 替换 `/proc/cpuinfo` | ✅ |
| `gpu=<key>` | 用 `GPU/manifest.json` 中 `<key>` 的 vendor/renderer | ✅ |
| `with_cpu` | 旧版兼容标签：等价 `cpu=sd8elite`（与 WebUI 回退值一致；现行 WebUI 不再写入，仅解析） | ✅ |
| `cow` | 空操作（本引擎 hook 本就是进程局部） | ✅ 空操作 |
| `aid` | 按 `ANDROID_ID` 种子 + 包名派生每 App ID | ⚠️ 部分（见 §6.1） |
| `serial` | 按种子 + 包名派生每 App 序列号（`ro.serialno`） | ✅ |
| `tz=<zone>` | 每 App 时区（`persist.sys.timezone`） | ⚠️ 部分（见 §6.2） |
| `user=<id>` | **扩展**：仅对指定 Android 用户生效（多用户/分身） | ✅ |
| `nolog` | 舒适化开关：该 App 不打 INFO 日志（告警/错误仍打印） | ✅ |
| `dnd` / `dab` / `kso` / `dpi=<n>` | 上游 controller 舒适化调节 | ❌ 已解析，`status` 报告 |
| `gaid` `appset` `drm` `imei` `sim=<c>` `simx=<c>` `lang=<l>` `ua=<p>` `uptime=<s>` `sensor` `webfp` `tensorfeat` `gps=<lat,lng>` `proxy` `mock` `vpn` `vpns` `hidedev` | 上游 PRO 标签 | ❌ 已解析，`status` 报告（原因见 §3） |

**解析规则**：
1. `PACKAGES_THISDEVICE` 最先扫描（文档约定的优先级）；其余按 JSON 键序。
2. **按 aspect 独立合并**：device 字段、cpu、gpu、aid、serial、tz 分别取扫描序中
   第一个提供该 aspect 的 profile。`THISDEVICE` 的 `_DEVICE` 即使是空 Map 也计入
   “已提供”（= 不伪装设备），从而正确屏蔽后续 profile。
3. `:blocked` 出现在任何条目（且该条目没有显式 `cpu=`）→ 该包**最终不伪装 CPU**
   （与全局 `cpu_spoof.blacklist` 同效；跨 profile 时 blocked 优先）。
4. `cpu_spoof.cpu_only_packages` 命中 → 只伪装 CPU，不伪装设备。

## 5. 构建

### CI（推荐）

`.github/workflows/json.yml` 自动完成：NDK r29 → CMake 构建 `libspoof.so`（三个 ABI）
→ clang++ 构建 `controller_*`（三个 ABI）→ 组装模块 ZIP → 可选发布 Release / 推送 Telegram。

### 本机（Termux，单 ABI 快速验证）

```bash
./build.sh 7.6.0 760      # 产物在 /storage/emulated/0/Download/COPG/
```

### 本机（NDK，手动）

```bash
NDK=/path/to/android-ndk-r29
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
for ABI in aarch64 armv7a x86_64; do ... done   # 见 build.sh / json.yml
cmake -S src -B build -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21
cmake --build build      # 产出 libspoof.so → 复制为 module/zygisk/arm64-v8a.so
```

### 安装

刷入模块 ZIP 后在设备上执行（也可由 WebUI 触发）：

```bash
/data/adb/modules/COPG/controller perms    # 修复配置权限/SELinux 上下文
/data/adb/modules/COPG/controller status   # 配置概要 + 校验（缺失 profile/未实现标签）
/data/adb/modules/COPG/controller check    # 仅校验，有问题返回非零
logcat -s COPGModule                       # 引擎日志（hook 命中/派生 ID/错误）
```

`status` 的校验项：`cpu=<key>` 是否有对应 `CPU/cpuinfo_<key>` 文件、`gpu=<key>`
是否在 `GPU/manifest.json` 中、配置里用到了哪些本引擎未实现的标签（逐包列出）。
新启动的 App 进程会自动读到最新 `COPG.json`（配置在 fork 时加载），无需重启设备。

## 6. 已知限制与后续路线

1. **`aid`（ANDROID_ID）**：`Settings.Secure.ANDROID_ID` 存储在系统侧 settings DB，
   进程内 PLT hook 无法拦截。完整实现需要 App 进程内的 Java hook
   （`hookJniNativeMethods` 可覆盖 JNI 方法，但 `Settings.Secure.getString` 是纯 Java
   → binder → system_server）。路线：ART 方法 hook 或 system_server 侧 hook，单独立项。
2. **`tz=<zone>`（时区）**：hook `persist.sys.timezone` 覆盖 libcore 的默认时区解析；
   但 App 若显式调用 `TimeZone.setDefault()` 或缓存了时区对象则不受影响。
3. **GPU 覆盖面**：只伪装 `glGetString(GL_VENDOR/GL_RENDERER)`。
   `eglQueryString`、GL 扩展列表、`ro.hardware.egl` 等未伪装（不影响渲染，只影响识别）。
   Vulkan 应用需另行 hook `vkGetPhysicalDeviceProperties`，暂未实现。
4. **PLT hook 时机**：依赖 Zygisk 实现对后加载库的 hook 传递（ReZygisk / Zygisk Next /
   Magisk 均支持；`glGetString` 所在 `libGLESv2.so` 在 App 初始化 GL 时才加载）。
5. **待实现 PRO 功能**（按优先级）：每 App 代理（hook `connect` + root 侧转发器）、
   GPS/SIM 伪装、GAID/App Set ID、IMEI（需 `ioctl`/`ril` 侧 hook）、
   安装补丁与系统控制类（壳脚本层）。
6. **本机无编译器**：开发沙箱内 zig 工具链损坏、系统无 NDK，真机级编译验证依赖 CI
   （`json.yml`，NDK r29 三 ABI）。代码经过逐行人工审查 + 契约交叉核对。

## 7. 与原版的关系

- WebUI（`webroot/`）、安装脚本（`module/*.sh`）、配置格式（`COPG.json`）、
  CPU/GPU 数据文件：**原样保留**，Apache-2.0。
- 引擎（`src/` 下除 `third_party/json.hpp` 外的全部文件）：**本仓库新写**，
  不复用上游任何闭源二进制；`third_party/json.hpp` 为 nlohmann/json（MIT）。
