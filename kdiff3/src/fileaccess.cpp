/***************************************************************************
 *   Copyright (C) 2003 by Joachim Eibl                                    *
 *   joachim.eibl@gmx.de                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "fileaccess.h"
#include <iostream>
#include <kio/global.h>
#include <kmessagebox.h>
#include "optiondialog.h"
#include <qlayout.h>
#include <qlabel.h>
#include <qapplication.h>
#if QT_VERSION==230
#else
#include <qeventloop.h>
#endif
#include "common.h"
#include <ktempfile.h>
#include <qdir.h>
#include <qregexp.h>
#include <qtextstream.h>
#include <vector>
#include <klocale.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <sys/utime.h>
#include <io.h>
#else
#include <unistd.h>          // Needed for creating symbolic links via symlink().
#include <utime.h>
#endif


ProgressDialog* g_pProgressDialog;


FileAccess::FileAccess( const QString& name, bool bWantToWrite )
{
   setFile( name, bWantToWrite );
}

FileAccess::FileAccess()
{
   m_bValidData = false;
   m_size = 0;
   m_creationTime = QDateTime();
   m_accessTime = QDateTime();
   m_modificationTime = QDateTime();
   m_bReadable = false;
   m_bWritable = false;
   m_bExecutable = false;
   m_bLocal = false;
   m_bHidden = false;
   m_bExists = false;
   m_bFile = false;
   m_bDir  = false;
   m_bSymLink = false;
}

FileAccess::~FileAccess()
{
   if( !m_localCopy.isEmpty() )
   {
      removeFile( m_localCopy );
   }
}

void FileAccess::setFile( const QString& name, bool bWantToWrite )
{
   m_url = KURL::fromPathOrURL( name );
   m_bValidData = false;

   m_size = 0;
   m_creationTime = QDateTime();
   m_accessTime = QDateTime();
   m_modificationTime = QDateTime();
   m_bReadable = false;
   m_bWritable = false;
   m_bExecutable = false;
   m_bHidden = false;
   m_bExists = false;
   m_bFile = false;
   m_bDir  = false;
   m_bSymLink = false;
   m_linkTarget = "";
   m_fileType = -1;

   // Note: Checking if the filename-string is empty is necessary for Win95/98/ME.
   //       The isFile() / isDir() queries would cause the program to crash.
   //       (This is a Win95-bug which has been corrected only in WinNT/2000/XP.)
   if ( !name.isEmpty() )
   {
      if ( m_url.isLocalFile() || !m_url.isValid() ) // assuming that malformed means relative
      {
         QString localName = name;
         if ( m_url.isLocalFile() && name.left(5).lower()=="file:" )
         {
            localName = m_url.path(); // I want the path without preceding "file:"
         }
         QFileInfo fi( localName );
         m_bReadable    = fi.isReadable();
         m_bWritable    = fi.isWritable();
         m_bExecutable  = fi.isExecutable();
#if QT_VERSION==230
         m_creationTime = fi.lastModified();
         m_bHidden    = false;
#else
         m_creationTime = fi.created();
         m_bHidden    = fi.isHidden();
#endif
         m_modificationTime = fi.lastModified();
         m_accessTime = fi.lastRead();
         m_size       = fi.size();
         m_bSymLink   = fi.isSymLink();
         m_bFile      = fi.isFile();
         m_bDir       = fi.isDir();
         m_bExists    = fi.exists();
         m_name       = fi.fileName();
         m_path       = fi.filePath();
         m_absFilePath= fi.absFilePath();
         if ( m_bSymLink ) m_linkTarget = fi.readLink();
         m_bLocal = true;
         m_bValidData = true;
         if ( ! m_url.isValid() )
         {
            m_url.setPath( m_absFilePath );
         }
      }
      else
      {
         m_absFilePath = name;
         m_name   = m_url.fileName();
         m_bLocal = false;

         FileAccessJobHandler jh( this ); // A friend, which writes to the parameters of this class!
         jh.stat(2/*all details*/, bWantToWrite); // returns bSuccess, ignored

         m_path = name;
      }
   }
}

void FileAccess::addPath( const QString& txt )
{
   m_url.addPath( txt );
   setFile( m_url.url() );  // reinitialise
}

/*     Filetype:
       S_IFMT     0170000   bitmask for the file type bitfields
       S_IFSOCK   0140000   socket
       S_IFLNK    0120000   symbolic link
       S_IFREG    0100000   regular file
       S_IFBLK    0060000   block device
       S_IFDIR    0040000   directory
       S_IFCHR    0020000   character device
       S_IFIFO    0010000   fifo
       S_ISUID    0004000   set UID bit
       S_ISGID    0002000   set GID bit (see below)
       S_ISVTX    0001000   sticky bit (see below)

       Access:
       S_IRWXU    00700     mask for file owner permissions
       S_IRUSR    00400     owner has read permission
       S_IWUSR    00200     owner has write permission
       S_IXUSR    00100     owner has execute permission
       S_IRWXG    00070     mask for group permissions
       S_IRGRP    00040     group has read permission
       S_IWGRP    00020     group has write permission
       S_IXGRP    00010     group has execute permission
       S_IRWXO    00007     mask for permissions for others (not in group)
       S_IROTH    00004     others have read permission
       S_IWOTH    00002     others have write permisson
       S_IXOTH    00001     others have execute permission
*/

void FileAccess::setUdsEntry( const KIO::UDSEntry& e )
{
#ifndef KREPLACEMENTS_H
   KIO::UDSEntry::const_iterator ei;
   long acc = 0;
   long fileType = 0;
   for( ei=e.begin(); ei!=e.end(); ++ei )
   {
      const KIO::UDSAtom& a = *ei;
      switch( a.m_uds )
      {
         case KIO::UDS_SIZE :              m_size   = a.m_long;   break;
         case KIO::UDS_USER :              m_user   = a.m_str;    break;
         case KIO::UDS_GROUP :             m_group  = a.m_str;    break;
         case KIO::UDS_NAME :              m_path   = a.m_str;    break;  // During listDir the relative path is given here.
         case KIO::UDS_MODIFICATION_TIME : m_modificationTime.setTime_t( a.m_long ); break;
         case KIO::UDS_ACCESS_TIME :       m_accessTime.setTime_t( a.m_long ); break;
         case KIO::UDS_CREATION_TIME :     m_creationTime.setTime_t( a.m_long ); break;
         case KIO::UDS_LINK_DEST :         m_linkTarget       = a.m_str; break;
         case KIO::UDS_ACCESS :
         {
            acc = a.m_long;
            m_bReadable   = (acc & S_IRUSR)!=0;
            m_bWritable   = (acc & S_IWUSR)!=0;
            m_bExecutable = (acc & S_IXUSR)!=0;
            break;
         }
         case KIO::UDS_FILE_TYPE :
         {
            fileType = a.m_long;
            m_bDir     = ( fileType & S_IFMT ) == S_IFDIR;
            m_bFile    = ( fileType & S_IFMT ) == S_IFREG;
            m_bSymLink = ( fileType & S_IFMT ) == S_IFLNK;
            m_bExists  = fileType != 0;
            m_fileType = fileType;
            break;
         }

         case KIO::UDS_URL :               // m_url = KURL( a.str );
                                           break;
         case KIO::UDS_MIME_TYPE :         break;
         case KIO::UDS_GUESSED_MIME_TYPE : break;
         case KIO::UDS_XML_PROPERTIES :    break;
         default: break;
      }
   }

   m_bExists = acc!=0 || fileType!=0;

   m_bLocal = false;
   m_bValidData = true;
   m_bSymLink = !m_linkTarget.isEmpty();
   if ( m_name.isEmpty() )
   {
      int pos = m_path.findRev('/') + 1;
      m_name = m_path.mid( pos );
   }
   m_bHidden = m_name[0]=='.';
#endif
}


bool FileAccess::isValid() const       {   return m_bValidData;  }
bool FileAccess::isFile() const        {   return m_bFile;       }
bool FileAccess::isDir() const         {   return m_bDir;        }
bool FileAccess::isSymLink() const     {   return m_bSymLink;    }
bool FileAccess::exists() const        {   return m_bExists;     }
long FileAccess::size() const          {   return m_size;        }
KURL FileAccess::url() const           {   return m_url;         }
bool FileAccess::isLocal() const       {   return m_bLocal;      }
bool FileAccess::isReadable() const    {   return m_bReadable;   }
bool FileAccess::isWritable() const    {   return m_bWritable;   }
bool FileAccess::isExecutable() const  {   return m_bExecutable; }
bool FileAccess::isHidden() const      {   return m_bHidden;     }
QString FileAccess::readLink() const   {   return m_linkTarget;  }
QString FileAccess::absFilePath() const{   return m_absFilePath; }  // Full abs path
QString FileAccess::fileName() const   {   return m_name;        }  // Just the name-part of the path, without parent directories
QString FileAccess::filePath() const   {   return m_path;        }  // The path-string that was used during construction
QString FileAccess::prettyAbsPath() const { return isLocal() ? m_absFilePath : m_url.prettyURL(); }

QDateTime FileAccess::created() const
{
   return ( m_creationTime.isValid() ?  m_creationTime : m_modificationTime );
}

QDateTime FileAccess::lastModified() const
{
   return m_modificationTime;
}

QDateTime FileAccess::lastRead() const
{
   return ( m_accessTime.isValid() ?  m_accessTime : m_modificationTime );
}

static bool interruptableReadFile( QFile& f, void* pDestBuffer, unsigned long maxLength )
{
   const unsigned long maxChunkSize = 100000;
   unsigned long i=0;
   while( i<maxLength )
   {
      unsigned long nextLength = min2( maxLength-i, maxChunkSize );
      unsigned long reallyRead = f.readBlock( (char*)pDestBuffer+i, nextLength );
      if ( reallyRead != nextLength )
      {
         return false;
      }
      i+=reallyRead;

      g_pProgressDialog->setSubCurrent( double(i)/maxLength );
      if ( g_pProgressDialog->wasCancelled() ) return false;
   }
   return true;
}

bool FileAccess::readFile( void* pDestBuffer, unsigned long maxLength )
{
   if ( !m_localCopy.isEmpty() )
   {
      QFile f( m_localCopy );
      if ( f.open( IO_ReadOnly ) )
         return interruptableReadFile(f, pDestBuffer, maxLength);// maxLength == f.readBlock( (char*)pDestBuffer, maxLength );
   }
   else if (m_bLocal)
   {
      QFile f( filePath() );

      if ( f.open( IO_ReadOnly ) )
         return interruptableReadFile(f, pDestBuffer, maxLength); //maxLength == f.readBlock( (char*)pDestBuffer, maxLength );
   }
   else
   {
      FileAccessJobHandler jh( this );
      return jh.get( pDestBuffer, maxLength );
   }
   return false;
}

bool FileAccess::writeFile( void* pSrcBuffer, unsigned long length )
{
   if (m_bLocal)
   {
      QFile f( filePath() );
      if ( f.open( IO_WriteOnly ) )
      {
         const unsigned long maxChunkSize = 100000;
         unsigned long i=0;
         while( i<length )
         {
            unsigned long nextLength = min2( length-i, maxChunkSize );
            unsigned long reallyWritten = f.writeBlock( (char*)pSrcBuffer+i, nextLength );
            if ( reallyWritten != nextLength )
            {
               return false;
            }
            i+=reallyWritten;

            g_pProgressDialog->setSubCurrent( double(i)/length );
            if ( g_pProgressDialog->wasCancelled() ) return false;
         }
         return true;
      }
   }
   else
   {
      FileAccessJobHandler jh( this );
      return jh.put( pSrcBuffer, length, true /*overwrite*/ );
   }
   return false;
}

bool FileAccess::copyFile( const QString& dest )
{
   FileAccessJobHandler jh( this );
   return jh.copyFile( dest );   // Handles local and remote copying.
}

bool FileAccess::rename( const QString& dest )
{
   FileAccessJobHandler jh( this );
   return jh.rename( dest );
}

bool FileAccess::removeFile()
{
   if ( isLocal() )
   {
      return QDir().remove( absFilePath() );
   }
   else
   {
      FileAccessJobHandler jh( this );
      return jh.removeFile( absFilePath() );
   }
}

bool FileAccess::removeFile( const QString& name ) // static
{
   return FileAccess(name).removeFile();
}

bool FileAccess::listDir( t_DirectoryList* pDirList, bool bRecursive, bool bFindHidden,
   const QString& filePattern, const QString& fileAntiPattern, const QString& dirAntiPattern,
   bool bFollowDirLinks, bool bUseCvsIgnore )
{
   FileAccessJobHandler jh( this );
   return jh.listDir( pDirList, bRecursive, bFindHidden, filePattern, fileAntiPattern,
                      dirAntiPattern, bFollowDirLinks, bUseCvsIgnore );
}

QString FileAccess::tempFileName()
{
   #ifdef KREPLACEMENTS_H

      QString fileName;
      #ifdef _WIN32
         QString tmpDir = getenv("TEMP");
         for(int i=0; ;++i)
         {
            // short filenames for WIN98 because for system() the command must not exceed 120 characters.
            fileName = tmpDir + "/" + QString::number(i);
            if ( ! FileAccess::exists(fileName) )
               break;
         }
      #else
         QString tmpDir = "/tmp";
         for(int i=0; ;++i)
         {
            fileName = tmpDir + "/kdiff3_" + QString::number(i) +".tmp";
            if ( ! FileAccess::exists(fileName) )
               break;
         }
      #endif

      return QDir::convertSeparators(fileName);

   #else  // using KDE

      KTempFile tmpFile;
      tmpFile.setAutoDelete( true );  // We only want the name. Delete the precreated file immediately.
      return tmpFile.name();

   #endif
}

bool FileAccess::makeDir( const QString& dirName )
{
   FileAccessJobHandler fh(0);
   return fh.mkDir( dirName );
}

bool FileAccess::removeDir( const QString& dirName )
{
   FileAccessJobHandler fh(0);
   return fh.rmDir( dirName );
}

bool FileAccess::symLink( const QString& linkTarget, const QString& linkLocation )
{
   return 0==::symlink( linkTarget.ascii(), linkLocation.ascii() );
   //FileAccessJobHandler fh(0);
   //return fh.symLink( linkTarget, linkLocation );
}

bool FileAccess::exists( const QString& name )
{
   FileAccess fa( name );
   return fa.exists();
}

// If the size couldn't be determined by stat() then the file is copied to a local temp file.
long FileAccess::sizeForReading()
{
   if ( m_size == 0 && !isLocal() )
   {
      // Size couldn't be determined. Copy the file to a local temp place.
      QString localCopy = tempFileName();
      bool bSuccess = copyFile( localCopy );
      if ( bSuccess )
      {
         QFileInfo fi( localCopy );
         m_size = fi.size();
         m_localCopy = localCopy;
         return m_size;
      }
      else
      {
         return 0;
      }
   }
   else
      return m_size;
}

QString FileAccess::getStatusText()
{
   return m_statusText;
}

QString FileAccess::cleanDirPath( const QString& path ) // static
{
   KURL url(path);
   if ( url.isLocalFile() || ! url.isValid() )
   {
      return QDir().cleanDirPath( path );
   }
   else
   {
      return path;
   }
}

bool FileAccess::createBackup( const QString& bakExtension )
{
   if ( exists() )
   {
      // First rename the existing file to the bak-file. If a bak-file file exists, delete that.
      QString bakName = absFilePath() + bakExtension;
      FileAccess bakFile( bakName, true /*bWantToWrite*/ );
      if ( bakFile.exists() )
      {
         bool bSuccess = bakFile.removeFile();
         if ( !bSuccess )
         {
            m_statusText = i18n("While trying to make a backup, deleting an older backup failed. \nFilename: ") + bakName;
            return false;
         }
      }
      bool bSuccess = rename( bakName );
      if (!bSuccess)
      {
         m_statusText = i18n("While trying to make a backup, renaming failed. \nFilenames: ") +
               absFilePath() + " -> " + bakName;
         return false;
      }
   }
   return true;
}

FileAccessJobHandler::FileAccessJobHandler( FileAccess* pFileAccess )
{
   m_pFileAccess = pFileAccess;
   m_bSuccess = false;
}

bool FileAccessJobHandler::stat( int detail, bool bWantToWrite )
{
   m_bSuccess = false;
   m_pFileAccess->m_statusText = QString();
   KIO::StatJob* pStatJob = KIO::stat( m_pFileAccess->m_url, ! bWantToWrite, detail, false );

   connect( pStatJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotStatResult(KIO::Job*)));

   g_pProgressDialog->enterEventLoop();

   return m_bSuccess;
}

void FileAccessJobHandler::slotStatResult(KIO::Job* pJob)
{
   if ( pJob->error() )
   {
      //pJob->showErrorDialog(g_pProgressDialog);
      m_pFileAccess->m_bExists = false;
      m_bSuccess = true;
   }
   else
   {
      m_bSuccess = true;

      m_pFileAccess->m_bValidData = true;
      const KIO::UDSEntry e = static_cast<KIO::StatJob*>(pJob)->statResult();

      m_pFileAccess->setUdsEntry( e );
   }

   g_pProgressDialog->exitEventLoop();
}


bool FileAccessJobHandler::get(void* pDestBuffer, long maxLength )
{
   if ( maxLength>0 )
   {
      KIO::TransferJob* pJob = KIO::get( m_pFileAccess->m_url, false /*reload*/, false );
      m_transferredBytes = 0;
      m_pTransferBuffer = (char*)pDestBuffer;
      m_maxLength = maxLength;
      m_bSuccess = false;
      m_pFileAccess->m_statusText = QString();

      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotSimpleJobResult(KIO::Job*)));
      connect( pJob, SIGNAL(data(KIO::Job*,const QByteArray &)), this, SLOT(slotGetData(KIO::Job*, const QByteArray&)));
      connect( pJob, SIGNAL(percent(KIO::Job*,unsigned long)), this, SLOT(slotPercent(KIO::Job*, unsigned long)));

      g_pProgressDialog->enterEventLoop();
      return m_bSuccess;
   }
   else
      return true;
}

void FileAccessJobHandler::slotGetData( KIO::Job* pJob, const QByteArray& newData )
{
   if ( pJob->error() )
   {
      pJob->showErrorDialog(g_pProgressDialog);
   }
   else
   {
      long length = min2( long(newData.size()), m_maxLength - m_transferredBytes );
      ::memcpy( m_pTransferBuffer + m_transferredBytes, newData.data(), newData.size() );
      m_transferredBytes += length;
   }
}

bool FileAccessJobHandler::put(void* pSrcBuffer, long maxLength, bool bOverwrite, bool bResume, int permissions )
{
   if ( maxLength>0 )
   {
      KIO::TransferJob* pJob = KIO::put( m_pFileAccess->m_url, permissions, bOverwrite, bResume, false );
      m_transferredBytes = 0;
      m_pTransferBuffer = (char*)pSrcBuffer;
      m_maxLength = maxLength;
      m_bSuccess = false;
      m_pFileAccess->m_statusText = QString();

      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotPutJobResult(KIO::Job*)));
      connect( pJob, SIGNAL(dataReq(KIO::Job*, QByteArray&)), this, SLOT(slotPutData(KIO::Job*, QByteArray&)));
      connect( pJob, SIGNAL(percent(KIO::Job*,unsigned long)), this, SLOT(slotPercent(KIO::Job*, unsigned long)));

      g_pProgressDialog->enterEventLoop();
      return m_bSuccess;
   }
   else
      return true;
}

void FileAccessJobHandler::slotPutData( KIO::Job* pJob, QByteArray& data )
{
   if ( pJob->error() )
   {
      pJob->showErrorDialog(g_pProgressDialog);
   }
   else
   {
      long maxChunkSize = 100000;
      long length = min2( maxChunkSize, m_maxLength - m_transferredBytes );
      bool bSuccess = data.resize( length );
      if ( bSuccess )
      {
         if ( length>0 )
         {
            ::memcpy( data.data(), m_pTransferBuffer + m_transferredBytes, data.size() );
            m_transferredBytes += length;
         }
      }
      else
      {
         KMessageBox::error( g_pProgressDialog, i18n("Out of memory") );
         data.resize(0);
         m_bSuccess = false;
      }
   }
}

void FileAccessJobHandler::slotPutJobResult(KIO::Job* pJob)
{
   if ( pJob->error() )
   {
      pJob->showErrorDialog(g_pProgressDialog);
   }
   else
   {
      m_bSuccess = (m_transferredBytes == m_maxLength); // Special success condition
   }
   g_pProgressDialog->exitEventLoop();  // Close the dialog, return from exec()
}

bool FileAccessJobHandler::mkDir( const QString& dirName )
{
   KURL dirURL = KURL::fromPathOrURL( dirName );
   if ( dirName.isEmpty() )
      return false;
   else if ( dirURL.isLocalFile() )
   {
      return QDir().mkdir( dirURL.path() );
   }
   else
   {
      m_bSuccess = false;
      KIO::SimpleJob* pJob = KIO::mkdir( dirURL );
      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotSimpleJobResult(KIO::Job*)));

      g_pProgressDialog->enterEventLoop();
      return m_bSuccess;
   }
}

bool FileAccessJobHandler::rmDir( const QString& dirName )
{
   KURL dirURL = KURL::fromPathOrURL( dirName );
   if ( dirName.isEmpty() )
      return false;
   else if ( dirURL.isLocalFile() )
   {
      return QDir().rmdir( dirURL.path() );
   }
   else
   {
      m_bSuccess = false;
      KIO::SimpleJob* pJob = KIO::rmdir( dirURL );
      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotSimpleJobResult(KIO::Job*)));

      g_pProgressDialog->enterEventLoop();
      return m_bSuccess;
   }
}

bool FileAccessJobHandler::removeFile( const QString& fileName )
{
   if ( fileName.isEmpty() )
      return false;
   else
   {
      m_bSuccess = false;
      KIO::SimpleJob* pJob = KIO::file_delete( fileName, false );
      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotSimpleJobResult(KIO::Job*)));

      g_pProgressDialog->enterEventLoop();
      return m_bSuccess;
   }
}

bool FileAccessJobHandler::symLink( const QString& linkTarget, const QString& linkLocation )
{
   if ( linkTarget.isEmpty() || linkLocation.isEmpty() )
      return false;
   else
   {
      m_bSuccess = false;
      KIO::CopyJob* pJob = KIO::link( linkTarget, linkLocation, false );
      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotSimpleJobResult(KIO::Job*)));

      g_pProgressDialog->enterEventLoop();
      return m_bSuccess;
   }
}

bool FileAccessJobHandler::rename( const QString& dest )
{
   KURL kurl = KURL::fromPathOrURL( dest );
   if ( dest.isEmpty() )
      return false;
   else if ( m_pFileAccess->isLocal() && kurl.isLocalFile() )
   {
      return QDir().rename( m_pFileAccess->absFilePath(), kurl.path() );
   }
   else
   {
      bool bOverwrite = false;
      bool bResume = false;
      bool bShowProgress = false;
      int permissions=-1;
      m_bSuccess = false;
      KIO::FileCopyJob* pJob = KIO::file_move( m_pFileAccess->m_url, kurl.url(), permissions, bOverwrite, bResume, bShowProgress );
      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotSimpleJobResult(KIO::Job*)));
      connect( pJob, SIGNAL(percent(KIO::Job*,unsigned long)), this, SLOT(slotPercent(KIO::Job*, unsigned long)));

      g_pProgressDialog->enterEventLoop();
      return m_bSuccess;
   }
}

void FileAccessJobHandler::slotSimpleJobResult(KIO::Job* pJob)
{
   if ( pJob->error() )
   {
      pJob->showErrorDialog(g_pProgressDialog);
   }
   else
   {
      m_bSuccess = true;
   }
   g_pProgressDialog->exitEventLoop();  // Close the dialog, return from exec()
}


// Copy local or remote files.
bool FileAccessJobHandler::copyFile( const QString& dest )
{
   KURL destUrl = KURL::fromPathOrURL( dest );
   m_pFileAccess->m_statusText = QString();
   if ( ! m_pFileAccess->isLocal() || ! destUrl.isLocalFile() ) // if either url is nonlocal
   {
      bool bOverwrite = false;
      bool bResume = false;
      bool bShowProgress = false;
      int permissions = (m_pFileAccess->isExecutable()?0111:0)+(m_pFileAccess->isWritable()?0222:0)+(m_pFileAccess->isReadable()?0444:0);
      m_bSuccess = false;
      KIO::FileCopyJob* pJob = KIO::file_copy ( m_pFileAccess->m_url, destUrl.url(), permissions, bOverwrite, bResume, bShowProgress );
      connect( pJob, SIGNAL(result(KIO::Job*)), this, SLOT(slotSimpleJobResult(KIO::Job*)));
      connect( pJob, SIGNAL(percent(KIO::Job*,unsigned long)), this, SLOT(slotPercent(KIO::Job*, unsigned long)));
      g_pProgressDialog->enterEventLoop();

      return m_bSuccess;
      // Note that the KIO-slave preserves the original date, if this is supported.
   }

   // Both files are local:
   QString srcName = m_pFileAccess->absFilePath();
   QString destName = dest;
   QFile srcFile( srcName );
   QFile destFile( destName );
   bool bReadSuccess = srcFile.open( IO_ReadOnly );
   if ( bReadSuccess == false )
   {
      m_pFileAccess->m_statusText = "Error during file copy operation: Opening file for reading failed. Filename: " + srcName;
      return false;
   }
   bool bWriteSuccess = destFile.open( IO_WriteOnly );
   if ( bWriteSuccess == false )
   {
      m_pFileAccess->m_statusText = "Error during file copy operation: Opening file for writing failed. Filename: " + destName;
      return false;
   }

#if QT_VERSION==230
   typedef long Q_LONG;
#endif
   std::vector<char> buffer(100000);
   Q_LONG bufSize = buffer.size();
   Q_LONG srcSize = srcFile.size();
   while ( srcSize > 0 )
   {
      Q_LONG readSize = srcFile.readBlock( &buffer[0], min2( srcSize, bufSize ) );
      if ( readSize==-1 )
      {
         m_pFileAccess->m_statusText = "Error during file copy operation: Reading failed. Filename: "+srcName;
         return false;
      }
      srcSize -= readSize;
      while ( readSize > 0 )
      {
         Q_LONG writeSize = destFile.writeBlock( &buffer[0], readSize );
         if ( writeSize==-1 )
         {
            m_pFileAccess->m_statusText = "Error during file copy operation: Writing failed. Filename: "+destName;
            return false;
         }
         readSize -= writeSize;
      }
      destFile.flush();
   }
   srcFile.close();
   destFile.close();

   // Update the times of the destFile
#ifdef _WIN32
   struct _stat srcFileStatus;
   int statResult = ::_stat( srcName.ascii(), &srcFileStatus );
   if (statResult==0)
   {
      _utimbuf destTimes;
      destTimes.actime = srcFileStatus.st_atime;/* time of last access */
      destTimes.modtime = srcFileStatus.st_mtime;/* time of last modification */

      _utime ( destName.ascii(), &destTimes );
      _chmod ( destName.ascii(), srcFileStatus.st_mode );
   }
#else
   struct stat srcFileStatus;
   int statResult = ::stat( srcName.ascii(), &srcFileStatus );
   if (statResult==0)
   {
      utimbuf destTimes;
      destTimes.actime = srcFileStatus.st_atime;/* time of last access */
      destTimes.modtime = srcFileStatus.st_mtime;/* time of last modification */

      utime ( destName.ascii(), &destTimes );
      chmod ( destName.ascii(), srcFileStatus.st_mode );
   }
#endif
   return true;
}

static bool wildcardMultiMatch( const QString& wildcard, const QString& testString, bool bCaseSensitive )
{
   QStringList sl = QStringList::split( ";", wildcard );

   for ( QStringList::Iterator it = sl.begin(); it != sl.end(); ++it )
   {
      QRegExp pattern( *it, bCaseSensitive, true /*wildcard mode*/);
#if QT_VERSION==230
      int len=0;
      if ( pattern.match( testString, 0, &len )!=-1 && len==testString.length())
         return true;
#else
      if ( pattern.exactMatch( testString ) )
         return true;
#endif
   }

   return false;
}


// class CvsIgnoreList from Cervisia cvsdir.cpp
//    Copyright (C) 1999-2002 Bernd Gehrmann <bernd@mail.berlios.de>
// with elements from class StringMatcher
//    Copyright (c) 2003 Andr� W�bbeking <Woebbeking@web.de>
// Modifications for KDiff3 by Joachim Eibl
class CvsIgnoreList
{
public:
    CvsIgnoreList(){}
    void init(FileAccess& dir, bool bUseLocalCvsIgnore );
    bool matches(const QString& fileName) const;

private:
    void addEntriesFromString(const QString& str);
    void addEntriesFromFile(const QString& name);
    void addEntry(const QString& entry);

    QStringList m_exactPatterns;
    QStringList m_startPatterns;
    QStringList m_endPatterns;
    QStringList m_generalPatterns;
};


void CvsIgnoreList::init( FileAccess& dir, bool bUseLocalCvsIgnore )
{
   static const char *ignorestr = ". .. core RCSLOG tags TAGS RCS SCCS .make.state "
           ".nse_depinfo #* .#* cvslog.* ,* CVS CVS.adm .del-* *.a *.olb *.o *.obj "
           "*.so *.Z *~ *.old *.elc *.ln *.bak *.BAK *.orig *.rej *.exe _$* *$";

   addEntriesFromString(QString::fromLatin1(ignorestr));
   addEntriesFromFile(QDir::homeDirPath() + "/.cvsignore");
   addEntriesFromString(QString::fromLocal8Bit(::getenv("CVSIGNORE")));

   if (bUseLocalCvsIgnore)
   {
      FileAccess file(dir);
      file.addPath( ".cvsignore" );
      int size = file.exists() ? file.sizeForReading() : 0;
      if ( size>0 )
      {
         char* buf=new char[size];
         if (buf!=0)
         {
            file.readFile( buf, size );
            int pos1 = 0;
            for ( int pos = 0; pos<=size; ++pos )
            {
               if( pos==size || buf[pos]==' ' || buf[pos]=='\t' || buf[pos]=='\n' || buf[pos]=='\r' )
               {
                  if (pos>pos1)
                  {
                     QCString entry( &buf[pos1], pos-pos1+1 );
                     addEntry( entry );
                  }
                  pos1=pos+1;
               }
            }
            delete buf;
         }
      }
   }
}


void CvsIgnoreList::addEntriesFromString(const QString& str)
{
    int posLast(0);
    int pos;
    while ((pos = str.find(' ', posLast)) >= 0)
    {
        if (pos > posLast)
            addEntry(str.mid(posLast, pos - posLast));
        posLast = pos + 1;
    }

    if (posLast < static_cast<int>(str.length()))
        addEntry(str.mid(posLast));
}


void CvsIgnoreList::addEntriesFromFile(const QString &name)
{
    QFile file(name);

    if( file.open(IO_ReadOnly) )
    {
        QTextStream stream(&file);
        while( !stream.eof() )
        {
            addEntriesFromString(stream.readLine());
        }
    }
}

void CvsIgnoreList::addEntry(const QString& pattern)
{
   if (pattern != QChar('!'))
   {
      if (pattern.isEmpty())    return;

      // The general match is general but slow.
      // Special tests for '*' and '?' at the beginning or end of a pattern
      // allow fast checks.

      // Count number of '*' and '?'
      unsigned int nofMetaCharacters = 0;

      const QChar* pos;
      pos = pattern.unicode();
      const QChar* posEnd;
      posEnd=pos + pattern.length();
      while (pos < posEnd)
      {
         if( *pos==QChar('*') || *pos==QChar('?') )  ++nofMetaCharacters;
         ++pos;
      }

      if ( nofMetaCharacters==0 )
      {
         m_exactPatterns.append(pattern);
      }
      else if ( nofMetaCharacters==1 )
      {
         if ( pattern.constref(0) == QChar('*') )
         {
            m_endPatterns.append( pattern.right( pattern.length() - 1) );
         }
         else if (pattern.constref(pattern.length() - 1) == QChar('*'))
         {
            m_startPatterns.append( pattern.left( pattern.length() - 1) );
         }
         else
         {
            m_generalPatterns.append(pattern.local8Bit());
         }
      }
      else
      {
         m_generalPatterns.append(pattern.local8Bit());
      }
   }
   else
   {
      m_exactPatterns.clear();
      m_startPatterns.clear();
      m_endPatterns.clear();
      m_generalPatterns.clear();
   }
}

bool CvsIgnoreList::matches(const QString& text) const
{
    if (m_exactPatterns.find(text) != m_exactPatterns.end())
    {
        return true;
    }

    QStringList::ConstIterator it;
    QStringList::ConstIterator itEnd;
    for ( it=m_startPatterns.begin(), itEnd=m_startPatterns.end(); it != itEnd; ++it)
    {
        if (text.startsWith(*it))
        {
            return true;
        }
    }

    for ( it = m_endPatterns.begin(), itEnd=m_endPatterns.end(); it != itEnd; ++it)
    {
        if (text.mid( text.length() - (*it).length() )==*it)  //(text.endsWith(*it))
        {
            return true;
        }
    }

    /*
    for (QValueList<QCString>::const_iterator it(m_generalPatterns.begin()),
                                              itEnd(m_generalPatterns.end());
         it != itEnd; ++it)
    {
        if (::fnmatch(*it, text.local8Bit(), FNM_PATHNAME) == 0)
        {
            return true;
        }
    }
    */


   for ( it = m_generalPatterns.begin(); it != m_generalPatterns.end(); ++it )
   {
      QRegExp pattern( *it, true /*CaseSensitive*/, true /*wildcard mode*/);
#if QT_VERSION==230
      int len=0;
      if ( pattern.match( text, 0, &len )!=-1 && len==text.length())
         return true;
#else
      if ( pattern.exactMatch( text ) )
         return true;
#endif
   }

   return false;
}

static QString nicePath( const QFileInfo& fi )
{
   QString fp = fi.filePath();
   if ( fp.length()>2 && fp[0] == '.' && fp[1] == '/' )
   {
      return fp.mid(2);
   }
   return fp;
}

static bool cvsIgnoreExists( t_DirectoryList* pDirList )
{
   t_DirectoryList::iterator i;
   for( i = pDirList->begin(); i!=pDirList->end(); ++i )
   {
      if ( i->fileName()==".cvsignore" )
         return true;
   }
   return false;
}

bool FileAccessJobHandler::listDir( t_DirectoryList* pDirList, bool bRecursive, bool bFindHidden, const QString& filePattern,
   const QString& fileAntiPattern, const QString& dirAntiPattern, bool bFollowDirLinks, bool bUseCvsIgnore )
{
   m_pDirList = pDirList;
   m_pDirList->clear();
   m_bFindHidden = bFindHidden;
   m_bRecursive = bRecursive;
   m_bFollowDirLinks = bFollowDirLinks;  // Only relevant if bRecursive==true.
   m_fileAntiPattern = fileAntiPattern;
   m_filePattern = filePattern;
   m_dirAntiPattern = dirAntiPattern;

   if ( g_pProgressDialog->wasCancelled() )
      return true; // Cancelled is not an error.

   g_pProgressDialog->setSubInformation( i18n("Reading directory: ") + m_pFileAccess->absFilePath(), 0, false );

   if( m_pFileAccess->isLocal() )
   {
      m_bSuccess = QDir::setCurrent( m_pFileAccess->absFilePath() );
      if ( m_bSuccess )
      {
         m_bSuccess = true;
         QDir dir( "." );

         dir.setSorting( QDir::Name | QDir::DirsFirst );
         dir.setFilter( QDir::Files | QDir::Dirs | QDir::Hidden );
         dir.setMatchAllDirs( true );

         const QFileInfoList *fiList = dir.entryInfoList();
         if ( fiList == 0 )
         {
            // No Permission to read directory or other error.
            m_bSuccess = false;
         }
         else
         {
            QFileInfoListIterator it( *fiList );      // create list iterator
            for ( ; it.current() != 0; ++it )       // for each file...
            {
               QFileInfo* fi = it.current();
               if ( fi->fileName() == "." ||  fi->fileName()==".." )
                  continue;

               pDirList->push_back( FileAccess( nicePath(*fi) ) );
            }
         }
      }
   }
   else
   {
      bool bShowProgress = false;

      KIO::ListJob* pListJob=0;
      pListJob = KIO::listDir( m_pFileAccess->m_url, bShowProgress, true /*bFindHidden*/ );

      m_bSuccess = false;
      if ( pListJob!=0 )
      {
         connect( pListJob, SIGNAL( entries( KIO::Job *, const KIO::UDSEntryList& ) ),
                  this,     SLOT( slotListDirProcessNewEntries( KIO::Job *, const KIO::UDSEntryList& )) );
         connect( pListJob, SIGNAL( result( KIO::Job* )),
                  this,     SLOT( slotSimpleJobResult(KIO::Job*) ) );

         connect( pListJob, SIGNAL( infoMessage(KIO::Job*, const QString&)),
                  this,     SLOT( slotListDirInfoMessage(KIO::Job*, const QString&) ));

         // This line makes the transfer via fish unreliable.:-(
         //connect( pListJob, SIGNAL(percent(KIO::Job*,unsigned long)), this, SLOT(slotPercent(KIO::Job*, unsigned long)));

         g_pProgressDialog->enterEventLoop();
      }
   }

   CvsIgnoreList cvsIgnoreList;
   if ( bUseCvsIgnore )
   {
      cvsIgnoreList.init( *m_pFileAccess, cvsIgnoreExists(pDirList) );
   }

   // Now remove all entries that don't match:
   t_DirectoryList::iterator i;
   for( i = pDirList->begin(); i!=pDirList->end();  )
   {
      t_DirectoryList::iterator i2=i;
      ++i2;
      QString fn = i->fileName();
      if (  (!bFindHidden && i->isHidden() )
            ||
            (i->isFile() &&
               ( !wildcardMultiMatch( filePattern, i->fileName(), true ) ||
                 wildcardMultiMatch( fileAntiPattern, i->fileName(), true ) ) )
            ||
            (i->isDir() && wildcardMultiMatch( dirAntiPattern, i->fileName(), true ) )
            ||
            cvsIgnoreList.matches(i->fileName())
         )
      {
         // Remove it
         pDirList->erase( i );
         i = i2;
      }
      else
      {
         ++i;
      }
   }

   if ( bRecursive )
   {
      t_DirectoryList subDirsList;

      t_DirectoryList::iterator i;
      for( i = m_pDirList->begin(); i!=m_pDirList->end(); ++i )
      {
         if  ( i->isDir() && (!i->isSymLink() || m_bFollowDirLinks))
         {
            t_DirectoryList dirList;
            i->listDir( &dirList, bRecursive, bFindHidden,
               filePattern, fileAntiPattern, dirAntiPattern, bFollowDirLinks, bUseCvsIgnore );

            t_DirectoryList::iterator j;
            for( j = dirList.begin(); j!=dirList.end(); ++j )
            {
               j->m_path = i->fileName() + "/" + j->m_path;
            }

            // append data onto the main list
            subDirsList.splice( subDirsList.end(), dirList );
         }
      }

      m_pDirList->splice( m_pDirList->end(), subDirsList );
   }

   return m_bSuccess;
}


// Return value false means that the directory or some subdirectories
// were not readable. Probably because of missing permissions.
bool FileAccessJobHandler::scanLocalDirectory( const QString& dirName, t_DirectoryList* pDirList )
{
   bool bSuccess = true;
   QDir dir( dirName );
   g_pProgressDialog->setInformation( "Scanning directory: " + dirName, 0, false );

   // First search subdirectorys
   bool bHidden =  m_bFindHidden;
   dir.setSorting( QDir::Name | QDir::DirsFirst );
   dir.setFilter( QDir::Dirs | (bHidden ? QDir::Hidden : 0) );
   dir.setMatchAllDirs( true );

   const QFileInfoList *fiList = dir.entryInfoList();
   if ( fiList == 0 )
   {
      // No Permission to read directory or other error.
      return false;
   }

   QFileInfoListIterator it( *fiList );      // create list iterator

   for ( ; it.current() != 0; ++it )       // for each file...
   {
      if ( g_pProgressDialog->wasCancelled() )
         return true;

      QFileInfo *fi = it.current();
      if ( fi->isDir() )
      {
         if ( fi->fileName() == "." ||  fi->fileName()==".." ||
            wildcardMultiMatch( m_dirAntiPattern, fi->fileName(), true/*case sensitive*/ ) )
            continue;
         else
         {
            if ( m_bRecursive )
               if ( ! fi->isSymLink()  ||  m_bFollowDirLinks  )
               {
                  bool bLocalSuccess = scanLocalDirectory( fi->filePath(), pDirList );
                  if ( ! bLocalSuccess )
                     bSuccess = false;
               }
         }
      }
   }

   dir.setFilter( QDir::Files | QDir::Dirs | (bHidden ? QDir::Hidden : 0) );
   dir.setMatchAllDirs( true );
   dir.setNameFilter( m_filePattern );

   fiList = dir.entryInfoList();
   it = *fiList;

   QString sizeString;

   for ( ; it.current() != 0; ++it )       // for each file...
   {
      QFileInfo* fi = it.current();

      if ( fi->fileName() == "." ||  fi->fileName()==".."  ||
           wildcardMultiMatch( fi->isDir() ? m_dirAntiPattern : m_fileAntiPattern, fi->fileName(), true/*case sensitive*/ ) )
         continue;

      pDirList->push_back( FileAccess( nicePath(*fi) ) );
   }
   return bSuccess;
}

void FileAccessJobHandler::slotListDirProcessNewEntries( KIO::Job *, const KIO::UDSEntryList& l )
{
   KURL parentUrl( m_pFileAccess->m_absFilePath );

   KIO::UDSEntryList::ConstIterator i;
   for ( i=l.begin(); i!=l.end(); ++i )
   {
      const KIO::UDSEntry& e = *i;
      FileAccess fa;
      fa.setUdsEntry( e );

      if ( fa.filePath() != "." && fa.filePath() != ".." )
      {
         fa.m_url = parentUrl;
         fa.m_url.addPath( fa.filePath() );
         fa.m_absFilePath = fa.m_url.url();
         m_pDirList->push_back( fa );
      }
   }
}

void FileAccessJobHandler::slotListDirInfoMessage( KIO::Job*, const QString& msg )
{
   g_pProgressDialog->setSubInformation( msg, 0 );
}

void FileAccessJobHandler::slotPercent( KIO::Job*, unsigned long percent )
{
   g_pProgressDialog->setSubCurrent( percent/100.0 );
}


ProgressDialog::ProgressDialog( QWidget* pParent )
: QDialog( pParent, 0, true )
{
   QVBoxLayout* layout = new QVBoxLayout(this);

   m_pInformation = new QLabel( " ", this );
   layout->addWidget( m_pInformation );

   m_pProgressBar = new KProgress(1000, this);
   layout->addWidget( m_pProgressBar );

   m_pSubInformation = new QLabel( " ", this);
   layout->addWidget( m_pSubInformation );

   m_pSubProgressBar = new KProgress(1000, this);
   layout->addWidget( m_pSubProgressBar );

   m_dCurrent = 0.0;
   m_dSubMax = 1.0;
   m_dSubMin = 0.0;
   m_dSubCurrent = 0.0;
   resize( 400, 100 );
   m_t1.start();
   m_t2.start();
   m_bWasCancelled = false;
}


void ProgressDialog::setInformation(const QString& info, double dCurrent, bool bRedrawUpdate )
{
   m_pInformation->setText( info );
   m_dCurrent = dCurrent;
   m_dSubCurrent=0;
   m_dSubMin = 0;
   m_dSubMax = 1;
   m_pSubInformation->setText("");
   recalc(bRedrawUpdate);
}

void ProgressDialog::setInformation(const QString& info, bool bRedrawUpdate )
{
   m_pInformation->setText( info );
   m_dSubCurrent = 0;
   m_dSubMin = 0;
   m_dSubMax = 1;
   m_pSubInformation->setText("");
   recalc(bRedrawUpdate);
}

void ProgressDialog::setMaximum( int maximum )
{
   m_maximum = maximum;
   m_dCurrent = 0;
}

void ProgressDialog::step( bool bRedrawUpdate )
{
   m_dCurrent += 1.0/m_maximum;
   m_dSubCurrent=0;
   recalc(bRedrawUpdate);
}

void ProgressDialog::setSubInformation(const QString& info, double dSubCurrent, bool bRedrawUpdate )
{
   m_pSubInformation->setText(info);
   m_dSubCurrent = dSubCurrent;
   recalc(bRedrawUpdate);
}

void ProgressDialog::setSubCurrent( double dSubCurrent, bool bRedrawUpdate )
{
   m_dSubCurrent = dSubCurrent;
   recalc( bRedrawUpdate );
}


void qt_enter_modal(QWidget*);
void qt_leave_modal(QWidget*);

void ProgressDialog::enterEventLoop()
{
   // instead of using exec() the eventloop is entered and exited often without hiding/showing the window.
#if QT_VERSION==230
   //qApp->enter_loop();
#else
   qt_enter_modal(this);
   qApp->eventLoop()->enterLoop();
   qt_leave_modal(this);
#endif
}

void ProgressDialog::exitEventLoop()
{
#if QT_VERSION==230
   //qApp->exit_loop();
#else
   qApp->eventLoop()->exitLoop();
#endif
}

void ProgressDialog::recalc( bool bUpdate )
{
   if( (bUpdate && m_dSubCurrent == 0) || m_t1.elapsed()>200 )
   {
      m_pProgressBar->setProgress( int( 1000.0 * m_dCurrent ) );
      m_pSubProgressBar->setProgress( int( 1000.0 * ( m_dSubCurrent * (m_dSubMax - m_dSubMin) + m_dSubMin ) ) );
      if ( !isVisible() ) show();
      qApp->processEvents();
      m_t1.restart();
   }
}

void ProgressDialog::start()
{
   setInformation("",0, true);
   setSubInformation("",0);
   m_bWasCancelled = false;
   m_t1.restart();
   m_t2.restart();
   show();
}

#include <qtimer.h>
void ProgressDialog::show()
{
#if QT_VERSION==230
   QWidget::show();
#else
   QDialog::show();
#endif
}

void ProgressDialog::hide()
{
   // Calling QDialog::hide() directly doesn't always work. (?)
   QTimer::singleShot( 100, this, SLOT(delayedHide()) );
}

void ProgressDialog::delayedHide()
{
   QDialog::hide();
}

void ProgressDialog::reject()
{
   m_bWasCancelled = true;
   QDialog::reject();
}

bool ProgressDialog::wasCancelled()
{
   if( m_t2.elapsed()>100 )
   {
      qApp->processEvents();
      m_t2.restart();
   }
   return m_bWasCancelled;
}

// The progressbar goes from 0 to 1 usually.
// By supplying a subrange transformation the subCurrent-values
// 0 to 1 will be transformed to dMin to dMax instead.
// Requirement: 0 < dMin < dMax < 1
void ProgressDialog::setSubRangeTransformation( double dMin, double dMax )
{
   m_dSubMin = dMin;
   m_dSubMax = dMax;
   m_dSubCurrent = 0;
}


#include "fileaccess.moc"