#ifndef CLANGPROXY_H
#define CLANGPROXY_H

#include <map>
#include <vector>
#include <list>
#include <wx/string.h>
#include <queue>
#include <backgroundthread.h>
#include "clangpluginapi.h"
#include "translationunit.h"

#undef CLANGPROXY_TRACE_FUNCTIONS

class TranslationUnit;
class TokenDatabase;
class ClangProxy;

typedef void* CXIndex;
typedef int FileId;

enum TokenCategory
{
    tcClassFolder,
    tcClass,            tcClassPrivate,
    tcClassProtected,   tcClassPublic,
    tcCtorPrivate,      tcCtorProtected,
    tcCtorPublic,
    tcDtorPrivate,      tcDtorProtected,
    tcDtorPublic,
    tcFuncPrivate,      tcFuncProtected,
    tcFuncPublic,
    tcVarPrivate,       tcVarProtected,
    tcVarPublic,
    tcMacroDef,
    tcEnum,             tcEnumPrivate,
    tcEnumProtected,    tcEnumPublic,
    tcEnumerator,
    tcNamespace,
    tcTypedef,          tcTypedefPrivate,
    tcTypedefProtected, tcTypedefPublic,
    tcSymbolsFolder,
    tcVarsFolder,
    tcFuncsFolder,
    tcEnumsFolder,
    tcPreprocFolder,
    tcOthersFolder,
    tcTypedefFolder,
    tkMacroUse,         tcMacroPrivate,
    tcMacroProtected,   tcMacroPublic,
    tcMacroFolder,
    tcLangKeyword, // added
    tcNone = -1
};

struct ClToken // TODO: do we want this, or is just using CCToken good enough?
{
    ClToken(const wxString& nm, int _id, int _weight, int categ) :
        id(_id), category(categ), weight(_weight), name(nm) {}

    int id;
    int category;
    int weight;
    wxString name;
};

class ClangProxy
{
public:
    /*abstract */
    class ClangJob : public AbstractJob, public wxObject
    {
    public:
        enum JobType {
            CreateTranslationUnitType,
            RemoveTranslationUnitType,
            ReparseType,
            GetDiagnosticsType,
            CodeCompleteAtType,
            GetTokensAtType,
            GetCallTipsAtType,
            GetOccurrencesOfType,
            GetFunctionScopeAtType
        };
    protected:
        ClangJob( JobType JobType ) :
            AbstractJob(),
            wxObject(),
            m_JobType(JobType),
            m_pProxy(NULL)
        {
        }
        ClangJob( const ClangJob& other ) : AbstractJob(), wxObject()
        {
            m_JobType = other.m_JobType;
            m_pProxy = other.m_pProxy;
        }

    public:
        /// Returns a copy of this job on the heap to make sure the objects lifecycle is guaranteed across threads
        virtual ClangJob* Clone() const = 0;
        // Called on job thread
        virtual void Execute(ClangProxy& /*clangproxy*/) = 0;
        // Called on job thread
        virtual void Completed(ClangProxy& /*clangproxy*/) {}
        // Called on job thread
        void SetProxy( ClangProxy* pProxy ) { m_pProxy = pProxy; }
        JobType GetJobType() const { return m_JobType; }
    public:
        void operator()(){
            assert( m_pProxy != NULL );
            Execute(*m_pProxy);
            Completed(*m_pProxy);
        }
    protected:
        JobType    m_JobType;
        ClangProxy* m_pProxy;
    };

    /**
     * @brief ClangJob that posts a wxEvent back when completed
     */
    /* abstract */
    class EventJob : public ClangJob
    {
    protected:
        EventJob( JobType JobType, wxEventType evtType, int evtId ) : ClangJob(JobType), m_EventType(evtType), m_EventId(evtId)
        {
        }
        EventJob( const EventJob& other ) : ClangJob(other.m_JobType)
        {
            m_EventType = other.m_EventType;
            m_EventId = other.m_EventId;
        }
    public:
        // Called on job thread
        virtual void Completed(ClangProxy& clangProxy)
        {
            if (clangProxy.m_pEventCallbackHandler&&(m_EventType != 0) )
            {
                ClangProxy::CallbackEvent evt( m_EventType, m_EventId, this);
                clangProxy.m_pEventCallbackHandler->AddPendingEvent( evt );
            }
        }
    private:
        wxEventType m_EventType;
        int         m_EventId;
    };

    /* final */
    class CreateTranslationUnitJob : public EventJob
    {
    public:
        CreateTranslationUnitJob( wxEventType evtType, int evtId, const wxString& filename, const wxString& commands ) :
            EventJob(CreateTranslationUnitType, evtType, evtId),
            m_Filename(filename.c_str()),
            m_Commands(commands.c_str()),
            m_TranslationUnitId(-1) {}
        ClangJob* Clone() const
        {
            CreateTranslationUnitJob* job = new CreateTranslationUnitJob(*this);
            return static_cast<ClangJob*>(job);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            m_TranslationUnitId = clangproxy.GetTranslationUnitId(m_TranslationUnitId, m_Filename);
            if (m_TranslationUnitId == wxNOT_FOUND )
            {
                clangproxy.CreateTranslationUnit(m_Filename, m_Commands, m_TranslationUnitId);
            }
        }
    protected:
        CreateTranslationUnitJob(const CreateTranslationUnitJob& other):
            EventJob(other),
            m_Filename(other.m_Filename.c_str()),
            m_Commands(other.m_Commands.c_str()),
            m_TranslationUnitId(other.m_TranslationUnitId){}
    public:
        wxString m_Filename;
        wxString m_Commands;
        int m_TranslationUnitId; // Returned value
    };

    /* final */
    class RemoveTranslationUnitJob : public EventJob
    {
    public:
        RemoveTranslationUnitJob( wxEventType evtType, int evtId, int TranslUnitId ) :
            EventJob(RemoveTranslationUnitType, evtType, evtId),
            m_TranslationUnitId(TranslUnitId) {}
        ClangJob* Clone() const
        {
            RemoveTranslationUnitJob* job = new RemoveTranslationUnitJob(*this);
            return static_cast<ClangJob*>(job);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.RemoveTranslationUnit(m_TranslationUnitId);
        }
    protected:
        RemoveTranslationUnitJob(const RemoveTranslationUnitJob& other):
            EventJob(other),
            m_TranslationUnitId(other.m_TranslationUnitId){}
        int m_TranslationUnitId;
    };

    /* final */
    class ReparseJob : public EventJob
    {
    public:
        ReparseJob( wxEventType evtType, int evtId, int translId, const wxString& compileCommand, const wxString& filename, const std::map<wxString, wxString>& unsavedFiles )
            : EventJob(ReparseType, evtType, evtId),
              m_TranslId(translId),
              m_UnsavedFiles(),
              m_CompileCommand(compileCommand.c_str()),
              m_Filename(filename.c_str())
        {
            /* deep copy */
            for( std::map<wxString, wxString>::const_iterator it = unsavedFiles.begin(); it != unsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
        ClangJob* Clone() const
        {
            return new ReparseJob(*this);
        }
        void Execute(ClangProxy& clangproxy);

        virtual void Completed(ClangProxy& /*clangProxy*/)
        {
            return; // Override: the event will be posted after AsyncReparse
        }
        virtual void ReparseCompleted( ClangProxy& clangProxy )
        {
            EventJob::Completed(clangProxy);
        }
    public:
        int m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        wxString m_CompileCommand;
        wxString m_Filename;
    };

    /* final */
    class GetDiagnosticsJob : public EventJob
    {
    public:
        GetDiagnosticsJob( wxEventType evtType, int evtId, int translId ):
            EventJob(GetDiagnosticsType, evtType, evtId),
            m_TranslId(translId)
        {}
        ClangJob* Clone() const
        {
            GetDiagnosticsJob* pJob = new GetDiagnosticsJob(*this);
            pJob->m_Diagnostics = m_Diagnostics;
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.GetDiagnostics(m_TranslId, m_Diagnostics);
        }
    protected:
        GetDiagnosticsJob( const GetDiagnosticsJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Diagnostics(other.m_Diagnostics){}
    public:
        int m_TranslId;
        std::vector<ClDiagnostic> m_Diagnostics; // Returned value
    };

    class GetFunctionScopeAtJob : public EventJob
    {
    public:
        GetFunctionScopeAtJob( wxEventType evtType, int evtId, int translId, const wxString& filename, int line, int column) :
            EventJob(GetFunctionScopeAtType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column){}
        ClangJob* Clone() const
        {
            GetFunctionScopeAtJob* pJob = new GetFunctionScopeAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.GetFunctionScopeAt(m_TranslId, m_Filename, m_Line, m_Column, m_ScopeName, m_MethodName);
        }

    protected:
        GetFunctionScopeAtJob( const GetFunctionScopeAtJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Line(other.m_Line),
            m_Column(other.m_Column),
            m_ScopeName(other.m_ScopeName.c_str()),
            m_MethodName(other.m_MethodName.c_str())
        {
        }
    public:
        int m_TranslId;
        wxString m_Filename;
        int m_Line;
        int m_Column;
        wxString m_ScopeName;
        wxString m_MethodName;
    };

    /// Job designed to be run synchronous
    /*abstract */
    class SyncJob : public EventJob
    {
    protected:
        SyncJob(JobType JobType, wxEventType evtType, int evtId) :
            EventJob(JobType, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(new wxMutex()),
            m_pCond(new wxCondition(*m_pMutex))
        {
        }
        SyncJob(JobType JobType, wxEventType evtType, int evtId, wxMutex* pMutex, wxCondition* pCond) :
            EventJob(JobType, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(pMutex),
            m_pCond(pCond) {}
    public:
        // Called on Job thread
        virtual void Completed( ClangProxy& clangproxy )
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            {
                wxMutexLocker lock(*m_pMutex);
                m_bCompleted = true;
                m_pCond->Signal();
            }
            EventJob::Completed(clangproxy);
        }
        /// Called on main thread to wait for completion of this job.
        wxCondError WaitCompletion( unsigned long milliseconds )
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            wxMutexLocker lock(*m_pMutex);
            if (m_bCompleted )
            {
                return wxCOND_NO_ERROR;
            }
            return m_pCond->WaitTimeout(milliseconds);
        }
        /// Called on main thread when the last/final copy of this object will be destroyed.
        virtual void Finalize()
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            m_pMutex->Unlock();
            delete m_pMutex;
            m_pMutex = NULL;
            delete m_pCond;
            m_pCond = NULL;
        }
    protected:
        bool m_bCompleted;
        mutable wxMutex* m_pMutex;
        mutable wxCondition* m_pCond;
    };

    /* final */
    class CodeCompleteAtJob : public SyncJob
    {
    public:
        CodeCompleteAtJob( wxEventType evtType, const int evtId, const bool isAuto,
                const wxString& filename, const int line, const int column,
                const int translId, const std::map<wxString, wxString>& unsavedFiles ):
            SyncJob(CodeCompleteAtType, evtType, evtId),
            m_IsAuto(isAuto),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId),
            m_UnsavedFiles()
        {
            for( std::map<wxString, wxString>::const_iterator it = unsavedFiles.begin(); it != unsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
            m_pResults = new std::vector<ClToken>();
        }

        ClangJob* Clone() const
        {
            CodeCompleteAtJob* pJob = new CodeCompleteAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            std::vector<ClToken> results;
            clangproxy.CodeCompleteAt( m_IsAuto, m_Filename, m_Line, m_Column, m_TranslId, m_UnsavedFiles, results);
            m_pResults->swap(results);
        }
        virtual void Finalize()
        {
            SyncJob::Finalize();
        }
        const std::vector<ClToken>& GetResults()
        {
            return *m_pResults;
        }
    protected:
        CodeCompleteAtJob( const CodeCompleteAtJob& other ) :
            SyncJob(other),
            m_IsAuto(other.m_IsAuto),
            m_Filename(other.m_Filename.c_str()),
            m_Line(other.m_Line),
            m_Column(other.m_Column),
            m_TranslId(other.m_TranslId),
            m_UnsavedFiles(other.m_UnsavedFiles),
            m_pResults(other.m_pResults) {}
        bool m_IsAuto;
        wxString m_Filename;
        int m_Line;
        int m_Column;
        int m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        std::vector<ClToken>* m_pResults; // Returned value
    };

    /* final */
    class GetTokensAtJob : public SyncJob
    {
    public:
        GetTokensAtJob( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId ):
            SyncJob(GetTokensAtType, evtType, evtId),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId)
        {
            m_pResults = new wxStringVec();
        }
        ClangJob* Clone() const
        {
            GetTokensAtJob* pJob = new GetTokensAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.GetTokensAt( m_Filename, m_Line, m_Column, m_TranslId, *m_pResults);
        }
        const wxStringVec& GetResults()
        {
            return *m_pResults;
        }
    protected:
        GetTokensAtJob( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId,
                wxMutex* pMutex, wxCondition* pCond,
                wxStringVec* pResults ):
            SyncJob(GetTokensAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId),
            m_pResults(pResults) {}
        wxString m_Filename;
        int m_Line;
        int m_Column;
        int m_TranslId;
        wxStringVec* m_pResults;
    };

    /* final */
    class GetCallTipsAtJob : public SyncJob
    {
    public:
        GetCallTipsAtJob( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId, const wxString& tokenStr ):
            SyncJob( GetCallTipsAtType, evtType, evtId),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId),
            m_TokenStr(tokenStr)
        {
            m_pResults = new std::vector<wxStringVec>();
        }
        ClangJob* Clone() const
        {
            GetCallTipsAtJob* pJob = new GetCallTipsAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.GetCallTipsAt( m_Filename, m_Line, m_Column, m_TranslId, m_TokenStr, *m_pResults);
        }
        const std::vector<wxStringVec>& GetResults()
        {
            return *m_pResults;
        }
    protected:
        GetCallTipsAtJob( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId, const wxString& tokenStr,
                wxMutex* pMutex, wxCondition* pCond,
                std::vector<wxStringVec>* pResults ):
            SyncJob( GetCallTipsAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId),
            m_TokenStr(tokenStr),
            m_pResults(pResults) {}
        wxString m_Filename;
        int m_Line;
        int m_Column;
        int m_TranslId;
        wxString m_TokenStr;
        std::vector<wxStringVec>* m_pResults;
    };

    /* final */
    class GetOccurrencesOfJob : public SyncJob
    {
    public:
        GetOccurrencesOfJob( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId ):
            SyncJob( GetOccurrencesOfType, evtType, evtId),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId)
        {
            m_pResults = new std::vector< std::pair<int, int> >();
        }
        ClangJob* Clone() const
        {
            GetOccurrencesOfJob* pJob = new GetOccurrencesOfJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.GetOccurrencesOf( m_Filename, m_Line, m_Column, m_TranslId, *m_pResults);
        }
        const std::vector< std::pair<int, int> >& GetResults()
        {
            return *m_pResults;
        }
    protected:
        GetOccurrencesOfJob( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId,
                wxMutex* pMutex, wxCondition* pCond,
                std::vector< std::pair<int, int> >* pResults ):
            SyncJob(GetOccurrencesOfType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId),
            m_pResults(pResults) {}
        wxString m_Filename;
        int m_Line;
        int m_Column;
        int m_TranslId;
        std::vector< std::pair<int, int> >* m_pResults;
    };


    /**
     * Helper class that manages the lifecycle of the Get/SetEventObject() object when passing threads
     */
    class CallbackEvent : public wxEvent
    {
    public:
        CallbackEvent( wxEventType evtType, int evtId, ClangJob* job ) : wxEvent( evtType, evtId )
        {
            SetEventObject(job);
        }
        CallbackEvent( const CallbackEvent& other ) : wxEvent(other)
        {
            ClangProxy::ClangJob* pJob = static_cast<ClangProxy::ClangJob*>(other.GetEventObject());
            if (pJob)
                SetEventObject( pJob->Clone() );
        }
        ~CallbackEvent()
        {
            wxObject* obj = GetEventObject();
            delete obj;
        }
        wxEvent* Clone() const
        {
            ClangProxy::ClangJob* pJob = static_cast<ClangProxy::ClangJob*>(GetEventObject());
            if (pJob )
                pJob = pJob->Clone();
            return new CallbackEvent( m_eventType, m_id, pJob );
        }
    };

public:
    ClangProxy( wxEvtHandler* pEvtHandler, TokenDatabase& database, const std::vector<wxString>& cppKeywords);
    ~ClangProxy();

    void AppendPendingJob( ClangProxy::ClangJob& job );
    //void PrependPendingJob( ClangProxy::ClangJob& job );

    int GetTranslationUnitId(int CtxTranslUnitId, FileId fId);
    int GetTranslationUnitId(int CtxTranslUnitId, const wxString& filename);

protected: // jobs that are run only on the thread
    void CreateTranslationUnit(const wxString& filename, const wxString& compileCommand, int& out_TranslId);
    void RemoveTranslationUnit(int TranslUnitId);
    /** Reparse translation id
     *
     * @param unsavedFiles reference to the unsaved files data. This function takes the data and this list will be empty after this call
     */
    void Reparse(int translId, const wxString& compileCommand, std::map<wxString, wxString>& unsavedFiles);
    void GetDiagnostics(int translId, std::vector<ClDiagnostic>& diagnostics);
    void CodeCompleteAt(bool isAuto, const wxString& filename, int line, int column, int translId,
            const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results);
    void GetTokensAt(const wxString& filename, int line, int column, int translId, std::vector<wxString>& results);
    void GetCallTipsAt(const wxString& filename, int line, int column, int translId,
            const wxString& tokenStr, std::vector<wxStringVec>& results);
    void GetOccurrencesOf(const wxString& filename, int line, int column,
            int translId, std::vector< std::pair<int, int> >& results);


public:
    wxString DocumentCCToken(ClTranslUnitId translId, int tknId);
    wxString GetCCInsertSuffix(ClTranslUnitId translId, int tknId, const wxString& newLine, std::pair<int, int>& offsets);
    void RefineTokenType(ClTranslUnitId translId, int tknId, int& tknType); // TODO: cache TokenId (if resolved) for DocumentCCToken()

    void ResolveDeclTokenAt(wxString& filename, int& line, int& column, int translId);
    void GetFunctionScopeAt(ClTranslUnitId translId, const wxString& filename, int line, int column,wxString &out_ClassName, wxString &out_FunctionName );
    wxStringVec GetFunctionScopes( ClTranslUnitId, const wxString& filename );

    void TakeTranslationUnit( TranslationUnit& translUnit );
    void ParsedTranslationUnit( TranslationUnit& translUnit );
    void ReturnTranslationUnit( TranslationUnit& translUnit );

private:
    mutable wxMutex m_Mutex;
    TokenDatabase& m_Database;
    const std::vector<wxString>& m_CppKeywords;
    std::vector<TranslationUnit> m_TranslUnits;
    CXIndex m_ClIndex[2];
private: // Thread
    wxEvtHandler* m_pEventCallbackHandler;
    BackgroundThread* m_pThread;
    BackgroundThread* m_pParsingThread;
    TranslationUnit m_ParsingTranslUnit;
};

#endif // CLANGPROXY_H
