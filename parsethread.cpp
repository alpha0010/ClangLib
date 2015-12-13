#include <sdk.h>

#include "parsethread.h"

DEFINE_EVENT_TYPE(EVT_PARSE_CC_READY)

ParseThreadCreate::ParseThreadCreate(ClangProxy& proxy, wxMutex& proxyMutex, const wxString& compileCommand,
                                     const wxString& filename, const wxString& source) :
    m_Proxy(proxy), m_ProxyMutex(proxyMutex), m_CompileCommand(compileCommand.c_str()),
    m_Filename(filename.c_str()), m_Source(source.c_str())
{
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


ParseThreadCodeComplete::ParseThreadCodeComplete(ClangProxy& proxy, wxMutex& proxyMutex, bool isAuto,
                                                 const wxString& filename, int line, int column, int translId,
                                                 const std::map<wxString, wxString>& unsavedFiles,
                                                 wxEvtHandler* handler) :
    m_Proxy(proxy), m_ProxyMutex(proxyMutex), m_IsAuto(isAuto), m_Filename(filename.c_str()),
    m_Line(line), m_Column(column), m_TranslId(translId), m_pHandler(handler)
{
    for (std::map<wxString, wxString>::const_iterator fileIt = unsavedFiles.begin();
         fileIt != unsavedFiles.end(); ++fileIt)
    {
        m_UnsavedFiles[fileIt->first.c_str()] = fileIt->second.c_str();
    }
}

wxThread::ExitCode ParseThreadCodeComplete::Entry()
{
    wxMutexLocker locker(m_ProxyMutex);
    std::vector<ClToken> tokens;
    m_Proxy.CodeCompleteAt(m_IsAuto, m_Filename, m_Line, m_Column,
                           m_TranslId, m_UnsavedFiles, tokens, true);
    wxCommandEvent evt(EVT_PARSE_CC_READY);
    evt.SetInt(m_Line ^ m_Column);
    m_pHandler->AddPendingEvent(evt);
    return 0;
}
