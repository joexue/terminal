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

static void tmux_log(std::wstring& str)
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
            _state = State::ATTACHING;
            _dcsBuffer.clear();

            auto _core = _controlPane->GetTerminalControl();
            // FIXUP: the this may be the split panel, then the width and height is not full client size
            _width = _core.ViewWidth();
            _height = _core.ViewHeight();

            tmux_log_open();

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


    std::shared_ptr<Pane> TmuxControl::_NewPane(const NewTerminalArgs& newTerminalArgs)
    {
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
        _dispatcherQueue.TryEnqueue([&]() {
            NewTerminalArgs newContentArgs{ 0 };
            //_page->_OpenNewTab(newTerminalArgs);
            std::shared_ptr<Pane> p;
            _page._CreateNewTabFromPane(p = _NewPane(newContentArgs));
            _attachedPanes.insert({ 0, p });
        });
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

    void TmuxControl::_ListWindows(int windowId)
    {
        auto cmd = std::make_unique<ListWindows>();
        cmd.get()->windowId = windowId;
        cmd.get()->sessionId = _sessionId;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    void TmuxControl::_RefreshClient()
    {
        auto cmd = std::make_unique<RefreshClient>();
        cmd.get()->width = _width;
        cmd.get()->height = _height;
        _SendCommand(std::move(cmd));
        _ScheduleCommand();
    }

    void TmuxControl::_Clean()
    {
        _state = INIT;
        _cmdQueue.clear();
        _dcsBuffer.clear();
        _cmdState = READY;
        // make sure the thread can exit
        tmux_log_close();
    }

    bool TmuxControl::_EventHandle(Event& e)
    {

        switch(e.type)
        {
        case SESSION_CHANGED:
        {
            _sessionId = e.sessionId;
            _RefreshClient();
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
        return true;
    }

    bool TmuxControl::_SyncWindowState(std::vector<TmuxWindow> /*windows*/)
    {
        return true;
    }

    bool TmuxControl::_Parse(std::vector<wchar_t> buffer)
    {

        std::wstring line(buffer.begin(), buffer.end());

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

        return _EventHandle(_event);
    }

    // from tmux to controller
    bool TmuxControl::_Advance(wchar_t ch)
    {
        bool res;

        tmux_log_put(ch);
        if (ch == '\n')
        {
            res = _Parse(_dcsBuffer);
            _dcsBuffer.clear();
        }
        else if (ch == '\r')
        {
            res = true;
        }
        //ESC, quit the DSC mode
        else if (ch == 27)
        {
            _Clean();
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

        _dispatcherQueue.TryEnqueue([&]() {
            while (_cmdQueue.size() != 0)
            {
                auto cmd = _cmdQueue.front().get();
                auto cmdStr = cmd->GetCommand();
                if (cmdStr.empty())
                {
                    _cmdQueue.pop_front();
                    continue;
                }
                tmux_log(cmdStr);
                auto _core = _controlPane->GetTerminalControl();
                _core.RawWriteString(cmdStr);
                return;
            }
            _cmdState = READY;
        });
    }

    bool TmuxControl::_KeyDown(wchar_t ch)
    {
        if (_state != INIT)
        {
            // Only accept 'q' in tmux control pane
            if (ch == 'q')
            {
                auto _core = _controlPane->GetTerminalControl();
                _core.RawWriteString(L"detach\n");
            }
            return true;
        }
        else
        {
            return false;
        }
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
            tmux_log(log);
            windows.push_back(w);
        }

        tmux._SyncWindowState(windows);
        return true;
    }

    std::wstring TmuxControl::RefreshClient::GetCommand()
    {
        return std::wstring(std::format(L"refresh-client -C {}x{}\n", this->width, this->height));
    }

    bool TmuxControl::RefreshClient::HandleResult(std::wstring& /*result*/, TmuxControl& tmux)
    {
        tmux._ListWindows(-1);
        return true;
    }
}
