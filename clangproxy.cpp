/*
 * Communication proxy to libclang-c
 */

#include "clangproxy.h"

#include <sdk.h>

#include <clang-c/Index.h>
#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
    #include <algorithm>
    #include <cbexception.h> // for cbThrow()
#endif // CB_PRECOMP

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack, unsigned include_len, CXClientData client_data);

class TranslationUnit
{
    public:
        TranslationUnit(const wxString& filename, const std::vector<const char*>& args, CXIndex clIndex)
        {
            m_ClTranslUnit = clang_parseTranslationUnit( clIndex, filename.ToUTF8().data(), args.empty() ? nullptr : &args[0],
                                                         args.size(), nullptr, 0, clang_defaultEditingTranslationUnitOptions() );

            clang_getInclusions(m_ClTranslUnit, ClInclusionVisitor, this);
            m_Files.reserve(1024);
            m_Files.push_back(filename);
            std::sort(m_Files.begin(), m_Files.end());
            std::unique(m_Files.begin(), m_Files.end());
#if __cplusplus >= 201103L
            m_Files.shrink_to_fit();
#else
            std::vector<wxString>(m_Files).swap(m_Files);
#endif
        }

        // move ctor
#if __cplusplus >= 201103L
        TranslationUnit(TranslationUnit&& other) :
            m_Files(std::move(other.m_Files)),
            m_ClTranslUnit(other.m_ClTranslUnit)
        {
             other.m_ClTranslUnit = nullptr;
        }
#else
        TranslationUnit(const TranslationUnit& other) :
            m_Files(other.m_Files),
            m_ClTranslUnit(other.m_ClTranslUnit)
        {
             const_cast<TranslationUnit&>(other).m_ClTranslUnit = nullptr;
        }
#endif

        ~TranslationUnit()
        {
            if (m_ClTranslUnit)
                clang_disposeTranslationUnit(m_ClTranslUnit);
        }

        void AddInclude(const wxString& filename)
        {
            m_Files.push_back(filename);
        }

        bool Contains(const wxString& filename)
        {
            return std::binary_search(m_Files.begin(), m_Files.end(), filename);
        }

        // call clang_disposeCodeCompleteResults() later to free memory (if not null)
        // note that complete_line and complete_column are 1 index, not 0 index!
        CXCodeCompleteResults* CodeCompleteAt( const char* complete_filename, unsigned complete_line,
                                               unsigned complete_column, struct CXUnsavedFile* unsaved_files,
                                               unsigned num_unsaved_files )
        {
            return clang_codeCompleteAt(m_ClTranslUnit, complete_filename, complete_line, complete_column, unsaved_files,
                                        num_unsaved_files, clang_defaultCodeCompleteOptions() | CXCodeComplete_IncludeCodePatterns);
        }

    private:
#if __cplusplus >= 201103L
        // copying not allowed (we can move)
        TranslationUnit(const TranslationUnit& /*other*/) { cbThrow(wxT("Illegal copy attempted of TranslationUnit object.")); }
#endif

        std::vector<wxString> m_Files;
        CXTranslationUnit m_ClTranslUnit;
};

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack, unsigned include_len, CXClientData client_data)
{
    TranslationUnit* clTranslUnit = static_cast<TranslationUnit*>(client_data);
    CXString filename = clang_getFileName(included_file);
    wxFileName inclFile(wxString::FromUTF8(clang_getCString(filename)));
    if (inclFile.MakeAbsolute())
        clTranslUnit->AddInclude(inclFile.GetFullPath());
    clang_disposeString(filename);
}

ClangProxy::ClangProxy()
{
    m_ClIndex = clang_createIndex(0, 0);
}

ClangProxy::~ClangProxy()
{
    clang_disposeIndex(m_ClIndex);
}

void ClangProxy::CreateTranslationUnit(const wxString& filename, const wxString& commands)
{
    wxStringTokenizer tokenizer(commands);
    std::vector<wxCharBuffer> argsBuffer;
    std::vector<const char*> args;
    while (tokenizer.HasMoreTokens())
    {
        argsBuffer.push_back(tokenizer.GetNextToken().ToUTF8());
        args.push_back(argsBuffer.back().data());
    }
    m_TranslUnits.push_back(TranslationUnit(filename, args, m_ClIndex));
}

int ClangProxy::GetTranslationUnitId(const wxString& filename)
{
    for (size_t i = 0; i < m_TranslUnits.size(); ++i)
    {
        if (m_TranslUnits[i].Contains(filename))
            return i;
    }
    return wxNOT_FOUND;
}

static TokenCategory GetTokenCategory(CXCursorKind kind)
{
    switch (kind)
    {
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
            return tcClassPublic;

        case CXCursor_Constructor:
            return tcCtorPublic;

        case CXCursor_Destructor:
            return tcDtorPublic;

        case CXCursor_FunctionDecl:
        case CXCursor_CXXMethod:
        case CXCursor_FunctionTemplate:
            return tcFuncPublic;

        case CXCursor_FieldDecl:
        case CXCursor_VarDecl:
        case CXCursor_ParmDecl:
            return tcVarPublic;

        case CXCursor_MacroDefinition:
            return tcPreprocessor;

        case CXCursor_EnumDecl:
            return tcEnumPublic;

        case CXCursor_EnumConstantDecl:
            return tcEnumerator;

        case CXCursor_Namespace:
            return tcNamespace;

        case CXCursor_TypedefDecl:
            return tcTypedefPublic;

        // TODO: what is this?
//        case:
//            return tcMacroPublic;

        default:
            return tcNone;
    }
}

void ClangProxy::CodeCompleteAt(const wxString& filename, int line, int column, int translId, const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results)
{
    wxCharBuffer chName = filename.ToUTF8();
    std::vector<CXUnsavedFile> clUnsavedFiles;
    std::vector<wxCharBuffer> clFileBuffer;
    for (std::map<wxString, wxString>::const_iterator fileIt = unsavedFiles.begin();
         fileIt != unsavedFiles.end(); ++fileIt)
    {
        CXUnsavedFile unit;
        clFileBuffer.push_back(fileIt->first.ToUTF8());
        unit.Filename = clFileBuffer.back().data();
        clFileBuffer.push_back(fileIt->second.ToUTF8());
        unit.Contents = clFileBuffer.back().data();
#if wxCHECK_VERSION(2, 9, 4)
        unit.Length   = clFileBuffer.back().length();
#else
        unit.Length   = strlen(unit.Contents); // extra work needed because wxString::Length() treats multibyte character length as '1'
#endif
        clUnsavedFiles.push_back(unit);
    }
    CXCodeCompleteResults* clResults = m_TranslUnits[translId].CodeCompleteAt(chName.data(), line, column, clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0], clUnsavedFiles.size());
    if (!clResults)
        return;
    const int numResults = clResults->NumResults;
    results.reserve(numResults);
    for (int i = 0; i < numResults; ++i)
    {
        const CXCompletionResult& token = clResults->Results[i];
        const int numChunks = clang_getNumCompletionChunks(token.CompletionString);
        for (int j = 0; j < numChunks; ++j)
        {
            if (clang_getCompletionChunkKind(token.CompletionString, j) == CXCompletionChunk_TypedText)
            {
                CXString completeTxt = clang_getCompletionChunkText(token.CompletionString, j);
                results.push_back(ClToken(wxString::FromUTF8(clang_getCString(completeTxt)),// + F(wxT("%d"), token.CursorKind),
                                          i, clang_getCompletionPriority(token.CompletionString), GetTokenCategory(token.CursorKind)));
                clang_disposeString(completeTxt);
                break;
            }
        }
    }
    clang_disposeCodeCompleteResults(clResults);
}
