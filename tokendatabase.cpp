/*
 * Database responsible for resolving tokens between translation units
 */

#include "tokendatabase.h"

#include <wx/filename.h>
#include <wx/string.h>
#include <iostream>

#include "treemap.h"

ClTokenDatabase::ClTokenDatabase() :
    m_pTokens(new ClTreeMap<ClAbstractToken>()),
    m_pFileTokens(new ClTreeMap<int>()),
    m_pFilenames(new ClTreeMap<wxString>()),
    m_Mutex(wxMUTEX_RECURSIVE)
{
}

ClTokenDatabase::~ClTokenDatabase()
{
    delete m_pFilenames;
    delete m_pTokens;
}

ClFileId ClTokenDatabase::GetFilenameId(const wxString& filename)
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pFilenames);
    assert(m_pTokens);
    wxFileName fln(filename.c_str());
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
    const wxString& normFile = fln.GetFullPath(wxPATH_UNIX);
    std::vector<int> id = m_pFilenames->GetIdSet(normFile);
    if (id.empty())
    {
        wxString f = wxString(normFile.c_str());
        ClFileId id = m_pFilenames->Insert( f, f );
        //fprintf(stdout,"%s this=%p Storing %s(%p) as %d\n", __PRETTY_FUNCTION__, (void*)this, (const char*)f.mb_str(), (void*)f.c_str(), (int)id );
        return id;
    }
    return id.front();
}

wxString ClTokenDatabase::GetFilename(ClFileId fId)
{
    wxMutexLocker lock( m_Mutex);

    assert(m_pFilenames->HasValue(fId));
    //if (!m_pFilenames->HasValue(fId))
    //    return wxString();

    const wxChar* val = m_pFilenames->GetValue(fId).c_str();
    if (val == NULL)
        return wxString();

    //fprintf(stdout, "%s this=%p id=%d Returning %p", __PRETTY_FUNCTION__, (void*)this, (int)fId, (void*)val );
    return wxString(val);
}

ClTokenId ClTokenDatabase::InsertToken(const wxString& identifier, const ClAbstractToken& token)
{
    wxMutexLocker lock(m_Mutex);

    ClTokenId tId = GetTokenId(identifier, token.fileId, token.tokenHash);
    if (tId == wxNOT_FOUND)
    {
        tId = m_pTokens->Insert(wxString(identifier.c_str()), token);
        wxString filen = wxString::Format(wxT("%d"), token.fileId);
        m_pFileTokens->Insert(filen, tId);
        if( token.fileId == 0 )
            fprintf(stdout,"FileTokens: %d\n", (int)m_pFileTokens->GetIdSet(filen).size());
    }
    return tId;
}

ClTokenId ClTokenDatabase::GetTokenId(const wxString& identifier, ClFileId fileId, unsigned tokenHash)
{
    wxMutexLocker lock( m_Mutex);
    std::vector<int> ids = m_pTokens->GetIdSet(identifier);
    for (std::vector<int>::const_iterator itr = ids.begin();
            itr != ids.end(); ++itr)
    {
        if (m_pTokens->HasValue(*itr))
        {
            ClAbstractToken tok = m_pTokens->GetValue(*itr);
            if( (tok.tokenHash == tokenHash)&&((tok.fileId == fileId)||(fileId == wxNOT_FOUND)) )
                return *itr;
        }
    }
    return wxNOT_FOUND;
}

ClAbstractToken ClTokenDatabase::GetToken(ClTokenId tId)
{
    wxMutexLocker lock( m_Mutex);
    assert( m_pTokens->HasValue(tId) );
    return m_pTokens->GetValue(tId);
}

std::vector<ClTokenId> ClTokenDatabase::GetTokenMatches(const wxString& identifier)
{
    wxMutexLocker lock( m_Mutex);
    return m_pTokens->GetIdSet(identifier);
}

std::vector<ClTokenId> ClTokenDatabase::GetFileTokens(ClFileId fId)
{
    wxMutexLocker lock( m_Mutex);
    wxString key = wxString::Format(wxT("%d"), fId);
    std::vector<ClTokenId> tokens = m_pFileTokens->GetIdSet(key);

    return tokens;
}

void ClTokenDatabase::Shrink()
{
    wxMutexLocker lock(m_Mutex);
    m_pFilenames->Shrink();
    m_pTokens->Shrink();
    m_pFileTokens->Shrink();
}
