/*
 * Wrapper class around CXTranslationUnit
 */

#include <sdk.h>
#include <iostream>
#include "translationunit.h"

#ifndef CB_PRECOMP
#include <cbexception.h> // for cbThrow()

#include <algorithm>
#endif // CB_PRECOMP

#include "tokendatabase.h"

#if 0
class ClangVisitorContext {
public:
    ClangVisitorContext(ClTranslationUnit* pTranslationUnit) :
        m_pTranslationUnit(pTranslationUnit)
    { }
    std::deque<wxString> m_ScopeStack;
    std::deque<CXCursor> m_CursorSt
};
#endif

struct ClangVisitorContext
{
    ClangVisitorContext( ClTokenDatabase* pDatabase ){ database = pDatabase; tokenCount = 0;}
    ClTokenDatabase* database;
    unsigned long long tokenCount;
};

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack,
        unsigned include_len, CXClientData client_data);

static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor parent, CXClientData client_data);

ClTranslationUnit::ClTranslationUnit( const ClTranslUnitId id, CXIndex clIndex ) :
    m_Id(id),
    m_FileId(-1),
    m_ClIndex(clIndex),
    m_ClTranslUnit(nullptr),
    m_LastCC(nullptr),
    m_LastPos(-1, -1),
    m_Occupied(false)
{
    //fprintf(stdout,"%p %s %d\n",this, __PRETTY_FUNCTION__, m_Id);
}
ClTranslationUnit::ClTranslationUnit( const ClTranslUnitId id ) :
    m_Id(id),
    m_FileId(-1),
    m_ClIndex(nullptr),
    m_ClTranslUnit(nullptr),
    m_LastCC(nullptr),
    m_LastPos(-1, -1),
    m_Occupied(true)
{
    //fprintf(stdout,"%p %s %d\n",this, __PRETTY_FUNCTION__, m_Id);
}


#if __cplusplus >= 201103L
ClTranslationUnit::ClTranslationUnit(ClTranslationUnit&& other) :
    m_Id(other.m_Id),
    m_Files(std::move(other.m_Files)),
    m_ClIndex(other.m_ClIndex),
    m_ClTranslUnit(other.m_ClTranslUnit),
    m_LastCC(nullptr),
    m_LastPos(-1, -1)
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
    other.m_ClTranslUnit = nullptr;
}

ClTranslationUnit::ClTranslationUnit(const ClTranslationUnit& WXUNUSED(other))
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);
    cbThrow(wxT("Illegal copy attempted of TranslationUnit object."));
}
#else
ClTranslationUnit::ClTranslationUnit(const ClTranslationUnit& other) :
    m_Id(other.m_Id),
    m_ClIndex(other.m_ClIndex),
    m_ClTranslUnit(other.m_ClTranslUnit),
    m_LastCC(nullptr),
    m_LastPos(-1, -1)
{
    //fprintf(stdout,"%p %s %d, other: %d\n",this, __PRETTY_FUNCTION__, m_Id, other.m_Id);
    m_Files.swap(const_cast<ClTranslationUnit&>(other).m_Files);
    const_cast<ClTranslationUnit&>(other).m_ClTranslUnit = nullptr;
}
#endif

ClTranslationUnit::~ClTranslationUnit()
{
    //fprintf(stdout,"%p %s %d\n", this, __PRETTY_FUNCTION__, m_Id);
    if (m_LastCC)
        clang_disposeCodeCompleteResults(m_LastCC);
    if (m_ClTranslUnit)
    {
        fprintf(stdout,"%p Disposing %p\n", this, m_ClTranslUnit);
        clang_disposeTranslationUnit(m_ClTranslUnit);
    }
}

std::ostream& operator << (std::ostream& str, const std::vector<ClFileId> files)
{
    str<<"[ ";
    for( std::vector<ClFileId>::const_iterator it = files.begin(); it != files.end(); ++it )
    {
        str<<*it<<", ";
    }
    str<<"]";
    return str;
}

void ClTranslationUnit::AddInclude(ClFileId fId)
{
    m_Files.push_back(fId);
    //std::cout<<"Added include file id "<<fId<<" to "<<m_Files<<std::endl;
}

bool ClTranslationUnit::Contains(ClFileId fId)
{
    //std::cout<<"Checking file id "<<fId<<" in "<<m_Files<<std::endl;
    //return std::binary_search(m_Files.begin(), m_Files.begin() + std::min(fId + 1, m_Files.size()), fId);
    return std::binary_search(m_Files.begin(), m_Files.end(), fId);
}

CXCodeCompleteResults* ClTranslationUnit::CodeCompleteAt( const wxString& complete_filename, const ClTokenPosition& complete_location, struct CXUnsavedFile* unsaved_files,
        unsigned num_unsaved_files )
{
    if (m_ClTranslUnit == nullptr )
    {
        fprintf(stdout,"%s: m_ClTranslUnit is NULL!\n", __PRETTY_FUNCTION__);
        return NULL;
    }
    //if (m_LastPos.Equals(complete_location.line, complete_location.column)&&(m_LastCC)&&m_LastCC->NumResults)
    //{
    //    fprintf(stdout,"%s: Returning last CC %d,%d (%d)\n", __PRETTY_FUNCTION__, (int)complete_location.line, (int)complete_location.column,  m_LastCC->NumResults);
    //    return m_LastCC;
    //}
    if (m_LastCC)
        clang_disposeCodeCompleteResults(m_LastCC);
    m_LastCC = clang_codeCompleteAt(m_ClTranslUnit, (const char*)complete_filename.ToUTF8(), complete_location.line, complete_location.column,
            unsaved_files, num_unsaved_files,
            clang_defaultCodeCompleteOptions()
            | CXCodeComplete_IncludeCodePatterns
            | CXCodeComplete_IncludeBriefComments);
    m_LastPos.Set(complete_location.line, complete_location.column);
    if (!m_LastCC )
    {
        fprintf(stdout,"%s: clang_CodeComplete returned NULL!\n", __PRETTY_FUNCTION__);
    }
    else
    {
        unsigned numDiag = clang_codeCompleteGetNumDiagnostics(m_LastCC);
        unsigned int IsIncomplete = 0;
        CXCursorKind kind = clang_codeCompleteGetContainerKind(m_LastCC, &IsIncomplete );
        fprintf(stdout, "codecomplete numdiag: %d, container kind: %d, incomplete: %d\n", (int)numDiag, kind, IsIncomplete );
        unsigned int diagIdx = 0;
        std::vector<ClDiagnostic> diaglist;
        for(diagIdx=0; diagIdx < numDiag; ++diagIdx)
        {
            CXDiagnostic diag = clang_codeCompleteGetDiagnostic( m_LastCC, diagIdx );
            ExpandDiagnostic( diag, complete_filename, diaglist );
        }
        for( std::vector<ClDiagnostic>::const_iterator it = diaglist.begin(); it != diaglist.end(); ++it)
        {
            fprintf(stdout, " l=%d  s=%d '%s'\n", it->line, it->severity, (const char*)it->message.mb_str() );
        }
    }

    //fprintf(stdout,"%s: Returning %d results\n", __PRETTY_FUNCTION__, (int)m_LastCC->NumResults);
    return m_LastCC;
}

const CXCompletionResult* ClTranslationUnit::GetCCResult(unsigned index)
{
    if (m_LastCC && index < m_LastCC->NumResults)
        return m_LastCC->Results + index;
    return nullptr;
}

CXCursor ClTranslationUnit::GetTokensAt(const wxString& filename, const ClTokenPosition& location)
{
    return clang_getCursor(m_ClTranslUnit, clang_getLocation(m_ClTranslUnit, GetFileHandle(filename), location.line, location.column));
}

/**
 * Parses the supplied file and unsaved files
 */
void ClTranslationUnit::Parse( const wxString& filename, ClFileId fileId, const std::vector<const char*>& args, const std::map<wxString, wxString>& unsavedFiles, ClTokenDatabase* pDatabase )
{
    fprintf(stdout,"%s\n", __PRETTY_FUNCTION__);

    if (m_LastCC)
    {
        clang_disposeCodeCompleteResults(m_LastCC);
        m_LastCC = nullptr;
    }
    if (m_ClTranslUnit)
    {
        fprintf(stdout,"Disposing %p\n", m_ClTranslUnit);
        clang_disposeTranslationUnit(m_ClTranslUnit);
        m_ClTranslUnit = nullptr;
    }

    // TODO: check and handle error conditions
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
    m_FileId = fileId;
    if (filename.length() != 0)
    {
        m_ClTranslUnit = clang_parseTranslationUnit( m_ClIndex, filename.ToUTF8().data(), args.empty() ? nullptr : &args[0], args.size(),
            //clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0], clUnsavedFiles.size(),
            nullptr, 0,
            clang_defaultEditingTranslationUnitOptions()
            | CXTranslationUnit_CacheCompletionResults
            | CXTranslationUnit_IncludeBriefCommentsInCodeCompletion
            | CXTranslationUnit_DetailedPreprocessingRecord
            | CXTranslationUnit_PrecompiledPreamble
            //CXTranslationUnit_CacheCompletionResults |
            //    CXTranslationUnit_Incomplete | CXTranslationUnit_DetailedPreprocessingRecord |
            //    CXTranslationUnit_CXXChainedPCH
            );
        if ( m_ClTranslUnit == nullptr )
        {
            return;
        }
        if ( pDatabase )
        {
            std::pair<ClTranslationUnit*, ClTokenDatabase*> visitorData = std::make_pair(this, pDatabase);
            clang_getInclusions(m_ClTranslUnit, ClInclusionVisitor, &visitorData);
            //m_FileId = pDatabase->GetFilenameId(filename);
            m_Files.reserve(1024);
            m_Files.push_back(m_FileId);
            std::sort(m_Files.begin(), m_Files.end());
            std::unique(m_Files.begin(), m_Files.end());
        #if __cplusplus >= 201103L
            m_Files.shrink_to_fit();
        #else
            std::vector<ClFileId>(m_Files).swap(m_Files);
        #endif
        //fprintf(stdout,"%s calling Reparse()\n", __PRETTY_FUNCTION__);
        //Reparse(0, nullptr); // seems to improve performance for some reason?
        int ret = clang_reparseTranslationUnit(m_ClTranslUnit, clUnsavedFiles.size(),
                            clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0],
                            clang_defaultReparseOptions(m_ClTranslUnit) );
        //                                   );

        //fprintf(stdout,"%s calling VisitChildren\n", __PRETTY_FUNCTION__);
            struct ClangVisitorContext ctx(pDatabase);
            unsigned rc = clang_visitChildren(clang_getTranslationUnitCursor(m_ClTranslUnit), ClAST_Visitor, &ctx);
            fprintf(stdout,"Visit count: %d, rc=%d\n", (int)ctx.tokenCount, (int)rc);
        //fprintf(stdout,"%s Shrinking database\n", __PRETTY_FUNCTION__);
        //database->Shrink();
        //fprintf(stdout,"%s Done\n", __PRETTY_FUNCTION__);
        }
    }
}

void ClTranslationUnit::Reparse( const std::map<wxString, wxString>& unsavedFiles, ClTokenDatabase* pDatabase)
{
    if (m_ClTranslUnit == nullptr )
    {
        fprintf(stdout,"ERROR: Reparsing a NULL translation Unit\n");
        return;
    }
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


    // TODO: check and handle error conditions
    int ret = clang_reparseTranslationUnit(m_ClTranslUnit, clUnsavedFiles.size(),
                            clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0],
                            clang_defaultReparseOptions(m_ClTranslUnit)
                //CXTranslationUnit_CacheCompletionResults | CXTranslationUnit_PrecompiledPreamble |
                //CXTranslationUnit_Incomplete | CXTranslationUnit_DetailedPreprocessingRecord |
                //CXTranslationUnit_CXXChainedPCH
                                           );
    if (ret != 0 )
    {
        assert(false&&"clang_reparseTranslationUnit should succeed");
        fprintf(stdout,"ERROR: reparseTranslationUnit() failed!");

        // The only thing we can do according to Clang documentation is dispose it...
        clang_disposeTranslationUnit(m_ClTranslUnit);
        m_ClTranslUnit = nullptr;
    }

    struct ClangVisitorContext ctx(pDatabase);
    unsigned rc = clang_visitChildren(clang_getTranslationUnitCursor(m_ClTranslUnit), ClAST_Visitor, &ctx);
    fprintf(stdout,"Visit count: %d, rc=%d\n", (int)ctx.tokenCount, (int)rc);
}

void ClTranslationUnit::GetDiagnostics( const wxString& filename,  std::vector<ClDiagnostic>& diagnostics )
{
    if (m_ClTranslUnit == nullptr )
    {
        return;
    }
    CXDiagnosticSet diagSet = clang_getDiagnosticSetFromTU(m_ClTranslUnit);
    ExpandDiagnosticSet(diagSet, filename, diagnostics);
    clang_disposeDiagnosticSet(diagSet);
}

CXFile ClTranslationUnit::GetFileHandle(const wxString& filename) const
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

void ClTranslationUnit::ExpandDiagnostic( CXDiagnostic diag, const wxString& filename, std::vector<ClDiagnostic>& diagnostics )
{
    if( diag == nullptr )
    {
        return;
    }
    CXSourceLocation loc = clang_getDiagnosticLocation(diag);
    if( clang_equalLocations( loc, clang_getNullLocation()) )
    {
        return;
    }
    switch( clang_getDiagnosticSeverity(diag) )
    {
    case CXDiagnostic_Ignored:
    case CXDiagnostic_Note:
        return;
    default:
        break;
    }
    unsigned line;
    unsigned column;
    CXFile file;
    clang_getSpellingLocation(loc, &file, &line, &column, nullptr);
    CXString str = clang_getFileName(file);
    wxString flName = wxString::FromUTF8(clang_getCString(str));
    clang_disposeString(str);

    if( flName == filename )
    {
        size_t numRnges = clang_getDiagnosticNumRanges(diag);
        unsigned rgStart = 0;
        unsigned rgEnd = 0;
        for (size_t j = 0; j < numRnges; ++j) // often no range data (clang bug?)
        {
            RangeToColumns(clang_getDiagnosticRange(diag, j), rgStart, rgEnd);
            if (rgStart != rgEnd)
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
        if (rgEnd == 0) // still no range -> use the range of the current token
        {
            CXCursor token = clang_getCursor(m_ClTranslUnit, loc);
            RangeToColumns(clang_getCursorExtent(token), rgStart, rgEnd);
        }
        if (rgEnd < column || rgStart > column) // out of bounds?
            rgStart = rgEnd = column;
        str = clang_formatDiagnostic(diag, 0);
        diagnostics.push_back(ClDiagnostic( line, rgStart, rgEnd,
                clang_getDiagnosticSeverity(diag) >= CXDiagnostic_Error ? sError : sWarning,
                flName, wxString::FromUTF8(clang_getCString(str)) ));
        clang_disposeString(str);
    }

}

void ClTranslationUnit::ExpandDiagnosticSet(CXDiagnosticSet diagSet, const wxString& filename, std::vector<ClDiagnostic>& diagnostics)
{
    size_t numDiags = clang_getNumDiagnosticsInSet(diagSet);
    for (size_t i = 0; i < numDiags; ++i)
    {
        CXDiagnostic diag = clang_getDiagnosticInSet(diagSet, i);
        ExpandDiagnostic(diag, filename, diagnostics );
        //ExpandDiagnosticSet(clang_getChildDiagnostics(diag), diagnostics);
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
        std::pair<ClTranslationUnit*, ClTokenDatabase*>* clTranslUnit
            = static_cast<std::pair<ClTranslationUnit*, ClTokenDatabase*>*>(client_data);
        clTranslUnit->first->AddInclude(clTranslUnit->second->GetFilenameId(inclFile.GetFullPath()));
    }
    clang_disposeString(filename);
}

static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor WXUNUSED(parent), CXClientData client_data)
{
    ClTokenType typ = ClTokenType_Unknown;
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
        typ = ClTokenType_ScopeDecl;
        break;

    case CXCursor_FieldDecl:
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_EnumConstantDecl:
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_FunctionDecl:
        typ = ClTokenType_FuncDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_VarDecl:
        typ = ClTokenType_VarDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_ParmDecl:
        typ = ClTokenType_ParmDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_TypedefDecl:
        //case CXCursor_MacroDefinition: // this can crash Clang on Windows
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_CXXMethod:
    case CXCursor_Constructor:
    case CXCursor_Destructor:
    case CXCursor_FunctionTemplate:
        typ = ClTokenType_FuncDecl;
        ret = CXChildVisit_Continue;
        break;

    default:
        return CXChildVisit_Recurse;
    }

    CXSourceLocation loc = clang_getCursorLocation(cursor);
    CXFile clFile;
    unsigned line = 1, col = 1;
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
        wxString displayName;
        wxString scopeName;
        while( !clang_Cursor_isNull(cursor) )
        {
            CXString str;
            switch( cursor.kind )
            {
            case CXCursor_Namespace:
            case CXCursor_StructDecl:
            case CXCursor_ClassDecl:
            case CXCursor_ClassTemplate:
            case CXCursor_ClassTemplatePartialSpecialization:
            case CXCursor_CXXMethod:
                str = clang_getCursorDisplayName(cursor);
                if( displayName.Length() == 0 )
                    displayName = wxString::FromUTF8(clang_getCString(str));
                else
                {
                    if ( scopeName.Length() > 0 )
                    {
                        scopeName = scopeName.Prepend(wxT("::"));
                    }
                    scopeName = scopeName.Prepend( wxString::FromUTF8(clang_getCString(str)) );
                }
                clang_disposeString(str);
                break;
            default:
                break;
            }
            cursor = clang_getCursorSemanticParent(cursor);
        }

        struct ClangVisitorContext* ctx = static_cast<struct ClangVisitorContext*>(client_data);
        //fprintf(stdout,"Inserting token '%s', file='%s', line=%d, col=%d\n", (const char*)identifier.mb_str(), (const char*)filename.mb_str(), line, col);
        ctx->database->InsertToken(identifier, ClAbstractToken(typ,ctx->database->GetFilenameId(filename), ClTokenPosition(line, col), displayName, scopeName, tokenHash));
        ctx->tokenCount++;
    }
    return ret;
}
