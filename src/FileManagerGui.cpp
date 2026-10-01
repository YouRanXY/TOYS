#include "FileWorkflow.h"
#include "FileManagerGuiIds.h"
#include "GuiTheme.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

namespace
{
    constexpr UINT operationFinished = WM_APP + 1;
    constexpr int detailButton = 152;
    enum class Page { Organize, Query, Help };
    enum class Operation { Preview, Apply, Index, Query };

    struct DisplayRow
    {
        std::array<std::wstring, 5> cells;
        std::wstring detail;
        int state = 0;
    };

    struct Completion
    {
        Operation operation = Operation::Preview;
        std::optional<OrganizationPlan> plan;
        std::vector<FileRecord> records;
        std::vector<MoveResult> moves;
        std::string error;
        bool indexSaved = false;
    };

    std::wstring windowText(HWND window)
    {
        std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
        GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
        value.resize(wcslen(value.c_str()));
        return value;
    }

    class Application
    {
    public:
        HWND window = nullptr;

        Application(HINSTANCE instance, fs::path folder, fs::path database)
            : instance(instance), initialFolder(std::move(folder)), initialDatabase(std::move(database))
        {
            whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        }

        ~Application()
        {
            if (worker.joinable()) worker.join();
            DeleteObject(whiteBrush);
        }

        static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM first, LPARAM second)
        {
            auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);
                app->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            }
            if (!app) return DefWindowProcW(window, message, first, second);
            try
            {
                return app->handle(message, first, second);
            }
            catch (const std::exception& error)
            {
                app->busy = false;
                app->plan.reset();
                app->status(L"操作未完成", GuiTheme::wide(error.what()), true);
                app->updateControls();
                return 0;
            }
        }

    private:
        HINSTANCE instance;
        fs::path initialFolder;
        fs::path initialDatabase;
        GuiTheme::Fonts fonts;
        UINT displayDpi = 96;
        HBRUSH whiteBrush = nullptr;
        std::map<int, HWND> controls;
        std::thread worker;
        std::optional<OrganizationPlan> plan;
        std::vector<DisplayRow> rows;
        std::array<std::size_t, 4> totals {};
        Page page = Page::Organize;
        bool initializing = true;
        bool busy = false;
        bool closeWhenDone = false;
        bool statusError = false;
        std::wstring detail;
        std::wstring tableTitle = L"文件预览";

        int s(int value) const { return fonts.scale(value); }
        HWND control(int id) const
        {
            const auto found = controls.find(id);
            return found == controls.end() ? nullptr : found->second;
        }

        HWND add(int id, const wchar_t* type, const wchar_t* text, DWORD style, DWORD extended = 0)
        {
            const auto handle = CreateWindowExW(extended, type, text,
                WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            if (!handle) throw std::runtime_error("Cannot create a window control");
            controls[id] = handle;
            SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.body), FALSE);
            return handle;
        }

        void button(int id, const wchar_t* title)
        {
            add(id, L"BUTTON", title, WS_TABSTOP | BS_OWNERDRAW);
        }

        void edit(int id, const wchar_t* hint)
        {
            const auto handle = add(id, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
            SendMessageW(handle, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(s(8), s(8)));
            SendMessageW(handle, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(hint));
        }

        void createControls()
        {
            displayDpi = GetDpiForWindow(window);
            fonts.reset(displayDpi);
            button(GuiId::organizePage, L"整理文件");
            button(GuiId::queryPage, L"查询记录");
            button(GuiId::helpPage, L"使用帮助");
            edit(GuiId::folder, L"选择文件夹，或粘贴完整路径");
            edit(GuiId::database, L"文件记录保存位置");
            button(GuiId::browseFolder, L"选择文件夹");
            button(GuiId::browseDatabase, L"更改位置");
            button(GuiId::openFolder, L"打开文件夹");
            button(GuiId::preview, L"预览整理");
            button(GuiId::index, L"只保存记录");
            button(GuiId::apply, L"按预览整理");
            add(GuiId::category, WC_COMBOBOXW, L"",
                WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
            for (const auto name : { L"全部类别", L"图片", L"代码", L"文本", L"其他" })
            {
                SendMessageW(control(GuiId::category), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            }
            SendMessageW(control(GuiId::category), CB_SETCURSEL, 0, 0);
            edit(GuiId::extension, L"扩展名，如 txt");
            edit(GuiId::name, L"搜索文件名");
            button(GuiId::query, L"查询记录");
            const auto list = add(GuiId::files, WC_LISTVIEWW, L"",
                WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            ListView_SetBkColor(list, RGB(255, 255, 255));
            ListView_SetTextBkColor(list, RGB(255, 255, 255));
            ListView_SetTextColor(list, GuiTheme::text);
            SetWindowTheme(list, L"Explorer", nullptr);
            const wchar_t* titles[] = { L"文件名", L"类别", L"大小", L"处理状态", L"目标 / 当前路径" };
            for (int index = 0; index < 5; ++index)
            {
                LVCOLUMNW column {};
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.pszText = const_cast<wchar_t*>(titles[index]);
                column.cx = s(120);
                column.iSubItem = index;
                ListView_InsertColumn(list, index, &column);
            }
            add(GuiId::statusTitle, L"STATIC", L"", SS_LEFT);
            add(GuiId::statusDetail, L"STATIC", L"", SS_LEFT);
            button(detailButton, L"详情");
            SetWindowTextW(control(GuiId::folder), initialFolder.c_str());
            SetWindowTextW(control(GuiId::database), initialDatabase.c_str());
            initializing = false;
            status(L"从选择文件夹开始", L"先预览，再整理。同名文件会跳过，其他类型保留原位。");
            updateControls();
            layout();
        }

        void status(const std::wstring& title, const std::wstring& message, bool error = false)
        {
            statusError = error;
            detail = message;
            SetWindowTextW(control(GuiId::statusTitle), title.c_str());
            SetWindowTextW(control(GuiId::statusDetail), message.c_str());
            InvalidateRect(window, nullptr, FALSE);
        }

        void invalidatePreview()
        {
            plan.reset();
            rows.clear();
            totals.fill(0);
            ListView_SetItemCount(control(GuiId::files), 0);
            status(L"路径已更新", L"请重新预览；尚未移动任何文件。");
            updateControls();
        }

        void updateControls()
        {
            for (const auto& item : controls)
            {
                EnableWindow(item.second, !busy);
            }
            const bool canApply = plan && std::any_of(plan->moves.begin(), plan->moves.end(),
                [](const MoveResult& move) { return move.status == MoveStatus::Planned; });
            EnableWindow(control(GuiId::apply), !busy && canApply);
            EnableWindow(control(GuiId::openFolder), !busy && !windowText(control(GuiId::folder)).empty());
            ShowWindow(control(detailButton), page != Page::Help && statusError && !busy ? SW_SHOW : SW_HIDE);
            InvalidateRect(window, nullptr, FALSE);
        }

        void switchPage(Page selected)
        {
            if (busy) return;
            page = selected;
            plan.reset();
            rows.clear();
            totals.fill(0);
            ListView_SetItemCount(control(GuiId::files), 0);
            if (page == Page::Query)
                status(L"查看已保存的文件", L"选择记录文件，设置筛选条件，然后点击查询。");
            else
                status(L"从选择文件夹开始", L"预览不会移动文件，也不会创建记录文件。");
            updateControls();
            layout();
        }

        void position(int id, int x, int y, int width, int height, bool visible = true)
        {
            MoveWindow(control(id), x, y, width, height, TRUE);
            ShowWindow(control(id), visible ? SW_SHOW : SW_HIDE);
        }

        void layout()
        {
            if (controls.empty()) return;
            RECT client {};
            GetClientRect(window, &client);
            if (client.right <= 0 || client.bottom <= 0) return;
            // Fit the complete layout when a scaled window cannot fit the monitor.
            const auto fittingDpi = static_cast<UINT>(std::max(48, std::min(
                MulDiv(client.right, 96, 990), MulDiv(client.bottom, 96, 660))));
            const auto layoutDpi = std::min(displayDpi, fittingDpi);
            if (fonts.dpi != layoutDpi)
            {
                fonts.reset(layoutDpi);
                for (const auto& item : controls)
                    SendMessageW(item.second, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.body), FALSE);
                for (const auto id : { GuiId::folder, GuiId::database, GuiId::extension, GuiId::name })
                    SendMessageW(control(id), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                        MAKELPARAM(s(8), s(8)));
            }
            const int left = s(232), right = client.right - s(28);
            const int width = right - left;
            const bool normal = page != Page::Help;
            const bool organize = page == Page::Organize;
            const bool query = page == Page::Query;
            position(GuiId::organizePage, s(16), s(158), s(170), s(46));
            position(GuiId::queryPage, s(16), s(214), s(170), s(46));
            position(GuiId::helpPage, s(16), s(270), s(170), s(46));
            position(GuiId::openFolder, right - s(116), s(42), s(116), s(34), normal);
            position(GuiId::folder, left + s(20), s(153), width - s(166), s(34), normal);
            position(GuiId::browseFolder, right - s(126), s(153), s(106), s(34), normal);
            position(GuiId::database, left + s(20), s(220), width - s(166), s(32), normal);
            position(GuiId::browseDatabase, right - s(126), s(220), s(106), s(32), normal);
            position(GuiId::index, right - s(344), s(380), s(108), s(34), organize);
            position(GuiId::preview, right - s(226), s(380), s(108), s(34), organize);
            position(GuiId::apply, right - s(108), s(380), s(108), s(34), organize);
            position(GuiId::category, left, s(380), s(132), s(230), query);
            position(GuiId::extension, left + s(142), s(380), s(130), s(34), query);
            position(GuiId::name, left + s(282), s(380), width - s(400), s(34), query);
            position(GuiId::query, right - s(108), s(380), s(108), s(34), query);
            position(GuiId::files, left, s(428), width, client.bottom - s(534), normal);
            position(GuiId::statusTitle, left + s(18), client.bottom - s(82), width - s(108), s(23), normal);
            position(GuiId::statusDetail, left + s(18), client.bottom - s(55), width - s(108), s(32), normal);
            position(detailButton, right - s(86), client.bottom - s(71), s(68), s(32),
                normal && statusError && !busy);
            SendMessageW(control(GuiId::statusTitle), WM_SETFONT, reinterpret_cast<WPARAM>(fonts.strong), FALSE);
            const int fixed = s(190 + 65 + 85 + 134);
            const int widths[] = { s(190), s(65), s(85), s(134), std::max(s(150), width - fixed - s(20)) };
            for (int index = 0; index < 5; ++index)
                ListView_SetColumnWidth(control(GuiId::files), index, widths[index]);
            InvalidateRect(window, nullptr, TRUE);
        }

        RECT rectangle(int left, int top, int right, int bottom) const
        {
            return { s(left), s(top), s(right), s(bottom) };
        }

        void paint(HDC context)
        {
            RECT client {};
            GetClientRect(window, &client);
            GuiTheme::fill(context, client, GuiTheme::background);
            RECT side { 0, 0, s(204), client.bottom };
            GuiTheme::fill(context, side, GuiTheme::sidebar);
            GuiTheme::card(context, rectangle(22, 30, 60, 68), GuiTheme::accent, GuiTheme::accent, s(12));
            GuiTheme::label(context, L"F", rectangle(22, 29, 60, 69), fonts.brand,
                RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            GuiTheme::label(context, L"FileManager", rectangle(22, 84, 192, 114),
                fonts.brand, RGB(255, 255, 255));
            GuiTheme::label(context, L"本地文件 · 有序管理", rectangle(22, 116, 192, 140),
                fonts.caption, RGB(159, 177, 203));
            RECT footer { s(22), client.bottom - s(98), s(184), client.bottom - s(28) };
            GuiTheme::label(context, L"先预览，再整理\n同名文件始终保留\n仅处理文件夹第一层", footer,
                fonts.caption, RGB(159, 177, 203), DT_LEFT | DT_TOP | DT_WORDBREAK);
            const int left = s(232), right = client.right - s(28);
            RECT title { left, s(28), right - s(135), s(71) };
            RECT subtitle { left, s(75), right, s(100) };
            GuiTheme::label(context, page == Page::Organize ? L"让文件各归其位"
                : page == Page::Query ? L"找到已保存的文件" : L"三步，完成文件整理",
                title, fonts.title, GuiTheme::text);
            GuiTheme::label(context, page == Page::Organize ? L"选择文件夹，查看整理计划，再决定是否执行。"
                : page == Page::Query ? L"按类别、扩展名和文件名，检索已保存的记录。"
                : L"从一个装有文件副本的练习文件夹开始。", subtitle, fonts.body, GuiTheme::muted);
            if (page == Page::Help)
            {
                const wchar_t* headings[] = { L"01   选择文件夹", L"02   预览整理", L"03   按预览整理" };
                const wchar_t* descriptions[] = {
                    L"点击“选择文件夹”，或直接粘贴文件夹的完整路径。",
                    L"查看文件类别、大小与目标位置。此步骤不会改动文件。",
                    L"点击“按预览整理”。图片、代码、文本分别放入对应子文件夹。" };
                for (int index = 0; index < 3; ++index)
                {
                    RECT card { left, s(126 + 100 * index), right, s(214 + 100 * index) };
                    GuiTheme::card(context, card, RGB(255, 255, 255), GuiTheme::border, s(14));
                    RECT heading { left + s(22), card.top + s(15), right - s(22), card.top + s(42) };
                    RECT description { left + s(22), card.top + s(50), right - s(22), card.bottom - s(12) };
                    GuiTheme::label(context, headings[index], heading, fonts.strong, GuiTheme::accent);
                    GuiTheme::label(context, descriptions[index], description, fonts.body, GuiTheme::text);
                }
                RECT help { left, s(438), right, client.bottom - s(24) };
                GuiTheme::label(context,
                    L"更多功能\n“只保存记录”：扫描文件并保存信息，不移动文件。\n"
                    L"“查询记录”：查询最近保存的批次，不实时扫描磁盘。\n"
                    L"整理规则\n未知类型保留原位；同名目标会跳过。\n"
                    L"分类子文件夹不会递归扫描，可单独选择它们来保存记录。\n"
                    L"整理过程中，请避免用其他程序修改这些文件。",
                    help, fonts.body, GuiTheme::muted, DT_LEFT | DT_TOP | DT_WORDBREAK);
                return;
            }
            RECT selector { left, s(115), right, s(270) };
            GuiTheme::card(context, selector, RGB(255, 255, 255), GuiTheme::border, s(14));
            RECT folderLabel { left + s(20), s(125), right - s(20), s(148) };
            RECT dbLabel { left + s(20), s(194), right - s(20), s(217) };
            GuiTheme::label(context, L"整理文件夹", folderLabel, fonts.strong, GuiTheme::text);
            GuiTheme::label(context, L"记录文件保存位置", dbLabel, fonts.caption, GuiTheme::muted);
            const wchar_t* labels[] = { L"文件总数", L"图片", L"代码", L"文本" };
            const COLORREF colors[] = { GuiTheme::text, GuiTheme::accent, GuiTheme::green, GuiTheme::orange };
            const int gap = s(12), width = (right - left - 3 * gap) / 4;
            for (int index = 0; index < 4; ++index)
            {
                RECT card { left + index * (width + gap), s(285), left + index * (width + gap) + width, s(360) };
                GuiTheme::card(context, card, RGB(255, 255, 255), GuiTheme::border, s(12));
                RECT caption { card.left + s(16), card.top + s(8), card.right - s(16), card.top + s(28) };
                RECT number { card.left + s(16), card.top + s(30), card.right - s(16), card.bottom - s(6) };
                GuiTheme::label(context, labels[index], caption, fonts.caption, GuiTheme::muted);
                GuiTheme::label(context, std::to_wstring(totals[index]), number, fonts.number, colors[index]);
            }
            if (page == Page::Organize)
            {
                RECT tableHeading { left, s(380), right - s(365), s(414) };
                GuiTheme::label(context, tableTitle, tableHeading, fonts.strong, GuiTheme::text);
            }
            RECT statusCard { left, client.bottom - s(96), right, client.bottom - s(12) };
            GuiTheme::card(context, statusCard, RGB(255, 255, 255), GuiTheme::border, s(12));
        }

        void drawButton(const DRAWITEMSTRUCT& item)
        {
            const bool navigation = item.CtlID >= GuiId::organizePage && item.CtlID <= GuiId::helpPage;
            const bool selected = (item.CtlID == GuiId::organizePage && page == Page::Organize)
                || (item.CtlID == GuiId::queryPage && page == Page::Query)
                || (item.CtlID == GuiId::helpPage && page == Page::Help);
            const bool disabled = (item.itemState & ODS_DISABLED) != 0;
            const bool pressed = (item.itemState & ODS_SELECTED) != 0;
            const bool primary = item.CtlID == GuiId::apply || item.CtlID == GuiId::query;
            COLORREF background = navigation ? (selected ? GuiTheme::sidebarSelected : GuiTheme::sidebar)
                : disabled ? RGB(233, 237, 244) : primary ? GuiTheme::accent : RGB(255, 255, 255);
            if (pressed && !disabled && !navigation) background = RGB(220, 229, 246);
            const COLORREF foreground = disabled ? GuiTheme::muted : navigation || primary
                ? RGB(255, 255, 255) : GuiTheme::text;
            GuiTheme::fill(item.hDC, item.rcItem, navigation ? GuiTheme::sidebar : GuiTheme::background);
            GuiTheme::card(item.hDC, item.rcItem, background,
                navigation || primary ? background : GuiTheme::border, s(10));
            auto rectangle = item.rcItem;
            if (navigation)
            {
                rectangle.left += s(20);
                if (selected)
                {
                    RECT marker { item.rcItem.left + s(8), item.rcItem.top + s(13),
                        item.rcItem.left + s(11), item.rcItem.bottom - s(13) };
                    GuiTheme::fill(item.hDC, marker, RGB(115, 165, 255));
                }
            }
            GuiTheme::label(item.hDC, windowText(item.hwndItem), rectangle, fonts.strong,
                foreground, (navigation ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE);
            if ((item.itemState & ODS_FOCUS) != 0)
            {
                auto focus = item.rcItem;
                InflateRect(&focus, -s(4), -s(4));
                DrawFocusRect(item.hDC, &focus);
            }
        }

        void browseFolder()
        {
            IFileOpenDialog* dialog = nullptr;
            if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog))))
                throw std::runtime_error("Cannot open folder picker");
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            dialog->SetTitle(L"选择要整理的文件夹");
            if (SUCCEEDED(dialog->Show(window)))
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)))
                {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                    {
                        SetWindowTextW(control(GuiId::folder), path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }

        void browseDatabase()
        {
            std::array<wchar_t, 32768> path {};
            const auto current = windowText(control(GuiId::database));
            std::copy_n(current.data(), std::min(current.size(), path.size() - 1), path.data());
            OPENFILENAMEW dialog {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = window;
            dialog.lpstrFile = path.data();
            dialog.nMaxFile = static_cast<DWORD>(path.size());
            dialog.lpstrFilter = L"文件记录 (*.db)\0*.db\0所有文件\0*.*\0\0";
            dialog.lpstrDefExt = L"db";
            dialog.lpstrTitle = L"选择或新建文件记录";
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&dialog))
                SetWindowTextW(control(GuiId::database), path.data());
        }

        void start(Operation operation)
        {
            if (busy || (operation == Operation::Apply && !plan)) return;
            const fs::path root = windowText(control(GuiId::folder));
            const fs::path database = windowText(control(GuiId::database));
            if (database.empty() || (operation != Operation::Query && root.empty()))
            {
                status(L"请先选择路径", L"选择整理文件夹和记录文件保存位置后再操作。", true);
                updateControls();
                return;
            }
            QueryFilter filter;
            const auto selection = SendMessageW(control(GuiId::category), CB_GETCURSEL, 0, 0);
            const char* categories[] = { "", "Images", "Code", "Text", "Other" };
            if (selection >= 0 && selection < 5) filter.category = categories[selection];
            filter.extension = GuiTheme::utf8(windowText(control(GuiId::extension)));
            filter.name = GuiTheme::utf8(windowText(control(GuiId::name)));
            const auto pendingPlan = plan;
            plan.reset();
            busy = true;
            status(L"正在处理，请稍候", operation == Operation::Apply
                ? L"正在按预览整理文件；完成后将显示每个文件的处理结果。"
                : L"正在读取文件与记录，完成后会更新列表。");
            updateControls();
            const auto destination = window;
            worker = std::thread([destination, root, database, operation, filter, pendingPlan]
            {
                auto result = std::make_unique<Completion>();
                result->operation = operation;
                try
                {
                    if (operation == Operation::Preview)
                    {
                        result->plan = FileWorkflow::preview(root, database);
                        result->records = result->plan->records;
                        result->moves = result->plan->moves;
                    }
                    else if (operation == Operation::Apply)
                    {
                        auto organized = FileWorkflow::apply(*pendingPlan);
                        result->records = std::move(organized.records);
                        result->moves = std::move(organized.moves);
                        result->error = std::move(organized.error);
                        result->indexSaved = organized.indexSaved;
                    }
                    else if (operation == Operation::Index)
                    {
                        result->records = FileWorkflow::index(root, database);
                        result->indexSaved = true;
                    }
                    else
                        result->records = FileWorkflow::query(database, filter);
                }
                catch (const std::exception& error)
                {
                    result->error = error.what();
                }
                catch (...)
                {
                    result->error = "Unexpected operation failure";
                }
                if (PostMessageW(destination, operationFinished, 0, reinterpret_cast<LPARAM>(result.get())))
                    result.release();
            });
        }

        void populate(const Completion& result)
        {
            rows.clear();
            totals.fill(0);
            std::map<fs::path, const MoveResult*> moves;
            for (const auto& move : result.moves)
            {
                moves[move.source] = &move;
                if (move.status == MoveStatus::Moved) moves[move.destination] = &move;
            }
            for (const auto& record : result.records)
            {
                DisplayRow row;
                row.cells = { GuiTheme::wide(record.name), GuiTheme::category(record.category),
                    GuiTheme::size(record.sizeBytes), L"已保存记录", record.path.wstring() };
                row.state = 1;
                const auto found = moves.find(record.path);
                if (found != moves.end())
                {
                    const auto& move = *found->second;
                    row.cells[4] = move.destination.wstring();
                    row.detail = GuiTheme::wide(move.message);
                    switch (move.status)
                    {
                        case MoveStatus::Planned: row.cells[3] = L"将移动"; row.state = 0; break;
                        case MoveStatus::Moved: row.cells[3] = L"已移动"; row.state = 1; break;
                        case MoveStatus::Skipped: row.cells[3] = L"同名目标，跳过"; row.state = 2; break;
                        case MoveStatus::Failed: row.cells[3] = L"移动失败"; row.state = 3; break;
                    }
                }
                else if (result.operation == Operation::Preview || result.operation == Operation::Apply)
                {
                    row.cells[3] = L"保留原位";
                    row.state = 4;
                    row.detail = L"未识别的文件类型，保留原位。";
                }
                ++totals[0];
                if (record.category == "Images") ++totals[1];
                if (record.category == "Code") ++totals[2];
                if (record.category == "Text") ++totals[3];
                rows.push_back(std::move(row));
            }
            ListView_SetItemCountEx(control(GuiId::files), static_cast<int>(rows.size()), LVSICF_NOSCROLL);
            InvalidateRect(control(GuiId::files), nullptr, TRUE);
        }

        void finish(std::unique_ptr<Completion> result)
        {
            if (worker.joinable()) worker.join();
            busy = false;
            populate(*result);
            plan = std::move(result->plan);
            if (!result->error.empty())
            {
                status(result->moves.empty() ? L"操作未完成" : L"文件已处理，记录未更新",
                    result->moves.empty() ? L"请检查路径与文件状态，必要时重新预览。 " + GuiTheme::wide(result->error)
                        : L"部分文件可能已移动。请单独选择分类文件夹来保存记录。 " + GuiTheme::wide(result->error),
                    true);
            }
            else if (result->operation == Operation::Preview)
            {
                tableTitle = L"文件预览";
                const auto planned = std::count_if(result->moves.begin(), result->moves.end(),
                    [](const MoveResult& move) { return move.status == MoveStatus::Planned; });
                status(L"预览完成 · 尚未移动文件", L"共 " + std::to_wstring(rows.size()) + L" 个文件，"
                    + std::to_wstring(planned) + L" 个可整理。双击一行可查看处理说明。");
            }
            else if (result->operation == Operation::Apply)
            {
                tableTitle = L"整理结果";
                std::size_t moved = 0, skipped = 0, failed = 0;
                for (const auto& move : result->moves)
                {
                    if (move.status == MoveStatus::Moved) ++moved;
                    if (move.status == MoveStatus::Skipped) ++skipped;
                    if (move.status == MoveStatus::Failed) ++failed;
                }
                status(failed ? L"整理结束 · 部分文件未移动" : L"整理完成",
                    L"已移动 " + std::to_wstring(moved) + L" 个，跳过 " + std::to_wstring(skipped)
                        + L" 个，失败 " + std::to_wstring(failed) + L" 个。文件记录已更新。",
                    failed != 0);
            }
            else
            {
                tableTitle = result->operation == Operation::Query ? L"查询结果" : L"已保存记录";
                status(result->operation == Operation::Query ? L"查询完成" : L"记录已保存 · 文件未移动",
                    L"共 " + std::to_wstring(rows.size()) + L" 条记录。查询展示最近保存的批次。");
            }
            updateControls();
            if (closeWhenDone) DestroyWindow(window);
        }

        LRESULT notify(NMHDR* header)
        {
            if (header->idFrom != GuiId::files) return 0;
            if (header->code == LVN_GETDISPINFOW)
            {
                auto info = reinterpret_cast<NMLVDISPINFOW*>(header);
                const auto index = info->item.iItem;
                const auto column = info->item.iSubItem;
                if ((info->item.mask & LVIF_TEXT) && index >= 0
                    && static_cast<std::size_t>(index) < rows.size() && column >= 0 && column < 5)
                    info->item.pszText = rows[index].cells[column].data();
                return 0;
            }
            if (header->code == NM_CUSTOMDRAW)
            {
                auto item = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
                if (item->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (item->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
                if (item->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)
                    && item->nmcd.dwItemSpec < rows.size())
                {
                    const auto& row = rows[item->nmcd.dwItemSpec];
                    item->clrTextBk = item->nmcd.dwItemSpec % 2 == 0 ? RGB(255, 255, 255) : RGB(249, 251, 254);
                    item->clrText = GuiTheme::text;
                    if (item->iSubItem == 3)
                    {
                        const COLORREF colors[] = { GuiTheme::accent, GuiTheme::green,
                            GuiTheme::orange, GuiTheme::red, GuiTheme::muted };
                        item->clrText = colors[row.state];
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (header->code == NM_DBLCLK)
            {
                const int selected = ListView_GetNextItem(control(GuiId::files), -1, LVNI_SELECTED);
                if (selected >= 0 && static_cast<std::size_t>(selected) < rows.size())
                {
                    const auto& row = rows[selected];
                    const auto description = row.cells[0] + L"\n\n" + row.cells[3] + L"\n"
                        + row.cells[4] + L"\n\n" + row.detail;
                    MessageBoxW(window, description.c_str(), L"文件处理说明", MB_OK | MB_ICONINFORMATION);
                }
            }
            return CDRF_DODEFAULT;
        }

        LRESULT handle(UINT message, WPARAM first, LPARAM second)
        {
            switch (message)
            {
                case WM_CREATE: createControls(); return 0;
                case WM_SIZE: layout(); return 0;
                case WM_GETMINMAXINFO:
                {
                    auto info = reinterpret_cast<MINMAXINFO*>(second);
                    MONITORINFO monitor { sizeof(monitor) };
                    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
                    info->ptMinTrackSize = {
                        std::min(990L, monitor.rcWork.right - monitor.rcWork.left),
                        std::min(720L, monitor.rcWork.bottom - monitor.rcWork.top) };
                    return 0;
                }
                case WM_DPICHANGED:
                {
                    displayDpi = HIWORD(first);
                    const auto bounds = reinterpret_cast<RECT*>(second);
                    SetWindowPos(window, nullptr, bounds->left, bounds->top,
                        bounds->right - bounds->left, bounds->bottom - bounds->top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
                    layout();
                    return 0;
                }
                case WM_ERASEBKGND: return 1;
                case WM_PRINTCLIENT: paint(reinterpret_cast<HDC>(first)); return 0;
                case WM_PAINT:
                {
                    PAINTSTRUCT painting {};
                    auto context = BeginPaint(window, &painting);
                    paint(context);
                    EndPaint(window, &painting);
                    return 0;
                }
                case WM_DRAWITEM: drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(second)); return TRUE;
                case WM_CTLCOLOREDIT:
                    SetTextColor(reinterpret_cast<HDC>(first), GuiTheme::text);
                    SetBkColor(reinterpret_cast<HDC>(first), RGB(255, 255, 255));
                    return reinterpret_cast<LRESULT>(whiteBrush);
                case WM_CTLCOLORSTATIC:
                    SetTextColor(reinterpret_cast<HDC>(first),
                        reinterpret_cast<HWND>(second) == control(GuiId::statusTitle)
                            ? (statusError ? GuiTheme::red : GuiTheme::text) : GuiTheme::muted);
                    SetBkMode(reinterpret_cast<HDC>(first), TRANSPARENT);
                    return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
                case WM_NOTIFY: return notify(reinterpret_cast<NMHDR*>(second));
                case operationFinished:
                    finish(std::unique_ptr<Completion>(reinterpret_cast<Completion*>(second)));
                    return 0;
                case WM_COMMAND:
                {
                    const int id = LOWORD(first);
                    if (!initializing && !busy && HIWORD(first) == EN_CHANGE
                        && (id == GuiId::folder || id == GuiId::database))
                    {
                        invalidatePreview();
                        return 0;
                    }
                    if (HIWORD(first) != BN_CLICKED || busy) return 0;
                    switch (id)
                    {
                        case GuiId::organizePage: switchPage(Page::Organize); break;
                        case GuiId::queryPage: switchPage(Page::Query); break;
                        case GuiId::helpPage: switchPage(Page::Help); break;
                        case GuiId::browseFolder: browseFolder(); break;
                        case GuiId::browseDatabase: browseDatabase(); break;
                        case GuiId::preview: start(Operation::Preview); break;
                        case GuiId::apply: start(Operation::Apply); break;
                        case GuiId::index: start(Operation::Index); break;
                        case GuiId::query: start(Operation::Query); break;
                        case detailButton: MessageBoxW(window, detail.c_str(), L"操作详情", MB_OK | MB_ICONINFORMATION); break;
                        case GuiId::openFolder:
                            ShellExecuteW(window, L"open", windowText(control(GuiId::folder)).c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
                            break;
                    }
                    return 0;
                }
                case WM_CLOSE:
                    if (busy)
                    {
                        closeWhenDone = true;
                        status(L"正在等待操作完成", L"本次处理结束后窗口将自动关闭。");
                    }
                    else DestroyWindow(window);
                    return 0;
                case WM_DESTROY: PostQuitMessage(0); return 0;
            }
            return DefWindowProcW(window, message, first, second);
        }
    };
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try
    {
        INITCOMMONCONTROLSEX common { sizeof(common), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&common);
        std::array<wchar_t, 32768> executable {};
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) throw std::runtime_error("Cannot locate executable");
        fs::path folder;
        fs::path database = fs::path(executable.data()).parent_path() / "filemanager.db";
        int count = 0;
        auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) throw std::runtime_error("Cannot read launch arguments");
        for (int index = 1; index < count; ++index)
        {
            const std::wstring option = arguments[index];
            if ((option != L"--folder" && option != L"--db") || index + 1 >= count)
            {
                LocalFree(arguments);
                throw std::runtime_error("Use --folder <directory> and --db <path>");
            }
            if (option == L"--folder") folder = arguments[++index];
            else database = arguments[++index];
        }
        LocalFree(arguments);
        Application application(instance, folder, database);
        WNDCLASSEXW type {};
        type.cbSize = sizeof(type);
        type.style = CS_HREDRAW | CS_VREDRAW;
        type.lpfnWndProc = Application::windowProcedure;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        type.lpszClassName = L"FileManagerWindow";
        RegisterClassExW(&type);
        RECT work {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const auto initialDpi = static_cast<int>(GetDpiForSystem());
        const int width = std::min(MulDiv(1180, initialDpi, 96), static_cast<int>(work.right - work.left) - 40);
        const int height = std::min(MulDiv(850, initialDpi, 96), static_cast<int>(work.bottom - work.top) - 40);
        const auto window = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName,
            L"FileManager · 本地文件整理", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            work.left + (work.right - work.left - width) / 2,
            work.top + (work.bottom - work.top - height) / 2,
            width, height, nullptr, nullptr, instance, &application);
        if (!window) throw std::runtime_error("Cannot create FileManager window");
        ShowWindow(window, show);
        UpdateWindow(window);
        MSG message {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(window, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (SUCCEEDED(com)) CoUninitialize();
        return 0;
    }
    catch (const std::exception& error)
    {
        MessageBoxW(nullptr, GuiTheme::wide(error.what()).c_str(), L"FileManager 无法启动", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(com)) CoUninitialize();
        return 1;
    }
}
