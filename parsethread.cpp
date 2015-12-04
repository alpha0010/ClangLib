#include <sdk.h>

#include "parsethread.h"

ParseThreadCreate::ParseThreadCreate(ClangProxy& proxy, wxMutex& proxyMutex, const wxString& compileCommand,
                                     const wxString& filename, const wxString& source) :
    m_Proxy(proxy), m_ProxyMutex(proxyMutex), m_CompileCommand(compileCommand.c_str()),
    m_Filename(filename.c_str()), m_Source(source.c_str())
{
}

ParseThreadCreate::~ParseThreadCreate()
{
    //dtor
}

wxThread::ExitCode ParseThreadCreate::Entry()
{
    wxMutexLocker locker(m_ProxyMutex);
    if (!m_Source.IsEmpty())
    {
        m_Proxy.CreateTranslationUnit(m_Source, m_CompileCommand);
        if (m_Proxy.GetTranslationUnitId(m_Filename) != wxNOT_FOUND)
            return 0; // got it
    }
    m_Proxy.CreateTranslationUnit(m_Filename, m_CompileCommand);
    return 0;
}
