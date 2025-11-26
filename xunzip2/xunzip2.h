//-------------------------------------------------------------------------------------------------
// File: xunzip2.h
//
// xUnzip2 public header
//-------------------------------------------------------------------------------------------------

#ifndef __XUNZIP2_H__
#define __XUNZIP2_H__

bool xunzipFromFile(const char * pszSource, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite = true);
bool xunzipFromMemory(void *pData, int iDataSize, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite = true);
bool xunzipFromXBESection(const char * pszSectionName, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite = true);

#endif // __XUNZIP2_H__
