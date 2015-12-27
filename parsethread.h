#ifndef PARSETHREAD_H
#define PARSETHREAD_H

#include "clangproxy.h"

DECLARE_LOCAL_EVENT_TYPE(EVT_PARSE_CC_READY, -1)
DECLARE_LOCAL_EVENT_TYPE(EVT_REPARSE_DONE,   -1)

class ParseThreadCreate : public wxThread
{
    public:
        ParseThreadCreate(ClangProxy& proxy, wxMutex& proxyMutex, const wxString& compileCommand,
                          const wxString& filename, const wxString& source = wxEmptyString);
        virtual ~ParseThreadCreate() {}

    protected:
        virtual ExitCode Entry();

    private:
        ClangProxy& m_Proxy;
        wxMutex& m_ProxyMutex;
        wxString m_CompileCommand;
        wxString m_Filename;
        wxString m_Source;
};


class ParseThreadCodeComplete : public wxThread
{
    public:
        ParseThreadCodeComplete(ClangProxy& proxy, wxMutex& proxyMutex, bool isAuto, const wxString& filename,
                                int line, int column, int translId, const std::map<wxString, wxString>& unsavedFiles,
                                wxEvtHandler* handler);
        virtual ~ParseThreadCodeComplete() {}

    protected:
        virtual ExitCode Entry();

    private:
        ClangProxy& m_Proxy;
        wxMutex& m_ProxyMutex;
        bool m_IsAuto;
        wxString m_Filename;
        int m_Line;
        int m_Column;
        int m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        wxEvtHandler* m_pHandler;
};


class ParseThreadReparse : public wxThread
{
    public:
        ParseThreadReparse(ClangProxy& proxy, wxMutex& proxyMutex, int translId,
                           const std::map<wxString, wxString>& unsavedFiles, wxEvtHandler* handler);
        virtual ~ParseThreadReparse() {}

    protected:
        virtual ExitCode Entry();

    private:
        ClangProxy& m_Proxy;
        wxMutex& m_ProxyMutex;
        int m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        wxEvtHandler* m_pHandler;
};

#endif // PARSETHREAD_H
