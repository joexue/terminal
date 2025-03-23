// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <regex>

#include <vector>
#include "Pane.h"

namespace winrt::TerminalApp::implementation
{
    class TmuxControl
    {
    public:
        TmuxControl(std::shared_ptr<Pane> pane);

    private:
        static const std::wregex REG_BEGIN;
        static const std::wregex REG_END;
        static const std::wregex REG_ERROR;

        static const std::wregex REG_SESSION_CHANGED;
        static const std::wregex REG_EXIT;

        enum State : int
        {
            INIT = 0,
            ATTACHING = 1,
        } _state{ INIT };

        enum EventType : int
        {
            BEGIN,
            END,
            ERR,
            ENTER,
            EXIT,
            SESSION_CHANGED,
        } _event;

        struct Event
        {
            EventType type;
        };

        bool _eventHandle(Event &e);
        bool _advance(wchar_t ch);
        bool _parse();
        bool _keyDown(wchar_t ch);

        std::shared_ptr<Pane> _pane { nullptr };
        std::vector<wchar_t> _buffer;
    };
}
