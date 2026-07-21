# 仓库 Agent 规范

## Windows 编译工具链

- 本仓库统一通过 `bat/` 目录中的批处理脚本配置和编译；Agent 不应直接调用 `cmake -B` 或 `cmake --build` 绕过这些脚本。
- 已存在 `out\build\x64-Debug` 配置时，使用：

  ```bat
  cmd /c .\bat\cmake-debug-build.bat
  ```

- 首次配置、构建目录不存在或 CMake 配置需要重新生成时，使用：

  ```bat
  cmd /c .\bat\cmake-debug-config-and-build.bat
  ```

- 两个脚本固定使用 Visual Studio 2022 Community 随附的 CMake，生成器为 `Visual Studio 17 2022`、平台为 `x64`、配置为 `Debug`，构建目录为 `out\build\x64-Debug`。
- `cmake-debug-build.bat` 会编译整个解决方案；修改 `src/08-ssr` 后，应确认日志中生成了 `out\build\x64-Debug\src\08-ssr\Debug\08_ssr.exe`。
- 若 Codex/PowerShell 会话同时注入大小写不同的 `PATH` 与 `Path`，MSBuild 可能报告环境变量键重复。此时仍须调用同一批处理工具链，只清理传给子进程的重复项：

  ```bat
  cmd /d /c "set PATH=& call .\bat\cmake-debug-build.bat"
  ```

  该写法是本仓库当前 Codex 环境的兼容入口，不是绕过 `bat/` 工具链。
- `bat\test-run.bat` 当前只启动 `11_blue_noise_sm.exe`，不是 `08_ssr` 的运行脚本；除非任务针对该示例，否则不要用它验证 SSR。
- 不要擅自修改或绕过 `bat/` 工具链。若脚本自身失效，先报告具体错误，再决定是否修改脚本。
