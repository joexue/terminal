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
        TmuxControl(TerminalPage* page, std::shared_ptr<Pane> pane, winrt::Windows::System::DispatcherQueue dq);
        ~TmuxControl();

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
            ENTER,
            EXIT,
            NOTHING,
            OUTPUT,
            RESPONSE,
            SESSION_CHANGED,
        };

        struct Event
        {
            EventType type;
            int32_t sessionId;
            int32_t windowId;
            int32_t paneId;

            std::wstring response;
        } _event;

        struct Command
        {
        public:
            virtual std::wstring GetCommand() = 0;
            virtual bool HandleResult(std::wstring& result) = 0;
        };

        struct ListWindows : public Command {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t windowId;
            int32_t sessionId;
        };

        struct ListPane : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t paneId;
        };

        struct Resize : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t paneId;
        };

        struct SendKey : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t paneId;
            std::vector<wchar_t> keys;
        };

        struct CapturePane : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t paneId;
        };

        struct NewWindow : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;
        };

        struct SplitPane : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t paneId;
        };

        struct SelectWindow : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t windowId;
        };

        struct SelectPane : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;

            int32_t paneId;
        };

        struct AttachDone : public Command
        {
        public:
            std::wstring GetCommand() override;
            bool HandleResult(std::wstring& result) override;
        };

        enum LayoutType : int
        {
            SIGNLE_PANE,
            SPLIT_HORIZONTAL,
            SPLIT_VERTICAL,
        };

        struct PaneRect
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
            std::vector<PaneRect> panes;
        };

        std::shared_ptr<Pane> _NewPane(const Microsoft::Terminal::Settings::Model::NewTerminalArgs& newTerminalArgs);
        void _NewTab();
        bool _EventHandle();

        std::vector<Layout> _ParseLayout(std::wstring& layout);
        bool _Parse();
        bool _Advance(wchar_t ch);

        bool _KeyDown(wchar_t ch);
        void _SendCommand(std::unique_ptr<Command> cmd);
        void _ScheduleCommand();
        void _Clean();
        void _Response(std::wstring& result);

        static DWORD WINAPI _OutputThreadProc(_In_ LPVOID lpParameter);
        void _StartOutputThread(void* parameter) noexcept;

        HANDLE _hCmdEvent;
        winrt::Windows::System::DispatcherQueue _dispatchQueue;

        std::shared_ptr<Pane> _pane{ nullptr };
        TerminalPage* _page{ nullptr };
        std::vector<wchar_t> _buffer;

        std::deque<std::unique_ptr<TmuxControl::Command>> _cmdQueue;
        std::unordered_map<int, std::shared_ptr<Pane>> _panes;

    };
}
