/*
 * Communication proxy to libclang-c
 */

#include "clangproxy.h"

#include <sdk.h>

#include <clang-c/Index.h>
#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
    #include <cbexception.h> // for cbThrow()

    #include <algorithm>
#endif // CB_PRECOMP

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack, unsigned include_len, CXClientData client_data);

class TranslationUnit
{
    public:
        TranslationUnit(const wxString& filename, const std::vector<const char*>& args, CXIndex clIndex) :
            m_LastCC(nullptr),
            m_LastPos(-1, -1)
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
            m_ClTranslUnit(other.m_ClTranslUnit),
            m_LastCC(nullptr),
            m_LastPos(-1, -1)
        {
             other.m_ClTranslUnit = nullptr;
        }
#else
        TranslationUnit(const TranslationUnit& other) :
            m_Files(other.m_Files),
            m_ClTranslUnit(other.m_ClTranslUnit),
            m_LastCC(nullptr),
            m_LastPos(-1, -1)
        {
             const_cast<TranslationUnit&>(other).m_ClTranslUnit = nullptr;
        }
#endif

        ~TranslationUnit()
        {
            if (m_LastCC)
                clang_disposeCodeCompleteResults(m_LastCC);
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

        // note that complete_line and complete_column are 1 index, not 0 index!
        CXCodeCompleteResults* CodeCompleteAt( const char* complete_filename, unsigned complete_line,
                                               unsigned complete_column, struct CXUnsavedFile* unsaved_files,
                                               unsigned num_unsaved_files )
        {
            if (m_LastPos.Equals(complete_line, complete_column))
                return m_LastCC;
            if (m_LastCC)
                clang_disposeCodeCompleteResults(m_LastCC);
            m_LastCC = clang_codeCompleteAt(m_ClTranslUnit, complete_filename, complete_line, complete_column, unsaved_files,
                                            num_unsaved_files, clang_defaultCodeCompleteOptions() | CXCodeComplete_IncludeCodePatterns | CXCodeComplete_IncludeBriefComments);
            m_LastPos.Set(complete_line, complete_column);
            return m_LastCC;
        }

        const CXCompletionResult* GetCCResult(unsigned index)
        {
            if (m_LastCC && index < m_LastCC->NumResults)
                return m_LastCC->Results + index;
            return nullptr;
        }

    private:
#if __cplusplus >= 201103L
        // copying not allowed (we can move)
        TranslationUnit(const TranslationUnit& /*other*/) { cbThrow(wxT("Illegal copy attempted of TranslationUnit object.")); }
#endif

        std::vector<wxString> m_Files;
        CXTranslationUnit m_ClTranslUnit;
        CXCodeCompleteResults* m_LastCC;

        struct FilePos
        {
            FilePos(unsigned ln, unsigned col) :
                line(ln), column(col) {}

            void Set(unsigned ln, unsigned col)
            {
                line   = ln;
                column = col;
            }

            bool Equals(unsigned ln, unsigned col)
            {
                return (line == ln && column == col);
            }

            unsigned line;
            unsigned column;
        } m_LastPos;
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
        if (CXAvailability_Available != clang_getCompletionAvailability(token.CompletionString))
            continue;
        const int numChunks = clang_getNumCompletionChunks(token.CompletionString);
        wxString type;
        for (int j = 0; j < numChunks; ++j)
        {
            CXCompletionChunkKind kind = clang_getCompletionChunkKind(token.CompletionString, j);
            if (kind == CXCompletionChunk_ResultType)
            {
                CXString str = clang_getCompletionChunkText(token.CompletionString, j);
                type = wxT(": ") + wxString::FromUTF8(clang_getCString(str));
                wxString prefix;
                if (type.EndsWith(wxT(" *"), &prefix) || type.EndsWith(wxT(" &"), &prefix))
                    type = prefix + type.Last();
                clang_disposeString(str);
            }
            else if (kind == CXCompletionChunk_TypedText)
            {
                CXString completeTxt = clang_getCompletionChunkText(token.CompletionString, j);
                results.push_back(ClToken(wxString::FromUTF8(clang_getCString(completeTxt)) + type,// + F(wxT("%d"), token.CursorKind),
                                          i, clang_getCompletionPriority(token.CompletionString), GetTokenCategory(token.CursorKind)));
                clang_disposeString(completeTxt);
                type.Empty();
                break;
            }
        }
    }
}

wxString ClangProxy::DocumentCCToken(int translId, int tknId)
{
    const CXCompletionResult* token = m_TranslUnits[translId].GetCCResult(tknId);
    if (!token)
        return wxEmptyString;
    int upperBound = clang_getNumCompletionChunks(token->CompletionString);
    wxString doc;
    for (int i = 0; i < upperBound; ++i)
    {
        CXString str = clang_getCompletionChunkText(token->CompletionString, i);
        doc += wxString::FromUTF8(clang_getCString(str));
        if (   clang_getCompletionChunkKind(token->CompletionString, i) == CXCompletionChunk_ResultType
            && (wxIsalpha(doc.Last()) || doc.Last() == wxT('_')) )
            doc += wxT(" ");
        clang_disposeString(str);
    }

    CXString comment = clang_getCompletionBriefComment(token->CompletionString);
    doc += wxT("\n") + wxString::FromUTF8(clang_getCString(comment));
    clang_disposeString(comment);

    wxString html = wxT("<html><body>");
    html.reserve(doc.Len() + 30);
    for (size_t i = 0; i < doc.Length(); ++i)
    {
        switch (doc.GetChar(i))
        {
            case wxT('&'):  html += wxT("&amp;");  break;
            case wxT('\"'): html += wxT("&quot;"); break;
            case wxT('\''): html += wxT("&apos;"); break;
            case wxT('<'):  html += wxT("&lt;");   break;
            case wxT('>'):  html += wxT("&gt;");   break;
            case wxT('\n'): html += wxT("<br>");   break;
            default:        html += doc[i];        break;
        }
    }
    html += wxT("</body></html>");
    return html;
}

wxString ClangProxy::GetCCInsertSuffix(int translId, int tknId, const wxString& newLine, std::pair<int, int>& offsets)
{
    const CXCompletionResult* token = m_TranslUnits[translId].GetCCResult(tknId);
    if (!token)
        return wxEmptyString;

    const CXCompletionString& clCompStr = token->CompletionString;
    int upperBound = clang_getNumCompletionChunks(clCompStr);
    enum BuilderState { init, store, exit };
    BuilderState state = init;
    wxString suffix;
    for (int i = 0; i < upperBound; ++i)
    {
        switch (clang_getCompletionChunkKind(clCompStr, i))
        {
            case CXCompletionChunk_TypedText:
                if (state == init)
                    state = store;
                break;

            case CXCompletionChunk_Placeholder:
                if (state == store)
                {
                    CXString str = clang_getCompletionChunkText(clCompStr, i);
                    const wxString& param = wxT("/*! ") + wxString::FromUTF8(clang_getCString(str)) + wxT(" !*/");
                    offsets = std::make_pair(suffix.Length(), suffix.Length() + param.Length());
                    suffix += param;
                    clang_disposeString(str);
                    state = exit;
                }
                break;

            case CXCompletionChunk_Informative:
                break;

            case CXCompletionChunk_VerticalSpace:
                if (state != init)
                    suffix += newLine;
                break;

            default:
                if (state != init)
                {
                    CXString str = clang_getCompletionChunkText(clCompStr, i);
                    suffix += wxString::FromUTF8(clang_getCString(str));
                    clang_disposeString(str);
                }
                break;
        }
    }
    if (state != exit)
        offsets = std::make_pair(suffix.Length(), suffix.Length());
    return suffix;
}
