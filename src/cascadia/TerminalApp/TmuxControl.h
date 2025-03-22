// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "Pane.h"

namespace winrt::TerminalApp::implementation
{
    class TmuxControl
    {
        enum State : int {
            INIT = 0,
            ATTACHING = 1,
        };
    public:
        TmuxControl(std::shared_ptr<Pane> pane);

        winrt::Microsoft::Terminal::Control::TermControl* _control { nullptr };
        //static bool DcsHandler(TmuxControl *This, wch const wchar_t);
    private:
        std::shared_ptr<Pane> _pane { nullptr };
        State _state { State::INIT };
    };
}
