#ifndef TRANSLATION_UNIT_H
#define TRANSLATION_UNIT_H

#include <clang-c/Index.h>
#include "clangproxy.h"

unsigned HashToken(CXCompletionString token, wxString& identifier);

class TranslationUnit
{
    public:
        TranslationUnit(const wxString& filename, const std::vector<const char*>& args,
                        CXIndex clIndex, TokenDatabase* database);
        // move ctor
#if __cplusplus >= 201103L
        TranslationUnit(TranslationUnit&& other);
#else
        TranslationUnit(const TranslationUnit& other);
#endif
        ~TranslationUnit();

        void AddInclude(FileId fId);
        bool Contains(FileId fId);

        // note that complete_line and complete_column are 1 index, not 0 index!
        CXCodeCompleteResults* CodeCompleteAt( const char* complete_filename, unsigned complete_line,
                                               unsigned complete_column, struct CXUnsavedFile* unsaved_files,
                                               unsigned num_unsaved_files );
        const CXCompletionResult* GetCCResult(unsigned index);
        CXCursor GetTokensAt(const wxString& filename, int line, int column);
        void Reparse(unsigned num_unsaved_files, struct CXUnsavedFile* unsaved_files);
        void GetDiagnostics(std::vector<ClDiagnostic>& diagnostics);
        CXFile GetFileHandle(const wxString& filename) const;

    private:
#if __cplusplus >= 201103L
        // copying not allowed (we can move)
        TranslationUnit(const TranslationUnit& other);
#endif

        void ExpandDiagnosticSet(CXDiagnosticSet diagSet, std::vector<ClDiagnostic>& diagnostics);

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
