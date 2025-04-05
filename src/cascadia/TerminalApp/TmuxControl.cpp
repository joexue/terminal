// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include <sstream>
#include <iostream>

#include <winrt/Microsoft.Terminal.TerminalConnection.h>
#include <winrt/impl/Microsoft.Terminal.TerminalConnection.1.h>

#include "pch.h"
#include "ScratchpadContent.h"
#include "TmuxControl.h"
#include "TerminalPage.h"
#include "TmuxPaneContent.h"

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
using winrt::Microsoft::Terminal::Control::TermControl;
using namespace winrt::Microsoft::Terminal::Settings::Model;
using namespace winrt::Microsoft::Terminal::TerminalConnection;
using winrt::Microsoft::Terminal::Settings::Model::SplitDirection;
using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Core;

namespace winrt::TerminalApp::implementation
{
    const std::wregex TmuxControl::REG_BEGIN{ L"^%begin \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_END{ L"^%end \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_ERROR{ L"^%error \\d+ \\d+ \\d+$" };

    const std::wregex TmuxControl::REG_CLIENT_SESSION_CHANGED{ L"^%client-session-changed \\w+ \\$\\d+ \\w+$" };
    const std::wregex TmuxControl::REG_CLIENT_DETACHED{ L"^%client-detached \\w+$" };
    const std::wregex TmuxControl::REG_CONFIG_ERROR{ L"^%config-error \\w+$" };
    const std::wregex TmuxControl::REG_CONTINUE{ L"^%continue %\\d+$" };
    const std::wregex TmuxControl::REG_EXIT{ L"^%exit$" };
    const std::wregex TmuxControl::REG_EXTENDED_OUTPUT{ L"^%extended-output %\\d+ \\w+$" };
    const std::wregex TmuxControl::REG_LAYOUT_CHANGED{ L"^%layout-change @(\\d+) ([\\dabcdefABCDEF]{4}),(\\w+)( \\w+)*$" };
    const std::wregex TmuxControl::REG_MESSAGE{ L"^%message \\w+$" };
    const std::wregex TmuxControl::REG_OUTPUT{ L"^%output %\\d+ \\w+$" };
    const std::wregex TmuxControl::REG_PANE_MODE_CHANGED{ L"^%pane-mode-changed %\\d+$" };
    const std::wregex TmuxControl::REG_PASTE_BUFFER_CHANGED{ L"^%paste-buffer-changed \\w+$" };
    const std::wregex TmuxControl::REG_PASTE_BUFFER_DELETED{ L"^%paste-buffer-deleted \\w+$" };
    const std::wregex TmuxControl::REG_PAUSE{ L"^%pause %\\d+$" };
    const std::wregex TmuxControl::REG_SESSION_CHANGED{ L"^%session-changed \\$(\\d+) \\w+$" };
    const std::wregex TmuxControl::REG_SESSION_RENAMED{ L"^%session-renamed \\w+$" };
    const std::wregex TmuxControl::REG_SESSION_WINDOW_CHANGED{ L"^%session-window-changed @(\\d+) \\d+$" };
    const std::wregex TmuxControl::REG_SESSIONS_CHANGED{ L"^%sessions-changed$" };
    const std::wregex TmuxControl::REG_SUBSCRIPTION_CHANGED{ L"^%subscription-changed \\w+$" };
    const std::wregex TmuxControl::REG_UNLINKED_WINDOW_ADD{ L"^%unlinked-window-add @\\d+$" };
    const std::wregex TmuxControl::REG_UNLINKED_WINDOW_CLOSE{ L"^%unlinked-window-close @\\d+$" };
    const std::wregex TmuxControl::REG_UNLINKED_WINDOW_RENAMED{ L"^%unlinked-window-renamed @\\d+$" };
    const std::wregex TmuxControl::REG_WINDOW_ADD{ L"^%window-add @\\d+$" };
    const std::wregex TmuxControl::REG_WINDOW_CLOSE{ L"^%window-close @\\d+$" };
    const std::wregex TmuxControl::REG_WINDOW_PANE_CHANGED{ L"^%window-pane-changed @\\d+ %\\d+$" };
    const std::wregex TmuxControl::REG_WINDOW_RENAMED{ L"^%window-renamed @\\d+ \\w+$" };

    TmuxControl::TmuxControl(TerminalPage& page, std::shared_ptr<Pane> pane) :
        _controlPane(pane),
        _page(page)
    {
        auto _core = _controlPane->GetTerminalControl();

        _core.SetTmuxControlHandlerProducer([this]() {
            _StartSession();
            
            return [this](const auto ch) mutable {
                return _Advance(ch);
            };
        });

        _core.SetTmuxKeyHandler([this](const auto ch) mutable {
            return _KeyDown(ch);
        });

        _dispatcherQueue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
        return;
    }

    TmuxControl::~TmuxControl()
    {
    }


    std::shared_ptr<Pane> TmuxControl::_NewPane()
    {
        NewTerminalArgs newTerminalArgs{ 0 };
        TerminalConnection::ITerminalConnection connection{ nullptr };
        connection = TerminalConnection::EchoConnection{};

        TerminalSettingsCreateResult controlSettings{ nullptr };

        const auto& profile = _page._settings.GetProfileForArgs(newTerminalArgs);

        controlSettings = TerminalSettings::CreateWithProfile(_page._settings, profile, *_page._bindings);
        const auto control = _page._CreateNewControlAndContent(controlSettings, connection);


        auto paneContent{ winrt::make<TerminalPaneContent> (profile, _page._terminalSettingsCache, control) };
        auto resultPane = std::make_shared<Pane>(paneContent);
        return resultPane;
    }

    void TmuxControl::_NewTab()
    {
#if 0
        _dispatcherQueue.TryEnqueue([&]() {
            NewTerminalArgs newContentArgs{ 0 };
            //_page->_OpenNewTab(newTerminalArgs);
            std::shared_ptr<Pane> p;
            _page._CreateNewTabFromPane(p = _NewPane(newContentArgs));
            _attachedPanes.insert({ 0, p });
        });
#endif
    }

    void TmuxControl::_Response(std::wstring& result)
    {
        if (_cmdState == WAITING && _cmdQueue.size() > 0)
        {
            auto cmd = _cmdQueue.front().get();
            cmd->HandleResult(result, *this);
            _cmdQueue.pop_front();
            _cmdState = READY;
        }
    }

    void TmuxControl::_CloseSession()
    {
        _dispatcherQueue.TryEnqueue([this]() {
            _state = INIT;
            _cmdQueue.clear();
            _dcsBuffer.clear();
            _cmdState = READY;

            std::vector<winrt::TerminalApp::TabBase> tabs;
            for (auto& t : _attachedTabs)
            {
                tabs.push_back(t.second);
            }
            _page._RemoveTabs(tabs);
            _attachedPanes.clear();
            _attachedTabs.clear();

            tmux_log_close();
        });
    }

    void TmuxControl::_StartSession()
    {
        _dispatcherQueue.TryEnqueue([this]() {

            auto _core = _controlPane->GetTerminalControl();
            _state = State::ATTACHING;
            _dcsBuffer.clear();

            // FIXUP: the this may be the split panel, then the width and height is not full client size
            _width = _core.ViewWidth();
            _height = _core.ViewHeight();

             tmux_log_open();
        });
    }

    void TmuxControl::_EventHandle(Event& e)
    {

        switch(e.type)
        {
        case SESSION_CHANGED:
        {
            _sessionId = e.sessionId;
            _ListWindows(-1);
        }
        break;
        case RESPONSE:
        {
            _Response(e.response);
            e.response.clear();
        }
        break;

        case EXIT:
        default:
            break;
        }

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
        else if (std::regex_match(line, REG_END))
        {
            _event.type = RESPONSE;
        }
        else if (std::regex_match(line, REG_ERROR))
        {
            _event.type = ERR;
            _event.response.clear();
        }
        // tmux specific rules
        else if (std::regex_match(line, REG_EXIT))
        {
            _event.type = EXIT;
        }
        else if (std::regex_match(line, matches, REG_LAYOUT_CHANGED))
        {
            _event.type = LAYOUT_CHANGED;
        }
        else if (std::regex_match(line, matches, REG_OUTPUT))
        {
            _event.type = OUTPUT;
        }
        else if (std::regex_match(line, matches, REG_SESSION_CHANGED))
        {
            _event.type = SESSION_CHANGED;
            _event.sessionId = std::stoi(matches.str(1));
        }
        else if (std::regex_match(line, matches, REG_WINDOW_ADD))
        {
            _event.type = WINDOW_ADD;
        }
        else if (std::regex_match(line, matches, REG_WINDOW_CLOSE))
        {
            _event.type = WINDOW_CLOSE;
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
        }
        // do nothing, but return true, otherwise the terminal will enter the DcsIgnore mode.
        else
        {
            if (_event.type == BEGIN)
            {
                //_event.type = RESPONSE;
                _event.response += line + L'\n';
            }
            else
            {
                _event.type = NOTHING;
            }
        }

        // Put in main thread
        _EventHandle(_event);

        return;
    }

    // from tmux to controller
    bool TmuxControl::_Advance(wchar_t ch)
    {
        bool res;

        if (ch == '\n')
        {
            std::wstring buffer(_dcsBuffer.begin(), _dcsBuffer.end());
            _dispatcherQueue.TryEnqueue([this, buffer]() {
                _Parse(buffer);
            });
            _dcsBuffer.clear();
            res = true;
        }
        else if (ch == '\r')
        {
            res = true;
        }
        //ESC, quit the DSC mode
        else if (ch == 27)
        {
            _CloseSession();
            res = true;
        }
        else
        {
            _dcsBuffer.push_back(ch);
            res = true;
        }
        return res;
    }

    // from controller to tmux
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

        while (_cmdQueue.size() != 0)
        {
            auto cmd = _cmdQueue.front().get();
            auto cmdStr = cmd->GetCommand();
            if (cmdStr.empty())
            {
                _cmdQueue.pop_front();
                continue;
            }
            tmux_log(L"   CMD: " + cmdStr);
            auto _core = _controlPane->GetTerminalControl();
            _core.RawWriteString(cmdStr);
            return;
        }
        _cmdState = READY;
    }

    bool TmuxControl::_KeyDown(wchar_t ch)
    {
        if (_state != INIT)
        {
            // Only accept 'q' in tmux control pane
            if (ch == 'q')
            {
                auto _core = _controlPane->GetTerminalControl();
                tmux_log(L"   CMD: detach\n");
                _core.RawWriteString(L"detach\n");
            }
            return true;
        }
        else
        {
            return false;
        }
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
                            rootPane = _NewPane();
                            _attachedPanes.insert({ p.id, rootPane });
                            auto c = rootPane->GetTerminalControl();
                            _attachedControl.insert({ p.id, c});
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
                if (search == _attachedPanes.end())
                {
                    targetPane = _NewPane();
                    _attachedPanes.insert({ p.id, targetPane });
                    auto c = targetPane->GetTerminalControl();
                    _attachedControl.insert({ p.id, c});
                    _CapturePane(p.id);
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

                    auto pane = _NewPane();
                    _attachedPanes.insert({ p.id, pane });
                    auto c = pane->GetTerminalControl();
                    _attachedControl.insert({ p.id, c});

                    float splitSize;
                    if (direction == SplitDirection::Left)
                    {
                        auto scalePane = panes.at(i).width;
                        splitSize = (float)scalePane / (float)scaleRoot;
                        scaleRoot -= scalePane;
                    }
                    else
                    {
                        auto scalePane = panes.at(i).height;
                        splitSize = (float)scalePane / (float)scaleRoot;
                        scaleRoot -= scalePane;
                    }
                    targetPane->AttachPane(pane, direction, splitSize);
                    _CapturePane(p.id);
                }
            }
            auto tab = _page._CreateNewTabFromPane(rootPane);
            _attachedTabs.insert({w.windowId, tab});
            rootPane = nullptr;
            _ResizeWindow(w.windowId, _width, _height);
        }
#if 0
        for (auto &p : _attachedPanes)
        {
            _CapturePane(p.first);
        }
#endif
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

                    //result.push_back(l);
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

    // ==============================
    // Commands Section
    // ==============================
    void TmuxControl::_CapturePane(int paneId)
    {
        auto cmd = std::make_unique<CapturePane>();
        cmd->paneId = paneId;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::CapturePane::GetCommand()
    {
        return std::wstring(std::format(L"capture-pane -p -t %{} -e -C\n", this->paneId));
    }

    bool TmuxControl::CapturePane::HandleResult(std::wstring& result, TmuxControl& tmux)
    {
        auto s = tmux._attachedControl.find(this->paneId);
        if (s == tmux._attachedControl.end()) {
            return false;
        }
        #if 1
        auto _core = s->second;

        //auto _core = p->GetLastFocusedTerminalControl();

        _core.SendInput(result);
        #endif
#if 0
        auto t = tmux._attachedTabs.at(0);
        auto tab = winrt::get_self<TerminalTab>(t);
        auto p = tab->GetRootPane();
        auto _core = p->GetTerminalControl();
        _core.SendInput(result);
        #endif
        return true;
    }

    void TmuxControl::_ListWindows(int windowId)
    {
        auto cmd = std::make_unique<ListWindows>();
        cmd->windowId = windowId;
        cmd->sessionId = _sessionId;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    std::wstring TmuxControl::ListWindows::GetCommand()
    {
        return std::wstring(std::format(L"list-windows -F '"
                                        L"#{{session_id}} #{{window_id}} "
                                        L"#{{window_width}} #{{window_height}} "
                                        L"#{{window_active}} "
                                        L"#{{window_layout}} "
                                        L"#{{history_limit}}"
                                        L"' -t ${}\n", this->sessionId));
    }

    bool TmuxControl::ListWindows::HandleResult(std::wstring& result, TmuxControl& tmux)
    {
        std::wstring line;
        std::wregex REG_WINDOW{ L"^\\$(\\d+) @(\\d+) (\\d+) (\\d+) (\\d+) ([\\dabcdefABCDEF]{4}),([\\S]+) (\\d+)$" };
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
            w.historyLimit = std::stoi(matches.str(8));
            std::wstring layout(matches.str(7));
            w.layout = tmux._ParseLayout(layout);
            std::wstring log(std::format(L"window: {} {} {} {} {} {} {}\n", w.sessionId, w.windowId, w.width, w.height, w.active, w.historyLimit, matches.str(7)));
            tmux_log(L"   LOG: " + log);
            windows.push_back(w);
        }

        tmux._SyncWindowState(windows);
        return true;
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
}
