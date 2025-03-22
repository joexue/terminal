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
        _pane->GetTerminalControl().SetTmuxControlHandlerProducer([this]() {

//                _isTmux = true;

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
    }

    /*
static bool DcsHandler(TmuxControl *This, wch const wchar_t)
{
        return true;
}
*/
}
