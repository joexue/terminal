// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "TmuxControl.h"

namespace winrt::TerminalApp::implementation
{
    const std::wregex TmuxControl::REG_BEGIN{ L"^%begin \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_END{ L"^%end \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_ERROR{ L"^%error \\d+ \\d+ \\d+$" };
    const std::wregex TmuxControl::REG_SESSION_CHANGED{ L"^%session-changed \\$\\d+ \\w+$" };
    const std::wregex TmuxControl::REG_EXIT{ L"^%exit$" };

    TmuxControl::TmuxControl(std::shared_ptr<Pane> pane):
        _pane(pane)
    {
        auto _core = _pane->GetTerminalControl();

        _core.SetTmuxControlHandlerProducer([this]() {
            _state = State::ATTACHING;
            _buffer.clear();

            return [this](const auto ch) mutable {
                return _advance(ch);
            };
        });
        _core.SetTmuxKeyHandler([this](const auto ch) mutable {
            return _keyDown(ch);
        });
    }

    bool TmuxControl::_eventHandle(Event& e)
    {
        switch (e.type)
        {
        case EXIT:
            auto _core = _pane->GetTerminalControl();
            _core.RawWriteString(L"\n\n");
            break;
        }
        return true;
    }

    bool TmuxControl::_parse()
    {
        Event e;

        std::wstring line(_buffer.begin(), _buffer.end());

        if (std::regex_match(line, REG_BEGIN))
        {
            e.type = BEGIN;
        }
        else if (std::regex_match(line, REG_END))
        {
            e.type = END;
        }
        else if (std::regex_match(line, REG_ERROR))
        {
            e.type = ERR;
        }
        else if (std::regex_match(line, REG_EXIT))
        {
            e.type = EXIT;
        }
        else if (std::regex_match(line, REG_SESSION_CHANGED))
        {
            e.type = SESSION_CHANGED;
        }
        else //either we send \n to let tmux control exit or exception such as ssh broken connection
        {
            _state = INIT;
            return false;
        }

        return _eventHandle(e);
    }

    bool TmuxControl::_advance(wchar_t ch)
    {
        bool res;

        //TODO: remove them
        if (ch == '\n')
        {
            _pane->GetTerminalControl().LineFeed();
        }
        else
        {
            _pane->GetTerminalControl().Print(ch);
        }

        if (ch == '\n')
        {
            res = _parse();
            _buffer.clear();
        }
        else if (ch == '\r')
        {
            res = true;
        }
        else
        {
            _buffer.push_back(ch);
            res = true;
        }
        return res;
    }

    bool TmuxControl::_keyDown(wchar_t ch)
    {   
        if (_state != INIT)
        {
            // Only accept 'q' in tmux control pane
            if (ch == 'q')
            {
                auto _core = _pane->GetTerminalControl();
                _core.RawWriteString(L"detach\n\n\n");
            }
            return true;
        }
        else
        {
            return false;
        }
    }
}
