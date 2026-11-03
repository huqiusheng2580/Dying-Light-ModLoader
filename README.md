# CE6ML — Chrome Engine 6 Mod Loader

针对 Chrome Engine 6 游戏（消逝的光芒 Dying Light / 死亡岛终极版）的 Mod 加载器。通过 `xinput1_3.dll` 劫持（DLL 代理）注入游戏进程，挂载 pak/rpack 格式的 mod 文件，并绕过文件 CRC 与多人联机校验。

## 工作原理

1. **DLL 代理（注入入口）** — [dllmain.cpp](src/dllmain.cpp)

   编译产物命名为 `xinput1_3.dll`，游戏启动时当作系统 XInput 库加载。它转发 3 个手柄 API 到系统真 DLL（见 [xinput.def](xinput.def)），同时执行初始化逻辑。

2. **Mod 加载** — [loader.hpp](src/loader.hpp)

   - 挂载 `.pak` / `.mpak` 为虚拟文件系统源，加载顺序由 `CE6ML.ini` 的 `[load_order]` 控制
   - 加载 `.rpack` 资源包（hook `PackLoader::Load`，AOB 特征码定位）
   - 把 `engine_x64_rwdi.dll` 中的条件跳转 `74` 补丁为 `EB`，强制开启 `.rpacz`（压缩资源包）加载

3. **完整性校验绕过** — [multiplayerHooks.hpp](src/multiplayerHooks.hpp)

   Hook 三个校验函数并强制返回 `true`，使 mod 客户端能通过反篡改检测进行联机：

   | 函数 | 作用 |
   |---|---|
   | `fs::calc_file_crc` | 文件 CRC 校验 |
   | `fs::izipped_buffer_file::check_zip_crc` | ZIP 包 CRC 校验 |
   | `IGame::AreDataAuthenticatedToPlayMultiplayer` | 多人游戏数据认证 |

4. **Hook 框架** — [safetyhook](https://github.com/cursey/safetyhook)（inline hook，含 zydis 反汇编器）

## 配置（CE6ML.ini）

首次启动游戏时自动在**游戏目录下的 `Game_Mods\`** 生成，UTF-8 编码，支持 `;` / `#` 注释：

```ini
[general]
; 是否显示调试控制台 (true/false)
debug = false

[paths]
; mod 目录：绝对路径，或相对于游戏目录的路径
mod_dir = Game_Mods

[load_order]
; pak 加载顺序：数字越小越先加载
1 = example.pak
```

- 新的 pak 文件会被自动追加到 `[load_order]` 末尾
- 配置为纯文本，用任意文本编辑器（如记事本、VS Code）直接修改即可，保存后重启游戏生效

## 构建

无需 CMake/Visual Studio，只需 **git** 和 **MinGW g++**（C++23，建议 GCC 14+）：

```powershell
.\build.ps1                # Release 构建
.\build.ps1 -Config Debug  # Debug 构建
.\build.ps1 -Clean         # 清理后重建
```

脚本自动完成：

- 克隆 safetyhook（main）和 zydis（v4.1.0，含 zycore 子模块）到 `deps\`，GitHub 直连失败时自动切换镜像源
- 用 g++ 直接编译全部源码（增量检测，含头文件时间戳）
- 静态链接 gcc 运行时，产物 `build\xinput1_3.dll` 无 MinGW DLL 依赖

## 使用

1. 运行 `.\build.ps1` 构建
2. 把 `build\xinput1_3.dll` 复制到游戏目录（与游戏 exe 同级）
3. 启动游戏，首次运行会生成 `Game_Mods\` 目录和其中的 `CE6ML.ini`
4. 将 mod 文件（`.pak` / `.mpak` / `.rpack`）放入 mod 目录，需要调整加载顺序时手动编辑 `CE6ML.ini` 的 `[load_order]`

## 项目结构

```
├── src/
│   ├── dllmain.cpp              # DLL 入口、XInput 代理转发
│   ├── config.hpp               # CE6ML.ini 解析/生成（保留注释）
│   ├── utils.hpp                # 控制台、AOB 扫描、内存补丁、编码转换
│   ├── loader.hpp               # pak / rpack 加载逻辑
│   ├── hooks.hpp                # 引擎 hook 安装与 rpacz 补丁
│   ├── multiplayerHooks.hpp     # 联机校验绕过
│   ├── safetyhook_easy_shim.hpp # create_inline 的轻量实现（绕过 MASM 依赖）
│   └── globals.hpp              # PackLoader 枚举定义
├── build.ps1                    # 纯 g++ 构建脚本
└── xinput.def                   # XInput 导出定义
```

## 流程图

```
游戏启动 → 加载 xinput1_3.dll → DllMain
    ├─ 转发 XInput 导出（伪装）
    ├─ CE6ML.ini [general] debug=true 则开控制台
    ├─ multiplayer::Init() → hook 3 个校验函数，强制返回 true
    └─ hooks::Init()
         ├─ patch rpacz 跳转（74→EB）
         ├─ hook PackLoader::Load → 加载 <mod 目录>/*.rpack
         ├─ hook fs::add_source
         └─ hook InitializeGameScript → 挂载 <mod 目录>/*.pak（按 [load_order]）
```
