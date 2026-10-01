#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

namespace GuiTheme
{
    constexpr COLORREF background = RGB(245, 247, 251);
    constexpr COLORREF sidebar = RGB(22, 32, 52);
    constexpr COLORREF sidebarSelected = RGB(43, 59, 87);
    constexpr COLORREF text = RGB(31, 43, 63);
    constexpr COLORREF muted = RGB(112, 124, 143);
    constexpr COLORREF border = RGB(224, 230, 239);
    constexpr COLORREF accent = RGB(48, 103, 230);
    constexpr COLORREF green = RGB(22, 139, 104);
    constexpr COLORREF orange = RGB(174, 111, 21);
    constexpr COLORREF red = RGB(190, 53, 63);

    class Fonts
    {
    public:
        HFONT body = nullptr;
        HFONT caption = nullptr;
        HFONT strong = nullptr;
        HFONT title = nullptr;
        HFONT brand = nullptr;
        HFONT number = nullptr;
        UINT dpi = 96;

        ~Fonts();
        void reset(UINT value);
        int scale(int value) const;

    private:
        void clear();
        HFONT create(int height, int weight) const;
    };

    void fill(HDC context, const RECT& rectangle, COLORREF color);
    void card(HDC context, RECT rectangle, COLORREF color, COLORREF outline, int radius);
    void label(HDC context, const std::wstring& value, RECT rectangle, HFONT font,
        COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    std::wstring wide(const std::string& value);
    std::string utf8(const std::wstring& value);
    std::wstring category(const std::string& value);
    std::wstring size(std::int64_t bytes);
}
