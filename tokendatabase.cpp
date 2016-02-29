/*
 * Database responsible for resolving tokens between translation units
 */

#include "tokendatabase.h"

#include <wx/filename.h>
#include <wx/string.h>
#include <iostream>
#include <wx/mstream.h>

#include "treemap.h"
#include "cclogger.h"

enum
{
    ClTokenPacketType_filenames = 1<<0,
    ClTokenPacketType_tokens = 1<<1
};

static bool WriteInt(wxOutputStream& out, const int val)
{
    out.Write((const void*)&val, sizeof(val));
    return true;
}

static bool  WriteLongLong(wxOutputStream& out, const long long val)
{
    out.Write((const void*)&val, sizeof(val));
    return true;
}

static bool WriteString(wxOutputStream& out, const char* str)
{
    int len = 0;

    if (str != nullptr)
        len = strlen(str); // Need size in amount of bytes
    if (!WriteInt(out, len))
        return false;
    if (len > 0)
        out.Write((const void*)str, len);
    return true;
}

static bool ReadInt(wxInputStream& in, int& out_Int)
{
    int val = 0;
    if (!in.CanRead())
        return false;
    in.Read(&val, sizeof(val));
    out_Int = val;

    return true;
}

static bool ReadLongLong(wxInputStream& in, long long& out_LongLong)
{
    long long val = 0;
    if (!in.CanRead())
        return false;
    in.Read(&val, sizeof(val));
    out_LongLong = val;

    return true;
}

static bool ReadString(wxInputStream& in, wxString& out_String)
{
    int len;
    if (!ReadInt(in, len))
        return false;
    if (len == 0)
    {
        out_String = out_String.Truncate(0);
        return true;
    }
    if (!in.CanRead())
        return false;
    char buffer[len + 1];

    //in.Read( wxStringBuffer(buffer, len), len );
    in.Read(buffer, len);
    buffer[len] = '\0';

    out_String = wxString::FromUTF8(buffer);

    return true;
}

bool ClAbstractToken::WriteOut(const ClAbstractToken& token,  wxOutputStream& out)
{
    // This is a cached database so we don't care about endianness for now. Who will ever copy these from one platform to another?
    WriteInt(out, token.tokenType);
    WriteInt(out, token.fileId);
    WriteInt(out, token.location.line);
    WriteInt(out, token.location.column);
    WriteString(out, token.identifier.mb_str());
    WriteString(out, token.displayName.mb_str());
    WriteString(out, token.scopeName.mb_str());
    WriteInt(out, token.tokenHash);
    return true;
}

bool ClAbstractToken::ReadIn(ClAbstractToken& token, wxInputStream& in)
{
    int val = 0;
    if (!ReadInt(in, val))
        return false;
    token.tokenType = (ClTokenType)val;
    if (!ReadInt(in, val))
        return false;
    token.fileId = val;
    if (!ReadInt(in, val))
        return false;
    token.location.line = val;
    if (!ReadInt(in, val))
        return false;
    token.location.column = val;
    if (!ReadString(in, token.identifier))
        return false;
    if (! ReadString(in, token.displayName))
        return false;
    if (!ReadString(in, token.scopeName))
        return false;
    if (!ReadInt(in, val))
        return false;
    token.tokenHash = val;
    return true;
}

ClFilenameDatabase::ClFilenameDatabase() :
    m_pFileEntries(new ClTreeMap<ClFilenameEntry>())
{
}

ClFilenameDatabase::~ClFilenameDatabase()
{
    delete m_pFileEntries;
}

bool ClFilenameDatabase::WriteOut(const ClFilenameDatabase& db, wxOutputStream& out)
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int cnt = db.m_pFileEntries->GetCount();
    WriteInt(out, cnt);
    for (i = 0; i < cnt; ++i)
    {
        ClFilenameEntry entry = db.m_pFileEntries->GetValue((ClFileId)i);
        if (!WriteString(out, entry.filename.mb_str()))
            return false;
        if (!WriteLongLong(out, entry.timestamp.GetValue().GetValue()))
            return false;
    }
    return true;
}

bool ClFilenameDatabase::ReadIn(ClFilenameDatabase& db, wxInputStream& in)
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int packetCount = 0;
    if (!ReadInt(in, packetCount))
        return false;
    for (i = 0; i < packetCount; ++i)
    {
        wxString filename;
        if (!ReadString(in, filename))
            return false;
        long long ts = 0;
        if (!ReadLongLong(in, ts))
            return false;
        db.m_pFileEntries->Insert(filename, ClFilenameEntry(filename, wxDateTime(wxLongLong(ts))));
    }
    return true;
}

ClFileId ClFilenameDatabase::GetFilenameId(const wxString& filename) const
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pFileEntries);
    wxFileName fln(filename.c_str());
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
    const wxString& normFile = fln.GetFullPath(wxPATH_UNIX);
    std::vector<int> id = m_pFileEntries->GetIdSet(normFile);
    if (id.empty())
    {
        wxString f = wxString(normFile.c_str());
        wxDateTime ts; // Timestamp updated when file was parsed into the token database.
        ClFilenameEntry entry(f,ts);
        return m_pFileEntries->Insert(f, entry);
    }
    return id.front();
}

wxString ClFilenameDatabase::GetFilename(const ClFileId fId) const
{
    wxMutexLocker lock(m_Mutex);

    assert(m_pFileEntries->HasValue(fId));

    ClFilenameEntry entry = m_pFileEntries->GetValue(fId);
    const wxChar* val = entry.filename.c_str();
    if (val == nullptr)
        return wxEmptyString;

    return wxString(val);
}

const wxDateTime ClFilenameDatabase::GetFilenameTimestamp(const ClFileId fId) const
{
    wxMutexLocker lock(m_Mutex);

    assert(m_pFileEntries->HasValue(fId));

    ClFilenameEntry entry = m_pFileEntries->GetValue(fId);
    return entry.timestamp;
}

void ClFilenameDatabase::UpdateFilenameTimestamp(const ClFileId fId, const wxDateTime& timestamp)
{
    wxMutexLocker lock(m_Mutex);

    assert(m_pFileEntries->HasValue(fId));

    ClFilenameEntry& entryRef = m_pFileEntries->GetValue(fId);
    entryRef.timestamp = timestamp;
}

ClTokenDatabase::ClTokenDatabase(ClFilenameDatabase& fileDB) :
        m_FileDB(fileDB),
        m_pTokens(new ClTreeMap<ClAbstractToken>()),
        m_pFileTokens(new ClTreeMap<int>()),
        m_Mutex(wxMUTEX_RECURSIVE)
{
}

ClTokenDatabase::ClTokenDatabase(const ClTokenDatabase& other) :
    m_FileDB(other.m_FileDB),
    m_pTokens(new ClTreeMap<ClAbstractToken>(*other.m_pTokens)),
    //m_pFileEntries(new ClTreeMap<ClFileEntry>(*other.m_pFileEntries)),
    m_pFileTokens(new ClTreeMap<int>(*other.m_pFileTokens)),
    m_Mutex(wxMUTEX_RECURSIVE)
{

}

ClTokenDatabase::~ClTokenDatabase()
{
    delete m_pTokens;
}

void swap(ClTokenDatabase& first, ClTokenDatabase& second)
{
    using std::swap;

    // Let's assume no inverse swap will be performed for now
    wxMutexLocker l1(first.m_Mutex);
    wxMutexLocker l2(second.m_Mutex);

    swap(*first.m_pTokens, *second.m_pTokens);
    swap(*first.m_pFileTokens, *second.m_pFileTokens);
}


bool ClTokenDatabase::ReadIn(ClTokenDatabase& tokenDatabase, wxInputStream& in)
{
    in.SeekI(4); // Magic number
    int version = 0;
    if (!ReadInt(in, version))
        return false;
    int i = 0;
    if (version != 0x01)
    {
        return false;
    }
    tokenDatabase.Clear();
    int read_count = 0;

    wxMutexLocker(tokenDatabase.m_Mutex);
    while (in.CanRead())
    {
        int packetType = 0;
        if (!ReadInt(in, packetType))
            return false;
        switch (packetType)
        {
        case ClTokenPacketType_filenames:
            if (!ClFilenameDatabase::ReadIn(tokenDatabase.m_FileDB, in))
                return false;
            break;
        case ClTokenPacketType_tokens:
            int packetCount = 0;
            if (!ReadInt(in, packetCount))
                return false;
            for (i = 0; i < packetCount; ++i)
            {
                ClAbstractToken token;
                if (!ClAbstractToken::ReadIn(token, in))
                    return false;
                if (token.fileId != -1)
                {
                    //ClTokenId tokId =
                    tokenDatabase.InsertToken(token);
                    //fprintf( stdout, " '%s' / '%s' / fId=%d location=%d:%d hash=%d dbEntryId=%d\n", (const char*)token.identifier.mb_str(), (const char*)token.displayName.mbc_str(), token.fileId, token.location.line, token.location.column,  token.tokenHash, tokId );
                    read_count++;
                }
            }
            break;
        }
    }
    return true;
}

bool ClTokenDatabase::WriteOut(const ClTokenDatabase& tokenDatabase, wxOutputStream& out)
{
    int i;
    int cnt;
    out.Write("CbCc", 4); // Magic number
    WriteInt(out, 1); // Version number

    WriteInt(out, ClTokenPacketType_filenames);
    if (!ClFilenameDatabase::WriteOut(tokenDatabase.m_FileDB, out))
        return false;

    wxMutexLocker(tokenDatabase.m_Mutex);

    WriteInt(out, ClTokenPacketType_tokens);
    cnt = tokenDatabase.m_pTokens->GetCount();

    WriteInt(out, cnt);
    uint32_t written_count = 0;
    for (i = 0; i < cnt; ++i)
    {
        ClAbstractToken tok = tokenDatabase.m_pTokens->GetValue(i);
        if (!ClAbstractToken::WriteOut(tok, out))
            return false;
        written_count++;
    }
    CCLogger::Get()->DebugLog(F(_T("Wrote token database: %d tokens"), written_count));
    return true;
}

void ClTokenDatabase::Clear()
{
    wxMutexLocker lock(m_Mutex);
    delete m_pTokens;
    delete m_pFileTokens;
    m_pTokens = new ClTreeMap<ClAbstractToken>(),
    m_pFileTokens = new ClTreeMap<int>();
}
ClFileId ClTokenDatabase::GetFilenameId(const wxString& filename) const
{
    return m_FileDB.GetFilenameId(filename);
}
wxString ClTokenDatabase::GetFilename(ClFileId fId) const
{
    return m_FileDB.GetFilename(fId);
}

wxDateTime ClTokenDatabase::GetFilenameTimestamp(const ClFileId fId) const
{
    return m_FileDB.GetFilenameTimestamp(fId);
}

ClTokenId ClTokenDatabase::InsertToken(const ClAbstractToken& token)
{
    wxMutexLocker lock(m_Mutex);

    ClTokenId tId = GetTokenId(token.identifier, token.fileId, token.tokenType, token.tokenHash);
    if (tId == wxNOT_FOUND)
    {
        tId = m_pTokens->Insert(wxString(token.identifier), token);
        wxString filen = wxString::Format(wxT("%d"), token.fileId);
        m_pFileTokens->Insert(filen, tId);
    }
    return tId;
}

ClTokenId ClTokenDatabase::GetTokenId(const wxString& identifier, ClFileId fileId, ClTokenType tokenType, unsigned tokenHash) const
{
    wxMutexLocker lock(m_Mutex);
    std::vector<int> ids = m_pTokens->GetIdSet(identifier);
    for (std::vector<int>::const_iterator itr = ids.begin();
            itr != ids.end(); ++itr)
    {
        if (m_pTokens->HasValue(*itr))
        {
            ClAbstractToken tok = m_pTokens->GetValue(*itr);
            if (   (tok.tokenHash == tokenHash)
                && ((tok.tokenType == tokenType) || (tokenType == ClTokenType_Unknown))
                && ((tok.fileId == fileId) || (fileId == wxNOT_FOUND)) )
            {

                return *itr;
            }
        }
    }
    return wxNOT_FOUND;
}

ClAbstractToken ClTokenDatabase::GetToken(const ClTokenId tId) const
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pTokens->HasValue(tId));
    return m_pTokens->GetValue(tId);
}

std::vector<ClTokenId> ClTokenDatabase::GetTokenMatches(const wxString& identifier) const
{
    wxMutexLocker lock(m_Mutex);
    return m_pTokens->GetIdSet(identifier);
}

std::vector<ClTokenId> ClTokenDatabase::GetFileTokens(const ClFileId fId) const
{
    wxMutexLocker lock(m_Mutex);
    wxString key = wxString::Format(wxT("%d"), fId);
    std::vector<ClTokenId> tokens = m_pFileTokens->GetIdSet(key);

    return tokens;
}

void ClTokenDatabase::Shrink()
{
    wxMutexLocker lock(m_Mutex);
    m_pTokens->Shrink();
    m_pFileTokens->Shrink();
}

void ClTokenDatabase::UpdateToken(const ClTokenId freeTokenId, const ClAbstractToken& token)
{
    ClAbstractToken tokenRef = m_pTokens->GetValue(freeTokenId);
    m_pTokens->RemoveIdKey(tokenRef.identifier, freeTokenId);
    assert((tokenRef.fileId == wxNOT_FOUND) && "Only an unused token can be updated");
    tokenRef.displayName = token.displayName;
    tokenRef.fileId = token.fileId;
    tokenRef.identifier = token.identifier;
    tokenRef.location = token.location;
    tokenRef.scopeName = token.scopeName;
    tokenRef.tokenHash = token.tokenHash;
    tokenRef.tokenType = token.tokenType;
    wxString filen = wxString::Format(wxT("%d"), token.fileId);
    m_pFileTokens->Insert(filen, freeTokenId);
}

void ClTokenDatabase::RemoveToken(const ClTokenId tokenId)
{
    ClAbstractToken oldToken = GetToken(tokenId);
    wxString key = wxString::Format(wxT("%d"), oldToken.fileId);
    m_pFileTokens->Remove(key, tokenId);
    ClAbstractToken t;
    // We just invalidate it here. Real removal is rather complex
    UpdateToken(tokenId, t);
}

void ClTokenDatabase::Update(const ClFileId fileId, const ClTokenDatabase& db)
{
    int i;
    std::vector<ClTokenId> oldTokenIds;
    wxString filename = GetFilename(fileId);
    wxFileName fln(filename);
    wxDateTime timestamp = fln.GetModificationTime();
    {
        wxMutexLocker lock(m_Mutex);
        oldTokenIds = GetFileTokens(fileId);
        int cnt = db.m_pTokens->GetCount();
        for (i = 0; i < cnt; ++i)
        {
            ClAbstractToken tok = db.m_pTokens->GetValue(i);
            ClTokenId tokId = InsertToken(tok);
            for(std::vector<ClTokenId>::iterator it = oldTokenIds.begin(); it != oldTokenIds.end(); ++it)
            {
                if (*it == tokId)
                {
                    it = oldTokenIds.erase(it);
                    break;
                }
            }
        }
        for (std::vector<ClTokenId>::iterator it = oldTokenIds.begin(); it != oldTokenIds.end(); ++it)
        {
            ClAbstractToken tok = db.m_pTokens->GetValue(*it);
            CCLogger::Get()->DebugLog(F(_T("Removing token %s/%s type=%d from database, "), (const char*)tok.displayName.mb_str(), (const char*)filename.mb_str(), (int)tok.tokenType));
            RemoveToken( *it );
        }
    }
    m_FileDB.UpdateFilenameTimestamp(fileId, timestamp);
}
