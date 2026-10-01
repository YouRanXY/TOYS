#include "GuiTheme.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace GuiTheme
{
    Fonts::~Fonts()
    {
        clear();
    }

    void Fonts::clear()
    {
        for (const auto font : { body, caption, strong, title, brand, number })
        {
            if (font)
            {
                DeleteObject(font);
            }
        }
        body = caption = strong = title = brand = number = nullptr;
    }

    HFONT Fonts::create(int height, int weight) const
    {
        return CreateFontW(-scale(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    }

    void Fonts::reset(UINT value)
    {
        clear();
        dpi = value;
        body = create(14, FW_NORMAL);
        caption = create(12, FW_NORMAL);
        strong = create(14, FW_SEMIBOLD);
        title = create(27, FW_SEMIBOLD);
        brand = create(20, FW_SEMIBOLD);
        number = create(25, FW_SEMIBOLD);
    }

    int Fonts::scale(int value) const
    {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    void fill(HDC context, const RECT& rectangle, COLORREF color)
    {
        auto brush = CreateSolidBrush(color);
        FillRect(context, &rectangle, brush);
        DeleteObject(brush);
    }

    void card(HDC context, RECT rectangle, COLORREF color, COLORREF outline, int radius)
    {
        auto brush = CreateSolidBrush(color);
        auto pen = CreatePen(PS_SOLID, 1, outline);
        auto previousBrush = SelectObject(context, brush);
        auto previousPen = SelectObject(context, pen);
        RoundRect(context, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, radius, radius);
        SelectObject(context, previousPen);
        SelectObject(context, previousBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void label(HDC context, const std::wstring& value, RECT rectangle, HFONT font,
        COLORREF color, UINT format)
    {
        const auto previous = SelectObject(context, font);
        SetBkMode(context, TRANSPARENT);
        SetTextColor(context, color);
        DrawTextW(context, value.c_str(), static_cast<int>(value.size()), &rectangle, format);
        SelectObject(context, previous);
    }

    std::wstring wide(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    std::string utf8(const std::wstring& value)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring category(const std::string& value)
    {
        if (value == "Images") return L"图片";
        if (value == "Code") return L"代码";
        if (value == "Text") return L"文本";
        return L"其他";
    }

    std::wstring size(std::int64_t bytes)
    {
        if (bytes < 1024)
        {
            return std::to_wstring(bytes) + L" B";
        }
        const double divisor = bytes < 1024 * 1024 ? 1024.0 : 1024.0 * 1024.0;
        std::wostringstream output;
        output << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / divisor
            << (bytes < 1024 * 1024 ? L" KB" : L" MB");
        return output.str();
    }
}
