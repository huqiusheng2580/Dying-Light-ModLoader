这是一个针对 Chrome Engine 6 游戏（消逝的光芒 Dying Light）的 Mod 加载器 ，项目名 CE6ML（Chrome Engine 6 Mod Loader）。通过 DLL 劫持 方式注入游戏进程。

## 工作原理
1. DLL 代理（注入入口） — dllmain.cpp

编译产物被命名为`xinput1_3.dll` （见 CMakeLists.txt ），游戏启动时当作系统 XInput 库加载。它转发 3 个手柄 API 到系统真 DLL（见 xinput.def ），同时执行自己的初始化逻辑。

2. Mod 加载 — loader.ixx

- 在游戏目录创建`mods` 文件夹
- 挂载`.pak` /`.mpak` 压缩包为虚拟文件系统源，支持`load_order.txt` 控制加载顺序
- 加载`.rpack` 资源包（通过 hook`PackLoader::Load` ，用 AOB 特征码定位）
- 在`engine_x64_rwdi.dll` 中把条件跳转`74` 补丁为`EB` ， 强制开启`.rpacz` （压缩资源包）加载
3. 完整性校验绕过 — multiplayerHooks.ixx

Hook 三个校验函数并强制返回`true` ：

函数 作用 `fs::calc_file_crc` 文件 CRC 校验 `fs::izipped_buffer_file::check_zip_crc` ZIP 包 CRC 校验 `IGame::AreDataAuthenticatedToPlayMultiplayer` 多人游戏数据认证

这让打了 mod 的客户端能通过游戏的反篡改检测， 实现 mod 状态下联机 。

4. Hook 框架 ：使用 safetyhook 做 inline hook，C++23 modules 编写。

## 流程图

`游戏启动 → 加载假 xinput1_3.dll → DllMain
    ├─ 转发 XInput 导出（伪装）
    ├─ mods\debug.txt 存在则开控制台
    ├─ multiplayer::Init()  → hook 3 个校验函数，强制返回 true
    └─ hooks::Init()
         ├─ patch rpacz 跳转（74→EB）
         ├─ hook PackLoader::Load → 加载 mods/*.rpack
         ├─ hook fs::add_source
         └─ hook InitializeGameScript → 触发挂载 mods/*.pak（按 load_order.txt）`

一句话总结： 这是一个通过 xinput DLL 劫持注入消逝的光芒（CE6）的 mod 加载器，挂载 pak/rpack 格式的 mod 文件，并绕过文件 CRC 与多人联机校验。