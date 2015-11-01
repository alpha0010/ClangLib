#ifndef CLANGPROXY_H
#define CLANGPROXY_H

#include <map>
#include <vector>
#include <list>
#include <wx/string.h>
#include <queue>

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

enum Severity { sWarning, sError };
struct ClDiagnostic
{
    ClDiagnostic(int ln, int rgStart, int rgEnd, Severity level, const wxString& fl, const wxString& msg) :
        line(ln), range(rgStart, rgEnd), severity(level), file(fl), message(msg) {}

    int line;
    std::pair<int, int> range;
    Severity severity;
    wxString file;
    wxString message;
};

class ClangProxy
{
public:
    /*abstract */
    class Task : public wxObject
    {
    protected:
        Task( wxEventType evtType, int evtId ) : wxObject(), m_EventType(evtType), m_EventId(evtId)
        {
        }
        Task( const Task& other ) : wxObject()
        {
            m_EventType = other.m_EventType;
            m_EventId = other.m_EventId;
        }

    public:
        /// Returns a copy of this task on the heap to make sure the objects lifecycle is guaranteed across threads
        virtual Task* Clone() const = 0;
        virtual void operator()(ClangProxy& /*clangproxy*/) {}
        virtual void Completed() {}
        wxEventType GetCallbackEventType() const
        {
            return m_EventType;
        }
        int GetCallbackEventId() const
        {
            return m_EventId;
        }

    protected:
        wxEventType m_EventType;
        int         m_EventId;
    };

    /* final */
    class CreateTranslationUnitTask : public Task
    {
    public:
        CreateTranslationUnitTask( wxEventType evtType, int evtId, const wxString& filename, const wxString& commands ) :
            Task(evtType, evtId),
            m_Filename(filename),
            m_Commands(commands),
            m_TranslationUnitId(0) {}
        Task* Clone() const
        {
            CreateTranslationUnitTask* task = new CreateTranslationUnitTask(*this);
            return static_cast<Task*>(task);
        }
        void operator()(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.CreateTranslationUnit(m_Filename, m_Commands);
            m_TranslationUnitId = clangproxy.GetTranslationUnitId(m_Filename);
        }
    protected:
        CreateTranslationUnitTask(const CreateTranslationUnitTask& other):
            Task(other.m_EventType, other.m_EventId),
            m_Filename(other.m_Filename),
            m_Commands(other.m_Commands),
            m_TranslationUnitId(other.m_TranslationUnitId){}
    public:
        wxString m_Filename;
        wxString m_Commands;
        int m_TranslationUnitId; // Returned value
    };

    /* final */
    class ReparseTask : public Task
    {
    public:
        ReparseTask( wxEventType evtType, int evtId, int translId, const std::map<wxString, wxString>& unsavedFiles )
            : Task(evtType, evtId),
              m_TranslId(translId),
              m_UnsavedFiles(unsavedFiles)
        {}
        Task* Clone() const
        {
            return new ReparseTask(m_EventType, m_EventId, m_TranslId, m_UnsavedFiles);
        }
        void operator()(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.Reparse(m_TranslId, m_UnsavedFiles);
        }
    public:
        int m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
    };

    /* final */
    class GetDiagnosticsTask : public Task
    {
    public:
        GetDiagnosticsTask( wxEventType evtType, int evtId, int translId ):
            Task(evtType, evtId),
            m_TranslId(translId)
        {}
    public:
        Task* Clone() const
        {
            GetDiagnosticsTask* pTask = new GetDiagnosticsTask(*this);
            pTask->m_Diagnostics = m_Diagnostics;
            return static_cast<Task*>(pTask);
        }
        void operator()(ClangProxy& clangproxy)
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            clangproxy.GetDiagnostics(m_TranslId, m_Diagnostics);
        }
    protected:
        GetDiagnosticsTask( const GetDiagnosticsTask& other ) :
            Task(other.m_EventType, other.m_EventId),
            m_TranslId(other.m_TranslId),
            m_Diagnostics(other.m_Diagnostics){}
    public:
        int m_TranslId;
        std::vector<ClDiagnostic> m_Diagnostics; // Returned value
    };

    /// Task designed to be run synchronous
    /*abstract */
    class SyncTask : public Task
    {
    protected:
        SyncTask(wxEventType evtType, int evtId) :
            Task(evtType, evtId),
            m_bCompleted(false),
            m_pMutex(new wxMutex()),
            m_pCond(new wxCondition(*m_pMutex))
        {
        }
        SyncTask(wxEventType evtType, int evtId, wxMutex* pMutex, wxCondition* pCond) :
            Task(evtType, evtId),
            m_bCompleted(false),
            m_pMutex(pMutex),
            m_pCond(pCond) {}
    public:
        // Called on task thread
        virtual void Completed()
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            wxMutexLocker lock(*m_pMutex);
            m_bCompleted = true;
            m_pCond->Signal();
        }
        /// Called on main thread to wait for completion of this task.
        wxCondError WaitCompletion( unsigned long milliseconds )
        {
#ifdef CLANGPROXY_TRACE_FUNCTIONS
            fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
#endif
            wxMutexLocker lock(*m_pMutex);
            if( m_bCompleted )
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
    class CodeCompleteAtTask : public SyncTask
    {
    public:
        CodeCompleteAtTask( wxEventType evtType, const int evtId, const bool isAuto,
                const wxString& filename, const int line, const int column,
                const int translId, const std::map<wxString, wxString>& unsavedFiles ):
            SyncTask(evtType, evtId),
            m_IsAuto(isAuto),
            m_Filename(filename),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId),
            m_UnsavedFiles(unsavedFiles)
        {
            m_pResults = new std::vector<ClToken>();
        }

        Task* Clone() const
        {
            CodeCompleteAtTask* pTask = new CodeCompleteAtTask(*this);
            return static_cast<Task*>(pTask);
        }
        void operator()(ClangProxy& clangproxy)
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
            SyncTask::Finalize();
            delete m_pResults;
        }
        const std::vector<ClToken>& GetResults()
        {
            return *m_pResults;
        }
    protected:
        CodeCompleteAtTask( const CodeCompleteAtTask& other ) :
            SyncTask(other.m_EventType, other.m_EventId, other.m_pMutex, other.m_pCond),
            m_IsAuto(other.m_IsAuto),
            m_Filename(other.m_Filename),
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
    class GetTokensAtTask : public SyncTask
    {
    public:
        GetTokensAtTask( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId ):
            SyncTask(evtType, evtId),
            m_Filename(filename),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId)
        {
            m_pResults = new wxStringVec();
        }
        Task* Clone() const
        {
            GetTokensAtTask* pTask = new GetTokensAtTask(m_EventType, m_EventId, m_Filename, m_Line, m_Column, m_TranslId, m_pMutex, m_pCond, m_pResults);
            return static_cast<Task*>(pTask);
        }
        void operator()(ClangProxy& clangproxy)
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
        GetTokensAtTask( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId,
                wxMutex* pMutex, wxCondition* pCond,
                wxStringVec* pResults ):
            SyncTask(evtType, evtId, pMutex, pCond),
            m_Filename(filename),
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
    class GetCallTipsAtTask : public SyncTask
    {
    public:
        GetCallTipsAtTask( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId, const wxString& tokenStr ):
            SyncTask(evtType, evtId),
            m_Filename(filename),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId),
            m_TokenStr(tokenStr)
        {
            m_pResults = new std::vector<wxStringVec>();
        }
        Task* Clone() const
        {
            GetCallTipsAtTask* pTask = new GetCallTipsAtTask(m_EventType, m_EventId, m_Filename, m_Line, m_Column, m_TranslId, m_TokenStr, m_pMutex, m_pCond, m_pResults);
            return static_cast<Task*>(pTask);
        }
        void operator()(ClangProxy& clangproxy)
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
        GetCallTipsAtTask( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId, const wxString& tokenStr,
                wxMutex* pMutex, wxCondition* pCond,
                std::vector<wxStringVec>* pResults ):
            SyncTask(evtType, evtId, pMutex, pCond),
            m_Filename(filename),
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
    class GetOccurrencesOfTask : public SyncTask
    {
    public:
        GetOccurrencesOfTask( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId ):
            SyncTask(evtType, evtId),
            m_Filename(filename),
            m_Line(line),
            m_Column(column),
            m_TranslId(translId)
        {
            m_pResults = new std::vector< std::pair<int, int> >();
        }
        Task* Clone() const
        {
            GetOccurrencesOfTask* pTask = new GetOccurrencesOfTask(m_EventType, m_EventId, m_Filename, m_Line, m_Column, m_TranslId, m_pMutex, m_pCond, m_pResults);
            return static_cast<Task*>(pTask);
        }
        void operator()(ClangProxy& clangproxy)
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
        GetOccurrencesOfTask( wxEventType evtType, int evtId, const wxString& filename, int line, int column, int translId,
                wxMutex* pMutex, wxCondition* pCond,
                std::vector< std::pair<int, int> >* pResults ):
            SyncTask(evtType, evtId, pMutex, pCond),
            m_Filename(filename),
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
        CallbackEvent( wxEventType evtType, int evtId, Task* task ) : wxEvent( evtType, evtId )
        {
            SetEventObject(task);
        }
        CallbackEvent( const CallbackEvent& other ) : wxEvent(other)
        {
            ClangProxy::Task* pTask = static_cast<ClangProxy::Task*>(other.GetEventObject());
            if (pTask)
                SetEventObject( pTask->Clone() );
        }
        ~CallbackEvent()
        {
            wxObject* obj = GetEventObject();
            delete obj;
        }
        wxEvent* Clone() const
        {
            ClangProxy::Task* pTask = static_cast<ClangProxy::Task*>(GetEventObject());
            if( pTask )
                pTask = pTask->Clone();
            return new CallbackEvent( m_eventType, m_id, pTask );
        }
    };


    class TaskThread : public wxThread
    {
    public:
        TaskThread( ClangProxy* pClangProxy ) :
            wxThread(wxTHREAD_DETACHED),
            m_pClangProxy(pClangProxy) {}

        wxThread::ExitCode Entry();
    public:
        ClangProxy* m_pClangProxy;
    };

public:
    ClangProxy( wxEvtHandler* pEvtHandler, TokenDatabase& database, const std::vector<wxString>& cppKeywords);
    ~ClangProxy();

    void AddPendingTask( ClangProxy::Task& task );

    int GetTranslationUnitId(FileId fId);
    int GetTranslationUnitId(const wxString& filename);

protected: // Tasks that are run only on the thread
    void CreateTranslationUnit(const wxString& filename, const wxString& commands);
    void Reparse(int translId, const std::map<wxString, wxString>& unsavedFiles);
    void GetDiagnostics(int translId, std::vector<ClDiagnostic>& diagnostics);
    void CodeCompleteAt(bool isAuto, const wxString& filename, int line, int column, int translId,
            const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results);
    void GetTokensAt(const wxString& filename, int line, int column, int translId, std::vector<wxString>& results);
    void GetCallTipsAt(const wxString& filename, int line, int column, int translId,
            const wxString& tokenStr, std::vector<wxStringVec>& results);
    void GetOccurrencesOf(const wxString& filename, int line, int column,
            int translId, std::vector< std::pair<int, int> >& results);

public:
    wxString DocumentCCToken(int translId, int tknId);
    wxString GetCCInsertSuffix(int translId, int tknId, const wxString& newLine, std::pair<int, int>& offsets);
    void RefineTokenType(int translId, int tknId, int& tknType); // TODO: cache TokenId (if resolved) for DocumentCCToken()


    void ResolveDeclTokenAt(wxString& filename, int& line, int& column, int translId);

private:
    mutable wxMutex m_Mutex;
    TokenDatabase& m_Database;
    const std::vector<wxString>& m_CppKeywords;
    std::vector<TranslationUnit> m_TranslUnits;
    CXIndex m_ClIndex;
private: // Thread
    mutable wxMutex m_TaskQueueMutex; // Protects: m_TaskQueue
    wxEvtHandler* m_pEventCallbackHandler;
    std::queue<ClangProxy::Task*> m_TaskQueue;
    mutable wxCondition m_ConditionQueueNotEmpty;
    TaskThread* m_pThread;
};

#endif // CLANGPROXY_H
