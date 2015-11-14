#ifndef TRANSLATION_UNIT_H
#define TRANSLATION_UNIT_H

#include <clang-c/Index.h>
#include "tokendatabase.h"

unsigned HashToken(CXCompletionString token, wxString& identifier);

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


class TranslationUnit
{
public:
    TranslationUnit( int id );
    // move ctor
#if __cplusplus >= 201103L
    TranslationUnit(TranslationUnit&& other);
#else
    TranslationUnit(const TranslationUnit& other);
#endif
    ~TranslationUnit();

    friend void swap( TranslationUnit& first, TranslationUnit& second )
    {
        using std::swap;
        swap(first.m_Id, second.m_Id);
        swap(first.m_FileId, second.m_FileId);
        swap(first.m_Files, second.m_Files);
        swap(first.m_ClTranslUnit, second.m_ClTranslUnit);
        swap(first.m_LastCC, second.m_LastCC);
    }
    TranslationUnit& operator=(TranslationUnit other)
    {
        swap(*this,other);
        return *this;
    }

    void AddInclude(FileId fId);
    bool Contains(FileId fId);
    int GetFileId() const { return m_FileId; }
    bool IsEmpty() const { return m_Files.empty(); }
    bool IsValid() const {
        if (IsEmpty())
            return false;
        if (m_ClTranslUnit==nullptr)
            return false;
        if (m_Id < 0)
            return false;
        return true;
    }
    int GetId() const { return m_Id; }

    // note that complete_line and complete_column are 1 index, not 0 index!
    CXCodeCompleteResults* CodeCompleteAt( const char* complete_filename, unsigned complete_line,
            unsigned complete_column, struct CXUnsavedFile* unsaved_files,
            unsigned num_unsaved_files );
    const CXCompletionResult* GetCCResult(unsigned index);
    CXCursor GetTokensAt(const wxString& filename, int line, int column);
    void Parse( const wxString& filename, const std::vector<const char*>& args,
                const std::map<wxString, wxString>& unsavedFiles,
                CXIndex clIndex, TokenDatabase* database );
    void Reparse( const std::map<wxString, wxString>& unsavedFiles );
    void GetDiagnostics(std::vector<ClDiagnostic>& diagnostics);
    CXFile GetFileHandle(const wxString& filename) const;

//private:
public:
#if __cplusplus >= 201103L
    // copying not allowed (we can move)
    TranslationUnit(const TranslationUnit& other);
#endif

    void ExpandDiagnosticSet(CXDiagnosticSet diagSet, std::vector<ClDiagnostic>& diagnostics);
    int m_Id;
    FileId m_FileId;
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

#endif // TRANSLATION_UNIT_H
