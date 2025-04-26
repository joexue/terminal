// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <regex>
#include <vector>
#include <unordered_map>
#include "TerminalPage.g.h"

#include "Pane.h"

namespace winrt::TerminalApp::implementation
{
    struct TerminalPage;

    class TmuxControl
    {
    public:
        TmuxControl(TerminalPage& page, std::shared_ptr<Pane> pane);

    private:
        static const std::wregex REG_BEGIN;
        static const std::wregex REG_END;
        static const std::wregex REG_ERROR;

        static const std::wregex REG_CLIENT_SESSION_CHANGED;
        static const std::wregex REG_CLIENT_DETACHED;
        static const std::wregex REG_CONFIG_ERROR;
        static const std::wregex REG_CONTINUE;
        static const std::wregex REG_DETACH;
        static const std::wregex REG_EXIT;
        static const std::wregex REG_EXTENDED_OUTPUT;
        static const std::wregex REG_LAYOUT_CHANGED;
        static const std::wregex REG_MESSAGE;
        static const std::wregex REG_OUTPUT;
        static const std::wregex REG_PANE_MODE_CHANGED;
        static const std::wregex REG_PASTE_BUFFER_CHANGED;
        static const std::wregex REG_PASTE_BUFFER_DELETED;
        static const std::wregex REG_PAUSE;
        static const std::wregex REG_SESSION_CHANGED;
        static const std::wregex REG_SESSION_RENAMED;
        static const std::wregex REG_SESSION_WINDOW_CHANGED;
        static const std::wregex REG_SESSIONS_CHANGED;
        static const std::wregex REG_SUBSCRIPTION_CHANGED;
        static const std::wregex REG_UNLINKED_WINDOW_ADD;
        static const std::wregex REG_UNLINKED_WINDOW_CLOSE;
        static const std::wregex REG_UNLINKED_WINDOW_RENAMED;
        static const std::wregex REG_WINDOW_ADD;
        static const std::wregex REG_WINDOW_CLOSE;
        static const std::wregex REG_WINDOW_PANE_CHANGED;
        static const std::wregex REG_WINDOW_RENAMED;

        enum State : int
        {
            INIT,
            ATTACHING,
            ATTACH_DONE,
        } _state{ INIT };

        enum CommandState : int
        {
            READY,
            WAITING,
        } _cmdState{ READY };

        enum EventType : int
        {
            BEGIN,
            END,
            ERR,

            ATTACH,
            DETACH,
            CLIENT_SESSION_CHANGED,
            CLIENT_DETACHED,
            CONFIG_ERROR,
            CONTINUE,
            EXIT,
            EXTENEDED_OUTPUT,
            LAYOUT_CHANGED,
            NOTHING,
            MESSAGE,
            OUTPUT,
            PANE_MODE_CHANGED,
            PASTE_BUFFER_CHANGED,
            PASTE_BUFFER_DELETED,
            PAUSE,
            RESPONSE,
            SESSION_CHANGED,
            SESSION_RENAMED,
            SESSION_WINDOW_CHANGED,
            SESSIONS_CHANGED,
            SUBSCRIPTION_CHANGED,
            UNLINKED_WINDOW_ADD,
            UNLINKED_WINDOW_CLOSE,
            UNLINKED_WINDOW_RENAMED,
            WINDOW_ADD,
            WINDOW_CLOSE,
            WINDOW_PANE_CHANGED,
            WINDOW_RENAMED,
        };

        struct Event
        {
            EventType type;
            int sessionId;
            int windowId;
            int paneId;

            std::wstring response;
        } _event;

        // Command structs
        struct Command
        {
        public:
            virtual std::wstring GetCommand() = 0;
            virtual bool HandleResult(std::wstring& /*result*/, TmuxControl& /*tmux*/) { return true; };
        };

        struct AttachDone : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;
        };

        struct CapturePane : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;

            int paneId;
            int cursorX;
            int cursorY;
            int history;
        };

        struct DiscoverPanes : public Command {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;

            int windowId;
        };

        struct DiscoverWindows : public Command {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;

            int sessionId;
        };

        struct ListPanes : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;

            int windowId;
            int history;
        };

        struct ListWindow : public Command {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;

            int windowId;
            int sessionId;
        };

        struct NewWindow : public Command
        {
        public:
            std::wstring GetCommand() override;
        };

        struct ResizePane : public Command
        {
        public:
            std::wstring GetCommand() override;

            int paneId;
        };

        struct ResizeWindow : public Command
        {
        public:
            std::wstring GetCommand() override;
            int width;
            int height;
            int windowId;
        };

        struct SelectWindow : public Command
        {
        public:
            std::wstring GetCommand() override;

            int windowId;
        };

        struct SelectPane : public Command
        {
        public:
            std::wstring GetCommand() override;

            int paneId;
        };

        struct SendKey : public Command
        {
        public:
            std::wstring GetCommand() override;

            int paneId;
            //std::vector<wchar_t> keys;
            std::wstring keys;
            wchar_t key;
        };

        struct SetOption : public Command
        {
        public:
            std::wstring GetCommand() override;

            std::wstring option;
        };

        struct SplitPane : public Command
        {
        public:
            std::wstring GetCommand() override;

            int paneId;
        };

        // Layout structs
        enum LayoutType : int
        {
            SIGNLE_PANE,
            SPLIT_HORIZONTAL,
            SPLIT_VERTICAL,
        };

        struct PaneLayout
        {
            int width;
            int height;
            int left;
            int top;
            int id;
        };

        struct Layout
        {
            LayoutType type;
            std::vector<PaneLayout> panes;
        };

        struct TmuxWindow
        {
            int sessionId;
            int windowId;
            int width;
            int height;
            int history;
            bool active;
            std::wstring name;
            std::wstring layoutCsum;
            std::vector<Layout> layout;
        };

        struct TmuxPane
        {
            int sessionId;
            int windowId;
            int paneId;
            int cursorX;
            int cursorY;
            bool active;
        };

        // Private methods
        void _AttachSession();
        void _DetachSession();

        void _DetachKeyHandler(const Windows::Foundation::IInspectable& sender, const Windows::UI::Xaml::Input::KeyRoutedEventArgs& e);
        void _NewTabButtonHandler(const Microsoft::UI::Xaml::Controls::SplitButton& SplitButton, const Microsoft::UI::Xaml::Controls::SplitButtonClickEventArgs& args);

        void _CharHandler(int paneId , const winrt::Microsoft::Terminal::Control::CharSentEventArgs& args);
        void _KeyHandler(int paneId, const winrt::Microsoft::Terminal::Control::KeySentEventArgs& args);
        void _TermReadyHandler(int paneId, const std::wstring& text);

        void _SendOutput(int paneId, const std::wstring& text);
        std::wstring& _DecodeOutput(const std::wstring& in, std::wstring& out);
        std::shared_ptr<Pane> _NewPane(int paneId);
        void _WindowClose(int windowId);
        void _Output(int paneId, const std::wstring& result);

        bool _SyncWindowState(std::vector<TmuxWindow> windows);
        bool _SyncPaneState(std::vector<TmuxPane> panes, int history);
        std::vector<Layout> _ParseLayout(std::wstring& layout);

        void _EventHandle(Event& e);
        void _Parse(const std::wstring& buffer);
        bool _Advance(wchar_t ch);

        // Command methods
        void _AttachDone();
        void _CapturePane(int paneId, int cursorX, int cursorY, int history);
        void _DiscoverPanes(int sessionId);
        void _DiscoverWindows(int sessionId);
        void _ListWindow(int sessionId, int windowId);
        void _ListPanes(int windowId, int history);
        void _NewWindow();
        void _NewWindowAndPane(int windowId, int paneId);
        void _ResizeWindow(int windowId, int width, int height);
        void _SendKey(int paneId, const std::wstring keys);
        void _SetOption(const std::wstring& option);

        void _HandleCommand(std::wstring& result);
        void _SendCommand(std::unique_ptr<Command> cmd);
        void _ScheduleCommand();

        // Private variables
        TerminalPage& _page;
        winrt::Microsoft::Terminal::Settings::Model::Profile _profile;
        winrt::Microsoft::Terminal::Control::TermControl _core { nullptr };
        winrt::Windows::System::DispatcherQueue _dispatcherQueue{ nullptr };
        winrt::event_token _detachKeyRevoker;
        winrt::event_token _newTabButtonHandler;

        Microsoft::UI::Xaml::Controls::SplitButton _newTabButton{ nullptr };
        Microsoft::UI::Xaml::Controls::SplitButton _newTmuxTabButton{ nullptr };

        std::vector<wchar_t> _dcsBuffer;
        std::deque<std::unique_ptr<TmuxControl::Command>> _cmdQueue;
        std::unordered_map<int, std::shared_ptr<Pane>> _attachedPanes;
        std::unordered_map<int, TerminalApp::TerminalTab> _attachedTabs;
        std::unordered_map<int, winrt::Microsoft::Terminal::Control::TermControl> _attachedControl;
        std::unordered_map<int, std::wstring> _outputBacklog;

        int _width{ 0 };
        int _height{ 0 };
    };
}
