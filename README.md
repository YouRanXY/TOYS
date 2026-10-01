# FileManager

个人学习用的 C++17 本地文件管理项目：扫描指定目录、按扩展名整理文件，并使用 SQLite 保存元数据、查询已建立的索引。

## 构建与测试

需要 CMake 3.14 或更新版本及支持 C++17 的编译器。Windows 可使用 Visual Studio 2022 的 C++ 桌面开发工具。

在仓库根目录运行 PowerShell：

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\FileManager.exe --help
```

CMake 优先使用已安装的 SQLite（至少 3.24）。如果未找到，会从 [SQLite 官网](https://www.sqlite.org/amalgamation.html)下载固定的 3.50.4 amalgamation 源码并校验 SHA-256，在构建目录内编译，无需全局安装。首次配置需要网络；后续构建复用下载结果。

离线构建可先下载同一版本的官方压缩包，再指定其绝对路径；版本与校验值必须匹配：

```powershell
cmake -S . -B build "-DFILEMANAGER_SQLITE_URL=D:/downloads/sqlite-amalgamation-3500400.zip"
```

Release 验证：

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 使用方式

以下示例中的 `.\test` 应是用于验证的临时目录，先放入可以移动的样本文件。路径包含空格时请加引号。

### 预览整理

```powershell
.\build\Debug\FileManager.exe organize ".\test"
```

默认只显示移动计划，不移动文件、不创建分类目录、不写数据库。无参数运行和 `--help` 也只显示帮助。

### 执行整理

```powershell
.\build\Debug\FileManager.exe organize ".\test" --apply --db ".\filemanager.db"
```

创建分类目录并移动已识别的文件，同时保存这个批次的文件元数据。输出每个文件的 `MOVED`、`SKIP` 或 `ERROR`，以及汇总数量。已有同名目标时跳过，源文件和目标文件都保留。一个文件移动失败后，继续处理其他文件。

### 只建立索引

```powershell
.\build\Debug\FileManager.exe scan ".\test" --db ".\filemanager.db"
```

扫描并保存元数据，不整理文件。默认数据库为**当前工作目录**下的 `filemanager.db`，也可以用 `--db` 指定其他路径。数据库的父目录必须已存在。

### 查询索引

```powershell
.\build\Debug\FileManager.exe query
.\build\Debug\FileManager.exe query --category Images
.\build\Debug\FileManager.exe query --extension TXT --name "笔记"
.\build\Debug\FileManager.exe query --db ".\filemanager.db" --category Text --extension ".md"
```

多个过滤条件同时生效。类别值为 `Images`、`Code`、`Text`、`Other`，区分大小写；扩展名支持带或不带点，忽略大小写。名称按**区分大小写的字面子串**匹配，单引号、`%` 和 `_` 不作为 SQL 或通配符解释。查询以只读模式打开已有数据库，不自动创建数据库。

返回码：成功为 `0`；参数错误、扫描/数据库失败、移动失败，以及实际执行时遇到重名跳过，为 `1`。预览遇到同名目标会显示 `SKIP`，不会执行移动。

## 分类规则

| 目录 | 扩展名 |
| --- | --- |
| Images | .jpg .jpeg .png .gif .bmp .webp |
| Code | .c .cpp .cc .cxx .h .hpp .py .js .ts .java |
| Text | .txt .md .markdown |

扩展名忽略大小写。未知扩展名及无扩展名文件保留在原处，索引类别为 `Other`。只扫描目录的第一层，跳过子目录、文件链接及 Windows 重解析点。

## 模块结构

```text
src/main.cpp           命令解析与工作流协调
src/FileScanner.cpp    单层扫描，稳定排序
src/FileClassifier.cpp 纯分类及扩展名规范化
src/FileMove.cpp       预览、移动、重名保护及逐文件结果
src/FileDatabase.cpp   SQLite 元数据、事务刷新和参数化查询
inclu/                对应头文件
tests/                CTest 自动测试
test/                 手动验证用的临时文件，Git 忽略
build/                CMake 生成内容，Git 忽略
```

文件元数据包含根目录、当前路径、文件名、规范化扩展名、类别、字节数、修改时间刻度和 UTC 索引时间。修改时间刻度来自 C++17 `filesystem::file_time_type`，用于同一工具链下比较，不是 Unix 时间戳。

## 索引规则与边界

- 每个根目录保存**最近一次成功建立索引的批次**；刷新事务只替换该根目录的记录，保留其他根目录的记录。
- 执行整理后，索引中的路径会更新为该批次文件的实际移动位置。下一次扫描同一根目录时仍只扫描顶层，因此先前已移入分类子目录的文件不会出现在新批次中。可单独 `scan` 分类目录来建立该目录的索引。
- 查询读取快照，不实时检查文件是否仍存在；外部修改后需要重新扫描。
- 活动数据库及其 `-journal`、`-wal`、`-shm` 文件不会被扫描或移动，即使自定义数据库名称的扩展名属于可识别类型。
- 执行整理前会验证数据库并保存原始索引，失败时不移动文件。文件移动和数据库提交无法构成同一个原子事务；移动后的索引刷新失败会明确报告，已完成的移动不会自动撤销。此时索引可能仍含原始路径；需要索引已移动文件时，应单独扫描分类目录。
- 分类路径必须是实际目录；遇到普通文件、符号链接或 Windows 目录联接时拒绝移动。目标文件不会被覆盖。请避免整理过程中由其他程序并发修改源文件或目录。
- Windows 使用宽字符参数和 UTF-8 数据库存储，支持中文路径。本机验证环境为 MSVC；MinGW-w64 配置了 [Unicode 启动选项](https://gcc.gnu.org/onlinedocs/gcc/Cygwin-and-MinGW-Options.html)，但未在本机实测。其他平台使用独占硬链接创建后移除源名称，文件系统不支持硬链接时会报告失败。
- 自动测试只使用构建目录内新建的临时样本。符号链接用例在缺少创建权限时会输出跳过原因；本机已实测 Windows 目录联接保护，普通符号链接用例因权限限制未执行。数据库、构建产物与私人测试文件不应提交到 Git。
