// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "TmuxPaneContent.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Microsoft::Terminal::Settings::Model;

namespace winrt::TerminalApp::implementation
{
    TmuxPaneContent::TmuxPaneContent()
    {
        _root = winrt::Windows::UI::Xaml::Controls::Grid{};
#if 0
        // Vertical and HorizontalAlignment are Stretch by default

        auto res = Windows::UI::Xaml::Application::Current().Resources();
        auto bg = res.Lookup(winrt::box_value(L"UnfocusedBorderBrush"));
        _root.Background(bg.try_as<Media::Brush>());

        _box = winrt::Windows::UI::Xaml::Controls::TextBox{};
        _box.Margin({ 10, 10, 10, 10 });
        _box.AcceptsReturn(true);
        _box.TextWrapping(TextWrapping::Wrap);
        _root.Children().Append(_box);
#endif
    }

    void TmuxPaneContent::UpdateSettings(const CascadiaSettings& /*settings*/)
    {
        // Nothing to do.
    }

    winrt::Windows::UI::Xaml::FrameworkElement TmuxPaneContent::GetRoot()
    {
        return _root;
    }
    winrt::Windows::Foundation::Size TmuxPaneContent::MinimumSize()
    {
        return { 1, 1 };
    }
    void TmuxPaneContent::Focus(winrt::Windows::UI::Xaml::FocusState /*reason*/)
    {
    }
    void TmuxPaneContent::Close()
    {
    }

    INewContentArgs TmuxPaneContent::GetNewTerminalArgs(const BuildStartupKind /* kind */) const
    {
        return BaseContentArgs(L"tmux");
    }

    winrt::hstring TmuxPaneContent::Icon() const
    {
        static constexpr std::wstring_view glyph{ L"\xe70b" }; // QuickNote
        return winrt::hstring{ glyph };
    }

    winrt::Windows::UI::Xaml::Media::Brush TmuxPaneContent::BackgroundBrush()
    {
        return _root.Background();
    }
}
