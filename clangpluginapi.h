#ifndef __CLANGPLUGINAPI_H
#define __CLANGPLUGINAPI_H

#include <cbplugin.h>


typedef int ClTranslUnitId;
typedef int ClTokenId;

enum ClTokenCategory
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
    tcMacroUse,         tcMacroPrivate,
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

struct ClTokenPosition{
    ClTokenPosition(unsigned int ln, unsigned int col){line = ln; column = col;}
    unsigned int line;
    unsigned int column;
};

enum ClDiagnosticLevel { dlMinimal, dlFull };

enum ClSeverity { sWarning, sError };

struct ClDiagnostic
{
    ClDiagnostic(int ln, int rgStart, int rgEnd, ClSeverity level, const wxString& fl, const wxString& msg) :
        line(ln), range(rgStart, rgEnd), severity(level), file(fl), message(msg) {}

    int line;
    std::pair<int, int> range;
    ClSeverity severity;
    wxString file;
    wxString message;
};

class ClangEvent : public wxCommandEvent{
public:
    ClangEvent( wxEventType evtId, const ClTranslUnitId id, const wxString& filename ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(0,0){}
    ClangEvent( wxEventType evtId, const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& pos, const std::vector< std::pair<int, int> >& occurrences ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(pos),
        m_GetOccurrencesResults(occurrences){}
    ClangEvent( wxEventType evtId, const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& pos, const std::vector<ClToken>& completions ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(pos),
        m_GetCodeCompletionResults(completions){}
    ClangEvent( wxEventType evtId, const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& loc, const std::vector<ClDiagnostic>& diag ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(loc),
        m_DiagnosticResults(diag){}
    ClangEvent( wxEventType evtId, const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& loc, const wxString& documentation ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(loc),
        m_DocumentationResults(documentation){}
    ClangEvent( const ClangEvent& other) :
        wxCommandEvent(other),
        m_TranslationUnitId(other.m_TranslationUnitId),
        m_Filename(other.m_Filename),
        m_Location(other.m_Location),
        m_GetOccurrencesResults(other.m_GetOccurrencesResults),
        m_GetCodeCompletionResults(other.m_GetCodeCompletionResults),
        m_DiagnosticResults(other.m_DiagnosticResults),
        m_DocumentationResults(other.m_DocumentationResults){}
    virtual ~ClangEvent(){}
    virtual wxEvent *Clone() const { return new ClangEvent(*this); }

    ClTranslUnitId GetTranslationUnitId() const { return m_TranslationUnitId; }
    const ClTokenPosition& GetLocation() const { return m_Location; }
    const std::vector< std::pair<int, int> >& GetOccurrencesResults(){ return m_GetOccurrencesResults; }
    const std::vector<ClToken>& GetCodeCompletionResults(){ return m_GetCodeCompletionResults; }
    const std::vector<ClDiagnostic>& GetDiagnosticResults(){ return m_DiagnosticResults; }
    const wxString GetDocumentationResults() { return m_DocumentationResults; }
private:
    ClTranslUnitId m_TranslationUnitId;
    wxString m_Filename;
    ClTokenPosition m_Location;
    std::vector< std::pair<int, int> > m_GetOccurrencesResults;
    std::vector<ClToken> m_GetCodeCompletionResults;
    std::vector<ClDiagnostic> m_DiagnosticResults;
    wxString m_DocumentationResults;
};

extern const wxEventType clEVT_TRANSLATIONUNIT_CREATED;
extern const wxEventType clEVT_REPARSE_FINISHED;
extern const wxEventType clEVT_GETCODECOMPLETE_FINISHED;
extern const wxEventType clEVT_GETOCCURRENCES_FINISHED;
extern const wxEventType clEVT_GETDOCUMENTATION_FINISHED;
extern const wxEventType clEVT_DIAGNOSTICS_UPDATED;

/* interface */
class IClangPlugin
{
public:
    virtual bool IsProviderFor(cbEditor* ed) = 0;
    virtual ClTranslUnitId GetTranslationUnitId( const wxString& filename ) = 0;
    virtual const wxImageList& GetImageList( const ClTranslUnitId id ) = 0;
    virtual const wxStringVec& GetKeywords( const ClTranslUnitId id ) = 0;
    /* Events  */
    virtual void RegisterEventSink( wxEventType, IEventFunctorBase<ClangEvent>* functor) = 0;
    virtual void RemoveAllEventSinksFor(void* owner) = 0;

    /** Request reparsing of a Translation unit */
    virtual void RequestReparse(const ClTranslUnitId id, const wxString& filename) = 0;
    /** Retrieve unction scope */
    virtual std::pair<wxString,wxString>    GetFunctionScopeAt( const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& location ) = 0;
    virtual ClTokenPosition                 GetFunctionScopeLocation( const ClTranslUnitId id, const wxString& filename, const wxString& scope, const wxString& functioname) = 0;
    virtual void                            GetFunctionScopes( const ClTranslUnitId, const wxString& filename, std::vector<std::pair<wxString, wxString> >& out_scopes  ) = 0;
    /** Occurrences highlighting */
    virtual wxCondError                     GetOccurrencesOf( const ClTranslUnitId, const wxString& filename, const ClTokenPosition& loc, unsigned long timeout, std::vector< std::pair<int, int> >& out_occurrences ) = 0;
    /* Code completion */
    virtual wxCondError                     GetCodeCompletionAt( const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& loc, unsigned long timeout, std::vector<ClToken>& out_tknResults) = 0;
    virtual wxString                        GetCodeCompletionTokenDocumentation( const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& location, ClTokenId tokenId ) = 0;
};

/* abstract */
class ClangPluginComponent : public wxEvtHandler
{
public:
    ClangPluginComponent(){}
    virtual void OnAttach( IClangPlugin *pClangPlugin ){ m_pClangPlugin = pClangPlugin; }
    virtual void OnRelease( IClangPlugin */*pClangPlugin*/ ){ m_pClangPlugin = NULL; }
    virtual bool IsAttached(){ return m_pClangPlugin != NULL;}
    virtual bool BuildToolBar(wxToolBar* /*toolBar*/){ return false; }
        // Does this plugin handle code completion for the editor ed?
    virtual cbCodeCompletionPlugin::CCProviderStatus GetProviderStatusFor(cbEditor* /*ed*/){ return cbCodeCompletionPlugin::ccpsInactive;}
        // Request code completion
    virtual std::vector<cbCodeCompletionPlugin::CCToken> GetAutocompList(bool /*isAuto*/, cbEditor* /*ed*/, int& /*tknStart*/, int& /*tknEnd*/) { return std::vector<cbCodeCompletionPlugin::CCToken>(); }
    virtual wxString GetDocumentation( const cbCodeCompletionPlugin::CCToken& /*token*/ ){ return wxEmptyString; }

protected:
    IClangPlugin* m_pClangPlugin;
};

#endif // __CLANGPLUGINAPI_H
