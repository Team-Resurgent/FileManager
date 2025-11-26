#ifndef __ZIPARCHIVE_H__
#define __ZIPARCHIVE_H__

#include <string>
#include <unzipLIB.h>

//src-dev -- Give this public access
char* strreplall(char* Str, size_t BufSiz, char* OldStr, char* NewStr);

// Callbacks
void * zipFile_Open(const char *filename, int32_t *size);
void zipFile_Close(void *p);
int32_t zipFile_Read(void *p, uint8_t *buffer, int32_t length);
int32_t zipFile_Seek(void *p, int32_t position, int iType);

class CZipArchive {
    public:
					CZipArchive(void);
					~CZipArchive(void);
	
	bool			ExtractFromFile(const char * pszSource, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite);
	bool			ExtractFromMemory(uint8_t *pData, int iDataSize, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite);
	
	//src-dev -- Give this public access
	int				ExtractCurrentFile(UNZIP& zip, const char* pszDestinationFolder, const bool bUseFolderNames, bool bOverwrite);

private:

	int				ExtractZip(UNZIP& zip, const char * pszDestinationFolder, const bool bUseFolderNames, const bool bOverwrite);
	//int				ExtractCurrentFile(UNZIP& zip, const char * pszDestinationFolder, const bool bUseFolderNames, bool bOverwrite);

	void		  * m_pUnZipBuffer;
	unsigned int	m_uiUnZipBufferSize;
};
#endif // __ZIPARCHIVE_H__
