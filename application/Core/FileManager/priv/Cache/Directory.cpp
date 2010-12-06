#include <priv/Cache/Directory.h>
using namespace FM;

#include <QDir>

#include <Common/ProtoHelper.h>

#include <priv/Constants.h>
#include <priv/Log.h>
#include <priv/FileManager.h>
#include <priv/Cache/File.h>
#include <priv/Cache/SharedDirectory.h>

Directory::Directory(Directory* parent, const QString& name, bool createPhysically)
   : Entry(parent->cache, name), parent(parent), mutex(QMutex::Recursive)
{
   QMutexLocker locker(&this->mutex);
   L_DEBU(QString("New Directory : %1, createPhysically = %2").arg(this->getFullPath()).arg(createPhysically));

   if (createPhysically)
      if (!QDir(this->parent->getFullPath()).mkdir(this->name))
         L_ERRO(QString("Unable to create the directory : %1").arg(this->getFullPath()));

   this->parent->append(this);
}

/**
  * Called by the root (SharedDirectory) which will not have parent and name.
  */
Directory::Directory(Cache* cache, const QString& name)
   : Entry(cache, name), parent(0), mutex(QMutex::Recursive)
{
}

Directory::~Directory()
{
   QMutexLocker locker(&this->mutex);

   foreach (File* f, this->files)
      delete f;
   foreach (Directory* d, this->subDirs)
      delete d;

   if (this->parent)
      this->parent->subDirDeleted(this);

   L_DEBU(QString("Directory deleted : %1").arg(this->getFullPath()));
}

/**
  * Retore the hashes from the cache.
  * All file which are not complete and not in the cache are physically removed.
  * Only files ending with the setting "unfinished_suffix_term" will be removed.
  * @return The files which have all theirs hashes (complete).
  */
QList<File*> Directory::restoreFromFileCache(const Protos::FileCache::Hashes_Dir& dir)
{
   QMutexLocker locker(&this->mutex);

   QList<File*> ret;

   if (Common::ProtoHelper::getStr(dir, &Protos::FileCache::Hashes_Dir::name) == this->getName())
   {
      // Sub directories..
      for (int i = 0; i < dir.dir_size(); i++)
         for (QListIterator<Directory*> d(this->subDirs); d.hasNext();)
            ret << d.next()->restoreFromFileCache(dir.dir(i));

      // .. And files.
      for (int i = 0; i < dir.file_size(); i++)
         for (QListIterator<File*> j(this->files); j.hasNext();)
         {
            File* f = j.next();
            if (f->restoreFromFileCache(dir.file(i)) && f->hasAllHashes())
               ret << f;
         }
   }

   return ret;
}

void Directory::populateHashesDir(Protos::FileCache::Hashes_Dir& dirToFill) const
{
   QList<Directory*> subDirsCopy;
   QList<File*> filesCopy;

   {
      QMutexLocker locker(&this->mutex);
      Common::ProtoHelper::setStr(dirToFill, &Protos::FileCache::Hashes_Dir::set_name, this->getName());
      subDirsCopy = this->subDirs;
      filesCopy = this->files;
   }

   for (QListIterator<File*> i(filesCopy); i.hasNext();)
   {
      File* f = i.next();

      if (f->hasOneOrMoreHashes())
      {
         Protos::FileCache::Hashes_File* file = dirToFill.add_file();
         f->populateHashesFile(*file);
      }
   }

   for (QListIterator<Directory*> dir(subDirsCopy); dir.hasNext();)
   {
      dir.next()->populateHashesDir(*dirToFill.add_dir());
   }
}

void Directory::populateEntry(Protos::Common::Entry* dir, bool setSharedDir) const
{
   QMutexLocker locker(&this->mutex);

   Entry::populateEntry(dir, setSharedDir);
   dir->set_is_empty(this->subDirs.isEmpty() && this->files.isEmpty());
   dir->set_type(Protos::Common::Entry_Type_DIR);
}

/**
  * Remove recursively all incomplete files which don't have all theirs hashes. The file is physically removed.
  */
void Directory::removeIncompleteFiles()
{
   QMutexLocker locker(&this->mutex);

   // Removes incomplete file we don't know.
   foreach (File* f, this->files)
      if (!f->isComplete() && !f->hasAllHashes())
         delete f;

   foreach (Directory* d, this->subDirs)
      d->removeIncompleteFiles();
}

/**
  * Called from one of its file.
  */
void Directory::fileDeleted(File* file)
{
   L_DEBU(QString("Directory::fileDeleted() remove %1").arg(file->getFullPath()));

   (*this) -= file->getSize();
   this->files.removeOne(file);
}

void Directory::subDirDeleted(Directory* dir)
{
   QMutexLocker locker(&this->mutex);
   this->subDirs.removeOne(dir);
}

QString Directory::getPath() const
{
   QString path('/');

   const Directory* dir = this;
   while (dir->parent && dir->parent->parent) // We don't care about the name of the root (SharedDirectory).
   {
      dir = dir->parent;
      path.prepend(dir->getName());
      path.prepend('/');
   }
   return path;
}

QString Directory::getFullPath() const
{
   // In case of a partially constructed ShareDirectory.
   // (When a exception is thrown from the SharedDirectory ctor).
   if (!this->parent)
      return this->getName();

   return this->parent->getFullPath().append('/').append(this->getName());
}

Directory* Directory::getRoot() const
{
   if (this->parent)
      return this->parent->getRoot();
   return const_cast<Directory*>(this);
}

bool Directory::isAChildOf(const Directory* dir) const
{
   if (this->parent)
   {
      if (this->parent == dir)
         return true;
      else
         return this->parent->isAChildOf(dir);
   }
   return false;
}

/**
  * @return Returns 0 if no one match.
  */
Directory* Directory::getSubDir(const QString& name) const
{
   QMutexLocker locker(&this->mutex);

   foreach (Directory* d, this->subDirs)
      if (d->getName() == name)
         return d;

   return 0;
}

QList<Directory*> Directory::getSubDirs() const
{
   QMutexLocker locker(&this->mutex);
   // TODO : it create a deadlock, rethink serously about the concurency problems ..
   // - main thread (MT) : setSharedDirsReadOnly(..) with a super shared directory -> Cache::lock
   // - FileUpdater thread (FT) : Scan some directories and be locked by the call currentDir->getSubDirs() -> Cache::lock;
   // - (MT) : SharedDirectory::init() call this->getCache()->removeSharedDir(subDir, current); and emit sharedDirectoryRemoved
   //          which will call FileUpdater::rmRoot which will try to stop scanning -> deadlock
   // QMutexLocker locker(&this->cache->getMutex());
   return this->subDirs;
}

QList<File*> Directory::getFiles() const
{
   QMutexLocker locker(&this->mutex);
   // TODO : it create a deadlock, rethink serously about the concurency problems ..
   // Same problem as above.
   // QMutexLocker locker(&this->cache->getMutex());
   return this->files;
}

QList<File*> Directory::getCompleteFiles() const
{
   QMutexLocker locker(&this->mutex);
   QList<File*> completeFiles;
   foreach (File* file, this->files)
   {
      if (file->isComplete())
         completeFiles << file;
   }
   return completeFiles;
}

/**
  * Creates a new sub-directory if none exists already otherwise
  * returns an already existing.
  */
Directory* Directory::createSubDirectory(const QString& name)
{
   QMutexLocker locker(&this->mutex);
   if (Directory* subDir = this->getSubDir(name))
      return subDir;
   return new Directory(this, name);
}

/**
  * Creates a new sub-directory if none exists already otherwise
  * returns an already existing.
  */
Directory* Directory::physicallyCreateSubDirectory(const QString& name)
{
   QMutexLocker locker(&this->mutex);
   if (Directory* subDir = this->getSubDir(name))
      return subDir;

   return new Directory(this, name, true);
}

File* Directory::getFile(const QString& name) const
{
   QMutexLocker locker(&this->mutex);
   foreach (File* f, this->files)
      if (f->getName() == name)
         return f;

   return 0;
}

/**
  * Only called by the class File.
  */
void Directory::addFile(File* file)
{
   QMutexLocker locker(&this->mutex);
   if (this->files.contains(file))
      return;
   this->files << file;

   (*this) += file->getSize();
}

void Directory::fileSizeChanged(qint64 oldSize, qint64 newSize)
{
   QMutexLocker locker(&this->mutex);
   (*this) += newSize - oldSize;
}

/**
  * Steal the sub directories and files from 'dir'.
  * The sub dirs and files will be removed from 'dir'.
  */
void Directory::stealContent(Directory* dir)
{
   QMutexLocker locker(&this->mutex);
   if (dir == this)
   {
      L_ERRO("Directory::stealSubDirs(..) : dir == this");
      return;
   }

   // L_DEBU(QString("this = %1, dir = %2").arg(this->getFullPath()).arg(dir->getFullPath()));

   this->subDirs.append(dir->subDirs);
   this->files.append(dir->files);

   foreach (Directory* d, dir->subDirs)
   {
      d->parent = this;
      (*this) += d->getSize();
      (*dir) -= d->getSize();
   }

   foreach (File* f, dir->files)
      f->changeDirectory(this);

   dir->subDirs.clear();
   dir->files.clear();
}

void Directory::append(Directory* dir)
{
   QMutexLocker locker(&this->mutex);
   this->subDirs << dir;
}

/**
  * When a new file is added to a directory this method is called
  * to add its size.
  */
Directory& Directory::operator+=(qint64 size)
{
   QMutexLocker locker(&this->mutex);
   this->size += size;
   if (this->parent)
      (*this->parent) += size;

   return *this;
}

Directory& Directory::operator-=(qint64 size)
{
   QMutexLocker locker(&this->mutex);
   this->size -= size;
   if (this->parent)
      (*this->parent) -= size;

   return *this;
}

/**
  * @class DirIterator
  * Iterate recursively over a directory tree structure.
  */

DirIterator::DirIterator(Directory* dir) :
   dirsToVisit(dir->subDirs)
{
}

/**
  * Return the next directory, 0 if there is no more directory.
  */
Directory* DirIterator::next()
{
   if (this->dirsToVisit.isEmpty())
      return 0;

   Directory* dir = this->dirsToVisit.front();
   this->dirsToVisit.removeFirst();
   this->dirsToVisit.append(dir->subDirs);
   return dir;
}

