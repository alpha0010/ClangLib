#ifndef PARSETHREAD_H
#define PARSETHREAD_H

#include "clangproxy.h"

class ParseThreadCreate : public wxThread
{
    public:
        ParseThreadCreate(ClangProxy& proxy, wxMutex& proxyMutex, const wxString& compileCommand,
                          const wxString& filename, const wxString& source = wxEmptyString);
        virtual ~ParseThreadCreate();

    protected:
        virtual ExitCode Entry();

    private:
        ClangProxy& m_Proxy;
        wxMutex& m_ProxyMutex;
        wxString m_CompileCommand;
        wxString m_Filename;
        wxString m_Source;
};

#endif // PARSETHREAD_H
