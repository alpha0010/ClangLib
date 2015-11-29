#ifndef TOKENDATABASE_H
#define TOKENDATABASE_H

#include "clangpluginapi.h"

#include <vector>
#include <wx/thread.h>
#include <wx/string.h>


template<typename _Tp> class ClTreeMap;
class wxString;
typedef int ClFileId;

typedef enum _TokenType
{
    ClTokenType_Unknown = 0,
    ClTokenType_FuncDecl  = 1<<0,
    ClTokenType_VarDecl   = 1<<1,
    ClTokenType_ParmDecl  = 1<<2,
    ClTokenType_ScopeDecl = 1<<3,

}ClTokenType;

struct ClAbstractToken
{
    ClAbstractToken( ClTokenType typ, ClFileId fId, ClTokenPosition location, wxString displayName, wxString scopeName, unsigned tknHash) :
        type(typ), fileId(fId), location(location), displayName(displayName.c_str()), scopeName(scopeName.c_str()), tokenHash(tknHash) {}
    ClAbstractToken( const ClAbstractToken& other ) :
        type(other.type), fileId(other.fileId), location(other.location), displayName( other.displayName.c_str()), scopeName(other.scopeName.c_str()), tokenHash(other.tokenHash) {}

    ClTokenType type;
    ClFileId fileId;
    ClTokenPosition location;
    wxString displayName;
    wxString scopeName;
    unsigned tokenHash;
};

class ClTokenDatabase
{
public:
    ClTokenDatabase();
    ~ClTokenDatabase();

    ClFileId GetFilenameId(const wxString& filename);
    wxString GetFilename(ClFileId fId);
    ClTokenId GetTokenId(const wxString& identifier, ClFileId fId, unsigned tokenHash); // returns wxNOT_FOUND on failure
    ClTokenId InsertToken(const wxString& identifier, const ClAbstractToken& token); // duplicate tokens are discarded
    ClAbstractToken GetToken(ClTokenId tId);
    std::vector<ClTokenId> GetTokenMatches(const wxString& identifier);
    std::vector<ClTokenId> GetFileTokens(ClFileId fId);

    void Shrink();

private:

    ClTreeMap<ClAbstractToken>* m_pTokens;
    ClTreeMap<int>* m_pFileTokens;
    ClTreeMap<wxString>* m_pFilenames;
    wxMutex m_Mutex;
};

#endif // TOKENDATABASE_H
