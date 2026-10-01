#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include "FileManagerGuiIds.h"
#include "TestSupport.h"
#include <sqlite3.h>

#include <chrono>
#include <thread>
#include <vector>

namespace
{
    struct WindowSearch
    {
        DWORD processId;
        HWND window = nullptr;
    };

    BOOL CALLBACK findWindow(HWND window, LPARAM context)
    {
        auto& search = *reinterpret_cast<WindowSearch*>(context);
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        wchar_t className[128] {};
        GetClassNameW(window, className, 128);
        if (processId == search.processId && std::wstring(className) == L"FileManagerWindow")
        {
            search.window = window;
            return FALSE;
        }
        return TRUE;
    }

    template <typename Predicate>
    void waitUntil(Predicate predicate, const std::string& message)
    {
        const auto deadline = GetTickCount64() + 12000;
        while (!predicate())
        {
            check(GetTickCount64() < deadline, message);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    class GuiProcess
    {
    public:
        PROCESS_INFORMATION process {};
        HWND window = nullptr;

        GuiProcess(const fs::path& executable, const fs::path& root, const fs::path& database)
        {
            auto command = L"\"" + executable.wstring() + L"\" --folder \"" + root.wstring()
                + L"\" --db \"" + database.wstring() + L"\"";
            STARTUPINFOW startup {};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                0, nullptr, root.c_str(), &startup, &process) != FALSE, "Launch GUI test process");
            WaitForInputIdle(process.hProcess, 5000);
            WindowSearch search { process.dwProcessId };
            waitUntil([&]
            {
                EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
                return search.window != nullptr || WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0;
            }, "GUI window creation timed out");
            window = search.window;
            check(window != nullptr, "Native FileManager window was not created");
        }

        ~GuiProcess()
        {
            if (window && IsWindow(window))
            {
                DWORD_PTR ignored = 0;
                SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
            }
            if (process.hProcess)
            {
                if (WaitForSingleObject(process.hProcess, 5000) == WAIT_TIMEOUT)
                {
                    // Only the subprocess created by this test, on disposable fixtures.
                    TerminateProcess(process.hProcess, 1);
                }
                CloseHandle(process.hProcess);
                CloseHandle(process.hThread);
            }
        }

        HWND control(int id) const
        {
            const auto handle = GetDlgItem(window, id);
            check(handle != nullptr, "Expected GUI control is missing");
            return handle;
        }

        std::wstring text(int id) const
        {
            const auto handle = control(id);
            const auto length = SendMessageW(handle, WM_GETTEXTLENGTH, 0, 0);
            std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
            SendMessageW(handle, WM_GETTEXT, buffer.size(), reinterpret_cast<LPARAM>(buffer.data()));
            return buffer.data();
        }

        void setText(int id, const std::wstring& value)
        {
            check(SendMessageW(control(id), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value.c_str())) != 0,
                "Update GUI edit control");
        }

        void click(int id)
        {
            check(IsWindowEnabled(control(id)) != FALSE, "Action should be enabled");
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                reinterpret_cast<LPARAM>(control(id)));
        }

        void idle()
        {
            waitUntil([&] { return IsWindowEnabled(control(GuiId::preview)) != FALSE; },
                "Background GUI action timed out");
            // A control can become enabled partway through the completion handler.
            // Synchronize with the UI thread before inspecting other control states.
            DWORD_PTR ignored = 0;
            check(SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG,
                5000, &ignored) != 0, "GUI completion handler did not finish");
        }

        int count() const
        {
            return static_cast<int>(SendMessageW(control(GuiId::files), LVM_GETITEMCOUNT, 0, 0));
        }
    };

    void capture(HWND window, const fs::path& path)
    {
        // Native themed controls need a visible window to render a complete image.
        // Show without activation, capture only our client, then return to hidden testing.
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        RECT bounds {};
        GetClientRect(window, &bounds);
        HDC source = GetDC(window);
        HDC buffer = CreateCompatibleDC(source);
        HBITMAP bitmap = CreateCompatibleBitmap(source, bounds.right, bounds.bottom);
        auto previous = SelectObject(buffer, bitmap);
        const bool printed = PrintWindow(window, buffer, PW_CLIENTONLY) != FALSE
            && GetPixel(buffer, 10, 10) == RGB(22, 32, 52);
        ShowWindow(window, SW_HIDE);
        SelectObject(buffer, previous);
        DeleteDC(buffer);
        ReleaseDC(window, source);
        check(printed, "Render GUI preview image");
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        check(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok, "Start image encoder");
        UINT count = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&count, &size);
        std::vector<unsigned char> bytes(size);
        auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
        Gdiplus::GetImageEncoders(count, size, encoders);
        Gdiplus::Status saved = Gdiplus::GenericError;
        {
            Gdiplus::Bitmap image(bitmap, nullptr);
            for (UINT index = 0; index < count; ++index)
            {
                if (std::wstring(encoders[index].MimeType) == L"image/png")
                {
                    saved = image.Save(path.c_str(), &encoders[index].Clsid, nullptr);
                    break;
                }
            }
        }
        Gdiplus::GdiplusShutdown(token);
        DeleteObject(bitmap);
        check(saved == Gdiplus::Ok, "Save GUI preview image");
    }
}

int wmain(int argc, wchar_t* argv[])
{
    // Capture physical pixels, matching the GUI's per-monitor DPI manifest.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc != 3)
    {
        return 1;
    }
    return runTest([&]
    {
        TestDirectory fixture("gui");
        fixture.write(fs::u8path(u8"笔记.TXT"), "text");
        fixture.write("main.cpp", "code");
        fixture.write("unknown.bin", "unknown");
        fixture.write("nested/ignored.txt");
        const auto database = fixture.path / fs::u8path(u8"文件索引.db");
        GuiProcess app(fs::absolute(argv[1]), fixture.path, database);
        check(!IsWindowEnabled(app.control(GuiId::apply)), "Apply starts disabled");
        check(!fs::exists(database), "Opening GUI creates no index");
        RECT windowBounds {};
        GetWindowRect(app.window, &windowBounds);
        const UINT originalDpi = GetDpiForWindow(app.window);
        SendMessageW(app.window, WM_DPICHANGED, MAKEWPARAM(192, 192),
            reinterpret_cast<LPARAM>(&windowBounds));
        MONITORINFO monitor { sizeof(monitor) };
        check(GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST), &monitor),
            "Read GUI monitor work area");
        RECT scaledBounds {};
        GetWindowRect(app.window, &scaledBounds);
        check(scaledBounds.right - scaledBounds.left <= monitor.rcWork.right - monitor.rcWork.left
            && scaledBounds.bottom - scaledBounds.top <= monitor.rcWork.bottom - monitor.rcWork.top,
            "High DPI minimum window size fits the available work area");
        RECT clientBounds {}, listBounds {};
        GetClientRect(app.window, &clientBounds);
        GetWindowRect(app.control(GuiId::files), &listBounds);
        MapWindowPoints(nullptr, app.window, reinterpret_cast<POINT*>(&listBounds), 2);
        check(listBounds.right > listBounds.left && listBounds.bottom > listBounds.top
            && listBounds.left >= 0 && listBounds.top >= 0
            && listBounds.right <= clientBounds.right && listBounds.bottom <= clientBounds.bottom,
            "High DPI layout keeps a positive results table inside the client");
        SendMessageW(app.window, WM_DPICHANGED, MAKEWPARAM(originalDpi, originalDpi),
            reinterpret_cast<LPARAM>(&windowBounds));
        app.click(GuiId::preview);
        app.idle();
        check(app.count() == 3 && !fs::exists(database), "Preview fills the table without writes");
        check(IsWindowEnabled(app.control(GuiId::apply)), "Preview enables apply");
        capture(app.window, fs::absolute(argv[2]));

        app.setText(GuiId::folder, (fixture.path / "nested").wstring());
        check(!IsWindowEnabled(app.control(GuiId::apply)), "Editing root invalidates the preview");
        app.setText(GuiId::folder, fixture.path.wstring());
        app.click(GuiId::preview);
        app.idle();
        fixture.write("main.cpp", "modified after preview");
        app.click(GuiId::apply);
        app.idle();
        check(fs::exists(fixture.path / "main.cpp") && !fs::exists(fixture.path / "Code"),
            "Stale GUI preview must not move any files");
        check(!IsWindowEnabled(app.control(GuiId::apply)), "Failed apply requires a fresh preview");

        app.click(GuiId::preview);
        app.idle();
        app.click(GuiId::apply);
        app.idle();
        check(fs::exists(fixture.path / fs::u8path(u8"Text/笔记.TXT"))
            && fs::exists(fixture.path / "Code/main.cpp") && fs::exists(fixture.path / "unknown.bin"),
            "GUI apply moves only recognized files");
        check(!IsWindowEnabled(app.control(GuiId::apply)), "Completed batch cannot be applied again");
        app.click(GuiId::queryPage);
        SendMessageW(app.control(GuiId::category), CB_SETCURSEL, 3, 0);
        app.setText(GuiId::extension, L"TXT");
        app.setText(GuiId::name, L"笔记");
        app.click(GuiId::query);
        app.idle();
        check(app.count() == 1, "GUI filters combine category, extension and Unicode filename");
        app.setText(GuiId::database, (fixture.path / "absent.db").wstring());
        app.click(GuiId::query);
        app.idle();
        check(!fs::exists(fixture.path / "absent.db"), "Missing query database must not be created");
        check(app.text(GuiId::statusTitle) == L"操作未完成", "Show query error in the window");
        app.click(GuiId::helpPage);
        check(!IsWindowVisible(app.control(GuiId::files)), "Help page hides the results table");
        app.click(GuiId::organizePage);
        app.setText(GuiId::database, database.wstring());

        sqlite3* lock = nullptr;
        check(sqlite3_open(database.u8string().c_str(), &lock) == SQLITE_OK, "Open controlled database lock");
        check(sqlite3_exec(lock, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_OK, "Lock database");
        app.click(GuiId::index);
        // The UI must still process close while its worker waits for the database.
        SendMessageW(app.window, WM_CLOSE, 0, 0);
        sqlite3_exec(lock, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(lock);
        check(WaitForSingleObject(app.process.hProcess, 10000) == WAIT_OBJECT_0,
            "Closing waits for worker completion and releases the process");
        DWORD exitCode = 1;
        GetExitCodeProcess(app.process.hProcess, &exitCode);
        check(exitCode == 0, "GUI exits cleanly after background work");
    });
}
