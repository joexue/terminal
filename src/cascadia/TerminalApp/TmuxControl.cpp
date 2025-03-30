// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"

#include "ScratchpadContent.h"
#include "TmuxControl.h"
#include "TerminalPage.h"
#include "TmuxPaneContent.h"

using namespace winrt::Microsoft::Terminal::Settings::Model;
namespace winrt::TerminalApp::implementation
{
    const std::wregex TmuxControl::REG_BEGIN{ L"^%begin \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_END{ L"^%end \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_ERROR{ L"^%error \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_SESSION_CHANGED{ L"^%session-changed \\$(\\d+) \\w+$" };
    const std::wregex TmuxControl::REG_EXIT{ L"^%exit$" };

    TmuxControl::TmuxControl(TerminalPage* page, std::shared_ptr<Pane> pane, winrt::Windows::System::DispatcherQueue dq) :
        _pane(pane),
        _page(page),
        _dispatchQueue(dq)
    {
        auto _core = _pane->GetTerminalControl();

        _core.SetTmuxControlHandlerProducer([this]() {
            _state = State::ATTACHING;
            _buffer.clear();
            _StartOutputThread(this);

            return [this](const auto ch) mutable {
                return _Advance(ch);
            };
        });

        _core.SetTmuxKeyHandler([this](const auto ch) mutable {
            return _KeyDown(ch);
        });


        return;
    }

    TmuxControl::~TmuxControl()
    {

    }

    void TmuxControl::_NewTab()
    {
        _dispatchQueue.TryEnqueue([&]() {
            NewTerminalArgs newContentArgs{ 0 };
            //_page->_OpenNewTab(newTerminalArgs);

            //const auto p = _page->_MakePane(newContentArgs);
            std::shared_ptr<Pane> p;
            _page->_CreateNewTabFromPane(p = _page->_MakePane(newContentArgs));
            auto _core = p->GetTerminalControl();
            _core.SendOutput(L"test\ntest\ntest");
            _core.RawWriteString(L"tett\n");

            #if 0
            const auto& scratchPane{ winrt::make_self<ScratchpadContent>() };
            auto pane = _page->_MakePane(scratchPane->GetNewTerminalArgs(BuildStartupKind::None));
            _page->_CreateNewTabFromPane(pane);
            #endif
        });
    }

    void TmuxControl::_Response(std::wstring& result)
    {
        if (_cmdState == WAITING && _cmdQueue.size() > 0)
        {
            auto cmd = _cmdQueue.front().get();
            cmd->HandleResult(result);
            _cmdQueue.pop_front();
            _cmdState = READY;
        }
    }

    void TmuxControl::_Clean()
    {
        _state = INIT;
        _cmdQueue.clear();
        _buffer.clear();
        _cmdState = READY;
        // make sure the thread can exit
        SetEvent(_hCmdEvent);
    }

    bool TmuxControl::_EventHandle()
    {

        switch(_event.type)
        {
        case SESSION_CHANGED:
        {
            auto cmd = std::make_unique<ListWindows>();
            cmd.get()->sessionId = _event.sessionId;
            _SendCommand(std::move(cmd));
            _NewTab();
        }
        break;
        case RESPONSE:
        {
            _Response(_event.response);
            _event.response.clear();
        }
        break;

        case EXIT:
        default:
            break;
        }

        _ScheduleCommand();
        return true;
    }

    std::vector<TmuxControl::Layout> TmuxControl::_ParseLayout(std::wstring& layout)
    {
        std::wregex RegPane { L"^,?(\\d+)x(\\d+),(\\d+),(\\d+),(\\d+)" };

        std::wregex RegSplitHPush { L"^,?(\\d+)x(\\d+),(\\d+),(\\d+)\\{" };
        std::wregex RegSplitVPush { L"^,?(\\d+)x(\\d+),(\\d+),(\\d+)\\[" };
        std::wregex RegSplitPop { L"^[\\} | \\]]" };
        std::vector<TmuxControl::Layout> result;

        auto _ExtractPane = [&](std::wsmatch& matches, PaneRect& p) {
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
            std::wsmatch maches;
            size_t parse_len = 0;
            Layout l;

            std::vector<Layout> stack;

            while (layout.length() > 0) {
                if (std::regex_search(layout, maches, RegSplitHPush)) {
                    PaneRect p;
                    _ExtractPane(maches, p);
                    l.panes.push_back(p);
                    stack.push_back(l);

                    l.type = SPLIT_HORIZONTAL;
                    l.panes.clear();
                    l.panes.push_back(p);
                } else if (std::regex_search(layout, maches, RegSplitVPush)) {
                    PaneRect p;
                    _ExtractPane(maches, p);
                    l.panes.push_back(p);
                    stack.push_back(l);

                    // New one
                    l.type = SPLIT_VERTICAL;
                    l.panes.clear();
                    l.panes.push_back(p);
                } else if (std::regex_search(layout, maches, RegPane)) {
                    PaneRect p;
                    _ExtractPane(maches, p);
                    l.panes.push_back(p);
                } else if (std::regex_search(layout, maches, RegSplitPop)) {
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
                parse_len = maches.length(0);
                layout = layout.substr(parse_len);
            }

            return result;
        };

        // Single pane mode
        std::wsmatch maches;
        if (std::regex_match(layout, maches, RegPane)) {
            PaneRect p;
            _ExtractPane(maches, p);

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

    bool TmuxControl::_Parse()
    {

        std::wstring line(_buffer.begin(), _buffer.end());

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
        }
        // tmux specific rules
        else if (std::regex_match(line, REG_EXIT))
        {
            _event.type = EXIT;
        }
        else if (std::regex_match(line, matches, REG_SESSION_CHANGED))
        {
            _event.type = SESSION_CHANGED;
            _event.sessionId = std::stoi(matches.str(1));
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

        return _EventHandle();
    }

    // from tmux to controller
    bool TmuxControl::_Advance(wchar_t ch)
    {
        bool res;

        //TODO: remove them
        if (ch == '\n')
        {
            _pane->GetTerminalControl().LineFeed();
        }
        else if (ch != 27)
        {
            _pane->GetTerminalControl().Print(ch);
        }

        if (ch == '\n')
        {
            res = _Parse();
            _buffer.clear();
        }
        else if (ch == '\r')
        {
            res = true;
        }
        else if (ch == 27) //ESC, quit the dsc mode
        {
            res = false;
            _Clean();
        }
        else
        {
            _buffer.push_back(ch);
            res = true;
        }
        return res;
    }

    // from controller to tmux
    void TmuxControl::_SendCommand(std::unique_ptr<TmuxControl::Command> cmd)
    {
        _cmdQueue.push_back(std::move(cmd));
        if (_cmdState == READY)
        {
            _cmdState = WAITING;
            SetEvent(_hCmdEvent);
        }
    }

    void TmuxControl::_ScheduleCommand()
    {
        if (_cmdState == READY)
        {
            _cmdState = WAITING;
            SetEvent(_hCmdEvent);
        }
    }

    bool TmuxControl::_KeyDown(wchar_t ch)
    {
        if (_state != INIT)
        {
            // Only accept 'q' in tmux control pane
            if (ch == 'q')
            {
                auto _core = _pane->GetTerminalControl();
                #if 0
                // Calculate the maximux terminal size
                const auto hwnd = reinterpret_cast<HWND>(_core.OwningHwnd());
                RECT clientRect, clientRect1;
                GetWindowRect(hwnd, &clientRect);
                GetClientRect(hwnd, &clientRect1);
                auto fd = _core.CharacterDimensions();
               // auto p = _core.GetFontSize();
                auto h = _core.ViewHeight();
                auto w = _core.ViewWidth();
                (void)h;
                (void)w;
                #endif
                
                _core.RawWriteString(L"detach\n");
            }
            return true;
        }
        else
        {
            return false;
        }
    }

    DWORD WINAPI TmuxControl::_OutputThreadProc(_In_ LPVOID lpParameter)
    {
        const auto pThis = static_cast<TmuxControl*>(lpParameter);

        while (pThis->_state != INIT)
        {
            WaitForSingleObject(pThis->_hCmdEvent, INFINITE);
            auto _core = pThis->_pane->GetTerminalControl();

            // To ensure the read side is completely done, SendInput cannot proceed if terminal is locked
            [[maybe_unused]] auto tmp = _core.HasSelection();

            while (pThis->_cmdQueue.size() != 0)
            {
                auto cmd = pThis->_cmdQueue.front().get();
                auto cmdStr = cmd->GetCommand();
                if (cmdStr.empty())
                {
                    pThis->_cmdQueue.pop_front();
                    continue;
                }
                _core.RawWriteString(cmdStr);
                break;
            }
        }

        return S_OK;
    }

    void TmuxControl::_StartOutputThread(void *lpParameter) noexcept
    {
        _hCmdEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        CreateThread(nullptr,
                                    0,
                                    TmuxControl::_OutputThreadProc,
                                    lpParameter,
                                    0,
                                    nullptr // we don't need the thread ID
        );
    }

    std::wstring TmuxControl::ListWindows::GetCommand()
    {
        return std::wstring(std::format(L"list-windows -F '"
                                        L"#{{session_id}} #{{window_id}} "
                                        L"#{{window_width}} #{{window_height}} "
                                        L"#{{window_active}} "
                                        L"#{{window_layout}} "
                                        L"#{{history_limit}} "
                                        L"' -t ${}\n", this->sessionId));
    }

    bool TmuxControl::ListWindows::HandleResult(std::wstring& /*result*/)
    {
        return true;
    }
}
