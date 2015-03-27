/*
 * Wrapper class around CXTranslationUnit
 */

#include <sdk.h>

#include "translationunit.h"

#ifndef CB_PRECOMP
    #include <cbexception.h> // for cbThrow()

    #include <algorithm>
#endif // CB_PRECOMP

#include "tokendatabase.h"

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack,
                               unsigned include_len, CXClientData client_data);

static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor parent, CXClientData client_data);

TranslationUnit::TranslationUnit(const wxString& filename, const std::vector<const char*>& args,
                                 CXIndex clIndex, TokenDatabase* database) :
    m_LastCC(nullptr),
    m_LastPos(-1, -1)
{
    // TODO: check and handle error conditions
    m_ClTranslUnit = clang_parseTranslationUnit( clIndex, filename.ToUTF8().data(), args.empty() ? nullptr : &args[0],
                                                 args.size(), nullptr, 0,
                                                   clang_defaultEditingTranslationUnitOptions()
                                                 | CXTranslationUnit_IncludeBriefCommentsInCodeCompletion
                                                 | CXTranslationUnit_DetailedPreprocessingRecord );
    std::pair<TranslationUnit*, TokenDatabase*> visitorData = std::make_pair(this, database);
    clang_getInclusions(m_ClTranslUnit, ClInclusionVisitor, &visitorData);
    m_Files.reserve(1024);
    m_Files.push_back(database->GetFilenameId(filename));
    std::sort(m_Files.begin(), m_Files.end());
    std::unique(m_Files.begin(), m_Files.end());
#if __cplusplus >= 201103L
    m_Files.shrink_to_fit();
#else
    std::vector<FileId>(m_Files).swap(m_Files);
#endif
    Reparse(0, nullptr); // seems to improve performance for some reason?

    clang_visitChildren(clang_getTranslationUnitCursor(m_ClTranslUnit), ClAST_Visitor, database);
    database->Shrink();
}

#if __cplusplus >= 201103L
TranslationUnit::TranslationUnit(TranslationUnit&& other) :
    m_Files(std::move(other.m_Files)),
    m_ClTranslUnit(other.m_ClTranslUnit),
    m_LastCC(nullptr),
    m_LastPos(-1, -1)
{
     other.m_ClTranslUnit = nullptr;
}

TranslationUnit::TranslationUnit(const TranslationUnit& WXUNUSED(other))
{
    cbThrow(wxT("Illegal copy attempted of TranslationUnit object."));
}
#else
TranslationUnit::TranslationUnit(const TranslationUnit& other) :
    m_ClTranslUnit(other.m_ClTranslUnit),
    m_LastCC(nullptr),
    m_LastPos(-1, -1)
{
    m_Files.swap(const_cast<TranslationUnit&>(other).m_Files);
    const_cast<TranslationUnit&>(other).m_ClTranslUnit = nullptr;
}
#endif

TranslationUnit::~TranslationUnit()
{
    if (m_LastCC)
        clang_disposeCodeCompleteResults(m_LastCC);
    if (m_ClTranslUnit)
        clang_disposeTranslationUnit(m_ClTranslUnit);
}

void TranslationUnit::AddInclude(FileId fId)
{
    m_Files.push_back(fId);
}

bool TranslationUnit::Contains(FileId fId)
{
    //return std::binary_search(m_Files.begin(), m_Files.begin() + std::min(fId + 1, m_Files.size()), fId);
    return std::binary_search(m_Files.begin(), m_Files.end(), fId);
}

CXCodeCompleteResults* TranslationUnit::CodeCompleteAt( const char* complete_filename, unsigned complete_line,
                                       unsigned complete_column, struct CXUnsavedFile* unsaved_files,
                                       unsigned num_unsaved_files )
{
    if (m_LastPos.Equals(complete_line, complete_column))
        return m_LastCC;
    if (m_LastCC)
        clang_disposeCodeCompleteResults(m_LastCC);
    m_LastCC = clang_codeCompleteAt(m_ClTranslUnit, complete_filename, complete_line, complete_column,
                                    unsaved_files, num_unsaved_files,
                                      clang_defaultCodeCompleteOptions()
                                    | CXCodeComplete_IncludeCodePatterns
                                    | CXCodeComplete_IncludeBriefComments);
    m_LastPos.Set(complete_line, complete_column);
    return m_LastCC;
}

const CXCompletionResult* TranslationUnit::GetCCResult(unsigned index)
{
    if (m_LastCC && index < m_LastCC->NumResults)
        return m_LastCC->Results + index;
    return nullptr;
}

CXCursor TranslationUnit::GetTokensAt(const wxString& filename, int line, int column)
{
    return clang_getCursor(m_ClTranslUnit, clang_getLocation(m_ClTranslUnit, GetFileHandle(filename), line, column));
}

void TranslationUnit::Reparse(unsigned num_unsaved_files, struct CXUnsavedFile* unsaved_files)
{
    // TODO: check and handle error conditions
    clang_reparseTranslationUnit(m_ClTranslUnit, num_unsaved_files,
                                 unsaved_files, clang_defaultReparseOptions(m_ClTranslUnit));
}

void TranslationUnit::GetDiagnostics(std::vector<ClDiagnostic>& diagnostics)
{
    CXDiagnosticSet diagSet = clang_getDiagnosticSetFromTU(m_ClTranslUnit);
    ExpandDiagnosticSet(diagSet, diagnostics);
    clang_disposeDiagnosticSet(diagSet);
}

CXFile TranslationUnit::GetFileHandle(const wxString& filename) const
{
    return clang_getFile(m_ClTranslUnit, filename.ToUTF8().data());
}

static void RangeToColumns(CXSourceRange range, unsigned& rgStart, unsigned& rgEnd)
{
    CXSourceLocation rgLoc = clang_getRangeStart(range);
    clang_getSpellingLocation(rgLoc, nullptr, nullptr, &rgStart, nullptr);
    rgLoc = clang_getRangeEnd(range);
    clang_getSpellingLocation(rgLoc, nullptr, nullptr, &rgEnd, nullptr);
}

void TranslationUnit::ExpandDiagnosticSet(CXDiagnosticSet diagSet, std::vector<ClDiagnostic>& diagnostics)
{
    size_t numDiags = clang_getNumDiagnosticsInSet(diagSet);
    for (size_t i = 0; i < numDiags; ++i)
    {
        CXDiagnostic diag = clang_getDiagnosticInSet(diagSet, i);
        //ExpandDiagnosticSet(clang_getChildDiagnostics(diag), diagnostics);
        size_t numRnges = clang_getDiagnosticNumRanges(diag);
        unsigned rgStart = 0;
        unsigned rgEnd = 0;
        for (size_t j = 0; j < numRnges; ++j) // often no range data (clang bug?)
        {
            RangeToColumns(clang_getDiagnosticRange(diag, j), rgStart, rgEnd);
            if(rgStart != rgEnd)
                break;
        }
        if (rgStart == rgEnd) // check if there is FixIt data for the range
        {
            numRnges = clang_getDiagnosticNumFixIts(diag);
            for (size_t j = 0; j < numRnges; ++j)
            {
                CXSourceRange range;
                clang_getDiagnosticFixIt(diag, j, &range);
                RangeToColumns(range, rgStart, rgEnd);
                if (rgStart != rgEnd)
                    break;
            }
        }
        CXSourceLocation loc = clang_getDiagnosticLocation(diag);
        if (rgEnd == 0) // still no range -> use the range of the current token
        {
            CXCursor token = clang_getCursor(m_ClTranslUnit, loc);
            RangeToColumns(clang_getCursorExtent(token), rgStart, rgEnd);
        }
        unsigned line;
        unsigned column;
        CXFile file;
        clang_getSpellingLocation(loc, &file, &line, &column, nullptr);
        if (rgEnd < column || rgStart > column) // out of bounds?
            rgStart = rgEnd = column;
        CXString str = clang_getFileName(file);
        wxString flName = wxString::FromUTF8(clang_getCString(str));
        clang_disposeString(str);
        str = clang_formatDiagnostic(diag, 0);
        diagnostics.push_back(ClDiagnostic( line, rgStart, rgEnd,
                                            clang_getDiagnosticSeverity(diag) >= CXDiagnostic_Error ? sError : sWarning,
                                            flName, wxString::FromUTF8(clang_getCString(str)) ));
        clang_disposeString(str);
        clang_disposeDiagnostic(diag);
    }
}

unsigned HashToken(CXCompletionString token, wxString& identifier)
{
    unsigned hVal = 2166136261u;
    size_t upperBound = clang_getNumCompletionChunks(token);
    for (size_t i = 0; i < upperBound; ++i)
    {
        CXString str = clang_getCompletionChunkText(token, i);
        const char* pCh = clang_getCString(str);
        if (clang_getCompletionChunkKind(token, i) == CXCompletionChunk_TypedText)
            identifier = wxString::FromUTF8(*pCh =='~' ? pCh + 1 : pCh);
        for (; *pCh; ++pCh)
        {
            hVal ^= *pCh;
            hVal *= 16777619u;
        }
        clang_disposeString(str);
    }
    return hVal;
}

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* WXUNUSED(inclusion_stack),
                               unsigned WXUNUSED(include_len), CXClientData client_data)
{
    CXString filename = clang_getFileName(included_file);
    wxFileName inclFile(wxString::FromUTF8(clang_getCString(filename)));
    if (inclFile.MakeAbsolute())
    {
        std::pair<TranslationUnit*, TokenDatabase*>* clTranslUnit
            = static_cast<std::pair<TranslationUnit*, TokenDatabase*>*>(client_data);
        clTranslUnit->first->AddInclude(clTranslUnit->second->GetFilenameId(inclFile.GetFullPath()));
    }
    clang_disposeString(filename);
}

static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor WXUNUSED(parent), CXClientData client_data)
{
    CXChildVisitResult ret = CXChildVisit_Break; // should never happen
    switch (cursor.kind)
    {
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
        case CXCursor_ClassDecl:
        case CXCursor_EnumDecl:
        case CXCursor_Namespace:
        case CXCursor_ClassTemplate:
            ret = CXChildVisit_Recurse;
            break;

        case CXCursor_FieldDecl:
        case CXCursor_EnumConstantDecl:
        case CXCursor_FunctionDecl:
        case CXCursor_VarDecl:
        case CXCursor_ParmDecl:
        case CXCursor_TypedefDecl:
        case CXCursor_CXXMethod:
        case CXCursor_Constructor:
        case CXCursor_Destructor:
        case CXCursor_FunctionTemplate:
        //case CXCursor_MacroDefinition: // this can crash Clang on Windows
            ret = CXChildVisit_Continue;
            break;

        default:
            return CXChildVisit_Recurse;
    }

    CXSourceLocation loc = clang_getCursorLocation(cursor);
    CXFile clFile;
    unsigned line, col;
    clang_getSpellingLocation(loc, &clFile, &line, &col, nullptr);
    CXString str = clang_getFileName(clFile);
    wxString filename = wxString::FromUTF8(clang_getCString(str));
    clang_disposeString(str);
    if (filename.IsEmpty())
        return ret;

    CXCompletionString token = clang_getCursorCompletionString(cursor);
    wxString identifier;
    unsigned tokenHash = HashToken(token, identifier);
    if (!identifier.IsEmpty())
    {
        TokenDatabase* database = static_cast<TokenDatabase*>(client_data);
        database->InsertToken(identifier, AbstractToken(database->GetFilenameId(filename), line, col, tokenHash));
    }
    return ret;
}
