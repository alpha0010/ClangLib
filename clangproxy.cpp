/*
 * Communication proxy to libclang-c
 */

#include <sdk.h>

#include "clangproxy.h"

#include <clang-c/Index.h>
#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
    #include <cbexception.h> // for cbThrow()

    #include <algorithm>
#endif // CB_PRECOMP

#include "tokendatabase.h"

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack, unsigned include_len, CXClientData client_data);

static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor parent, CXClientData client_data);

class TranslationUnit
{
    public:
        TranslationUnit(const wxString& filename, const std::vector<const char*>& args, CXIndex clIndex, TokenDatabase* database) :
            m_LastCC(nullptr),
            m_LastPos(-1, -1)
        {
            // TODO: check and handle error conditions
            m_ClTranslUnit = clang_parseTranslationUnit( clIndex, filename.ToUTF8().data(), args.empty() ? nullptr : &args[0], args.size(), nullptr, 0,
                                                         clang_defaultEditingTranslationUnitOptions() | CXTranslationUnit_IncludeBriefCommentsInCodeCompletion | CXTranslationUnit_DetailedPreprocessingRecord );
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
            m_ClTranslUnit(other.m_ClTranslUnit),
            m_LastCC(nullptr),
            m_LastPos(-1, -1)
        {
            m_Files.swap(const_cast<TranslationUnit&>(other).m_Files);
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

        void AddInclude(FileId fId)
        {
            m_Files.push_back(fId);
        }

        bool Contains(FileId fId)
        {
            //return std::binary_search(m_Files.begin(), m_Files.begin() + std::min(fId + 1, m_Files.size()), fId);
            return std::binary_search(m_Files.begin(), m_Files.end(), fId);
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

        CXCursor GetTokensAt(const wxString& filename, int line, int column)
        {
            return clang_getCursor(m_ClTranslUnit, clang_getLocation(m_ClTranslUnit, clang_getFile(m_ClTranslUnit, filename.ToUTF8().data()), line, column));
        }

        void Reparse(unsigned num_unsaved_files, struct CXUnsavedFile* unsaved_files)
        {
            // TODO: check and handle error conditions
            clang_reparseTranslationUnit(m_ClTranslUnit, num_unsaved_files, unsaved_files, clang_defaultReparseOptions(m_ClTranslUnit));
        }

        void GetDiagnostics(std::vector<ClDiagnostic>& diagnostics)
        {
            CXDiagnosticSet diagSet = clang_getDiagnosticSetFromTU(m_ClTranslUnit);
            ExpandDiagnosticSet(diagSet, diagnostics);
            clang_disposeDiagnosticSet(diagSet);
        }

    private:
#if __cplusplus >= 201103L
        // copying not allowed (we can move)
        TranslationUnit(const TranslationUnit& /*other*/) { cbThrow(wxT("Illegal copy attempted of TranslationUnit object.")); }
#endif

        void ExpandDiagnosticSet(CXDiagnosticSet diagSet, std::vector<ClDiagnostic>& diagnostics)
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
                    CXSourceRange range = clang_getDiagnosticRange(diag, j);
                    CXSourceLocation loc = clang_getRangeStart(range);
                    clang_getSpellingLocation(loc, nullptr, nullptr, &rgStart, nullptr);
                    loc = clang_getRangeEnd(range);
                    clang_getSpellingLocation(loc, nullptr, nullptr, &rgEnd, nullptr);
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
                        CXSourceLocation loc = clang_getRangeStart(range);
                        clang_getSpellingLocation(loc, nullptr, nullptr, &rgStart, nullptr);
                        loc = clang_getRangeEnd(range);
                        clang_getSpellingLocation(loc, nullptr, nullptr, &rgEnd, nullptr);
                        if (rgStart != rgEnd)
                            break;
                    }
                }
                CXSourceLocation loc = clang_getDiagnosticLocation(diag);
                if (rgEnd == 0) // still no range -> use the range of the current token
                {
                    CXCursor token = clang_getCursor(m_ClTranslUnit, loc);
                    CXSourceRange range = clang_getCursorExtent(token);
                    CXSourceLocation rgLoc = clang_getRangeStart(range);
                    clang_getSpellingLocation(rgLoc, nullptr, nullptr, &rgStart, nullptr);
                    rgLoc = clang_getRangeEnd(range);
                    clang_getSpellingLocation(rgLoc, nullptr, nullptr, &rgEnd, nullptr);
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
                diagnostics.push_back(ClDiagnostic(line, rgStart, rgEnd, (clang_getDiagnosticSeverity(diag) >= CXDiagnostic_Error ? sError : sWarning), flName, wxString::FromUTF8(clang_getCString(str))));
                clang_disposeString(str);
                clang_disposeDiagnostic(diag);
            }
        }

        std::vector<FileId> m_Files;
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

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* WXUNUSED(inclusion_stack), unsigned WXUNUSED(include_len), CXClientData client_data)
{
    CXString filename = clang_getFileName(included_file);
    wxFileName inclFile(wxString::FromUTF8(clang_getCString(filename)));
    if (inclFile.MakeAbsolute())
    {
        std::pair<TranslationUnit*, TokenDatabase*>* clTranslUnit = static_cast<std::pair<TranslationUnit*, TokenDatabase*>*>(client_data);
        clTranslUnit->first->AddInclude(clTranslUnit->second->GetFilenameId(inclFile.GetFullPath()));
    }
    clang_disposeString(filename);
}

static unsigned HashToken(CXCompletionString token, wxString& identifier)
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

namespace HTML_Writer
{
    static wxString Escape(const wxString& text)
    {
        wxString html;
        html.reserve(text.size());
        for (wxString::const_iterator itr = text.begin();
             itr != text.end(); ++itr)
        {
            switch (*itr)
            {
                case wxT('&'):  html += wxT("&amp;");  break;
                case wxT('\"'): html += wxT("&quot;"); break;
                case wxT('\''): html += wxT("&apos;"); break;
                case wxT('<'):  html += wxT("&lt;");   break;
                case wxT('>'):  html += wxT("&gt;");   break;
                case wxT('\n'): html += wxT("<br>");   break;
                default:        html += *itr;          break;
            }
        }
        return html;
    }

    static wxString Colourise(const wxString& text, const wxString& colour)
    {
        return wxT("<font color=\"") + colour + wxT("\">") + text + wxT("</font>");
    }

    static wxString SyntaxHl(const wxString& code, const std::vector<wxString>& cppKeywords) // C++ style (ish)
    {
        wxString html;
        html.reserve(code.size());
        int stRg = 0;
        int style = wxSCI_C_DEFAULT;
        const int codeLen = code.Length();
        for (int enRg = 0; enRg <= codeLen; ++enRg)
        {
            wxChar ch = (enRg < codeLen ? code[enRg] : wxT('\0'));
            wxChar nextCh = (enRg < codeLen - 1 ? code[enRg + 1] : wxT('\0'));
            switch (style)
            {
                default:
                case wxSCI_C_DEFAULT:
                {
                    if (wxIsalpha(ch) || ch == wxT('_'))
                        style = wxSCI_C_IDENTIFIER;
                    else if (wxIsdigit(ch))
                        style = wxSCI_C_NUMBER;
                    else if (ch == wxT('"'))
                        style = wxSCI_C_STRING;
                    else if (ch == wxT('\''))
                        style = wxSCI_C_CHARACTER;
                    else if (ch == wxT('/') && nextCh == wxT('/'))
                        style = wxSCI_C_COMMENTLINE;
                    else if (wxIspunct(ch))
                        style = wxSCI_C_OPERATOR;
                    else
                        break;
                    if (stRg != enRg)
                    {
                        html += Escape(code.Mid(stRg, enRg - stRg));
                        stRg = enRg;
                    }
                    break;
                }

                case wxSCI_C_IDENTIFIER:
                {
                    if (wxIsalnum(ch) || ch == wxT('_'))
                        break;
                    if (stRg != enRg)
                    {
                        const wxString& tkn = code.Mid(stRg, enRg - stRg);
                        if (std::binary_search(cppKeywords.begin(), cppKeywords.end(), tkn))
                            html += wxT("<b>") + Colourise(Escape(tkn), wxT("#00008b")) + wxT("</b>"); // DarkBlue
                        else
                            html += Escape(tkn);
                        stRg = enRg;
                        --enRg;
                    }
                    style = wxSCI_C_DEFAULT;
                    break;
                }

                case wxSCI_C_NUMBER:
                {
                    if (wxIsalnum(ch))
                        break;
                    if (stRg != enRg)
                    {
                        html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("Magenta"));
                        stRg = enRg;
                        --enRg;
                    }
                    style = wxSCI_C_DEFAULT;
                    break;
                }

                case wxSCI_C_STRING:
                {
                    if (ch == wxT('\\'))
                    {
                        if (nextCh != wxT('\n'))
                            ++enRg;
                        break;
                    }
                    else if (ch && ch != wxT('"') && ch != wxT('\n'))
                        break;
                    if (stRg != enRg)
                    {
                        if (ch == wxT('"'))
                            ++enRg;
                        html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("#0000cd")); // MediumBlue
                        stRg = enRg;
                        --enRg;
                    }
                    style = wxSCI_C_DEFAULT;
                    break;
                }

                case wxSCI_C_CHARACTER:
                {
                    if (ch == wxT('\\'))
                    {
                        if (nextCh != wxT('\n'))
                            ++enRg;
                        break;
                    }
                    else if (ch && ch != wxT('\'') && ch != wxT('\n'))
                        break;
                    if (stRg != enRg)
                    {
                        if (ch == wxT('\''))
                            ++enRg;
                        html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("GoldenRod"));
                        stRg = enRg;
                        --enRg;
                    }
                    style = wxSCI_C_DEFAULT;
                    break;
                }

                case wxSCI_C_COMMENTLINE:
                {
                    if (ch && ch != wxT('\n'))
                        break;
                    if (stRg != enRg)
                    {
                        html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("#778899")); // LightSlateGray
                        stRg = enRg;
                    }
                    style = wxSCI_C_DEFAULT;
                    break;
                }

                case wxSCI_C_OPERATOR:
                {
                    if (wxIspunct(ch) && ch != wxT('"') && ch != wxT('\'') && ch != wxT('_'))
                        break;
                    if (stRg != enRg)
                    {
                        html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("Red"));
                        stRg = enRg;
                        --enRg;
                    }
                    style = wxSCI_C_DEFAULT;
                    break;
                }
            }
        }
        return html;
    }

    static void FormatDocumentation(CXComment comment, wxString& doc, const std::vector<wxString>& cppKeywords)
    {
        size_t numChildren = clang_Comment_getNumChildren(comment);
        for (size_t childIdx = 0; childIdx < numChildren; ++childIdx)
        {
            CXComment cmt = clang_Comment_getChild(comment, childIdx);
            switch (clang_Comment_getKind(cmt))
            {
                case CXComment_Null:
                    break;

                case CXComment_Text:
                {
                    CXString str = clang_TextComment_getText(cmt);
                    doc += Escape(wxString::FromUTF8(clang_getCString(str)));
                    clang_disposeString(str);
                    break;
                }

                case CXComment_InlineCommand:
                {
                    size_t numArgs = clang_InlineCommandComment_getNumArgs(cmt);
                    wxString argText;
                    for (size_t argIdx = 0; argIdx < numArgs; ++argIdx)
                    {
                        CXString str = clang_InlineCommandComment_getArgText(cmt, argIdx);
                        argText += Escape(wxString::FromUTF8(clang_getCString(str)));
                        clang_disposeString(str);
                    }
                    CXCommentInlineCommandRenderKind cmtFont = clang_InlineCommandComment_getRenderKind(cmt);
                    switch (cmtFont)
                    {
                        default:
                        case CXCommentInlineCommandRenderKind_Normal:
                            doc += argText;
                            break;

                        case CXCommentInlineCommandRenderKind_Bold:
                            doc += wxT("<b>") + argText + wxT("</b>");
                            break;

                        case CXCommentInlineCommandRenderKind_Monospaced:
                            doc += wxT("<tt>") + argText + wxT("</tt>");
                            break;

                        case CXCommentInlineCommandRenderKind_Emphasized:
                            doc += wxT("<em>") + argText + wxT("</em>");
                            break;
                    }
                    break;
                }

                case CXComment_HTMLStartTag:
                case CXComment_HTMLEndTag:
                {
                    CXString str = clang_HTMLTagComment_getAsString(cmt);
                    doc += wxString::FromUTF8(clang_getCString(str));
                    clang_disposeString(str);
                    break;
                }

                case CXComment_Paragraph:
                    if (!clang_Comment_isWhitespace(cmt))
                    {
                        doc += wxT("<p>");
                        FormatDocumentation(cmt, doc, cppKeywords);
                        doc += wxT("</p>");
                    }
                    break;

                case CXComment_BlockCommand: // TODO: follow the command's instructions
                    FormatDocumentation(cmt, doc, cppKeywords);
                    break;

                case CXComment_ParamCommand:  // TODO
                case CXComment_TParamCommand: // TODO
                    break;

                case CXComment_VerbatimBlockCommand:
                    doc += wxT("<table cellspacing=\"0\" cellpadding=\"1\" bgcolor=\"black\" width=\"100%\"><tr><td><table bgcolor=\"white\" width=\"100%\"><tr><td><pre>");
                    FormatDocumentation(cmt, doc, cppKeywords);
                    doc += wxT("</pre></td></tr></table></td></tr></table>");
                    break;

                case CXComment_VerbatimBlockLine:
                {
                    CXString str = clang_VerbatimBlockLineComment_getText(cmt);
                    wxString codeLine = wxString::FromUTF8(clang_getCString(str));
                    clang_disposeString(str);
                    int endIdx = codeLine.Find(wxT("*/")); // clang will throw in the rest of the file when this happens
                    if (endIdx != wxNOT_FOUND)
                    {
                        endIdx = codeLine.Truncate(endIdx).Find(wxT("\\endcode")); // try to save a bit of grace, and recover what we can
                        if (endIdx == wxNOT_FOUND)
                        {
                            endIdx = codeLine.Find(wxT("@endcode"));
                            if (endIdx != wxNOT_FOUND)
                                codeLine.Truncate(endIdx);
                        }
                        else
                            codeLine.Truncate(endIdx);
                        doc += SyntaxHl(codeLine, cppKeywords) + wxT("<br><font color=\"red\"><em>__clang_doxygen_parsing_error__</em></font><br>");
                        return; // abort
                    }
                    doc += SyntaxHl(codeLine, cppKeywords) + wxT("<br>");
                    break;
                }

                case CXComment_VerbatimLine:
                {
                    CXString str = clang_VerbatimLineComment_getText(cmt);
                    doc += wxT("<pre>") + Escape(wxString::FromUTF8(clang_getCString(str))) + wxT("</pre>"); // TODO: syntax highlight
                    clang_disposeString(str);
                    break;
                }

                case CXComment_FullComment: // ignore?
                default:
                    break;
            }
        }
    }
}

ClangProxy::ClangProxy(TokenDatabase& database, const std::vector<wxString>& cppKeywords):
    m_Database(database),
    m_CppKeywords(cppKeywords)
{
    m_ClIndex = clang_createIndex(0, 0);
}

ClangProxy::~ClangProxy()
{
    m_TranslUnits.clear();
    clang_disposeIndex(m_ClIndex);
}

void ClangProxy::CreateTranslationUnit(const wxString& filename, const wxString& commands)
{
    wxStringTokenizer tokenizer(commands);
    if (!filename.EndsWith(wxT(".c"))) // force language reduces chance of error on STL headers
        tokenizer.SetString(commands + wxT(" -x c++"));
    std::vector<wxString> unknownOptions;
    unknownOptions.push_back(wxT("-Wno-unused-local-typedefs"));
    unknownOptions.push_back(wxT("-Wzero-as-null-pointer-constant"));
    std::sort(unknownOptions.begin(), unknownOptions.end());
    std::vector<wxCharBuffer> argsBuffer;
    std::vector<const char*> args;
    while (tokenizer.HasMoreTokens())
    {
        const wxString& compilerSwitch = tokenizer.GetNextToken();
        if (std::binary_search(unknownOptions.begin(), unknownOptions.end(), compilerSwitch))
            continue;
        argsBuffer.push_back(compilerSwitch.ToUTF8());
        args.push_back(argsBuffer.back().data());
    }
    m_TranslUnits.push_back(TranslationUnit(filename, args, m_ClIndex, &m_Database));
}

int ClangProxy::GetTranslationUnitId(FileId fId)
{
    for (size_t i = 0; i < m_TranslUnits.size(); ++i)
    {
        if (m_TranslUnits[i].Contains(fId))
            return i;
    }
    return wxNOT_FOUND;
}

int ClangProxy::GetTranslationUnitId(const wxString& filename)
{
    return GetTranslationUnitId(m_Database.GetFilenameId(filename));
}

static TokenCategory GetTokenCategory(CXCursorKind kind, CX_CXXAccessSpecifier access = CX_CXXInvalidAccessSpecifier)
{
    switch (kind)
    {
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
            switch (access)
            {
                case CX_CXXPublic:
                    return tcClassPublic;
                case CX_CXXProtected:
                    return tcClassProtected;
                case CX_CXXPrivate:
                    return tcClassPrivate;
                default:
                case CX_CXXInvalidAccessSpecifier:
                    return tcClass;
            }

        case CXCursor_Constructor:
            switch (access)
            {
                default:
                case CX_CXXInvalidAccessSpecifier:
                case CX_CXXPublic:
                    return tcCtorPublic;
                case CX_CXXProtected:
                    return tcCtorProtected;
                case CX_CXXPrivate:
                    return tcCtorPrivate;
            }

        case CXCursor_Destructor:
            switch (access)
            {
                default:
                case CX_CXXInvalidAccessSpecifier:
                case CX_CXXPublic:
                    return tcDtorPublic;
                case CX_CXXProtected:
                    return tcDtorProtected;
                case CX_CXXPrivate:
                    return tcDtorPrivate;
            }

        case CXCursor_FunctionDecl:
        case CXCursor_CXXMethod:
        case CXCursor_FunctionTemplate:
            switch (access)
            {
                default:
                case CX_CXXInvalidAccessSpecifier:
                case CX_CXXPublic:
                    return tcFuncPublic;
                case CX_CXXProtected:
                    return tcFuncProtected;
                case CX_CXXPrivate:
                    return tcFuncPrivate;
            }

        case CXCursor_FieldDecl:
        case CXCursor_VarDecl:
        case CXCursor_ParmDecl:
            switch (access)
            {
                default:
                case CX_CXXInvalidAccessSpecifier:
                case CX_CXXPublic:
                    return tcVarPublic;
                case CX_CXXProtected:
                    return tcVarProtected;
                case CX_CXXPrivate:
                    return tcVarPrivate;
            }

        case CXCursor_MacroDefinition:
            return tcMacroDef;

        case CXCursor_EnumDecl:
            switch (access)
            {
                case CX_CXXPublic:
                    return tcEnumPublic;
                case CX_CXXProtected:
                    return tcEnumProtected;
                case CX_CXXPrivate:
                    return tcEnumPrivate;
                default:
                case CX_CXXInvalidAccessSpecifier:
                    return tcEnum;
            }

        case CXCursor_EnumConstantDecl:
            return tcEnumerator;

        case CXCursor_Namespace:
            return tcNamespace;

        case CXCursor_TypedefDecl:
            switch (access)
            {
                case CX_CXXPublic:
                    return tcTypedefPublic;
                case CX_CXXProtected:
                    return tcTypedefProtected;
                case CX_CXXPrivate:
                    return tcTypedefPrivate;
                default:
                case CX_CXXInvalidAccessSpecifier:
                    return tcTypedef;
            }

        default:
            return tcNone;
    }
}

void ClangProxy::CodeCompleteAt(bool isAuto, const wxString& filename, int line, int column, int translId, const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results)
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

    if (isAuto && clang_codeCompleteGetContexts(clResults) == CXCompletionContext_Unknown)
        return;

    const int numResults = clResults->NumResults;
    results.reserve(numResults);
    for (int resIdx = 0; resIdx < numResults; ++resIdx)
    {
        const CXCompletionResult& token = clResults->Results[resIdx];
        if (CXAvailability_Available != clang_getCompletionAvailability(token.CompletionString))
            continue;
        const int numChunks = clang_getNumCompletionChunks(token.CompletionString);
        wxString type;
        for (int chunkIdx = 0; chunkIdx < numChunks; ++chunkIdx)
        {
            CXCompletionChunkKind kind = clang_getCompletionChunkKind(token.CompletionString, chunkIdx);
            if (kind == CXCompletionChunk_ResultType)
            {
                CXString str = clang_getCompletionChunkText(token.CompletionString, chunkIdx);
                type = wxT(": ") + wxString::FromUTF8(clang_getCString(str));
                wxString prefix;
                if (type.EndsWith(wxT(" *"), &prefix) || type.EndsWith(wxT(" &"), &prefix))
                    type = prefix + type.Last();
                clang_disposeString(str);
            }
            else if (kind == CXCompletionChunk_TypedText)
            {
                if (type.Length() > 40)
                {
                    type.Truncate(35);
                    if (wxIsspace(type.Last()))
                        type.Trim();
                    else if (wxIspunct(type.Last()))
                    {
                        for (int i = type.Length() - 2; i > 10; --i)
                        {
                            if (!wxIspunct(type[i]))
                            {
                                type.Truncate(i + 1);
                                break;
                            }
                        }
                    }
                    else if (wxIsalnum(type.Last()) || type.Last() == wxT('_'))
                    {
                        for (int i = type.Length() - 2; i > 10; --i)
                        {
                            if (!( wxIsalnum(type[i]) || type[i] == wxT('_') ))
                            {
                                type.Truncate(i + 1);
                                break;
                            }
                        }
                    }
                    type += wxT("...");
                }
                CXString completeTxt = clang_getCompletionChunkText(token.CompletionString, chunkIdx);
                results.push_back(ClToken(wxString::FromUTF8(clang_getCString(completeTxt)) + type,// + F(wxT("%d"), token.CursorKind),
                                          resIdx, clang_getCompletionPriority(token.CompletionString), GetTokenCategory(token.CursorKind)));
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
    if (token->CursorKind == CXCursor_Namespace)
        doc = wxT("namespace ");
    for (int i = 0; i < upperBound; ++i)
    {
        CXCompletionChunkKind kind = clang_getCompletionChunkKind(token->CompletionString, i);
        if (kind == CXCompletionChunk_TypedText)
        {
            CXString str = clang_getCompletionParent(token->CompletionString, nullptr);
            wxString parent = wxString::FromUTF8(clang_getCString(str));
            if (!parent.IsEmpty())
                doc += parent + wxT("::");
            clang_disposeString(str);
        }
        CXString str = clang_getCompletionChunkText(token->CompletionString, i);
        doc += wxString::FromUTF8(clang_getCString(str));
        if (kind == CXCompletionChunk_ResultType)
        {
            if (doc.Length() > 2 && doc[doc.Length() - 2] == wxT(' '))
                doc.RemoveLast(2) += doc.Last();
            doc += wxT(' ');
        }
        clang_disposeString(str);
    }

    wxString descriptor;
    wxString identifier;
    unsigned tokenHash = HashToken(token->CompletionString, identifier);
    if (!identifier.IsEmpty())
    {
        TokenId tId = m_Database.GetTokenId(identifier, tokenHash);
        if (tId != wxNOT_FOUND)
        {
            const AbstractToken& aTkn = m_Database.GetToken(tId);
            CXCursor clTkn = m_TranslUnits[translId].GetTokensAt(m_Database.GetFilename(aTkn.fileId), aTkn.line, aTkn.column);
            if (!clang_Cursor_isNull(clTkn) && !clang_isInvalid(clTkn.kind))
            {
                CXComment docComment = clang_Cursor_getParsedComment(clTkn);
                HTML_Writer::FormatDocumentation(docComment, descriptor, m_CppKeywords);
                if (clTkn.kind == CXCursor_EnumConstantDecl)
                {
                    // can possibly yield incorrect results
                    doc += wxString::Format(wxT("=%d"), static_cast<int>(clang_getEnumConstantDeclValue(clTkn)));
                }
                else if (clTkn.kind == CXCursor_TypedefDecl)
                {
                    CXString str = clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(clTkn));
                    wxString type = wxString::FromUTF8(clang_getCString(str));
                    if (!type.IsEmpty())
                        doc.Prepend(wxT("typedef ") + type + wxT(" "));
                    clang_disposeString(str);
                }
            }
        }
    }

    if (descriptor.IsEmpty())
    {
        CXString comment = clang_getCompletionBriefComment(token->CompletionString);
        descriptor = HTML_Writer::Escape(wxT("\n") + wxString::FromUTF8(clang_getCString(comment)));
        clang_disposeString(comment);
    }

    return wxT("<html><body><br><tt>") + HTML_Writer::SyntaxHl(doc, m_CppKeywords) + wxT("</tt>") + descriptor + wxT("</body></html>");
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

void ClangProxy::RefineTokenType(int translId, int tknId, int& tknType)
{
    const CXCompletionResult* token = m_TranslUnits[translId].GetCCResult(tknId);
    if (!token)
        return;
    wxString identifier;
    unsigned tokenHash = HashToken(token->CompletionString, identifier);
    if (!identifier.IsEmpty())
    {
        TokenId tId = m_Database.GetTokenId(identifier, tokenHash);
        if (tId != wxNOT_FOUND)
        {
            const AbstractToken& aTkn = m_Database.GetToken(tId);
            CXCursor clTkn = m_TranslUnits[translId].GetTokensAt(m_Database.GetFilename(aTkn.fileId), aTkn.line, aTkn.column);
            if (!clang_Cursor_isNull(clTkn) && !clang_isInvalid(clTkn.kind))
            {
                TokenCategory tkCat = GetTokenCategory(token->CursorKind, clang_getCXXAccessSpecifier(clTkn));
                if (tkCat != tcNone)
                    tknType = tkCat;
            }
        }
    }
}

static CXChildVisitResult ClCallTipCtorAST_Visitor(CXCursor cursor, CXCursor WXUNUSED(parent), CXClientData client_data)
{
    switch (cursor.kind)
    {
        case CXCursor_Constructor:
        {
            std::vector<CXCursor>* tokenSet = static_cast<std::vector<CXCursor>*>(client_data);
            tokenSet->push_back(cursor);
            break;
        }

        case CXCursor_FunctionDecl:
        case CXCursor_CXXMethod:
        case CXCursor_FunctionTemplate:
        {
            CXString str = clang_getCursorSpelling(cursor);
            if (strcmp(clang_getCString(str), "operator()") == 0)
            {
                std::vector<CXCursor>* tokenSet = static_cast<std::vector<CXCursor>*>(client_data);
                tokenSet->push_back(cursor);
            }
            clang_disposeString(str);
            break;
        }

        default:
            break;
    }
    return CXChildVisit_Continue;
}

void ClangProxy::GetCallTipsAt(const wxString& filename, int line, int column, int translId, const wxString& tokenStr, std::vector<wxStringVec>& results)
{
    std::vector<CXCursor> tokenSet;
    if (column > static_cast<int>(tokenStr.Length()))
    {
        column -= tokenStr.Length() / 2;
        CXCursor token = m_TranslUnits[translId].GetTokensAt(filename, line, column);
        if (!clang_Cursor_isNull(token))
        {
            CXCursor resolve = clang_getCursorDefinition(token);
            if (clang_Cursor_isNull(resolve) || clang_isInvalid(token.kind))
            {
                resolve = clang_getCursorReferenced(token);
                if (!clang_Cursor_isNull(resolve) && !clang_isInvalid(token.kind))
                    token = resolve;
            }
            else
                token = resolve;
            tokenSet.push_back(token);
        }
    }
    // TODO: searching the database is very inexact, but necessary, as clang
    // does not resolve the token when the code is invalid (incomplete)
    std::vector<TokenId> tknIds = m_Database.GetTokenMatches(tokenStr);
    for (std::vector<TokenId>::const_iterator itr = tknIds.begin(); itr != tknIds.end(); ++itr)
    {
        const AbstractToken& aTkn = m_Database.GetToken(*itr);
        CXCursor token = m_TranslUnits[translId].GetTokensAt(m_Database.GetFilename(aTkn.fileId), aTkn.line, aTkn.column);
        if (!clang_Cursor_isNull(token) && !clang_isInvalid(token.kind))
            tokenSet.push_back(token);
    }
    std::set<wxString> uniqueTips;
    for (size_t tknIdx = 0; tknIdx < tokenSet.size(); ++tknIdx)
    {
        CXCursor token = tokenSet[tknIdx];
        switch (GetTokenCategory(token.kind, CX_CXXPublic))
        {
            case tcVarPublic:
            {
                token = clang_getTypeDeclaration(clang_getCursorResultType(token));
                if (!clang_Cursor_isNull(token) && !clang_isInvalid(token.kind))
                    tokenSet.push_back(token);
                break;
            }

            case tcTypedefPublic:
            {
                token = clang_getTypeDeclaration(clang_getTypedefDeclUnderlyingType(token));
                if (!clang_Cursor_isNull(token) && !clang_isInvalid(token.kind))
                    tokenSet.push_back(token);
                break;
            }

            case tcClassPublic:
            {
                // search for constructors and 'operator()'
                clang_visitChildren(token, &ClCallTipCtorAST_Visitor, &tokenSet);
                break;
            }

            case tcCtorPublic:
            {
                if (clang_getCXXAccessSpecifier(token) == CX_CXXPrivate)
                    break;
                // fall through
            }
            case tcFuncPublic:
            {
                const CXCompletionString& clCompStr = clang_getCursorCompletionString(token);
                wxStringVec entry;
                int upperBound = clang_getNumCompletionChunks(clCompStr);
                entry.push_back(wxEmptyString);
                for (int chunkIdx = 0; chunkIdx < upperBound; ++chunkIdx)
                {
                    CXCompletionChunkKind kind = clang_getCompletionChunkKind(clCompStr, chunkIdx);
                    if (kind == CXCompletionChunk_TypedText)
                    {
                        CXString str = clang_getCompletionParent(clCompStr, nullptr);
                        wxString parent = wxString::FromUTF8(clang_getCString(str));
                        if (!parent.IsEmpty())
                            entry[0] += parent + wxT("::");
                        clang_disposeString(str);
                    }
                    else if (kind == CXCompletionChunk_LeftParen)
                    {
                        if (entry[0].IsEmpty() || !entry[0].EndsWith(wxT("operator")))
                            break;
                    }
                    CXString str = clang_getCompletionChunkText(clCompStr, chunkIdx);
                    entry[0] += wxString::FromUTF8(clang_getCString(str));
                    if (kind == CXCompletionChunk_ResultType)
                    {
                        if (entry[0].Length() > 2 && entry[0][entry[0].Length() - 2] == wxT(' '))
                            entry[0].RemoveLast(2) += entry[0].Last();
                        entry[0] += wxT(' ');
                    }
                    clang_disposeString(str);
                }
                entry[0] += wxT('(');
                int numArgs = clang_Cursor_getNumArguments(token);
                for (int argIdx = 0; argIdx < numArgs; ++argIdx)
                {
                    CXCursor arg = clang_Cursor_getArgument(token, argIdx);

                    wxString tknStr;
                    const CXCompletionString& argStr = clang_getCursorCompletionString(arg);
                    upperBound = clang_getNumCompletionChunks(argStr);
                    for (int chunkIdx = 0; chunkIdx < upperBound; ++chunkIdx)
                    {
                        CXCompletionChunkKind kind = clang_getCompletionChunkKind(argStr, chunkIdx);
                        if (kind == CXCompletionChunk_TypedText)
                        {
                            CXString str = clang_getCompletionParent(argStr, nullptr);
                            wxString parent = wxString::FromUTF8(clang_getCString(str));
                            if (!parent.IsEmpty())
                                tknStr += parent + wxT("::");
                            clang_disposeString(str);
                        }
                        CXString str = clang_getCompletionChunkText(argStr, chunkIdx);
                        tknStr += wxString::FromUTF8(clang_getCString(str));
                        if (kind == CXCompletionChunk_ResultType)
                        {
                            if (tknStr.Length() > 2 && tknStr[tknStr.Length() - 2] == wxT(' '))
                                tknStr.RemoveLast(2) += tknStr.Last();
                            tknStr += wxT(' ');
                        }
                        clang_disposeString(str);
                    }

                    entry.push_back(tknStr.Trim());
                }
                entry.push_back(wxT(')'));
                wxString composit;
                for (wxStringVec::const_iterator itr = entry.begin();
                     itr != entry.end(); ++itr)
                {
                    composit += *itr;
                }
                if (uniqueTips.find(composit) != uniqueTips.end())
                    break;
                uniqueTips.insert(composit);
                results.push_back(entry);
                break;
            }

            default:
                break;
        }
    }
}

static CXChildVisitResult ClInheritance_Visitor(CXCursor cursor, CXCursor WXUNUSED(parent), CXClientData client_data)
{
    if (cursor.kind != CXCursor_CXXBaseSpecifier)
        return CXChildVisit_Break;
    CXString str = clang_getTypeSpelling(clang_getCursorType(cursor));
    static_cast<wxStringVec*>(client_data)->push_back(wxString::FromUTF8(clang_getCString(str)));
    clang_disposeString(str);
    return CXChildVisit_Continue;
}

void ClangProxy::GetTokensAt(const wxString& filename, int line, int column, int translId, wxStringVec& results)
{
    CXCursor token = m_TranslUnits[translId].GetTokensAt(filename, line, column);
    if (clang_Cursor_isNull(token))
        return;
    CXCursor resolve = clang_getCursorDefinition(token);
    if (clang_Cursor_isNull(resolve) || clang_isInvalid(token.kind))
    {
        resolve = clang_getCursorReferenced(token);
        if (!clang_Cursor_isNull(resolve) && !clang_isInvalid(token.kind))
            token = resolve;
    }
    else
        token = resolve;

    wxString tknStr;
    const CXCompletionString& clCompStr = clang_getCursorCompletionString(token);
    int upperBound = clang_getNumCompletionChunks(clCompStr);
    for (int i = 0; i < upperBound; ++i)
    {
        CXCompletionChunkKind kind = clang_getCompletionChunkKind(clCompStr, i);
        if (kind == CXCompletionChunk_TypedText)
        {
            CXString str = clang_getCompletionParent(clCompStr, nullptr);
            wxString parent = wxString::FromUTF8(clang_getCString(str));
            if (!parent.IsEmpty())
                tknStr += parent + wxT("::");
            clang_disposeString(str);
        }
        CXString str = clang_getCompletionChunkText(clCompStr, i);
        tknStr += wxString::FromUTF8(clang_getCString(str));
        if (kind == CXCompletionChunk_ResultType)
        {
            if (tknStr.Length() > 2 && tknStr[tknStr.Length() - 2] == wxT(' '))
                tknStr.RemoveLast(2) += tknStr.Last();
            tknStr += wxT(' ');
        }
        clang_disposeString(str);
    }
    if (!tknStr.IsEmpty())
    {
        switch (token.kind)
        {
            case CXCursor_StructDecl:
            case CXCursor_ClassDecl:
            case CXCursor_ClassTemplate:
            case CXCursor_ClassTemplatePartialSpecialization:
            {
                if (token.kind == CXCursor_StructDecl)
                    tknStr.Prepend(wxT("struct "));
                else
                    tknStr.Prepend(wxT("class "));
                wxStringVec directAncestors;
                clang_visitChildren(token, &ClInheritance_Visitor, &directAncestors);
                for (wxStringVec::const_iterator daItr = directAncestors.begin();
                     daItr != directAncestors.end(); ++daItr)
                {
                    if (daItr == directAncestors.begin())
                        tknStr += wxT(" : ");
                    else
                        tknStr += wxT(", ");
                    tknStr += *daItr;
                }
                break;
            }

            case CXCursor_UnionDecl:
                tknStr.Prepend(wxT("union "));
                break;

            case CXCursor_EnumDecl:
                tknStr.Prepend(wxT("enum "));
                break;

            case CXCursor_EnumConstantDecl:
                tknStr += wxString::Format(wxT("=%d"), static_cast<int>(clang_getEnumConstantDeclValue(token)));
                break;

            case CXCursor_TypedefDecl:
            {
                CXString str = clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(token));
                wxString type = wxString::FromUTF8(clang_getCString(str));
                clang_disposeString(str);
                if (!type.IsEmpty())
                    tknStr.Prepend(wxT("typedef ") + type + wxT(" "));
                break;
            }

            case CXCursor_Namespace:
                tknStr.Prepend(wxT("namespace "));
                break;

            case CXCursor_MacroDefinition:
                tknStr.Prepend(wxT("#define ")); // TODO: show (partial) definition
                break;

            default:
                break;
        }
        results.push_back(tknStr);
    }
}

void ClangProxy::ResolveTokenAt(wxString& filename, int& line, int& column, int translId)
{
    CXCursor token = m_TranslUnits[translId].GetTokensAt(filename, line, column);
    if (clang_Cursor_isNull(token))
        return;
    CXCursor resolve = clang_getCursorDefinition(token);
    if (clang_Cursor_isNull(resolve) || clang_isInvalid(token.kind))
    {
        resolve = clang_getCursorReferenced(token);
        if (!clang_Cursor_isNull(resolve) && !clang_isInvalid(token.kind))
            token = resolve;
    }
    else
        token = resolve;
    CXFile file;
    if (token.kind == CXCursor_InclusionDirective)
    {
        file = clang_getIncludedFile(token);
        line = 1;
        column = 1;
    }
    else
    {
        CXSourceLocation loc = clang_getCursorLocation(token);
        unsigned ln, col;
        clang_getSpellingLocation(loc, &file, &ln, &col, nullptr);
        line   = ln;
        column = col;
    }
    CXString str = clang_getFileName(file);
    filename = wxString::FromUTF8(clang_getCString(str));
    clang_disposeString(str);
}

void ClangProxy::Reparse(int translId, const std::map<wxString, wxString>& unsavedFiles)
{
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
    m_TranslUnits[translId].Reparse(clUnsavedFiles.size(), clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0]);
}

void ClangProxy::GetDiagnostics(int translId, std::vector<ClDiagnostic>& diagnostics)
{
    m_TranslUnits[translId].GetDiagnostics(diagnostics);
}
