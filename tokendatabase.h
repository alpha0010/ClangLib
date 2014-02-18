#ifndef TOKENDATABASE_H
#define TOKENDATABASE_H

#include <vector>

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
        wxString GetFilename(FileId fId) const;

        TokenId InsertToken(const wxString& identifier, const AbstractToken& token); // duplicate tokens are discarded
        TokenId GetTokenId(const wxString& identifier, unsigned tokenHash) const; // returns wxNOT_FOUND on failure
        AbstractToken& GetToken(TokenId tId) const;
        std::vector<TokenId> GetTokenMatches(const wxString& identifier) const;

        void Shrink();

    private:
        TreeMap<AbstractToken>* m_pTokens;
        TreeMap<wxString>* m_pFilenames;
};

#endif // TOKENDATABASE_H
