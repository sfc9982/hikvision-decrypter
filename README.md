# Hikvision 配置工具

用于本地配置文件的加密、解密、只读预览和另存为。

## 界面语言 / Language

默认跟随系统，支持简体中文和英文。可在「语言」菜单中选择，立即生效并记住选择。系统文件对话框的原生按钮仍使用系统语言。

The app follows the system language by default and supports Simplified Chinese and English. Use the **Language** menu to switch immediately. The app remembers your choice. Native file-dialog buttons still follow the system language.

## 配置预览

主窗口提供输入文件与转换结果的只读预览，转换完成后自动切换到结果。

- **十六进制**：显示字节偏移、原始字节和 ASCII 列。
- **文本 / UTF-8**：显示 UTF-8 文本，二进制控制字节和无效编码显示为 `.`。
- **可读字符串**：全文件一次扫描，按偏移列出至少 4 个可读字符的片段。表格显示文件偏移、字节长度和内容，可连续滚动，不按页切分。双击一行或按 Enter 跳到对应的十六进制位置。解密完成后默认打开此列表。
- **分页**：十六进制和文本视图每页 4096 字节，支持上一页、下一页和页码跳转。
- **查找**：按 Enter 或点击“查找下一个”，按 UTF-8 字节精确匹配、区分大小写，到末尾后从头继续。字符串视图在列表中选中匹配行；其他视图搜索原始字节并在十六进制中标出起始位置。

预览不修改文件或转换结果，不要求先导出。字符串列表是文本片段索引，不解析配置结构。

## 构建与测试

需要 CMake 3.27+、C++17 编译器和 wxWidgets 3.2.12 或更高版本。Windows 可使用 Visual Studio 2022 的 C++ 工具集和对应架构的 wxWidgets SDK；将头文件、开发库和运行时 DLL 解压到同一目录。

```powershell
cmake -S . -B build/windows -A x64 -DwxWidgets_ROOT_DIR="C:/deps/wxWidgets-3.2.12" -DHIKVISION_GUI_SMOKE_TESTS=ON
cmake --build build/windows --config Release
ctest --test-dir build/windows -C Release --output-on-failure
```

也可以将 `WXWIN` 指向 SDK 目录后用 Visual Studio 打开根目录的解决方案；建议优先使用 CMake 构建。

GUI 测试使用合成文件，需要桌面环境，默认关闭。不构建 GUI 时可指定 `-DHIKVISION_BUILD_GUI=OFF`，无需安装 wxWidgets。

本项目沿用 WormChickenWizard 的 Hikvision Decrypter 和 Sergey Bel 的 AES 实现。许可见 `LICENSE`。
