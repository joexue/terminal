// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include <sstream>
#include <iostream>
#include <winrt/base.h>

#include "pch.h"
#include "ScratchpadContent.h"
#include "TmuxControl.h"
#include "TerminalPage.h"
#include "TmuxPaneContent.h"
#include "TabRowControl.h"

// To be deleted
#include <fstream>
static std::wfstream tmuxLog;

static void tmux_log(const std::wstring& str)
{
    tmuxLog << str;
    tmuxLog.flush();
}

static void tmux_log_put(wchar_t ch)
{
    tmuxLog.put(ch);
    if (ch == '\n') {
        tmuxLog.flush();
    }
}

static void tmux_log_open()
{
    tmuxLog.open(L"d:\\tmux.log", std::wfstream::out | std::wfstream::trunc);
}

static void tmux_log_close()
{
    tmuxLog.close();
}

using namespace winrt::Microsoft::Terminal;
using namespace winrt::Microsoft::Terminal::Control;
using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Microsoft::Terminal::TerminalConnection;
using namespace winrt::Windows::System;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Core;

using winrt::Microsoft::Terminal::Settings::Model::SplitDirection;
//using namespace Microsoft::Console::VirtualTerminal;

namespace winrt::TerminalApp::implementation
{
    const std::wregex TmuxControl::REG_BEGIN{ L"^%begin \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_END{ L"^%end \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_ERROR{ L"^%error \\d+ \\d+ \\d+$" };

    const std::wregex TmuxControl::REG_CLIENT_SESSION_CHANGED{ L"^%client-session-changed \\S+ \\$\\d+ \\S+$" };
    const std::wregex TmuxControl::REG_CLIENT_DETACHED{ L"^%client-detached \\S+$" };
    const std::wregex TmuxControl::REG_CONFIG_ERROR{ L"^%config-error \\S+$" };
    const std::wregex TmuxControl::REG_CONTINUE{ L"^%continue %\\d+$" };
    const std::wregex TmuxControl::REG_DETACH{ L"^\033$" };
    const std::wregex TmuxControl::REG_EXIT{ L"^%exit$" };
    const std::wregex TmuxControl::REG_EXTENDED_OUTPUT{ L"^%extended-output %\\d+ \\S+$" };
    const std::wregex TmuxControl::REG_LAYOUT_CHANGED{ L"^%layout-change @(\\d+) ([\\dabcdefABCDEF]{4}),(\\S+)( \\S+)*$" };
    const std::wregex TmuxControl::REG_MESSAGE{ L"^%message \\S+$" };
    const std::wregex TmuxControl::REG_OUTPUT{ L"^%output %(\\d+) (.+)$" };
    const std::wregex TmuxControl::REG_PANE_MODE_CHANGED{ L"^%pane-mode-changed %\\d+$" };
    const std::wregex TmuxControl::REG_PASTE_BUFFER_CHANGED{ L"^%paste-buffer-changed \\S+$" };
    const std::wregex TmuxControl::REG_PASTE_BUFFER_DELETED{ L"^%paste-buffer-deleted \\S+$" };
    const std::wregex TmuxControl::REG_PAUSE{ L"^%pause %\\d+$" };
    const std::wregex TmuxControl::REG_SESSION_CHANGED{ L"^%session-changed \\$(\\d+) \\S+$" };
    const std::wregex TmuxControl::REG_SESSION_RENAMED{ L"^%session-renamed \\S+$" };
    const std::wregex TmuxControl::REG_SESSION_WINDOW_CHANGED{ L"^%session-window-changed @(\\d+) \\d+$" };
    const std::wregex TmuxControl::REG_SESSIONS_CHANGED{ L"^%sessions-changed$" };
    const std::wregex TmuxControl::REG_SUBSCRIPTION_CHANGED{ L"^%subscription-changed \\S+$" };
    const std::wregex TmuxControl::REG_UNLINKED_WINDOW_ADD{ L"^%unlinked-window-add @\\d+$" };
    const std::wregex TmuxControl::REG_UNLINKED_WINDOW_CLOSE{ L"^%unlinked-window-close @(\\d+)$" };
    const std::wregex TmuxControl::REG_UNLINKED_WINDOW_RENAMED{ L"^%unlinked-window-renamed @\\d+$" };
    const std::wregex TmuxControl::REG_WINDOW_ADD{ L"^%window-add @(\\d+)$" };
    const std::wregex TmuxControl::REG_WINDOW_CLOSE{ L"^%window-close @(\\d+)$" };
    const std::wregex TmuxControl::REG_WINDOW_PANE_CHANGED{ L"^%window-pane-changed @\\d+ %\\d+$" };
    const std::wregex TmuxControl::REG_WINDOW_RENAMED{ L"^%window-renamed @\\d+ \\S+$" };

    TmuxControl::TmuxControl(TerminalPage& page, std::shared_ptr<Pane> pane) :
        _page(page)
    {
        _core = pane->GetTerminalControl();
        _profile = pane->GetProfile();

        _core.SetTmuxControlHandlerProducer([this](auto print) {
            print(L"Running the TMUX control mode, press 'q' to detach: ");

            return [this](const auto ch) mutable {
                return _Advance(ch);
            };
        });

        _dispatcherQueue = DispatcherQueue::GetForCurrentThread();
        return;
    }

    void TmuxControl::_AttachSession()
    {
        _state = State::ATTACHING;

        // Calculate our dimension
        auto fontSize = _core.CharacterDimensions();
        auto x = _page.ActualWidth();
        auto y = _page.ActualHeight();
        _width = (int)(x / fontSize.Width);
        _height = (int)(y / fontSize.Height);

        // Change the padding, otherwise the split panes will not match tmux panes size.
        _profile.Padding(L"0, 0, 0, 0");
        _profile.ScrollState(winrt::Microsoft::Terminal::Control::ScrollbarState::Hidden);
        _profile.Icon(L"\uF714");

        // Intercept the control terminal's input, ignore all user input, except 'q' as detach command.
        _detachKeyRevoker = _core.KeyDown({ this, &TmuxControl::_DetachKeyHandler });

        // Hide the system's splitbutton, show tmux control owns
        auto tabRow = _page.TabRow();
        auto tabRowImpl = winrt::get_self<implementation::TabRowControl>(tabRow);
        _newTabButton = tabRowImpl->NewTabButton();
        _newTmuxTabButton = tabRowImpl->NewTmuxTabButton();
        _newTabButtonHandler = _newTmuxTabButton.Click({ this, &TmuxControl::_NewTabButtonHandler });

        _newTmuxTabButton.Background(_newTabButton.Background());
        _newTmuxTabButton.Foreground(_newTabButton.Foreground());

        _newTabButton.Visibility(Visibility::Collapsed);
        _newTmuxTabButton.Visibility(Visibility::Visible);

        tmux_log_open();
    }

    void TmuxControl::_DetachSession()
    {
        _state = INIT;
        _cmdQueue.clear();
        _dcsBuffer.clear();
        _cmdState = READY;

        std::vector<winrt::TerminalApp::TabBase> tabs;
        for (auto& t : _attachedTabs)
        {
            //tabs.push_back(t.second);
            _page._RemoveTab(t.second);
        }
        //_page._RemoveTabs(tabs);
        _attachedPanes.clear();
        _attachedTabs.clear();

        _core.KeyDown(_detachKeyRevoker);
        _newTmuxTabButton.Click(_newTabButtonHandler);

        _newTabButton.Visibility(Visibility::Visible);
        _newTmuxTabButton.Visibility(Visibility::Collapsed);
        _core.RawWriteString(L"\n");

        tmux_log_close();
    }

    void TmuxControl::_DetachKeyHandler(const Windows::Foundation::IInspectable& /*sender*/, const Windows::UI::Xaml::Input::KeyRoutedEventArgs& e)
    {
        if (e.Key() == VirtualKey::Q)
        {
            tmux_log(L"   CMD: detach\n");
            _core.RawWriteString(L"detach\n");
        }
        e.Handled(true);
    }

    void TmuxControl::_NewTabButtonHandler(const Microsoft::UI::Xaml::Controls::SplitButton& /*SplitButton*/, const Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs& /*args*/)
    {
        _NewWindow();
    }

    void TmuxControl::_CharHandler(int paneId , const Control::CharSentEventArgs& args)
    {
        auto ch = args.Character();
        std::wstring keys(1, static_cast<wchar_t>(ch));
        _SendKey(paneId, keys);
    }

    // Poor version keymap
    void TmuxControl::_KeyHandler(int paneId, const Control::KeySentEventArgs& args)
    {
        auto ch = static_cast<VirtualKey>(args.VKey());
        auto keyDown = args.KeyDown();
        std::wstring out = L"";

        if (!keyDown)
        {
            return;
        }

        if (ch == VirtualKey::Up)
        {
            out = L"\033[A";
        }
        else if (ch == VirtualKey::Down)
        {
            out = L"\033[B";
        }
        else if (ch == VirtualKey::Right)
        {
            out = L"\033[C";
        }
        else if (ch == VirtualKey::Left)
        {
            out = L"\033[D";
        }
        else if (ch == VirtualKey::Tab)
        {
            out = L"\t";
        }

        if (out.size() > 0)
        {
            _SendKey(paneId, out);
        }
    }

    void TmuxControl::_TermReadyHandler(int paneId, const std::wstring& text)
    {
        _SendOutput(paneId, text);
    }

    void TmuxControl::_SendOutput(int paneId, const std::wstring& text)
    {
        auto search = _attachedPanes.find(paneId);
        if (search == _attachedPanes.end())
        {
            _outputBacklog.insert_or_assign(paneId, text);
            return;
        }

        auto p = search->second;
        auto c = p->GetTerminalControl();

        if (c.ViewHeight() != 0) {
            std::wstring out = L"";
            _DecodeOutput(text, out);
            c.SendOutput(out);
        }
        else
        {
            std::wstring res(text);
            c.Initialized([this, paneId, res](auto& /*i*/, auto& /*e*/) {
                return _TermReadyHandler(paneId, res);
            });
        }
    }

    std::shared_ptr<Pane> TmuxControl::_NewPane(int paneId)
    {
        auto connection = TerminalConnection::DumyConnection{};

        auto controlSettings = TerminalSettings::CreateWithProfile(_page._settings, _profile, *_page._bindings);
        const auto control = _page._CreateNewControlAndContent(controlSettings, connection);

        auto paneContent{ winrt::make<TerminalPaneContent> (_profile, _page._terminalSettingsCache, control) };
        auto resultPane = std::make_shared<Pane>(paneContent);

        control.CharSent([this, paneId](auto& /*i*/, auto& e) {
            return _CharHandler(paneId, e);
        });
        control.KeySent([this, paneId](auto& /*i*/, auto& e) {
            return _KeyHandler(paneId, e);
        });

        return resultPane;
    }

    void TmuxControl::_NewWindowAndPane(int windowId, int paneId)
    {
        auto rootPane = _NewPane(paneId);
        _attachedPanes.insert({ paneId, rootPane });
        auto tab = _page._CreateNewTabFromPane(rootPane);
        _attachedTabs.insert({windowId, tab});

        // Check if we have output before we are ready
        auto search = _outputBacklog.find(paneId);
        if (search == _outputBacklog.end())
        {
            return;
        }

        auto result = search->second;
        _SendOutput(paneId, result);
    }

    void TmuxControl::_Output(int paneId, const std::wstring& result)
    {
        if (_state != ATTACH_DONE)
        {
            return;
        }

        _SendOutput(paneId, result);
    }

    void TmuxControl::_WindowClose(int windowId)
    {
        auto search = _attachedTabs.find(windowId);
        if (search == _attachedTabs.end())
        {
            return;
        }

        auto t = search->second;
        _attachedTabs.erase(search);

        t.Shutdown();

        _page._RemoveTab(t);
    }

    void TmuxControl::_EventHandle(Event& e)
    {
        switch(e.type)
        {
            case ATTACH:
                _AttachSession();
                break;
            case DETACH:
                _DetachSession();
                break;
            case OUTPUT:
                _Output(e.paneId, e.response);
                break;
            // Commands response
            case RESPONSE:
                _HandleCommand(e.response);
                break;
            case SESSION_CHANGED:
                _SetOption(std::format(L"default-size {}x{}", _width, _height));
                _DiscoverWindows(e.sessionId);
                break;
            case WINDOW_ADD:
                _DiscoverPanes(e.windowId);
                break;
            case WINDOW_CLOSE:
            case UNLINKED_WINDOW_CLOSE:
                _WindowClose(e.windowId);
                break;

            default:
                break;
        }

        // We are done, give the command in queue a chance to run
        _ScheduleCommand();
    }

    void TmuxControl::_Parse(const std::wstring& line)
    {
        tmux_log(L"OUTPUT: " + line + L'\n');
        std::wsmatch matches;

        // tmux generic rules
        if (std::regex_match(line, REG_BEGIN))
        {
            _event.type = BEGIN;
        }
        else if (std::regex_match(line, REG_END) || std::regex_match(line, REG_ERROR))
        {
            if (_state == INIT)
            {
                _event.type = ATTACH;
            }
            else
            {
                _event.type = RESPONSE;
            }
        }
        // tmux specific rules
        else if (std::regex_match(line, REG_DETACH))
        {
            _event.type = DETACH;
        }
        else if (std::regex_match(line, matches, REG_LAYOUT_CHANGED))
        {
            _event.type = LAYOUT_CHANGED;
        }
        else if (std::regex_match(line, matches, REG_OUTPUT))
        {
            _event.paneId = std::stoi(matches.str(1));
            _event.response = matches.str(2);
            _event.type = OUTPUT;
        }
        else if (std::regex_match(line, matches, REG_SESSION_CHANGED))
        {
            _event.type = SESSION_CHANGED;
            _event.sessionId = std::stoi(matches.str(1));
        }
        else if (std::regex_match(line, matches, REG_WINDOW_ADD))
        {
            _event.windowId = std::stoi(matches.str(1));
            _event.type = WINDOW_ADD;
        }
        else if (std::regex_match(line, matches, REG_WINDOW_CLOSE))
        {
            _event.type = WINDOW_CLOSE;
            _event.windowId = std::stoi(matches.str(1));
        }
        else if (std::regex_match(line, matches, REG_WINDOW_PANE_CHANGED))
        {
            _event.type = WINDOW_PANE_CHANGED;
        }
        else if (std::regex_match(line, matches, REG_WINDOW_RENAMED))
        {
            _event.type = WINDOW_RENAMED;
        }
        else if (std::regex_match(line, matches, REG_UNLINKED_WINDOW_CLOSE))
        {
            _event.type = UNLINKED_WINDOW_CLOSE;
            _event.windowId = std::stoi(matches.str(1));
        }
        else
        {
            if (_event.type == BEGIN)
            {
                _event.response += line + L'\n';
            }
            else
            {
                // Other events that we don't care, do nothing
                _event.type = NOTHING;
            }
        }

        if (_event.type != BEGIN && _event.type != NOTHING)
        {
            _EventHandle(_event);
            _event.response.clear();
        }

        return;
    }

    // from tmux to controller
    bool TmuxControl::_Advance(wchar_t ch)
    {
        std::wstring buffer = L"";

        switch(ch)
        {
            case '\033':
                buffer.push_back(ch);
                break;
            case '\n':
                buffer = std::wstring(_dcsBuffer.begin(), _dcsBuffer.end());
                break;
            case '\r':
                break;
            default:
                _dcsBuffer.push_back(ch);
                break;
        }

        if (buffer.size() > 0)
        {
            _dispatcherQueue.TryEnqueue([this, buffer]() {
                _Parse(buffer);
            });
            _dcsBuffer.clear();
        }

        return true;
    }

    bool TmuxControl::_SyncPaneState(std::vector<TmuxPane> panes, int history)
    {
        for (auto& p : panes)
        {
            auto search = _attachedPanes.find(p.paneId);
            if (search == _attachedPanes.end())
            {
                continue;
            }

            _CapturePane(p.paneId, p.cursorX, p.cursorY, history);
        }

        return true;
    }

    bool TmuxControl::_SyncWindowState(std::vector<TmuxWindow> windows)
    {
        for (auto& w : windows)
        {
            auto direction = SplitDirection::Left;
            std::shared_ptr<Pane> rootPane{ nullptr };
            for (auto& l : w.layout)
            {
                int scaleRoot;
                auto& panes = l.panes;
                auto& p = panes.at(0);
                switch (l.type)
                {
                    case SIGNLE_PANE:
                        {
                            rootPane = _NewPane(p.id);
                            _attachedPanes.insert({ p.id, rootPane });
                            continue;
                        }
                    case SPLIT_HORIZONTAL:
                        direction = SplitDirection::Left;
                        scaleRoot = p.width;
                        break;
                    case SPLIT_VERTICAL:
                        direction = SplitDirection::Up;
                        scaleRoot = p.height;
                        break;
                }

                auto search = _attachedPanes.find(p.id);
                std::shared_ptr<Pane> targetPane{ nullptr };
                int targetPandId = p.id;
                if (search == _attachedPanes.end())
                {
                    targetPane = _NewPane(p.id);
                    _attachedPanes.insert({ p.id, targetPane });
                    if (rootPane == nullptr) {
                        rootPane = targetPane;
                    }
                }
                else
                {
                    targetPane = search->second;
                }

                for (size_t i = 1; i < panes.size(); i++)
                {
                    // Create and attach
                    auto& p = panes.at(i);

                    auto pane = _NewPane(p.id);
                    _attachedPanes.insert({ p.id, pane });

                    float splitSize;
                    if (direction == SplitDirection::Left)
                    {
                        auto scalePane = panes.at(i).width;
                        splitSize = 1.0f - (float)scalePane / (float)scaleRoot;
                        scaleRoot -= scalePane;
                    }
                    else
                    {
                        auto scalePane = panes.at(i).height;
                        splitSize = 1.0f - (float)scalePane / (float)scaleRoot;
                        scaleRoot -= scalePane;
                    }
                    targetPane = targetPane->AttachPane(pane, direction, splitSize);
                    _attachedPanes.insert_or_assign(targetPandId, targetPane);
                }
            }
            auto tab = _page._CreateNewTabFromPane(rootPane);
            _attachedTabs.insert({w.windowId, tab});
            rootPane = nullptr;

            tab.try_as<TerminalTab>()->SetTabText(winrt::hstring{ w.name });
            _ListPanes(w.windowId, w.history);
        }
        return true;
    }

    std::vector<TmuxControl::Layout> TmuxControl::_ParseLayout(std::wstring& layout)
    {
        std::wregex RegPane { L"^,?(\\d+)x(\\d+),(\\d+),(\\d+),(\\d+)" };

        std::wregex RegSplitHPush { L"^,?(\\d+)x(\\d+),(\\d+),(\\d+)\\{" };
        std::wregex RegSplitVPush { L"^,?(\\d+)x(\\d+),(\\d+),(\\d+)\\[" };
        std::wregex RegSplitPop { L"^[\\} | \\]]" };
        std::vector<TmuxControl::Layout> result;

        auto _ExtractPane = [&](std::wsmatch& matches, PaneLayout& p) {
            p.width = std::stoi(matches.str(1));
            p.height = std::stoi(matches.str(2));
            p.left = std::stoi(matches.str(3));
            p.top = std::stoi(matches.str(4));
            if (matches.size() > 5)
            {
                p.id = std::stoi(matches.str(5));
            }
        };

        auto _ParseNested = [&](std::wstring) {
            std::wsmatch matches;
            size_t parse_len = 0;
            Layout l;

            std::vector<Layout> stack;

            while (layout.length() > 0) {
                if (std::regex_search(layout, matches, RegSplitHPush)) {
                    PaneLayout p;
                    _ExtractPane(matches, p);
                    l.panes.push_back(p);
                    stack.push_back(l);

                    l.type = SPLIT_HORIZONTAL;
                    l.panes.clear();
                    l.panes.push_back(p);
                } else if (std::regex_search(layout, matches, RegSplitVPush)) {
                    PaneLayout p;
                    _ExtractPane(matches, p);
                    l.panes.push_back(p);
                    stack.push_back(l);

                    // New one
                    l.type = SPLIT_VERTICAL;
                    l.panes.clear();
                    l.panes.push_back(p);
                } else if (std::regex_search(layout, matches, RegPane)) {
                    PaneLayout p;
                    _ExtractPane(matches, p);
                    l.panes.push_back(p);
                } else if (std::regex_search(layout, matches, RegSplitPop)) {
                    auto id = l.panes.back().id;
                    l.panes.pop_back();
                    l.panes.front().id = id;
                    result.insert(result.begin(), l);

                    l = stack.back();
                    l.panes.back().id = id;
                    stack.pop_back();
                } else {
                    assert(0);
                }
                parse_len = matches.length(0);
                layout = layout.substr(parse_len);
            }

            return result;
        };

        // Single pane mode
        std::wsmatch matches;
        if (std::regex_match(layout, matches, RegPane)) {
            PaneLayout p;
            _ExtractPane(matches, p);

            Layout l;
            l.type = SIGNLE_PANE;
            l.panes.push_back(p);

            result.push_back(l);
            return result;
        }

        // Nested mode
        _ParseNested(layout);

        return result;
    }

    std::wstring& TmuxControl::_DecodeOutput(const std::wstring& in, std::wstring& out)
    {
        auto it = in.begin();
        while (it != in.end())
        {
            wchar_t c = *it;
            if (c == L'\\')
            {
                ++it;
                c = 0;
                for (int i = 0; i < 3 && it != in.end(); ++i, ++it)
                {
                    if (*it < L'0' || *it > L'7')
                    {
                        c = L'?';
                        break;
                    }
                    c = c * 8 + (*it - L'0');
                }
                out.push_back(c);
                continue;
            }

            if (c == L'\n') {
                out.push_back(L'\r');
            }

            out.push_back(c);
            ++it;
        }

        return out;
    }

    // Commands
    void TmuxControl::_AttachDone()
    {
        auto cmd = std::make_unique<AttachDone>();
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::AttachDone::GetCommand()
    {
        return std::wstring(std::format(L"list-session\n"));
    }

    bool TmuxControl::AttachDone::HandleResult(std::wstring& /*result*/, TmuxControl& tmux)
    {
        if (tmux._cmdQueue.size() > 1)
        {
            tmux_log(std::format(L" size = {}", tmux._cmdQueue.size()));
            // Not done, requeue it, this is because capture may requeue in case the pane is not ready
            tmux._AttachDone();
        } else {
            tmux._state = ATTACH_DONE;
        }

        return true;
    }

    void TmuxControl::_CapturePane(int paneId, int cursorX, int cursorY, int history)
    {
        auto cmd = std::make_unique<CapturePane>();
        cmd->paneId = paneId;
        cmd->cursorX = cursorX;
        cmd->cursorY = cursorY;
        cmd->history = history;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::CapturePane::GetCommand()
    {
        return std::wstring(std::format(L"capture-pane -p -t %{} -e -C -S {}\n", this->paneId, this->history * -1));
    }

    bool TmuxControl::CapturePane::HandleResult(std::wstring& result, TmuxControl& tmux)
    {
        // Tmux output has an extra newline
        result.pop_back();
        // Put the cursor to right posit
        result += std::format(L"\033[{};{}H", this->cursorY + 1, this->cursorX + 1);
#if 0
        auto s = tmux._attachedPanes.find(this->paneId);
        if (s == tmux._attachedPanes.end()) {
            return false;
        }

        auto c = s->second->GetTerminalControl();
        result += std::format(L" {}x{}", c.ViewWidth(), c.ViewHeight());
#endif
        tmux._SendOutput(this->paneId, result);
        return true;
    }

    void TmuxControl::_DiscoverPanes(int windowId)
    {
        auto cmd = std::make_unique<DiscoverPanes>();
        cmd->windowId = windowId;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::DiscoverPanes::GetCommand()
    {
        return std::wstring(std::format(L"list-panes -F '"
                                        L"#{{pane_id}}"
                                        L"' -t @{}\n", this->windowId));
    }

    bool TmuxControl::DiscoverPanes::HandleResult(std::wstring& result, TmuxControl& tmux)
    {
        std::wstring line;
        std::wregex REG_PANE{ L"^%(\\d+)$" };

        std::wstringstream in;
        in.str(result);

        while (std::getline(in, line, L'\n'))
        {
            std::wsmatch matches;

            if (!std::regex_match(line, matches, REG_PANE)) {
                continue;
            }
            int paneId = std::stoi(matches.str(1));
            std::wstring log(std::format(L"pane: {}\n", windowId));
            tmux_log(L"   LOG: " + log);
            tmux._NewWindowAndPane(this->windowId, paneId);
        }

        return true;
    }

    void TmuxControl::_DiscoverWindows(int sessionId)
    {
        auto cmd = std::make_unique<DiscoverWindows>();
        cmd->sessionId = sessionId;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::DiscoverWindows::GetCommand()
    {
        return std::wstring(std::format(L"list-windows -F '"
                                        L"#{{window_id}}"
                                        L"' -t ${}\n", this->sessionId));
    }

    bool TmuxControl::DiscoverWindows::HandleResult(std::wstring& result, TmuxControl& tmux)
    {
        std::wstring line;
        std::wregex REG_WINDOW{ L"^@(\\d+)$" };

        std::wstringstream in;
        in.str(result);

        while (std::getline(in, line, L'\n'))
        {
            std::wsmatch matches;

            if (!std::regex_match(line, matches, REG_WINDOW)) {
                continue;
            }
            int windowId = std::stoi(matches.str(1));
            std::wstring log(std::format(L"window: {}\n", windowId));
            tmux_log(L"   LOG: " + log);
            tmux._ResizeWindow(windowId, tmux._width, tmux._height);
        }

        tmux._ListWindow(this->sessionId, -1);
        return true;
    }

    void TmuxControl::_ListPanes(int windowId, int history)
    {
        auto cmd = std::make_unique<ListPanes>();
        cmd->windowId = windowId;
        cmd->history = history;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::ListPanes::GetCommand()
    {
        return std::wstring(std::format(L"list-panes -F '"
                                        L"#{{session_id}} #{{window_id}} #{{pane_id}} "
                                        L"#{{cursor_x}} #{{cursor_y}} "
                                        L"#{{pane_active}}"
                                        L"' -t @{}\n",
                                        this->windowId));
    }

    bool TmuxControl::ListPanes::HandleResult(std::wstring& result, TmuxControl& tmux)
    {
        std::wstring line;
        std::wregex REG_PANE{ L"^\\$(\\d+) @(\\d+) %(\\d+) (\\d+) (\\d+) (\\d+)$" };
        std::vector<TmuxPane> panes;

        std::wstringstream in;
        in.str(result);

        while (std::getline(in, line, L'\n'))
        {
            TmuxPane p;
            std::wsmatch matches;

            if (!std::regex_match(line, matches, REG_PANE))
            {
                continue;
            }

            p.sessionId = std::stoi(matches.str(1));
            p.windowId = std::stoi(matches.str(2));
            p.paneId = std::stoi(matches.str(3));
            p.cursorX = std::stoi(matches.str(4));
            p.cursorY = std::stoi(matches.str(5));
            p.active = (std::stoi(matches.str(6)) == 1);

            std::wstring log(std::format(L"pane: {} {} {} {} {} {}\n", p.sessionId, p.windowId, p.paneId, p.cursorX, p.cursorY, p.active));
            tmux_log(L"   LOG: " + log);

            panes.push_back(p);
        }


        tmux._SyncPaneState(panes, this->history);
        return true;
    }

    void TmuxControl::_ListWindow(int sessionId, int windowId)
    {
        auto cmd = std::make_unique<ListWindow>();
        cmd->windowId = windowId;
        cmd->sessionId = sessionId;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::ListWindow::GetCommand()
    {
        return std::wstring(std::format(L"list-windows -F '"
                                        L"#{{session_id}} #{{window_id}} "
                                        L"#{{window_width}} #{{window_height}} "
                                        L"#{{window_active}} "
                                        L"#{{window_layout}} "
                                        L"#{{window_name}} "
                                        L"#{{history_limit}}"
                                        L"' -t ${}\n", this->sessionId));
    }

    bool TmuxControl::ListWindow::HandleResult(std::wstring& result, TmuxControl& tmux)
    {
        std::wstring line;
        std::wregex REG_WINDOW{ L"^\\$(\\d+) @(\\d+) (\\d+) (\\d+) (\\d+) ([\\dabcdefABCDEF]{4}),(\\S+) (\\S+) (\\d+)$" };
        std::vector<TmuxWindow> windows;

        std::wstringstream in;
        in.str(result);

        while (std::getline(in, line, L'\n'))
        {
            TmuxWindow w;
            std::wsmatch matches;

            if (!std::regex_match(line, matches, REG_WINDOW)) {
                continue;
            }
            w.sessionId = std::stoi(matches.str(1));
            w.windowId = std::stoi(matches.str(2));
            w.width = std::stoi(matches.str(3));
            w.height = std::stoi(matches.str(4));
            w.active = (std::stoi(matches.str(5)) == 1);
            w.layoutCsum = matches.str(6);
            w.name = matches.str(8);
            w.history = std::stoi(matches.str(9));
            std::wstring layout(matches.str(7));
            w.layout = tmux._ParseLayout(layout);
            std::wstring log(std::format(L"window: {} {} {} {} {} {} {}\n", w.sessionId, w.windowId, w.width, w.height, w.active, w.history, matches.str(7)));
            tmux_log(L"   LOG: " + log);
            windows.push_back(w);
        }

        tmux._SyncWindowState(windows);
        tmux._AttachDone();
        return true;
    }

    void TmuxControl::_NewWindow()
    {
        auto cmd = std::make_unique<NewWindow>();
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::NewWindow::GetCommand()
    {
        return std::wstring(L"new-window\n");
    }

    void TmuxControl::_ResizeWindow(int windowId, int width, int height)
    {
        auto cmd = std::make_unique<ResizeWindow>();
        cmd->windowId = windowId;
        cmd->width = width;
        cmd->height = height;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::ResizeWindow::GetCommand()
    {
        return std::wstring(std::format(L"resize-window -x {} -y {} -t @{}\n", this->width, this->height, this->windowId));
    }

    void TmuxControl::_SendKey(int paneId, const std::wstring keys)
    {
        auto cmd = std::make_unique<SendKey>();
        cmd->paneId = paneId;
        cmd->keys = keys;

        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::SendKey::GetCommand()
    {
        std::wstring out = L"";
        for (auto & c : this->keys)
        {
            out += std::format(L"{:#x} ", c);
        }

        return std::wstring(std::format(L"send-key -t %{} {}\n", this->paneId, out));
    }


    void TmuxControl::_SetOption(const std::wstring& option)
    {
        auto cmd = std::make_unique<SetOption>();
        cmd->option = option;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::SetOption::GetCommand()
    {
        return std::wstring(std::format(L"set-option {}\n", this->option));
    }

    // from controller to tmux
    void TmuxControl::_HandleCommand(std::wstring& result)
    {
        if (_cmdState == WAITING && _cmdQueue.size() > 0)
        {
            auto cmd = _cmdQueue.front().get();
            cmd->HandleResult(result, *this);
            _cmdQueue.pop_front();
            _cmdState = READY;
        }
    }

    void TmuxControl::_SendCommand(std::unique_ptr<TmuxControl::Command> cmd)
    {
        _cmdQueue.push_back(std::move(cmd));
    }

    void TmuxControl::_ScheduleCommand()
    {
        if (_cmdState != READY)
        {
            return;
        }

        _cmdState = WAITING;

        while (_cmdQueue.size() > 0)
        {
            auto cmd = _cmdQueue.front().get();
            auto cmdStr = cmd->GetCommand();
            if (cmdStr.empty())
            {
                _cmdQueue.pop_front();
                continue;
            }
            tmux_log(L"   CMD: " + cmdStr);
            _core.RawWriteString(cmdStr);
            return;
        }
        _cmdState = READY;
    }
}
