#ifndef CLANGPROXY_H
#define CLANGPROXY_H

#include <map>
#include <vector>
#include <wx/string.h>

class TranslationUnit;
typedef void* CXIndex;

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
    tcPreprocessor,
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
    tcMacro,            tcMacroPrivate,
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
        ClangProxy();
        ~ClangProxy();

        void CreateTranslationUnit(const wxString& filename, const wxString& commands);
        int GetTranslationUnitId(const wxString& filename);

        void CodeCompleteAt(const wxString& filename, int line, int column, int translId, const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results);
        wxString DocumentCCToken(int translId, int tknId);
        wxString GetCCInsertSuffix(int translId, int tknId, const wxString& newLine, std::pair<int, int>& offsets);

    protected:
    private:
        std::vector<TranslationUnit> m_TranslUnits;
        CXIndex m_ClIndex;
};

#endif // CLANGPROXY_H
