// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "TmuxControl.h"

//using namespace winrt::Microsoft::Terminal::Control::TermControl;

namespace winrt::TerminalApp::implementation
{
    TmuxControl::TmuxControl(std::shared_ptr<Pane> pane):
        _pane(pane)
    {
        auto _core = _pane->GetTerminalControl();

        _core.SetTmuxControlHandlerProducer([this]() {
                _state = State::ATTACHING;

                return [this](const auto ch) mutable {
                    if (ch == '\n') {
                        _pane->GetTerminalControl().LineFeed();
                    } else {
                        _pane->GetTerminalControl().Print(ch);
                    }
                    /*
                    bool ret = _tmuxDcsHandler ? _tmuxDcsHandler(ch) : false;
                    if (!ret) {
                        _isTmux = false;
                        _sendInputToConnection(L"\n");
                    }
                    return ret;
                    */
                    return true;
                };
        });
        _core.SetTmuxKeyHandler([this, _core](const auto ch) mutable {
            if (_state != INIT)
            {
                if (ch == 'q' || ch == 'Q')
                {
                    _core.RawWriteString(L"detach\n");
                }
                return true;
            }
            else
            {
                return false;
            }
        });
    }
}
