#ifndef TRANSLATION_UNIT_H
#define TRANSLATION_UNIT_H

#include <clang-c/Index.h>
#include <clang-c/Documentation.h>
#include "clangpluginapi.h"
#include "tokendatabase.h"

#include <map>

unsigned HashToken(CXCompletionString token, wxString& identifier);

class ClTranslationUnit
{
public:
    ClTranslationUnit( const ClTranslUnitId id );
    ClTranslationUnit( const ClTranslUnitId id, CXIndex clIndex );
    // move ctor
#if __cplusplus >= 201103L
    ClTranslationUnit(ClTranslationUnit&& other);
#else
    ClTranslationUnit(const ClTranslationUnit& other);
#endif
    ~ClTranslationUnit();

    friend void swap( ClTranslationUnit& first, ClTranslationUnit& second )
    {
        using std::swap;
        swap(first.m_Id, second.m_Id);
        swap(first.m_FileId, second.m_FileId);
        swap(first.m_Files, second.m_Files);
        swap(first.m_ClIndex, second.m_ClIndex);
        swap(first.m_ClTranslUnit, second.m_ClTranslUnit);
        swap(first.m_LastCC, second.m_LastCC);
    }
    ClTranslationUnit& operator=(ClTranslationUnit other)
    {
        swap(*this,other);
        return *this;
    }
    bool UsesClangIndex( const CXIndex& idx ){ return idx == m_ClIndex; }

    void AddInclude(ClFileId fId);
    bool Contains(ClFileId fId);
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
    ClTranslUnitId GetId() const { return m_Id; }

    // note that complete_line and complete_column are 1 index, not 0 index!
    CXCodeCompleteResults* CodeCompleteAt( const wxString& complete_filename, const ClTokenPosition& location,
                                           struct CXUnsavedFile* unsaved_files,
                                           unsigned num_unsaved_files );
    const CXCompletionResult* GetCCResult(unsigned index);
    CXCursor GetTokensAt(const wxString& filename, const ClTokenPosition& location);
    void Parse( const wxString& filename, ClFileId FileId, const std::vector<const char*>& args,
                const std::map<wxString, wxString>& unsavedFiles,
                ClTokenDatabase* pUpdateDatabase );
    void Reparse( const std::map<wxString, wxString>& unsavedFiles, ClTokenDatabase* pDatabase );
    void GetDiagnostics( const wxString& filename, std::vector<ClDiagnostic>& diagnostics);
    CXFile GetFileHandle(const wxString& filename) const;

//private:
public:
#if __cplusplus >= 201103L
    // copying not allowed (we can move)
    ClTranslationUnit(const ClTranslationUnit& other);
#endif

    void ExpandDiagnosticSet(CXDiagnosticSet diagSet, const wxString& filename, std::vector<ClDiagnostic>& diagnostics);
    void ExpandDiagnostic( CXDiagnostic diag, const wxString& filename, std::vector<ClDiagnostic>& diagnostics );
    //wxString GetCCDocumentation(const CXCompletionResult* token, ClTokenDatabase* pDatabase);

    ClTranslUnitId m_Id;
    ClFileId m_FileId;
    std::vector<ClFileId> m_Files;
    CXIndex m_ClIndex;
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
    bool m_Occupied; // Sentinel flag
};

#endif // TRANSLATION_UNIT_H
