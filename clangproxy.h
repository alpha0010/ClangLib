#ifndef CLANGPROXY_H
#define CLANGPROXY_H

#include <map>
#include <vector>
#include <wx/string.h>

class TranslationUnit;
class TokenDatabase;
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
        ClangProxy(TokenDatabase& database, const std::vector<wxString>& cppKeywords);
        ~ClangProxy();

        void CreateTranslationUnit(const wxString& filename, const wxString& commands);
        int GetTranslationUnitId(FileId fId);
        int GetTranslationUnitId(const wxString& filename);

        void CodeCompleteAt(bool isAuto, const wxString& filename, int line, int column, int translId,
                            const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results);
        wxString DocumentCCToken(int translId, int tknId);
        wxString GetCCInsertSuffix(int translId, int tknId, const wxString& newLine, std::pair<int, int>& offsets);
        void RefineTokenType(int translId, int tknId, int& tknType); // TODO: cache TokenId (if resolved) for DocumentCCToken()

        void GetCallTipsAt(const wxString& filename, int line, int column, int translId,
                           const wxString& tokenStr, std::vector<wxStringVec>& results);

        void GetTokensAt(const wxString& filename, int line, int column, int translId, std::vector<wxString>& results);
        void GetOccurrencesOf(const wxString& filename, int line, int column,
                              int translId, std::vector< std::pair<int, int> >& results);
        void ResolveTokenAt(wxString& filename, int& line, int& column, int translId);

        void Reparse(int translId, const std::map<wxString, wxString>& unsavedFiles);

        void GetDiagnostics(int translId, std::vector<ClDiagnostic>& diagnostics);

    private:
        TokenDatabase& m_Database;
        const std::vector<wxString>& m_CppKeywords;
        std::vector<TranslationUnit> m_TranslUnits;
        CXIndex m_ClIndex;
};

#endif // CLANGPROXY_H
