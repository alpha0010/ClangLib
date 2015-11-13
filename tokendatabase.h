#ifndef TOKENDATABASE_H
#define TOKENDATABASE_H

#include <vector>
#include <wx/thread.h>

template<typename _Tp> class TreeMap;
class wxString;
typedef int FileId;
typedef int TokenId;

struct AbstractToken
{
    AbstractToken(FileId fId, int ln, int col, unsigned tknHash) :
        fileId(fId), line(ln), column(col), tokenHash(tknHash) {}

    FileId fileId;
    int line;
    int column;
    unsigned tokenHash;
};

class TokenDatabase
{
public:
    TokenDatabase();
    ~TokenDatabase();

    FileId GetFilenameId(const wxString& filename);
    wxString GetFilename(FileId fId);
    TokenId GetTokenId(const wxString& identifier, unsigned tokenHash); // returns wxNOT_FOUND on failure
    TokenId InsertToken(const wxString& identifier, const AbstractToken& token); // duplicate tokens are discarded
    AbstractToken GetToken(TokenId tId);
    std::vector<TokenId> GetTokenMatches(const wxString& identifier);

    void Shrink();

private:

    TreeMap<AbstractToken>* m_pTokens;
    TreeMap<wxString>* m_pFilenames;
    wxMutex m_Mutex;
};

#endif // TOKENDATABASE_H
