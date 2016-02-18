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

class ClTranslationUnit;
class ClTokenDatabase;
class ClangProxy;

typedef void* CXIndex;
typedef int ClFileId;

class ClangProxy
{
public:
    /*abstract */
    class ClangJob : public AbstractJob, public wxObject
    {
    public:
        enum JobType
        {
            CreateTranslationUnitType,
            RemoveTranslationUnitType,
            ReparseType,
            UpdateTokenDatabaseType,
            GetDiagnosticsType,
            CodeCompleteAtType,
            DocumentCCTokenType,
            GetTokensAtType,
            GetCallTipsAtType,
            GetOccurrencesOfType,
            GetFunctionScopeAtType
        };
    protected:
        ClangJob(JobType jt) :
            AbstractJob(),
            wxObject(),
            m_JobType(jt),
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
        void SetProxy( ClangProxy* pProxy )
        {
            m_pProxy = pProxy;
        }
        JobType GetJobType() const
        {
            return m_JobType;
        }
    public:
        void operator()()
        {
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
        EventJob(JobType jt, wxEventType evtType, int evtId) :
            ClangJob(jt), m_EventType(evtType), m_EventId(evtId)
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
        CreateTranslationUnitJob( wxEventType evtType, int evtId, const wxString& filename, const wxString& commands, const std::map<wxString, wxString>& unsavedFiles ) :
            EventJob(CreateTranslationUnitType, evtType, evtId),
            m_Filename(filename),
            m_Commands(commands),
            m_TranslationUnitId(-1),
            m_UnsavedFiles(unsavedFiles)
        {
        }
        ClangJob* Clone() const
        {
            CreateTranslationUnitJob* job = new CreateTranslationUnitJob(*this);
            return static_cast<ClangJob*>(job);
        }
        void Execute(ClangProxy& clangproxy)
        {
            m_TranslationUnitId = clangproxy.GetTranslationUnitId(m_TranslationUnitId, m_Filename);
            if (m_TranslationUnitId == wxNOT_FOUND )
            {
                clangproxy.CreateTranslationUnit(m_Filename, m_Commands, m_UnsavedFiles, m_TranslationUnitId);
            }
            m_UnsavedFiles.clear();
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslationUnitId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
    protected:
        CreateTranslationUnitJob(const CreateTranslationUnitJob& other):
            EventJob(other),
            m_Filename(other.m_Filename.c_str()),
            m_Commands(other.m_Commands.c_str()),
            m_TranslationUnitId(other.m_TranslationUnitId),
            m_UnsavedFiles()
        {
            /* deep copy */
            for ( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
    public:
        wxString m_Filename;
        wxString m_Commands;
        ClTranslUnitId m_TranslationUnitId; // Returned value
        std::map<wxString, wxString> m_UnsavedFiles;
    };

    /* final */
    class RemoveTranslationUnitJob : public EventJob
    {
    public:
        RemoveTranslationUnitJob( wxEventType evtType, int evtId, int TranslUnitId ) :
            EventJob(RemoveTranslationUnitType, evtType, evtId),
            m_TranslUnitId(TranslUnitId) {}
        ClangJob* Clone() const
        {
            RemoveTranslationUnitJob* job = new RemoveTranslationUnitJob(*this);
            return static_cast<ClangJob*>(job);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.RemoveTranslationUnit(m_TranslUnitId);
        }
    protected:
        RemoveTranslationUnitJob(const RemoveTranslationUnitJob& other):
            EventJob(other),
            m_TranslUnitId(other.m_TranslUnitId) {}
        ClTranslUnitId m_TranslUnitId;
    };

    /* final */
    class ReparseJob : public EventJob
    {
    public:
        ReparseJob( wxEventType evtType, int evtId, ClTranslUnitId translId, const wxString& compileCommand, const wxString& filename, const std::map<wxString, wxString>& unsavedFiles, bool parents = false )
            : EventJob(ReparseType, evtType, evtId),
              m_TranslId(translId),
              m_UnsavedFiles(unsavedFiles),
              m_CompileCommand(compileCommand.c_str()),
              m_Filename(filename.c_str()),
              m_Parents(parents)
        {
        }
        ClangJob* Clone() const
        {
            return new ReparseJob(*this);
        }
        void Execute(ClangProxy& clangproxy);
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
    private:
        ReparseJob( const ReparseJob& other )
            : EventJob(other),
              m_TranslId(other.m_TranslId),
              m_UnsavedFiles(),
              m_CompileCommand(other.m_CompileCommand.c_str()),
              m_Filename(other.m_Filename.c_str()),
              m_Parents(other.m_Parents)
        {
            /* deep copy */
            for ( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
    public:
        ClTranslUnitId m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        wxString m_CompileCommand;
        wxString m_Filename;
        bool m_Parents; // If the parents also need to be reparsed
    };

    /* final */
    class UpdateTokenDatabaseJob : public EventJob
    {
    public:
        UpdateTokenDatabaseJob( wxEventType evtType, int evtId, int translId ) :
            EventJob(UpdateTokenDatabaseType, evtType, evtId),
            m_TranslId(translId)
        {
        }
        ClangJob* Clone() const
        {
            return new UpdateTokenDatabaseJob(*this);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.UpdateTokenDatabase(m_TranslId);
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }

    private:
        ClTranslUnitId m_TranslId;
    };

    /* final */
    class GetDiagnosticsJob : public EventJob
    {
    public:
        GetDiagnosticsJob( wxEventType evtType, int evtId, int translId, const wxString& filename ):
            EventJob(GetDiagnosticsType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename)
        {}
        ClangJob* Clone() const
        {
            GetDiagnosticsJob* pJob = new GetDiagnosticsJob(*this);
            pJob->m_Results = m_Results;
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetDiagnostics(m_TranslId, m_Filename, m_Results);
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const std::vector<ClDiagnostic>& GetResults() const
        {
            return m_Results;
        }

    protected:
        GetDiagnosticsJob( const GetDiagnosticsJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Results(other.m_Results) {}
    public:
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        std::vector<ClDiagnostic> m_Results; // Returned value
    };

    class GetFunctionScopeAtJob : public EventJob
    {
    public:
        GetFunctionScopeAtJob( wxEventType evtType, int evtId, int translId, const wxString& filename, const ClTokenPosition& location) :
            EventJob(GetFunctionScopeAtType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Location(location) {}
        ClangJob* Clone() const
        {
            GetFunctionScopeAtJob* pJob = new GetFunctionScopeAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetFunctionScopeAt(m_TranslId, m_Filename, m_Location, m_ScopeName, m_MethodName);
        }

    protected:
        GetFunctionScopeAtJob( const GetFunctionScopeAtJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Location(other.m_Location),
            m_ScopeName(other.m_ScopeName.c_str()),
            m_MethodName(other.m_MethodName.c_str())
        {
        }
    public:
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Location;
        wxString m_ScopeName;
        wxString m_MethodName;
    };

    /// Job designed to be run synchronous
    /*abstract */
    class SyncJob : public EventJob
    {
    protected:
        SyncJob(JobType jt, wxEventType evtType, int evtId) :
            EventJob(jt, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(new wxMutex()),
            m_pCond(new wxCondition(*m_pMutex))
        {
        }
        SyncJob(JobType jt, wxEventType evtType, int evtId, wxMutex* pMutex, wxCondition* pCond) :
            EventJob(jt, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(pMutex),
            m_pCond(pCond) {}
    public:
        // Called on Job thread
        virtual void Completed( ClangProxy& clangproxy )
        {
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
                           const wxString& filename, const ClTokenPosition& location,
                           const ClTranslUnitId translId, const std::map<wxString, wxString>& unsavedFiles,
                           bool includeCtors ):
            SyncJob(CodeCompleteAtType, evtType, evtId),
            m_IsAuto(isAuto),
            m_Filename(filename),
            m_Location(location),
            m_TranslId(translId),
            m_UnsavedFiles(unsavedFiles),
            m_IncludeCtors(includeCtors),
            m_pResults(new std::vector<ClToken>()),
            m_Diagnostics()
        {
        }

        ClangJob* Clone() const
        {
            CodeCompleteAtJob* pJob = new CodeCompleteAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            std::vector<ClToken> results;
            clangproxy.CodeCompleteAt( m_TranslId, m_Filename, m_Location, m_IsAuto, m_UnsavedFiles, results, m_Diagnostics);
            for (std::vector<ClToken>::iterator tknIt = results.begin(); tknIt != results.end(); ++tknIt)
            {
                switch (tknIt->category)
                {
                case tcCtorPublic:
                case tcDtorPublic:
                    if ( !m_IncludeCtors )
                        continue;
                case tcClass:
                case tcFuncPublic:
                case tcVarPublic:
                case tcEnum:
                case tcTypedef:
                    clangproxy.RefineTokenType(m_TranslId, tknIt->id, tknIt->category);
                    break;
                default:
                    break;
                }
            }

            // Get rid of some copied memory we no longer need
            m_UnsavedFiles.clear();

            m_pResults->swap(results);
        }
        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetLocation() const
        {
            return m_Location;
        }
        const std::vector<ClToken>& GetResults() const
        {
            return *m_pResults;
        }
        const std::vector<ClDiagnostic>& GetDiagnostics() const
        {
            return m_Diagnostics;
        }
    protected:
        CodeCompleteAtJob( const CodeCompleteAtJob& other ) :
            SyncJob(other),
            m_IsAuto(other.m_IsAuto),
            m_Filename(other.m_Filename.c_str()),
            m_Location(other.m_Location),
            m_TranslId(other.m_TranslId),
            m_IncludeCtors(other.m_IncludeCtors),
            m_pResults(other.m_pResults),
            m_Diagnostics(other.m_Diagnostics)
        {
            for ( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }

        }
        bool m_IsAuto;
        wxString m_Filename;
        ClTokenPosition m_Location;
        ClTranslUnitId m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        bool m_IncludeCtors;
        std::vector<ClToken>* m_pResults; // Returned value
        std::vector<ClDiagnostic> m_Diagnostics;
    };

    /* final */
    class DocumentCCTokenJob : public SyncJob
    {
    public:
        DocumentCCTokenJob( wxEventType evtType, const int evtId, ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location, ClTokenId tknId ):
            SyncJob(DocumentCCTokenType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Location(location),
            m_TokenId(tknId),
            m_pResult(new wxString())
        {
        }

        ClangJob* Clone() const
        {
            DocumentCCTokenJob* pJob = new DocumentCCTokenJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            wxString str = clangproxy.DocumentCCToken( m_TranslId, m_TokenId );
            *m_pResult = str;
        }
        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResult;
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetLocation() const
        {
            return m_Location;
        }
        const wxString& GetResult()
        {
            return *m_pResult;
        }
    protected:
        DocumentCCTokenJob( const DocumentCCTokenJob& other ) :
            SyncJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Location(other.m_Location),
            m_TokenId(other.m_TokenId),
            m_pResult(other.m_pResult) {}
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Location;
        ClTokenId m_TokenId;
        wxString* m_pResult;
    };
    /* final */
    class GetTokensAtJob : public SyncJob
    {
    public:
        GetTokensAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId ):
            SyncJob(GetTokensAtType, evtType, evtId),
            m_Filename(filename),
            m_Location(location),
            m_TranslId(translId),
            m_pResults(new wxStringVec())
        {
        }

        ClangJob* Clone() const
        {
            GetTokensAtJob* pJob = new GetTokensAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }

        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetTokensAt( m_TranslId, m_Filename, m_Location, *m_pResults);
        }

        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }

        const wxStringVec& GetResults()
        {
            return *m_pResults;
        }
    protected:
        GetTokensAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId,
                        wxMutex* pMutex, wxCondition* pCond,
                        wxStringVec* pResults ):
            SyncJob(GetTokensAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Location(location),
            m_TranslId(translId),
            m_pResults(pResults) {}
        wxString m_Filename;
        ClTokenPosition m_Location;
        ClTranslUnitId m_TranslId;
        wxStringVec* m_pResults;
    };

    /* final */
    class GetCallTipsAtJob : public SyncJob
    {
    public:
        GetCallTipsAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId, const wxString& tokenStr ):
            SyncJob( GetCallTipsAtType, evtType, evtId),
            m_Filename(filename),
            m_Location(location),
            m_TranslId(translId),
            m_TokenStr(tokenStr),
            m_pResults(new std::vector<wxStringVec>())
        {
        }
        ClangJob* Clone() const
        {
            GetCallTipsAtJob* pJob = new GetCallTipsAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetCallTipsAt( m_TranslId, m_Filename, m_Location, m_TokenStr, *m_pResults);
        }

        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }

        const std::vector<wxStringVec>& GetResults()
        {
            return *m_pResults;
        }
    protected:
        GetCallTipsAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId, const wxString& tokenStr,
                          wxMutex* pMutex, wxCondition* pCond,
                          std::vector<wxStringVec>* pResults ):
            SyncJob( GetCallTipsAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Location(location),
            m_TranslId(translId),
            m_TokenStr(tokenStr.c_str()),
            m_pResults(pResults) {}
        wxString m_Filename;
        ClTokenPosition m_Location;
        ClTranslUnitId m_TranslId;
        wxString m_TokenStr;
        std::vector<wxStringVec>* m_pResults;
    };

    /* final */
    class GetOccurrencesOfJob : public SyncJob
    {
    public:
        GetOccurrencesOfJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, ClTranslUnitId translId ):
            SyncJob( GetOccurrencesOfType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Location(location)
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
            clangproxy.GetOccurrencesOf( m_TranslId, m_Filename, m_Location, *m_pResults);
        }

        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }

        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetLocation() const
        {
            return m_Location;
        }
        const std::vector< std::pair<int, int> >& GetResults() const
        {
            return *m_pResults;
        }
    protected:
        GetOccurrencesOfJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId,
                             wxMutex* pMutex, wxCondition* pCond,
                             std::vector< std::pair<int, int> >* pResults ):
            SyncJob(GetOccurrencesOfType, evtType, evtId, pMutex, pCond),
            m_TranslId(translId),
            m_Filename(filename.c_str()),
            m_Location(location),
            m_pResults(pResults) {}
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Location;
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
    ClangProxy( wxEvtHandler* pEvtHandler, ClTokenDatabase& database, const std::vector<wxString>& cppKeywords);
    ~ClangProxy();

    /** Append a job to the end of the queue */
    void AppendPendingJob( ClangProxy::ClangJob& job );
    //void PrependPendingJob( ClangProxy::ClangJob& job );

    ClTranslUnitId GetTranslationUnitId(ClTranslUnitId CtxTranslUnitId, ClFileId fId);
    ClTranslUnitId GetTranslationUnitId(ClTranslUnitId CtxTranslUnitId, const wxString& filename);

protected: // jobs that are run only on the thread
    void CreateTranslationUnit(const wxString& filename, const wxString& compileCommand,  const std::map<wxString, wxString>& unsavedFiles, ClTranslUnitId& out_TranslId);
    void RemoveTranslationUnit(ClTranslUnitId TranslUnitId);
    /** Reparse translation id
     *
     * @param unsavedFiles reference to the unsaved files data. This function takes the data and this list will be empty after this call
     */
    void Reparse(         ClTranslUnitId translId, const wxString& compileCommand, const std::map<wxString, wxString>& unsavedFiles);

    /** Update token database with all tokens from the passed translation unit id
     * @param translId The ID of the intended translation unit
     */
    void UpdateTokenDatabase( ClTranslUnitId translId );
    void GetDiagnostics(  ClTranslUnitId translId, const wxString& filename, std::vector<ClDiagnostic>& diagnostics);
    void CodeCompleteAt(  ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location,
                          bool isAuto, const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results, std::vector<ClDiagnostic>& diagnostics);
    wxString DocumentCCToken( ClTranslUnitId translId, int tknId );
    void GetTokensAt(     ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location, std::vector<wxString>& results);
    void GetCallTipsAt(   ClTranslUnitId translId,const wxString& filename, const ClTokenPosition& location,
                          const wxString& tokenStr, std::vector<wxStringVec>& results);
    void GetOccurrencesOf(ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location,
                          std::vector< std::pair<int, int> >& results);
    void RefineTokenType( ClTranslUnitId translId, int tknId, int& tknType); // TODO: cache TokenId (if resolved) for DocumentCCToken()

public:
    wxString GetCCInsertSuffix( ClTranslUnitId translId, int tknId, const wxString& newLine, std::vector< std::pair<int, int> >& offsets );
    bool ResolveDeclTokenAt( const ClTranslUnitId translId, wxString& filename, ClTokenPosition& out_location);
    bool ResolveDefinitionTokenAt( const ClTranslUnitId translUnitId, wxString& filename, ClTokenPosition& inout_location);

    void GetFunctionScopeAt( ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location, wxString &out_ClassName, wxString &out_FunctionName );
    std::vector<std::pair<wxString, wxString> > GetFunctionScopes( ClTranslUnitId, const wxString& filename );

private:
    mutable wxMutex m_Mutex;
    ClTokenDatabase& m_Database;
    const std::vector<wxString>& m_CppKeywords;
    std::vector<ClTranslationUnit> m_TranslUnits;
    CXIndex m_ClIndex[2];
private: // Thread
    wxEvtHandler* m_pEventCallbackHandler;
    BackgroundThread* m_pThread;
    BackgroundThread* m_pDiagnosticThread;
    //BackgroundThread* m_pParsingThread;
};

#endif // CLANGPROXY_H
