/** @file     reparselib.cpp
 *  @brief    A library for working with NTFS Reparse Points
 *  @author   amdf
 *  @version  0.1
 *  @date     May 2011
 */

#include "stdafx.h"
#include <ks.h> // because of GUID_NULL
#include "reparselib.h"

static HANDLE OpenFileForWrite(IN LPCWSTR sFileName, IN BOOL bBackup)
{
  return CreateFile(
    sFileName, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
    (bBackup)
    ? FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS
    : FILE_FLAG_OPEN_REPARSE_POINT, 0);
}

static HANDLE OpenFileForRead(IN LPCWSTR sFileName, IN BOOL bBackup)
{
  return CreateFile(
    sFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
    (bBackup)
    ? FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS
    : FILE_FLAG_OPEN_REPARSE_POINT, 0);
}

/**
 *  @brief      Checks an existence of a Reparse Point of a specified file
 *  @param[in]  sFileName File name
 *  @return     TRUE if exists, FALSE otherwise
 */
REPARSELIB_API BOOL ReparsePointExists(IN LPCWSTR sFileName)
{
  return (GetFileAttributes(sFileName) & FILE_ATTRIBUTE_REPARSE_POINT);
}

/**
 *  @brief      Get a GUID field of a reparse point
 *  @param[in]  sFileName File name
 *  @param[out] pGuid Pointer to GUID
 *  @return     TRUE if success
 */
REPARSELIB_API BOOL GetReparseGUID(IN LPCWSTR sFileName, OUT GUID* pGuid)
{  
  DWORD dwRet;
  HANDLE hSrc;
  BOOL bResult = FALSE;

  if (NULL == pGuid)
  {
    return FALSE;
  }

  if (!ReparsePointExists(sFileName))
  {
    return FALSE;
  }

  hSrc = OpenFileForRead(sFileName,
    (GetFileAttributes(sFileName) & FILE_ATTRIBUTE_DIRECTORY));

  if (hSrc == INVALID_HANDLE_VALUE)
  {
    return bResult;
  }
  
  PREPARSE_GUID_DATA_BUFFER rd
    = (PREPARSE_GUID_DATA_BUFFER) GlobalAlloc(GPTR, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);

  if (DeviceIoControl(hSrc, FSCTL_GET_REPARSE_POINT,
    NULL, 0, rd, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &dwRet, NULL))
  {
    *pGuid = rd->ReparseGuid;
    bResult = TRUE;
  }

  CloseHandle(hSrc);
  GlobalFree(rd);

  return bResult;
}

/**
 *  @brief      Get a reparse tag of a reparse point
 *  @param[in]  sFileName File name
 *  @param[out] pTag Pointer to reparse tag
 *  @return     Reparse tag or 0 if fails
 */
REPARSELIB_API BOOL GetReparseTag(IN LPCWSTR sFileName, OUT DWORD* pTag)
{  
  DWORD dwRet;
  HANDLE hSrc;
  BOOL bResult = FALSE;

  if (NULL == pTag)
  {
    return FALSE;
  }

  if (!ReparsePointExists(sFileName))
  {
    return FALSE;
  }

  hSrc = OpenFileForRead(sFileName,
    (GetFileAttributes(sFileName) & FILE_ATTRIBUTE_DIRECTORY));

  if (hSrc == INVALID_HANDLE_VALUE)
  {
    return bResult;
  }
  
  PREPARSE_GUID_DATA_BUFFER rd
    = (PREPARSE_GUID_DATA_BUFFER) GlobalAlloc(GPTR, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);

  if (DeviceIoControl(hSrc, FSCTL_GET_REPARSE_POINT,
    NULL, 0, rd, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &dwRet, NULL))
  {
    *pTag = rd->ReparseTag;
    bResult = TRUE;
  }

  CloseHandle(hSrc);
  GlobalFree(rd);
  return bResult;
}

/**
 *  @brief      Delete a reparse point
 *  @param[in]  sFileName File name
 *  @return     TRUE if success, FALSE otherwise
 */
REPARSELIB_API BOOL DeleteReparsePoint(IN LPCWSTR sFileName)
{
  PREPARSE_GUID_DATA_BUFFER rd;
  BOOL bResult;
  GUID gu;
  DWORD dwRet, dwReparseTag;

  if (!ReparsePointExists(sFileName) || !GetReparseGUID(sFileName, &gu))
  {
    return FALSE;
  }

  rd = (PREPARSE_GUID_DATA_BUFFER)
    GlobalAlloc(GPTR, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE);  

  if (GetReparseTag(sFileName, &dwReparseTag))
  {
    rd->ReparseTag = dwReparseTag;
  } else
  {
    //! The routine cannot delete a reparse point without determining it's reparse tag
    GlobalFree(rd);
    return FALSE;
  }

  HANDLE hDel = OpenFileForWrite(sFileName,
    (GetFileAttributes(sFileName) & FILE_ATTRIBUTE_DIRECTORY));

  if (hDel == INVALID_HANDLE_VALUE)
  {
    return FALSE;
  }  

  // Try to delete a system type of the reparse point (without GUID)
  bResult = DeviceIoControl(hDel, FSCTL_DELETE_REPARSE_POINT,
      rd, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, NULL, 0,
      &dwRet, NULL);

  if (!bResult)
  {
    // Set up the GUID
    rd->ReparseGuid = gu;

    // Try to delete with GUID
    bResult = DeviceIoControl(hDel, FSCTL_DELETE_REPARSE_POINT,
        rd, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, NULL, 0,
        &dwRet, NULL);    
  }

  GlobalFree(rd);
  CloseHandle(hDel);
  return bResult;  
}

/**
 *  @brief      Creates a custom reparse point
 *  @param[in]  sFileName   File name
 *  @param[in]  pBuffer     Reparse point content
 *  @param[in]  uBufSize    Size of the content
 *  @param[in]  uGuid       Reparse point GUID
 *  @param[in]  uReparseTag Reparse point tag
 *  @return     TRUE if success, FALSE otherwise
 */
REPARSELIB_API BOOL CreateCustomReparsePoint
(
  IN LPCWSTR  sFileName,
  IN PVOID    pBuffer,
  IN UINT     uBufSize,
  IN GUID     uGuid,
  IN ULONG    uReparseTag
)
{ 
  DWORD dwLen = 0;
  BOOL bResult = FALSE;

  if (NULL == pBuffer || 0 == uBufSize || uBufSize > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
  {
    return bResult;
  }

  HANDLE hHandle = OpenFileForWrite(sFileName,
    (GetFileAttributes(sFileName) & FILE_ATTRIBUTE_DIRECTORY));

  if (INVALID_HANDLE_VALUE == hHandle)
  {
    return bResult;
  }

  PREPARSE_GUID_DATA_BUFFER rd
    = (PREPARSE_GUID_DATA_BUFFER) GlobalAlloc(GPTR, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
  
  rd->ReparseTag = uReparseTag;
  rd->ReparseDataLength = uBufSize;
  rd->Reserved = 0;
  rd->ReparseGuid = uGuid;

  memcpy(rd->GenericReparseBuffer.DataBuffer, pBuffer, uBufSize);

  if (DeviceIoControl(hHandle, FSCTL_SET_REPARSE_POINT, rd,
    rd->ReparseDataLength + REPARSE_GUID_DATA_BUFFER_HEADER_SIZE,
    NULL, 0, &dwLen, NULL))
  {
    bResult = TRUE;
  }

  CloseHandle(hHandle);
  GlobalFree(rd);
  
  return bResult;
}