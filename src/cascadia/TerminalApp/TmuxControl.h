// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <regex>
#include <vector>
#include <unordered_map>

#include "Pane.h"

namespace winrt::TerminalApp::implementation
{
    struct TerminalPage;

    class TmuxControl
    {
    public:
        TmuxControl(TerminalPage& page, std::shared_ptr<Pane> pane);
        ~TmuxControl();

    private:
        static const std::wregex REG_BEGIN;
        static const std::wregex REG_END;
        static const std::wregex REG_ERROR;

        static const std::wregex REG_CLIENT_SESSION_CHANGED;
        static const std::wregex REG_CLIENT_DETACHED;
        static const std::wregex REG_CONFIG_ERROR;
        static const std::wregex REG_CONTINUE;
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
            int32_t sessionId;
            int32_t windowId;
            int32_t paneId;

            std::wstring response;
        } _event;

        //=================================
        // Commands section
        //=================================
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

            int32_t paneId;
        };

        struct ListPanes : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;

            int32_t paneId;
        };

        struct ListWindows : public Command {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;

            int32_t windowId;
            int32_t sessionId;
        };

        struct NewWindow : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result, TmuxControl& tmux) override;
        };

        struct ResizePane : public Command
        {
        public:
            std::wstring GetCommand() override;

            int32_t paneId;
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

            int32_t windowId;
        };

        struct SelectPane : public Command
        {
        public:
            std::wstring GetCommand() override;

            int32_t paneId;
        };

        struct SendKey : public Command
        {
        public:
            std::wstring GetCommand() override;

            int32_t paneId;
            std::vector<wchar_t> keys;
        };

        struct SplitPane : public Command
        {
        public:
            std::wstring GetCommand() override;

            int32_t paneId;
        };

        //=================================
        // Layout section
        //=================================
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
            int historyLimit;
            bool active;
            std::wstring name;
            std::wstring layoutCsum;
            std::vector<Layout> layout;
        };

        // Private methods
        //std::shared_ptr<Pane> _NewPane(const Microsoft::Terminal::Settings::Model::NewTerminalArgs& newTerminalArgs);
        std::shared_ptr<Pane> _NewPane();
        void _NewTab();
        void _EventHandle(Event& e);

        bool _SyncWindowState(std::vector<TmuxWindow> windows);
        std::vector<Layout> _ParseLayout(std::wstring& layout);
        void _Parse(const std::wstring& buffer);
        bool _Advance(wchar_t ch);

        bool _KeyDown(wchar_t ch);
        void _SendCommand(std::unique_ptr<Command> cmd);
        void _ScheduleCommand();
        void _CloseSession();
        void _StartSession();
        void _Response(std::wstring& result);

        // Private variables
        winrt::Windows::System::DispatcherQueue _dispatcherQueue{ nullptr };
        std::shared_ptr<Pane> _controlPane{ nullptr };
        TerminalPage& _page;

        std::vector<wchar_t> _dcsBuffer;
        std::deque<std::unique_ptr<TmuxControl::Command>> _cmdQueue;
        std::unordered_map<int, std::shared_ptr<Pane>> _attachedPanes;
        std::unordered_map<int, TerminalApp::TerminalTab> _attachedTabs;
        std::unordered_map<int, winrt::Microsoft::Terminal::Control::TermControl> _attachedControl;

        int _width;
        int _height;
        int _sessionId;

        // Commands
        void _CapturePane(int paneId);
        void _ListWindows(int windowId);
        void _ResizeWindow(int windowId, int width, int height);

    };
}
